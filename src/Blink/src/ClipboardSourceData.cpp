#include "Blink/App/ClipboardSourceData.h"
#include "Blink/App/CopyTransferPresentation.h"
#include <shlobj.h>
#include <shldisp.h>
#include <wrl.h>
#include <cstring>
#include <new>
#include <optional>
#include <filesystem>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Uuid.lib")

namespace blink::app
{
    namespace
    {
        struct Medium
        {
            STGMEDIUM value{};
            ~Medium() { if (value.tymed != TYMED_NULL) { ReleaseStgMedium(&value); } }
        };
        HRESULT GlobalData(const void* bytes, const std::size_t size, STGMEDIUM* output) noexcept
        {
            if (!output) { return E_POINTER; }
            *output = {};
            const auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
            if (!memory) { return E_OUTOFMEMORY; }
            auto* data = GlobalLock(memory);
            if (!data) { GlobalFree(memory); return E_OUTOFMEMORY; }
            std::memcpy(data, bytes, size); GlobalUnlock(memory);
            output->tymed = TYMED_HGLOBAL; output->hGlobal = memory;
            return S_OK;
        }
        HRESULT Validate(const FORMATETC* format) noexcept
        {
            if (!format) { return E_POINTER; }
            if (format->ptd) { return DV_E_DVTARGETDEVICE; }
            if (format->dwAspect != DVASPECT_CONTENT) { return DV_E_DVASPECT; }
            if (format->lindex != -1) { return DV_E_LINDEX; }
            return format->tymed & TYMED_HGLOBAL ? S_OK : DV_E_TYMED;
        }
        FORMATETC Format(const UINT id) noexcept
        { return { static_cast<CLIPFORMAT>(id), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }; }
        HRESULT SetEffect(IDataObject* object, const UINT id, const DWORD effect) noexcept
        {
            Medium medium; auto result = GlobalData(&effect, sizeof(effect), &medium.value);
            if (FAILED(result)) { return result; }
            auto format = Format(id);
            result = object->SetData(&format, &medium.value, TRUE);
            if (SUCCEEDED(result)) { medium.value = {}; }
            return result;
        }
        struct Formats
        {
            UINT preferred{ RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT) };
            UINT performed{ RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT) };
            UINT logical{ RegisterClipboardFormatW(CFSTR_LOGICALPERFORMEDDROPEFFECT) };
            UINT pasted{ RegisterClipboardFormatW(CFSTR_PASTESUCCEEDED) };
            explicit operator bool() const noexcept { return preferred && performed && logical && pasted; }
        };
        class Source final : public Microsoft::WRL::RuntimeClass<
            Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDataObject, IDataObjectAsyncCapability>
        {
        public:
            Source(IDataObject* shell, std::vector<std::byte> paths, const DWORD preferred,
                Formats formats, ClipboardSourceCallback callback, ClipboardSourceValidation validation)
                : paths_(std::move(paths)), preferred_(preferred), formats_(formats),
                  callback_(std::move(callback)), validation_(std::move(validation))
            { shell_.copy_from(shell); asynchronous_ = shell_.try_as<IDataObjectAsyncCapability>(); }

            HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* output) override
            {
                if (!output) { return E_POINTER; } *output = {};
                if (!format) { return E_POINTER; }
                if (moved_) { return DV_E_FORMATETC; }
                if (format->cfFormat == CF_HDROP || format->cfFormat == formats_.preferred)
                {
                    if (const auto error = Validate(format); FAILED(error)) { return error; }
                    if (format->cfFormat == CF_HDROP)
                    {
                        const auto valid = ValidateSources();
                        if (valid != S_OK) { Notify(valid, DROPEFFECT_NONE, false, false); return valid; }
                    }
                    return format->cfFormat == CF_HDROP ? GlobalData(paths_.data(), paths_.size(), output) :
                        GlobalData(&preferred_, sizeof(preferred_), output);
                }
                if (const auto* effect = Feedback(format->cfFormat); effect && *effect)
                {
                    if (const auto error = Validate(format); FAILED(error)) { return error; }
                    return GlobalData(&**effect, sizeof(DWORD), output);
                }
                if (!Feedback(format->cfFormat))
                {
                    const auto valid = ValidateSources();
                    if (valid != S_OK) { Notify(valid, DROPEFFECT_NONE, false, false); return valid; }
                }
                return shell_->GetData(format, output);
            }
            HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override
            {
                if (!format) { return E_POINTER; }
                if (moved_) { return DV_E_FORMATETC; }
                if (format->cfFormat == CF_HDROP || format->cfFormat == formats_.preferred) { return Validate(format); }
                if (const auto* effect = Feedback(format->cfFormat); effect && *effect) { return Validate(format); }
                return shell_->QueryGetData(format);
            }
            HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* format, STGMEDIUM* medium) override
            {
                if (!format) { return E_POINTER; }
                if (moved_ || format->cfFormat == CF_HDROP || format->cfFormat == formats_.preferred || Feedback(format->cfFormat))
                { return DATA_E_FORMATETC; }
                const auto valid = ValidateSources();
                if (valid != S_OK) { Notify(valid, DROPEFFECT_NONE, false, false); return valid; }
                return shell_->GetDataHere(format, medium);
            }
            HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* format, FORMATETC* output) override
            {
                if (!output) { return E_POINTER; }
                if (!format) { return E_POINTER; }
                if (format->cfFormat == CF_HDROP || format->cfFormat == formats_.preferred || Feedback(format->cfFormat))
                { output->ptd = nullptr; return DATA_S_SAMEFORMATETC; }
                return shell_->GetCanonicalFormatEtc(format, output);
            }
            HRESULT STDMETHODCALLTYPE SetData(FORMATETC* format, STGMEDIUM* medium, BOOL release) override
            {
                if (!format) { return E_POINTER; }
                auto* stored = Feedback(format->cfFormat);
                if (!stored)
                {
                    if (format->cfFormat == CF_HDROP || format->cfFormat == formats_.preferred) { return DV_E_FORMATETC; }
                    return shell_->SetData(format, medium, release);
                }
                if (const auto error = Validate(format); FAILED(error)) { return error; }
                if (!medium || medium->tymed != TYMED_HGLOBAL || !medium->hGlobal || GlobalSize(medium->hGlobal) < sizeof(DWORD))
                { return DV_E_TYMED; }
                auto* bytes = GlobalLock(medium->hGlobal); if (!bytes) { return E_OUTOFMEMORY; }
                DWORD effect{}; std::memcpy(&effect, bytes, sizeof(effect)); GlobalUnlock(medium->hGlobal);
                if (effect != DROPEFFECT_NONE && effect != DROPEFFECT_COPY && effect != DROPEFFECT_MOVE && effect != DROPEFFECT_LINK)
                { return E_INVALIDARG; }
                // A consumed MOVE must never ask the Shell to delete a second
                // time, even when a receiver retries its completion message.
                if (moved_)
                { if (release) { ReleaseStgMedium(medium); } return S_OK; }
                const bool cleanup = effect == DROPEFFECT_MOVE &&
                    (format->cfFormat == formats_.performed ||
                        (format->cfFormat == formats_.pasted && performed_ == DROPEFFECT_MOVE));
                if (cleanup)
                {
                    const auto valid = ValidateSources();
                    if (valid != S_OK) { Notify(valid, effect, false, true); return valid; }
                }
                // The Shell owns filesystem cleanup for this external paste.
                // Logical feedback is presentation-only and not universally
                // accepted by Shell providers, so retain it locally. The native
                // filesystem object's Paste Succeeded MOVE deletes originals;
                // consume optimized completion locally, never delete twice.
                const bool local = format->cfFormat == formats_.logical ||
                    (format->cfFormat == formats_.pasted && effect == DROPEFFECT_MOVE && !cleanup);
                const auto result = local ? S_OK : shell_->SetData(format, medium, release);
                if (SUCCEEDED(result) && local && release) { ReleaseStgMedium(medium); }
                if (result != S_OK) { Notify(result, effect, false, cleanup); return result; }
                *stored = effect;
                if (format->cfFormat == formats_.pasted)
                {
                    moved_ = effect == DROPEFFECT_MOVE;
                    Notify(result, effect, true, cleanup);
                    if (!moved_) { ResetAttempt(); }
                }
                return result;
            }
            HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** output) override
            {
                if (!output) { return E_POINTER; } *output = nullptr;
                if (moved_ && direction == DATADIR_GET) { return SHCreateStdEnumFmtEtc(0, nullptr, output); }
                if (direction == DATADIR_SET)
                {
                    FORMATETC formats[]{ Format(formats_.performed), Format(formats_.logical), Format(formats_.pasted) };
                    return SHCreateStdEnumFmtEtc(3, formats, output);
                }
                if (direction != DATADIR_GET) { return E_INVALIDARG; }
                // Flush only path metadata. Advertising arbitrary provider
                // streams could read an entire large file while closing.
                FORMATETC formats[]{ Format(CF_HDROP), Format(formats_.preferred),
                    Format(RegisterClipboardFormatW(CFSTR_SHELLIDLIST)) };
                const auto count = formats[2].cfFormat && shell_->QueryGetData(&formats[2]) == S_OK ? 3u : 2u;
                return SHCreateStdEnumFmtEtc(count, formats, output);
            }
            HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* format, DWORD flags, IAdviseSink* sink, DWORD* connection) override
            { return shell_->DAdvise(format, flags, sink, connection); }
            HRESULT STDMETHODCALLTYPE DUnadvise(DWORD connection) override { return shell_->DUnadvise(connection); }
            HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** output) override { return shell_->EnumDAdvise(output); }
            HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL mode) override
            { return asynchronous_ ? asynchronous_->SetAsyncMode(mode) : mode ? E_NOTIMPL : S_OK; }
            HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL* mode) override
            { if (!mode) { return E_POINTER; } if (asynchronous_) { return asynchronous_->GetAsyncMode(mode); } *mode = FALSE; return S_OK; }
            HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx* context) override
            { return asynchronous_ ? asynchronous_->StartOperation(context) : E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE InOperation(BOOL* active) override
            { if (!active) { return E_POINTER; } if (asynchronous_) { return asynchronous_->InOperation(active); } *active = FALSE; return S_OK; }
            HRESULT STDMETHODCALLTYPE EndOperation(HRESULT result, IBindCtx* context, DWORD effects) override
            {
                if (!asynchronous_) { return E_NOTIMPL; }
                if (SUCCEEDED(result) && effects == DROPEFFECT_MOVE && performed_ == DROPEFFECT_MOVE && !moved_)
                {
                    const auto valid = ValidateSources();
                    if (valid != S_OK) { Notify(valid, effects, false, true); return valid; }
                }
                const auto ended = asynchronous_->EndOperation(result, context, effects);
                if (FAILED(ended) || FAILED(result))
                { Notify(FAILED(ended) ? ended : result, effects, false, false); if (!moved_) { ResetAttempt(); } }
                return ended;
            }
        private:
            void ResetAttempt() noexcept { performed_.reset(); logical_.reset(); pasted_.reset(); }
            std::optional<DWORD>* Feedback(const UINT id) noexcept
            { return id == formats_.performed ? &performed_ : id == formats_.logical ? &logical_ : id == formats_.pasted ? &pasted_ : nullptr; }
            HRESULT ValidateSources() noexcept
            {
                try
                {
                    const auto result = validation_ ? validation_() : S_OK;
                    // SetData success transfers medium ownership. A positive
                    // validation rejection must therefore become a failure.
                    return result == S_OK || FAILED(result) ? result : E_UNEXPECTED;
                }
                catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
                catch (...) { return E_FAIL; }
            }
            void Notify(HRESULT result, DWORD effect, bool complete, bool cleanup) noexcept
            {
                try
                {
                    if (callback_) { callback_({ result, effect, performed_.value_or(0), logical_.value_or(0),
                        complete, performed_.has_value(), logical_.has_value(), cleanup }); }
                }
                catch (...) {} // A UI notification cannot undo the Shell result.
            }
            winrt::com_ptr<IDataObject> shell_;
            winrt::com_ptr<IDataObjectAsyncCapability> asynchronous_;
            std::vector<std::byte> paths_;
            DWORD preferred_{};
            Formats formats_;
            ClipboardSourceCallback callback_;
            ClipboardSourceValidation validation_;
            std::optional<DWORD> performed_, logical_, pasted_;
            bool moved_{};
        };
        struct Pidls
        {
            std::vector<PIDLIST_ABSOLUTE> values;
            ~Pidls() { for (const auto value : values) { CoTaskMemFree(value); } }
        };
    }
    ClipboardSourceValidation MakeClipboardSourceValidation(std::vector<std::wstring> paths, ClipboardCopySources expected)
    {
        return [paths = std::move(paths), expected = std::move(expected)] {
            ClipboardCopySources actual;
            const auto error = CaptureClipboardCopySources(paths, actual);
            return error ? HRESULT_FROM_WIN32(error) :
                SameCapturedCopySources(expected, actual) ? S_OK : HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
        };
    }
    HRESULT WrapClipboardSourceData(IDataObject* shell, const std::span<const std::wstring> paths,
        const ClipboardTransferIntent intent, ClipboardSourceCallback callback,
        ClipboardSourceValidation validation, winrt::com_ptr<IDataObject>& output) noexcept
    {
        output = nullptr;
        try
        {
            if (!shell) { return E_POINTER; }
            auto encoded = EncodeClipboardFilePaths(paths);
            const auto effect = ClipboardDropEffectValue(intent);
            if (encoded.empty() || !effect) { return E_INVALIDARG; }
            Formats formats;
            if (!formats) { return E_FAIL; }
            const auto preferred = SetEffect(shell, formats.preferred, effect);
            if (preferred != S_OK) { return preferred; }
            auto object = Microsoft::WRL::Make<Source>(shell, std::move(encoded), effect, formats,
                std::move(callback), std::move(validation));
            if (!object) { return E_OUTOFMEMORY; }
            return object.CopyTo(output.put());
        }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }
    HRESULT CreateClipboardSourceData(const std::span<const std::wstring> paths,
        const ClipboardTransferIntent intent, ClipboardSourceCallback callback,
        ClipboardSourceValidation validation, winrt::com_ptr<IDataObject>& output) noexcept
    {
        output = nullptr;
        try
        {
            if (EncodeClipboardFilePaths(paths).empty() || !ClipboardDropEffectValue(intent)) { return E_INVALIDARG; }
            Pidls pidls; pidls.values.reserve(paths.size());
            for (const auto& path : paths)
            {
                if (!std::filesystem::path(path).is_absolute()) { return E_INVALIDARG; }
                PIDLIST_ABSOLUTE item{};
                const auto parsed = SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr);
                if (parsed != S_OK) { if (item) { CoTaskMemFree(item); } return parsed; }
                if (!item) { return E_UNEXPECTED; }
                pidls.values.push_back(item);
            }
            winrt::com_ptr<IShellItemArray> items;
            std::vector<PCIDLIST_ABSOLUTE> borrowed(pidls.values.begin(), pidls.values.end());
            auto result = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(pidls.values.size()),
                borrowed.data(), items.put());
            if (result != S_OK) { return result; }
            winrt::com_ptr<IDataObject> shell;
            result = items->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(shell.put()));
            if (result != S_OK) { return result; }
            return WrapClipboardSourceData(shell.get(), paths, intent, std::move(callback), std::move(validation), output);
        }
        catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        catch (...) { return E_FAIL; }
    }
}
