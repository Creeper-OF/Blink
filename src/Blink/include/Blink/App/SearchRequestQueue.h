#pragma once
#include "Blink/App/SearchState.h"
#include <deque>
#include <optional>
#include <algorithm>

namespace blink::app
{
    struct PendingSearchRequest
    {
        std::uint64_t tab_id{};
        SearchQuery query;
        bool retry{};
    };
    struct SearchRequestTicket
    {
        std::uint64_t serial{};
        PendingSearchRequest request;
    };
    // UI-thread owned. One service search action per window, latest pending
    // intent per tab; cancellation does not release an active native action.
    class SearchRequestQueue
    {
    public:
        void Enqueue(PendingSearchRequest request)
        {
            if (closed_ || request.tab_id == 0) { return; }
            const auto found = std::find_if(pending_.begin(), pending_.end(),
                [&request](const auto& old) { return old.tab_id == request.tab_id; });
            if (found != pending_.end()) { *found = std::move(request); }
            else { pending_.push_back(std::move(request)); }
        }
        [[nodiscard]] std::optional<SearchRequestTicket> TakeNext()
        {
            if (closed_ || active_ || pending_.empty()) { return {}; }
            active_ = SearchRequestTicket{ next_serial_++, std::move(pending_.front()) };
            pending_.pop_front();
            return active_;
        }
        [[nodiscard]] bool Finish(std::uint64_t serial) noexcept
        {
            if (!active_ || active_->serial != serial) { return false; }
            active_.reset(); return true;
        }
        void RemoveTab(std::uint64_t id) noexcept
        { std::erase_if(pending_, [id](const auto& request) { return request.tab_id == id; }); }
        void Close() noexcept { closed_ = true; pending_.clear(); }
        [[nodiscard]] bool Busy() const noexcept { return active_.has_value(); }
        [[nodiscard]] bool HasPending() const noexcept { return !pending_.empty(); }
    private:
        std::deque<PendingSearchRequest> pending_;
        std::optional<SearchRequestTicket> active_;
        std::uint64_t next_serial_{ 1 };
        bool closed_{};
    };
}
