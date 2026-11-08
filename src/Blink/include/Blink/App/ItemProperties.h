#pragma once
#include "Blink/App/TabState.h"
#include <atomic>
#include <functional>
#include <memory>

namespace blink::app
{
    inline constexpr std::size_t MaximumPropertiesItems = 1024;
    struct ItemPropertiesRequest
    {
        std::wstring parent;
        blink::core::FileIdentity parent_identity;
        std::vector<blink::core::FileEntry> entries;
    };
    struct ItemPropertiesValue
    {
        blink::core::FileEntry entry;
        DWORD error{ ERROR_IO_PENDING };
    };
    struct ItemPropertiesSnapshot
    {
        std::vector<ItemPropertiesValue> items;
        std::size_t selected_count{}, selected_files{}, selected_folders{}, read_count{}, unavailable_count{}, links_not_followed{};
        std::uint64_t files{}, folders{}, bytes{}, allocated_bytes{};
        DWORD error{};
        std::wstring failure_path;
        bool finished{}, cancelled{};
    };
    using PropertiesCancellation = std::shared_ptr<std::atomic_bool>;
    using PropertiesProgress = std::function<void(const ItemPropertiesSnapshot&)>;
    [[nodiscard]] HRESULT CaptureItemProperties(const TabState& state, ItemPropertiesRequest& request);
    // Ordinary, read-only filesystem metadata. Directory totals include
    // descendants, count hardlink names separately and never follow links.
    // Cancellation is cooperative between directory enumerations/items.
    [[nodiscard]] ItemPropertiesSnapshot ReadItemProperties(const ItemPropertiesRequest& request,
        const PropertiesCancellation& cancelled = {}, const PropertiesProgress& progress = {}) noexcept;
}
