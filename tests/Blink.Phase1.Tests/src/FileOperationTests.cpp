#include "FileOperationTests.h"
#include "Blink/App/FileOperations.h"
#include "Blink/App/TabState.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <winioctl.h>
#include <algorithm>

namespace
{
    bool Check(bool condition, const char* name)
    { std::cout << (condition ? "PASS " : "FAIL ") << name << '\n'; return condition; }

    blink::core::FileIdentity Identity(const std::filesystem::path& path)
    {
        blink::core::FileEntry entry;
        (void)blink::core::ReadEntryMetadata(path.wstring(), entry);
        return entry.identity;
    }

    bool Junction(const std::filesystem::path& link, const std::filesystem::path& target)
    {
        std::filesystem::create_directory(link);
        const auto substitute = L"\\??\\" + target.wstring();
        const auto printed = target.wstring();
        struct Header { DWORD tag; WORD length, reserved, sub_offset, sub_length, print_offset, print_length; };
        std::vector<std::byte> buffer(sizeof(Header) + (substitute.size() + printed.size() + 2) * sizeof(wchar_t));
        auto* header = reinterpret_cast<Header*>(buffer.data());
        header->tag = IO_REPARSE_TAG_MOUNT_POINT;
        header->length = static_cast<WORD>(buffer.size() - 8);
        header->sub_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
        header->print_offset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
        header->print_length = static_cast<WORD>(printed.size() * sizeof(wchar_t));
        auto* names = reinterpret_cast<wchar_t*>(buffer.data() + sizeof(Header));
        std::copy(substitute.begin(), substitute.end(), names);
        std::copy(printed.begin(), printed.end(), names + substitute.size() + 1);
        CREATEFILE3_EXTENDED_PARAMETERS parameters{};
        parameters.dwSize = sizeof(parameters);
        parameters.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
        blink::core::UniqueHandle handle(CreateFile3(link.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, &parameters));
        DWORD returned{};
        return handle && DeviceIoControl(handle.get(), FSCTL_SET_REPARSE_POINT, buffer.data(),
            static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr);
    }
}

