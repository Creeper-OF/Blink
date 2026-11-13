#include "Blink/App/ClipboardPasteSession.h"

#include <ole2.h>
#include <atomic>
#include <future>
#include <new>
#include <thread>
#include <winrt/base.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "User32.lib")

namespace blink::app
{
    namespace
    {
        struct OleApartment
        {
            HRESULT result{ OleInitialize(nullptr) };
            ~OleApartment() { Close(); }
            void Close() noexcept
            { if (SUCCEEDED(result)) { OleUninitialize(); result = E_FAIL; } }
        };

        ClipboardPasteCapture CaptureFailure(const HRESULT error) noexcept
        {
            ClipboardPasteCapture capture;
            capture.error = error;
            // Normalize allocation failures before unpacking Win32 HRESULTs.
            capture.payload.error = error == E_OUTOFMEMORY ? ERROR_NOT_ENOUGH_MEMORY :
                HRESULT_FACILITY(error) == FACILITY_WIN32 ? HRESULT_CODE(error) :
                error == E_ACCESSDENIED ? ERROR_ACCESS_DENIED :
                error == E_ABORT ? ERROR_CANCELLED : ERROR_GEN_FAILURE;
            return capture;
        }

        bool IsCancelled(const FileOperationCancellation& cancelled) noexcept
        { return cancelled && cancelled->load(); }

        DWORD Sequence(const ClipboardPasteSessionApis& apis)
        { return apis.sequence ? apis.sequence() : GetClipboardSequenceNumber(); }

        using Ready = std::shared_ptr<std::promise<ClipboardPasteCapture>>;
        bool SetReady(Ready& ready, ClipboardPasteCapture capture) noexcept
        {
            try { ready->set_value(std::move(capture)); ready.reset(); return true; }
            catch (...) { ready.reset(); return false; }
        }
    }

    struct ClipboardPasteSession::State : std::enable_shared_from_this<State>
    {
        struct Completion
        {
            ClipboardTransferIntent intent{ ClipboardTransferIntent::Copy };
            bool fully_completed{}, cancelled{};
            HRESULT operation_result{ E_ABORT };
        };

