#pragma once
#include "Blink/App/ClipboardFileTransfer.h"
#include <objidl.h>
#include <functional>
#include <winrt/base.h>

namespace blink::app
{
    struct ClipboardSourceReport
    {
        HRESULT result{ S_OK };
        DWORD effect{}, performed{}, logical{};
        bool completed{}, has_performed{}, has_logical{}, source_cleanup{};
    };
    using ClipboardSourceCallback = std::function<void(ClipboardSourceReport)>;
    using ClipboardSourceValidation = std::function<HRESULT()>;

    [[nodiscard]] ClipboardSourceValidation MakeClipboardSourceValidation(
        std::vector<std::wstring> paths, ClipboardCopySources expected);

    // Owning STA only. The Shell supplies its normal filesystem/CIDA support
    // and delete-on-paste implementation; do not delete the source a second time.
    // validation rechecks identities before rendering paths/content and before
    // a normal move can ask that implementation to remove originals. Optimized
    // MOVE completion stays local, because the target already moved the files.
    // No Clipboard API here.
    [[nodiscard]] HRESULT CreateClipboardSourceData(std::span<const std::wstring> paths,
        ClipboardTransferIntent intent, ClipboardSourceCallback callback,
        ClipboardSourceValidation validation, winrt::com_ptr<IDataObject>& output) noexcept;

    // Same source adapter with an explicitly supplied Shell object for headless
    // contract tests. The adapter retains it on the caller's apartment.
    [[nodiscard]] HRESULT WrapClipboardSourceData(IDataObject* shell,
        std::span<const std::wstring> paths, ClipboardTransferIntent intent,
        ClipboardSourceCallback callback, ClipboardSourceValidation validation,
        winrt::com_ptr<IDataObject>& output) noexcept;
}