bool RunFileOperationTests()
{
    using namespace blink::app;
    bool passed = true;
    passed &= Check(ValidateNewFolderName(L"新建目录 {0}") == ERROR_SUCCESS,
        "new_folder_valid_unicode_name_and_literal_braces");
    passed &= Check(ValidateNewFolderName(L"") == ERROR_INVALID_NAME &&
        ValidateNewFolderName(L".") == ERROR_INVALID_NAME && ValidateNewFolderName(L"..") == ERROR_INVALID_NAME,
        "new_folder_empty_and_relative_names_rejected");
    passed &= Check(ValidateNewFolderName(L"child\\other") == ERROR_INVALID_NAME &&
        ValidateNewFolderName(L"child/other") == ERROR_INVALID_NAME &&
        ValidateNewFolderName(L"name:stream") == ERROR_INVALID_NAME &&
        ValidateNewFolderName(L"file*") == ERROR_INVALID_NAME,
        "new_folder_separators_streams_and_wildcards_rejected");
    passed &= Check(ValidateNewFolderName(std::wstring(L"name\0other", 10)) == ERROR_INVALID_NAME &&
        ValidateNewFolderName(L"line\nname") == ERROR_INVALID_NAME &&
        ValidateNewFolderName(L"name.") == ERROR_INVALID_NAME && ValidateNewFolderName(L"name ") == ERROR_INVALID_NAME,
        "new_folder_control_characters_and_trailing_aliases_rejected");
    passed &= Check(ValidateNewFolderName(L"CoN.txt") == ERROR_INVALID_NAME &&
        ValidateNewFolderName(L"LPT1") == ERROR_INVALID_NAME && ValidateNewFolderName(L"COM\x00b9.log") == ERROR_INVALID_NAME &&
        ValidateNewFolderName(L"CONOUT$") == ERROR_INVALID_NAME && ValidateNewFolderName(L"COM10") == ERROR_SUCCESS,
        "new_folder_reserved_dos_devices_rejected_without_overrejecting_com10");
    passed &= Check(ValidateNewFolderName(std::wstring(255, L'a')) == ERROR_SUCCESS &&
        ValidateNewFolderName(std::wstring(256, L'a')) == ERROR_FILENAME_EXCED_RANGE,
        "new_folder_component_length_is_bounded");

    const auto root = std::filesystem::absolute(std::filesystem::path(L".build") / L"file-operation-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    std::filesystem::create_directories(root / L"父目录");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path, e); } } cleanup{root};
    const auto parent = root / L"父目录";
    NewFolderRequest request{ parent.wstring(), Identity(parent), L"新目录" };
    const auto listing = blink::core::EnumerateDirectory(parent.wstring());
    passed &= Check(listing.error == ERROR_SUCCESS && listing.directory_identity == request.parent_identity,
        "new_folder_listing_captures_actual_directory_identity");
    blink::app::TabState tab;
    const auto navigate = tab.Navigate(parent.wstring());
    if (navigate) { (void)tab.CommitBrowse(*navigate, listing); }
    passed &= Check(navigate && tab.CommittedDirectoryIdentity() == request.parent_identity,
        "new_folder_tab_exposes_committed_directory_identity_without_io");
    if (const auto failed = tab.Navigate((root / L"unavailable").wstring()))
    { (void)tab.RejectBrowse(*failed, ERROR_ACCESS_DENIED); }
    passed &= Check(tab.CommittedDirectoryIdentity() == request.parent_identity,
        "new_folder_failed_navigation_keeps_committed_directory_identity");
    if (const auto this_pc = tab.Navigate(blink::app::Location::ThisPC()))
    { (void)tab.CommitThisPC(*this_pc, {}); }
    passed &= Check(!tab.CommittedDirectoryIdentity().valid,
        "new_folder_this_pc_has_no_filesystem_directory_identity");

    auto result = CreateFolderSafely(request, {}, [](const std::wstring& destination, DWORD* error) {
        const auto same = blink::core::NormalizePath(destination) == destination;
        const auto created = blink::core::CreateDirectoryModern(destination, error);
        std::cout << "INFO new_folder_create normalized_same=" << same << " error=" << *error << '\n';
        return created;
    });
    passed &= Check(result.committed && result.error == ERROR_SUCCESS && std::filesystem::is_directory(parent / request.name),
        "new_folder_real_unicode_directory_created");
    const auto existing_identity = Identity(parent / request.name);
    result = CreateFolderSafely(request);
    passed &= Check(!result.committed && (result.error == ERROR_ALREADY_EXISTS || result.error == ERROR_FILE_EXISTS) &&
        Identity(parent / request.name) == existing_identity,
        "new_folder_existing_directory_not_replaced");
    request.name = L"existing.txt";
    { std::ofstream file(parent / request.name); file << "preserve source"; }
    const auto existing_file_identity = Identity(parent / request.name);
    result = CreateFolderSafely(request);
    passed &= Check(!result.committed && result.error != ERROR_SUCCESS &&
        Identity(parent / request.name) == existing_file_identity && std::filesystem::file_size(parent / request.name) == 15,
        "new_folder_existing_file_preserved");
    request.name = L"wrong-id";
    request.parent_identity.file_id[0] ^= 1;
    result = CreateFolderSafely(request);
    passed &= Check(!result.committed && result.error == ERROR_FILE_INVALID && !std::filesystem::exists(parent / request.name),
        "new_folder_wrong_parent_identity_has_no_side_effect");
    request.parent_identity = Identity(parent);
    std::filesystem::rename(parent, root / L"原目录");
    std::filesystem::create_directory(parent);
    result = CreateFolderSafely(request);
    passed &= Check(!result.committed && result.error == ERROR_FILE_INVALID && !std::filesystem::exists(parent / request.name),
        "new_folder_replaced_parent_is_not_written");
    request.parent_identity = Identity(parent);
    const auto cancelled = std::make_shared<std::atomic_bool>(true);
    result = CreateFolderSafely(request, cancelled);
    passed &= Check(!result.committed && result.error == ERROR_CANCELLED && !std::filesystem::exists(parent / request.name),
        "new_folder_cancelled_before_commit_has_no_side_effect");
    bool parent_rename_blocked{}, ancestor_rename_blocked{};
    bool resolved_parent_used{};
    result = CreateFolderSafely(request, {}, [&](const std::wstring& destination, DWORD* error) {
        blink::core::FileEntry current_parent;
        resolved_parent_used = destination.starts_with(L"\\\\?\\") && blink::core::ReadEntryMetadata(
            destination.substr(0, destination.find_last_of(L'\\')), current_parent) && current_parent.identity == request.parent_identity;
        const bool renamed_parent = MoveFileExW(parent.c_str(), (root / L"unexpected-parent").c_str(), 0) != FALSE;
        const auto parent_error = GetLastError();
        if (renamed_parent) { (void)MoveFileExW((root / L"unexpected-parent").c_str(), parent.c_str(), 0); }
        parent_rename_blocked = !renamed_parent &&
            (parent_error == ERROR_SHARING_VIOLATION || parent_error == ERROR_ACCESS_DENIED);
        const bool renamed_root = MoveFileExW(root.c_str(), (root.wstring() + L"-unexpected").c_str(), 0) != FALSE;
        const auto root_error = GetLastError();
        if (renamed_root) { (void)MoveFileExW((root.wstring() + L"-unexpected").c_str(), root.c_str(), 0); }
        ancestor_rename_blocked = !renamed_root &&
            (root_error == ERROR_SHARING_VIOLATION || root_error == ERROR_ACCESS_DENIED);
        std::cout << "INFO new_folder_guard parent_renamed=" << renamed_parent << " error=" << parent_error <<
            " ancestor_renamed=" << renamed_root << " error=" << root_error << '\n';
        *error = ERROR_ACCESS_DENIED; return false;
    });
    passed &= Check(parent_rename_blocked && ancestor_rename_blocked,
        "new_folder_parent_and_ancestor_held_against_rename_during_commit");
    passed &= Check(resolved_parent_used, "new_folder_creation_uses_identity_verified_handle_path");
    passed &= Check(!result.committed && result.error == ERROR_ACCESS_DENIED && !std::filesystem::exists(parent / request.name),
        "new_folder_native_creation_error_is_preserved");
    const auto unlocked_parent = root / L"unlocked-parent";
    const bool parent_unlocked = MoveFileExW(parent.c_str(), unlocked_parent.c_str(), 0) != FALSE;
    const bool parent_restored = parent_unlocked && MoveFileExW(unlocked_parent.c_str(), parent.c_str(), 0) != FALSE;
    passed &= Check(parent_unlocked && parent_restored,
        "new_folder_directory_guards_released_after_operation");
    {
        CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
        parameters.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
        blink::core::UniqueHandle conflicting(CreateFile3(parent.c_str(), DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, &parameters));
        result = CreateFolderSafely(request);
        passed &= Check(conflicting && !result.committed &&
            (result.error == ERROR_SHARING_VIOLATION || result.error == ERROR_ACCESS_DENIED) &&
            !std::filesystem::exists(parent / request.name),
            "new_folder_conflicting_delete_handle_rejects_mutation");
    }
    const auto target = root / L"junction-target";
    std::filesystem::create_directory(target);
    const auto link = root / L"junction";
    const bool linked = Junction(link, target);
    passed &= Check(linked, "new_folder_owned_junction_fixture_created_without_elevation");
    if (linked)
    {
        request.parent = link.wstring(); request.parent_identity = Identity(link); request.name = L"must-not-follow";
        result = CreateFolderSafely(request);
        passed &= Check(!result.committed && result.error == ERROR_NOT_SUPPORTED && !std::filesystem::exists(target / request.name),
            "new_folder_reparse_parent_is_not_followed");
        std::filesystem::create_directory(target / L"child");
        request.parent = (link / L"child").wstring(); request.parent_identity = Identity(target / L"child");
        result = CreateFolderSafely(request);
        passed &= Check(!result.committed && result.error == ERROR_NOT_SUPPORTED && !std::filesystem::exists(target / L"child" / request.name),
            "new_folder_reparse_ancestor_is_not_followed");
    }
    request = { L"relative", Identity(parent), L"invalid-parent" };
    passed &= Check(CreateFolderSafely(request).error == ERROR_NOT_SUPPORTED,
        "new_folder_relative_parent_rejected");
    request.parent = L"\\\\server\\share";
    passed &= Check(CreateFolderSafely(request).error == ERROR_NOT_SUPPORTED,
        "new_folder_unc_parent_explicitly_unsupported");
    request = { parent.wstring(), Identity(parent), L"..\\escape" };
    passed &= Check(CreateFolderSafely(request).error == ERROR_INVALID_NAME && !std::filesystem::exists(root / L"escape"),
        "new_folder_invalid_leaf_cannot_escape_parent");
    auto long_parent = parent;
    for (int index = 0; index < 6; ++index) { long_parent /= std::wstring(45, L'a') + std::to_wstring(index); }
    const auto extended_parent = L"\\\\?\\" + long_parent.wstring();
    std::filesystem::create_directories(extended_parent);
    request = { extended_parent, Identity(extended_parent), L"长路径子目录" };
    result = CreateFolderSafely(request);
    passed &= Check(result.committed && result.error == ERROR_SUCCESS &&
        std::filesystem::is_directory(extended_parent + L"\\" + request.name),
        "new_folder_extended_long_path_supported");
    return passed;
}

bool CreateOperationTestJunction(const std::filesystem::path& link, const std::filesystem::path& target)
{ return Junction(link, target); }
