#include "MoveBatchTests.h"
#include "Blink/App/FileOperationCoordinator.h"
#include "Blink/App/ClipboardFileTransfer.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace blink::app;
    using namespace std::chrono_literals;
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
        return future.wait_for(8s) == std::future_status::ready && future.get() == Status::Completed;
    }
    struct Gate
    {
        std::promise<void> entered, release;
        std::future<void> started{ entered.get_future() };
        std::shared_future<void> proceed{ release.get_future().share() };
        std::atomic_bool signalled{}, released{};
        void Enter() { if (!signalled.exchange(true)) { entered.set_value(); } if (proceed.wait_for(8s) != std::future_status::ready) { throw std::runtime_error("gate timeout"); } }
        bool Started() { return started.wait_for(8s) == std::future_status::ready; }
        void Release() { if (!released.exchange(true)) { release.set_value(); } }
        ~Gate() { Release(); }
    };
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry result;
        if (!blink::core::ReadEntryMetadata(path.wstring(), result)) { throw std::runtime_error("metadata"); }
        result.name = path.filename().wstring(); result.full_path = path.wstring(); return result;
    }
    std::string Read(const fs::path& path)
    { std::ifstream file(path, std::ios::binary); return { std::istreambuf_iterator<char>(file), {} }; }
    struct Fixture
    {
        fs::path root, source, target;
        Fixture(const fs::path& suite, const wchar_t* name) : root(suite / name), source(root / L"source"), target(root / L"target")
        {
            fs::create_directories(source / L"目录"); fs::create_directory(target);
            std::ofstream(source / L"first.txt") << "first";
            std::ofstream(source / L"目录" / L"nested.txt") << "nested";
            std::ofstream(source / L"tail.txt") << "tail";
        }
        MoveRequest Request(const wchar_t* name) const
        { return { source.wstring(), Entry(source).identity, Entry(source / name), target.wstring(), Entry(target).identity, name }; }
        std::vector<MoveRequest> Requests() const { return { Request(L"first.txt"), Request(L"目录"), Request(L"tail.txt") }; }
        bool Intact() const { return Read(source / L"first.txt") == "first" && Read(source / L"目录" / L"nested.txt") == "nested" && Read(source / L"tail.txt") == "tail" && fs::is_empty(target); }
    };
    std::vector<std::byte> Effect(DWORD value)
    { std::vector<std::byte> result(sizeof(value)); std::memcpy(result.data(), &value, sizeof(value)); return result; }
}

