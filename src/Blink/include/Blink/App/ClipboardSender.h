#pragma once
#include "Blink/App/ClipboardSourceData.h"

namespace blink::app
{
    struct ClipboardSendResult { HRESULT result{ E_FAIL }; DWORD sequence{}; };
    struct ClipboardSenderApis
    {
        std::function<HRESULT(std::span<const std::wstring>, ClipboardTransferIntent,
            ClipboardSourceCallback, ClipboardSourceValidation, winrt::com_ptr<IDataObject>&)> create;
        std::function<HRESULT(IDataObject*)> publish, current;
        std::function<HRESULT()> flush;
        std::function<DWORD()> sequence;
        std::function<HWND()> owner;
        std::function<HRESULT(HWND)> discard;
    };
    // One lazy STA services all source objects. Every OLE call and final release
    // stays there. Production uses the normal APIs; tests inject Clipboard I/O.
    class ClipboardSender
    {
    public:
        explicit ClipboardSender(ClipboardSenderApis apis = {});
        ~ClipboardSender() noexcept;
        ClipboardSender(const ClipboardSender&) = delete;
        ClipboardSender& operator=(const ClipboardSender&) = delete;
        // Worker only. The owning window never waits for this result.
        [[nodiscard]] ClipboardSendResult Publish(std::span<const std::wstring> paths,
            ClipboardTransferIntent intent, const FileOperationCancellation& cancelled = {},
            ClipboardSourceCallback callback = {}, ClipboardSourceValidation validation = {});
        // Returns immediately. Callback runs after a successful STA teardown;
        // a flush failure keeps the apartment/object alive and permits retry.
        // S_FALSE withdraws an expired offer belonging to this sender. A window
        // should defer its close until this callback reports a nonfailure.
        void Shutdown(std::function<void(HRESULT)> callback = {}) noexcept;
        [[nodiscard]] bool Active() const noexcept;
    private:
        struct State;
        std::shared_ptr<State> state_;
    };
}
