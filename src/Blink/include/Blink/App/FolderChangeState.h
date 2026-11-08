#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

namespace blink::app
{
    struct FolderWatchToken
    {
        std::uint64_t generation{};
        std::wstring path;
        friend bool operator==(const FolderWatchToken&, const FolderWatchToken&) = default;
    };

    // UI-thread state only: a notification invalidates a snapshot, it is not
    // filesystem truth. Native callbacks must cross the token gate on the UI turn.
    class FolderChangeState
    {
    public:
        using Clock = std::chrono::steady_clock;
        [[nodiscard]] FolderWatchToken Begin(std::wstring path)
        {
            token_ = { token_.generation + 1, std::move(path) };
            dirty_ = false;
            return token_;
        }
        void Stop() noexcept { ++token_.generation; token_.path.clear(); dirty_ = false; }
        [[nodiscard]] const FolderWatchToken& Token() const noexcept { return token_; }
        [[nodiscard]] bool IsCurrent(const FolderWatchToken& token) const noexcept
        { return !token_.path.empty() && token == token_; }
        [[nodiscard]] bool Notify(const FolderWatchToken& token, const Clock::time_point now) noexcept
        {
            if (!IsCurrent(token)) { return false; }
            if (!dirty_) { first_change_ = now; }
            dirty_ = true;
            last_change_ = now;
            return true;
        }
        [[nodiscard]] bool TakeRefresh(const FolderWatchToken& token,
            const Clock::time_point now, const bool busy) noexcept
        {
            if (!IsCurrent(token) || !dirty_ || busy ||
                (now - last_change_ < std::chrono::milliseconds(200) &&
                    now - first_change_ < std::chrono::seconds(1)))
            {
                return false;
            }
            // A later notification starts a new batch even if this browse
            // has not finished. Busy gates keep that batch until it can run.
            dirty_ = false;
            return true;
        }
        [[nodiscard]] bool HasChanges() const noexcept { return dirty_; }

    private:
        FolderWatchToken token_;
        bool dirty_{};
        Clock::time_point first_change_{};
        Clock::time_point last_change_{};
    };
}
