#include "MoveTransferTests.h"
#include "Blink/App/MoveTransferPresentation.h"
#include "Blink/App/FolderCommandPolicy.h"
#include "Blink/App/Localization.h"
#include <iostream>

namespace
{
    using namespace blink::app;
    bool Check(bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    blink::core::FileIdentity Identity(unsigned char value, std::uint64_t volume = 17)
    { blink::core::FileIdentity id; id.valid = true; id.volume_serial = volume; id.file_id[0] = value; return id; }
    FileCopyRequest Request(const wchar_t* name, unsigned char id, bool directory = false)
    {
        FileCopyRequest request; request.source_parent = L"D:\\source"; request.source_parent_identity = Identity(1);
        request.source.name = name; request.source.identity = Identity(id); request.source.is_directory = directory;
        request.source.attributes = directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        return request;
    }
}
bool RunMoveTransferTests()
{
    bool passed = true;
    const auto file = Request(L"first.txt", 2); const auto dir = Request(L"目录", 3, true);
    ClipboardCopySources captured = CopyItemRequests{ file, DirectoryCopyProjection(dir), file };
    ClipboardCopyCache cache{ captured, { L"D:\\source\\first.txt", L"D:\\source\\目录", L"D:\\source\\first.txt" }, 73,
        ClipboardTransferIntent::Move };
    ClipboardFilePaths payload{ ERROR_SUCCESS, cache.paths, 73, ClipboardTransferIntent::Move };
    ClipboardCopySources output;
    passed &= Check(PrepareClipboardPasteSources(payload, cache, captured, output) == ERROR_SUCCESS &&
        SameCapturedCopySources(captured, output), "move_transfer_internal_payload_keeps_original_identities_and_duplicates");
    cache.move_submitted = true;
    passed &= Check(PrepareClipboardPasteSources(payload, cache, captured, output) == ERROR_INVALID_STATE &&
        std::holds_alternative<std::monostate>(output), "move_transfer_submitted_cut_sequence_cannot_replay_partially_moved_selection");
    cache.move_submitted = false;
    auto replaced = captured; std::get<FileCopyRequest>(std::get<CopyItemRequests>(replaced)[0]).source.identity = Identity(9);
    passed &= Check(PrepareClipboardPasteSources(payload, cache, replaced, output) == ERROR_FILE_INVALID &&
        std::holds_alternative<std::monostate>(output), "move_transfer_same_name_replacement_never_becomes_new_external_source");
    payload.intent = ClipboardTransferIntent::Copy;
    passed &= Check(PrepareClipboardPasteSources(payload, cache, captured, output) == ERROR_FILE_INVALID,
        "move_transfer_same_sequence_changed_intent_is_rejected");
    payload.intent = ClipboardTransferIntent::Move; payload.sequence = 74;
    passed &= Check(PrepareClipboardPasteSources(payload, cache, replaced, output) == ERROR_SUCCESS &&
        SameCapturedCopySources(replaced, output), "move_transfer_new_external_sequence_captures_its_own_identity");
    payload.error = ERROR_NOT_SUPPORTED;
    passed &= Check(PrepareClipboardPasteSources(payload, cache, captured, output) == ERROR_NOT_SUPPORTED &&
        std::holds_alternative<std::monostate>(output), "move_transfer_bad_payload_clears_previous_prepared_sources");
    payload.error = ERROR_SUCCESS; payload.intent = static_cast<ClipboardTransferIntent>(77);
    passed &= Check(PrepareClipboardPasteSources(payload, cache, captured, output) == ERROR_NOT_SUPPORTED,
        "move_transfer_unknown_intent_never_defaults_to_copy");
    auto copy_cache = cache; copy_cache.intent = ClipboardTransferIntent::Copy; copy_cache.move_submitted = true;
    payload.sequence = 73; payload.intent = ClipboardTransferIntent::Copy;
    passed &= Check(PrepareClipboardPasteSources(payload, copy_cache, captured, output) == ERROR_SUCCESS,
        "move_transfer_copy_payload_can_still_be_pasted_repeatedly");

    std::vector<MoveRequest> moves;
    const auto target = Identity(8);
    passed &= Check(PrepareMoveDestination(captured, L"D:\\target", target, moves) == ERROR_SUCCESS && moves.size() == 3 &&
        moves[0].source.identity == Identity(2) && moves[1].source.is_directory && moves[2].source.name == L"first.txt" &&
        moves[0].destination_parent_identity == target && moves[1].destination_name == L"目录",
        "move_transfer_destination_binding_preserves_mixed_order_type_and_source_ids");
    passed &= Check(PrepareMoveDestination(captured, L"D:\\target", Identity(8, 99), moves) == ERROR_SUCCESS && moves.size() == 3 &&
        moves[1].source.is_directory && moves[1].destination_parent_identity == Identity(8, 99),
        "move_transfer_cross_volume_mixed_binding_preserves_order_and_source_ids");
    passed &= Check(PrepareMoveDestination(captured, L"D:\\target", {}, moves) == ERROR_INVALID_PARAMETER && moves.empty(),
        "move_transfer_invalid_destination_identity_clears_queue");
    ClipboardCopySources single = DirectoryCopyProjection(dir);
    passed &= Check(PrepareMoveDestination(single, L"D:\\target", target, moves) == ERROR_SUCCESS &&
        moves.size() == 1 && moves.front().source.is_directory, "move_transfer_single_directory_uses_same_move_engine_projection");

    FileOperationSnapshot value; value.kind = FileOperationKind::Move; value.stage = FileOperationStage::Failed;
    value.move_request = file; value.move_batch.emplace(); value.move_batch->resumable = true;
    value.result.error = ERROR_ALREADY_EXISTS; value.move_result = MoveResult{ ERROR_ALREADY_EXISTS };
    passed &= Check(MoveQueueHasRootConflict(value), "move_transfer_true_root_collision_offers_move_conflict");
    value.move_result->moved = true;
    passed &= Check(!MoveQueueHasRootConflict(value), "move_transfer_committed_warning_never_offers_repeat_conflict");
    value.stage = FileOperationStage::Completed; value.move_request->destination_parent_identity = Identity(8);
    const auto both = MoveRefreshParents(value);
    passed &= Check(both.size() == 2 && both[0] == Identity(1) && both[1] == Identity(8),
        "move_transfer_real_move_refreshes_original_source_and_destination_parents");
    value.move_request->destination_parent_identity = Identity(1);
    passed &= Check(MoveRefreshParents(value).size() == 1, "move_transfer_same_parent_refresh_is_deduplicated");
    value.move_result->moved = false; value.move_result->unchanged = true;
    passed &= Check(MoveRefreshParents(value).empty(), "move_transfer_unchanged_or_skipped_never_claims_parent_mutation");

    FolderCommandCapabilities command{ true, false, 3, false, true, false };
    passed &= Check(EvaluateFolderCommand(FolderCommand::Cut, command) == FolderCommandStatus::Ready,
        "move_transfer_cut_command_accepts_multiple_selected_items");
    command.mutation_busy = true;
    passed &= Check(EvaluateFolderCommand(FolderCommand::Cut, command) == FolderCommandStatus::Busy,
        "move_transfer_cut_command_respects_existing_operation_and_preparation");
    const Localization localization;
    passed &= Check(localization.GetString(L"Move.Running") == L"Moving items." &&
        localization.GetString(L"Move.Unchanged") == L"The selected items are already in this folder.",
        "move_transfer_status_uses_move_specific_localized_fallback");
    FileOperationSnapshot summary; summary.stage = FileOperationStage::Completed; summary.move_batch.emplace();
    summary.move_batch->requested_count = 2; summary.move_batch->unchanged_count = 1; summary.move_batch->skipped_count = 1;
    passed &= Check(localization.GetString(MoveSummaryKey(summary)) == L"Move queue completed.",
        "move_transfer_mixed_unchanged_and_skip_cannot_claim_entire_selection_already_present");
    summary.move_batch->unchanged_count = 2; summary.move_batch->skipped_count = 0;
    passed &= Check(localization.GetString(MoveSummaryKey(summary)) == L"The selected items are already in this folder.",
        "move_transfer_all_unchanged_selection_has_truthful_noop_summary");
    return passed;
}
