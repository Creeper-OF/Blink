#pragma once

#include <Windows.h>

namespace blink::app
{
    // App-private, window-wide policy. System-marked entries remain excluded
    // until the separately deferred protected-system-items policy is defined.
    struct ItemVisibilityPolicy
    {
        bool show_hidden_items{};

        [[nodiscard]] constexpr bool Includes(const DWORD attributes) const noexcept
        {
            return (attributes & FILE_ATTRIBUTE_SYSTEM) == 0 &&
                (show_hidden_items || (attributes & FILE_ATTRIBUTE_HIDDEN) == 0);
        }

        friend bool operator==(const ItemVisibilityPolicy&, const ItemVisibilityPolicy&) = default;
    };
}
