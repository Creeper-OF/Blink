#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace blink::app
{
    // Thin app-private boundary over MRT Core. ResourceLoader instances are
    // acquired per lookup so this helper does not retain language-sensitive
    // resource state across a future language/context change.
    class Localization
    {
    public:
        [[nodiscard]] std::wstring GetString(std::wstring_view key) const;

        [[nodiscard]] std::wstring Format(
            std::wstring_view key,
            std::wstring_view value) const;

        [[nodiscard]] std::wstring Format(
            std::wstring_view key,
            std::uint64_t value) const;

        [[nodiscard]] std::wstring Format(
            std::wstring_view key,
            std::uint64_t first,
            std::uint64_t second) const;

        [[nodiscard]] std::wstring Format(
            std::wstring_view key,
            std::vector<std::wstring> values) const;

        [[nodiscard]] bool IsThisPcDisplayText(std::wstring_view text) const;
    };
}
