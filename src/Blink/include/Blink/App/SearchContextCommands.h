#pragma once

#include "Blink/App/SearchResultValidation.h"
#include "Blink/App/ClipboardFileTransfer.h"
#include <cstdint>

namespace blink::app
{
    struct TabRecord;
    struct SearchContextSelection
    {
        std::uint64_t tab_id{}, query_generation{};
        SearchResult result;
    };

    // Pass the current active tab. The UI also owns the menu lifetime token.
    [[nodiscard]] bool IsSearchContextCurrent(const SearchContextSelection& selection,
        const TabRecord* tab, bool closed = false) noexcept;

    // Value conversion only: the caller validates the search result on its
    // worker before publication. No browsing-page selection or filesystem I/O.
    [[nodiscard]] DWORD BuildSearchClipboardSources(const SearchActionTarget& target,
        ClipboardCopySources& output);
}
