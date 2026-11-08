#pragma once
#include "Blink/App/CopyTransferPresentation.h"

namespace blink::app
{
    [[nodiscard]] inline const wchar_t* MoveSummaryKey(const FileOperationSnapshot& value) noexcept
    {
        using Stage = FileOperationStage;
        if (value.stage == Stage::Completed)
        {
            return value.move_batch && value.move_batch->requested_count &&
                value.move_batch->unchanged_count == value.move_batch->requested_count ? L"Move.Unchanged" : L"Move.Completed";
        }
        return value.stage == Stage::Skipped ? L"Move.Skipped" : value.stage == Stage::Cancelled ? L"Move.Cancelled" :
            value.stage == Stage::Cancelling ? L"Move.Cancelling" : value.stage == Stage::Failed ? L"Move.Failed" : L"Move.Running";
    }

    [[nodiscard]] inline DWORD PrepareClipboardPasteSources(const ClipboardFilePaths& payload,
        const ClipboardCopyCache& own, const ClipboardCopySources& captured, ClipboardCopySources& output)
    {
        output.emplace<std::monostate>();
        if (payload.error) { return payload.error; }
        if (!ClipboardDropEffectValue(payload.intent)) { return ERROR_NOT_SUPPORTED; }
        if (own.sequence && own.sequence == payload.sequence)
        {
            if (!own.MatchesPayload(payload)) { return ERROR_FILE_INVALID; }
            if (own.intent == ClipboardTransferIntent::Move && own.move_submitted) { return ERROR_INVALID_STATE; }
            if (!SameCapturedCopySources(own.sources, captured)) { return ERROR_FILE_INVALID; }
        }
        if (!SameCapturedCopySources(captured, captured)) { return ERROR_INVALID_PARAMETER; }
        try { output = captured; return ERROR_SUCCESS; }
        catch (const std::bad_alloc&) { return ERROR_NOT_ENOUGH_MEMORY; }
        catch (...) { return ERROR_GEN_FAILURE; }
    }

    [[nodiscard]] inline DWORD PrepareMoveDestination(const ClipboardCopySources& captured, const std::wstring& parent,
        const blink::core::FileIdentity& identity, std::vector<MoveRequest>& output)
    {
        output.clear();
        try
        {
            ClipboardCopySources bound;
            if (const auto error = PrepareCopyDestination(captured, parent, identity, bound); error) { return error; }
            std::vector<MoveRequest> pending;
            if (auto* files = std::get_if<std::vector<FileCopyRequest>>(&bound)) { pending = std::move(*files); }
            else if (const auto* directory = std::get_if<DirectoryCopyRequest>(&bound)) { pending.push_back(FileCopyProjection(*directory)); }
            else if (const auto* items = std::get_if<CopyItemRequests>(&bound))
            {
                pending.reserve(items->size());
                for (const auto& item : *items)
                { std::visit([&](const auto& request) {
                    if constexpr (std::is_same_v<std::decay_t<decltype(request)>, DirectoryCopyRequest>)
                    { pending.push_back(FileCopyProjection(request)); }
                    else { pending.push_back(request); }
                }, item); }
            }
            output = std::move(pending); return ERROR_SUCCESS;
        }
        catch (const std::bad_alloc&) { return ERROR_NOT_ENOUGH_MEMORY; }
        catch (...) { return ERROR_GEN_FAILURE; }
    }

    [[nodiscard]] inline bool MoveQueueHasRootConflict(const FileOperationSnapshot& value) noexcept
    {
        return value.kind == FileOperationKind::Move && value.stage == FileOperationStage::Failed && value.move_request &&
            value.move_batch && value.move_batch->resumable && value.move_result && !value.move_result->moved &&
            !MoveHasPartialDestination(*value.move_result) && !MoveHasSourceChanges(*value.move_result) &&
            (value.result.error == ERROR_FILE_EXISTS || value.result.error == ERROR_ALREADY_EXISTS);
    }
    [[nodiscard]] inline std::vector<blink::core::FileIdentity> MoveRefreshParents(const FileOperationSnapshot& value)
    {
        if (value.kind != FileOperationKind::Move || !value.move_request || !value.move_result) { return {}; }
        const auto& moved = *value.move_result;
        const bool source_changed = MoveHasSourceChanges(moved);
        const bool destination_changed = moved.moved || MoveHasPartialDestination(moved);
        if (!source_changed && !destination_changed) { return {}; }
        const auto& source = value.move_request->source_parent_identity;
        const auto& destination = value.move_request->destination_parent_identity;
        std::vector<blink::core::FileIdentity> result;
        if (source_changed && source.valid) { result.push_back(source); }
        if (destination_changed && destination.valid && (!source_changed || !source.valid || destination != source))
        { result.push_back(destination); }
        return result;
    }
}
