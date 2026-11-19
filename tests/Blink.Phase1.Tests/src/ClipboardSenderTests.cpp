#include "ClipboardSenderTests.h"
#include "Blink/App/ClipboardSender.h"
#include <wrl.h>
#include <atomic>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace blink::app;
    using namespace Microsoft::WRL;
    bool Check(bool value, const char* label)
    { std::cout << (value ? "PASS " : "FAIL ") << label << '\n'; return value; }
    struct Stats
    {
        HANDLE closed{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        std::atomic<DWORD> created_thread{}, published_thread{}, current_thread{}, flush_thread{}, released_thread{}, validation_thread{};
        std::atomic<unsigned> created{}, published{}, checks{}, flushed{}, released{}, sequences{}, discarded{};
        std::atomic<HRESULT> create_error{ S_OK }, publish_error{ S_OK }, current_error{ S_OK }, flush_error{ S_OK }, close_result{ E_PENDING };
        std::atomic_bool apartment_ok{}, make_window{}, window_destroyed{}, throw_create{}, throw_sequence{};
        std::atomic<HWND> window{};
        std::atomic<IDataObject*> published_object{};
        std::atomic_bool current_matches{};
        FileOperationCancellation cancel_after_create, cancel_after_publish;
        ~Stats() { if (closed) { CloseHandle(closed); } }
    };
    class Object final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDataObject>
    {
    public:
        explicit Object(std::shared_ptr<Stats> stats) : stats_(std::move(stats))
        {
            if (stats_->make_window)
            { window_ = CreateWindowExW(0, L"STATIC", L"BlinkSenderTest", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr); stats_->window = window_; }
        }
        ~Object()
        {
            if (window_) { stats_->window_destroyed = DestroyWindow(window_) != FALSE; }
            stats_->released_thread = GetCurrentThreadId(); ++stats_->released;
        }
        HRESULT STDMETHODCALLTYPE GetData(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*) override { return DV_E_FORMATETC; }
        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
    private:
        std::shared_ptr<Stats> stats_;
        HWND window_{};
    };
    ClipboardSenderApis Apis(const std::shared_ptr<Stats>& stats)
    {
        ClipboardSenderApis result;
        result.create = [stats](auto, auto, auto, auto, winrt::com_ptr<IDataObject>& output) -> HRESULT {
            ++stats->created; stats->created_thread = GetCurrentThreadId();
            APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
            stats->apartment_ok = CoGetApartmentType(&type, &qualifier) == S_OK && (type == APTTYPE_STA || type == APTTYPE_MAINSTA);
            if (stats->throw_create) { throw std::bad_alloc(); }
            const auto object = Make<Object>(stats); const auto copied = object.CopyTo(output.put());
            if (stats->cancel_after_create) { stats->cancel_after_create->store(true); }
            return FAILED(copied) ? copied : stats->create_error.load();
        };
        result.publish = [stats](IDataObject* object) {
            ++stats->published; stats->published_thread = GetCurrentThreadId();
            if (stats->cancel_after_publish) { stats->cancel_after_publish->store(true); }
            const auto error = stats->publish_error.load();
            if (error == S_OK) { stats->published_object = object; }
            return error;
        };
        result.current = [stats](IDataObject* object) {
            ++stats->checks; stats->current_thread = GetCurrentThreadId(); stats->current_matches = object == stats->published_object;
            return stats->current_error.load();
        };
        result.flush = [stats] { ++stats->flushed; stats->flush_thread = GetCurrentThreadId(); return stats->flush_error.load(); };
        result.sequence = [stats]() -> DWORD { ++stats->sequences; if (stats->throw_sequence) { throw std::runtime_error("sequence"); } return 71; };
        result.owner = [] { return reinterpret_cast<HWND>(static_cast<UINT_PTR>(0xB11C)); };
        result.discard = [stats](HWND owner) { if (owner != reinterpret_cast<HWND>(static_cast<UINT_PTR>(0xB11C))) { return E_INVALIDARG; } ++stats->discarded; return S_OK; };
        return result;
    }
    struct Fixture
    {
        std::shared_ptr<Stats> stats{ std::make_shared<Stats>() };
        ClipboardSender sender{ Apis(stats) };
        const std::vector<std::wstring> paths{ L"D:\\source\\甲.txt" };
        ClipboardSendResult Send(const FileOperationCancellation& cancelled = {}, ClipboardSourceValidation validation = {})
        { return sender.Publish(paths, ClipboardTransferIntent::Move, cancelled, {}, std::move(validation)); }
        HRESULT Close()
        {
            ResetEvent(stats->closed); const auto observed = stats;
            sender.Shutdown([observed](const HRESULT result) { observed->close_result = result; SetEvent(observed->closed); });
            return WaitForSingleObject(stats->closed, 5000) == WAIT_OBJECT_0 ? stats->close_result.load() : E_PENDING;
        }
        ~Fixture()
        {
            stats->current_error = S_OK; stats->flush_error = S_OK;
            (void)Close();
        }
    };
}
bool RunClipboardSenderTests()
{
    bool passed = true;
    {
        Fixture fixture; const auto cancelled = std::make_shared<std::atomic_bool>(true);
        passed &= Check(fixture.Send(cancelled).result == HRESULT_FROM_WIN32(ERROR_CANCELLED) && !fixture.sender.Active() && fixture.stats->created == 0,
            "clipboard_sender_precancelled_send_never_starts_sta_or_clipboard_io");
        passed &= Check(fixture.sender.Publish({}, ClipboardTransferIntent::Copy).result == E_INVALIDARG && fixture.stats->created == 0,
            "clipboard_sender_empty_payload_does_not_start_the_owner_thread");
        passed &= Check(fixture.Close() == S_OK && fixture.Send().result == HRESULT_FROM_WIN32(ERROR_CANCELLED) && !fixture.stats->created,
            "clipboard_sender_shutdown_before_first_publication_rejects_late_work");
    }
    {
        Fixture fixture; const auto caller = GetCurrentThreadId(); const auto result = fixture.Send();
        passed &= Check(result.result == S_OK && result.sequence == 71 && fixture.sender.Active() && fixture.stats->apartment_ok && fixture.stats->created_thread != caller,
            "clipboard_sender_creates_and_publishes_on_a_dedicated_sta");
        passed &= Check(fixture.stats->created_thread == fixture.stats->published_thread && fixture.stats->published == 1 && fixture.stats->sequences == 1 && fixture.stats->released == 0,
            "clipboard_sender_retains_original_object_after_publication");
        passed &= Check(fixture.Close() == S_OK && !fixture.sender.Active() && fixture.stats->flushed == 1 && fixture.stats->checks == 1 && fixture.stats->current_matches,
            "clipboard_sender_shutdown_flushes_only_its_current_object");
        passed &= Check(fixture.stats->released == 1 && fixture.stats->released_thread == fixture.stats->created_thread && fixture.stats->flush_thread == fixture.stats->created_thread && fixture.stats->current_thread == fixture.stats->created_thread,
            "clipboard_sender_flush_and_final_release_finish_on_original_sta_before_callback");
        passed &= Check(fixture.Close() == S_OK && fixture.stats->flushed == 1 && fixture.stats->released == 1,
            "clipboard_sender_repeated_shutdown_does_not_flush_or_release_twice");
    }
    {
        Fixture fixture; (void)fixture.Send(); const auto sta = fixture.stats->created_thread.load();
        const auto second = fixture.Send();
        passed &= Check(second.result == S_OK && fixture.stats->created == 2 && fixture.stats->published == 2 && fixture.stats->created_thread == sta && fixture.stats->released == 1,
            "clipboard_sender_replacement_reuses_sta_and_releases_previous_local_reference");
        fixture.stats->publish_error = E_ACCESSDENIED; const auto rejected = fixture.Send();
        passed &= Check(rejected.result == E_ACCESSDENIED && rejected.sequence == 0 && fixture.stats->released == 2 && fixture.stats->sequences == 2,
            "clipboard_sender_failed_replacement_keeps_last_successful_source");
        passed &= Check(fixture.Close() == S_OK && fixture.stats->current_matches && fixture.stats->flushed == 1 && fixture.stats->released == 3,
            "clipboard_sender_failed_new_publication_still_preserves_old_clipboard_on_exit");
    }
    {
        Fixture fixture; (void)fixture.Send(); fixture.stats->current_error = S_FALSE;
        passed &= Check(fixture.Close() == S_OK && fixture.stats->flushed == 0 && fixture.stats->released == 1,
            "clipboard_sender_external_clipboard_replacement_skips_flush_and_preserves_new_owner");
    }
    {
        Fixture fixture; (void)fixture.Send(); fixture.stats->current_error = E_FAIL;
        passed &= Check(fixture.Close() == E_FAIL && fixture.sender.Active() && fixture.stats->flushed == 0 && fixture.stats->released == 0,
            "clipboard_sender_identity_query_failure_keeps_source_alive_for_close_retry");
        fixture.stats->current_error = S_OK; fixture.stats->flush_error = E_ACCESSDENIED;
        passed &= Check(fixture.Close() == E_ACCESSDENIED && fixture.sender.Active() && fixture.stats->released == 0,
            "clipboard_sender_flush_failure_does_not_destroy_clipboard_owner");
        fixture.stats->flush_error = S_OK;
        passed &= Check(fixture.Close() == S_OK && !fixture.sender.Active() && fixture.stats->released == 1 && fixture.stats->flushed == 2,
            "clipboard_sender_close_can_retry_successfully_on_the_same_sta");
    }
    {
        Fixture fixture; fixture.stats->create_error = E_ACCESSDENIED;
        passed &= Check(fixture.Send().result == E_ACCESSDENIED && fixture.stats->published == 0 && fixture.stats->released == 1,
            "clipboard_sender_source_factory_failure_never_replaces_clipboard");
        fixture.stats->create_error = S_FALSE;
        passed &= Check(fixture.Send().result == E_UNEXPECTED && fixture.stats->published == 0,
            "clipboard_sender_positive_factory_non_success_is_not_usable_data");
        fixture.stats->create_error = S_OK; fixture.stats->throw_create = true;
        passed &= Check(fixture.Send().result == E_OUTOFMEMORY && fixture.stats->published == 0,
            "clipboard_sender_factory_allocation_failure_preserves_original_hresult");
        fixture.stats->throw_create = false;
        passed &= Check(fixture.Send().result == S_OK && fixture.Close() == S_OK,
            "clipboard_sender_factory_error_does_not_poison_subsequent_publication");
    }
    {
        Fixture fixture; const auto observed = fixture.stats;
        auto validation = [observed] { observed->validation_thread = GetCurrentThreadId(); return HRESULT_FROM_WIN32(ERROR_FILE_INVALID); };
        passed &= Check(fixture.Send({}, validation).result == HRESULT_FROM_WIN32(ERROR_FILE_INVALID) && !fixture.stats->published && fixture.stats->released == 1 &&
            fixture.stats->validation_thread == fixture.stats->created_thread,
            "clipboard_sender_rechecks_bound_source_identity_on_sta_before_publication");
        passed &= Check(fixture.Send({}, [] { return S_FALSE; }).result == E_UNEXPECTED && !fixture.stats->published,
            "clipboard_sender_positive_validation_rejection_cannot_commit_data");
    }
    {
        Fixture fixture; const auto cancelled = std::make_shared<std::atomic_bool>(false); fixture.stats->cancel_after_create = cancelled;
        passed &= Check(fixture.Send(cancelled).result == HRESULT_FROM_WIN32(ERROR_CANCELLED) && fixture.stats->published == 0 && fixture.stats->released == 1,
            "clipboard_sender_cancel_between_capture_and_publish_releases_without_writing");
    }
    {
        Fixture fixture; const auto cancelled = std::make_shared<std::atomic_bool>(false); fixture.stats->cancel_after_publish = cancelled;
        const auto result = fixture.Send(cancelled);
        passed &= Check(result.result == S_OK && result.sequence == 71 && cancelled->load() && fixture.stats->released == 0 && fixture.Close() == S_OK,
            "clipboard_sender_late_cancel_preserves_already_committed_publication");
    }
    {
        Fixture fixture; fixture.stats->publish_error = S_FALSE;
        passed &= Check(fixture.Send().result == E_UNEXPECTED && fixture.stats->sequences == 0 && fixture.stats->released == 1,
            "clipboard_sender_unconfirmed_publish_has_no_sequence_or_ownership_claim");
    }
    {
        Fixture fixture; fixture.stats->throw_sequence = true; const auto result = fixture.Send();
        passed &= Check(result.result == S_OK && result.sequence == 0 && fixture.stats->released == 0 && fixture.Close() == S_OK,
            "clipboard_sender_sequence_observation_failure_does_not_undo_publication");
    }
    {
        Fixture fixture; fixture.stats->make_window = true; const auto result = fixture.Send();
        wchar_t text[64]{}; DWORD_PTR copied{};
        const auto responded = fixture.stats->window && SendMessageTimeoutW(fixture.stats->window, WM_GETTEXT, 64,
            reinterpret_cast<LPARAM>(text), SMTO_ABORTIFHUNG, 2000, &copied);
        passed &= Check(result.result == S_OK && responded && std::wstring(text) == L"BlinkSenderTest",
            "clipboard_sender_idle_owner_pumps_messages_without_polling_or_ui_wait");
        passed &= Check(fixture.Close() == S_OK && fixture.stats->window_destroyed && fixture.stats->released_thread == fixture.stats->created_thread,
            "clipboard_sender_owned_message_window_is_destroyed_on_its_sta");
    }
    {
        Fixture fixture; const auto validity = std::make_shared<std::atomic<HRESULT>>(S_OK);
        const auto sent = fixture.Send({}, [validity] { return validity->load(); });
        validity->store(HRESULT_FROM_WIN32(ERROR_FILE_INVALID));
        passed &= Check(sent.result == S_OK && fixture.Close() == S_FALSE && !fixture.sender.Active() &&
            fixture.stats->discarded == 1 && fixture.stats->flushed == 0 && fixture.stats->released == 1,
            "clipboard_sender_expired_source_withdraws_only_its_offer_and_does_not_trap_window_close");
    }
    {
        Fixture fixture; const auto observed = fixture.stats;
        const auto calls = std::make_shared<std::atomic<unsigned>>();
        const auto sent = fixture.Send({}, [observed, calls] {
            if (++*calls > 1) { observed->current_error = S_FALSE; }
            return S_OK;
        });
        passed &= Check(sent.result == S_OK && fixture.Close() == S_OK && fixture.stats->flushed == 0 && fixture.stats->discarded == 0 &&
            fixture.stats->checks == 2 && fixture.stats->released == 1,
            "clipboard_sender_owner_changed_during_validation_is_neither_flushed_nor_discarded");
    }
    return passed;
}
