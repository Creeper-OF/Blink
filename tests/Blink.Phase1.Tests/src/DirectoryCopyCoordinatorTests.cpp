#include "DirectoryCopyCoordinatorTests.h"
#include "Blink/App/FileOperationCoordinator.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace blink::app;
    bool Check(bool condition, const char* name)
    { std::cout << (condition ? "PASS " : "FAIL ") << name << '\n'; return condition; }

    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        auto done = std::make_shared<std::promise<Status>>(); auto result = done->get_future();
        action.Completed([done](auto&&, Status status) { done->set_value(status); });
        return result.wait_for(std::chrono::seconds(5)) == std::future_status::ready && result.get() == Status::Completed;
    }

    struct Gate
    {
        std::promise<void> entered, release;
        std::future<void> started{ entered.get_future() };
        std::shared_future<void> proceed{ release.get_future().share() };
        std::atomic_bool signalled{}, released{};
        ~Gate() { Release(); }
        void Enter()
        {
            if (!signalled.exchange(true)) { entered.set_value(); }
            if (proceed.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
            { throw std::runtime_error("owned directory coordinator gate timed out"); }
        }
        bool Started() { return started.wait_for(std::chrono::seconds(5)) == std::future_status::ready; }
        void Release() noexcept
        { if (!released.exchange(true)) { try { release.set_value(); } catch (...) {} } }
    };

    template<class T> struct Box
    {
        mutable std::mutex mutex;
        std::optional<T> value;
        void Put(T input) { std::scoped_lock lock(mutex); value = std::move(input); }
        std::optional<T> Get() const { std::scoped_lock lock(mutex); return value; }
    };

    blink::core::FileEntry Entry(const std::filesystem::path& path)
    {
        blink::core::FileEntry entry;
        if (!blink::core::ReadEntryMetadata(path.wstring(), entry))
        { throw std::runtime_error("cannot read owned directory coordinator fixture metadata"); }
        entry.name = path.filename().wstring(); entry.full_path = path.wstring(); return entry;
    }
    void Write(const std::filesystem::path& path, const std::string& value)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(value.data(), static_cast<std::streamsize>(value.size()));
        if (!file) { throw std::runtime_error("cannot write owned directory coordinator fixture"); }
    }
    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    }
    bool Exists(const std::filesystem::path& path)
    { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

    struct FileOracle
    {
        std::filesystem::path path;
        blink::core::FileIdentity identity;
        std::string data;
        bool Matches() const { return Exists(path) && Entry(path).identity == identity && Read(path) == data; }
    };
    FileOracle Oracle(const std::filesystem::path& path) { return { path, Entry(path).identity, Read(path) }; }

    void CleanupOwnedTree(const std::filesystem::path& root) noexcept
    {
        WIN32_FIND_DATAW entry{}; const auto found = FindFirstFileW((root / L"*").c_str(), &entry);
        if (found == INVALID_HANDLE_VALUE) { return; }
        do
        {
            if (!std::wcscmp(entry.cFileName, L".") || !std::wcscmp(entry.cFileName, L"..")) { continue; }
            const auto path = root / entry.cFileName;
            const bool reparse = (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!reparse) { CleanupOwnedTree(path); (void)SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); }
                (void)RemoveDirectoryW(path.c_str());
            }
            else
            {
                if (!reparse) { (void)SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); }
                (void)DeleteFileW(path.c_str());
            }
        } while (FindNextFileW(found, &entry));
        FindClose(found);
    }

    struct Fixture
    {
        std::filesystem::path root, source_parent, source, destination_parent, destination;
        std::vector<FileOracle> files;
        Fixture(const std::filesystem::path& suite, const wchar_t* label, bool large = false)
            : root(suite / label), source_parent(root / L"源父目录"), source(source_parent / L"源目录"),
            destination_parent(root / L"目标父目录"), destination(destination_parent / L"复制目录")
        {
            std::filesystem::create_directories(source / L"子目录" / L"空目录");
            std::filesystem::create_directories(destination_parent);
            Write(source / L"文件 {0}.txt", large ? std::string(3 * 1024 * 1024 + 17, 'q') : "owned-root-data");
            Write((source / L"文件 {0}.txt").wstring() + L":Zone.Identifier:$DATA", "[ZoneTransfer]\r\nZoneId=3\r\n");
            Write(source / L"子目录" / L"资料.txt", "owned-nested-data");
            files.push_back(Oracle(source / L"文件 {0}.txt")); files.push_back(Oracle(source / L"子目录" / L"资料.txt"));
        }
        DirectoryCopyRequest Request() const
        { return { source_parent.wstring(), Entry(source_parent).identity, Entry(source), destination_parent.wstring(),
            Entry(destination_parent).identity, destination.filename().wstring() }; }
        bool SourcesPreserved() const
        { return std::all_of(files.begin(), files.end(), [](const auto& file) { return file.Matches(); }) &&
            Read(files.front().path.wstring() + L":Zone.Identifier:$DATA") == "[ZoneTransfer]\r\nZoneId=3\r\n"; }
        bool DestinationMatches(const std::filesystem::path& target) const
        {
            return std::filesystem::is_directory(target / L"子目录" / L"空目录") &&
                std::all_of(files.begin(), files.end(), [&](const auto& file) {
                    const auto copied = target / file.path.lexically_relative(source);
                    return Exists(copied) && Read(copied) == file.data && Entry(copied).identity != file.identity;
                }) && Read((target / files.front().path.filename()).wstring() + L":Zone.Identifier:$DATA") ==
                    "[ZoneTransfer]\r\nZoneId=3\r\n";
        }
        FileCopyRequest FileRequest(const wchar_t* leaf) const
        { return { source.wstring(), Entry(source).identity, Entry(files.front().path), destination_parent.wstring(),
            Entry(destination_parent).identity, leaf }; }
    };

    bool SameResult(const DirectoryCopyResult& left, const DirectoryCopyResult& right)
    {
        return left.error == right.error && left.copied == right.copied && left.partial_tree_left == right.partial_tree_left &&
            left.path == right.path && left.files_copied == right.files_copied &&
            left.directories_created == right.directories_created && left.failure_path == right.failure_path;
    }
    bool NoFileContext(const FileOperationSnapshot& value)
    { return !value.copy_request && !value.copy_batch && !value.rename_source &&
        value.copy_progress.total_bytes == 0 && value.copy_progress.transferred_bytes == 0; }
    std::pair<std::uint64_t, std::uint64_t> ActualObjects(const std::filesystem::path& root)
    {
        if (!Exists(root)) { return {}; }
        std::uint64_t files{}, directories{ 1 };
        for (const auto& child : std::filesystem::recursive_directory_iterator(root))
        {
            const auto attributes = GetFileAttributesW(child.path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            { throw std::runtime_error("unexpected owned directory coordinator result entry"); }
            if (attributes & FILE_ATTRIBUTE_DIRECTORY) { ++directories; } else { ++files; }
        }
        return { files, directories };
    }
}

