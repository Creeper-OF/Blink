#include "DirectoryMoveMergeTests.h"
#include "Blink/App/FileOperationCoordinator.h"
#include "Blink/App/MoveTransferPresentation.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace blink::app;
    bool Check(const bool value, const std::string& name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    fs::path Extended(const fs::path& path)
    { return path.wstring().starts_with(L"\\\\?\\") ? path : fs::path(L"\\\\?\\" + path.wstring()); }
    void Write(const fs::path& path, const std::string& data)
    {
        std::ofstream stream(Extended(path), std::ios::binary); stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!stream) { throw std::runtime_error("directory move merge fixture write"); }
    }
    std::string Read(const fs::path& path)
    { std::ifstream stream(Extended(path), std::ios::binary); return { std::istreambuf_iterator<char>(stream), {} }; }
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry value;
        if (!blink::core::ReadEntryMetadata(Extended(path).wstring(), value))
        { throw std::runtime_error("directory move merge fixture metadata"); }
        value.name = path.filename().wstring(); value.full_path = path.wstring(); return value;
    }
    void Cleanup(const fs::path& root) noexcept
    {
        WIN32_FIND_DATAW value{}; const auto find = FindFirstFileW((Extended(root) / L"*").c_str(), &value);
        if (find == INVALID_HANDLE_VALUE) { return; }
        do
        {
            if (!std::wcscmp(value.cFileName, L".") || !std::wcscmp(value.cFileName, L"..")) { continue; }
            const auto child = Extended(root) / value.cFileName;
            if (value.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!(value.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { Cleanup(child); }
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL); RemoveDirectoryW(child.c_str());
            }
            else { SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(child.c_str()); }
        } while (FindNextFileW(find, &value));
        FindClose(find);
    }
    struct OwnedRoot
    {
        fs::path path;
        ~OwnedRoot() { Cleanup(path); RemoveDirectoryW(Extended(path).c_str()); }
    };
    struct Fixture
    {
        fs::path source_parent, target_parent, source, target;
        Fixture(const fs::path& local, const fs::path& other, const wchar_t* name)
            : source_parent(local / name / L"来源"), target_parent(other / name / L"目标"),
            source(source_parent / L"内容目录"), target(target_parent / L"内容目录")
        { fs::create_directories(Extended(source)); fs::create_directories(Extended(target)); }
        DirectoryCopyRequest Request() const
        {
            DirectoryCopyRequest value{ source_parent.wstring(), Entry(source_parent).identity, Entry(source),
                target_parent.wstring(), Entry(target_parent).identity, target.filename().wstring() };
            value.conflict_policy = FileCopyConflictPolicy::Merge; return value;
        }
        MoveRequest Move() const
        {
            MoveRequest value{ source_parent.wstring(), Entry(source_parent).identity, Entry(source),
                target_parent.wstring(), Entry(target_parent).identity, target.filename().wstring() };
            value.conflict_policy = FileCopyConflictPolicy::Merge; return value;
        }
        void Conflict(const wchar_t* name = L"same.txt") const
        { Write(source / name, "new-content"); Write(target / name, "old-content"); }
    };
    bool SetCreation(const fs::path& path, const ULONGLONG ticks)
    {
        blink::core::UniqueHandle handle(CreateFileW(Extended(path).c_str(), FILE_WRITE_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        const FILETIME creation{ static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32) };
        return handle && SetFileTime(handle.get(), &creation, nullptr, nullptr);
    }
    FILE_BASIC_INFO Basic(const fs::path& path)
    {
        blink::core::UniqueHandle handle(CreateFileW(Extended(path).c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        FILE_BASIC_INFO value{};
        if (!handle || !GetFileInformationByHandleEx(handle.get(), FileBasicInfo, &value, sizeof(value)))
        { throw std::runtime_error("directory move merge fixture basic metadata"); }
        return value;
    }
    void Info(const char* name, const MoveResult& result)
    {
        std::cout << "INFO directory_move_merge_" << name << " error=" << result.error << " moved=" << result.moved <<
            " skipped=" << result.skipped << " files_removed=" << result.source_files_removed <<
            " dirs_removed=" << result.source_directories_removed << " remaining=" << result.source_items_remaining << '\n';
    }
    bool Relocated(const MoveResult& result, const fs::path& source, const fs::path& target,
        const blink::core::FileIdentity& source_identity)
    {
        return std::any_of(result.directory_relocations.begin(), result.directory_relocations.end(), [&](const auto& value) {
            return value.source_path == source.wstring() && value.destination_path == target.wstring() && value.source_identity == source_identity;
        });
    }
    bool RunFilesystemCases(const fs::path& local, const fs::path& other, const bool cross)
    {
        bool passed = true;
        const auto check = [&](const bool value, const char* name) {
            return Check(value, std::string("directory_move_merge_") + (cross ? "cross_" : "same_") + name);
        };
        {
            Fixture f(local, other, L"nested-metadata");
            fs::create_directories(f.source / L"shared" / L"empty"); fs::create_directory(f.target / L"shared");
            const std::string payload = std::string(131072, 'a') + std::string("\0tail", 5);
            Write(f.source / L"shared" / L"new.txt", payload);
            Write((f.source / L"shared" / L"new.txt").wstring() + L":Zone.Identifier", "zone-data");
            Write((f.source / L"shared" / L"new.txt").wstring() + L":extra", "extra-data");
            Write(f.target / L"shared" / L"keep.txt", "target-only");
            const auto leaf_id = Entry(f.source / L"shared" / L"new.txt").identity;
            const auto source_root_id = Entry(f.source).identity, source_shared_id = Entry(f.source / L"shared").identity;
            const auto source_empty_id = Entry(f.source / L"shared" / L"empty").identity;
            const auto root_id = Entry(f.target).identity, shared_id = Entry(f.target / L"shared").identity;
            const auto keep_id = Entry(f.target / L"shared" / L"keep.txt").identity;
            const bool prepared = SetCreation(f.source, 132537600000000000ULL) && SetCreation(f.source / L"shared", 132537600000000000ULL) &&
                SetCreation(f.target, 129067776000000000ULL) && SetCreation(f.target / L"shared", 129067776000000000ULL) &&
                SetFileAttributesW(Extended(f.target).c_str(), FILE_ATTRIBUTE_HIDDEN) &&
                SetFileAttributesW(Extended(f.target / L"shared").c_str(), FILE_ATTRIBUTE_HIDDEN);
            const auto before = Basic(f.target), shared_before = Basic(f.target / L"shared");
            unsigned conflicts{}; auto request = f.Request();
            request.resolve_conflict = [&](const auto&) { ++conflicts; return FileCopyConflictPolicy::Cancel; };
            const auto result = MoveDirectoryMergeSafely(request);
            const auto after = Basic(f.target), shared_after = Basic(f.target / L"shared");
            Info(cross ? "cross_nested" : "same_nested", result);
            passed &= check(prepared && result.merged && result.moved && result.source_removed && !result.error && !result.skipped &&
                !result.source_items_remaining && !fs::exists(f.source) && !conflicts && Entry(f.target).identity == root_id &&
                Entry(f.target / L"shared").identity == shared_id && Entry(f.target / L"shared" / L"keep.txt").identity == keep_id &&
                Read(f.target / L"shared" / L"keep.txt") == "target-only" && Read(f.target / L"shared" / L"new.txt") == payload &&
                Read((f.target / L"shared" / L"new.txt").wstring() + L":Zone.Identifier") == "zone-data" &&
                Read((f.target / L"shared" / L"new.txt").wstring() + L":extra") == "extra-data" &&
                fs::is_directory(f.target / L"shared" / L"empty"), "nested_target_only_bytes_ads_and_empty_directory");
            passed &= check(after.CreationTime.QuadPart == before.CreationTime.QuadPart &&
                shared_after.CreationTime.QuadPart == shared_before.CreationTime.QuadPart &&
                (after.FileAttributes & FILE_ATTRIBUTE_HIDDEN) && (shared_after.FileAttributes & FILE_ATTRIBUTE_HIDDEN),
                "existing_directory_identity_and_metadata_are_retained");
            passed &= check(result.directory_relocations.size() == 3 && Relocated(result, f.source, f.target, source_root_id) &&
                Relocated(result, f.source / L"shared", f.target / L"shared", source_shared_id) &&
                Relocated(result, f.source / L"shared" / L"empty", f.target / L"shared" / L"empty", source_empty_id),
                "removed_source_directories_report_exact_existing_target_mappings");
            if (!cross) { passed &= check(Entry(f.target / L"shared" / L"new.txt").identity == leaf_id, "leaf_native_move_keeps_file_identity"); }
        }
        {
            Fixture f(local, other, L"replace"); f.Conflict();
            Write((f.source / L"same.txt").wstring() + L":new-stream", "new-ads");
            Write((f.target / L"same.txt").wstring() + L":old-stream", "old-ads");
            const auto source_id = Entry(f.source / L"same.txt").identity, old_id = Entry(f.target / L"same.txt").identity;
            unsigned conflicts{}; bool context{}; auto request = f.Request();
            request.resolve_conflict = [&](const auto& conflict) {
                ++conflicts; context = !conflict.request.source.is_directory && !conflict.destination_is_directory &&
                    conflict.request.source_parent == f.source.wstring() && conflict.request.destination_parent == f.target.wstring();
                return FileCopyConflictPolicy::Replace;
            };
            const auto result = MoveDirectoryMergeSafely(request);
            passed &= check(result.moved && !result.error && conflicts == 1 && context && !fs::exists(f.source) &&
                Entry(f.target / L"same.txt").identity != old_id && Read(f.target / L"same.txt") == "new-content" &&
                Read((f.target / L"same.txt").wstring() + L":new-stream") == "new-ads" &&
                Read((f.target / L"same.txt").wstring() + L":old-stream").empty() &&
                (cross || Entry(f.target / L"same.txt").identity == source_id), "replace_leaf_preserves_source_data_and_removes_old_ads");
        }
        {
            Fixture f(local, other, L"keep-both"); f.Conflict(); Write(f.target / L"same (2).txt", "occupied");
            const auto source_id = Entry(f.source / L"same.txt").identity; auto request = f.Request();
            request.resolve_conflict = [](const auto&) { return FileCopyConflictPolicy::KeepBoth; };
            const auto result = MoveDirectoryMergeSafely(request);
            passed &= check(result.moved && !result.error && !fs::exists(f.source) && Read(f.target / L"same.txt") == "old-content" &&
                Read(f.target / L"same (2).txt") == "occupied" && Read(f.target / L"same (3).txt") == "new-content" &&
                (cross || Entry(f.target / L"same (3).txt").identity == source_id), "keep_both_uses_available_name_without_replacing_siblings");
        }
        {
            Fixture f(local, other, L"skip-leaf"); f.Conflict(); Write(f.source / L"tail.txt", "tail");
            fs::create_directory(f.source / L"empty"); const auto old_id = Entry(f.target / L"same.txt").identity;
            const auto source_root_id = Entry(f.source).identity, empty_id = Entry(f.source / L"empty").identity;
            auto request = f.Request(); request.resolve_conflict = [](const auto&) { return FileCopyConflictPolicy::Skip; };
            const auto result = MoveDirectoryMergeSafely(request);
            passed &= check(result.merged && result.skipped && !result.moved && !result.source_removed && !result.error &&
                result.files_skipped == 1 && result.source_items_remaining == 1 && fs::is_directory(f.source) &&
                Read(f.source / L"same.txt") == "new-content" && !fs::exists(f.source / L"tail.txt") && !fs::exists(f.source / L"empty") &&
                fs::is_directory(f.target / L"empty") && Entry(f.target / L"same.txt").identity == old_id &&
                Read(f.target / L"same.txt") == "old-content" && Read(f.target / L"tail.txt") == "tail" &&
                result.directory_relocations.size() == 1 && Relocated(result, f.source / L"empty", f.target / L"empty", empty_id) &&
                !Relocated(result, f.source, f.target, source_root_id), "skip_retains_source_root_and_only_skipped_items");
        }
        {
            Fixture f(local, other, L"all-skipped"); fs::create_directory(f.source / L"shared"); fs::create_directory(f.target / L"shared");
            Write(f.source / L"shared" / L"same.txt", "new-content"); Write(f.target / L"shared" / L"same.txt", "old-content");
            const auto source_id = Entry(f.source).identity, nested_id = Entry(f.source / L"shared").identity;
            auto request = f.Request(); request.resolve_conflict = [](const auto&) { return FileCopyConflictPolicy::Skip; };
            const auto result = MoveDirectoryMergeSafely(request);
            passed &= check(result.skipped && !result.moved && !result.source_removed && !result.error && result.files_skipped == 1 &&
                result.source_items_remaining == 2 && !result.source_files_removed && !result.source_directories_removed &&
                Entry(f.source).identity == source_id && Entry(f.source / L"shared").identity == nested_id &&
                Read(f.source / L"shared" / L"same.txt") == "new-content" && Read(f.target / L"shared" / L"same.txt") == "old-content",
                "all_skip_keeps_source_ancestors_and_reports_remaining_planned_items");
        }
        {
            Fixture f(local, other, L"skip-subtree"); fs::create_directories(f.source / L"same" / L"nested");
            Write(f.source / L"same" / L"nested" / L"child.txt", "child"); Write(f.target / L"same", "old-file");
            Write(f.source / L"tail.txt", "tail"); bool context{}; auto request = f.Request();
            request.resolve_conflict = [&](const auto& conflict) {
                context = conflict.request.source.is_directory && !conflict.destination_is_directory; return FileCopyConflictPolicy::Skip;
            };
            const auto result = MoveDirectoryMergeSafely(request);
            passed &= check(result.skipped && !result.error && context && !result.moved && result.files_skipped == 1 &&
                result.directories_skipped == 2 && result.source_items_remaining == 3 &&
                Read(f.source / L"same" / L"nested" / L"child.txt") == "child" && Read(f.target / L"same") == "old-file" &&
                !fs::exists(f.source / L"tail.txt") && Read(f.target / L"tail.txt") == "tail", "skip_type_conflict_keeps_complete_source_subtree");
        }
        {
            Fixture f(local, other, L"keep-directory"); fs::create_directories(f.source / L"same" / L"nested");
            Write(f.source / L"same" / L"nested" / L"child.txt", "child"); Write(f.target / L"same", "old-file");
            const auto directory_id = Entry(f.source / L"same").identity;
            auto request = f.Request(); request.resolve_conflict = [](const auto&) { return FileCopyConflictPolicy::KeepBoth; };
            const auto result = MoveDirectoryMergeSafely(request);
            const bool content_ok = Read(f.target / L"same") == "old-file" && Read(f.target / L"same (2)" / L"nested" / L"child.txt") == "child";
            const bool mapping_ok = Relocated(result, f.source / L"same", f.target / L"same (2)", directory_id);
            const bool valid = result.moved && !result.error && !fs::exists(f.source) && content_ok && mapping_ok;
            if (!valid)
            {
                const auto utf8 = [](const std::wstring& value) {
                    const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
                    std::string text(static_cast<std::size_t>((std::max)(0, size)), '\0');
                    if (size > 0) { WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), text.data(), size, nullptr, nullptr); }
                    return text;
                };
                Info(cross ? "cross_keep_directory" : "same_keep_directory", result);
                const auto* tree = result.directory_copy_result ? &*result.directory_copy_result : nullptr;
                std::cout << "INFO directory_move_merge_keep_directory source_removed=" << result.source_removed <<
                    " files_copied=" << (tree ? tree->files_copied : 0) << " dirs_created=" << (tree ? tree->directories_created : 0) <<
                    " source_exists=" << fs::exists(f.source) << " content_ok=" << content_ok << " mapping_ok=" << mapping_ok <<
                    " failure_path=" << (tree ? utf8(tree->failure_path) : std::string{}) << '\n';
                for (const auto& relocation : result.directory_relocations)
                {
                    std::cout << "INFO directory_move_merge_keep_directory relocation=" << utf8(relocation.source_path) <<
                        " -> " << utf8(relocation.destination_path) << '\n';
                }
            }
            passed &= check(valid, "directory_file_keep_both_rebinds_nested_paths");
        }
        {
            Fixture f(local, other, L"skip-file-directory"); Write(f.source / L"same", "file"); fs::create_directory(f.target / L"same");
            Write(f.target / L"same" / L"keep.txt", "target-only"); const auto target_id = Entry(f.target / L"same").identity;
            bool context{}; auto request = f.Request();
            request.resolve_conflict = [&](const auto& conflict) {
                context = !conflict.request.source.is_directory && conflict.destination_is_directory;
                return FileCopyConflictPolicy::Skip;
            };
            const auto result = MoveDirectoryMergeSafely(request);
            passed &= check(context && result.skipped && !result.moved && !result.error && result.files_skipped == 1 &&
                result.source_items_remaining == 1 && !result.source_files_removed && Entry(f.target / L"same").identity == target_id &&
                Read(f.target / L"same" / L"keep.txt") == "target-only" && Read(f.source / L"same") == "file",
                "file_directory_skip_keeps_target_directory_and_selected_source_file");
        }
        {
            Fixture f(local, other, L"unresolved-child"); f.Conflict(); const auto old_id = Entry(f.target / L"same.txt").identity;
            const auto result = MoveDirectoryMergeSafely(f.Request());
            passed &= check(!result.moved && !result.skipped && (result.error == ERROR_FILE_EXISTS || result.error == ERROR_ALREADY_EXISTS) &&
                !MoveHasSourceChanges(result) && !MoveHasPartialDestination(result) && Entry(f.target / L"same.txt").identity == old_id &&
                Read(f.source / L"same.txt") == "new-content" && Read(f.target / L"same.txt") == "old-content",
                "unresolved_child_collision_never_implicitly_overwrites_or_skips");
        }
        {
            Fixture f(local, other, L"invalid-type-replace"); Write(f.source / L"same", "file"); fs::create_directory(f.target / L"same");
            Write(f.target / L"same" / L"keep.txt", "target-only"); auto request = f.Request();
            request.resolve_conflict = [](const auto&) { return FileCopyConflictPolicy::Replace; };
            const auto result = MoveDirectoryMergeSafely(request);
            passed &= check(!result.moved && !result.skipped && result.error == ERROR_NOT_SUPPORTED &&
                !result.source_files_removed && !MoveHasPartialDestination(result) && Read(f.source / L"same") == "file" &&
                Read(f.target / L"same" / L"keep.txt") == "target-only", "file_directory_type_conflict_cannot_replace_whole_directory");
        }
        {
            Fixture f(local, other, L"empty-root"); const auto target_id = Entry(f.target).identity;
            const auto result = MoveDirectoryMergeSafely(f.Request());
            passed &= check(result.merged && result.moved && result.source_removed && !result.error && !fs::exists(f.source) &&
                Entry(f.target).identity == target_id && !result.source_files_removed && result.source_directories_removed == 1,
                "empty_source_root_is_removed_without_recreating_target_root");
        }
        {
            Fixture f(local, other, L"cancel-before-start"); Write(f.source / L"a.txt", "source"); Write(f.target / L"keep.txt", "keep");
            auto cancel = std::make_shared<std::atomic_bool>(true); const auto result = MoveDirectoryMergeSafely(f.Request(), cancel);
            passed &= check(!result.moved && result.error == ERROR_CANCELLED && !result.source_files_removed &&
                !result.source_directories_removed && Read(f.source / L"a.txt") == "source" && !fs::exists(f.target / L"a.txt") &&
                Read(f.target / L"keep.txt") == "keep", "pre_cancel_has_no_native_effect");
        }
        {
            Fixture f(local, other, L"cancel-prefix"); Write(f.source / L"a.txt", "first"); Write(f.source / L"b.txt", "tail");
            auto cancel = std::make_shared<std::atomic_bool>(); const auto first_id = Entry(f.source / L"a.txt").identity;
            const auto result = MoveDirectoryMergeSafely(f.Request(), cancel, [cancel](const MoveProgress& value) {
                if (value.source_files_removed == 1) { cancel->store(true); }
            });
            passed &= check(!result.moved && !result.skipped && result.error == ERROR_CANCELLED && result.source_files_removed == 1 &&
                MoveHasSourceChanges(result) && MoveHasPartialDestination(result) && !fs::exists(f.source / L"a.txt") &&
                Read(f.target / L"a.txt") == "first" && Read(f.source / L"b.txt") == "tail" && !fs::exists(f.target / L"b.txt") &&
                (cross || Entry(f.target / L"a.txt").identity == first_id), "cancel_after_commit_retains_actual_prefix_and_unstarted_source");
        }
        {
            Fixture f(local, other, L"observer-failure"); Write(f.source / L"a.txt", "first"); Write(f.source / L"b.txt", "tail");
            const auto result = MoveDirectoryMergeSafely(f.Request(), {}, [](const MoveProgress& value) {
                if (value.source_files_removed == 1) { throw std::runtime_error("move merge progress observer"); }
            });
            passed &= check(!result.moved && result.error == ERROR_UNHANDLED_EXCEPTION && result.source_files_removed == 1 &&
                MoveHasPartialDestination(result) && !fs::exists(f.source / L"a.txt") && Read(f.target / L"a.txt") == "first" &&
                Read(f.source / L"b.txt") == "tail" && !fs::exists(f.target / L"b.txt"), "observer_failure_reports_real_committed_prefix");
        }
        {
            Fixture f(local, other, L"new-source-child"); Write(f.source / L"a.txt", "first"); bool added{};
            const auto result = MoveDirectoryMergeSafely(f.Request(), {}, [&](const MoveProgress& value) {
                if (!added && value.source_files_removed == 1) { Write(f.source / L"unplanned.txt", "new-source-data"); added = true; }
            });
            passed &= check(added && !result.moved && !result.skipped &&
                (result.error == ERROR_DIR_NOT_EMPTY || result.error == ERROR_FILE_INVALID) && result.source_files_removed == 1 &&
                !fs::exists(f.source / L"a.txt") && Read(f.target / L"a.txt") == "first" &&
                Read(f.source / L"unplanned.txt") == "new-source-data" && !fs::exists(f.target / L"unplanned.txt"),
                "new_source_child_after_first_commit_is_retained_and_reports_real_failure");
        }
        {
            Fixture f(local, other, L"replace-planned-source"); Write(f.source / L"a.txt", "first"); Write(f.source / L"b.txt", "planned");
            bool attempted{}, changed{}; DWORD change_error{};
            const auto result = MoveDirectoryMergeSafely(f.Request(), {}, [&](const MoveProgress& value) {
                if (!attempted && value.source_files_removed == 1)
                {
                    attempted = true; changed = MoveFileExW((f.source / L"b.txt").c_str(), (f.source / L"original.txt").c_str(), 0) != FALSE;
                    change_error = changed ? ERROR_SUCCESS : GetLastError();
                    if (changed) { Write(f.source / L"b.txt", "replacement"); }
                }
            });
            const bool preserved = changed ? !result.moved && result.error == ERROR_FILE_INVALID &&
                result.source_files_removed == 1 && Read(f.source / L"b.txt") == "replacement" &&
                Read(f.source / L"original.txt") == "planned" && !fs::exists(f.target / L"b.txt") :
                (change_error == ERROR_ACCESS_DENIED || change_error == ERROR_SHARING_VIOLATION) && result.moved &&
                !fs::exists(f.source) && Read(f.target / L"b.txt") == "planned";
            passed &= check(attempted && preserved && Read(f.target / L"a.txt") == "first", "replaced_planned_source_is_not_moved_as_original_item");
        }
        {
            Fixture f(local, other, L"cancel-after-empty-directory");
            fs::create_directory(f.source / L"empty-a"); fs::create_directory(f.source / L"empty-b");
            const auto a_id = Entry(f.source / L"empty-a").identity, b_id = Entry(f.source / L"empty-b").identity;
            const auto root_id = Entry(f.source).identity; auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = MoveDirectoryMergeSafely(f.Request(), cancel, [cancel](const MoveProgress& value) {
                if (value.source_directories_removed == 1) { cancel->store(true); }
            });
            const bool a_removed = !fs::exists(f.source / L"empty-a"), b_removed = !fs::exists(f.source / L"empty-b");
            passed &= check(!result.moved && result.error == ERROR_CANCELLED && result.source_directories_removed == 1 &&
                result.directory_relocations.size() == 1 && a_removed != b_removed && fs::is_directory(f.source) &&
                !Relocated(result, f.source, f.target, root_id) &&
                (a_removed ? Relocated(result, f.source / L"empty-a", f.target / L"empty-a", a_id) :
                    Relocated(result, f.source / L"empty-b", f.target / L"empty-b", b_id)),
                "cancel_after_empty_directory_removal_keeps_only_committed_relocations");
        }
        {
            Fixture f(local, other, L"completed-observer"); Write(f.source / L"a.txt", "source");
            auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = MoveDirectoryMergeSafely(f.Request(), cancel, [cancel](const MoveProgress& value) {
                if (value.phase == MovePhase::Completed) { cancel->store(true); throw std::runtime_error("late observer"); }
            });
            passed &= check(result.moved && result.source_removed && !result.error && cancel->load() &&
                !fs::exists(f.source) && Read(f.target / L"a.txt") == "source", "completed_move_wins_over_late_cancel_or_observer_exception");
        }
        if (cross)
        {
            Fixture f(local, other, L"cancel-copied-before-removal"); Write(f.source / L"a.txt", "source");
            Write((f.source / L"a.txt").wstring() + L":extra", "ads"); auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = MoveDirectoryMergeSafely(f.Request(), cancel, {}, [cancel] { cancel->store(true); return ERROR_SUCCESS; });
            passed &= check(!result.moved && !result.source_removed && result.error == ERROR_CANCELLED && !result.source_files_removed &&
                MoveHasPartialDestination(result) && Read(f.source / L"a.txt") == "source" && Read(f.target / L"a.txt") == "source" &&
                Read((f.source / L"a.txt").wstring() + L":extra") == "ads" && Read((f.target / L"a.txt").wstring() + L":extra") == "ads",
                "cancel_after_copy_retains_complete_destination_and_original_source");
        }
        return passed;
    }
    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        auto done = std::make_shared<std::promise<Status>>(); auto future = done->get_future();
        action.Completed([done](auto&&, const Status status) { done->set_value(status); });
        return future.wait_for(std::chrono::seconds(8)) == std::future_status::ready && future.get() == Status::Completed;
    }
    struct ConflictInbox
    {
        std::mutex mutex;
        std::condition_variable ready;
        std::vector<FileOperationSnapshot> items;
        void Publish(const FileOperationSnapshot& value)
        {
            if (!value.directory_conflict) { return; }
            {
                std::scoped_lock lock(mutex);
                if (!items.empty() && items.back().operation_id == value.operation_id &&
                    items.back().directory_conflict_id == value.directory_conflict_id) { return; }
                items.push_back(value);
            }
            ready.notify_all();
        }
        std::optional<FileOperationSnapshot> Next(const std::size_t index)
        {
            std::unique_lock lock(mutex);
            if (!ready.wait_for(lock, std::chrono::seconds(8), [&] { return items.size() > index; })) { return {}; }
            return items[index];
        }
    };
    MoveRequest Tail(const Fixture& fixture, const wchar_t* name)
    {
        Write(fixture.source_parent / name, "queue-tail");
        return { fixture.source_parent.wstring(), Entry(fixture.source_parent).identity, Entry(fixture.source_parent / name),
            fixture.target_parent.wstring(), Entry(fixture.target_parent).identity, name };
    }
    bool RunQueueCases(const fs::path& root)
    {
        bool passed = true;
        const wchar_t* type_names[]{ L"queue-conflict-directory", L"queue-conflict-file", L"queue-conflict-missing" };
        const char* type_labels[]{ "directory", "file", "missing" };
        for (std::size_t type = 0; type < std::size(type_names); ++type)
        {
            Fixture f(root, root, type_names[type]); Write(f.source / L"source.txt", "source-untouched");
            if (type == 0) { Write(f.target / L"keep.txt", "target-untouched"); }
            else
            {
                fs::remove(f.target);
                if (type == 1) { Write(f.target, "target-file-untouched"); }
            }
            const auto source_id = Entry(f.source).identity;
            const auto target_id = type != 2 ? Entry(f.target).identity : blink::core::FileIdentity{};
            auto request = f.Move(); request.conflict_policy = FileCopyConflictPolicy::Fail;
            const DWORD conflict_error = type == 1 ? ERROR_ALREADY_EXISTS : ERROR_FILE_EXISTS;
            FileOperationCoordinator operation({}, {}, {}, [type, conflict_error](const auto&, const auto&, const auto&) {
                MoveResult result{ conflict_error };
                // A stale hint must be replaced by the worker's real metadata,
                // including clearing it when the conflicting name disappears.
                result.conflict_destination_is_directory = type != 0;
                return result;
            });
            const auto id = operation.MoveEntry(request); const bool finished = Finished(operation.CurrentAction());
            const auto result = operation.Snapshot();
            const std::optional<bool> expected = type == 2 ? std::nullopt : std::optional<bool>{ type == 0 };
            const bool target_untouched = type == 0 ? Entry(f.target).identity == target_id && Read(f.target / L"keep.txt") == "target-untouched" :
                type == 1 ? Entry(f.target).identity == target_id && Read(f.target) == "target-file-untouched" : !fs::exists(f.target);
            passed &= Check(id && finished && result.stage == FileOperationStage::Failed && result.move_result &&
                result.move_result->error == conflict_error && result.move_result->conflict_destination_is_directory == expected &&
                !MoveHasSourceChanges(*result.move_result) && !MoveHasPartialDestination(*result.move_result) && target_untouched &&
                Entry(f.source).identity == source_id && Read(f.source / L"source.txt") == "source-untouched",
                std::string("directory_move_merge_conflict_worker_records_real_target_type_") + type_labels[type]);
        }
        {
            Fixture f(root, root, L"queue-root-resume"); Write(f.source / L"new.txt", "new-content"); Write(f.target / L"keep.txt", "target-only");
            const auto target_id = Entry(f.target).identity; const auto tail = Tail(f, L"queue-tail.txt");
            auto request = f.Move(); request.conflict_policy = FileCopyConflictPolicy::Fail;
            FileOperationCoordinator operation; const auto id = operation.MoveEntries({ request, tail });
            const bool first_finished = Finished(operation.CurrentAction()); const auto first = operation.Snapshot();
            const bool root_conflict = first.stage == FileOperationStage::Failed && first.move_batch && first.move_batch->resumable &&
                first.move_result && first.move_result->conflict_destination_is_directory == std::optional<bool>{ true } &&
                !MoveHasSourceChanges(*first.move_result) && !MoveHasPartialDestination(*first.move_result);
            const auto resumed = operation.ResumeMoveBatch(id, FileCopyConflictPolicy::Merge);
            const bool finished = Finished(operation.CurrentAction()); const auto result = operation.Snapshot();
            passed &= Check(id && first_finished && root_conflict && resumed > id && finished && result.stage == FileOperationStage::Completed &&
                result.move_batch && result.move_batch->moved_count == 2 && !result.move_batch->partial_count && !fs::exists(f.source) &&
                Entry(f.target).identity == target_id && Read(f.target / L"keep.txt") == "target-only" && Read(f.target / L"new.txt") == "new-content" &&
                !fs::exists(f.source_parent / L"queue-tail.txt") && Read(f.target_parent / L"queue-tail.txt") == "queue-tail",
                "directory_move_merge_root_collision_resume_merge_uses_existing_target_and_continues_batch");
        }
        {
            Fixture f(root, root, L"queue-all-skip"); f.Conflict(); const auto inbox = std::make_shared<ConflictInbox>();
            FileOperationCoordinator operation;
            const auto id = operation.MoveEntry(f.Move(), [inbox](const auto& value) { inbox->Publish(value); });
            const auto action = operation.CurrentAction(); const auto pending = inbox->Next(0);
            const bool resolved = pending && operation.ResolveDirectoryConflict(id, pending->directory_conflict_id, FileCopyConflictPolicy::Skip);
            if (!resolved) { operation.Cancel(); }
            const bool finished = Finished(action); const auto result = operation.Snapshot();
            passed &= Check(id && resolved && finished && result.stage == FileOperationStage::Skipped && result.move_result &&
                result.move_result->skipped && !result.move_result->moved && !result.move_result->error &&
                !MoveHasSourceChanges(*result.move_result) && Read(f.source / L"same.txt") == "new-content" && Read(f.target / L"same.txt") == "old-content",
                "directory_move_merge_single_all_skip_is_explicit_skipped_not_completed_move");
        }
        {
            Fixture f(root, root, L"queue-decisions"); Write(f.source / L"a-prefix.txt", "prefix");
            f.Conflict(L"b-first.txt"); f.Conflict(L"c-second.txt"); Write(f.source / L"d-tail.txt", "tail");
            const auto tail = Tail(f, L"queue-tail.txt"); const auto inbox = std::make_shared<ConflictInbox>();
            FileOperationCoordinator operation;
            const auto id = operation.MoveEntries({ f.Move(), tail }, [inbox](const auto& value) { inbox->Publish(value); });
            const auto action = operation.CurrentAction(); const auto first = inbox->Next(0);
            bool valid{}, stale{}, second_valid{}; blink::core::FileIdentity prefix_id;
            if (first)
            {
                prefix_id = Entry(f.target / L"a-prefix.txt").identity;
                stale = !operation.ResolveDirectoryConflict(id + 1, first->directory_conflict_id, FileCopyConflictPolicy::Replace) &&
                    !operation.ResolveDirectoryConflict(id, first->directory_conflict_id + 1, FileCopyConflictPolicy::Replace);
                valid = first->operation_id == id && first->kind == FileOperationKind::Move && operation.IsBusy() &&
                    first->directory_conflict->request.destination_name == L"b-first.txt" &&
                    operation.ResolveDirectoryConflict(id, first->directory_conflict_id, FileCopyConflictPolicy::KeepBoth);
                const auto second = inbox->Next(1);
                if (second)
                {
                    stale = stale && !operation.ResolveDirectoryConflict(id, first->directory_conflict_id, FileCopyConflictPolicy::Replace);
                    second_valid = second->directory_conflict_id != first->directory_conflict_id &&
                        second->directory_conflict->request.destination_name == L"c-second.txt" &&
                        operation.ResolveDirectoryConflict(id, second->directory_conflict_id, FileCopyConflictPolicy::Skip);
                }
            }
            if (!valid || !second_valid) { operation.Cancel(); }
            const bool finished = Finished(action); const auto result = operation.Snapshot();
            passed &= Check(id && valid && stale && second_valid && finished && !result.directory_conflict &&
                result.stage == FileOperationStage::Completed && result.move_batch && result.move_batch->skipped_count == 1 &&
                result.move_batch->moved_count == 1 && !result.move_batch->partial_count && Entry(f.target / L"a-prefix.txt").identity == prefix_id &&
                Read(f.target / L"b-first.txt") == "old-content" && Read(f.target / L"b-first (2).txt") == "new-content" &&
                Read(f.source / L"c-second.txt") == "new-content" && Read(f.target / L"c-second.txt") == "old-content" &&
                !fs::exists(f.source / L"a-prefix.txt") && !fs::exists(f.source / L"d-tail.txt") &&
                Read(f.target / L"d-tail.txt") == "tail" && !fs::exists(f.source_parent / L"queue-tail.txt") &&
                Read(f.target_parent / L"queue-tail.txt") == "queue-tail", "directory_move_merge_queue_decisions_stale_ids_and_skipped_source_continue_tail");
        }
        {
            Fixture f(root, root, L"queue-cancel"); Write(f.source / L"a-prefix.txt", "prefix"); f.Conflict(L"b-conflict.txt");
            const auto tail = Tail(f, L"queue-tail.txt"); const auto inbox = std::make_shared<ConflictInbox>(); FileOperationCoordinator operation;
            const auto id = operation.MoveEntries({ f.Move(), tail }, [inbox](const auto& value) { inbox->Publish(value); });
            const auto action = operation.CurrentAction(); const auto pending = inbox->Next(0); operation.Cancel();
            const bool finished = Finished(action); const auto result = operation.Snapshot();
            passed &= Check(id && pending && finished && !operation.IsBusy() && result.stage == FileOperationStage::Cancelled &&
                !result.directory_conflict && result.move_result && result.move_result->source_files_removed == 1 &&
                MoveHasPartialDestination(*result.move_result) && Read(f.target / L"a-prefix.txt") == "prefix" &&
                !fs::exists(f.source / L"a-prefix.txt") && Read(f.source / L"b-conflict.txt") == "new-content" &&
                Read(f.source_parent / L"queue-tail.txt") == "queue-tail" && !fs::exists(f.target_parent / L"queue-tail.txt"),
                "directory_move_merge_queue_cancel_wakes_child_decision_and_keeps_pending_tail");
            passed &= Check(pending && !operation.ResolveDirectoryConflict(id, pending->directory_conflict_id, FileCopyConflictPolicy::Replace),
                "directory_move_merge_cancelled_conflict_cannot_be_resolved_later");
            const auto refresh = MoveRefreshParents(result);
            passed &= Check(refresh.size() == 2 &&
                std::find(refresh.begin(), refresh.end(), Entry(f.source_parent).identity) != refresh.end() &&
                std::find(refresh.begin(), refresh.end(), Entry(f.target_parent).identity) != refresh.end(),
                "directory_move_merge_partial_source_changes_refresh_both_parent_locations");
        }
        {
            Fixture f(root, root, L"queue-close"); f.Conflict(); const auto inbox = std::make_shared<ConflictInbox>();
            FileOperationCoordinator operation;
            const auto id = operation.MoveEntry(f.Move(), [inbox](const auto& value) { inbox->Publish(value); });
            const auto action = operation.CurrentAction(); const auto pending = inbox->Next(0); operation.Shutdown();
            const bool finished = Finished(action); const auto result = operation.Snapshot();
            passed &= Check(id && pending && finished && result.closed && !operation.IsBusy() &&
                !operation.ResolveDirectoryConflict(id, pending->directory_conflict_id, FileCopyConflictPolicy::Replace) &&
                Read(f.source / L"same.txt") == "new-content" && Read(f.target / L"same.txt") == "old-content",
                "directory_move_merge_shutdown_wakes_worker_without_mutating_waiting_collision");
        }
        {
            Fixture f(root, root, L"queue-publish-throw"); f.Conflict(); auto published = std::make_shared<std::atomic_bool>();
            FileOperationCoordinator operation;
            const auto id = operation.MoveEntry(f.Move(), [published](const auto& value) {
                if (value.directory_conflict) { published->store(true); throw std::runtime_error("move merge conflict publication"); }
            });
            const bool finished = Finished(operation.CurrentAction()); const auto result = operation.Snapshot();
            passed &= Check(id && published->load() && finished && !operation.IsBusy() && !result.directory_conflict &&
                result.stage == FileOperationStage::Cancelled && Read(f.source / L"same.txt") == "new-content" &&
                Read(f.target / L"same.txt") == "old-content", "directory_move_merge_failed_publication_does_not_leave_worker_waiting");
        }
        {
            Fixture f(root, root, L"queue-partial-resume"); Write(f.source / L"a.txt", "first"); Write(f.source / L"b.txt", "remaining");
            const auto prefix = Tail(f, L"queue-prefix.txt"), tail = Tail(f, L"queue-tail.txt");
            const auto prefix_id = prefix.source.identity; auto calls = std::make_shared<std::atomic_uint>();
            FileOperationCoordinator operation({}, {}, {}, {}, {},
                [calls](const auto& request, const auto& cancelled, const auto& progress) {
                    ++*calls;
                    return MoveDirectoryMergeSafely(request, cancelled, [&](const MoveProgress& value) {
                        if (progress) { progress(value); }
                        if (value.source_files_removed == 1) { throw std::runtime_error("move merge progress failure"); }
                    });
                });
            const auto id = operation.MoveEntries({ prefix, f.Move(), tail });
            const bool first_finished = Finished(operation.CurrentAction()); const auto first = operation.Snapshot();
            const bool partial = first.move_batch && first.stage == FileOperationStage::Failed && first.move_batch->resumable &&
                first.move_batch->moved_count == 1 && first.move_batch->partial_count == 1 && first.move_batch->remaining_count == 1 &&
                first.move_result && first.move_result->source_files_removed == 1;
            blink::core::FileIdentity moved_id;
            if (partial) { moved_id = Entry(f.target / L"a.txt").identity; }
            const bool replay_rejected = !operation.ResumeMoveBatch(id + 1, FileCopyConflictPolicy::Skip) &&
                !operation.ResumeMoveBatch(id, FileCopyConflictPolicy::Merge) && !operation.ResumeMoveBatch(id, FileCopyConflictPolicy::KeepBoth);
            const auto resumed = operation.ResumeMoveBatch(id, FileCopyConflictPolicy::Skip);
            const bool finished = Finished(operation.CurrentAction()); const auto result = operation.Snapshot();
            passed &= Check(id && first_finished && partial && replay_rejected && resumed > id && finished && calls->load() == 1 &&
                result.stage == FileOperationStage::Completed && result.move_batch && result.move_batch->moved_count == 2 &&
                result.move_batch->skipped_count == 1 && result.move_batch->partial_count == 1 &&
                result.move_batch->items[1].retained_result && result.move_batch->items[1].retained_result->source_files_removed == 1 &&
                Entry(f.target_parent / L"queue-prefix.txt").identity == prefix_id && Entry(f.target / L"a.txt").identity == moved_id &&
                !fs::exists(f.source / L"a.txt") && Read(f.source / L"b.txt") == "remaining" && !fs::exists(f.target / L"b.txt") &&
                !fs::exists(f.source_parent / L"queue-tail.txt") && Read(f.target_parent / L"queue-tail.txt") == "queue-tail" &&
                !operation.ResumeMoveBatch(id, FileCopyConflictPolicy::Skip),
                "directory_move_merge_partial_retry_only_skips_to_tail_and_never_replays_committed_items");
        }
        return passed;
    }
}

