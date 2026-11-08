#include "RenameOperationTests.h"
#include "Blink/App/RenameOperation.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

bool CreateOperationTestJunction(const std::filesystem::path&, const std::filesystem::path&);
namespace
{
    bool Check(bool condition, const char* name)
    { std::cout << (condition ? "PASS " : "FAIL ") << name << '\n'; return condition; }
    blink::core::FileEntry Entry(const std::filesystem::path& path)
    {
        blink::core::FileEntry entry;
        (void)blink::core::ReadEntryMetadata(path.wstring(), entry);
        entry.name = path.filename().wstring(); entry.full_path = path.wstring();
        return entry;
    }
    void Write(const std::filesystem::path& path) { std::ofstream file(path); file << "owned-source"; }
}
bool RunRenameOperationTests()
{
    using namespace blink::app;
    bool passed = true;
    const auto root = std::filesystem::absolute(std::filesystem::path(L".build") / L"rename-operation-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    std::filesystem::create_directories(root / L"parent");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path, e); } } cleanup{root};
    const auto parent = root / L"parent";
    const auto parent_id = Entry(parent).identity;
    Write(parent / L"源文件.txt");
    RenameRequest request{ parent.wstring(), parent_id, Entry(parent / L"源文件.txt"), L"新名称 {0}.txt" };
    auto result = RenameEntrySafely(request);
    passed &= Check(result.renamed && result.error == ERROR_SUCCESS && !std::filesystem::exists(parent / L"源文件.txt") &&
        Entry(parent / request.new_name).identity == request.source.identity && std::filesystem::file_size(parent / request.new_name) == 12,
        "rename_real_unicode_file_preserves_identity_and_contents");
    request.source = Entry(parent / request.new_name); request.new_name = request.source.name;
    result = RenameEntrySafely(request);
    passed &= Check(result.error == ERROR_SUCCESS && !result.renamed && Entry(parent / request.new_name).identity == request.source.identity,
        "rename_same_name_is_successful_no_op");
    Write(parent / L"occupied.txt");
    const auto destination_id = Entry(parent / L"occupied.txt").identity;
    request.new_name = L"occupied.txt"; result = RenameEntrySafely(request);
    passed &= Check(!result.renamed && result.error != ERROR_SUCCESS &&
        Entry(parent / L"occupied.txt").identity == destination_id && std::filesystem::exists(parent / request.source.name),
        "rename_existing_file_not_replaced_and_source_preserved");
    std::filesystem::create_directory(parent / L"occupied-dir");
    request.new_name = L"occupied-dir"; result = RenameEntrySafely(request);
    passed &= Check(!result.renamed && result.error != ERROR_SUCCESS && std::filesystem::is_directory(parent / L"occupied-dir") &&
        std::filesystem::exists(parent / request.source.name), "rename_existing_directory_not_replaced");
    request.new_name = L"must-not-change.txt";
    auto wrong = request; wrong.source.identity.file_id[0] ^= 1;
    result = RenameEntrySafely(wrong);
    passed &= Check(!result.renamed && result.error == ERROR_FILE_INVALID && !std::filesystem::exists(parent / request.new_name),
        "rename_wrong_source_identity_has_no_side_effect");
    wrong = request; wrong.parent_identity.file_id[0] ^= 1;
    passed &= Check(RenameEntrySafely(wrong).error == ERROR_FILE_INVALID, "rename_wrong_parent_identity_rejected");
    wrong = request; wrong.source.is_directory = true;
    passed &= Check(RenameEntrySafely(wrong).error == ERROR_FILE_INVALID, "rename_source_type_change_rejected");
    passed &= Check(RenameEntrySafely(request, std::make_shared<std::atomic_bool>(true)).error == ERROR_CANCELLED &&
        std::filesystem::exists(parent / request.source.name), "rename_cancel_before_commit_preserves_source");
    wrong = request; wrong.new_name = L"..\\escape";
    passed &= Check(RenameEntrySafely(wrong).error == ERROR_INVALID_NAME && !std::filesystem::exists(root / L"escape"),
        "rename_invalid_leaf_cannot_escape_parent");
    wrong = request; wrong.parent = L"\\\\server\\share";
    passed &= Check(RenameEntrySafely(wrong).error == ERROR_NOT_SUPPORTED, "rename_unc_parent_explicitly_unsupported");

    std::filesystem::create_directory(parent / L"目录"); Write(parent / L"目录" / L"child.txt");
    request = { parent.wstring(), parent_id, Entry(parent / L"目录"), L"新目录" };
    result = RenameEntrySafely(request);
    passed &= Check(result.renamed && !std::filesystem::exists(parent / L"目录") &&
        Entry(parent / L"新目录").identity == request.source.identity && std::filesystem::file_size(parent / L"新目录" / L"child.txt") == 12,
        "rename_directory_preserves_identity_and_nested_contents");
    Write(parent / L"link-a.txt");
    const bool linked = CreateHardLinkW((parent / L"link-b.txt").c_str(), (parent / L"link-a.txt").c_str(), nullptr) != FALSE;
    request = { parent.wstring(), parent_id, Entry(parent / L"link-a.txt"), L"link-c.txt" };
    result = RenameEntrySafely(request);
    passed &= Check(linked && result.renamed && !std::filesystem::exists(parent / L"link-a.txt") &&
        Entry(parent / L"link-b.txt").identity == request.source.identity && Entry(parent / L"link-c.txt").identity == request.source.identity,
        "rename_only_selected_hardlink_name_changes");
    Write(parent / L"lower.txt");
    request = { parent.wstring(), parent_id, Entry(parent / L"lower.txt"), L"LOWER.txt" };
    result = RenameEntrySafely(request);
    const auto listing = blink::core::EnumerateDirectory(parent.wstring());
    passed &= Check(result.renamed && std::any_of(listing.entries.begin(), listing.entries.end(), [&](const auto& row) {
        return row.name == L"LOWER.txt" && row.identity == request.source.identity;
    }), "rename_case_only_change_preserves_identity_and_updates_stored_name");
    Write(parent / L"stale.txt"); request = { parent.wstring(), parent_id, Entry(parent / L"stale.txt"), L"after-stale.txt" };
    std::filesystem::rename(parent / L"stale.txt", parent / L"original-stale.txt"); Write(parent / L"stale.txt");
    result = RenameEntrySafely(request);
    passed &= Check(!result.renamed && result.error == ERROR_FILE_INVALID && std::filesystem::exists(parent / L"stale.txt") &&
        std::filesystem::exists(parent / L"original-stale.txt") && !std::filesystem::exists(parent / request.new_name),
        "rename_replaced_source_is_not_renamed");
    std::filesystem::remove(parent / L"stale.txt");
    passed &= Check(RenameEntrySafely(request).error != ERROR_SUCCESS, "rename_deleted_source_fails_without_new_name");
    Write(parent / L"guarded.txt"); request = { parent.wstring(), parent_id, Entry(parent / L"guarded.txt"), L"after-guard.txt" };
    bool source_blocked{}, parent_blocked{};
    result = RenameEntrySafely(request, {}, [&](HANDLE, const std::wstring&, DWORD* error) {
        const bool moved = MoveFileExW((parent / L"guarded.txt").c_str(), (parent / L"unexpected.txt").c_str(), 0) != FALSE;
        if (moved) { (void)MoveFileExW((parent / L"unexpected.txt").c_str(), (parent / L"guarded.txt").c_str(), 0); }
        source_blocked = !moved;
        const bool moved_parent = MoveFileExW(parent.c_str(), (root / L"unexpected-parent").c_str(), 0) != FALSE;
        if (moved_parent) { (void)MoveFileExW((root / L"unexpected-parent").c_str(), parent.c_str(), 0); }
        parent_blocked = !moved_parent; *error = ERROR_ACCESS_DENIED; return false;
    });
    passed &= Check(source_blocked && parent_blocked && !result.renamed && result.error == ERROR_ACCESS_DENIED &&
        std::filesystem::exists(parent / L"guarded.txt"), "rename_source_and_parent_held_until_native_commit_returns");
    std::filesystem::create_directory(parent / L"target-dir"); Write(parent / L"target-dir" / L"owned.txt");
    const bool junction = CreateOperationTestJunction(parent / L"junction", parent / L"target-dir");
    request = { parent.wstring(), parent_id, Entry(parent / L"junction"), L"renamed-junction" };
    result = RenameEntrySafely(request);
    passed &= Check(junction && !result.renamed && result.error == ERROR_NOT_SUPPORTED &&
        std::filesystem::exists(parent / L"target-dir" / L"owned.txt") && std::filesystem::exists(parent / L"junction"),
        "rename_reparse_source_not_followed_or_renamed");
    request = { (parent / L"junction").wstring(), Entry(parent / L"target-dir").identity,
        Entry(parent / L"target-dir" / L"owned.txt"), L"must-not-follow.txt" };
    result = RenameEntrySafely(request);
    passed &= Check(!result.renamed && result.error == ERROR_NOT_SUPPORTED &&
        std::filesystem::exists(parent / L"target-dir" / L"owned.txt"), "rename_reparse_parent_not_followed");
    auto long_parent = parent;
    for (int index = 0; index < 6; ++index) { long_parent /= std::wstring(45, L'a') + std::to_wstring(index); }
    const auto extended = L"\\\\?\\" + long_parent.wstring();
    std::filesystem::create_directories(extended); Write(extended + L"\\source.txt");
    request = { extended, Entry(extended).identity, Entry(extended + L"\\source.txt"), L"长路径 {0}.txt" };
    result = RenameEntrySafely(request);
    passed &= Check(result.renamed && !std::filesystem::exists(extended + L"\\source.txt") &&
        Entry(extended + L"\\" + request.new_name).identity == request.source.identity,
        "rename_extended_long_path_preserves_identity");
    return passed;
}
