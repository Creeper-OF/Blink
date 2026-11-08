#include "Blink/App/FileDragData.h"
#include <roapi.h>
#include <shlobj.h>
#include <wrl.h>
#include <cstring>
#include <optional>
#include <algorithm>
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "RuntimeObject.lib")
#pragma comment(lib, "Uuid.lib")

namespace blink::app
{
    namespace
    {
        UINT TokenFormat() { return RegisterClipboardFormatW(L"Blink.Files.Drag.Source.v1"); }
        UINT PerformedFormat() { return RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT); }
        UINT LogicalFormat() { return RegisterClipboardFormatW(CFSTR_LOGICALPERFORMEDDROPEFFECT); }
        UINT PasteFormat() { return RegisterClipboardFormatW(CFSTR_PASTESUCCEEDED); }
        UINT LoopFormat() { return RegisterClipboardFormatW(CFSTR_INDRAGLOOP); }
        HRESULT Validate(const FORMATETC* format)
        {
            if (!format) { return E_POINTER; }
            if (format->dwAspect != DVASPECT_CONTENT) { return DV_E_DVASPECT; }
            if (format->lindex != -1) { return DV_E_LINDEX; }
            if (!(format->tymed & TYMED_HGLOBAL)) { return DV_E_TYMED; }
            return S_OK;
        }
        HRESULT Medium(const void* bytes, const std::size_t size, STGMEDIUM* output)
        {
            *output = {};
            const auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size); if (!memory) { return E_OUTOFMEMORY; }
            auto* destination = GlobalLock(memory);
            if (!destination) { GlobalFree(memory); return E_OUTOFMEMORY; }
            memcpy(destination, bytes, size); GlobalUnlock(memory);
            output->tymed = TYMED_HGLOBAL; output->hGlobal = memory; return S_OK;
        }
        HRESULT Notify(IDataObject* object, const UINT format, const DWORD effect)
        {
            STGMEDIUM medium{}; auto error = Medium(&effect, sizeof(effect), &medium); if (FAILED(error)) { return error; }
            FORMATETC request{ static_cast<CLIPFORMAT>(format), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            error = object->SetData(&request, &medium, TRUE);
            if (FAILED(error)) { ReleaseStgMedium(&medium); } return error;
        }
        class DataObject final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
            IDataObject, IDataObjectAsyncCapability, Microsoft::WRL::FtmBase>
        {
        public:
            std::vector<std::byte> paths;
            std::wstring token;
            std::shared_ptr<FileDragFeedback> feedback;
            std::mutex mutex;
            std::optional<DWORD> performed, logical, pasted, loop;
            HRESULT STDMETHODCALLTYPE GetData(FORMATETC* request, STGMEDIUM* output) override
            {
                if (!output) { return E_POINTER; } *output = {};
                try
                {
                    if (const auto error = Validate(request); FAILED(error)) { return error; }
                    if (request->cfFormat == CF_HDROP) { return Medium(paths.data(), paths.size(), output); }
                    if (request->cfFormat == TokenFormat()) { return Medium(token.c_str(), (token.size() + 1) * sizeof(wchar_t), output); }
                    std::scoped_lock lock(mutex); const auto* value = Effect(request->cfFormat);
                    return value && *value ? Medium(&**value, sizeof(DWORD), output) : DV_E_FORMATETC;
                }
                catch (...) { return E_OUTOFMEMORY; }
            }
            std::optional<DWORD>* Effect(UINT format)
            {
                return format == PerformedFormat() ? &performed : format == LogicalFormat() ? &logical :
                    format == PasteFormat() ? &pasted : format == LoopFormat() ? &loop : nullptr;
            }
            HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return DATA_E_FORMATETC; }
            HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* request) override
            {
                if (const auto error = Validate(request); FAILED(error)) { return error; }
                if (request->cfFormat == CF_HDROP || request->cfFormat == TokenFormat()) { return S_OK; }
                std::scoped_lock lock(mutex); const auto* value = Effect(request->cfFormat); return value && *value ? S_OK : DV_E_FORMATETC;
            }
            HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* result) override
            { if (!result) { return E_POINTER; } result->ptd = nullptr; return DATA_S_SAMEFORMATETC; }
            HRESULT STDMETHODCALLTYPE SetData(FORMATETC* request, STGMEDIUM* medium, BOOL release) override
            {
                if (const auto error = Validate(request); FAILED(error)) { return error; }
                if (!medium || medium->tymed != TYMED_HGLOBAL || !medium->hGlobal || GlobalSize(medium->hGlobal) < sizeof(DWORD)) { return DV_E_TYMED; }
                auto* data = GlobalLock(medium->hGlobal); if (!data) { return E_OUTOFMEMORY; }
                DWORD value{}; memcpy(&value, data, sizeof(value)); GlobalUnlock(medium->hGlobal);
                {
                    std::scoped_lock lock(mutex); auto* effect = Effect(request->cfFormat); if (!effect) { return DV_E_FORMATETC; } *effect = value;
                }
                if (release) { ReleaseStgMedium(medium); }
                if (request->cfFormat == PerformedFormat()) { feedback->performed.store(value); }
                if (request->cfFormat == LogicalFormat())
                {
                    feedback->logical.store(value);
                    try { if (feedback->changed) { feedback->changed(value); } } catch (...) {}
                }
                return S_OK; // Publishing data and feedback never deletes files.
            }
            HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** output) override
            {
                if (!output) { return E_POINTER; } *output = nullptr;
                if (direction != DATADIR_GET) { return E_NOTIMPL; }
                std::vector<FORMATETC> formats;
                try
                {
                    const auto add = [&](UINT value) { formats.push_back({ static_cast<CLIPFORMAT>(value), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }); };
                    add(CF_HDROP); add(TokenFormat());
                    { std::scoped_lock lock(mutex); for (UINT format : { PerformedFormat(), LogicalFormat(), PasteFormat(), LoopFormat() })
                        { const auto* value = Effect(format); if (value && *value) { add(format); } } }
                    return SHCreateStdEnumFmtEtc(static_cast<UINT>(formats.size()), formats.data(), output);
                }
                catch (...) { return E_OUTOFMEMORY; }
            }
            HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
            HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
            HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
            HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL value) override { feedback->async_mode.store(value != FALSE); return S_OK; }
            HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL* value) override { if (!value) { return E_POINTER; } *value = feedback->async_mode.load(); return S_OK; }
            HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx*) override { feedback->in_operation.store(true); return S_OK; }
            HRESULT STDMETHODCALLTYPE InOperation(BOOL* value) override { if (!value) { return E_POINTER; } *value = feedback->in_operation.load(); return S_OK; }
            HRESULT STDMETHODCALLTYPE EndOperation(HRESULT error, IBindCtx*, DWORD effect) override
            { feedback->error.store(error); feedback->performed.store(effect); feedback->in_operation.store(false); return S_OK; }
        };
        struct ReleaseMedium { STGMEDIUM value{}; ~ReleaseMedium() { if (value.tymed) { ReleaseStgMedium(&value); } } };
    }
    HRESULT CreateFileDragData(std::span<const std::wstring> paths, std::wstring token, FileDragData& output) noexcept
    {
        output = {};
        try
        {
            auto payload = EncodeClipboardFilePaths(paths);
            if (payload.empty() || token.empty() || token.size() > 96 || token.find(L'\0') != std::wstring::npos) { return E_INVALIDARG; }
            auto object = Microsoft::WRL::Make<DataObject>(); if (!object) { return E_OUTOFMEMORY; }
            object->paths = std::move(payload); object->token = std::move(token); object->feedback = std::make_shared<FileDragFeedback>();
            output.feedback = object->feedback; output.object.attach(object.Detach()); return S_OK;
        }
        catch (...) { output = {}; return E_OUTOFMEMORY; }
    }
    FileDragPayload ReadFileDragData(IDataObject* object) noexcept
    {
        FileDragPayload result;
        try
        {
            if (!object) { result.error = E_POINTER; return result; }
            FORMATETC format{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }; ReleaseMedium medium;
            result.error = object->GetData(&format, &medium.value); if (FAILED(result.error)) { return result; }
            if (medium.value.tymed != TYMED_HGLOBAL || !medium.value.hGlobal) { result.error = DV_E_TYMED; return result; }
            const auto size = GlobalSize(medium.value.hGlobal);
            if (!size || size > MaxClipboardFilePayloadBytes) { result.error = E_INVALIDARG; return result; }
            const auto* bytes = static_cast<const std::byte*>(GlobalLock(medium.value.hGlobal));
            if (!bytes) { result.error = E_OUTOFMEMORY; return result; }
            struct Unlock { HGLOBAL memory; ~Unlock() { GlobalUnlock(memory); } } unlock{ medium.value.hGlobal };
            auto paths = DecodeClipboardFilePaths({ bytes, size }); result.error = HRESULT_FROM_WIN32(paths.error);
            if (FAILED(result.error)) { return result; }
            result.paths = std::move(paths.paths);
            format.cfFormat = static_cast<CLIPFORMAT>(TokenFormat()); ReleaseMedium token;
            if (SUCCEEDED(object->QueryGetData(&format)) && SUCCEEDED(object->GetData(&format, &token.value)))
            {
                const auto length = token.value.tymed == TYMED_HGLOBAL && token.value.hGlobal ? GlobalSize(token.value.hGlobal) : 0;
                if (!length || length > 128 * sizeof(wchar_t) || length % sizeof(wchar_t)) { result = {}; result.error = E_INVALIDARG; return result; }
                const auto* text = static_cast<const wchar_t*>(GlobalLock(token.value.hGlobal));
                if (!text) { result = {}; result.error = E_OUTOFMEMORY; return result; }
                struct UnlockToken { HGLOBAL memory; ~UnlockToken() { GlobalUnlock(memory); } } token_unlock{ token.value.hGlobal };
                const auto count = length / sizeof(wchar_t);
                const auto last = std::find(text, text + count, L'\0');
                if (last == text + count || last - text > 96) { result = {}; result.error = E_INVALIDARG; return result; }
                result.token.assign(text, last);
            }
        }
        catch (const std::bad_alloc&) { result = {}; result.error = E_OUTOFMEMORY; }
        catch (...) { result = {}; result.error = E_FAIL; }
        return result;
    }
    HRESULT QueryFileDragDataObject(IUnknown* value, winrt::com_ptr<IDataObject>& object) noexcept
    {
        object = nullptr; if (!value) { return E_POINTER; }
        winrt::com_ptr<IDataObjectProvider> provider;
        auto error = value->QueryInterface(IID_PPV_ARGS(provider.put()));
        if (SUCCEEDED(error)) { error = provider->GetDataObject(object.put()); }
        else { error = value->QueryInterface(IID_PPV_ARGS(object.put())); }
        return error;
    }
    HRESULT FileDragDataReference::Capture(IUnknown* value) noexcept
    {
        reference_ = nullptr; winrt::com_ptr<IDataObject> object;
        const auto error = QueryFileDragDataObject(value, object);
        if (FAILED(error)) { return error; }
        return RoGetAgileReference(AGILEREFERENCE_DEFAULT, __uuidof(IDataObject), object.get(), reference_.put());
    }
    HRESULT FileDragDataReference::Resolve(winrt::com_ptr<IDataObject>& object) const noexcept
    { object = nullptr; return reference_ ? reference_->Resolve(IID_PPV_ARGS(object.put())) : E_POINTER; }
    HRESULT IncomingFileDrag::Begin() noexcept
    {
        std::scoped_lock lock(mutex_); if (ended_) { return E_ABORT; }
        if (started_) { return S_FALSE; }
        started_ = true;
        winrt::com_ptr<IDataObject> object; auto error = reference_.Resolve(object); if (FAILED(error)) { return error; }
        const auto capability = object.try_as<IDataObjectAsyncCapability>(); BOOL supported{};
        if (!capability || FAILED(capability->GetAsyncMode(&supported)) || !supported) { return S_OK; }
        error = capability->StartOperation(nullptr); if (SUCCEEDED(error)) { asynchronous_.store(true); } return error;
    }
    HRESULT IncomingFileDrag::Finish(const HRESULT result, const DWORD logical_effect) noexcept
    {
        std::scoped_lock lock(mutex_); if (ended_) { return S_FALSE; } ended_ = true;
        winrt::com_ptr<IDataObject> object; auto error = reference_.Resolve(object); if (FAILED(error)) { return error; }
        // Blink performs the whole filesystem Move. NEVER request source-side
        // deletion by reporting an unoptimized DROPEFFECT_MOVE.
        const auto physical = moving_ ? DROPEFFECT_NONE : SUCCEEDED(result) ? logical_effect : DROPEFFECT_NONE;
        const auto notified = Notify(object.get(), PerformedFormat(), physical);
        (void)Notify(object.get(), LogicalFormat(), SUCCEEDED(result) ? logical_effect : DROPEFFECT_NONE);
        const auto capability = object.try_as<IDataObjectAsyncCapability>();
        const auto ended = asynchronous_.load() && capability ? capability->EndOperation(result, nullptr, physical) : S_OK;
        return FAILED(ended) ? ended : notified;
    }
}
