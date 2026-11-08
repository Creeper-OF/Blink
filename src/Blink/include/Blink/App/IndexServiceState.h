#pragma once

#include "Blink/Index/Contracts.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace blink::app
{
    enum class IndexConnectionStage { Idle, Refreshing, Available, Unavailable, Closed };
    enum class VolumeIndexStage { Unconfirmed, Healthy, CatchingUp, RebuildRequired, Rebuilding, Unavailable, Corrupted };

    struct IndexRpcResult
    {
        bool received{};
        DWORD error{ ERROR_SUCCESS };
        // The real Client assigned and validated this wire ID. Fake adapters
        // supply it explicitly to test correlation without changing the ABI.
        std::uint64_t expected_request_id{};
        blink::index::ipc::Response response;
    };

    struct VolumeIndexStatus
    {
        std::wstring root;
        VolumeIndexStage stage{ VolumeIndexStage::Unconfirmed };
        DWORD error{ ERROR_SUCCESS };
        std::uint64_t request_id{};
        std::uint64_t records_seen{};
        std::uint64_t files{};
        std::uint64_t directories{};
    };

    struct IndexServiceSnapshot
    {
        std::uint64_t generation{};
        IndexConnectionStage stage{ IndexConnectionStage::Idle };
        blink::index::ipc::ServiceState service_state{ blink::index::ipc::ServiceState::Stopped };
        DWORD error{ ERROR_SUCCESS };
        std::uint64_t last_request_id{};
        std::uint64_t operation_id{};
        blink::index::ipc::IndexState operation_state{ blink::index::ipc::IndexState::Unknown };
        std::uint64_t operation_records{};
        bool operation_running{};
        std::wstring selected_root;
        std::vector<VolumeIndexStatus> volumes;
    };

    [[nodiscard]] DWORD ValidateIndexReply(const IndexRpcResult& result) noexcept;
    [[nodiscard]] VolumeIndexStatus MapVolumeIndexStatus(std::wstring root, const IndexRpcResult& result);

    class IndexServiceState
    {
    public:
        [[nodiscard]] std::uint64_t Begin() noexcept;
        [[nodiscard]] bool IsCurrent(std::uint64_t generation) const noexcept;
        [[nodiscard]] bool Complete(std::uint64_t generation, IndexServiceSnapshot snapshot);
        [[nodiscard]] IndexServiceSnapshot Snapshot() const;
        void Shutdown() noexcept;

    private:
        mutable std::mutex mutex_;
        IndexServiceSnapshot snapshot_;
        bool closed_{};
    };
}
