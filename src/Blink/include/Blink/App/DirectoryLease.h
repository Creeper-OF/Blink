#pragma once
#include "Blink/Core/FileTypes.h"
#include <atomic>
#include <memory>

namespace blink::app::detail
{
    // A short worker-only lease for an identity-checked local directory chain.
    struct DirectoryLease
    {
        DWORD error{ ERROR_SUCCESS };
        std::wstring path;
        std::wstring native_path;
        std::vector<blink::core::UniqueHandle> handles;
    };
    [[nodiscard]] DirectoryLease AcquireDirectoryLease(const std::wstring& path,
        const blink::core::FileIdentity& expected, const std::shared_ptr<std::atomic_bool>& cancelled = {});
}
