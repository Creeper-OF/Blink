#pragma once
#include "Blink/App/FileOperationCoordinator.h"
#include "Blink/App/TabState.h"

namespace blink::app
{
    [[nodiscard]] inline HRESULT CaptureDeleteRequests(const TabState& state, const DeleteMode mode, std::vector<DeleteRequest>& output)
    {
        output.clear();
        try
        {
            if (mode != DeleteMode::Recycle && mode != DeleteMode::Permanent) { return E_INVALIDARG; }
            const auto page = state.PresentationSnapshot();
            if (!page.committed_location.IsFolder() || page.has_pending || !state.CommittedDirectoryIdentity().valid) { return E_INVALIDARG; }
            const auto selected = state.SelectedCommittedRows(MaximumDeleteItems);
            if (!selected) { return E_INVALIDARG; }
            std::vector<DeleteRequest> requests; requests.reserve(selected->size());
            for (const auto& entry : *selected)
            { requests.push_back({ page.committed_location.FolderPath(), state.CommittedDirectoryIdentity(), entry, mode, false, false }); }
            output = std::move(requests); return S_OK;
        }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }
    [[nodiscard]] inline const wchar_t* DeleteSummaryKey(const FileOperationSnapshot& value) noexcept
    {
        using Stage = FileOperationStage;
        return value.stage == Stage::Completed ? L"Delete.Completed" : value.stage == Stage::Cancelled ? L"Delete.Cancelled" :
            value.stage == Stage::Cancelling ? L"Delete.Cancelling" : value.stage == Stage::Failed ? L"Delete.Failed" :
            value.delete_request && value.delete_request->mode == DeleteMode::Permanent ? L"Delete.PermanentRunning" : L"Delete.RecycleRunning";
    }
    [[nodiscard]] inline std::optional<blink::core::FileIdentity> DeleteRefreshParent(const FileOperationSnapshot& value) noexcept
    {
        return value.kind == FileOperationKind::Delete && value.delete_request && value.delete_result &&
            DeleteHasSourceChanges(*value.delete_result) && value.delete_request->parent_identity.valid ?
            std::optional<blink::core::FileIdentity>{ value.delete_request->parent_identity } : std::nullopt;
    }
}
