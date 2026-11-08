#pragma once

#include "Blink/Index/Contracts.h"

#include <cstdint>
#include <mutex>

namespace blink::index::service
{
    enum class ProgressStage : std::uint32_t
    {
        None = 0,
        LoadingStore,
        EnumeratingMft,
        CatchingUpUsn,
        Saving,
        Completed,
        Failed,
    };

    struct ProgressSnapshot
    {
        std::uint64_t operation_id{};
        bool initial{};
        ProgressStage stage{ ProgressStage::None };
        ipc::IndexState index_state{ ipc::IndexState::Unknown };
        std::uint64_t records_seen{};
        std::uint64_t files{};
        std::uint64_t directories{};
        std::uint64_t index_bytes{};
        std::int64_t last_usn{};
        std::int64_t target_usn{};
        std::uint32_t error{};
        bool completed{};
    };

    // The mutex protects only the small value copy.  Callers must prepare the
    // snapshot before Publish and must never perform I/O or call FastIndex
    // while this mutex is held.
    class ProgressState
    {
    public:
        ProgressState() = default;
        explicit ProgressState(const ProgressSnapshot& initial)
            : snapshot_(initial) {}

        void Publish(const ProgressSnapshot& snapshot)
        {
            std::scoped_lock lock(progress_mutex_);
            snapshot_ = snapshot;
        }

        [[nodiscard]] ProgressSnapshot Read() const
        {
            std::scoped_lock lock(progress_mutex_);
            return snapshot_;
        }

    private:
        mutable std::mutex progress_mutex_;
        ProgressSnapshot snapshot_;
    };
}
