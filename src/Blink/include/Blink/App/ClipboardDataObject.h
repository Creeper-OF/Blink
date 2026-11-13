#pragma once

#include "Blink/App/ClipboardFileTransfer.h"
#include <objidl.h>
#include <functional>

namespace blink::app
{
    struct ClipboardDataObjectReadResult
    {
        // Keep the provider's HRESULT; a DWORD projection cannot represent RPC
        // and COM failures faithfully. Only S_OK denotes a usable payload.
        HRESULT result{ E_FAIL };
        ClipboardFilePaths payload;
    };

    struct ClipboardPasteFeedback
    {
        // These describe notification, never the committed filesystem result.
        HRESULT result{ S_FALSE };
        HRESULT performed{ S_FALSE }, logical{ S_FALSE }, paste_succeeded{ S_FALSE };
        HRESULT operation_result{ S_FALSE };
        bool fully_completed{}, attempted{}, paste_succeeded_attempted{};
        bool unsupported{}, warning{}, clipboard_changed{};
    };

    // No clipboard-content or filesystem access. The caller retains the original
    // IDataObject on its owning apartment and supplies the captured sequence.
    [[nodiscard]] ClipboardDataObjectReadResult ReadClipboardDataObject(IDataObject* object) noexcept;
    // The session owns completion-once and clipboard-replacement checks. This
    // codec sends nothing for partial, skipped, unchanged or failed operations.
    // fully_completed is the caller's verified commit fact; a late cancellation
    // or post-commit warning in operation_result does not undo those commits.
    // A completed Blink MOVE is optimized: NONE / MOVE / MOVE, never a request
    // for source-side deletion. COPY reports COPY / COPY / COPY.
    [[nodiscard]] ClipboardPasteFeedback CompleteClipboardPaste(IDataObject* object,
        ClipboardTransferIntent intent, bool fully_completed, HRESULT operation_result,
        const std::function<bool()>& still_current = {}) noexcept;
}