        HANDLE event{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        std::atomic_bool claimed{}, ready{}, ended{};
        Completion completion;
        ClipboardTransferIntent captured_intent{ ClipboardTransferIntent::Copy };
        ClipboardPasteSessionCallback callback;

        ~State() { if (event) { CloseHandle(event); } }

        void Submit(const Completion& value) noexcept
        {
            if (ended.load(std::memory_order_acquire)) { return; }
            bool expected = false;
            if (!claimed.compare_exchange_strong(expected, true)) { return; }
            completion = value;
            // Publish the primitive record before signalling. No mutex/RPC
            // or allocation occurs in a UI/destructor submission.
            ready.store(true, std::memory_order_release);
            (void)SetEvent(event);
        }

        HRESULT Wait() noexcept
        {
            for (;;)
            {
                if (ready.load(std::memory_order_acquire)) { return S_OK; }
                const auto result = MsgWaitForMultipleObjectsEx(1, &event, INFINITE,
                    QS_ALLINPUT, MWMO_INPUTAVAILABLE | MWMO_ALERTABLE);
                if (result == WAIT_OBJECT_0 || result == WAIT_IO_COMPLETION) { continue; }
                if (result == WAIT_OBJECT_0 + 1)
                {
                    MSG message{};
                    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                    {
                        if (message.message == WM_QUIT) { return E_ABORT; }
                        TranslateMessage(&message); DispatchMessageW(&message);
                        if (ready.load(std::memory_order_acquire)) { return S_OK; }
                    }
                    continue;
                }
                const auto error = GetLastError();
                return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
            }
        }

        void Notify(ClipboardPasteSessionReport report) noexcept
        {
            ended.store(true, std::memory_order_release);
            auto notify = std::move(callback);
            try { if (notify) { notify(std::move(report)); } } catch (...) {}
        }

        void Run(Ready ready_value, ClipboardPasteSessionApis apis,
            FileOperationCancellation cancelled) noexcept
        {
            OleApartment apartment;
            winrt::com_ptr<IDataObject> object;
            auto capture = CaptureFailure(E_FAIL);
            ClipboardPasteSessionReport report;
            report.sta_thread_id = GetCurrentThreadId();
            bool published{};
            try
            {
                if (FAILED(apartment.result)) { capture = CaptureFailure(apartment.result); }
                else if (IsCancelled(cancelled)) { capture = CaptureFailure(HRESULT_FROM_WIN32(ERROR_CANCELLED)); }
                else
                {
                    MSG message{}; (void)PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
                    const auto before = Sequence(apis);
                    const auto acquired = apis.acquire ? apis.acquire(object.put()) : OleGetClipboard(object.put());
                    if (acquired != S_OK) { capture = CaptureFailure(acquired); }
                    else if (!object) { capture = CaptureFailure(E_UNEXPECTED); }
                    else if (IsCancelled(cancelled)) { capture = CaptureFailure(HRESULT_FROM_WIN32(ERROR_CANCELLED)); }
                    else
                    {
                        auto read = ReadClipboardDataObject(object.get());
                        if (IsCancelled(cancelled)) { capture = CaptureFailure(HRESULT_FROM_WIN32(ERROR_CANCELLED)); }
                        else if (read.result != S_OK || read.payload.error != ERROR_SUCCESS)
                        { capture.error = read.result; capture.payload = std::move(read.payload); }
                        else
                        {
                            const auto after = Sequence(apis);
                            if (before != after)
                            {
                                capture = CaptureFailure(HRESULT_FROM_WIN32(ERROR_RETRY));
                                object = nullptr; apartment.Close();
                                (void)SetReady(ready_value, std::move(capture)); return;
                            }
                            capture.error = S_OK; capture.payload = std::move(read.payload); capture.payload.sequence = after;
                            captured_intent = capture.payload.intent;
                            report.captured_sequence = after;
                            capture.session = std::shared_ptr<ClipboardPasteSession>(new ClipboardPasteSession(shared_from_this()));
                            published = SetReady(ready_value, std::move(capture));
                            if (published)
                            {
                                const auto waited = Wait();
                                if (FAILED(waited))
                                {
                                    report.status = waited == E_ABORT ? ClipboardPasteSessionStatus::Cancelled : ClipboardPasteSessionStatus::Failed;
                                    report.error = waited; report.feedback.operation_result = E_ABORT;
                                }
                                else if (completion.cancelled)
                                {
                                    report.status = ClipboardPasteSessionStatus::Cancelled;
                                    report.error = E_ABORT; report.feedback.operation_result = E_ABORT;
                                }
                                else if (completion.intent != captured_intent)
                                {
                                    report.status = ClipboardPasteSessionStatus::Failed; report.error = E_INVALIDARG;
                                    report.feedback.result = E_INVALIDARG; report.feedback.warning = true;
                                    report.feedback.operation_result = completion.operation_result;
                                }
                                else
                                {
                                    const auto current = [&] {
                                        report.current_sequence = Sequence(apis);
                                        return report.captured_sequence && report.current_sequence == report.captured_sequence;
                                    };
                                    report.feedback = CompleteClipboardPaste(object.get(), completion.intent,
                                        completion.fully_completed, completion.operation_result, current);
                                    report.error = report.feedback.result;
                                    report.status = report.feedback.clipboard_changed ? ClipboardPasteSessionStatus::SkippedClipboardChanged :
                                        FAILED(report.error) ? ClipboardPasteSessionStatus::Failed : ClipboardPasteSessionStatus::Finished;
                                }
                            }
                        }
                    }
                }
            }
            catch (const std::bad_alloc&)
            {
                capture = CaptureFailure(E_OUTOFMEMORY);
                report.error = E_OUTOFMEMORY; report.status = ClipboardPasteSessionStatus::Failed;
                report.feedback.result = E_OUTOFMEMORY; report.feedback.warning = true;
            }
            catch (...)
            {
                capture = CaptureFailure(E_FAIL);
                report.error = E_FAIL; report.status = ClipboardPasteSessionStatus::Failed;
                report.feedback.result = E_FAIL; report.feedback.warning = true;
            }
            // No IDataObject survives its apartment. Reporting after teardown
            // also lets tests/callers observe a finished lease, without joining
            // this thread or running foreign COM calls on the UI thread.
            object = nullptr;
            apartment.Close();
            if (published) { Notify(std::move(report)); }
            else if (ready_value) { (void)SetReady(ready_value, std::move(capture)); }
        }
    };

    ClipboardPasteSession::~ClipboardPasteSession() noexcept { Cancel(); }
    void ClipboardPasteSession::Complete(const ClipboardTransferIntent intent,
        const bool fully_completed, const HRESULT operation_result) noexcept
    { state_->Submit({ intent, fully_completed, false, operation_result }); }
    void ClipboardPasteSession::Cancel() noexcept
    { state_->Submit({ state_->captured_intent, false, true, E_ABORT }); }

    ClipboardPasteCapture CaptureClipboardPaste(const FileOperationCancellation& cancelled,
        ClipboardPasteSessionCallback completion, ClipboardPasteSessionApis apis)
    {
        if (IsCancelled(cancelled)) { return CaptureFailure(HRESULT_FROM_WIN32(ERROR_CANCELLED)); }
        try
        {
            auto state = std::make_shared<ClipboardPasteSession::State>();
            if (!state->event)
            {
                const auto error = GetLastError();
                return CaptureFailure(HRESULT_FROM_WIN32(error ? error : ERROR_NOT_ENOUGH_MEMORY));
            }
            state->callback = std::move(completion);
            auto ready_value = std::make_shared<std::promise<ClipboardPasteCapture>>();
            auto result = ready_value->get_future();
            std::thread([state, ready_value, apis = std::move(apis), cancelled]() mutable {
                state->Run(std::move(ready_value), std::move(apis), cancelled);
            }).detach();
            return result.get();
        }
        catch (const std::bad_alloc&) { return CaptureFailure(E_OUTOFMEMORY); }
        catch (...) { return CaptureFailure(E_FAIL); }
    }
}