bool RunDirectoryCopyCoordinatorTests()
{
    using namespace blink::app;
    bool passed = true;
    const auto root = std::filesystem::absolute(std::filesystem::path(L".build") / L"directory-copy-coordinator-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    std::filesystem::create_directories(root);
    struct Cleanup
    {
        std::filesystem::path root;
        ~Cleanup()
        {
            const auto extended = std::filesystem::path(L"\\\\?\\" + root.wstring());
            CleanupOwnedTree(extended); (void)RemoveDirectoryW(extended.c_str());
        }
    } cleanup{ root };
    try
    {
        {
            Fixture fixture(root, L"default-real-engine"); auto request = fixture.Request(); request.limits.max_entries = 8;
            auto publications = std::make_shared<std::atomic_uint>(0);
            FileOperationCoordinator operation;
            const auto id = operation.CopyDirectory(request, [publications](const auto&) {
                ++*publications; throw std::runtime_error("owned directory UI publication failure");
            });
            const bool finished = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id != 0 && finished && value.kind == FileOperationKind::DirectoryCopy &&
                value.stage == FileOperationStage::Completed && value.directory_copy_request && value.directory_copy_result &&
                value.directory_copy_request->source.identity == request.source.identity &&
                value.directory_copy_request->destination_parent_identity == request.destination_parent_identity &&
                value.directory_copy_request->limits.max_entries == 8 && value.request.parent == request.destination_parent &&
                value.request.parent_identity == request.destination_parent_identity && value.request.name == request.destination_name &&
                value.directory_copy_result->copied && value.directory_copy_result->error == ERROR_SUCCESS &&
                value.directory_copy_result->files_copied == 2 && value.directory_copy_result->directories_created == 3 &&
                !value.directory_copy_result->partial_tree_left && value.directory_copy_progress.completed &&
                value.result.committed && value.result.error == ERROR_SUCCESS && !value.result.partial_file_left && !value.result.skipped &&
                value.result.path == value.directory_copy_result->path && NoFileContext(value) && publications->load() > 0 &&
                fixture.DestinationMatches(fixture.destination) && fixture.SourcesPreserved(),
                "directory_coordinator_default_native_engine_copies_owned_tree_ads_and_preserves_independent_truth_despite_ui_throw");
        }
        {
            Fixture fixture(root, L"precreate-cancel"); auto gate = std::make_shared<Gate>();
            auto raw = std::make_shared<Box<DirectoryCopyResult>>(); auto worker = std::make_shared<std::atomic<DWORD>>(0);
            FileOperationCoordinator operation({}, {}, [gate, raw, worker](const auto& request, const auto& cancelled, const auto& progress) {
                worker->store(GetCurrentThreadId());
                auto result = CopyDirectorySafely(request, cancelled, [&](const DirectoryCopyProgress& value) {
                    progress(value); if (!value.directories_created && !value.completed) { gate->Enter(); }
                }); raw->Put(result); return result;
            });
            const auto caller = GetCurrentThreadId(); const auto id = operation.CopyDirectory(fixture.Request());
            const bool started = gate->Started(); const auto running = operation.Snapshot();
            const auto file_request = fixture.FileRequest(L"must-not-copy.txt");
            passed &= Check(started && id != 0 && worker->load() != caller && operation.IsBusy() && !Exists(fixture.destination) &&
                running.directory_copy_request && !running.directory_copy_result && NoFileContext(running) &&
                operation.CopyDirectory(fixture.Request()) == 0 && operation.Copy(file_request) == 0 &&
                operation.CopyBatch({ file_request }) == 0 &&
                operation.CreateFolder({ fixture.destination_parent.wstring(), Entry(fixture.destination_parent).identity, L"must-not-create" }) == 0 &&
                operation.Rename({ fixture.source.wstring(), Entry(fixture.source).identity, Entry(fixture.files.front().path), L"must-not-rename" }) == 0 &&
                operation.Snapshot().operation_id == id,
                "directory_coordinator_owns_one_worker_and_rejects_every_other_mutation_before_native_create");
            operation.Cancel();
            passed &= Check(operation.IsBusy() && operation.Snapshot().stage == FileOperationStage::Cancelling,
                "directory_coordinator_cancel_request_keeps_gated_native_action_owned");
            gate->Release(); const bool finished = Finished(operation.CurrentAction());
            const auto value = operation.Snapshot(); const auto actual = raw->Get();
            passed &= Check(finished && actual && value.directory_copy_result && SameResult(*actual, *value.directory_copy_result) &&
                value.stage == FileOperationStage::Cancelled && value.cancel_requested && value.result.error == ERROR_CANCELLED &&
                !value.result.committed && !value.result.partial_file_left && !actual->partial_tree_left &&
                actual->directories_created == 0 && actual->files_copied == 0 && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                "directory_coordinator_precreate_cancel_records_actual_zero_write_native_result");
        }
        {
            Fixture fixture(root, L"midfile-cancel", true); auto gate = std::make_shared<Gate>();
            auto raw = std::make_shared<Box<DirectoryCopyResult>>(); auto interrupted = std::make_shared<Box<std::wstring>>();
            FileOperationCoordinator operation({}, {}, [gate, raw, interrupted](const auto& request, const auto& cancelled, const auto& progress) {
                auto result = CopyDirectorySafely(request, cancelled, [&](const DirectoryCopyProgress& value) {
                    progress(value);
                    if (!gate->signalled.load() && value.current_file.transferred_bytes > 0 &&
                        value.current_file.transferred_bytes < value.current_file.total_bytes)
                    { interrupted->Put(value.current_destination_path); gate->Enter(); }
                }); raw->Put(result); return result;
            });
            const auto id = operation.CopyDirectory(fixture.Request()); const bool started = gate->Started();
            operation.Cancel(); const bool cancelling = operation.IsBusy() && operation.Snapshot().stage == FileOperationStage::Cancelling;
            gate->Release(); const bool finished = Finished(operation.CurrentAction());
            const auto value = operation.Snapshot(); const auto actual = raw->Get(); const auto path = interrupted->Get();
            const auto counts = ActualObjects(fixture.destination);
            passed &= Check(id != 0 && started && cancelling && finished && actual && path && !path->empty() &&
                value.directory_copy_result && SameResult(*actual, *value.directory_copy_result) &&
                value.stage == FileOperationStage::Cancelled && actual->error == ERROR_CANCELLED && actual->partial_tree_left &&
                actual->directories_created == counts.second && actual->files_copied == counts.first &&
                !actual->failure_path.empty() && Exists(fixture.destination) && !Exists(*path) &&
                !value.result.committed && !value.result.partial_file_left && fixture.SourcesPreserved(),
                "directory_coordinator_midfile_cancel_retains_actual_partial_tree_counts_and_file_cleanup_without_file_partial_alias");
        }
        {
            Fixture fixture(root, L"partial-native-failure"); auto raw = std::make_shared<Box<DirectoryCopyResult>>();
            auto rival = std::make_shared<Box<FileOracle>>(); auto inserted = std::make_shared<std::atomic_bool>(false);
            const auto rival_path = fixture.destination / fixture.files.front().path.filename();
            FileOperationCoordinator operation({}, {}, [raw, rival, inserted, rival_path](const auto& request, const auto& cancelled, const auto& progress) {
                auto result = CopyDirectorySafely(request, cancelled, [&](const DirectoryCopyProgress& value) {
                    progress(value);
                    if (value.directories_created && !value.completed && !inserted->exchange(true))
                    { Write(rival_path, "owned-adapter-rival"); rival->Put(Oracle(rival_path)); }
                }); raw->Put(result); return result;
            });
            const auto id = operation.CopyDirectory(fixture.Request()); const bool finished = Finished(operation.CurrentAction());
            const auto value = operation.Snapshot(); const auto actual = raw->Get(); const auto protected_file = rival->Get();
            const auto rejected_resume = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Skip); const auto after_resume = operation.Snapshot();
            passed &= Check(id != 0 && finished && inserted->load() && actual && protected_file && protected_file->Matches() &&
                value.directory_copy_result && SameResult(*actual, *value.directory_copy_result) &&
                value.stage == FileOperationStage::Failed && actual->error != ERROR_SUCCESS && actual->error != ERROR_CANCELLED &&
                actual->partial_tree_left && actual->directories_created >= 1 && !actual->failure_path.empty() &&
                std::filesystem::path(actual->failure_path).filename() == protected_file->path.filename() &&
                value.result.path == actual->path && !value.result.partial_file_left && !value.result.committed &&
                !operation.HasPendingCopyBatch() && rejected_resume == 0 && after_resume.directory_copy_result &&
                SameResult(*value.directory_copy_result, *after_resume.directory_copy_result) && fixture.SourcesPreserved(),
                "directory_coordinator_preserves_real_child_collision_partial_root_failure_path_and_rival_without_batch_resume");
        }
        {
            Fixture fixture(root, L"native-preflight-failure"); auto request = fixture.Request(); request.source.identity.file_id[0] ^= 1;
            FileOperationCoordinator operation; const auto id = operation.CopyDirectory(request);
            const bool finished = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id != 0 && finished && value.stage == FileOperationStage::Failed && value.directory_copy_result &&
                value.directory_copy_result->error == ERROR_FILE_INVALID && !value.directory_copy_result->copied &&
                !value.directory_copy_result->partial_tree_left && value.directory_copy_result->directories_created == 0 &&
                value.directory_copy_result->files_copied == 0 && value.result.error == ERROR_FILE_INVALID &&
                !value.result.partial_file_left && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                "directory_coordinator_default_engine_rechecks_captured_source_identity_before_any_write");
        }
        {
            Fixture fixture(root, L"late-cancel-native-return-gate"); auto gate = std::make_shared<Gate>();
            auto raw = std::make_shared<Box<DirectoryCopyResult>>();
            FileOperationCoordinator operation({}, {}, [gate, raw](const auto& request, const auto& cancelled, const auto& progress) {
                auto result = CopyDirectorySafely(request, cancelled, progress); raw->Put(result);
                if (result.copied) { gate->Enter(); } return result;
            });
            const auto id = operation.CopyDirectory(fixture.Request()); const bool started = gate->Started();
            const auto before = operation.Snapshot(); operation.Cancel();
            passed &= Check(id != 0 && started && before.directory_copy_progress.completed && !before.directory_copy_result &&
                before.stage == FileOperationStage::Running && operation.IsBusy() &&
                operation.CopyDirectory(fixture.Request()) == 0 && fixture.DestinationMatches(fixture.destination),
                "directory_coordinator_complete_native_progress_does_not_finish_executor_or_admit_another_action");
            gate->Release(); const bool finished = Finished(operation.CurrentAction()); const auto value = operation.Snapshot(); const auto actual = raw->Get();
            passed &= Check(finished && actual && value.directory_copy_result && SameResult(*actual, *value.directory_copy_result) &&
                value.cancel_requested && value.stage == FileOperationStage::Completed && value.result.committed &&
                value.result.error == ERROR_SUCCESS && !value.result.partial_file_left && !actual->partial_tree_left &&
                fixture.DestinationMatches(fixture.destination) && fixture.SourcesPreserved(),
                "directory_coordinator_late_cancel_preserves_actual_complete_tree_after_executor_return");
        }
        {
            Fixture fixture(root, L"terminal-publication-action-gate"); auto gate = std::make_shared<Gate>();
            FileOperationCoordinator operation;
            const auto id = operation.CopyDirectory(fixture.Request(), [gate](const auto& value) {
                if (value.stage == FileOperationStage::Completed) { gate->Enter(); }
            });
            const bool started = gate->Started(); const auto action = operation.CurrentAction(); const auto terminal = operation.Snapshot();
            passed &= Check(id != 0 && started && terminal.stage == FileOperationStage::Completed && terminal.directory_copy_result &&
                terminal.directory_copy_result->copied && operation.IsBusy() &&
                action.Status() == winrt::Windows::Foundation::AsyncStatus::Started && operation.CopyDirectory(fixture.Request()) == 0,
                "directory_coordinator_terminal_snapshot_is_not_native_action_exit_and_cannot_release_worker_admission");
            gate->Release(); const bool finished = Finished(action);
            const auto next = operation.CreateFolder({ fixture.destination_parent.wstring(), Entry(fixture.destination_parent).identity, L"下次新建目录" });
            const bool next_finished = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(finished && next > id && next_finished && value.kind == FileOperationKind::NewFolder &&
                !value.directory_copy_request && !value.directory_copy_result && !value.directory_copy_progress.completed &&
                value.directory_copy_progress.files_copied == 0 && value.directory_copy_progress.directories_created == 0 &&
                terminal.directory_copy_request && terminal.directory_copy_result && terminal.directory_copy_result->copied && fixture.SourcesPreserved(),
                "directory_coordinator_new_operation_waits_for_real_action_exit_and_clears_only_current_directory_context");
        }
        for (const bool committed : { false, true })
        {
            Fixture fixture(root, committed ? L"shutdown-after-native-commit" : L"destroy-with-created-root");
            auto gate = std::make_shared<Gate>(); auto raw = std::make_shared<Box<DirectoryCopyResult>>();
            auto publications = std::make_shared<std::atomic_uint>(0);
            auto operation = std::make_unique<FileOperationCoordinator>(FileOperationCoordinator::ExecuteFunction{},
                FileOperationCoordinator::CopyExecuteFunction{}, [gate, raw, committed](const auto& request, const auto& cancelled, const auto& progress) {
                    auto result = CopyDirectorySafely(request, cancelled, [&](const DirectoryCopyProgress& value) {
                        progress(value);
                        if (!committed && value.directories_created && !gate->signalled.load()) { gate->Enter(); }
                    }); raw->Put(result); if (committed && result.copied) { gate->Enter(); } return result;
                });
            const auto id = operation->CopyDirectory(fixture.Request(), [publications](const auto&) { ++*publications; });
            const bool started = gate->Started(); const auto action = operation->CurrentAction(); const auto count = publications->load();
            const auto before = std::chrono::steady_clock::now(); operation->Shutdown();
            const auto elapsed = std::chrono::steady_clock::now() - before;
            passed &= Check(id != 0 && started && elapsed < std::chrono::milliseconds(250) && operation->Snapshot().closed &&
                operation->IsBusy() && operation->CopyDirectory(fixture.Request()) == 0,
                committed ? "directory_coordinator_shutdown_does_not_wait_for_already_committed_native_return" :
                    "directory_coordinator_shutdown_does_not_wait_for_created_root_native_callback");
            if (!committed) { operation.reset(); }
            gate->Release(); const bool finished = Finished(action); const auto actual = raw->Get();
            const bool terminal_truth = !committed || (operation->Snapshot().closed && operation->Snapshot().directory_copy_result &&
                operation->Snapshot().stage == FileOperationStage::Completed && operation->Snapshot().directory_copy_result->copied);
            passed &= Check(finished && actual && publications->load() == count && terminal_truth && fixture.SourcesPreserved() &&
                (committed ? (actual->copied && !actual->partial_tree_left && fixture.DestinationMatches(fixture.destination)) :
                    (!actual->copied && actual->error == ERROR_CANCELLED && actual->partial_tree_left &&
                        actual->directories_created == 1 && actual->files_copied == 0 && std::filesystem::is_empty(fixture.destination))),
                committed ? "directory_coordinator_closed_owner_suppresses_publication_but_keeps_returned_committed_tree_result" :
                    "directory_coordinator_saved_action_outlives_destroyed_owner_and_retains_real_empty_partial_root");
        }
        {
            Fixture fixture(root, L"per-file-progress-reset"); auto gate = std::make_shared<Gate>();
            auto prior = std::make_shared<Box<FileOperationSnapshot>>(); auto reentrant = std::make_shared<std::atomic_uint>(0);
            const auto source_a = (fixture.source / L"A.bin").wstring(), source_b = (fixture.source / L"B.bin").wstring();
            const auto target_a = (fixture.destination / L"A.bin").wstring(), target_b = (fixture.destination / L"B.bin").wstring();
            FileOperationCoordinator operation({}, {}, [gate, source_a, source_b, target_a, target_b](const auto& request, const auto&, const auto& progress) {
                progress({ 1, 1, source_a, target_a, { 100, 100 }, false });
                progress({ 1, 1, source_b, target_b, { 20, 0 }, false });
                for (unsigned index = 0; index < 1000; ++index) { progress({ 1, 1, source_b, target_b, { 20, 0 }, false }); }
                gate->Enter(); progress({ 2, 1, source_b, target_b, { 20, 20 }, true });
                return DirectoryCopyResult{ ERROR_SUCCESS, true, false,
                    request.destination_parent + L"\\" + request.destination_name, 2, 1, {} };
            });
            const auto id = operation.CopyDirectory(fixture.Request(), [&operation, prior, reentrant, target_a](const auto& value) {
                (void)operation.Snapshot(); ++*reentrant;
                if (value.directory_copy_progress.current_destination_path == target_a) { prior->Put(value); }
            });
            const bool started = gate->Started(); const auto value = operation.Snapshot(); const auto previous = prior->Get();
            passed &= Check(id != 0 && started && value.stage == FileOperationStage::Running &&
                value.directory_copy_progress.files_copied == 1 && value.directory_copy_progress.directories_created == 1 &&
                value.directory_copy_progress.current_source_path == source_b && value.directory_copy_progress.current_destination_path == target_b &&
                value.directory_copy_progress.current_file.total_bytes == 20 && value.directory_copy_progress.current_file.transferred_bytes == 0 &&
                previous && previous->directory_copy_progress.current_destination_path == target_a &&
                previous->directory_copy_progress.current_file.transferred_bytes == 100 && reentrant->load() > 0 && !Exists(fixture.destination),
                "directory_coordinator_progress_resets_current_file_bytes_on_path_change_and_keeps_prior_value_snapshot");
            gate->Release(); passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Completed,
                "directory_coordinator_progress_only_fixture_finishes_without_reentrant_publication_deadlock");
        }
        {
            Fixture fixture(root, L"old-progress-across-kinds");
            auto directory_callback = std::make_shared<Box<DirectoryCopyProgressCallback>>();
            auto file_callback = std::make_shared<Box<FileCopyProgressCallback>>();
            auto file_gate = std::make_shared<Gate>(); auto directory_gate = std::make_shared<Gate>();
            auto directory_calls = std::make_shared<std::atomic_uint>(0); auto publications = std::make_shared<std::atomic_uint>(0);
            FileOperationCoordinator operation({}, [file_callback, file_gate](const auto& request, const auto& cancelled, const auto& progress) {
                file_callback->Put(progress); file_gate->Enter(); return CopyFileSafely(request, cancelled, progress);
            }, [directory_callback, directory_gate, directory_calls](const auto& request, const auto& cancelled, const auto& progress) {
                const auto number = ++*directory_calls; directory_callback->Put(progress);
                return CopyDirectorySafely(request, cancelled, [&](const DirectoryCopyProgress& value) {
                    progress(value); if (number == 2 && !value.directories_created && !directory_gate->signalled.load()) { directory_gate->Enter(); }
                });
            });
            const auto publish = [publications](const auto&) { ++*publications; };
            const auto first = operation.CopyDirectory(fixture.Request(), publish); const bool first_finished = Finished(operation.CurrentAction());
            const auto old_directory = directory_callback->Get(); const auto first_snapshot = operation.Snapshot(); const auto first_count = publications->load();
            if (old_directory) { (*old_directory)({ 999, 999, L"old-source", L"old-target", { 999, 999 }, true }); }
            passed &= Check(first != 0 && first_finished && old_directory && first_snapshot.directory_copy_result && operation.Snapshot().directory_copy_result &&
                SameResult(*first_snapshot.directory_copy_result, *operation.Snapshot().directory_copy_result) && publications->load() == first_count,
                "directory_coordinator_saved_progress_cannot_mutate_or_republish_terminal_directory_result");
            const auto second = operation.CopyBatch({ fixture.FileRequest(L"独立复制文件.txt") }, publish); const bool file_started = file_gate->Started();
            const auto file_before = operation.Snapshot(); const auto file_count = publications->load();
            if (old_directory) { (*old_directory)({ 999, 999, L"old-source", L"old-target", { 999, 999 }, true }); }
            const auto file_after = operation.Snapshot();
            passed &= Check(second > first && file_started && file_after.operation_id == second && file_after.kind == FileOperationKind::Copy &&
                file_before.copy_batch && file_after.copy_batch && !file_after.directory_copy_request && !file_after.directory_copy_result &&
                file_after.directory_copy_progress.directories_created == 0 && file_before.copy_batch->current_index == file_after.copy_batch->current_index &&
                file_after.copy_batch->copied_count == 0 && publications->load() == file_count,
                "directory_coordinator_old_directory_callback_cannot_pollute_new_running_file_batch");
            file_gate->Release(); const bool file_finished = Finished(operation.CurrentAction()); const auto old_file = file_callback->Get();
            auto next = fixture.Request(); next.destination_name = L"第二复制目录";
            const auto third = operation.CopyDirectory(next, publish); const bool directory_started = directory_gate->Started();
            const auto directory_before = operation.Snapshot(); const auto directory_count = publications->load();
            if (old_file) { (*old_file)({ 777, 777 }); }
            const auto directory_after = operation.Snapshot();
            passed &= Check(file_finished && old_file && third > second && directory_started &&
                directory_after.kind == FileOperationKind::DirectoryCopy && directory_after.operation_id == third && NoFileContext(directory_after) &&
                directory_after.directory_copy_progress.current_destination_path == directory_before.directory_copy_progress.current_destination_path &&
                directory_after.directory_copy_progress.directories_created == 0 && publications->load() == directory_count,
                "directory_coordinator_old_file_callback_cannot_forge_progress_in_new_running_directory");
            directory_gate->Release(); passed &= Check(Finished(operation.CurrentAction()) &&
                fixture.DestinationMatches(fixture.destination_parent / next.destination_name) && fixture.SourcesPreserved(),
                "directory_coordinator_new_native_operation_completes_normally_after_rejected_cross_kind_callbacks");
        }
        {
            Fixture fixture(root, L"pending-file-batch-protection"); FileOperationCoordinator operation;
            auto first = fixture.FileRequest(L"批量第一项.txt"), collision = fixture.FileRequest(L"批量冲突.txt"), tail = fixture.FileRequest(L"批量尾项.txt");
            const auto existing_path = fixture.destination_parent / collision.destination_name; Write(existing_path, "owned-existing-batch-target");
            const auto existing = Oracle(existing_path);
            const auto id = operation.CopyBatch({ first, collision, tail }); const bool finished = Finished(operation.CurrentAction()); const auto before = operation.Snapshot();
            const auto rejected = operation.CopyDirectory(fixture.Request()); const auto after = operation.Snapshot();
            passed &= Check(id != 0 && finished && before.copy_batch && before.copy_batch->resumable && before.copy_batch->copied_count == 1 &&
                before.copy_batch->remaining_count == 1 && rejected == 0 && after.operation_id == id && after.kind == FileOperationKind::Copy &&
                after.copy_batch && after.copy_batch->current_index == before.copy_batch->current_index &&
                !after.directory_copy_request && !after.directory_copy_result && !Exists(fixture.destination) &&
                !Exists(fixture.destination_parent / tail.destination_name) && existing.Matches(),
                "directory_coordinator_new_entry_cannot_discard_failed_file_batch_decision_or_unstarted_tail");
            const auto cancelled = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Cancel); const bool stopped = Finished(operation.CurrentAction());
            const auto next = operation.CopyDirectory(fixture.Request()); const bool directory_finished = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(cancelled > id && stopped && next > cancelled && directory_finished &&
                value.kind == FileOperationKind::DirectoryCopy && value.directory_copy_result && value.directory_copy_result->copied && NoFileContext(value) &&
                fixture.DestinationMatches(fixture.destination) && !Exists(fixture.destination_parent / tail.destination_name) &&
                existing.Matches() && fixture.SourcesPreserved(),
                "directory_coordinator_explicit_batch_cancel_then_real_action_exit_allows_clean_directory_operation");
        }
        {
            Fixture fixture(root, L"executor-throw-after-real-create"); auto raw = std::make_shared<Box<DirectoryCopyResult>>();
            FileOperationCoordinator operation({}, {}, [raw](const auto& request, const auto& cancelled, const auto& progress) -> DirectoryCopyResult {
                auto result = CopyDirectorySafely(request, cancelled, [&](const DirectoryCopyProgress& value) {
                    progress(value); if (value.directories_created) { cancelled->store(true); }
                }); raw->Put(result); throw std::runtime_error("owned injected executor failed after effects");
            });
            const auto id = operation.CopyDirectory(fixture.Request()); const bool finished = Finished(operation.CurrentAction());
            const auto value = operation.Snapshot(); const auto actual = raw->Get();
            passed &= Check(id != 0 && finished && actual && actual->partial_tree_left && actual->directories_created == 1 &&
                value.stage == FileOperationStage::Failed && value.directory_copy_result && value.result.error == ERROR_GEN_FAILURE &&
                value.directory_copy_result->error == ERROR_GEN_FAILURE && value.directory_copy_result->partial_tree_left &&
                value.directory_copy_result->directories_created == 1 && value.directory_copy_result->files_copied == 0 &&
                !value.directory_copy_result->path.empty() && !value.directory_copy_result->failure_path.empty() &&
                !value.result.partial_file_left && !value.result.committed && std::filesystem::is_empty(fixture.destination) && fixture.SourcesPreserved(),
                "directory_coordinator_executor_exception_keeps_observed_real_creation_facts_without_claiming_cancel_or_rollback");
        }
        for (const bool bad_allocation : { false, true })
        {
            Fixture fixture(root, bad_allocation ? L"executor-bad-allocation" : L"missing-native-commit");
            FileOperationCoordinator operation({}, {}, [bad_allocation](const auto&, const auto&, const auto&) -> DirectoryCopyResult {
                if (bad_allocation) { throw std::bad_alloc(); } return { ERROR_SUCCESS, false };
            });
            const auto id = operation.CopyDirectory(fixture.Request()); const bool finished = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id != 0 && finished && value.stage == FileOperationStage::Failed && value.directory_copy_result &&
                value.directory_copy_result->error == (bad_allocation ? ERROR_NOT_ENOUGH_MEMORY : ERROR_GEN_FAILURE) &&
                !value.directory_copy_result->copied && !value.directory_copy_result->partial_tree_left &&
                !value.result.committed && !value.result.partial_file_left && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                bad_allocation ? "directory_coordinator_bad_allocation_is_independent_directory_failure_without_fake_effects" :
                    "directory_coordinator_zero_error_without_native_commit_is_failure_instead_of_false_success");
        }
    }
    catch (const std::exception& error)
    {
        std::cout << "INFO directory_coordinator_fixture_exception " << error.what() << '\n';
        passed &= Check(false, "directory_coordinator_owned_fixture_completed_without_exception");
    }
    return passed;
}

