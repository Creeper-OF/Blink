#include "Blink/App/BrowseState.h"

#include <utility>

namespace blink::app
{
    std::uint64_t BrowseState::Begin(std::wstring normalized_path)
    {
        std::scoped_lock lock(mutex_);
        if (shutting_down_)
        {
            return 0;
        }

        ++generation_;
        requested_path_ = std::move(normalized_path);
        state_ = BrowseStateKind::Loading;
        error_ = ERROR_IO_PENDING;
        return generation_;
    }

    bool BrowseState::IsCurrent(
        const std::uint64_t generation,
        const std::wstring& requested_path) const
    {
        std::scoped_lock lock(mutex_);
        return !shutting_down_ &&
            generation == generation_ &&
            requested_path == requested_path_;
    }

    bool BrowseState::Complete(
        const std::uint64_t generation,
        const std::wstring& requested_path,
        const DWORD error) noexcept
    {
        std::scoped_lock lock(mutex_);
        if (shutting_down_ || generation != generation_ ||
            requested_path != requested_path_)
        {
            return false;
        }

        error_ = error;
        state_ = error == ERROR_SUCCESS ?
            BrowseStateKind::Completed : BrowseStateKind::Failed;
        return true;
    }

    void BrowseState::Shutdown() noexcept
    {
        std::scoped_lock lock(mutex_);
        shutting_down_ = true;
        ++generation_;
        state_ = BrowseStateKind::Idle;
        error_ = ERROR_CANCELLED;
    }

    void BrowseState::CancelPending() noexcept
    {
        std::scoped_lock lock(mutex_);
        if (shutting_down_)
        {
            return;
        }
        ++generation_;
        state_ = BrowseStateKind::Idle;
        error_ = ERROR_CANCELLED;
    }

    BrowseSnapshot BrowseState::Snapshot() const
    {
        std::scoped_lock lock(mutex_);
        BrowseSnapshot snapshot;
        snapshot.requested_path = requested_path_;
        snapshot.generation = generation_;
        snapshot.state = state_;
        snapshot.error = error_;
        snapshot.shutting_down = shutting_down_;
        return snapshot;
    }
}
