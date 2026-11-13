#include "Blink/App/ClipboardDataObject.h"

#include <shlobj.h>
#include <cstring>
#include <new>
#include <utility>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "User32.lib")

namespace blink::app
{
    namespace
    {
        struct Medium
        {
            STGMEDIUM value{};
            ~Medium() { if (value.tymed != TYMED_NULL) { ReleaseStgMedium(&value); } }
        };
        struct Unlock
        {
            HGLOBAL memory;
            ~Unlock() { GlobalUnlock(memory); }
        };
        HRESULT RegistrationError() noexcept
        {
            const auto error = GetLastError();
            return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
        }
        bool MissingFormat(const HRESULT error) noexcept
        { return error == DV_E_FORMATETC || error == DATA_E_FORMATETC; }
        bool OptionalFeedback(const HRESULT error) noexcept
        { return MissingFormat(error) || error == E_NOTIMPL; }
        ClipboardDataObjectReadResult ReadFailure(const HRESULT error,
            const DWORD payload_error = ERROR_GEN_FAILURE) noexcept
        {
            ClipboardDataObjectReadResult result;
            result.result = error;
            result.payload.error = HRESULT_FACILITY(error) == FACILITY_WIN32 ? HRESULT_CODE(error) : payload_error;
            return result;
        }
        HRESULT Notify(IDataObject* object, const wchar_t* name, const DWORD effect,
            const std::function<bool()>& still_current, bool& clipboard_changed, bool& attempted)
        {
            const auto id = RegisterClipboardFormatW(name);
            if (!id) { return RegistrationError(); }
            Medium medium;
            medium.value.hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(effect));
            if (!medium.value.hGlobal) { return E_OUTOFMEMORY; }
            medium.value.tymed = TYMED_HGLOBAL;
            auto* bytes = GlobalLock(medium.value.hGlobal);
            if (!bytes) { return E_OUTOFMEMORY; }
            std::memcpy(bytes, &effect, sizeof(effect));
            GlobalUnlock(medium.value.hGlobal);
            FORMATETC format{ static_cast<CLIPFORMAT>(id), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            if (still_current && !still_current()) { clipboard_changed = true; return S_FALSE; }
            attempted = true;
            const auto error = object->SetData(&format, &medium.value, TRUE);
            // SetData accepts ownership only on success. A failed call leaves
            // the medium with us; its scope releases it exactly once.
            if (SUCCEEDED(error)) { medium.value = {}; }
            return error;
        }
        void Summarize(ClipboardPasteFeedback& feedback) noexcept
        {
            HRESULT first_failure = S_OK;
            bool unconfirmed{};
            const HRESULT results[]{ feedback.performed, feedback.logical, feedback.paste_succeeded };
            const auto count = feedback.paste_succeeded_attempted ? 3u : 2u;
            for (unsigned index = 0; index < count; ++index)
            {
                const auto error = results[index];
                if (OptionalFeedback(error)) { feedback.unsupported = true; continue; }
                if (FAILED(error) && first_failure == S_OK) { first_failure = error; }
                else if (error != S_OK) { unconfirmed = true; }
            }
            feedback.result = FAILED(first_failure) ? first_failure :
                feedback.unsupported || unconfirmed || !feedback.paste_succeeded_attempted ? S_FALSE : S_OK;
            feedback.warning = feedback.result != S_OK;
        }
    }

