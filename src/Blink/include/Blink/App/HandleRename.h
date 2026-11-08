#pragma once
#include "Blink/App/FileOperations.h"
namespace blink::app::detail
{
    struct DirectoryLease;
    [[nodiscard]] DWORD RenameRelativeByHandle(HANDLE source, HANDLE parent, const std::wstring& leaf,
        bool replace_existing = false) noexcept;
    [[nodiscard]] DWORD ReadFileReplacementTarget(const DirectoryLease& parent, const std::wstring& leaf,
        const blink::core::FileIdentity& source, blink::core::FileIdentity& target) noexcept;
}
