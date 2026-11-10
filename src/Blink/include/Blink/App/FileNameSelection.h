#pragma once

#include <cstddef>
#include <string_view>

namespace blink::app
{
    enum class FileNameSelectionMode
    {
        WholeName,
        FileStem,
    };

    struct FileNameSelection
    {
        std::size_t start{};
        std::size_t length{};
    };

    // Offsets use the input's UTF-16 code units on Windows, just like TextBox.
    // This only chooses a range; the caller supplies the object/dialog mode.
    [[nodiscard]] inline FileNameSelection BuildFileNameSelection(
        const std::wstring_view name, const FileNameSelectionMode mode) noexcept
    {
        auto length = name.size();
        if (mode == FileNameSelectionMode::FileStem)
        {
            const auto dot = name.find_last_of(L'.');
            if (dot != std::wstring_view::npos && dot != 0 && dot + 1 < name.size())
            {
                length = dot;
            }
        }
        return { 0, length };
    }
}
