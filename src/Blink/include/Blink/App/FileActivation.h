#pragma once

#include "Blink/Core/FileTypes.h"

namespace blink::app
{
    [[nodiscard]] DWORD ValidateFileActivation(const blink::core::FileEntry& entry);
    [[nodiscard]] HRESULT FileActivationHostResult(DWORD exit_code) noexcept;

    // Private helper-process arguments, with Windows argv quoting. The complete
    // FileIdentity is passed alongside the current path, never replaced by it.
    [[nodiscard]] std::wstring BuildFileActivationCommandLine(
        const std::wstring& host,
        const blink::core::FileEntry& entry);
}
