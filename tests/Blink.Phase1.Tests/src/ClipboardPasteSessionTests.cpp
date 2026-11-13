#include "ClipboardPasteSessionTests.h"
#include "Blink/App/ClipboardPasteSession.h"

#include <shlobj.h>
#include <wrl.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace blink::app;
    using namespace std::chrono_literals;
    bool Check(const bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }

    struct Observed
    {
        std::atomic<unsigned> acquired{}, reads{}, writes{}, callbacks{}, destroyed{}, messages{}, sequence_calls{};
        std::atomic<DWORD> sequence{ 71 }, thread{}, release_thread{};
        std::atomic_bool wrong_apartment{}, blocked{}, changed_on_read{}, cancelled_on_read{}, changed_on_feedback{};
        std::atomic<HWND> message_window{};
        HANDLE thread_handle{};
        HANDLE entered{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        HANDLE release{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        HANDLE message_received{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        ClipboardTransferIntent intent{ ClipboardTransferIntent::Move };
        FileOperationCancellation cancellation;
        HRESULT read_error{ S_OK }, feedback_error{ S_OK };
        bool with_message_window{};
        std::mutex mutex;
        std::vector<std::pair<CLIPFORMAT, DWORD>> effects;
        std::promise<ClipboardPasteSessionReport> report;
        ~Observed()
        {
            if (thread_handle) { CloseHandle(thread_handle); }
            if (entered) { CloseHandle(entered); }
            if (release) { CloseHandle(release); }
            if (message_received) { CloseHandle(message_received); }
        }
        void Apartment() noexcept
        {
            APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
            if (CoGetApartmentType(&type, &qualifier) != S_OK ||
                (type != APTTYPE_STA && type != APTTYPE_MAINSTA) || GetCurrentThreadId() != thread.load())
            { wrong_apartment = true; }
        }
    };

    LRESULT CALLBACK MessageWindow(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE)
        { SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams)); }
        auto* observed = reinterpret_cast<Observed*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_APP + 73 && observed)
        { observed->Apartment(); ++observed->messages; SetEvent(observed->message_received); return 0; }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    class Files final : public Microsoft::WRL::RuntimeClass<
        Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDataObject>
    {
    public:
        explicit Files(std::shared_ptr<Observed> observed) : observed_(std::move(observed))
        {
            observed_->thread = GetCurrentThreadId(); observed_->Apartment();
            (void)DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                &observed_->thread_handle, SYNCHRONIZE, FALSE, 0);
            if (observed_->with_message_window)
            {
                WNDCLASSW window_class{}; window_class.lpfnWndProc = MessageWindow;
                window_class.hInstance = GetModuleHandleW(nullptr);
                window_class.lpszClassName = L"BlinkClipboardPasteSessionMessageTest";
                const auto registered = RegisterClassW(&window_class);
                if (registered || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
                {
                    observed_->message_window = CreateWindowExW(0, window_class.lpszClassName, L"", 0,
                        0, 0, 0, 0, HWND_MESSAGE, nullptr, window_class.hInstance, observed_.get());
                }
            }
        }
        ~Files()
        {
            observed_->Apartment(); observed_->release_thread = GetCurrentThreadId();
            if (const auto window = observed_->message_window.load()) { DestroyWindow(window); }
            ++observed_->destroyed;
        }
        HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* output) override
        {
            observed_->Apartment(); ++observed_->reads;
            if (!format || !output) { return E_POINTER; }
            *output = {};
            if (observed_->read_error != S_OK) { return observed_->read_error; }
            std::vector<std::byte> bytes;
            if (format->cfFormat == CF_HDROP)
            {
                const std::vector<std::wstring> paths{ L"D:\\source\\文件.txt", L"D:\\source\\目录" };
                bytes = EncodeClipboardFilePaths(paths);
                if (observed_->changed_on_read) { ++observed_->sequence; }
                if (observed_->cancelled_on_read && observed_->cancellation) { *observed_->cancellation = true; }
            }
            else if (format->cfFormat == RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT))
            {
                const auto effect = ClipboardDropEffectValue(observed_->intent);
                bytes.resize(sizeof(effect)); std::memcpy(bytes.data(), &effect, sizeof(effect));
            }
            else { return DV_E_FORMATETC; }
            const auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes.size());
            const auto pointer = memory ? GlobalLock(memory) : nullptr;
            if (!pointer) { if (memory) { GlobalFree(memory); } return E_OUTOFMEMORY; }
            std::memcpy(pointer, bytes.data(), bytes.size()); GlobalUnlock(memory);
            output->tymed = TYMED_HGLOBAL; output->hGlobal = memory; return S_OK;
        }
        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override
        {
            observed_->Apartment();
            return format && (format->cfFormat == CF_HDROP ||
                format->cfFormat == RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)) ? S_OK : DV_E_FORMATETC;
        }
        HRESULT STDMETHODCALLTYPE SetData(FORMATETC* format, STGMEDIUM* medium, BOOL release) override
        {
            observed_->Apartment(); ++observed_->writes;
            if (observed_->blocked)
            { SetEvent(observed_->entered); if (WaitForSingleObject(observed_->release, 5000) != WAIT_OBJECT_0) { return E_ABORT; } }
            if (observed_->feedback_error != S_OK) { return observed_->feedback_error; }
            if (!format || !medium || medium->tymed != TYMED_HGLOBAL || GlobalSize(medium->hGlobal) < sizeof(DWORD))
            { return DV_E_FORMATETC; }
            const auto pointer = GlobalLock(medium->hGlobal); if (!pointer) { return E_FAIL; }
            DWORD effect{}; std::memcpy(&effect, pointer, sizeof(effect)); GlobalUnlock(medium->hGlobal);
            { std::scoped_lock lock(observed_->mutex); observed_->effects.emplace_back(format->cfFormat, effect); }
            if (observed_->changed_on_feedback) { ++observed_->sequence; }
            if (release) { ReleaseStgMedium(medium); }
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* output) override
        { if (!output) { return E_POINTER; } output->ptd = nullptr; return DATA_S_SAMEFORMATETC; }
        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
    private:
        std::shared_ptr<Observed> observed_;
    };

    struct Fixture
    {
        std::shared_ptr<Observed> observed{ std::make_shared<Observed>() };
        std::future<ClipboardPasteSessionReport> report{ observed->report.get_future() };
        ~Fixture() { SetEvent(observed->release); }
        ClipboardPasteSessionApis Apis() const
        {
            return {
                [observed = observed](IDataObject** object) {
                    ++observed->acquired; auto files = Microsoft::WRL::Make<Files>(observed);
                    return files ? files.CopyTo(object) : E_OUTOFMEMORY;
                },
                [observed = observed] { ++observed->sequence_calls; return observed->sequence.load(); }
            };
        }
        ClipboardPasteSessionCallback Callback(bool throwing = false) const
        {
            return [observed = observed, throwing](ClipboardPasteSessionReport value) {
                if (++observed->callbacks == 1) { observed->report.set_value(std::move(value)); }
                if (throwing) { throw std::runtime_error("test callback"); }
            };
        }
        ClipboardPasteCapture Capture(const FileOperationCancellation& cancelled = {}, bool throwing = false)
        { return CaptureClipboardPaste(cancelled, Callback(throwing), Apis()); }
        bool Ended() const
        { return observed->thread_handle && WaitForSingleObject(observed->thread_handle, 5000) == WAIT_OBJECT_0; }
        ClipboardPasteSessionReport Finished()
        {
            if (report.wait_for(5s) != std::future_status::ready) { throw std::runtime_error("clipboard STA settlement timeout"); }
            auto result = report.get();
            if (!Ended()) { throw std::runtime_error("clipboard STA thread did not end"); }
            return result;
        }
        bool SameApartment() const
        { return !observed->wrong_apartment && observed->destroyed == 1 && observed->thread == observed->release_thread; }
    };

    bool Effects(const Observed& observed, DWORD performed, DWORD logical, DWORD pasted)
    {
        const std::vector<std::pair<CLIPFORMAT, DWORD>> expected{
            { static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT)), performed },
            { static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_LOGICALPERFORMEDDROPEFFECT)), logical },
            { static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PASTESUCCEEDED)), pasted } };
        return observed.effects == expected;
    }
}

