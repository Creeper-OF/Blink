#pragma once
#include "Blink/App/SearchState.h"
#include "Blink/App/ItemVisibilityPolicy.h"
#include <optional>

namespace blink::app
{
    struct SearchViewData
    {
        std::vector<SearchResult> rows;
        std::optional<std::size_t> selected_index;
        bool limit_reached{};
        bool query_incomplete{};
    };
    [[nodiscard]] SearchViewData BuildSearchView(const SearchSnapshot& snapshot, ItemVisibilityPolicy policy,
        const std::optional<SearchResult>& selection);
    [[nodiscard]] bool CanAppendSearchRows(const std::vector<SearchResult>& current,
        const std::vector<SearchResult>& incoming) noexcept;
}
