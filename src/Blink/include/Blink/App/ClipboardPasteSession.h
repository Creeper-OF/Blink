#pragma once

#include "Blink/App/ClipboardDataObject.h"
#include <functional>
#include <memory>
#include <utility>

namespace blink::app
{
    enum class ClipboardPasteSessionStatus
    {
        Finished, SkippedClipboardChanged, Cancelled, Failed
    };

    struct ClipboardPasteSessionReport
    {
        // Notification outcome only; never a replacement for the file result.
        ClipboardPasteSessionStatus status{ ClipboardPasteSessionStatus::Failed };
        HRESULT error{ E_FAIL };
        ClipboardPasteFeedback feedback;
        DWORD captured_sequence{}, current_sequence{}, sta_thread_id{};
    };
    using ClipboardPasteSessionCallback = std::function<void(ClipboardPasteSessionReport)>;

    struct ClipboardPasteSessionApis
    {
        // Empty functions use OleGetClipboard/GetClipboardSequenceNumber.
        // Both run only on the session STA. Tests create their object there;
        // no caller-apartment COM pointer is captured or exposed by the lease.
        std::function<HRESULT(IDataObject**)> acquire;
        std::function<DWORD()> sequence;
    };

    struct ClipboardPasteCapture;
    class ClipboardPasteSession
    {
    public:
        ~ClipboardPasteSession() noexcept;
        ClipboardPasteSession(const ClipboardPasteSession&) = delete;
        ClipboardPasteSession& operator=(const ClipboardPasteSession&) = delete;

        // A native event submission only: no RPC, apartment switch or wait on
        // the caller (including the UI). The first Complete/Cancel wins.
        // fully_completed comes from verified per-item native commit facts;
        // a late cancellation or metadata warning does not erase that fact.
        void Complete(ClipboardTransferIntent intent, bool fully_completed,
            HRESULT operation_result) noexcept;
        void Cancel() noexcept;

    private:
        struct State;
        explicit ClipboardPasteSession(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}
        std::shared_ptr<State> state_;
        friend ClipboardPasteCapture CaptureClipboardPaste(const FileOperationCancellation&,
            ClipboardPasteSessionCallback, ClipboardPasteSessionApis);
    };

    struct ClipboardPasteCapture
    {
        HRESULT error{ E_FAIL };
        ClipboardFilePaths payload;
        std::shared_ptr<ClipboardPasteSession> session;
    };

    // Worker-only: waits for one STA capture; subsequent COM calls and the
    // original object's final release stay on that STA until settlement.
    // Sequence changes during capture reject with ERROR_RETRY; changes after
    // capture skip obsolete metadata feedback, without cancelling file work.
    // Capture failure/cancellation returns directly and has no completion
    // callback. Successful leases report once after COM/object teardown.
    [[nodiscard]] ClipboardPasteCapture CaptureClipboardPaste(
        const FileOperationCancellation& cancelled = {},
        ClipboardPasteSessionCallback completion = {}, ClipboardPasteSessionApis apis = {});
}
