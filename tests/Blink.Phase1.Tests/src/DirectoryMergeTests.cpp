#include "DirectoryMergeTests.h"
#include "Blink/App/FileOperationCoordinator.h"
#include <chrono>
#include <condition_variable>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
    namespace fs = std::filesystem;
    using namespace blink::app;
    bool Check(const bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    fs::path Extended(const fs::path& path)
    { return path.wstring().starts_with(L"\\\\?\\") ? path : fs::path(L"\\\\?\\" + path.wstring()); }
    void Write(const fs::path& path, const std::string& data)
    {
        std::ofstream stream(Extended(path), std::ios::binary); stream << data;
        if (!stream) { throw std::runtime_error("directory merge fixture write"); }
    }
    std::string Read(const fs::path& path)
    { std::ifstream stream(Extended(path), std::ios::binary); return { std::istreambuf_iterator<char>(stream), {} }; }
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry value;
        if (!blink::core::ReadEntryMetadata(Extended(path).wstring(), value))
        { throw std::runtime_error("directory merge fixture metadata"); }
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
            { if (!(value.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { Cleanup(child); } SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL); RemoveDirectoryW(child.c_str()); }
            else { SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(child.c_str()); }
        } while (FindNextFileW(find, &value));
        FindClose(find);
    }
    struct OwnedRoot
    {
        fs::path root;
        ~OwnedRoot() { Cleanup(root); RemoveDirectoryW(Extended(root).c_str()); }
    };
    struct Fixture
    {
        fs::path root, source_parent, target_parent, source, target;
        Fixture(const fs::path& suite, const wchar_t* name) : root(suite / name), source_parent(root / L"来源"),
            target_parent(root / L"目标"), source(source_parent / L"内容目录"), target(target_parent / L"内容目录")
        { fs::create_directories(Extended(source)); fs::create_directories(Extended(target)); }
        DirectoryCopyRequest Request() const
        {
            DirectoryCopyRequest result{ source_parent.wstring(), Entry(source_parent).identity, Entry(source),
                target_parent.wstring(), Entry(target_parent).identity, target.filename().wstring() };
            result.conflict_policy = FileCopyConflictPolicy::Merge; return result;
        }
        void AddConflict(const wchar_t* name = L"same.txt") const
        { Write(source / name, "new-content"); Write(target / name, "old-content"); }
    };
    bool NameConflict(const DWORD error)
    { return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS; }
    void Info(const char* name, const DirectoryCopyRequest& request, const DirectoryCopyResult& result)
    {
        std::cout << "INFO directory_merge_" << name << " error=" << result.error << " copied=" << result.copied <<
            " partial=" << result.partial_tree_left << " files=" << result.files_copied << " dirs=" << result.directories_created <<
            " source.attrs=" << request.source.attributes << " source.identity.valid=" << request.source.identity.valid << '\n';
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
        std::vector<FileOperationSnapshot> conflicts;
        void Publish(const FileOperationSnapshot& value)
        {
            if (!value.directory_conflict) { return; }
            {
                std::scoped_lock lock(mutex);
                if (!conflicts.empty() && conflicts.back().operation_id == value.operation_id &&
                    conflicts.back().directory_conflict_id == value.directory_conflict_id) { return; }
                conflicts.push_back(value);
            }
            ready.notify_all();
        }
        std::optional<FileOperationSnapshot> Next(const std::size_t index)
        {
            std::unique_lock lock(mutex);
            if (!ready.wait_for(lock, std::chrono::seconds(8), [&] { return conflicts.size() > index; })) { return {}; }
            return conflicts[index];
        }
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
        { throw std::runtime_error("directory merge fixture basic metadata"); }
        return value;
    }
}

