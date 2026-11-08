#include "FileOperationCoordinatorTests.h"
#include "Blink/App/FileOperationCoordinator.h"
#include <future>
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <optional>
#include <fstream>
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
        const auto done = std::make_shared<std::promise<Status>>(); auto result = done->get_future();
        action.Completed([done](auto&&, Status status) { done->set_value(status); });
        return result.wait_for(std::chrono::seconds(5)) == std::future_status::ready && result.get() == Status::Completed;
    }
    struct Gate
    {
        std::promise<void> entered, release;
        std::future<void> started{ entered.get_future() };
        std::shared_future<void> proceed{ release.get_future().share() };
    };
}

bool RunFileOperationCoordinatorTests()
{
    using namespace blink::app;
    bool passed = true;
    const NewFolderRequest request{ L"D:\\owned", { 77, { 7 }, true }, L"folder" };
    {
        const auto gate = std::make_shared<Gate>();
        auto published = std::make_shared<std::atomic_uint>(0);
        const auto ui_thread = GetCurrentThreadId();
        auto worker_thread = std::make_shared<std::atomic<DWORD>>(0);
        FileOperationCoordinator operation([gate, worker_thread](auto&&, const auto& cancelled) {
            worker_thread->store(GetCurrentThreadId()); gate->entered.set_value(); gate->proceed.wait();
            return NewFolderResult{ static_cast<DWORD>(cancelled->load() ? ERROR_CANCELLED : ERROR_SUCCESS), !cancelled->load() };
        });
        const auto id = operation.CreateFolder(request, [published](auto) { ++*published; });
        const bool started = gate->started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        passed &= Check(id != 0 && started && worker_thread->load() != ui_thread && operation.IsBusy(),
            "file_operation_runs_on_owned_worker_without_ui_io");
        passed &= Check(operation.CreateFolder(request) == 0,
            "file_operation_rejects_second_worker_while_first_is_owned");
        operation.Cancel();
        passed &= Check(operation.Snapshot().stage == FileOperationStage::Cancelling && operation.IsBusy(),
            "file_operation_cancel_keeps_native_work_owned_until_return");
        gate->release.set_value();
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Cancelled &&
            operation.Snapshot().result.error == ERROR_CANCELLED && published->load() == 1 && !operation.IsBusy(),
            "file_operation_cooperative_cancel_publishes_actual_no_commit_result");
    }
    {
        const auto gate = std::make_shared<Gate>();
        auto published = std::make_shared<std::atomic_uint>(0);
        FileOperationCoordinator operation([gate](auto&&, auto&&) {
            gate->entered.set_value(); gate->proceed.wait(); return NewFolderResult{ ERROR_SUCCESS, true };
        });
        (void)operation.CreateFolder(request, [published](auto) { ++*published; });
        const bool started = gate->started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        const auto before = std::chrono::steady_clock::now(); operation.Shutdown();
        const auto elapsed = std::chrono::steady_clock::now() - before;
        passed &= Check(started && elapsed < std::chrono::milliseconds(250) && operation.Snapshot().closed &&
            operation.CreateFolder(request) == 0,
            "file_operation_shutdown_does_not_wait_for_blocked_executor");
        gate->release.set_value();
        passed &= Check(Finished(operation.CurrentAction()) && published->load() == 0 &&
            operation.Snapshot().stage == FileOperationStage::Completed && operation.Snapshot().result.committed,
            "file_operation_shutdown_suppresses_ui_but_preserves_committed_truth");
    }
    {
        const auto gate = std::make_shared<Gate>();
        FileOperationCoordinator operation([gate](auto&&, auto&&) {
            gate->entered.set_value(); gate->proceed.wait(); return NewFolderResult{ ERROR_SUCCESS, true };
        });
        (void)operation.CreateFolder(request);
        const bool started = gate->started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        operation.Cancel(); gate->release.set_value();
        passed &= Check(started && Finished(operation.CurrentAction()) && operation.Snapshot().cancel_requested &&
            operation.Snapshot().stage == FileOperationStage::Completed && operation.Snapshot().result.error == ERROR_SUCCESS,
            "file_operation_cancel_after_commit_does_not_claim_rollback");
    }
    {
        auto first_result = std::make_shared<std::optional<FileOperationSnapshot>>();
        FileOperationCoordinator operation([](auto&&, auto&&) { return NewFolderResult{ ERROR_ACCESS_DENIED }; });
        const auto first = operation.CreateFolder(request, [first_result](auto result) { *first_result = std::move(result); });
        const bool first_done = Finished(operation.CurrentAction());
        auto retry = request; retry.name = L"new-name";
        const auto second = operation.CreateFolder(retry);
        passed &= Check(first_done && first != 0 && second > first && Finished(operation.CurrentAction()) &&
            operation.Snapshot().stage == FileOperationStage::Failed && operation.Snapshot().result.error == ERROR_ACCESS_DENIED,
            "file_operation_failure_preserved_and_explicit_retry_gets_new_id");
        passed &= Check(*first_result && (*first_result)->operation_id == first && (*first_result)->request.name == request.name &&
            operation.Snapshot().request.name == retry.name,
            "file_operation_immutable_completion_survives_later_request");
    }
    {
        FileOperationCoordinator operation([](auto&&, auto&&) -> NewFolderResult { throw std::runtime_error("owned failure"); });
        (void)operation.CreateFolder(request);
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Failed &&
            operation.Snapshot().result.error == ERROR_GEN_FAILURE,
            "file_operation_executor_exception_becomes_visible_failure");
    }
    {
        FileOperationCoordinator operation([](auto&&, auto&&) { return NewFolderResult{ ERROR_SUCCESS, true }; });
        (void)operation.CreateFolder(request, [](auto) { throw std::runtime_error("owned callback failure"); });
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Completed,
            "file_operation_callback_failure_cannot_reverse_commit");
    }
    {
        const auto gate = std::make_shared<Gate>(); auto published = std::make_shared<std::atomic_uint>(0);
        auto operation = std::make_unique<FileOperationCoordinator>([gate](auto&&, auto&&) {
            gate->entered.set_value(); gate->proceed.wait(); return NewFolderResult{ ERROR_CANCELLED };
        });
        (void)operation->CreateFolder(request, [published](auto) { ++*published; });
        const bool started = gate->started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        const auto work = operation->CurrentAction(); operation.reset(); gate->release.set_value();
        passed &= Check(started && Finished(work) && published->load() == 0,
            "file_operation_worker_outlives_owner_without_stale_ui_callback");
    }
    {
        const auto root = std::filesystem::absolute(std::filesystem::path(L".build") / L"file-operation-coordinator-fixtures" /
            (L"pid-" + std::to_wstring(GetCurrentProcessId())));
        std::filesystem::create_directories(root);
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path, e); } } cleanup{root};
        blink::core::FileEntry parent;
        (void)blink::core::ReadEntryMetadata(root.wstring(), parent);
        FileOperationCoordinator operation;
        const auto id = operation.CreateFolder({ root.wstring(), parent.identity, L"真实后台目录" });
        passed &= Check(id != 0 && Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Completed &&
            std::filesystem::is_directory(root / L"真实后台目录"),
            "file_operation_default_worker_creates_real_ordinary_user_directory");
        const auto source_path = root / L"source.txt";
        { std::ofstream file(source_path); file << "owned rename"; }
        blink::core::FileEntry source;
        (void)blink::core::ReadEntryMetadata(source_path.wstring(), source);
        source.name = L"source.txt"; source.full_path = source_path.wstring();
        const auto rename_id = operation.Rename({ root.wstring(), parent.identity, source, L"renamed.txt" });
        passed &= Check(rename_id > id && Finished(operation.CurrentAction()) &&
            operation.Snapshot().kind == FileOperationKind::Rename && operation.Snapshot().result.committed &&
            operation.Snapshot().rename_source && operation.Snapshot().rename_source->identity == source.identity &&
            !std::filesystem::exists(source_path) && std::filesystem::exists(root / L"renamed.txt"),
            "file_operation_rename_runs_through_shared_owned_coordinator");
        source.name = L"renamed.txt"; source.full_path = (root / source.name).wstring();
        (void)operation.Rename({ root.wstring(), parent.identity, source, source.name });
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Completed &&
            operation.Snapshot().result.error == ERROR_SUCCESS && !operation.Snapshot().result.committed,
            "file_operation_unchanged_rename_completes_without_fake_commit");
        { std::ofstream file(root / L"exists.txt"); file << "preserve target"; }
        (void)operation.Rename({ root.wstring(), parent.identity, source, L"exists.txt" });
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Failed &&
            !operation.Snapshot().result.committed && std::filesystem::exists(root / L"renamed.txt") &&
            std::filesystem::file_size(root / L"exists.txt") == 15,
            "file_operation_rename_conflict_keeps_both_originals");
        const auto copied_path = root / L"copied.txt";
        const auto copy_id = operation.Copy({ root.wstring(), parent.identity, source,
            root.wstring(), parent.identity, L"copied.txt" });
        passed &= Check(copy_id > rename_id && Finished(operation.CurrentAction()) &&
            operation.Snapshot().kind == FileOperationKind::Copy && operation.Snapshot().result.committed &&
            operation.Snapshot().copy_request && std::filesystem::exists(root / L"renamed.txt") &&
            std::filesystem::file_size(copied_path) == 12 &&
            operation.Snapshot().copy_progress.total_bytes == 12 && operation.Snapshot().copy_progress.transferred_bytes == 12,
            "file_copy_coordinator_default_worker_copies_real_file_and_preserves_source");
    }

    blink::core::FileEntry copy_source;
    copy_source.name = L"source.txt"; copy_source.full_path = L"D:\\owned\\source.txt";
    copy_source.identity = { 77, { 8 }, true };
    const FileCopyRequest copy_request{ request.parent, request.parent_identity, copy_source,
        L"D:\\destination", { 77, { 9 }, true }, L"copied.txt" };
    {
        const auto gate = std::make_shared<Gate>();
        struct Publications { std::mutex mutex; std::vector<FileOperationSnapshot> values; };
        auto publications = std::make_shared<Publications>();
        auto reentrant_reads = std::make_shared<std::atomic_uint>(0);
        FileOperationCoordinator operation([](auto&&, auto&&) { return NewFolderResult{ ERROR_SUCCESS, true }; },
            [gate](const FileCopyRequest& value, const auto&, const FileCopyProgressCallback& progress) {
                progress({ 100, 25 });
                for (unsigned i = 0; i < 1000; ++i) { progress({ 100, 25 }); }
                gate->entered.set_value(); gate->proceed.wait();
                progress({ 100, 10 }); // A stale progress report cannot move the UI backwards.
                progress({ 100, 50 }); progress({ 100, 100 });
                return FileCopyResult{ ERROR_SUCCESS, true, false, value.destination_parent + L"\\" + value.destination_name };
            });
        const auto id = operation.Copy(copy_request, [&operation, publications, reentrant_reads](auto value) {
            (void)operation.Snapshot(); ++*reentrant_reads;
            std::scoped_lock lock(publications->mutex); publications->values.push_back(std::move(value));
        });
        const bool started = gate->started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        const auto running = operation.Snapshot();
        passed &= Check(started && id != 0 && operation.IsBusy() && running.kind == FileOperationKind::Copy &&
            running.copy_request && running.copy_request->source.identity == copy_source.identity &&
            running.request.parent == copy_request.destination_parent &&
            running.request.parent_identity == copy_request.destination_parent_identity &&
            running.request.name == copy_request.destination_name,
            "file_copy_coordinator_captures_source_and_destination_for_shared_refresh");
        passed &= Check(operation.CreateFolder(request) == 0 && operation.Copy(copy_request) == 0 &&
            operation.Rename({ request.parent, request.parent_identity, copy_source, L"renamed.txt" }) == 0,
            "file_copy_coordinator_rejects_every_second_mutation_while_owned");
        {
            std::scoped_lock lock(publications->mutex);
            passed &= Check(publications->values.size() == 2 && reentrant_reads->load() == 2 &&
                publications->values.front().stage == FileOperationStage::Running &&
                publications->values.back().copy_progress.total_bytes == 100 &&
                publications->values.back().copy_progress.transferred_bytes == 25,
                "file_copy_coordinator_starts_and_merges_duplicate_progress_without_locking_callback");
        }
        gate->release.set_value();
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Completed &&
            operation.Snapshot().result.committed && operation.Snapshot().copy_progress.transferred_bytes == 100 &&
            running.copy_progress.transferred_bytes == 25,
            "file_copy_coordinator_completion_keeps_latest_progress_and_immutable_prior_snapshot");
        const auto next = operation.CreateFolder(request);
        passed &= Check(next > id && Finished(operation.CurrentAction()) && !operation.Snapshot().copy_request &&
            operation.Snapshot().copy_progress.total_bytes == 0 && operation.Snapshot().copy_progress.transferred_bytes == 0,
            "file_copy_coordinator_next_mutation_resets_copy_progress_and_context");
    }
    {
        const auto gate = std::make_shared<Gate>();
        FileOperationCoordinator operation({}, [gate](const FileCopyRequest& value, const auto& cancelled,
            const FileCopyProgressCallback& progress) {
            progress({ 100, 20 }); gate->entered.set_value(); gate->proceed.wait();
            progress({ 100, 50 });
            return FileCopyResult{ static_cast<DWORD>(cancelled->load() ? ERROR_CANCELLED : ERROR_ACCESS_DENIED),
                false, true, value.destination_parent + L"\\" + value.destination_name };
        });
        (void)operation.Copy(copy_request);
        const bool started = gate->started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        operation.Cancel();
        passed &= Check(started && operation.Snapshot().stage == FileOperationStage::Cancelling && operation.IsBusy(),
            "file_copy_coordinator_cancel_request_does_not_finish_owned_io");
        gate->release.set_value();
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Cancelled &&
            operation.Snapshot().cancel_requested && !operation.Snapshot().result.committed &&
            operation.Snapshot().result.partial_file_left && operation.Snapshot().result.error == ERROR_CANCELLED &&
            operation.Snapshot().copy_progress.transferred_bytes == 50,
            "file_copy_coordinator_reports_actual_cancel_and_owned_partial_cleanup_failure");
    }
    {
        const auto gate = std::make_shared<Gate>();
        FileOperationCoordinator operation({}, [gate](auto&&, auto&&, const FileCopyProgressCallback& progress) {
            progress({ 100, 100 }); gate->entered.set_value(); gate->proceed.wait();
            return FileCopyResult{ ERROR_CANCELLED, true, false, L"D:\\destination\\copied.txt" };
        });
        (void)operation.Copy(copy_request);
        const bool started = gate->started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        operation.Cancel(); gate->release.set_value();
        passed &= Check(started && Finished(operation.CurrentAction()) && operation.Snapshot().cancel_requested &&
            operation.Snapshot().stage == FileOperationStage::Completed && operation.Snapshot().result.committed &&
            operation.Snapshot().result.error == ERROR_SUCCESS && !operation.Snapshot().result.partial_file_left,
            "file_copy_coordinator_late_cancel_cannot_reverse_committed_copy");
    }
    {
        FileOperationCoordinator operation({}, [](auto&&, auto&&, auto&&) {
            return FileCopyResult{ ERROR_SUCCESS, false, false, L"D:\\destination\\copied.txt" };
        });
        (void)operation.Copy(copy_request);
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Failed &&
            operation.Snapshot().result.error == ERROR_GEN_FAILURE && !operation.Snapshot().result.committed,
            "file_copy_coordinator_requires_native_commit_for_success");
    }
    {
        FileOperationCoordinator operation({}, [](auto&&, auto&&, auto&&) -> FileCopyResult {
            throw std::runtime_error("owned copy executor failure");
        });
        (void)operation.Copy(copy_request);
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Failed &&
            operation.Snapshot().result.error == ERROR_GEN_FAILURE,
            "file_copy_coordinator_executor_exception_becomes_visible_failure");
    }
    {
        FileOperationCoordinator operation({}, [](auto&&, auto&&, const FileCopyProgressCallback& progress) {
            progress({ 100, 25 }); progress({ 100, 100 });
            return FileCopyResult{ ERROR_SUCCESS, true };
        });
        (void)operation.Copy(copy_request, [](auto) { throw std::runtime_error("owned copy UI callback failure"); });
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Completed &&
            operation.Snapshot().result.committed,
            "file_copy_coordinator_ui_callback_exception_isolated_from_native_progress_and_commit");
    }
    {
        const auto gate = std::make_shared<Gate>();
        auto late_progress = std::make_shared<FileCopyProgressCallback>();
        auto published = std::make_shared<std::atomic_uint>(0);
        auto operation = std::make_unique<FileOperationCoordinator>(FileOperationCoordinator::ExecuteFunction{},
            [gate, late_progress](auto&&, auto&&, const FileCopyProgressCallback& progress) {
                *late_progress = progress; progress({ 100, 10 }); gate->entered.set_value(); gate->proceed.wait();
                progress({ 100, 50 }); return FileCopyResult{ ERROR_SUCCESS, true };
            });
        (void)operation->Copy(copy_request, [published](auto) { ++*published; });
        const bool started = gate->started.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        const auto work = operation->CurrentAction(); const auto before = std::chrono::steady_clock::now();
        operation->Shutdown();
        const auto elapsed = std::chrono::steady_clock::now() - before;
        const auto count_at_close = published->load();
        passed &= Check(started && elapsed < std::chrono::milliseconds(250) && operation->Snapshot().closed &&
            operation->Copy(copy_request) == 0,
            "file_copy_coordinator_shutdown_immediately_closes_without_waiting_for_native_io");
        operation.reset(); gate->release.set_value();
        const bool finished = Finished(work);
        if (*late_progress) { (*late_progress)({ 100, 100 }); }
        passed &= Check(finished && published->load() == count_at_close,
            "file_copy_coordinator_destroyed_owner_suppresses_progress_and_completion");
    }
    {
        auto late_progress = std::make_shared<FileCopyProgressCallback>();
        auto published = std::make_shared<std::atomic_uint>(0);
        FileOperationCoordinator operation({}, [late_progress](auto&&, auto&&, const FileCopyProgressCallback& progress) {
            *late_progress = progress; progress({ 100, 100 }); return FileCopyResult{ ERROR_SUCCESS, true };
        });
        (void)operation.Copy(copy_request, [published](auto) { ++*published; });
        const bool finished = Finished(operation.CurrentAction());
        const auto count_at_finish = published->load();
        if (*late_progress) { (*late_progress)({ 1000, 1000 }); }
        passed &= Check(finished && operation.Snapshot().copy_progress.total_bytes == 100 &&
            operation.Snapshot().stage == FileOperationStage::Completed && published->load() == count_at_finish,
            "file_copy_coordinator_stale_progress_cannot_mutate_or_republish_terminal_state");
        const auto old_id = operation.Snapshot().operation_id;
        const auto first_progress = *late_progress;
        auto next = copy_request; next.destination_name = L"second.txt";
        const auto next_id = operation.Copy(next);
        const bool next_finished = Finished(operation.CurrentAction());
        if (first_progress) { first_progress({ 1000, 1000 }); }
        passed &= Check(next_finished && next_id > old_id && operation.Snapshot().copy_request &&
            operation.Snapshot().copy_request->destination_name == next.destination_name &&
            operation.Snapshot().copy_progress.total_bytes == 100,
            "file_copy_coordinator_explicit_retry_has_new_immutable_source_and_destination_context");
    }
    {
        FileOperationCoordinator operation({}, [](const FileCopyRequest& value, auto&&, auto&&) {
            return FileCopyResult{ ERROR_SUCCESS, false, false,
                value.destination_parent + L"\\" + value.destination_name, true };
        });
        auto skip_request = copy_request; skip_request.conflict_policy = FileCopyConflictPolicy::Skip;
        std::optional<FileOperationSnapshot> completion;
        (void)operation.Copy(skip_request, [&completion](auto snapshot) {
            if (snapshot.stage == FileOperationStage::Skipped) { completion = std::move(snapshot); }
        });
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Skipped &&
            operation.Snapshot().result.skipped && !operation.Snapshot().result.committed &&
            !operation.Snapshot().result.partial_file_left && operation.Snapshot().result.error == ERROR_SUCCESS,
            "file_copy_coordinator_skip_is_a_distinct_terminal_without_fake_copy");
        passed &= Check(completion && completion->copy_request &&
            completion->copy_request->conflict_policy == FileCopyConflictPolicy::Skip &&
            completion->request.parent_identity == skip_request.destination_parent_identity,
            "file_copy_coordinator_skip_publishes_the_explicit_choice_and_destination");
        operation.Cancel();
        passed &= Check(operation.Snapshot().stage == FileOperationStage::Skipped && !operation.Snapshot().cancel_requested,
            "file_copy_coordinator_late_cancel_preserves_completed_skip");
    }
    {
        FileOperationCoordinator operation({}, [](auto&&, auto&&, auto&&) {
            return FileCopyResult{ ERROR_CANCELLED, false, false, {}, true };
        });
        (void)operation.Copy(copy_request);
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Cancelled &&
            !operation.Snapshot().result.skipped && operation.Snapshot().result.error == ERROR_CANCELLED,
            "file_copy_coordinator_invalid_skip_flag_cannot_hide_actual_cancellation");
    }
    {
        FileOperationCoordinator operation({}, [](auto&&, auto&&, auto&&) {
            return FileCopyResult{ ERROR_SUCCESS, false, true, {}, true };
        });
        (void)operation.Copy(copy_request);
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Failed &&
            !operation.Snapshot().result.skipped && operation.Snapshot().result.partial_file_left,
            "file_copy_coordinator_partial_target_cannot_be_reported_as_clean_skip");
    }
    {
        FileOperationCoordinator operation([](auto&&, auto&&) {
            return FileOperationResult{ ERROR_SUCCESS, false, {}, false, true };
        });
        (void)operation.CreateFolder(request);
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Failed &&
            !operation.Snapshot().result.skipped,
            "file_operation_skip_is_only_valid_for_copy");
    }
    {
        FileOperationCoordinator operation({}, [](const FileCopyRequest& value, auto&&, auto&&) {
            return FileCopyResult{ ERROR_SUCCESS, true, false,
                value.destination_parent + L"\\kept (2).txt" };
        });
        auto keep_request = copy_request; keep_request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
        (void)operation.Copy(keep_request);
        passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().result.committed &&
            operation.Snapshot().stage == FileOperationStage::Completed && operation.Snapshot().copy_request &&
            operation.Snapshot().copy_request->conflict_policy == FileCopyConflictPolicy::KeepBoth &&
            operation.Snapshot().result.path.ends_with(L"kept (2).txt") &&
            operation.Snapshot().request.parent_identity == keep_request.destination_parent_identity,
            "file_copy_coordinator_keeps_actual_numbered_path_with_original_destination_refresh_identity");
    }

    return passed;
}