    ClipboardDataObjectReadResult ReadClipboardDataObject(IDataObject* object) noexcept
    {
        try
        {
            if (!object) { return ReadFailure(E_POINTER, ERROR_INVALID_PARAMETER); }
            FORMATETC format{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            Medium drop;
            auto error = object->GetData(&format, &drop.value);
            if (error != S_OK) { return ReadFailure(error); }
            if (drop.value.tymed != TYMED_HGLOBAL || !drop.value.hGlobal)
            { return ReadFailure(DV_E_TYMED, ERROR_INVALID_DATA); }
            const auto size = GlobalSize(drop.value.hGlobal);
            if (size > MaxClipboardFilePayloadBytes)
            { return ReadFailure(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)); }
            if (!size) { return ReadFailure(HRESULT_FROM_WIN32(ERROR_INVALID_DATA)); }
            const auto* bytes = static_cast<const std::byte*>(GlobalLock(drop.value.hGlobal));
            if (!bytes) { return ReadFailure(E_OUTOFMEMORY, ERROR_NOT_ENOUGH_MEMORY); }
            ClipboardFilePaths payload;
            {
                const Unlock unlock{ drop.value.hGlobal };
                payload = DecodeClipboardFilePaths({ bytes, size });
            }
            if (payload.error) { return ReadFailure(HRESULT_FROM_WIN32(payload.error)); }

            const auto preferred = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
            if (!preferred) { return ReadFailure(RegistrationError()); }
            format.cfFormat = static_cast<CLIPFORMAT>(preferred);
            const auto queried = object->QueryGetData(&format);
            if (!MissingFormat(queried))
            {
                // QueryGetData is optional. An unimplemented query can still
                // render via GetData; RPC, access and advertised-format errors
                // are not silently converted into a default COPY preference.
                if (queried != S_OK && queried != E_NOTIMPL) { return ReadFailure(queried); }
                Medium effect;
                error = object->GetData(&format, &effect.value);
                if (queried == E_NOTIMPL && MissingFormat(error)) { error = S_OK; }
                else
                {
                    if (error != S_OK) { return ReadFailure(error); }
                    if (effect.value.tymed != TYMED_HGLOBAL || !effect.value.hGlobal)
                    { return ReadFailure(DV_E_TYMED, ERROR_INVALID_DATA); }
                    const auto effect_size = GlobalSize(effect.value.hGlobal);
                    if (effect_size < sizeof(DWORD) || effect_size > MaxClipboardFilePayloadBytes)
                    { return ReadFailure(HRESULT_FROM_WIN32(ERROR_INVALID_DATA)); }
                    const auto* value = static_cast<const std::byte*>(GlobalLock(effect.value.hGlobal));
                    if (!value) { return ReadFailure(E_OUTOFMEMORY, ERROR_NOT_ENOUGH_MEMORY); }
                    const Unlock unlock{ effect.value.hGlobal };
                    const auto decoded = DecodeClipboardDropEffect({ value, effect_size });
                    if (decoded.error) { return ReadFailure(HRESULT_FROM_WIN32(decoded.error)); }
                    payload.intent = decoded.intent;
                }
            }
            return { S_OK, std::move(payload) };
        }
        catch (const std::bad_alloc&) { return ReadFailure(E_OUTOFMEMORY, ERROR_NOT_ENOUGH_MEMORY); }
        catch (...) { return ReadFailure(E_FAIL); }
    }

    ClipboardPasteFeedback CompleteClipboardPaste(IDataObject* object,
        const ClipboardTransferIntent intent, const bool fully_completed, const HRESULT operation_result,
        const std::function<bool()>& still_current) noexcept
    {
        ClipboardPasteFeedback feedback;
        feedback.operation_result = operation_result;
        feedback.fully_completed = fully_completed;
        if (!object) { feedback.result = E_POINTER; feedback.warning = true; return feedback; }
        if (intent != ClipboardTransferIntent::Copy && intent != ClipboardTransferIntent::Move)
        { feedback.result = E_INVALIDARG; feedback.warning = true; return feedback; }
        if (!feedback.fully_completed) { return feedback; }

        // Microsoft Shell delete-on-paste/optimized-move protocol:
        // https://learn.microsoft.com/windows/win32/shell/datascenarios
        // https://learn.microsoft.com/windows/win32/shell/clipboard
        // Performed is recorded before PasteSucceeded can trigger source work.
        try
        {
            const auto logical = ClipboardDropEffectValue(intent);
            feedback.performed = Notify(object, CFSTR_PERFORMEDDROPEFFECT,
                intent == ClipboardTransferIntent::Move ? DROPEFFECT_NONE : logical,
                still_current, feedback.clipboard_changed, feedback.attempted);
            if (feedback.clipboard_changed) { return feedback; }
            feedback.logical = Notify(object, CFSTR_LOGICALPERFORMEDDROPEFFECT, logical,
                still_current, feedback.clipboard_changed, feedback.attempted);
            if (feedback.clipboard_changed) { return feedback; }
            if (intent != ClipboardTransferIntent::Move || feedback.performed == S_OK)
            {
                feedback.paste_succeeded = Notify(object, CFSTR_PASTESUCCEEDED, logical,
                    still_current, feedback.clipboard_changed, feedback.paste_succeeded_attempted);
            }
            if (feedback.clipboard_changed) { return feedback; }
            Summarize(feedback);
        }
        catch (const std::bad_alloc&) { feedback.result = E_OUTOFMEMORY; feedback.warning = true; }
        catch (...) { feedback.result = E_FAIL; feedback.warning = true; }
        return feedback;
    }
}
