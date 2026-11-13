#include "ClipboardTransferPresentationTests.h"
#include "Blink/App/ClipboardTransferPresentation.h"
#include "Blink/App/MoveTransferPresentation.h"
#include <iostream>

namespace
{
    using namespace blink::app;
    bool Check(bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    blink::core::FileIdentity Identity(unsigned char value, std::uint64_t volume = 17)
    { blink::core::FileIdentity id; id.valid = true; id.volume_serial = volume; id.file_id[0] = value; return id; }
    FileCopyRequest Request(const wchar_t* name, unsigned char identity, bool directory = false)
    {
        FileCopyRequest request; request.source_parent = L"D:\\source"; request.source_parent_identity = Identity(1);
        request.source.name = name; request.source.identity = Identity(identity); request.source.is_directory = directory;
        request.source.attributes = directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        request.destination_parent = L"D:\\target"; request.destination_parent_identity = Identity(99);
        request.destination_name = name; return request;
    }
    ClipboardCopyCache Cache(ClipboardCopySources sources, DWORD sequence = 71,
        ClipboardTransferIntent intent = ClipboardTransferIntent::Move)
    {
        ClipboardCopyCache cache; cache.sources = std::move(sources); cache.sequence = sequence; cache.intent = intent;
        const auto add = [&cache](const auto& request) { cache.paths.push_back(request.source_parent + L"\\" + request.source.name); };
        if (const auto* files = std::get_if<std::vector<FileCopyRequest>>(&cache.sources)) { for (const auto& file : *files) { add(file); } }
        else if (const auto* directory = std::get_if<DirectoryCopyRequest>(&cache.sources)) { add(*directory); }
        else if (const auto* items = std::get_if<CopyItemRequests>(&cache.sources)) { for (const auto& item : *items) { std::visit(add, item); } }
        return cache;
    }
    FileOperationSnapshot Moves(const std::vector<FileCopyRequest>& requests, std::uint64_t id = 10)
    {
        FileOperationSnapshot snapshot; snapshot.operation_id = id; snapshot.kind = FileOperationKind::Move;
        snapshot.stage = FileOperationStage::Running; snapshot.result.error = ERROR_SUCCESS;
        auto& batch = snapshot.move_batch.emplace(); batch.requested_count = requests.size();
        for (const auto& request : requests) { MoveBatchItem item; item.request = request; batch.items.push_back(std::move(item)); }
        return snapshot;
    }
    FileOperationSnapshot Copies(const std::vector<FileCopyRequest>& requests, std::uint64_t id = 20)
    {
        FileOperationSnapshot snapshot; snapshot.operation_id = id; snapshot.kind = FileOperationKind::Copy;
        snapshot.stage = FileOperationStage::Running; snapshot.result.error = ERROR_SUCCESS;
        auto& batch = snapshot.copy_batch.emplace(); batch.requested_count = requests.size();
        for (const auto& request : requests)
        {
            FileCopyBatchItem item; item.request = request;
            if (request.source.is_directory) { item.directory_limits.emplace(); }
            batch.items.push_back(std::move(item));
        }
        return snapshot;
    }
    void Moved(MoveBatchItem& item)
    { item.stage = MoveBatchItemStage::Moved; item.result = {}; item.result.error = ERROR_SUCCESS; item.result.moved = true; }
    void Copied(FileCopyBatchItem& item)
    { item.stage = FileCopyBatchItemStage::Copied; item.result = {}; item.result.error = ERROR_SUCCESS; item.result.copied = true; }
    FileOperationSnapshot Directory(const FileCopyRequest& request, std::uint64_t id = 30)
    {
        FileOperationSnapshot snapshot; snapshot.operation_id = id; snapshot.kind = FileOperationKind::DirectoryCopy;
        snapshot.stage = FileOperationStage::Running; snapshot.result.error = ERROR_SUCCESS;
        snapshot.directory_copy_request = DirectoryCopyProjection(request); return snapshot;
    }
}

bool RunClipboardTransferPresentationTests()
{
    using Stage = ClipboardTransferStage;
    bool passed = true;
    const auto first = Request(L"Ångström.txt", 2), second = Request(L"second.txt", 3), folder = Request(L"目录", 4, true);
    const auto parent = first.source_parent_identity;
    const std::vector<FileCopyRequest> files{ first, second };
    const ClipboardCopySources mixed = CopyItemRequests{ first, DirectoryCopyProjection(folder) };

    {
        ClipboardTransferPresentation view; const auto cache = Cache(mixed);
        passed &= Check(view.Outcome().stage == Stage::Idle && !view.IsCut(parent, first.source) && !view.CutGeneration(),
            "clipboard_presentation_initial_state_has_no_unpublished_cut");
        view.Publish(cache);
        passed &= Check(view.Outcome().stage == Stage::Published && view.Outcome().requested_count == 2 &&
            view.IsCut(parent, first.source) && view.IsCut(parent, folder.source) && view.CutGeneration() == 1,
            "clipboard_presentation_successful_cut_marks_mixed_source_links");
        passed &= Check(view.IsCut(parent, first.source.identity, std::wstring_view(first.source.name), false),
            "clipboard_presentation_view_lookup_uses_identity_without_copying_entry");
        auto replaced = first.source; replaced.identity = Identity(55);
        passed &= Check(!view.IsCut(parent, replaced), "clipboard_presentation_same_name_replacement_is_not_cut");
        passed &= Check(!view.IsCut(Identity(55), first.source) && !view.IsCut(Identity(1, 99), first.source),
            "clipboard_presentation_source_parent_and_volume_are_part_of_link_identity");
        auto hardlink = first.source; hardlink.name = L"another-hardlink.txt";
        passed &= Check(!view.IsCut(parent, hardlink), "clipboard_presentation_same_file_id_different_hardlink_name_is_not_cut");
        auto typed = folder.source; typed.is_directory = false;
        passed &= Check(!view.IsCut(parent, typed), "clipboard_presentation_directory_type_is_part_of_link_identity");
        auto renamed = first.source; renamed.name = L"ÅNGSTRÖM.txt";
        passed &= Check(!view.IsCut(parent, renamed), "clipboard_presentation_name_matches_existing_exact_row_identity");
        view.Publish(cache);
        passed &= Check(view.CutGeneration() == 1, "clipboard_presentation_republishing_same_cut_set_does_not_rescan_rows");
        passed &= Check(!view.ClipboardChanged(71) && view.CutGeneration() == 1 && view.IsCut(parent, first.source),
            "clipboard_presentation_own_clipboard_notification_keeps_current_cut");
        passed &= Check(view.ClipboardChanged(72) && !view.IsCut(parent, first.source) && !view.IsCut(parent, folder.source) &&
            view.CutGeneration() == 2 && view.Outcome().stage == Stage::Idle,
            "clipboard_presentation_external_replacement_clears_only_old_display_batch");
        view.Publish(Cache(mixed, 72, ClipboardTransferIntent::Copy));
        passed &= Check(!view.IsCut(parent, first.source) && view.CutGeneration() == 2 && view.Outcome().stage == Stage::Published,
            "clipboard_presentation_copy_publication_never_dims_sources");
        view.Publish({});
        passed &= Check(view.Outcome().stage == Stage::Idle && view.CutGeneration() == 2,
            "clipboard_presentation_empty_cache_cannot_create_cut_display");
        auto submitted = cache; submitted.move_submitted = true; view.Publish(submitted);
        passed &= Check(!view.IsCut(parent, first.source) && view.CutGeneration() == 2,
            "clipboard_presentation_submitted_cache_cannot_rearm_cut_display");
    }
    {
        ClipboardTransferPresentation view; auto cache = Cache(ClipboardCopySources{ files });
        view.Track(cache.sources, cache.intent, cache.sequence, 10);
        passed &= Check(view.Outcome().stage == Stage::Running && view.Outcome().requested_count == 2 && !view.IsCut(parent, first.source),
            "clipboard_presentation_external_paste_tracks_result_without_claiming_own_cut");
        view.Publish(cache); view.Track(cache.sources, cache.intent, cache.sequence, 10);
        const auto generation = view.CutGeneration(); auto snapshot = Moves(files);
        passed &= Check(view.Outcome().operation_id == 10 && view.IsCut(parent, first.source) && view.CutGeneration() == generation,
            "clipboard_presentation_tracking_own_cut_preserves_display_and_binds_operation");
        snapshot.move_progress.copy_progress = { 100, 50 };
        passed &= Check(!view.Observe(snapshot) && view.CutGeneration() == generation,
            "clipboard_presentation_byte_progress_does_not_change_outcome_or_cut_generation");
        snapshot.move_progress.copy_progress.transferred_bytes = 100; snapshot.move_progress.phase = MovePhase::Completed;
        passed &= Check(!view.Observe(snapshot) && !view.Outcome().completed_count && view.IsCut(parent, first.source),
            "clipboard_presentation_completed_progress_hint_cannot_fabricate_move_commit");
        Moved(snapshot.move_batch->items[0]);
        passed &= Check(view.Observe(snapshot) && view.Outcome().completed_count == 1 && view.Outcome().remaining_count == 1 &&
            !view.IsCut(parent, first.source) && view.IsCut(parent, second.source) && view.CutGeneration() == generation + 1,
            "clipboard_presentation_each_real_move_retires_only_its_source_cut_link");
        auto earlier = Moves(files);
        passed &= Check(!view.Observe(earlier) && view.Outcome().completed_count == 1 && view.CutGeneration() == generation + 1,
            "clipboard_presentation_late_progress_cannot_undo_observed_commit");
        auto& failed = snapshot.move_batch->items[1]; failed.stage = MoveBatchItemStage::Failed; failed.result.error = ERROR_FILE_EXISTS;
        snapshot.stage = FileOperationStage::Failed; snapshot.result.error = ERROR_FILE_EXISTS; snapshot.move_batch->resumable = true;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::AwaitingDecision && view.Outcome().resumable &&
            !view.Outcome().terminal && view.Outcome().failed_count == 1 && view.IsCut(parent, second.source) && view.CutGeneration() == generation + 1,
            "clipboard_presentation_resumable_conflict_keeps_only_unfinished_cut_links");
        view.Advance(99, 11); view.Advance(10, 0);
        passed &= Check(view.Outcome().operation_id == 10 && view.Outcome().stage == Stage::AwaitingDecision,
            "clipboard_presentation_invalid_resume_ids_cannot_replace_current_operation");
        view.Advance(10, 11);
        passed &= Check(view.Outcome().operation_id == 11 && view.Outcome().stage == Stage::Running &&
            view.Outcome().completed_count == 1 && view.Outcome().remaining_count == 1 && !view.Outcome().failed_count &&
            view.IsCut(parent, second.source), "clipboard_presentation_resume_keeps_commits_and_reopens_only_unresolved_member");
        snapshot.stage = FileOperationStage::Cancelled; snapshot.move_batch->resumable = false;
        passed &= Check(!view.Observe(snapshot) && view.Outcome().operation_id == 11 && view.IsCut(parent, second.source),
            "clipboard_presentation_old_operation_terminal_cannot_finish_resumed_transfer");
        snapshot.operation_id = 11; snapshot.stage = FileOperationStage::Completed; snapshot.result.error = ERROR_SUCCESS;
        Moved(snapshot.move_batch->items[1]);
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Completed && view.Outcome().terminal &&
            view.Outcome().completed_count == 2 && !view.Outcome().remaining_count && !view.IsCut(parent, second.source) &&
            view.CutGeneration() == generation + 2, "clipboard_presentation_resumed_transfer_completes_from_real_members_once");
        const auto completed = view.Outcome(); view.Track(cache.sources, cache.intent, cache.sequence, 11);
        passed &= Check(!view.Observe(snapshot) && view.Outcome() == completed && view.CutGeneration() == generation + 2,
            "clipboard_presentation_duplicate_completion_or_tracking_cannot_rearm_cut");
        cache.move_submitted = true;
        ClipboardCopySources prepared; const ClipboardFilePaths payload{ ERROR_SUCCESS, cache.paths, cache.sequence, cache.intent };
        passed &= Check(PrepareClipboardPasteSources(payload, cache, cache.sources, prepared) == ERROR_INVALID_STATE && cache.move_submitted,
            "clipboard_presentation_model_leaves_existing_submitted_move_replay_contract_intact");
    }
    {
        ClipboardTransferPresentation view; const auto cache = Cache(ClipboardCopySources{ files });
        view.Publish(cache); view.Track(cache.sources, cache.intent, cache.sequence, 10); auto snapshot = Moves(files);
        Moved(snapshot.move_batch->items[0]); auto& skip = snapshot.move_batch->items[1]; skip.stage = MoveBatchItemStage::Skipped;
        skip.result.error = ERROR_SUCCESS; skip.result.skipped = true; snapshot.stage = FileOperationStage::Completed;
        snapshot.move_batch->moved_count = 999; snapshot.move_batch->remaining_count = 999;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Partial && view.Outcome().completed_count == 1 &&
            view.Outcome().skipped_count == 1 && !view.Outcome().remaining_count && !view.IsCut(parent, second.source),
            "clipboard_presentation_moved_plus_skipped_uses_actual_items_and_restores_remaining_appearance");
    }
    {
        ClipboardTransferPresentation view; const auto cache = Cache(ClipboardCopySources{ files });
        view.Publish(cache); view.Track(cache.sources, cache.intent, cache.sequence, 10); auto snapshot = Moves(files);
        for (auto& item : snapshot.move_batch->items)
        { item.stage = MoveBatchItemStage::Skipped; item.result.error = ERROR_SUCCESS; item.result.skipped = true; }
        snapshot.stage = FileOperationStage::Skipped;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Skipped && view.Outcome().skipped_count == 2 &&
            !view.Outcome().completed_count && !view.IsCut(parent, first.source),
            "clipboard_presentation_all_skipped_sources_report_no_transfer_and_restore_appearance");
        view.Track(cache.sources, cache.intent, cache.sequence, 11); snapshot.operation_id = 11;
        snapshot.move_batch->items[0].stage = MoveBatchItemStage::Unchanged;
        snapshot.move_batch->items[0].result.skipped = false; snapshot.move_batch->items[0].result.unchanged = true;
        snapshot.stage = FileOperationStage::Completed;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Skipped && view.Outcome().skipped_count == 1 &&
            view.Outcome().unchanged_count == 1 && !view.Outcome().completed_count,
            "clipboard_presentation_mixed_skip_and_noop_cannot_claim_all_sources_already_present");
    }
    {
        const auto third = Request(L"third.txt", 5); const std::vector<FileCopyRequest> requests{ first, second, third };
        ClipboardTransferPresentation view; const auto cache = Cache(ClipboardCopySources{ requests });
        view.Publish(cache); view.Track(cache.sources, cache.intent, cache.sequence, 10); auto snapshot = Moves(requests);
        Moved(snapshot.move_batch->items[0]); snapshot.move_batch->items[2].stage = MoveBatchItemStage::Cancelled;
        snapshot.move_batch->items[2].result.error = ERROR_CANCELLED; snapshot.stage = FileOperationStage::Cancelled;
        snapshot.result.error = ERROR_CANCELLED;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Cancelled && view.Outcome().completed_count == 1 &&
            view.Outcome().cancelled_count == 1 && view.Outcome().remaining_count == 1 && !view.IsCut(parent, second.source) &&
            !view.IsCut(parent, third.source), "clipboard_presentation_cancel_preserves_real_commits_and_untouched_pending_count");
    }
    {
        ClipboardTransferPresentation view; const ClipboardCopySources source = std::vector<FileCopyRequest>{ first };
        const auto cache = Cache(source); view.Publish(cache); view.Track(source, cache.intent, cache.sequence, 10);
        auto snapshot = Moves({ first }); snapshot.stage = FileOperationStage::Failed; snapshot.result.error = ERROR_ACCESS_DENIED;
        snapshot.move_batch->items[0].stage = MoveBatchItemStage::Failed; snapshot.move_batch->items[0].result.error = ERROR_ACCESS_DENIED;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Failed && view.Outcome().failed_count == 1 &&
            view.Outcome().error == ERROR_ACCESS_DENIED && !view.Outcome().completed_count && !view.IsCut(parent, first.source),
            "clipboard_presentation_terminal_failure_restores_cut_and_preserves_real_error");
        view.Publish(cache); view.Track(source, cache.intent, cache.sequence, 11); snapshot.operation_id = 11; snapshot.move_batch->resumable = true;
        (void)view.Observe(snapshot); const bool pending = view.IsCut(parent, first.source);
        snapshot.move_batch->resumable = false; snapshot.stage = FileOperationStage::Cancelled; snapshot.result.error = ERROR_CANCELLED;
        passed &= Check(pending && view.Observe(snapshot) && view.Outcome().stage == Stage::Cancelled && !view.IsCut(parent, first.source),
            "clipboard_presentation_cancelling_conflict_releases_still_pending_cut_display");
    }
    {
        auto unchanged = first; unchanged.destination_parent_identity = parent; unchanged.destination_name = unchanged.source.name;
        ClipboardTransferPresentation view; const ClipboardCopySources source = std::vector<FileCopyRequest>{ unchanged };
        const auto cache = Cache(source); view.Publish(cache); view.Track(source, cache.intent, cache.sequence, 10);
        auto snapshot = Moves({ unchanged }); snapshot.stage = FileOperationStage::Completed;
        snapshot.move_batch->items[0].stage = MoveBatchItemStage::Unchanged;
        snapshot.move_batch->items[0].result.error = ERROR_SUCCESS; snapshot.move_batch->items[0].result.unchanged = true;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Unchanged && view.Outcome().unchanged_count == 1 &&
            !view.Outcome().completed_count && !view.IsCut(parent, first.source), "clipboard_presentation_same_folder_noop_is_not_a_completed_move");
    }
    {
        ClipboardTransferPresentation view; const ClipboardCopySources source = DirectoryCopyProjection(folder); const auto cache = Cache(source);
        view.Publish(cache); view.Track(source, cache.intent, cache.sequence, 10); auto snapshot = Moves({ folder });
        auto& item = snapshot.move_batch->items[0]; item.stage = MoveBatchItemStage::Skipped; item.result.error = ERROR_SUCCESS;
        item.result.skipped = item.result.merged = true; item.result.files_skipped = 1; item.result.source_items_remaining = 1;
        item.result.source_files_removed = 1; item.result.directory_copy_result.emplace(); item.result.directory_copy_result->copied = true;
        item.result.directory_copy_result->files_copied = 1; snapshot.stage = FileOperationStage::Completed;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Partial && view.Outcome().partial_count == 1 &&
            view.Outcome().skipped_count == 1 && !view.Outcome().completed_count && !view.IsCut(parent, folder.source),
            "clipboard_presentation_merge_with_retained_source_items_is_partial_not_whole_move");
    }
    {
        ClipboardTransferPresentation view; const ClipboardCopySources source = std::vector<FileCopyRequest>{ first };
        view.Track(source, ClipboardTransferIntent::Move, 71, 10); auto snapshot = Moves({ first });
        auto& item = snapshot.move_batch->items[0]; item.stage = MoveBatchItemStage::Failed; item.result.error = ERROR_ACCESS_DENIED;
        item.result.copy_result.emplace(); item.result.copy_result->copied = true; item.result.copy_result->error = ERROR_SUCCESS;
        snapshot.stage = FileOperationStage::Failed; snapshot.result.error = ERROR_ACCESS_DENIED;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Partial && view.Outcome().partial_count == 1 &&
            view.Outcome().failed_count == 1 && !view.Outcome().completed_count,
            "clipboard_presentation_cross_volume_copy_with_source_remaining_cannot_report_move_success");
    }
    {
        ClipboardTransferPresentation view; const ClipboardCopySources source = std::vector<FileCopyRequest>{ first };
        view.Track(source, ClipboardTransferIntent::Move, 71, 10); auto snapshot = Moves({ first }); Moved(snapshot.move_batch->items[0]);
        snapshot.move_batch->items[0].result.post_commit_error = ERROR_ACCESS_DENIED; snapshot.stage = FileOperationStage::Completed;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Completed && view.Outcome().completed_count == 1 &&
            view.Outcome().warning_count == 1 && view.Outcome().error == ERROR_SUCCESS,
            "clipboard_presentation_postcommit_warning_keeps_actual_move_complete");
        view.Track(source, ClipboardTransferIntent::Move, 71, 11); snapshot = Moves({ first }, 11);
        snapshot.stage = FileOperationStage::Completed; snapshot.move_batch->items[0].stage = MoveBatchItemStage::Moved;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Failed && !view.Outcome().completed_count,
            "clipboard_presentation_stage_label_without_native_commit_cannot_complete_move");
    }
    {
        ClipboardTransferPresentation view; const ClipboardCopySources source = std::vector<FileCopyRequest>{ first };
        const auto cache = Cache(source); view.Publish(cache); view.Track(source, cache.intent, cache.sequence, 10);
        FileOperationSnapshot snapshot; snapshot.operation_id = 10; snapshot.kind = FileOperationKind::Move;
        snapshot.move_request = first; snapshot.move_result.emplace(); snapshot.move_result->error = ERROR_SUCCESS;
        snapshot.move_result->moved = true; snapshot.stage = FileOperationStage::Completed; snapshot.result.error = ERROR_SUCCESS;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Completed && view.Outcome().completed_count == 1 &&
            !view.IsCut(parent, first.source), "clipboard_presentation_single_move_without_batch_retires_real_source_link");
    }
    {
        ClipboardTransferPresentation view; const auto cache = Cache(ClipboardCopySources{ files }, 81, ClipboardTransferIntent::Copy);
        view.Publish(cache); view.Track(cache.sources, cache.intent, cache.sequence, 20); auto snapshot = Copies(files);
        for (auto& item : snapshot.copy_batch->items) { Copied(item); } snapshot.stage = FileOperationStage::Completed;
        const bool first_copy = view.Observe(snapshot) && view.Outcome().stage == Stage::Completed && view.Outcome().completed_count == 2;
        view.Track(cache.sources, cache.intent, cache.sequence, 21); snapshot.operation_id = 21;
        passed &= Check(first_copy && view.Outcome().completed_count == 0 && view.Observe(snapshot) &&
            view.Outcome().completed_count == 2 && view.Outcome().stage == Stage::Completed && !view.CutGeneration(),
            "clipboard_presentation_copy_payload_can_be_tracked_and_completed_repeatedly");
    }
    {
        ClipboardTransferPresentation view; view.Track(mixed, ClipboardTransferIntent::Copy, 81, 20);
        auto snapshot = Copies({ first, folder }); Copied(snapshot.copy_batch->items[0]); auto& item = snapshot.copy_batch->items[1];
        Copied(item); item.directory_result.emplace(); item.directory_result->error = ERROR_SUCCESS;
        item.directory_result->copied = true; item.directory_result->files_copied = 1; item.directory_result->files_skipped = 1;
        snapshot.stage = FileOperationStage::Completed;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Partial && view.Outcome().completed_count == 1 &&
            view.Outcome().skipped_count == 1 && view.Outcome().partial_count == 1,
            "clipboard_presentation_mixed_copy_directory_child_skip_prevents_whole_selection_success");
    }
    {
        ClipboardTransferPresentation view; const ClipboardCopySources source = std::vector<FileCopyRequest>{ first };
        view.Track(source, ClipboardTransferIntent::Copy, 81, 20); auto snapshot = Copies({ first });
        auto& item = snapshot.copy_batch->items[0]; item.stage = FileCopyBatchItemStage::Failed;
        item.result.error = ERROR_DISK_FULL; item.result.partial_file_left = true;
        snapshot.stage = FileOperationStage::Failed; snapshot.result.error = ERROR_DISK_FULL;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Partial && view.Outcome().partial_count == 1 &&
            view.Outcome().failed_count == 1 && !view.Outcome().completed_count,
            "clipboard_presentation_partial_file_result_remains_distinct_from_completed_copy");
        view.Track(source, ClipboardTransferIntent::Copy, 81, 21); snapshot.operation_id = 21; snapshot.copy_batch->resumable = true;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::AwaitingDecision && !view.Outcome().terminal &&
            view.Outcome().partial_count == 1, "clipboard_presentation_copy_conflict_with_partial_effect_keeps_result_open");
        view.Advance(21, 22); snapshot.operation_id = 22; snapshot.copy_batch->resumable = false;
        item.previous_results.push_back(item.result); Copied(item); snapshot.stage = FileOperationStage::Completed; snapshot.result.error = ERROR_SUCCESS;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Completed && view.Outcome().completed_count == 1 &&
            !view.Outcome().partial_count, "clipboard_presentation_successful_retry_is_complete_despite_historical_partial_evidence");
    }
    {
        ClipboardTransferPresentation view; const ClipboardCopySources source = std::vector<FileCopyRequest>{ first };
        view.Track(source, ClipboardTransferIntent::Copy, 81, 20);
        FileOperationSnapshot snapshot; snapshot.operation_id = 20; snapshot.kind = FileOperationKind::Copy;
        snapshot.copy_request = first; snapshot.stage = FileOperationStage::Completed; snapshot.result.error = ERROR_SUCCESS;
        snapshot.result.committed = true;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Completed && view.Outcome().completed_count == 1,
            "clipboard_presentation_single_copy_without_batch_uses_actual_committed_result");
        view.Track(source, ClipboardTransferIntent::Copy, 81, 21); snapshot.operation_id = 21;
        snapshot.result.committed = false; snapshot.copy_progress = { 100, 100 };
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Failed && !view.Outcome().completed_count,
            "clipboard_presentation_single_copy_full_progress_is_not_commit_evidence");
    }
    {
        ClipboardTransferPresentation view; const ClipboardCopySources source = DirectoryCopyProjection(folder);
        view.Track(source, ClipboardTransferIntent::Copy, 81, 30); auto snapshot = Directory(folder);
        snapshot.directory_copy_result.emplace(); snapshot.directory_copy_result->error = ERROR_SUCCESS;
        snapshot.directory_copy_result->copied = true; snapshot.directory_copy_result->directories_created = 1;
        snapshot.stage = FileOperationStage::Completed;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Completed && view.Outcome().completed_count == 1,
            "clipboard_presentation_single_directory_copy_has_its_own_completion_path");
        view.Track(source, ClipboardTransferIntent::Copy, 81, 31); snapshot = Directory(folder, 31);
        auto& result = snapshot.directory_copy_result.emplace(); result.error = ERROR_DISK_FULL;
        result.partial_tree_left = true; result.files_copied = 2; result.directories_created = 1;
        snapshot.stage = FileOperationStage::Failed; snapshot.result.error = ERROR_DISK_FULL;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Partial && view.Outcome().partial_count == 1 &&
            !view.Outcome().completed_count, "clipboard_presentation_single_directory_partial_tree_is_not_full_copy");
        view.Track(source, ClipboardTransferIntent::Copy, 81, 32); snapshot = Directory(folder, 32);
        snapshot.directory_copy_result.emplace(); snapshot.directory_copy_result->error = ERROR_ALREADY_EXISTS;
        snapshot.stage = FileOperationStage::Failed; snapshot.result.error = ERROR_ALREADY_EXISTS;
        const bool conflict = view.Observe(snapshot) && view.Outcome().stage == Stage::Failed;
        view.Advance(32, 33);
        passed &= Check(conflict && view.Outcome().stage == Stage::Running && view.Outcome().operation_id == 33 && !view.Outcome().terminal,
            "clipboard_presentation_single_directory_root_conflict_can_advance_explicit_retry");
        passed &= Check(!view.Observe(snapshot) && view.Outcome().operation_id == 33,
            "clipboard_presentation_single_directory_previous_attempt_is_ignored_after_retry");
        snapshot.operation_id = 33; snapshot.stage = FileOperationStage::Completed; snapshot.result.error = ERROR_SUCCESS;
        snapshot.directory_copy_result->error = ERROR_SUCCESS; snapshot.directory_copy_result->copied = true;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Completed && view.Outcome().completed_count == 1,
            "clipboard_presentation_single_directory_explicit_retry_completes_original_selection_once");
        view.Track(source, ClipboardTransferIntent::Copy, 81, 34); snapshot.operation_id = 34;
        snapshot.directory_copy_result->files_copied = 1; snapshot.directory_copy_result->files_skipped = 1;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Partial && !view.Outcome().completed_count &&
            view.Outcome().partial_count == 1, "clipboard_presentation_single_directory_merge_skip_is_not_whole_root_success");
        view.Track(source, ClipboardTransferIntent::Copy, 81, 35); snapshot = Directory(folder, 35);
        snapshot.directory_copy_result.emplace(); snapshot.directory_copy_result->error = ERROR_CANCELLED;
        snapshot.directory_copy_result->partial_tree_left = true; snapshot.directory_copy_result->directories_created = 1;
        snapshot.stage = FileOperationStage::Cancelled; snapshot.result.error = ERROR_CANCELLED;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Cancelled && view.Outcome().cancelled_count == 1 &&
            view.Outcome().partial_count == 1, "clipboard_presentation_directory_cancel_retains_actual_partial_effect_and_reason");
    }
    {
        ClipboardTransferPresentation view; const ClipboardCopySources source = DirectoryCopyProjection(folder);
        view.Track(source, ClipboardTransferIntent::Copy, 81, 30); auto snapshot = Directory(folder);
        snapshot.directory_conflict.emplace(); snapshot.directory_conflict->request = first;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::AwaitingDecision && view.Outcome().resumable &&
            !view.Outcome().terminal && view.Outcome().remaining_count == 1,
            "clipboard_presentation_inline_directory_conflict_waits_without_finishing_paste");
        snapshot.directory_conflict.reset(); snapshot.stage = FileOperationStage::Cancelling;
        snapshot.directory_copy_progress.completed = true;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Cancelling && !view.Outcome().terminal &&
            !view.Outcome().completed_count, "clipboard_presentation_cancelling_directory_waits_for_native_result");
        snapshot.stage = FileOperationStage::Completed; snapshot.result.committed = true;
        passed &= Check(view.Observe(snapshot) && view.Outcome().stage == Stage::Failed && !view.Outcome().completed_count,
            "clipboard_presentation_directory_generic_commit_or_full_progress_cannot_replace_tree_result");
    }
    {
        ClipboardTransferPresentation view; const auto old_cache = Cache(ClipboardCopySources{ files });
        view.Publish(old_cache); view.Track(old_cache.sources, old_cache.intent, old_cache.sequence, 10);
        auto old = Moves(files); for (auto& item : old.move_batch->items) { Moved(item); } old.stage = FileOperationStage::Completed;
        // Use the proper typed-directory variant, as production capture does.
        view.Publish(Cache(ClipboardCopySources{ DirectoryCopyProjection(folder) }, 72));
        const auto generation = view.CutGeneration();
        passed &= Check(!view.Observe(old) && view.Outcome().stage == Stage::Published && view.IsCut(parent, folder.source) &&
            view.CutGeneration() == generation, "clipboard_presentation_old_transfer_completion_cannot_clear_new_cut_batch");
        view.Track(ClipboardCopySources{ DirectoryCopyProjection(folder) }, ClipboardTransferIntent::Move, 72, 11);
        const bool replaced = view.ClipboardChanged(73); auto current = Moves({ folder }, 11);
        Moved(current.move_batch->items[0]); current.stage = FileOperationStage::Completed;
        passed &= Check(replaced && view.Observe(current) && view.Outcome().stage == Stage::Completed &&
            !view.IsCut(parent, folder.source) && view.CutGeneration() == generation + 1,
            "clipboard_presentation_clipboard_replacement_hides_cut_but_keeps_original_transfer_result");
    }
    {
        ClipboardTransferPresentation view; const auto cache = Cache(ClipboardCopySources{ files });
        view.Publish(cache); const ClipboardCopySources other = std::vector<FileCopyRequest>{ Request(L"external.txt", 9) };
        view.Track(other, ClipboardTransferIntent::Move, cache.sequence, 10); auto snapshot = Moves({ Request(L"external.txt", 9) });
        Moved(snapshot.move_batch->items[0]); snapshot.stage = FileOperationStage::Completed;
        passed &= Check(view.Observe(snapshot) && view.IsCut(parent, first.source) && view.IsCut(parent, second.source),
            "clipboard_presentation_unrelated_sources_with_same_sequence_do_not_own_cut_members");
        view.Track(cache.sources, cache.intent, cache.sequence, 11); snapshot = Moves(files, 11);
        snapshot.move_batch->items[0].request.source.identity = Identity(56); Moved(snapshot.move_batch->items[0]);
        passed &= Check(!view.Observe(snapshot) && !view.Outcome().completed_count && view.IsCut(parent, first.source),
            "clipboard_presentation_same_operation_wrong_source_identity_cannot_retire_cut");
        const auto generation = view.CutGeneration(); view.Clear(); const auto cleared = view.CutGeneration(); view.Clear();
        passed &= Check(cleared == generation + 1 && view.CutGeneration() == cleared && view.Outcome().stage == Stage::Idle &&
            !view.IsCut(parent, first.source), "clipboard_presentation_clear_is_idempotent_and_releases_display_batch");
        view.Publish(cache); view.Track(cache.sources, cache.intent, cache.sequence, 12); view.Close(); const auto closed_generation = view.CutGeneration();
        view.Publish(cache); view.Track(cache.sources, cache.intent, cache.sequence, 13); view.Advance(12, 13);
        passed &= Check(view.Outcome().stage == Stage::Closed && view.Outcome().terminal && !view.IsCut(parent, first.source) &&
            !view.Observe(snapshot) && !view.ClipboardChanged(100) && view.CutGeneration() == closed_generation,
            "clipboard_presentation_close_retires_cut_and_ignores_late_publication_work");
    }
    {
        ClipboardTransferPresentation view; auto duplicate = Cache(ClipboardCopySources{ std::vector<FileCopyRequest>{ first, first } });
        view.Publish(duplicate); const auto generation = view.CutGeneration();
        view.Publish(Cache(ClipboardCopySources{ std::vector<FileCopyRequest>{ first } }, 72));
        passed &= Check(view.CutGeneration() == generation && view.IsCut(parent, first.source) && view.Outcome().requested_count == 1,
            "clipboard_presentation_generation_tracks_visible_set_not_duplicate_request_count");
        const std::wstring padded = L"prefix" + first.source.name + L"suffix";
        const auto name = std::wstring_view(padded).substr(6, first.source.name.size());
        passed &= Check(view.IsCut(parent, first.source.identity, name, false),
            "clipboard_presentation_unicode_view_lookup_obeys_explicit_length_without_terminator");
    }
    return passed;
}
