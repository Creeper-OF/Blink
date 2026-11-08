#pragma once

#include "Blink/Core/FileTypes.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace blink::app
{
    enum class ShellMenuCommand : std::uint32_t
    {
        None = 0, CopyFullPath = 1, CopyParentPath = 2, Locate = 3,
        PasteParent = 4, Open = 5, Copy = 6, Cut = 7, Rename = 8,
        Delete = 9, PermanentDelete = 10,
    };
    enum class ShellMenuResultKind { Cancelled, BlinkCommand, NativeCommand, Error };
    struct ShellMenuRequest
    {
        blink::core::FileEntry entry;
        blink::core::FileIdentity parent_identity;
        POINT position{};
        HWND owner{};
        std::wstring locale{ L"en-US" };
        bool extended{};
        std::wstring verb;
        // Extra selected siblings. The first entry and old field order stay
        // compatible with the search-result caller.
        std::vector<blink::core::FileEntry> additional_entries;
    };
    struct ShellMenuResult
    {
        ShellMenuResultKind kind{ ShellMenuResultKind::Error };
        ShellMenuCommand command{ ShellMenuCommand::None };
        HRESULT error{ E_FAIL };
    };

    [[nodiscard]] std::wstring BuildShellMenuCommandLine(const std::wstring& host,
        const ShellMenuRequest& request, std::uintptr_t cancel_handle = 0);
    // Performs only the metadata checks needed to keep a menu tied to its
    // captured selection. Call on a worker; no Shell extensions are loaded.
    [[nodiscard]] DWORD ValidateShellMenuRequest(const ShellMenuRequest& request);
    // One UTF-8/ASCII protocol line. NativeCommand means selected, not completed.
    [[nodiscard]] bool ParseShellMenuReply(std::string_view line, ShellMenuResult& result) noexcept;
}