bool RunDirectoryMoveMergeTests()
{
    bool passed = true;
    const auto suffix = L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto local = fs::absolute(fs::path(L".build") / L"directory-move-merge-fixtures" / suffix);
    OwnedRoot local_cleanup{ local };
    try
    {
        fs::create_directories(local);
        passed &= RunFilesystemCases(local / L"same", local / L"same", false);
        passed &= RunQueueCases(local / L"queue");
        wchar_t temp[32768]{};
        if (!GetTempPathW(static_cast<DWORD>(std::size(temp)), temp))
        { return Check(false, "directory_move_merge_get_other_volume_temp_path") && passed; }
        const auto other = fs::path(temp) / (L"BlinkDirectoryMoveMerge-" + suffix);
        OwnedRoot other_cleanup{ other };
        std::error_code error; fs::create_directories(other, error);
        if (error)
        {
            if (error == std::errc::permission_denied || error.value() == ERROR_PRIVILEGE_NOT_HELD)
            { std::cout << "SKIP directory_move_merge_cross_volume_fixture_permission " << error.value() << '\n'; return passed; }
            return Check(false, "directory_move_merge_other_volume_fixture_created") && passed;
        }
        if (Entry(local).identity.volume_serial == Entry(other).identity.volume_serial)
        { std::cout << "SKIP directory_move_merge_real_other_volume_unavailable\n"; return passed; }
        passed &= RunFilesystemCases(local / L"cross", other, true);
    }
    catch (const std::exception& error)
    {
        std::cout << "INFO directory_move_merge_fixture_exception " << error.what() << '\n';
        passed &= Check(false, "directory_move_merge_fixture_completed_without_exception");
    }
    catch (...) { passed &= Check(false, "directory_move_merge_fixture_completed_without_unknown_exception"); }
    return passed;
}
