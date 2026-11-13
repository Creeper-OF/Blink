#include "ClipboardDataObjectTests.h"
#include "Blink/App/ClipboardDataObject.h"

#include <wrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace
{
    using namespace blink::app;
    using Microsoft::WRL::Make;
    using Microsoft::WRL::RuntimeClass;
    using Microsoft::WRL::RuntimeClassFlags;
    using Microsoft::WRL::ClassicCom;

    bool Check(const bool condition, const char* label)
    { std::cout << (condition ? "PASS " : "FAIL ") << label << '\n'; return condition; }

    const std::vector<std::wstring> paths{ L"D:\\资料\\甲.txt", L"C:\\日本語\U0001f680\\乙.bin", L"D:\\资料\\甲.txt" };
    UINT Preferred() { return RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT); }
    UINT Performed() { return RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT); }
    UINT Logical() { return RegisterClipboardFormatW(CFSTR_LOGICALPERFORMEDDROPEFFECT); }
    UINT Pasted() { return RegisterClipboardFormatW(CFSTR_PASTESUCCEEDED); }

    std::vector<std::byte> Effect(const DWORD value)
    {
        std::vector<std::byte> bytes(sizeof(value));
        std::memcpy(bytes.data(), &value, sizeof(value));
        return bytes;
    }

    struct MediumCounts { unsigned produced{}, released{}; };
    class MediumOwner final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IUnknown>
    {
    public:
        MediumOwner(std::shared_ptr<MediumCounts> counts, HGLOBAL memory) : counts_(std::move(counts)), memory_(memory) {}
        ~MediumOwner() { if (memory_) { GlobalFree(memory_); } ++counts_->released; }
    private:
        std::shared_ptr<MediumCounts> counts_;
        HGLOBAL memory_{};
    };
    enum class MediumKind { Global, Stream, None, NullGlobal };
    struct Notification
    {
        UINT format{};
        DWORD effect{};
        BOOL release{};
        bool valid{};
        HGLOBAL handle{};
    };

    // In-process WRL object only. There is no OleGet/SetClipboard, disk access,
    // file creation or source deletion anywhere in this test group.
    class ClipboardObject final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDataObject>
    {
    public:
        std::vector<std::byte> drop = EncodeClipboardFilePaths(paths), preferred;
        HRESULT drop_result{ S_OK }, preferred_query{ DV_E_FORMATETC }, preferred_result{ DV_E_FORMATETC };
        HRESULT performed_result{ S_OK }, logical_result{ S_OK }, pasted_result{ S_OK };
        MediumKind drop_kind{ MediumKind::Global }, preferred_kind{ MediumKind::Global };
        bool medium_on_failure{}, requests_valid{ true };
        unsigned get_calls{}, query_calls{};
        std::vector<Notification> notifications;
        std::vector<STGMEDIUM> accepted;
        std::shared_ptr<MediumCounts> media = std::make_shared<MediumCounts>();
        std::function<void(unsigned)> after_set;

        ~ClipboardObject() { ReleaseAccepted(); }
        void SetPreferred(const DWORD value)
        { preferred = Effect(value); preferred_query = preferred_result = S_OK; }
        void ReleaseAccepted()
        {
            for (auto& medium : accepted) { ReleaseStgMedium(&medium); }
            accepted.clear();
        }
        static HRESULT Validate(const FORMATETC* format)
        {
            if (!format) { return E_POINTER; }
            if (format->ptd) { return DV_E_DVTARGETDEVICE; }
            if (format->dwAspect != DVASPECT_CONTENT) { return DV_E_DVASPECT; }
            if (format->lindex != -1) { return DV_E_LINDEX; }
            return format->tymed == TYMED_HGLOBAL ? S_OK : DV_E_TYMED;
        }
        HRESULT Render(const std::vector<std::byte>& bytes, const MediumKind kind, STGMEDIUM* output)
        {
            if (kind == MediumKind::None) { return S_OK; }
            if (kind == MediumKind::NullGlobal) { output->tymed = TYMED_HGLOBAL; return S_OK; }
            HGLOBAL memory{};
            if (kind == MediumKind::Stream)
            {
                const auto error = CreateStreamOnHGlobal(nullptr, TRUE, &output->pstm);
                if (FAILED(error)) { return error; }
                output->tymed = TYMED_ISTREAM;
            }
            else
            {
                memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes.size());
                if (!memory) { return E_OUTOFMEMORY; }
                output->tymed = TYMED_HGLOBAL; output->hGlobal = memory;
                if (!bytes.empty())
                {
                    auto* destination = GlobalLock(memory);
                    if (!destination) { ReleaseStgMedium(output); *output = {}; return E_OUTOFMEMORY; }
                    std::memcpy(destination, bytes.data(), bytes.size()); GlobalUnlock(memory);
                }
            }
            auto owner = Make<MediumOwner>(media, memory);
            if (!owner) { ReleaseStgMedium(output); *output = {}; return E_OUTOFMEMORY; }
            ++media->produced; output->pUnkForRelease = owner.Detach();
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* output) override
        {
            if (!output) { return E_POINTER; } *output = {};
            const auto valid = Validate(format); requests_valid &= valid == S_OK;
            if (FAILED(valid)) { return valid; }
            ++get_calls;
            const bool is_drop = format->cfFormat == CF_HDROP;
            if (!is_drop && format->cfFormat != Preferred()) { return DV_E_FORMATETC; }
            const auto error = is_drop ? drop_result : preferred_result;
            if (error != S_OK && !medium_on_failure) { return error; }
            const auto rendered = Render(is_drop ? drop : preferred, is_drop ? drop_kind : preferred_kind, output);
            return FAILED(rendered) ? rendered : error;
        }
        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override
        {
            const auto valid = Validate(format); requests_valid &= valid == S_OK;
            if (FAILED(valid)) { return valid; }
            ++query_calls;
            return format->cfFormat == Preferred() ? preferred_query : DV_E_FORMATETC;
        }
        HRESULT STDMETHODCALLTYPE SetData(FORMATETC* format, STGMEDIUM* medium, BOOL release) override
        {
            try
            {
                const auto validation = Validate(format);
                if (FAILED(validation) || !medium || medium->tymed != TYMED_HGLOBAL || !medium->hGlobal || GlobalSize(medium->hGlobal) < sizeof(DWORD)) { return E_INVALIDARG; }
                DWORD effect{};
                const auto* bytes = GlobalLock(medium->hGlobal);
                if (!bytes) { return E_OUTOFMEMORY; }
                std::memcpy(&effect, bytes, sizeof(effect)); GlobalUnlock(medium->hGlobal);
                notifications.push_back({ format->cfFormat, effect, release, medium->pUnkForRelease == nullptr, medium->hGlobal });
                const auto result = format->cfFormat == Performed() ? performed_result :
                    format->cfFormat == Logical() ? logical_result : format->cfFormat == Pasted() ? pasted_result : DV_E_FORMATETC;
                if (after_set) { after_set(static_cast<unsigned>(notifications.size())); }
                if (SUCCEEDED(result) && release) { accepted.push_back(*medium); }
                return result;
            }
            catch (...) { return E_OUTOFMEMORY; }
        }
        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* output) override
        { if (output) { output->ptd = nullptr; } return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC** output) override
        { if (output) { *output = nullptr; } return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** output) override
        { if (output) { *output = nullptr; } return OLE_E_ADVISENOTSUPPORTED; }
    };

    bool EmptyFailure(const ClipboardDataObjectReadResult& result, const HRESULT error)
    { return result.result == error && result.payload.paths.empty() && result.payload.error != ERROR_SUCCESS; }
    bool Ordered(const ClipboardObject& object, const DWORD performed, const DWORD logical, const DWORD pasted)
    {
        return object.notifications.size() == 3 &&
            object.notifications[0].format == Performed() && object.notifications[0].effect == performed &&
            object.notifications[1].format == Logical() && object.notifications[1].effect == logical &&
            object.notifications[2].format == Pasted() && object.notifications[2].effect == pasted;
    }
    bool RejectedAll(const ClipboardObject& object, const ClipboardPasteFeedback& feedback)
    { return object.notifications.empty() && !feedback.attempted && !feedback.paste_succeeded_attempted && feedback.result == S_FALSE; }

    bool RunReadTests()
    {
        bool passed = true;
        passed &= Check(EmptyFailure(ReadClipboardDataObject(nullptr), E_POINTER),
            "clipboard_data_object_null_input_is_pointer_error_without_payload");
        {
            auto object = Make<ClipboardObject>();
            const auto result = ReadClipboardDataObject(object.Get());
            passed &= Check(result.result == S_OK && result.payload.error == ERROR_SUCCESS && result.payload.paths == paths && result.payload.intent == ClipboardTransferIntent::Copy,
                "clipboard_data_object_missing_preference_defaults_copy_preserving_unicode_order_and_duplicates");
            passed &= Check(result.payload.sequence == 0 && object->requests_valid && object->get_calls == 1 && object->query_calls == 1,
                "clipboard_data_object_uses_content_hglobal_request_and_leaves_sequence_to_session");
            passed &= Check(object->media->produced == 1 && object->media->released == 1,
                "clipboard_data_object_success_releases_hdrop_through_provider_owner");
        }
        {
            auto object = Make<ClipboardObject>(); object->SetPreferred(DROPEFFECT_MOVE);
            const auto result = ReadClipboardDataObject(object.Get());
            passed &= Check(result.result == S_OK && result.payload.intent == ClipboardTransferIntent::Move && result.payload.paths == paths,
                "clipboard_data_object_move_preference_is_read_from_original_object");
            passed &= Check(object->media->produced == 2 && object->media->released == 2 && object->get_calls == 2,
                "clipboard_data_object_hdrop_and_preference_media_are_released_once");
        }
        {
            auto object = Make<ClipboardObject>(); object->SetPreferred(DROPEFFECT_COPY);
            const auto result = ReadClipboardDataObject(object.Get());
            passed &= Check(result.result == S_OK && result.payload.intent == ClipboardTransferIntent::Copy && result.payload.paths == paths,
                "clipboard_data_object_explicit_copy_preference_is_accepted");
        }
        {
            auto object = Make<ClipboardObject>(); object->drop_result = E_ACCESSDENIED;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), E_ACCESSDENIED) && object->query_calls == 0,
                "clipboard_data_object_hdrop_access_denied_remains_original_hresult");
        }
        {
            auto object = Make<ClipboardObject>(); object->drop_result = RPC_E_DISCONNECTED;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), RPC_E_DISCONNECTED),
                "clipboard_data_object_hdrop_rpc_disconnect_is_not_format_absence");
        }
        {
            auto object = Make<ClipboardObject>(); object->drop_result = S_FALSE;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), S_FALSE),
                "clipboard_data_object_nonzero_success_code_does_not_claim_usable_paths");
        }
        {
            auto object = Make<ClipboardObject>(); object->drop_kind = MediumKind::Stream;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), DV_E_TYMED) && object->media->released == 1,
                "clipboard_data_object_wrong_hdrop_medium_is_rejected_and_released");
        }
        {
            auto object = Make<ClipboardObject>(); object->drop_kind = MediumKind::None;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), DV_E_TYMED),
                "clipboard_data_object_missing_hdrop_medium_is_not_an_empty_success");
        }
        {
            auto object = Make<ClipboardObject>(); object->drop_kind = MediumKind::NullGlobal;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), DV_E_TYMED),
                "clipboard_data_object_null_hglobal_is_rejected");
        }
        {
            auto object = Make<ClipboardObject>(); object->drop.clear();
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), HRESULT_FROM_WIN32(ERROR_INVALID_DATA)),
                "clipboard_data_object_zero_length_hdrop_is_invalid_data");
        }
        {
            auto object = Make<ClipboardObject>(); DROPFILES header{}; std::memcpy(&header, object->drop.data(), sizeof(header));
            header.pFiles = 0; std::memcpy(object->drop.data(), &header, sizeof(header));
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), HRESULT_FROM_WIN32(ERROR_INVALID_DATA)) && object->media->released == 1,
                "clipboard_data_object_malformed_hdrop_uses_strict_existing_codec");
        }
        {
            auto object = Make<ClipboardObject>(); DROPFILES header{}; std::memcpy(&header, object->drop.data(), sizeof(header));
            header.fWide = FALSE; std::memcpy(object->drop.data(), &header, sizeof(header));
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)),
                "clipboard_data_object_ansi_hdrop_does_not_deliver_partial_paths");
        }
        {
            auto object = Make<ClipboardObject>(); object->drop.resize(MaxClipboardFilePayloadBytes + 1);
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)) && object->media->released == 1,
                "clipboard_data_object_hdrop_payload_budget_is_bounded");
        }
        {
            auto object = Make<ClipboardObject>(); object->preferred_query = E_ACCESSDENIED;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), E_ACCESSDENIED) && object->get_calls == 1,
                "clipboard_data_object_preference_query_access_error_is_not_default_copy");
        }
        {
            auto object = Make<ClipboardObject>(); object->preferred_query = RPC_E_SERVER_DIED;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), RPC_E_SERVER_DIED),
                "clipboard_data_object_preference_query_rpc_error_is_preserved");
        }
        {
            auto object = Make<ClipboardObject>(); object->SetPreferred(DROPEFFECT_MOVE); object->preferred_query = E_NOTIMPL;
            const auto result = ReadClipboardDataObject(object.Get());
            passed &= Check(result.result == S_OK && result.payload.intent == ClipboardTransferIntent::Move,
                "clipboard_data_object_unimplemented_query_can_still_render_preference");
        }
        {
            auto object = Make<ClipboardObject>(); object->preferred_query = E_NOTIMPL;
            const auto result = ReadClipboardDataObject(object.Get());
            passed &= Check(result.result == S_OK && result.payload.intent == ClipboardTransferIntent::Copy && object->get_calls == 2,
                "clipboard_data_object_unimplemented_query_with_missing_format_defaults_copy");
        }
        {
            auto object = Make<ClipboardObject>(); object->SetPreferred(DROPEFFECT_MOVE); object->preferred_result = E_ACCESSDENIED;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), E_ACCESSDENIED),
                "clipboard_data_object_advertised_preference_render_error_clears_paths");
        }
        {
            auto object = Make<ClipboardObject>(); object->SetPreferred(DROPEFFECT_MOVE); object->preferred_result = DV_E_FORMATETC;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), DV_E_FORMATETC),
                "clipboard_data_object_advertised_then_missing_preference_is_not_silently_copy");
        }
        {
            auto object = Make<ClipboardObject>(); object->SetPreferred(DROPEFFECT_MOVE); object->preferred_kind = MediumKind::Stream;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), DV_E_TYMED) && object->media->released == 2,
                "clipboard_data_object_bad_preference_medium_releases_both_provider_media");
        }
        {
            auto object = Make<ClipboardObject>(); object->SetPreferred(DROPEFFECT_COPY); object->preferred.clear();
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), HRESULT_FROM_WIN32(ERROR_INVALID_DATA)),
                "clipboard_data_object_empty_preference_bytes_are_not_a_default");
        }
        {
            auto object = Make<ClipboardObject>(); object->SetPreferred(DROPEFFECT_LINK);
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)),
                "clipboard_data_object_link_preference_is_explicitly_unsupported");
        }
        {
            auto object = Make<ClipboardObject>(); object->SetPreferred(DROPEFFECT_COPY | DROPEFFECT_MOVE);
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)),
                "clipboard_data_object_ambiguous_effect_bits_are_not_treated_as_move");
        }
        {
            auto object = Make<ClipboardObject>(); object->drop_result = E_ACCESSDENIED; object->medium_on_failure = true;
            passed &= Check(EmptyFailure(ReadClipboardDataObject(object.Get()), E_ACCESSDENIED) && object->media->produced == 1 && object->media->released == 1,
                "clipboard_data_object_error_return_does_not_leak_an_attached_medium");
        }
        return passed;
    }

    bool RunCompletionTests()
    {
        bool passed = true;
        {
            auto object = Make<ClipboardObject>();
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Copy, true, S_OK);
            passed &= Check(Ordered(*object.Get(), DROPEFFECT_COPY, DROPEFFECT_COPY, DROPEFFECT_COPY) && feedback.result == S_OK && feedback.performed == S_OK && feedback.logical == S_OK && feedback.paste_succeeded == S_OK,
                "clipboard_data_object_completed_copy_reports_three_effects_in_order");
            passed &= Check(feedback.attempted && feedback.paste_succeeded_attempted && feedback.fully_completed && !feedback.warning && !feedback.unsupported,
                "clipboard_data_object_feedback_result_is_distinct_from_filesystem_commit_fact");
            passed &= Check(object->accepted.size() == 3 && object->notifications[0].release && object->notifications[1].release && object->notifications[2].release && object->notifications[0].valid && object->notifications[1].valid && object->notifications[2].valid,
                "clipboard_data_object_setdata_uses_caller_allocated_hglobal_with_release_transfer");
            passed &= Check(GlobalFlags(object->accepted[0].hGlobal) != GMEM_INVALID_HANDLE && GlobalFlags(object->accepted[1].hGlobal) != GMEM_INVALID_HANDLE && GlobalFlags(object->accepted[2].hGlobal) != GMEM_INVALID_HANDLE,
                "clipboard_data_object_successful_setdata_media_remain_owned_by_receiver");
            const auto retained = object->accepted.back().hGlobal; object->ReleaseAccepted();
            passed &= Check(GlobalFlags(retained) == GMEM_INVALID_HANDLE,
                "clipboard_data_object_receiver_releases_transferred_medium_once");
        }
        {
            auto object = Make<ClipboardObject>();
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, true, S_OK);
            passed &= Check(feedback.result == S_OK && Ordered(*object.Get(), DROPEFFECT_NONE, DROPEFFECT_MOVE, DROPEFFECT_MOVE),
                "clipboard_data_object_optimized_move_never_requests_source_side_deletion");
        }
        {
            auto object = Make<ClipboardObject>();
            passed &= Check(RejectedAll(*object.Get(), CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, false, S_OK)),
                "clipboard_data_object_partial_move_sends_no_paste_success");
        }
        {
            auto object = Make<ClipboardObject>();
            passed &= Check(RejectedAll(*object.Get(), CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Copy, false, S_OK)),
                "clipboard_data_object_skipped_copy_does_not_claim_full_completion");
        }
        {
            auto object = Make<ClipboardObject>(); const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, false, E_ABORT);
            passed &= Check(RejectedAll(*object.Get(), feedback) && feedback.operation_result == E_ABORT && !feedback.warning,
                "clipboard_data_object_cancelled_operation_preserves_result_without_feedback_warning");
        }
        {
            auto object = Make<ClipboardObject>(); const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, false, E_ACCESSDENIED);
            passed &= Check(RejectedAll(*object.Get(), feedback) && feedback.operation_result == E_ACCESSDENIED,
                "clipboard_data_object_failed_operation_does_not_send_success_formats");
        }
        {
            auto object = Make<ClipboardObject>();
            passed &= Check(RejectedAll(*object.Get(), CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, false, S_FALSE)),
                "clipboard_data_object_unchanged_noop_does_not_report_a_move");
        }
        {
            auto object = Make<ClipboardObject>(); const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, true, E_ABORT);
            passed &= Check(feedback.result == S_OK && feedback.operation_result == E_ABORT && Ordered(*object.Get(), DROPEFFECT_NONE, DROPEFFECT_MOVE, DROPEFFECT_MOVE),
                "clipboard_data_object_late_cancel_does_not_undo_verified_complete_commits");
        }
        {
            auto object = Make<ClipboardObject>(); const auto warning = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Copy, true, warning);
            passed &= Check(feedback.result == S_OK && feedback.operation_result == warning && feedback.fully_completed && !feedback.warning,
                "clipboard_data_object_postcommit_warning_is_not_reclassified_as_file_failure");
        }
        {
            const auto feedback = CompleteClipboardPaste(nullptr, ClipboardTransferIntent::Copy, true, S_OK);
            passed &= Check(feedback.result == E_POINTER && feedback.warning && !feedback.attempted,
                "clipboard_data_object_null_feedback_target_is_a_real_notification_error");
        }
        {
            auto object = Make<ClipboardObject>(); const auto feedback = CompleteClipboardPaste(object.Get(), static_cast<ClipboardTransferIntent>(17), true, S_OK);
            passed &= Check(feedback.result == E_INVALIDARG && feedback.warning && object->notifications.empty(),
                "clipboard_data_object_invalid_intent_does_not_emit_effects");
        }
        {
            auto object = Make<ClipboardObject>(); object->performed_result = object->logical_result = object->pasted_result = E_NOTIMPL;
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Copy, true, S_OK);
            passed &= Check(feedback.result == S_FALSE && feedback.unsupported && feedback.warning && feedback.performed == E_NOTIMPL && feedback.logical == E_NOTIMPL && feedback.paste_succeeded == E_NOTIMPL && object->notifications.size() == 3,
                "clipboard_data_object_optional_feedback_support_is_not_rpc_or_file_failure");
            passed &= Check(GlobalFlags(object->notifications.back().handle) == GMEM_INVALID_HANDLE && object->accepted.empty(),
                "clipboard_data_object_failed_setdata_keeps_and_releases_caller_owned_medium");
        }
        {
            auto object = Make<ClipboardObject>(); object->performed_result = DV_E_FORMATETC;
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, true, S_OK);
            passed &= Check(feedback.result == S_FALSE && feedback.unsupported && !feedback.paste_succeeded_attempted && object->notifications.size() == 2,
                "clipboard_data_object_move_without_acknowledged_none_never_sends_paste_succeeded");
        }
        {
            auto object = Make<ClipboardObject>(); object->logical_result = E_NOTIMPL;
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, true, S_OK);
            passed &= Check(feedback.result == S_FALSE && feedback.unsupported && feedback.paste_succeeded_attempted && object->notifications.size() == 3,
                "clipboard_data_object_optional_logical_format_does_not_hide_accepted_optimized_move");
        }
        {
            auto object = Make<ClipboardObject>(); object->performed_result = E_ACCESSDENIED; object->logical_result = E_NOTIMPL;
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, true, S_OK);
            passed &= Check(feedback.result == E_ACCESSDENIED && feedback.performed == E_ACCESSDENIED && feedback.logical == E_NOTIMPL && feedback.unsupported && feedback.warning && !feedback.paste_succeeded_attempted && feedback.operation_result == S_OK,
                "clipboard_data_object_access_failure_and_optional_support_are_recorded_separately");
        }
        {
            auto object = Make<ClipboardObject>(); object->pasted_result = RPC_E_DISCONNECTED;
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Copy, true, S_OK);
            passed &= Check(feedback.result == RPC_E_DISCONNECTED && feedback.warning && !feedback.unsupported && feedback.paste_succeeded == RPC_E_DISCONNECTED && feedback.operation_result == S_OK,
                "clipboard_data_object_completion_rpc_failure_is_warning_after_committed_copy");
        }
        {
            auto object = Make<ClipboardObject>(); object->logical_result = E_ACCESSDENIED; object->pasted_result = RPC_E_DISCONNECTED;
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Copy, true, S_OK);
            passed &= Check(feedback.result == E_ACCESSDENIED && feedback.logical == E_ACCESSDENIED && feedback.paste_succeeded == RPC_E_DISCONNECTED,
                "clipboard_data_object_first_real_notification_failure_keeps_later_diagnostics");
        }
        {
            auto object = Make<ClipboardObject>(); object->performed_result = S_FALSE;
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, true, S_OK);
            passed &= Check(feedback.result == S_FALSE && feedback.warning && !feedback.unsupported && !feedback.paste_succeeded_attempted,
                "clipboard_data_object_unconfirmed_performed_none_is_not_an_acknowledgement");
        }
        {
            auto original = Make<ClipboardObject>(); auto replacement = Make<ClipboardObject>();
            const auto read = ReadClipboardDataObject(original.Get());
            const auto feedback = CompleteClipboardPaste(original.Get(), read.payload.intent, true, S_OK);
            passed &= Check(feedback.result == S_OK && original->notifications.size() == 3 && replacement->get_calls == 0 && replacement->notifications.empty(),
                "clipboard_data_object_feedback_uses_only_the_supplied_original_object");
        }
        {
            auto object = Make<ClipboardObject>();
            const auto first = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Copy, true, S_OK);
            const auto second = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Copy, true, S_OK);
            passed &= Check(first.result == S_OK && second.result == S_OK && object->notifications.size() == 6,
                "clipboard_data_object_codec_is_stateless_completion_once_belongs_to_session");
        }
        return passed;
    }

    bool RunCurrentObjectTests()
    {
        bool passed = true;
        {
            auto object = Make<ClipboardObject>(); unsigned checks{};
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, true, S_OK, [&] { ++checks; return true; });
            passed &= Check(feedback.result == S_OK && checks == 3 && object->notifications.size() == 3,
                "clipboard_data_object_current_predicate_runs_once_before_each_setdata");
        }
        {
            auto object = Make<ClipboardObject>();
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, true, S_OK, [] { return false; });
            passed &= Check(feedback.result == S_FALSE && feedback.clipboard_changed && !feedback.attempted && !feedback.warning && !feedback.unsupported && object->notifications.empty(),
                "clipboard_data_object_replaced_before_feedback_sends_no_formats");
        }
        {
            auto object = Make<ClipboardObject>(); bool current = true; unsigned checks{};
            object->after_set = [&](unsigned count) { if (count == 1) { current = false; } };
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, true, S_OK, [&] { ++checks; return current; });
            passed &= Check(feedback.result == S_FALSE && feedback.clipboard_changed && feedback.performed == S_OK && object->notifications.size() == 1 && checks == 2 && !feedback.paste_succeeded_attempted && !feedback.warning,
                "clipboard_data_object_change_during_performed_stops_logical_and_paste");
        }
        {
            auto object = Make<ClipboardObject>(); bool current = true;
            object->after_set = [&](unsigned count) { if (count == 2) { current = false; } };
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Copy, true, S_OK, [&] { return current; });
            passed &= Check(feedback.result == S_FALSE && feedback.clipboard_changed && object->notifications.size() == 2 && !feedback.paste_succeeded_attempted && !feedback.warning && !feedback.unsupported,
                "clipboard_data_object_change_during_logical_prevents_paste_succeeded");
        }
        {
            auto object = Make<ClipboardObject>();
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Copy, true, S_OK, []() -> bool { throw std::runtime_error("owned predicate failure"); });
            passed &= Check(feedback.result == E_FAIL && feedback.warning && !feedback.clipboard_changed && !feedback.attempted && object->notifications.empty(),
                "clipboard_data_object_predicate_exception_is_real_warning_without_setdata");
        }
        {
            auto object = Make<ClipboardObject>(); unsigned checks{};
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, true, S_OK, [&]() -> bool { if (++checks == 2) { throw std::runtime_error("owned predicate failure"); } return true; });
            passed &= Check(feedback.result == E_FAIL && feedback.warning && feedback.performed == S_OK && object->notifications.size() == 1 && !feedback.paste_succeeded_attempted,
                "clipboard_data_object_mid_feedback_exception_keeps_prior_hresult_and_stops");
        }
        {
            auto object = Make<ClipboardObject>(); unsigned checks{};
            const auto feedback = CompleteClipboardPaste(object.Get(), ClipboardTransferIntent::Move, false, E_ABORT, [&] { ++checks; return false; });
            passed &= Check(feedback.result == S_FALSE && !feedback.clipboard_changed && checks == 0 && object->notifications.empty(),
                "clipboard_data_object_noop_completion_does_not_call_current_predicate");
        }
        return passed;
    }
}

bool RunClipboardDataObjectTests()
{
    bool passed = RunReadTests();
    passed &= RunCompletionTests();
    passed &= RunCurrentObjectTests();
    return passed;
}