bool RunMoveBatchTests()
{
    bool passed = true;
    const auto root = fs::absolute(fs::path(L".build") / L"move-batch-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    fs::create_directories(root);
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code error; fs::remove_all(root, error); } } cleanup{ root };
    {
        const auto move = DecodeClipboardDropEffect(Effect(2));
        passed &= Check(move.error == ERROR_SUCCESS && move.intent == ClipboardTransferIntent::Move,
            "move_clipboard_preferred_effect_preserves_move_intent");
        passed &= Check(DecodeClipboardDropEffect(Effect(4)).error == ERROR_NOT_SUPPORTED &&
            DecodeClipboardDropEffect(Effect(3)).error == ERROR_NOT_SUPPORTED,
            "move_clipboard_links_or_ambiguous_effects_never_fall_back_to_copy");
        passed &= Check(DecodeClipboardDropEffect({}).error == ERROR_INVALID_DATA,
            "move_clipboard_present_truncated_effect_is_invalid");
        passed &= Check(DecodeClipboardDropEffect({}, false).intent == ClipboardTransferIntent::Copy &&
            DecodeClipboardDropEffect(Effect(1)).intent == ClipboardTransferIntent::Copy,
            "move_clipboard_absent_effect_keeps_legacy_copy_default");
    }
    try
    {
        {
            Fixture f(root, L"mixed"); auto requests = f.Requests(); const auto original = requests;
            std::ofstream(f.source / L"first.txt:owned") << "ads";
            FileOperationCoordinator operation; const auto id = operation.MoveEntries(std::move(requests));
            const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.kind == FileOperationKind::Move && value.stage == FileOperationStage::Completed &&
                value.move_batch && value.move_batch->moved_count == 3 && value.move_batch->remaining_count == 0 &&
                !value.copy_batch && !value.copy_request && !value.directory_copy_request &&
                !fs::exists(f.source / L"first.txt") && !fs::exists(f.source / L"目录") &&
                Entry(f.target / L"first.txt").identity == original[0].source.identity &&
                Entry(f.target / L"目录").identity == original[1].source.identity && Read(f.target / L"first.txt:owned") == "ads" &&
                Read(f.target / L"目录" / L"nested.txt") == "nested" && Read(f.target / L"tail.txt") == "tail",
                "move_batch_real_mixed_files_directory_ads_keep_identity_and_order");
        }
        {
            Fixture f(root, L"collision"); std::ofstream(f.target / L"目录") << "rival";
            FileOperationCoordinator operation; const auto id = operation.MoveEntries(f.Requests());
            const bool done = Finished(operation.CurrentAction()); const auto failed = operation.Snapshot();
            passed &= Check(id && done && failed.stage == FileOperationStage::Failed && failed.move_batch &&
                failed.move_batch->current_index == 1 && failed.move_batch->moved_count == 1 && failed.move_batch->failed_count == 1 &&
                failed.move_batch->remaining_count == 1 && failed.move_batch->resumable && operation.HasPendingMoveBatch() &&
                operation.Copy(f.Request(L"tail.txt")) == 0 && fs::exists(f.source / L"tail.txt") && Read(f.target / L"目录") == "rival",
                "move_batch_collision_preserves_committed_prefix_pending_tail_and_admission");
            const auto resumed = operation.ResumeMoveBatch(id, FileCopyConflictPolicy::KeepBoth);
            const bool continued = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(resumed > id && continued && value.stage == FileOperationStage::Completed && value.move_batch &&
                value.move_batch->moved_count == 3 && value.move_batch->remaining_count == 0 &&
                Read(f.target / L"目录 (2)" / L"nested.txt") == "nested" && Read(f.target / L"tail.txt") == "tail" &&
                Read(f.target / L"first.txt") == "first" && Read(f.target / L"目录") == "rival",
                "move_batch_keep_both_resumes_failed_item_and_tail_without_replaying_prefix");
        }
        {
            Fixture f(root, L"duplicate"); auto first = f.Request(L"first.txt"), tail = f.Request(L"tail.txt");
            FileOperationCoordinator operation; const auto id = operation.MoveEntries({ first, first, tail });
            const bool done = Finished(operation.CurrentAction()); const auto failed = operation.Snapshot();
            passed &= Check(id && done && failed.move_batch && failed.move_batch->moved_count == 1 &&
                failed.move_batch->current_index == 1 && failed.stage == FileOperationStage::Failed && fs::exists(f.source / L"tail.txt"),
                "move_batch_duplicate_source_is_not_recaptured_or_silently_deduplicated");
            const auto resumed = operation.ResumeMoveBatch(id, FileCopyConflictPolicy::Skip);
            const bool continued = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(resumed && continued && value.move_batch && value.move_batch->moved_count == 2 &&
                value.move_batch->skipped_count == 1 && value.stage == FileOperationStage::Completed && Read(f.target / L"tail.txt") == "tail",
                "move_batch_explicit_skip_after_missing_duplicate_continues_tail");
        }
        {
            Fixture f(root, L"invalid-tail"); auto requests = f.Requests(); requests.back().source.identity.valid = false;
            FileOperationCoordinator operation; const auto id = operation.MoveEntries(std::move(requests));
            passed &= Check(id && Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Failed && f.Intact(),
                "move_batch_invalid_later_identity_rejects_before_any_write");
        }
        {
            Fixture f(root, L"cross-volume"); auto requests = f.Requests();
            requests.back().source.identity.volume_serial ^= 1; requests.back().source_parent_identity.volume_serial ^= 1;
            FileOperationCoordinator operation; const auto id = operation.MoveEntries(std::move(requests));
            const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.result.error == ERROR_FILE_INVALID && value.move_batch && value.move_batch->moved_count == 2 &&
                Read(f.target / L"first.txt") == "first" && Read(f.target / L"目录" / L"nested.txt") == "nested" &&
                Read(f.source / L"tail.txt") == "tail",
                "move_batch_cross_volume_file_source_identity_checked_before_its_own_write");
        }
        {
            Fixture f(root, L"cancel"); auto gate = std::make_shared<Gate>(); FileOperationCoordinator* current{};
            FileOperationCoordinator operation({}, {}, {}, [gate](const auto& request, const auto& cancel, const auto& progress) {
                gate->Enter(); return MoveEntrySafely(request, cancel, progress);
            }); current = &operation;
            const auto id = current->MoveEntries(f.Requests()); const bool entered = id && gate->Started(); current->Cancel(); gate->Release();
            const bool done = Finished(current->CurrentAction()); const auto value = current->Snapshot();
            passed &= Check(id && entered && done && value.stage == FileOperationStage::Cancelled && value.move_batch &&
                value.move_batch->cancelled_count == 1 && value.move_batch->remaining_count == 2 && f.Intact(),
                "move_batch_precommit_cancel_preserves_untouched_tail_and_source");
        }
        {
            Fixture f(root, L"late-cancel"); FileOperationCoordinator* current{};
            FileOperationCoordinator operation({}, {}, {}, [&](const auto& request, const auto& cancel, const auto& progress) {
                auto result = MoveEntrySafely(request, cancel, progress); if (request.source.name == L"tail.txt") { current->Cancel(); } return result;
            }); current = &operation;
            const auto id = operation.MoveEntries(f.Requests()); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.move_batch && value.move_batch->moved_count == 3 && value.stage == FileOperationStage::Completed &&
                value.cancel_requested && Read(f.target / L"tail.txt") == "tail", "move_batch_last_real_commit_wins_over_late_cancel");
        }
        {
            Fixture f(root, L"action"); auto gate = std::make_shared<Gate>(); FileOperationCoordinator operation;
            const auto id = operation.MoveEntries(f.Requests(), [gate](const auto& value) {
                if (value.stage == FileOperationStage::Completed) { gate->Enter(); }
            }); const bool entered = id && gate->Started();
            passed &= Check(entered && operation.IsBusy() && operation.MoveEntries({}) == 0,
                "move_batch_terminal_publication_cannot_release_live_native_action");
            gate->Release(); passed &= Check(id && Finished(operation.CurrentAction()) && !operation.IsBusy(),
                "move_batch_actual_action_exit_releases_admission");
        }
        {
            Fixture f(root, L"unchanged"); auto requests = f.Requests();
            for (auto& request : requests)
            { request.destination_parent = request.source_parent; request.destination_parent_identity = request.source_parent_identity; }
            FileOperationCoordinator operation; const auto id = operation.MoveEntries(std::move(requests));
            const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Completed && value.move_batch &&
                value.move_batch->unchanged_count == 3 && value.move_batch->moved_count == 0 && !value.result.committed && f.Intact(),
                "move_batch_same_parent_selection_counts_unchanged_without_mutation");
        }
        {
            Fixture f(root, L"skip"); auto requests = f.Requests();
            for (auto& request : requests) { request.conflict_policy = FileCopyConflictPolicy::Skip; }
            fs::remove(f.source / L"tail.txt");
            FileOperationCoordinator operation; const auto id = operation.MoveEntries(std::move(requests));
            const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Skipped && value.move_batch &&
                value.move_batch->skipped_count == 3 && value.move_batch->moved_count == 0 && fs::is_empty(f.target),
                "move_batch_explicit_all_skip_is_zero_filesystem_io_even_after_source_disappears");
        }
        {
            Fixture f(root, L"prefix-cancel"); FileOperationCoordinator operation;
            const auto id = operation.MoveEntries(f.Requests(), [&](const auto& value) {
                if (value.move_batch && value.move_batch->moved_count == 1) { operation.Cancel(); }
            }); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Cancelled && value.move_batch &&
                value.move_batch->moved_count == 1 && value.move_batch->remaining_count == 2 &&
                value.move_batch->items[1].stage == MoveBatchItemStage::Pending && Read(f.target / L"first.txt") == "first" &&
                Read(f.source / L"目录" / L"nested.txt") == "nested" && fs::exists(f.source / L"tail.txt"),
                "move_batch_cancel_between_items_keeps_moved_prefix_and_pending_tail_truthful");
        }
        {
            Fixture f(root, L"shutdown"); auto gate = std::make_shared<Gate>(); std::atomic_uint publications{};
            FileOperationCoordinator operation({}, {}, {}, [gate](const auto& request, const auto& cancel, const auto& progress) {
                gate->Enter(); return MoveEntrySafely(request, cancel, progress);
            });
            const auto id = operation.MoveEntries(f.Requests(), [&](auto) { ++publications; });
            const bool entered = id && gate->Started(); operation.Shutdown(); const auto before = publications.load();
            gate->Release(); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && entered && done && value.closed && value.move_batch && value.stage == FileOperationStage::Cancelled &&
                publications.load() == before && value.move_batch->remaining_count == 2 && f.Intact(),
                "move_batch_shutdown_suppresses_publication_and_preserves_sources");
        }
        {
            Fixture f(root, L"exception"); unsigned executions{};
            FileOperationCoordinator operation({}, {}, {}, [&](const auto& request, const auto& cancel, const auto& progress) -> MoveResult {
                if (++executions == 2) { throw std::runtime_error("owned executor failure"); }
                return MoveEntrySafely(request, cancel, progress);
            });
            const auto id = operation.MoveEntries(f.Requests()); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Failed && value.move_batch &&
                value.move_batch->moved_count == 1 && value.move_batch->failed_count == 1 && value.move_batch->remaining_count == 1 &&
                Read(f.target / L"first.txt") == "first" && fs::exists(f.source / L"tail.txt"),
                "move_batch_executor_exception_preserves_committed_prefix_and_tail");
            passed &= Check(operation.ResumeMoveBatch(id + 1, FileCopyConflictPolicy::Skip) == 0 &&
                operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Skip) == 0 &&
                operation.ResumeMoveBatch(id, static_cast<FileCopyConflictPolicy>(99)) == 0,
                "move_batch_wrong_generation_kind_or_unknown_policy_cannot_resume_queue");
        }
        {
            Fixture f(root, L"old-callback"); MoveProgressCallback first_progress; FileOperationCoordinator* current{};
            bool old_rejected = false; unsigned executions{};
            FileOperationCoordinator operation({}, {}, {}, [&](const auto& request, const auto& cancel, const auto& progress) {
                if (++executions == 1) { first_progress = progress; }
                else
                {
                    const auto before = current->Snapshot(); first_progress({ MovePhase::Completed, L"old-source", L"poison" });
                    const auto after = current->Snapshot();
                    old_rejected = before.move_progress.destination_path == after.move_progress.destination_path &&
                        before.move_batch->current_index == after.move_batch->current_index;
                }
                return MoveEntrySafely(request, cancel, progress);
            }); current = &operation;
            const auto id = operation.MoveEntries(f.Requests()); const bool done = Finished(operation.CurrentAction());
            passed &= Check(id && done && old_rejected && operation.Snapshot().move_batch->moved_count == 3,
                "move_batch_previous_item_callback_cannot_mutate_current_item");
        }
        {
            Fixture f(root, L"replaced"); FileOperationCoordinator operation; bool replaced = false;
            const auto id = operation.MoveEntries(f.Requests(), [&](const auto& value) {
                if (!replaced && value.move_batch && value.move_batch->moved_count == 1)
                {
                    replaced = true; fs::rename(f.source / L"目录", f.source / L"old-tree");
                    fs::create_directory(f.source / L"目录");
                }
            }); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && replaced && value.stage == FileOperationStage::Failed && value.result.error == ERROR_FILE_INVALID &&
                value.move_batch && value.move_batch->moved_count == 1 && Read(f.source / L"old-tree" / L"nested.txt") == "nested" &&
                fs::exists(f.source / L"目录") && fs::exists(f.source / L"tail.txt"),
                "move_batch_later_source_replacement_cannot_move_new_identity");
        }
        {
            Fixture f(root, L"warning"); FileOperationCoordinator operation({}, {}, {}, [](const auto& request, const auto& cancel, const auto& progress) {
                auto result = MoveEntrySafely(request, cancel, progress);
                if (result.moved && request.source.is_directory) { result.post_commit_error = ERROR_ACCESS_DENIED; }
                return result;
            });
            const auto id = operation.MoveEntries(f.Requests()); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Completed && value.move_batch &&
                value.move_batch->moved_count == 3 && value.move_batch->warning_count == 1 &&
                value.move_batch->items[1].result.moved && value.move_batch->items[1].result.post_commit_error == ERROR_ACCESS_DENIED,
                "move_batch_postcommit_warning_survives_later_successful_items");
        }
        {
            Fixture f(root, L"copy-blocks"); std::ofstream(f.target / L"first.txt") << "rival";
            FileOperationCoordinator operation; const auto copy = operation.CopyBatch({ f.Request(L"first.txt"), f.Request(L"tail.txt") });
            const bool done = Finished(operation.CurrentAction());
            passed &= Check(copy && done && operation.HasPendingCopyBatch() && operation.MoveEntries(f.Requests()) == 0,
                "move_batch_cannot_replace_pending_copy_queue");
            operation.Cancel();
            passed &= Check(!operation.HasPendingBatch(), "move_batch_explicit_cancel_releases_protected_copy_tail");
        }
        {
            Fixture f(root, L"overlap"); auto tree = f.Request(L"目录"), child = f.Request(L"tail.txt");
            child.source_parent = (f.source / L"目录").wstring(); child.source_parent_identity = Entry(f.source / L"目录").identity;
            child.source = Entry(f.source / L"目录" / L"nested.txt"); child.destination_name = L"nested.txt";
            FileOperationCoordinator operation; const auto id = operation.MoveEntries({ tree, child, f.Request(L"tail.txt") });
            const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Failed && value.move_batch &&
                value.move_batch->moved_count == 1 && value.move_batch->current_index == 1 && value.move_batch->remaining_count == 1 &&
                Read(f.target / L"目录" / L"nested.txt") == "nested" && fs::exists(f.source / L"tail.txt") && !fs::exists(f.target / L"nested.txt"),
                "move_batch_overlapping_parent_child_never_rewrites_captured_source_path");
            operation.Cancel();
            passed &= Check(!operation.HasPendingBatch() && !operation.Snapshot().move_batch->resumable &&
                Read(f.target / L"目录" / L"nested.txt") == "nested", "move_batch_explicit_cancel_releases_failed_tail_without_rollback");
        }
        {
            Fixture f(root, L"hardlinks"); fs::create_hard_link(f.source / L"first.txt", f.source / L"alias.txt");
            const auto original = Entry(f.source / L"first.txt").identity;
            FileOperationCoordinator operation; const auto id = operation.MoveEntries({ f.Request(L"first.txt"), f.Request(L"alias.txt") });
            const bool done = Finished(operation.CurrentAction());
            passed &= Check(id && done && operation.Snapshot().move_batch && operation.Snapshot().move_batch->moved_count == 2 &&
                Entry(f.target / L"first.txt").identity == original && Entry(f.target / L"alias.txt").identity == original &&
                !fs::exists(f.source / L"alias.txt") && Read(f.target / L"alias.txt") == "first",
                "move_batch_distinct_hardlink_names_keep_identity_without_deduplication");
        }
        {
            Fixture f(root, L"structural"); bool rejected = true; unsigned executions{};
            FileOperationCoordinator operation({}, {}, {}, [&](const auto&, const auto&, const auto&) {
                ++executions; return MoveResult{ ERROR_GEN_FAILURE };
            });
            for (unsigned mode = 0; mode != 6; ++mode)
            {
                auto requests = f.Requests(); auto& tail = requests.back();
                if (mode == 0) { tail = f.Request(L"目录"); tail.conflict_policy = FileCopyConflictPolicy::Replace; }
                if (mode == 1) { tail.conflict_policy = static_cast<FileCopyConflictPolicy>(99); }
                if (mode == 2) { tail.destination_name = L".."; }
                if (mode == 3) { tail.destination_parent_identity = Entry(f.source).identity; }
                if (mode == 4) { tail.source.attributes |= FILE_ATTRIBUTE_REPARSE_POINT; }
                if (mode == 5) { requests.resize(MaximumFileCopyBatchItems + 1, requests.front()); }
                const auto id = operation.MoveEntries(std::move(requests));
                rejected &= id && Finished(operation.CurrentAction()) && operation.Snapshot().stage == FileOperationStage::Failed &&
                    !operation.HasPendingBatch() && f.Intact();
            }
            passed &= Check(rejected && executions == 0, "move_batch_all_structural_scope_and_count_checks_precede_any_executor");
        }
        {
            Fixture f(root, L"false-progress");
            FileOperationCoordinator operation({}, {}, {}, [](const auto&, const auto&, const auto& progress) {
                progress({ MovePhase::Completed, L"source", L"destination" });
                MoveResult result; result.error = ERROR_SUCCESS; return result;
            });
            const auto id = operation.MoveEntries(f.Requests()); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Failed && value.move_batch && value.move_batch->moved_count == 0 &&
                value.move_batch->remaining_count == 2 && !value.result.committed && value.result.error == ERROR_GEN_FAILURE && f.Intact(),
                "move_batch_completed_progress_cannot_substitute_for_native_outcome");
        }
    }
    catch (...) { passed &= Check(false, "move_batch_fixture_unexpected_exception"); }
    return passed;
}
