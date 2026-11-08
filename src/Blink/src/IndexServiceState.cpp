#include "Blink/App/IndexServiceState.h"

#include <utility>

namespace blink::app
{
    DWORD ValidateIndexReply(const IndexRpcResult& result) noexcept
    {
        namespace ipc = blink::index::ipc;
        if (!result.received || result.error != ERROR_SUCCESS)
        {
            return result.error == ERROR_SUCCESS ? ERROR_PIPE_NOT_CONNECTED : result.error;
        }
        if (result.expected_request_id == 0) { return ERROR_INVALID_DATA; }
        const auto& response = result.response;
        if (response.protocol_version != ipc::kProtocolVersion ||
            response.request_id != result.expected_request_id)
        {
            return ERROR_REVISION_MISMATCH;
        }
        if (static_cast<std::uint32_t>(response.status) > static_cast<std::uint32_t>(ipc::ResponseStatus::Unsupported) ||
            static_cast<std::uint32_t>(response.service_state) > static_cast<std::uint32_t>(ipc::ServiceState::Faulted) ||
            static_cast<std::uint32_t>(response.index_state) > static_cast<std::uint32_t>(ipc::IndexState::Corrupted))
        {
            return ERROR_INVALID_DATA;
        }
        return ERROR_SUCCESS;
    }

    VolumeIndexStatus MapVolumeIndexStatus(std::wstring root, const IndexRpcResult& result)
    {
        namespace ipc = blink::index::ipc;
        VolumeIndexStatus status;
        status.root = std::move(root);
        status.error = ValidateIndexReply(result);
        if (status.error != ERROR_SUCCESS)
        {
            status.stage = VolumeIndexStage::Unavailable;
            return status;
        }
        const auto& response = result.response;
        status.request_id = response.request_id;
        status.error = response.error;
        if (response.status == ipc::ResponseStatus::Busy || response.error == ERROR_IO_PENDING)
        {
            // An in-flight operation snapshot has no volume identifier in v1.
            status.error = ERROR_IO_PENDING;
            return status;
        }
        status.records_seen = response.records_seen;
        status.files = response.files;
        status.directories = response.directories;
        switch (response.index_state)
        {
        case ipc::IndexState::Healthy:
            status.stage = response.status == ipc::ResponseStatus::Ok && response.error == ERROR_SUCCESS ?
                VolumeIndexStage::Healthy : VolumeIndexStage::Unavailable;
            break;
        case ipc::IndexState::CatchingUp: status.stage = VolumeIndexStage::CatchingUp; break;
        case ipc::IndexState::RebuildRequired: status.stage = VolumeIndexStage::RebuildRequired; break;
        case ipc::IndexState::Rebuilding: status.stage = VolumeIndexStage::Rebuilding; break;
        case ipc::IndexState::Unavailable: status.stage = VolumeIndexStage::Unavailable; break;
        case ipc::IndexState::Corrupted: status.stage = VolumeIndexStage::Corrupted; break;
        default: break;
        }
        return status;
    }

    std::uint64_t IndexServiceState::Begin() noexcept
    {
        std::scoped_lock lock(mutex_);
        if (closed_ || snapshot_.stage == IndexConnectionStage::Refreshing) { return 0; }
        ++snapshot_.generation;
        snapshot_.stage = IndexConnectionStage::Refreshing;
        snapshot_.error = ERROR_SUCCESS;
        return snapshot_.generation;
    }

    bool IndexServiceState::IsCurrent(const std::uint64_t generation) const noexcept
    {
        std::scoped_lock lock(mutex_);
        return !closed_ && generation != 0 && generation == snapshot_.generation;
    }

    bool IndexServiceState::Complete(const std::uint64_t generation, IndexServiceSnapshot snapshot)
    {
        std::scoped_lock lock(mutex_);
        if (closed_ || generation == 0 || generation != snapshot_.generation ||
            snapshot_.stage != IndexConnectionStage::Refreshing ||
            (snapshot.stage != IndexConnectionStage::Available && snapshot.stage != IndexConnectionStage::Unavailable))
        {
            return false;
        }
        snapshot.generation = generation;
        snapshot_ = std::move(snapshot);
        return true;
    }

    IndexServiceSnapshot IndexServiceState::Snapshot() const
    {
        std::scoped_lock lock(mutex_);
        return snapshot_;
    }

    void IndexServiceState::Shutdown() noexcept
    {
        std::scoped_lock lock(mutex_);
        closed_ = true;
        ++snapshot_.generation;
        snapshot_.stage = IndexConnectionStage::Closed;
    }
}
