#include "MoveCoordinatorTests.h"
#include "Blink/App/FileOperationCoordinator.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <future>
#include <chrono>

namespace
{
    using namespace blink::app;
    namespace fs = std::filesystem;
    bool Check(bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        auto done = std::make_shared<std::promise<Status>>(); auto future = done->get_future();
        action.Completed([done](auto&&, Status value) { done->set_value(value); });
        return future.wait_for(std::chrono::seconds(8)) == std::future_status::ready && future.get() == Status::Completed;
    }
    struct Gate
    {
        std::promise<void> entered, released;
        std::future<void> started{ entered.get_future() };
        std::shared_future<void> proceed{ released.get_future().share() };
        std::atomic_bool signalled{}, finished{};
        void Enter() { if (!signalled.exchange(true)) { entered.set_value(); } if (proceed.wait_for(std::chrono::seconds(8)) != std::future_status::ready) { throw 1; } }
        bool Started() { return started.wait_for(std::chrono::seconds(8)) == std::future_status::ready; }
        void Release() { if (!finished.exchange(true)) { released.set_value(); } }
        ~Gate() { Release(); }
    };
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry entry;
        if (!blink::core::ReadEntryMetadata(path.wstring(), entry)) { throw std::runtime_error("move coordinator metadata"); }
        entry.name = path.filename().wstring(); entry.full_path = path.wstring(); return entry;
    }
    std::string Read(const fs::path& path)
    { std::ifstream file(path, std::ios::binary); return { std::istreambuf_iterator<char>(file), {} }; }
    struct Fixture
    {
        fs::path root, parent, target, source;
        MoveRequest request;
        Fixture(const fs::path& suite, const wchar_t* name) : root(suite / name), parent(root / L"source"), target(root / L"target"), source(parent / L"data.txt")
        {
            fs::create_directories(parent); fs::create_directory(target); std::ofstream(source) << "owned-data";
            request = { parent.wstring(), Entry(parent).identity, Entry(source), target.wstring(), Entry(target).identity, L"data.txt" };
        }
        bool Preserved() const { return fs::exists(source) && Entry(source).identity == request.source.identity && Read(source) == "owned-data"; }
        bool Moved() const { return !fs::exists(source) && fs::exists(target / L"data.txt") && Entry(target / L"data.txt").identity == request.source.identity && Read(target / L"data.txt") == "owned-data"; }
    };
}
bool RunMoveCoordinatorTests()
{
    bool passed = true;
    const auto root = fs::absolute(fs::path(L".build") / L"move-coordinator-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    fs::create_directories(root);
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code error; fs::remove_all(root, error); } } cleanup{ root };
    try
    {
        {
            Fixture f(root, L"native"); FileOperationCoordinator operation; const auto id = operation.MoveEntry(f.request);
            const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.kind == FileOperationKind::Move && value.stage == FileOperationStage::Completed &&
                value.move_request && value.move_result && value.move_result->moved && value.result.committed && !value.result.partial_file_left &&
                !value.copy_batch && !value.copy_request && !value.directory_copy_request && f.Moved(),
                "move_coordinator_real_native_commit_uses_unique_action_and_independent_result");
        }
        {
            Fixture f(root, L"cancel"); auto gate = std::make_shared<Gate>();
            FileOperationCoordinator operation({}, {}, {}, [gate](const auto& request, const auto& cancel, const auto& progress) {
                gate->Enter(); return MoveEntrySafely(request, cancel, progress);
            });
            const auto id = operation.MoveEntry(f.request); const bool entered = gate->Started(); operation.Cancel(); gate->Release();
            const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && entered && done && value.stage == FileOperationStage::Cancelled && value.move_result &&
                !value.move_result->moved && !value.result.committed && f.Preserved(), "move_coordinator_precommit_cancel_is_not_a_move_or_rollback");
        }
        {
            Fixture f(root, L"late-cancel"); FileOperationCoordinator* current{};
            FileOperationCoordinator operation({}, {}, {}, [&](const auto& request, const auto& cancel, const auto& progress) {
                return MoveEntrySafely(request, cancel, [&](const MoveProgress& value) {
                    if (progress) { progress(value); } if (value.phase == MovePhase::Completed) { current->Cancel(); }
                });
            }); current = &operation;
            const auto id = operation.MoveEntry(f.request); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Completed && value.move_result && value.move_result->moved &&
                value.result.committed && value.cancel_requested && f.Moved(), "move_coordinator_actual_move_wins_over_late_cancel_request");
        }
        {
            Fixture f(root, L"terminal-action"); auto gate = std::make_shared<Gate>(); FileOperationCoordinator operation;
            const auto id = operation.MoveEntry(f.request, [gate](const auto& value) {
                if (value.stage == FileOperationStage::Completed) { gate->Enter(); }
            }); const bool entered = gate->Started(); const auto value = operation.Snapshot();
            passed &= Check(id && entered && value.result.committed && operation.IsBusy() && operation.MoveEntry(f.request) == 0 &&
                operation.CreateFolder({ f.target.wstring(), f.request.destination_parent_identity, L"blocked" }) == 0,
                "move_coordinator_terminal_publication_does_not_release_running_action");
            gate->Release(); passed &= Check(Finished(operation.CurrentAction()) && !operation.IsBusy() && f.Moved(),
                "move_coordinator_actual_native_exit_releases_admission");
        }
        {
            Fixture f(root, L"shutdown"); auto gate = std::make_shared<Gate>(); std::atomic_uint calls{};
            FileOperationCoordinator operation({}, {}, {}, [gate](const auto& request, const auto& cancel, const auto& progress) {
                gate->Enter(); return MoveEntrySafely(request, cancel, progress);
            });
            const auto id = operation.MoveEntry(f.request, [&](const auto&) { ++calls; }); const bool entered = gate->Started();
            operation.Shutdown(); const auto previous = calls.load(); gate->Release(); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && entered && done && value.closed && value.stage == FileOperationStage::Cancelled && calls == previous &&
                !value.result.committed && f.Preserved(), "move_coordinator_shutdown_cancels_without_late_publication");
        }
        {
            Fixture f(root, L"old-callback"); MoveProgressCallback old, current; auto gate = std::make_shared<Gate>(); unsigned executions{};
            FileOperationCoordinator operation({}, {}, {}, [&](const auto& request, const auto& cancel, const auto& progress) {
                if (++executions == 1) { old = progress; }
                else
                {
                    current = progress;
                    progress({ MovePhase::Committing, blink::core::JoinPath(request.source_parent, request.source.name),
                        blink::core::JoinPath(request.destination_parent, request.destination_name) });
                    gate->Enter();
                }
                return MoveEntrySafely(request, cancel, progress);
            });
            const auto first = operation.MoveEntry(f.request); const bool first_done = Finished(operation.CurrentAction());
            auto back = f.request; back.source_parent = f.target.wstring(); back.source_parent_identity = f.request.destination_parent_identity;
            back.destination_parent = f.parent.wstring(); back.destination_parent_identity = f.request.source_parent_identity;
            const auto second = operation.MoveEntry(back); const bool entered = gate->Started(); const auto before = operation.Snapshot();
            if (old) { old({ MovePhase::Completed, L"old-source", L"old-destination" }); } const auto after = operation.Snapshot();
            passed &= Check(first && first_done && second > first && entered && before.move_progress.phase == after.move_progress.phase &&
                before.move_progress.destination_path == after.move_progress.destination_path && !after.result.committed,
                "move_coordinator_old_generation_progress_cannot_complete_new_move");
            if (current) { current({ static_cast<MovePhase>(99), L"invalid", L"invalid" }); }
            const auto unknown = operation.Snapshot();
            passed &= Check(entered && current && unknown.move_progress.phase == before.move_progress.phase &&
                unknown.move_progress.destination_path == before.move_progress.destination_path,
                "move_coordinator_unknown_phase_cannot_mutate_running_progress");
            if (current) { current({ MovePhase::Preparing, L"regression", L"regression" }); }
            const auto regressed = operation.Snapshot();
            passed &= Check(entered && current && regressed.move_progress.phase == before.move_progress.phase &&
                regressed.move_progress.destination_path == before.move_progress.destination_path,
                "move_coordinator_progress_phase_never_regresses_to_preparing");
            if (current) { current({ MovePhase::Committing, L"source", std::wstring(40000, L'x') }); }
            passed &= Check(entered && current && operation.Snapshot().move_progress.destination_path == before.move_progress.destination_path,
                "move_coordinator_progress_paths_are_bounded_before_publication");
            gate->Release(); passed &= Check(Finished(operation.CurrentAction()) && f.Preserved(), "move_coordinator_new_generation_finishes_after_old_callback_rejected");
        }
        {
            Fixture f(root, L"occupied"); std::ofstream(f.target / L"data.txt") << "incumbent"; const auto incumbent = Entry(f.target / L"data.txt").identity;
            FileOperationCoordinator operation; const auto id = operation.MoveEntry(f.request); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Failed && value.move_result && !value.result.committed &&
                f.Preserved() && Entry(f.target / L"data.txt").identity == incumbent && Read(f.target / L"data.txt") == "incumbent",
                "move_coordinator_existing_target_failure_preserves_both_objects");
        }
        {
            Fixture f(root, L"directory-native"); const auto source = f.parent / L"folder";
            fs::create_directory(source); std::ofstream(source / L"child") << "directory-data";
            const auto identity = Entry(source).identity; auto request = f.request; request.source = Entry(source); request.destination_name = L"folder";
            FileOperationCoordinator operation; const auto id = operation.MoveEntry(request); const bool done = Finished(operation.CurrentAction());
            const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Completed && value.move_result && value.move_result->moved &&
                value.move_request && value.move_request->source.is_directory && value.result.committed && !value.directory_copy_request &&
                !value.directory_copy_result && !value.copy_batch && !value.result.partial_file_left && !fs::exists(source) &&
                Entry(f.target / L"folder").identity == identity && Read(f.target / L"folder" / L"child") == "directory-data" && f.Preserved(),
                "move_coordinator_directory_rename_keeps_independent_move_facts_and_nested_content");
        }
        {
            Fixture f(root, L"postcommit-warning");
            FileOperationCoordinator operation({}, {}, {}, [](const auto& request, const auto& cancel, const auto& progress) {
                auto result = MoveEntrySafely(request, cancel, progress);
                if (result.moved) { result.post_commit_error = ERROR_FILE_INVALID; }
                return result;
            });
            const auto id = operation.MoveEntry(f.request); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Completed && value.result.committed && value.result.error == 0 &&
                value.move_result && value.move_result->moved && value.move_result->post_commit_error == ERROR_FILE_INVALID && f.Moved(),
                "move_coordinator_postcommit_warning_never_erases_native_commit");
        }
        for (unsigned invalid = 0; invalid < 4; ++invalid)
        {
            Fixture f(root, (L"invalid-result-" + std::to_wstring(invalid)).c_str());
            FileOperationCoordinator operation({}, {}, {}, [invalid](const auto&, const auto&, const auto& progress) {
                if (progress) { progress({ MovePhase::Completed, L"source", L"target" }); }
                MoveResult result; result.error = invalid == 2 ? ERROR_CANCELLED : ERROR_SUCCESS; result.skipped = invalid == 0; result.unchanged = invalid == 3; return result;
            });
            const auto id = operation.MoveEntry(f.request); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.move_result && !value.result.committed && !value.result.skipped &&
                value.stage == (invalid == 2 ? FileOperationStage::Cancelled : FileOperationStage::Failed) &&
                value.result.error == (invalid == 2 ? ERROR_CANCELLED : ERROR_GEN_FAILURE) && f.Preserved(),
                "move_coordinator_progress_or_invalid_skip_never_substitutes_for_real_commit");
        }
        {
            Fixture f(root, L"noop"); auto request = f.request; request.destination_parent = f.parent.wstring();
            request.destination_parent_identity = f.request.source_parent_identity; FileOperationCoordinator operation;
            const auto id = operation.MoveEntry(request); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Completed && value.move_result && value.move_result->unchanged &&
                !value.result.committed && f.Preserved(), "move_coordinator_noop_is_completed_without_fabricated_mutation");
        }
        {
            MoveRequest request; request.conflict_policy = FileCopyConflictPolicy::Skip; FileOperationCoordinator operation;
            const auto id = operation.MoveEntry(request); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Skipped && value.move_result && value.move_result->skipped &&
                value.result.skipped && !value.result.committed, "move_coordinator_explicit_skip_keeps_independent_skipped_outcome");
        }
        {
            Fixture f(root, L"exception"); FileOperationCoordinator operation({}, {}, {}, [](const auto&, const auto&, const auto&) -> MoveResult {
                throw std::runtime_error("owned pre-I/O executor");
            });
            const auto id = operation.MoveEntry(f.request); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Failed && value.move_result && value.result.error == ERROR_GEN_FAILURE &&
                !value.result.committed && f.Preserved(), "move_coordinator_executor_exception_does_not_escape_native_action");
        }
        {
            Fixture f(root, L"pending-copy"); std::ofstream(f.target / L"data.txt") << "occupied"; FileOperationCoordinator operation;
            auto second = f.request; second.destination_name = L"second.txt";
            const auto id = operation.CopyBatch({ f.request, second }); const bool done = Finished(operation.CurrentAction());
            const auto attempted = operation.MoveEntry(f.request);
            passed &= Check(id && done && operation.HasPendingCopyBatch() && !attempted && operation.Snapshot().operation_id == id && f.Preserved(),
                "move_coordinator_cannot_replace_unresolved_copy_queue");
        }
    }
    catch (const std::exception& e)
    { std::cout << "INFO move_coordinator_fixture_exception " << e.what() << '\n'; passed &= Check(false, "move_coordinator_owned_fixture_no_exception"); }
    return passed;
}
