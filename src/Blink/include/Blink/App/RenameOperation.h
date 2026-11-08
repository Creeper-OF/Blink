#pragma once
#include "Blink/App/FileOperations.h"

namespace blink::app
{
    struct RenameRequest
    {
        std::wstring parent;
        blink::core::FileIdentity parent_identity;
        blink::core::FileEntry source;
        std::wstring new_name;
    };
    struct RenameResult
    {
        DWORD error{ ERROR_GEN_FAILURE };
        bool renamed{};
        std::wstring path;
    };
    using RenameByHandleFunction = std::function<bool(HANDLE, const std::wstring&, DWORD*)>;
    // Worker-only; source is opened and identity-checked once, then renamed
    // through that same handle. Destination never replaces an existing name.
    [[nodiscard]] RenameResult RenameEntrySafely(const RenameRequest& request,
        const FileOperationCancellation& cancelled = {}, const RenameByHandleFunction& rename = {});
}