bool RunClipboardPasteSessionTests()
{
    bool passed = true;
    try
    {
        {
            Fixture fixture; const auto cancelled = std::make_shared<std::atomic_bool>(true);
            const auto capture = fixture.Capture(cancelled);
            passed &= Check(capture.error == HRESULT_FROM_WIN32(ERROR_CANCELLED) && capture.payload.error == ERROR_CANCELLED &&
                !capture.session && !fixture.observed->acquired && !fixture.observed->sequence_calls && !fixture.observed->callbacks,
                "clipboard_paste_session_precancelled_capture_has_no_source_or_clipboard_calls");
        }
        {
            Fixture fixture; auto apis = fixture.Apis(); apis.acquire = [](IDataObject**) { return E_ACCESSDENIED; };
            const auto capture = CaptureClipboardPaste({}, fixture.Callback(), std::move(apis));
            passed &= Check(capture.error == E_ACCESSDENIED && capture.payload.error == ERROR_ACCESS_DENIED &&
                !capture.session && !fixture.observed->callbacks, "clipboard_paste_session_acquire_failure_returns_without_completion_receipt");
        }
        {
            Fixture fixture; auto apis = fixture.Apis();
            apis.acquire = [observed = fixture.observed](IDataObject** object) {
                auto files = Microsoft::WRL::Make<Files>(observed); (void)files.CopyTo(object); return S_FALSE;
            };
            const auto capture = CaptureClipboardPaste({}, fixture.Callback(), std::move(apis));
            passed &= Check(capture.error == S_FALSE && !capture.session && !fixture.observed->reads &&
                fixture.SameApartment() && fixture.Ended(), "clipboard_paste_session_positive_non_success_acquire_does_not_accept_payload");
        }
        {
            Fixture fixture; auto apis = fixture.Apis(); apis.acquire = [](IDataObject** object) { *object = nullptr; return S_OK; };
            const auto capture = CaptureClipboardPaste({}, fixture.Callback(), std::move(apis));
            passed &= Check(capture.error == E_UNEXPECTED && !capture.session && !fixture.observed->callbacks,
                "clipboard_paste_session_null_success_object_is_not_a_valid_capture");
        }
        {
            Fixture fixture; auto apis = fixture.Apis(); apis.acquire = [](IDataObject**) -> HRESULT { throw std::bad_alloc(); };
            const auto capture = CaptureClipboardPaste({}, fixture.Callback(), std::move(apis));
            passed &= Check(capture.error == E_OUTOFMEMORY && capture.payload.error == ERROR_NOT_ENOUGH_MEMORY && !capture.session,
                "clipboard_paste_session_factory_allocation_failure_ends_capture");
        }
        {
            Fixture fixture; auto apis = fixture.Apis(); apis.sequence = []() -> DWORD { throw std::runtime_error("sequence"); };
            const auto capture = CaptureClipboardPaste({}, fixture.Callback(), std::move(apis));
            passed &= Check(capture.error == E_FAIL && !capture.session && !fixture.observed->acquired,
                "clipboard_paste_session_sequence_failure_does_not_acquire_a_source");
        }
        {
            Fixture fixture; fixture.observed->read_error = DV_E_FORMATETC;
            const auto capture = fixture.Capture();
            passed &= Check(capture.error == DV_E_FORMATETC && !capture.session && fixture.SameApartment() &&
                fixture.Ended() && !fixture.observed->callbacks, "clipboard_paste_session_decode_failure_releases_on_original_sta");
        }
        {
            Fixture fixture; const auto cancelled = std::make_shared<std::atomic_bool>(false); auto apis = fixture.Apis();
            const auto acquire = apis.acquire;
            apis.acquire = [acquire, cancelled](IDataObject** object) { const auto result = acquire(object); *cancelled = true; return result; };
            const auto capture = CaptureClipboardPaste(cancelled, fixture.Callback(), std::move(apis));
            passed &= Check(capture.error == HRESULT_FROM_WIN32(ERROR_CANCELLED) && !capture.session &&
                !fixture.observed->reads && fixture.SameApartment() && fixture.Ended(),
                "clipboard_paste_session_cancellation_after_acquire_prevents_getdata");
        }
        {
            Fixture fixture; fixture.observed->cancellation = std::make_shared<std::atomic_bool>(false);
            fixture.observed->cancelled_on_read = true; const auto capture = fixture.Capture(fixture.observed->cancellation);
            passed &= Check(capture.error == HRESULT_FROM_WIN32(ERROR_CANCELLED) && !capture.session &&
                !fixture.observed->writes && fixture.SameApartment() && fixture.Ended(),
                "clipboard_paste_session_cancellation_during_read_never_starts_a_paste_lease");
        }
        {
            Fixture fixture; fixture.observed->changed_on_read = true; const auto capture = fixture.Capture();
            passed &= Check(capture.error == HRESULT_FROM_WIN32(ERROR_RETRY) && capture.payload.error == ERROR_RETRY &&
                !capture.session && fixture.observed->acquired == 1 && !fixture.observed->writes && fixture.Ended(),
                "clipboard_paste_session_capture_replacement_rejects_mixed_snapshot_without_reacquire");
        }
        {
            Fixture fixture; fixture.observed->intent = ClipboardTransferIntent::Copy; auto capture = fixture.Capture();
            passed &= Check(capture.error == S_OK && capture.payload.error == ERROR_SUCCESS && capture.payload.sequence == 71 &&
                capture.payload.paths.size() == 2 && capture.payload.intent == ClipboardTransferIntent::Copy &&
                capture.session && fixture.observed->thread != GetCurrentThreadId() && !fixture.observed->wrong_apartment,
                "clipboard_paste_session_capture_returns_complete_payload_from_separate_sta");
            capture.session->Complete(ClipboardTransferIntent::Copy, true, S_OK); const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::Finished && result.error == S_OK &&
                result.feedback.fully_completed && Effects(*fixture.observed, DROPEFFECT_COPY, DROPEFFECT_COPY, DROPEFFECT_COPY) &&
                fixture.SameApartment(), "clipboard_paste_session_copy_receipt_and_release_use_original_sta");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK);
            const auto result = fixture.Finished();
            passed &= Check(result.error == S_OK && result.feedback.fully_completed &&
                Effects(*fixture.observed, DROPEFFECT_NONE, DROPEFFECT_MOVE, DROPEFFECT_MOVE) && fixture.SameApartment(),
                "clipboard_paste_session_optimized_move_never_requests_second_source_deletion");
        }
        {
            Fixture fixture; auto apis = fixture.Apis(); const auto original = apis.acquire;
            apis.acquire = [original, observed = fixture.observed](IDataObject** object) {
                if (observed->acquired) { return E_UNEXPECTED; } return original(object);
            };
            auto capture = CaptureClipboardPaste({}, fixture.Callback(), std::move(apis));
            capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK); const auto result = fixture.Finished();
            passed &= Check(result.error == S_OK && fixture.observed->acquired == 1 && fixture.observed->writes == 3,
                "clipboard_paste_session_completion_uses_captured_object_without_getting_clipboard_again");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); ++fixture.observed->sequence;
            capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK); const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::SkippedClipboardChanged && result.error == S_FALSE &&
                result.captured_sequence == 71 && result.current_sequence == 72 && !fixture.observed->writes &&
                result.feedback.operation_result == S_OK && !result.feedback.warning && fixture.SameApartment(),
                "clipboard_paste_session_new_clipboard_skips_old_feedback_without_changing_file_success");
        }
        {
            Fixture fixture; fixture.observed->sequence = 0; auto capture = fixture.Capture();
            capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK); const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::SkippedClipboardChanged && !fixture.observed->writes,
                "clipboard_paste_session_unknown_sequence_does_not_notify_unbound_clipboard");
        }
        {
            Fixture fixture; fixture.observed->changed_on_feedback = true; auto capture = fixture.Capture();
            capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK); const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::SkippedClipboardChanged && fixture.observed->writes == 1 &&
                result.feedback.performed == S_OK && !result.feedback.paste_succeeded_attempted && !result.feedback.warning,
                "clipboard_paste_session_replacement_inside_feedback_stops_before_next_format");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); capture.session->Complete(ClipboardTransferIntent::Move, false, S_OK);
            const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::Finished && !fixture.observed->writes &&
                !result.feedback.attempted && !result.feedback.fully_completed && result.feedback.operation_result == S_OK,
                "clipboard_paste_session_partial_or_noop_success_does_not_send_full_receipt");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); capture.session->Complete(ClipboardTransferIntent::Move, false, E_ACCESSDENIED);
            const auto result = fixture.Finished();
            passed &= Check(!fixture.observed->writes && !result.feedback.fully_completed && result.feedback.operation_result == E_ACCESSDENIED,
                "clipboard_paste_session_failed_file_operation_retains_its_result_without_success_metadata");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); const auto cancelled = HRESULT_FROM_WIN32(ERROR_CANCELLED);
            capture.session->Complete(ClipboardTransferIntent::Move, false, cancelled); const auto result = fixture.Finished();
            passed &= Check(!fixture.observed->writes && result.feedback.operation_result == cancelled && !result.feedback.warning,
                "clipboard_paste_session_cancelled_file_operation_sends_no_success_receipt");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); capture.session->Complete(ClipboardTransferIntent::Move, true, E_ABORT);
            const auto result = fixture.Finished();
            passed &= Check(result.error == S_OK && result.feedback.fully_completed && result.feedback.operation_result == E_ABORT &&
                Effects(*fixture.observed, DROPEFFECT_NONE, DROPEFFECT_MOVE, DROPEFFECT_MOVE),
                "clipboard_paste_session_verified_commit_survives_late_cancel_diagnostic");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); capture.session->Cancel(); const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::Cancelled && result.error == E_ABORT &&
                !fixture.observed->writes && fixture.observed->callbacks == 1 && fixture.SameApartment(),
                "clipboard_paste_session_explicit_cancel_settles_once_without_foreign_success");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); capture.session.reset(); const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::Cancelled && !fixture.observed->writes &&
                fixture.observed->callbacks == 1 && fixture.SameApartment(),
                "clipboard_paste_session_abandoned_lease_releases_source_and_sta_without_joining_ui");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK);
            capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK); capture.session->Cancel(); capture.session.reset();
            const auto result = fixture.Finished();
            passed &= Check(result.error == S_OK && fixture.observed->writes == 3 && fixture.observed->callbacks == 1,
                "clipboard_paste_session_duplicate_completion_cancel_and_destruction_emit_once");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); capture.session->Cancel();
            capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK); const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::Cancelled && !fixture.observed->writes,
                "clipboard_paste_session_cancel_first_cannot_be_replaced_by_late_success");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); std::vector<std::thread> callers;
            for (unsigned index = 0; index < 4; ++index)
            { callers.emplace_back([session = capture.session] { session->Complete(ClipboardTransferIntent::Move, true, S_OK); }); }
            for (auto& caller : callers) { caller.join(); }
            const auto result = fixture.Finished();
            passed &= Check(result.error == S_OK && fixture.observed->writes == 3 && fixture.observed->callbacks == 1,
                "clipboard_paste_session_concurrent_completion_has_one_receipt");
        }
        {
            Fixture fixture; fixture.observed->blocked = true; auto capture = fixture.Capture();
            capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK);
            const bool entered = WaitForSingleObject(fixture.observed->entered, 2000) == WAIT_OBJECT_0;
            const bool still_pending = fixture.observed->callbacks == 0;
            SetEvent(fixture.observed->release); const auto result = fixture.Finished();
            passed &= Check(entered && still_pending && result.error == S_OK && fixture.observed->writes == 3,
                "clipboard_paste_session_complete_returns_before_blocked_foreign_setdata_finishes");
        }
        {
            Fixture fixture; auto capture = fixture.Capture({}, true); capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK);
            const auto result = fixture.Finished();
            passed &= Check(result.error == S_OK && fixture.observed->callbacks == 1 && fixture.SameApartment(),
                "clipboard_paste_session_throwing_client_callback_cannot_leak_sta_or_source");
        }
        {
            Fixture fixture; fixture.observed->feedback_error = E_NOTIMPL; auto capture = fixture.Capture();
            capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK); const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::Finished && result.error == S_FALSE &&
                result.feedback.unsupported && result.feedback.operation_result == S_OK && !result.feedback.paste_succeeded_attempted,
                "clipboard_paste_session_optional_unsupported_feedback_does_not_fail_committed_file_work");
        }
        {
            Fixture fixture; fixture.observed->feedback_error = E_ACCESSDENIED; auto capture = fixture.Capture();
            capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK); const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::Failed && result.error == E_ACCESSDENIED &&
                result.feedback.warning && result.feedback.operation_result == S_OK && !result.feedback.paste_succeeded_attempted,
                "clipboard_paste_session_provider_failure_is_notification_warning_not_file_rollback");
        }
        {
            Fixture fixture; auto apis = fixture.Apis();
            apis.sequence = [observed = fixture.observed]() -> DWORD {
                if (++observed->sequence_calls > 2) { throw std::runtime_error("settlement sequence"); }
                return observed->sequence.load();
            };
            auto capture = CaptureClipboardPaste({}, fixture.Callback(), std::move(apis));
            capture.session->Complete(ClipboardTransferIntent::Move, true, S_OK); const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::Failed && result.error == E_FAIL &&
                result.feedback.warning && result.feedback.operation_result == S_OK && !fixture.observed->writes && fixture.SameApartment(),
                "clipboard_paste_session_settlement_guard_failure_releases_original_sta_without_false_success");
        }
        {
            Fixture fixture; auto capture = fixture.Capture(); capture.session->Complete(ClipboardTransferIntent::Copy, true, S_OK);
            const auto result = fixture.Finished();
            passed &= Check(result.status == ClipboardPasteSessionStatus::Failed && result.error == E_INVALIDARG &&
                !fixture.observed->writes && fixture.SameApartment(),
                "clipboard_paste_session_mismatched_intent_never_notifies_wrong_transfer_semantics");
        }
        {
            Fixture fixture; fixture.observed->with_message_window = true; auto capture = fixture.Capture();
            const auto window = fixture.observed->message_window.load();
            const bool posted = window && PostMessageW(window, WM_APP + 73, 0, 0);
            const bool pumped = posted && WaitForSingleObject(fixture.observed->message_received, 2000) == WAIT_OBJECT_0;
            capture.session->Cancel(); const auto result = fixture.Finished();
            passed &= Check(pumped && fixture.observed->messages == 1 && !IsWindow(window) && fixture.SameApartment() &&
                result.status == ClipboardPasteSessionStatus::Cancelled,
                "clipboard_paste_session_wait_pumps_owned_message_only_window_and_releases_it");
        }
    }
    catch (const std::exception& error)
    { passed &= Check(false, "clipboard_paste_session_unexpected_exception"); std::cout << "detail=" << error.what() << '\n'; }
    return passed;
}