bool RunDirectoryCopyConflictCoordinatorTests()
{
    using namespace blink::app;
    bool passed = true;
    const auto root = std::filesystem::absolute(std::filesystem::path(L".build") / L"directory-conflict-coordinator" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path root; ~Cleanup() { CleanupOwnedTree(root); (void)RemoveDirectoryW(root.c_str()); } } cleanup{ root };
    {
        Fixture fixture(root, L"skip"); auto request = fixture.Request(); request.conflict_policy = FileCopyConflictPolicy::Skip;
        FileOperationCoordinator operation; const auto id = operation.CopyDirectory(request); const bool finished = Finished(operation.CurrentAction());
        const auto value = operation.Snapshot();
        passed &= Check(id && finished && value.stage == FileOperationStage::Skipped && value.directory_copy_result &&
            value.directory_copy_result->skipped && !value.directory_copy_result->copied && !value.directory_copy_result->partial_tree_left &&
            value.result.skipped && !value.result.committed && value.result.error == 0 && !Exists(fixture.destination) &&
            !operation.IsBusy() && !operation.HasPendingCopyBatch() && NoFileContext(value) && fixture.SourcesPreserved(),
            "directory_conflict_coordinator_skip_is_terminal_zero_effect_without_file_batch");
    }
    {
        Fixture fixture(root, L"keep-both"); auto request = fixture.Request(); request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
        std::filesystem::create_directory(fixture.destination); Write(fixture.destination / L"sentinel", "existing");
        const auto sentinel = Oracle(fixture.destination / L"sentinel");
        const auto selected = fixture.destination_parent / (request.destination_name + L" (2)");
        FileOperationCoordinator operation; const auto id = operation.CopyDirectory(request); const bool finished = Finished(operation.CurrentAction());
        const auto value = operation.Snapshot();
        passed &= Check(id && finished && value.stage == FileOperationStage::Completed && value.directory_copy_result &&
            value.directory_copy_result->copied && !value.directory_copy_result->skipped && value.directory_copy_result->path == selected.wstring() &&
            value.result.committed && value.result.path == selected.wstring() && fixture.DestinationMatches(selected) &&
            sentinel.Matches() && NoFileContext(value) && fixture.SourcesPreserved(),
            "directory_conflict_coordinator_publishes_actual_numbered_root_path");
    }
    {
        Fixture fixture(root, L"invalid-skip-with-effects");
        FileOperationCoordinator operation({}, {}, [](const auto& request, const auto& cancel, const auto& progress) {
            auto result = CopyDirectorySafely(request, cancel, progress);
            result.copied = false; result.skipped = true; result.error = 0; return result;
        });
        const auto id = operation.CopyDirectory(fixture.Request()); const bool finished = Finished(operation.CurrentAction());
        const auto value = operation.Snapshot();
        passed &= Check(id && finished && value.stage == FileOperationStage::Failed && value.directory_copy_result &&
            !value.directory_copy_result->skipped && value.directory_copy_result->partial_tree_left && value.result.error == ERROR_GEN_FAILURE &&
            !value.result.skipped && !value.result.committed && value.directory_copy_result->directories_created > 0 &&
            fixture.DestinationMatches(fixture.destination) && fixture.SourcesPreserved(),
            "directory_conflict_coordinator_cannot_hide_real_tree_effects_as_skipped");
    }
    return passed;
}