bool RunDirectoryMergeTests()
{
    bool passed = true;
    const auto root = fs::absolute(fs::path(L".build") / L"directory-merge-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    fs::create_directories(root); OwnedRoot cleanup{ root };
    try
    {
        {
            Fixture f(root, L"existing-root"); Write(f.source / L"new.txt", "source"); Write(f.target / L"keep.txt", "existing");
            const auto root_id = Entry(f.target).identity, keep_id = Entry(f.target / L"keep.txt").identity;
            unsigned decisions{}; auto request = f.Request();
            request.resolve_conflict = [&](const auto&) { ++decisions; return FileCopyConflictPolicy::Cancel; };
            const auto result = CopyDirectorySafely(request);
            Info("existing_root", request, result);
            passed &= Check(result.copied && !result.error && !result.partial_tree_left && result.files_copied == 1 &&
                !result.directories_created && !decisions && Entry(f.target).identity == root_id &&
                Entry(f.target / L"keep.txt").identity == keep_id && Read(f.target / L"keep.txt") == "existing" &&
                Read(f.target / L"new.txt") == "source" && Read(f.source / L"new.txt") == "source",
                "merge_existing_directory_preserves_root_and_target_only_entries");
        }
        {
            Fixture f(root, L"nested"); fs::create_directories(f.source / L"shared" / L"empty"); fs::create_directory(f.target / L"shared");
            Write(f.source / L"shared" / L"new.txt", "nested"); Write(f.target / L"shared" / L"keep.txt", "keep");
            const auto shared_id = Entry(f.target / L"shared").identity; const auto request = f.Request(); const auto result = CopyDirectorySafely(request);
            Info("nested", request, result);
            passed &= Check(result.copied && !result.error && result.files_copied == 1 && result.directories_created == 1 &&
                Entry(f.target / L"shared").identity == shared_id && Read(f.target / L"shared" / L"keep.txt") == "keep" &&
                Read(f.target / L"shared" / L"new.txt") == "nested" && fs::is_directory(f.target / L"shared" / L"empty"),
                "merge_recurses_into_existing_subdirectories_and_creates_missing_empty_directory");
        }
        {
            Fixture f(root, L"replace"); f.AddConflict(); Write(f.source / L"tail.txt", "tail");
            Write((f.source / L"same.txt").wstring() + L":new-stream", "new-ads");
            Write((f.target / L"same.txt").wstring() + L":old-stream", "old-ads");
            const auto old_id = Entry(f.target / L"same.txt").identity; unsigned decisions{}; bool context{};
            auto request = f.Request(); request.resolve_conflict = [&](const auto& value) {
                ++decisions; context = !value.request.source.is_directory && !value.destination_is_directory &&
                    value.request.source.name == L"same.txt" && value.request.source_parent == f.source.wstring() &&
                    value.request.destination_parent == f.target.wstring() && value.request.destination_name == L"same.txt";
                return FileCopyConflictPolicy::Replace;
            };
            const auto result = CopyDirectorySafely(request);
            passed &= Check(result.copied && !result.error && result.files_copied == 2 && decisions == 1 && context &&
                Read(f.target / L"same.txt") == "new-content" && Entry(f.target / L"same.txt").identity != old_id &&
                Read((f.target / L"same.txt").wstring() + L":new-stream") == "new-ads" &&
                Read((f.target / L"same.txt").wstring() + L":old-stream").empty() && Read(f.target / L"tail.txt") == "tail" &&
                Read(f.source / L"same.txt") == "new-content", "merge_file_conflict_replace_uses_existing_copy_engine_and_continues_tail");
        }
        {
            Fixture f(root, L"skip-file"); f.AddConflict(); Write(f.source / L"tail.txt", "tail"); const auto old_id = Entry(f.target / L"same.txt").identity;
            auto request = f.Request(); request.resolve_conflict = [](const auto&) { return FileCopyConflictPolicy::Skip; };
            DirectoryCopyProgress last; const auto result = CopyDirectorySafely(request, {}, [&](const auto& value) { last = value; });
            passed &= Check(result.copied && !result.error && result.files_copied == 1 && result.files_skipped == 1 &&
                last.completed && last.files_skipped == 1 && Entry(f.target / L"same.txt").identity == old_id &&
                Read(f.target / L"same.txt") == "old-content" && Read(f.target / L"tail.txt") == "tail" &&
                Read(f.source / L"same.txt") == "new-content", "merge_skip_file_preserves_existing_content_and_reports_skip");
        }
        {
            Fixture f(root, L"keep-both-file"); f.AddConflict(); Write(f.target / L"same (2).txt", "occupied");
            auto request = f.Request(); request.resolve_conflict = [](const auto&) { return FileCopyConflictPolicy::KeepBoth; };
            const auto result = CopyDirectorySafely(request);
            passed &= Check(result.copied && !result.error && result.files_copied == 1 &&
                Read(f.target / L"same.txt") == "old-content" && Read(f.target / L"same (2).txt") == "occupied" &&
                Read(f.target / L"same (3).txt") == "new-content", "merge_keep_both_file_claims_next_available_name");
        }
        {
            Fixture f(root, L"cancel-prefix"); Write(f.source / L"a-prefix.txt", "prefix"); f.AddConflict(L"b-conflict.txt"); Write(f.source / L"c-tail.txt", "tail");
            const auto old_id = Entry(f.target / L"b-conflict.txt").identity;
            auto request = f.Request(); request.resolve_conflict = [](const auto&) { return FileCopyConflictPolicy::Cancel; };
            const auto result = CopyDirectorySafely(request);
            passed &= Check(!result.copied && result.error == ERROR_CANCELLED && result.partial_tree_left && result.files_copied == 1 &&
                Read(f.target / L"a-prefix.txt") == "prefix" && Read(f.target / L"b-conflict.txt") == "old-content" &&
                Entry(f.target / L"b-conflict.txt").identity == old_id && !fs::exists(f.target / L"c-tail.txt") &&
                Read(f.source / L"a-prefix.txt") == "prefix", "merge_cancel_at_child_conflict_keeps_committed_prefix_and_unstarted_tail");
        }
        {
            Fixture f(root, L"throw-prefix"); Write(f.source / L"a-prefix.txt", "prefix"); f.AddConflict(L"b-conflict.txt");
            auto request = f.Request(); request.resolve_conflict = [](const auto&) -> FileCopyConflictPolicy { throw std::runtime_error("decision"); };
            const auto result = CopyDirectorySafely(request);
            passed &= Check(!result.copied && result.error == ERROR_UNHANDLED_EXCEPTION && result.partial_tree_left && result.files_copied == 1 &&
                Read(f.target / L"a-prefix.txt") == "prefix" && Read(f.target / L"b-conflict.txt") == "old-content",
                "merge_conflict_callback_exception_reports_real_partial_result");
        }
        {
            Fixture f(root, L"no-callback"); f.AddConflict(); const auto old_id = Entry(f.target / L"same.txt").identity;
            const auto result = CopyDirectorySafely(f.Request());
            passed &= Check(!result.copied && NameConflict(result.error) && !result.partial_tree_left && !result.files_copied &&
                !result.directories_created && Entry(f.target / L"same.txt").identity == old_id && Read(f.target / L"same.txt") == "old-content",
                "merge_without_child_decision_never_implicitly_overwrites_a_file");
        }
        {
            Fixture f(root, L"file-directory-skip"); Write(f.source / L"same", "file"); fs::create_directory(f.target / L"same");
            Write(f.target / L"same" / L"keep", "sentinel"); bool context{}; auto request = f.Request();
            request.resolve_conflict = [&](const auto& value) { context = !value.request.source.is_directory && value.destination_is_directory; return FileCopyConflictPolicy::Skip; };
            const auto result = CopyDirectorySafely(request);
            passed &= Check(result.copied && !result.error && context && result.files_skipped == 1 && !result.files_copied &&
                fs::is_directory(f.target / L"same") && Read(f.target / L"same" / L"keep") == "sentinel" && Read(f.source / L"same") == "file",
                "merge_type_conflict_skip_keeps_existing_directory_and_source_file");
        }
        {
            Fixture f(root, L"directory-file-keep-both"); fs::create_directories(f.source / L"same" / L"nested");
            Write(f.source / L"same" / L"nested" / L"new.txt", "child"); Write(f.target / L"same", "old-file"); bool context{};
            auto request = f.Request(); request.resolve_conflict = [&](const auto& value) {
                context = value.request.source.is_directory && !value.destination_is_directory; return FileCopyConflictPolicy::KeepBoth;
            };
            const auto result = CopyDirectorySafely(request);
            passed &= Check(result.copied && !result.error && context && result.files_copied == 1 && result.directories_created == 2 &&
                Read(f.target / L"same") == "old-file" && Read(f.target / L"same (2)" / L"nested" / L"new.txt") == "child",
                "merge_keep_both_directory_rebinds_all_descendants_without_replacing_a_file");
        }
        {
            Fixture f(root, L"skip-subtree"); fs::create_directories(f.source / L"same" / L"nested"); Write(f.source / L"same" / L"nested" / L"new.txt", "child");
            Write(f.target / L"same", "old-file"); Write(f.source / L"tail.txt", "tail");
            auto request = f.Request(); request.resolve_conflict = [](const auto&) { return FileCopyConflictPolicy::Skip; };
            const auto result = CopyDirectorySafely(request);
            passed &= Check(result.copied && !result.error && result.files_copied == 1 && result.files_skipped == 1 &&
                result.directories_skipped == 2 && !result.directories_created && Read(f.target / L"same") == "old-file" &&
                Read(f.target / L"tail.txt") == "tail" && Read(f.source / L"same" / L"nested" / L"new.txt") == "child",
                "merge_skip_directory_conflict_skips_entire_source_subtree_and_counts_it");
        }
        {
            Fixture f(root, L"invalid-type-replace"); Write(f.source / L"same", "file"); fs::create_directory(f.target / L"same"); Write(f.target / L"same" / L"keep", "sentinel");
            auto request = f.Request(); request.resolve_conflict = [](const auto&) { return FileCopyConflictPolicy::Replace; };
            const auto result = CopyDirectorySafely(request);
            passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_tree_left &&
                Read(f.target / L"same" / L"keep") == "sentinel" && Read(f.source / L"same") == "file",
                "merge_type_mismatch_cannot_replace_a_directory_with_a_file");
        }
        {
            Fixture f(root, L"metadata"); fs::create_directory(f.source / L"shared"); fs::create_directory(f.target / L"shared");
            Write(f.source / L"shared" / L"new.txt", "child");
            const bool prepared = SetCreation(f.source, 132537600000000000ULL) && SetCreation(f.source / L"shared", 132537600000000000ULL) &&
                SetCreation(f.target, 129067776000000000ULL) && SetCreation(f.target / L"shared", 129067776000000000ULL) &&
                SetFileAttributesW(Extended(f.target).c_str(), FILE_ATTRIBUTE_HIDDEN) && SetFileAttributesW(Extended(f.target / L"shared").c_str(), FILE_ATTRIBUTE_HIDDEN);
            const auto before = Basic(f.target), nested_before = Basic(f.target / L"shared"); const auto result = CopyDirectorySafely(f.Request());
            const auto after = Basic(f.target), nested_after = Basic(f.target / L"shared");
            passed &= Check(prepared && result.copied && !result.error && after.CreationTime.QuadPart == before.CreationTime.QuadPart &&
                nested_after.CreationTime.QuadPart == nested_before.CreationTime.QuadPart && (after.FileAttributes & FILE_ATTRIBUTE_HIDDEN) &&
                (nested_after.FileAttributes & FILE_ATTRIBUTE_HIDDEN) && Read(f.target / L"shared" / L"new.txt") == "child",
                "merge_preserves_existing_directory_creation_time_and_attributes");
        }
        {
            Fixture f(root, L"missing-root"); fs::remove(f.target); Write(f.source / L"new.txt", "source"); const auto request = f.Request(); const auto result = CopyDirectorySafely(request);
            Info("missing_root", request, result);
            passed &= Check(result.copied && !result.error && result.directories_created == 1 && result.files_copied == 1 && Read(f.target / L"new.txt") == "source",
                "merge_also_creates_a_missing_destination_directory");
        }
        {
            Fixture f(root, L"empty"); const auto old_id = Entry(f.target).identity; const auto request = f.Request(); const auto result = CopyDirectorySafely(request);
            Info("empty", request, result);
            passed &= Check(result.copied && !result.error && !result.partial_tree_left && !result.files_copied && !result.directories_created &&
                Entry(f.target).identity == old_id, "merge_empty_existing_directory_finishes_without_recreating_it");
        }
        {
            Fixture f(root, L"coordinator-decisions"); Write(f.source / L"a-prefix.txt", "prefix"); f.AddConflict(L"b-first.txt");
            f.AddConflict(L"c-second.txt"); Write(f.source / L"d-tail.txt", "tail");
            const auto inbox = std::make_shared<ConflictInbox>(); FileOperationCoordinator operation;
            const auto id = operation.CopyItems(CopyItemRequests{ f.Request() }, [inbox](const auto& value) { inbox->Publish(value); });
            const auto action = operation.CurrentAction(); const auto first = inbox->Next(0);
            bool first_valid{}, stale_rejected{}, second_valid{}; blink::core::FileIdentity prefix_id;
            if (first)
            {
                prefix_id = Entry(f.target / L"a-prefix.txt").identity;
                stale_rejected = !operation.ResolveDirectoryConflict(id + 1, first->directory_conflict_id, FileCopyConflictPolicy::Replace) &&
                    !operation.ResolveDirectoryConflict(id, first->directory_conflict_id + 1, FileCopyConflictPolicy::Replace);
                first_valid = first->operation_id == id && first->stage == FileOperationStage::Running && operation.IsBusy() &&
                    first->directory_conflict->request.destination_name == L"b-first.txt" &&
                    operation.ResolveDirectoryConflict(id, first->directory_conflict_id, FileCopyConflictPolicy::KeepBoth);
                const auto second = inbox->Next(1);
                if (second)
                {
                    stale_rejected = stale_rejected && !operation.ResolveDirectoryConflict(id, first->directory_conflict_id, FileCopyConflictPolicy::Replace);
                    second_valid = second->directory_conflict_id != first->directory_conflict_id &&
                        second->directory_conflict->request.destination_name == L"c-second.txt" &&
                        operation.ResolveDirectoryConflict(id, second->directory_conflict_id, FileCopyConflictPolicy::Skip);
                }
            }
            if (!first_valid || !second_valid) { operation.Cancel(); }
            const bool finished = Finished(action); const auto result = operation.Snapshot();
            std::cout << "INFO directory_merge_coordinator_decisions first=" << first.has_value() << " first_valid=" << first_valid <<
                " second_valid=" << second_valid << " finished=" << finished << " stage=" << static_cast<unsigned>(result.stage) <<
                " error=" << result.result.error << " directory_error=" << (result.directory_copy_result ? result.directory_copy_result->error : 0) << '\n';
            passed &= Check(id && first_valid && second_valid && stale_rejected && finished && result.stage == FileOperationStage::Completed &&
                result.copy_batch && result.copy_batch->copied_count == 1 && !result.directory_conflict && result.directory_copy_result &&
                result.directory_copy_result->files_copied == 3 && result.directory_copy_result->files_skipped == 1 &&
                Entry(f.target / L"a-prefix.txt").identity == prefix_id && Read(f.target / L"b-first (2).txt") == "new-content" &&
                Read(f.target / L"b-first.txt") == "old-content" && Read(f.target / L"c-second.txt") == "old-content" &&
                Read(f.target / L"d-tail.txt") == "tail", "merge_coordinator_waits_for_each_decision_rejects_stale_ids_and_never_restarts_prefix");
        }
        {
            Fixture f(root, L"coordinator-cancel"); Write(f.source / L"a-prefix.txt", "prefix"); f.AddConflict(L"b-conflict.txt"); Write(f.source / L"c-tail.txt", "tail");
            const auto inbox = std::make_shared<ConflictInbox>(); FileOperationCoordinator operation;
            const auto id = operation.CopyDirectory(f.Request(), [inbox](const auto& value) { inbox->Publish(value); });
            const auto action = operation.CurrentAction(); const auto pending = inbox->Next(0); operation.Cancel();
            const bool finished = Finished(action); const auto result = operation.Snapshot();
            passed &= Check(id && pending && finished && !operation.IsBusy() && result.stage == FileOperationStage::Cancelled &&
                !result.directory_conflict && result.directory_copy_result && result.directory_copy_result->partial_tree_left &&
                result.directory_copy_result->files_copied == 1 && Read(f.target / L"a-prefix.txt") == "prefix" &&
                Read(f.target / L"b-conflict.txt") == "old-content" && !fs::exists(f.target / L"c-tail.txt"),
                "merge_coordinator_cancel_wakes_waiting_worker_and_reports_committed_prefix");
        }
        {
            Fixture f(root, L"coordinator-close"); f.AddConflict(); const auto inbox = std::make_shared<ConflictInbox>(); FileOperationCoordinator operation;
            const auto id = operation.CopyDirectory(f.Request(), [inbox](const auto& value) { inbox->Publish(value); });
            const auto action = operation.CurrentAction(); const auto pending = inbox->Next(0); operation.Shutdown();
            const bool finished = Finished(action); const auto result = operation.Snapshot();
            passed &= Check(id && pending && finished && result.closed && !operation.ResolveDirectoryConflict(id,
                pending->directory_conflict_id, FileCopyConflictPolicy::Replace) && Read(f.target / L"same.txt") == "old-content" &&
                Read(f.source / L"same.txt") == "new-content", "merge_coordinator_shutdown_releases_pending_decision_without_changing_existing_file");
        }
        {
            Fixture f(root, L"coordinator-publish-throw"); f.AddConflict(); const auto attempted = std::make_shared<std::atomic_bool>();
            FileOperationCoordinator operation;
            const auto id = operation.CopyDirectory(f.Request(), [attempted](const auto& value) {
                if (value.directory_conflict) { attempted->store(true); throw std::runtime_error("conflict publication"); }
            });
            const bool finished = Finished(operation.CurrentAction()); const auto result = operation.Snapshot();
            passed &= Check(id && attempted->load() && finished && !operation.IsBusy() && result.stage == FileOperationStage::Cancelled &&
                !result.directory_conflict && result.directory_copy_result && !result.directory_copy_result->partial_tree_left &&
                Read(f.target / L"same.txt") == "old-content" && Read(f.source / L"same.txt") == "new-content",
                "merge_coordinator_failed_conflict_publication_cancels_without_leaving_worker_waiting");
        }
        {
            Fixture f(root, L"coordinator-partial-resume"); Write(f.source / L"a-prefix.txt", "prefix"); f.AddConflict(L"b-conflict.txt");
            Write(f.source_parent / L"queue-tail.txt", "queue-tail"); const auto merge_calls = std::make_shared<std::atomic_uint>();
            FileOperationCoordinator operation({}, {}, [merge_calls](const auto& request, const auto& cancelled, const auto& progress) {
                if (request.conflict_policy == FileCopyConflictPolicy::Merge) { ++*merge_calls; }
                return CopyDirectorySafely(request, cancelled, [&](const DirectoryCopyProgress& value) {
                    progress(value);
                    if (value.files_copied == 1 && !value.completed) { throw std::runtime_error("directory progress"); }
                });
            });
            const FileCopyRequest tail{ f.source_parent.wstring(), Entry(f.source_parent).identity, Entry(f.source_parent / L"queue-tail.txt"),
                f.target_parent.wstring(), Entry(f.target_parent).identity, L"after-merge.txt" };
            const auto first_id = operation.CopyItems(CopyItemRequests{ f.Request(), tail });
            const bool first_finished = Finished(operation.CurrentAction()); const auto first = operation.Snapshot();
            const bool partial = first.stage == FileOperationStage::Failed && first.directory_copy_result && first.directory_copy_result->partial_tree_left &&
                first.directory_copy_result->files_copied == 1 && first.copy_batch && first.copy_batch->resumable && first.copy_batch->remaining_count == 1;
            blink::core::FileIdentity prefix_id;
            if (partial) { prefix_id = Entry(f.target / L"a-prefix.txt").identity; }
            const bool repeat_rejected = !operation.ResumeCopyBatch(first_id, FileCopyConflictPolicy::Merge) &&
                !operation.ResumeCopyBatch(first_id, FileCopyConflictPolicy::KeepBoth);
            const auto resumed = operation.ResumeCopyBatch(first_id, FileCopyConflictPolicy::Skip);
            const bool finished = Finished(operation.CurrentAction()); const auto result = operation.Snapshot();
            passed &= Check(first_id && first_finished && partial && repeat_rejected && resumed && finished && merge_calls->load() == 1 &&
                result.stage == FileOperationStage::Completed && result.copy_batch && result.copy_batch->copied_count == 1 &&
                result.copy_batch->skipped_count == 1 && result.copy_batch->partial_count == 1 &&
                result.copy_batch->items[0].previous_directory_results.size() == 1 &&
                result.copy_batch->items[0].previous_directory_results[0].files_copied == 1 &&
                Entry(f.target / L"a-prefix.txt").identity == prefix_id && Read(f.target / L"b-conflict.txt") == "old-content" &&
                Read(f.target_parent / L"after-merge.txt") == "queue-tail", "merge_partial_failure_can_skip_to_queue_tail_without_reexecuting_committed_prefix");
        }
    }
    catch (const std::exception& error)
    { std::cout << "INFO directory_merge_exception " << error.what() << '\n'; passed &= Check(false, "directory_merge_fixture_completed_without_exception"); }
    catch (...)
    { passed &= Check(false, "directory_merge_fixture_completed_without_unknown_exception"); }
    return passed;
}
