#include "ClipboardSourceDataTests.h"
#include "Blink/App/ClipboardSourceData.h"
#include "Blink/App/ClipboardDataObject.h"
#include <shlobj.h>
#include <shldisp.h>
#include <wrl.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace blink::app;
    using namespace Microsoft::WRL;
    bool Check(bool value, const char* label)
    { std::cout << (value ? "PASS " : "FAIL ") << label << '\n'; return value; }
    UINT Id(const wchar_t* name) { return RegisterClipboardFormatW(name); }
    FORMATETC Format(UINT id) { return { static_cast<CLIPFORMAT>(id), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL }; }
    class Owner final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IUnknown>
    {
    public:
        Owner(HGLOBAL memory, std::shared_ptr<unsigned> released) : memory_(memory), released_(std::move(released)) {}
        ~Owner() { GlobalFree(memory_); ++*released_; }
    private:
        HGLOBAL memory_;
        std::shared_ptr<unsigned> released_;
    };
    struct Sent { HRESULT result; unsigned before_release{}, total_release{}; };
    Sent Set(IDataObject* object, UINT id, DWORD effect, BOOL release = TRUE)
    {
        auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(effect));
        auto bytes = GlobalLock(memory); std::memcpy(bytes, &effect, sizeof(effect)); GlobalUnlock(memory);
        auto count = std::make_shared<unsigned>(); auto owner = Make<Owner>(memory, count);
        STGMEDIUM medium{ TYMED_HGLOBAL }; medium.hGlobal = memory; medium.pUnkForRelease = owner.Detach();
        auto format = Format(id); const auto error = object->SetData(&format, &medium, release);
        const auto before = *count;
        if (FAILED(error) || !release) { ReleaseStgMedium(&medium); }
        return { error, before, *count };
    }
    class Shell final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDataObject, IDataObjectAsyncCapability>
    {
    public:
        std::map<UINT, DWORD> effects;
        unsigned writes{}, queries{}, end_calls{};
        UINT fail_format{};
        HRESULT failure{ E_ACCESSDENIED }, end_error{ S_OK };
        BOOL async{ TRUE }, active{};
        HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* output) override
        {
            if (!format || !output) { return E_POINTER; } *output = {};
            const DWORD value = effects.contains(format->cfFormat) ? effects[format->cfFormat] : 91;
            auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(value));
            if (!memory) { return E_OUTOFMEMORY; }
            auto bytes = GlobalLock(memory); if (!bytes) { GlobalFree(memory); return E_OUTOFMEMORY; }
            std::memcpy(bytes, &value, sizeof(value)); GlobalUnlock(memory);
            output->tymed = TYMED_HGLOBAL; output->hGlobal = memory; return S_OK;
        }
        HRESULT STDMETHODCALLTYPE SetData(FORMATETC* format, STGMEDIUM* medium, BOOL release) override
        {
            if (!format || !medium) { return E_POINTER; }
            ++writes;
            if (format->cfFormat == fail_format) { return failure; }
            auto data = GlobalLock(medium->hGlobal); DWORD value{}; std::memcpy(&value, data, sizeof(value)); GlobalUnlock(medium->hGlobal);
            effects[format->cfFormat] = value;
            if (release) { ReleaseStgMedium(medium); }
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override
        { ++queries; return format ? S_OK : E_POINTER; }
        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* output) override
        { if (output) { output->ptd = nullptr; } return output ? DATA_S_SAMEFORMATETC : E_POINTER; }
        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC** output) override
        { if (output) { *output = nullptr; } return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE SetAsyncMode(BOOL mode) override { async = mode; return S_OK; }
        HRESULT STDMETHODCALLTYPE GetAsyncMode(BOOL* mode) override { if (!mode) { return E_POINTER; } *mode = async; return S_OK; }
        HRESULT STDMETHODCALLTYPE StartOperation(IBindCtx*) override { active = TRUE; return S_OK; }
        HRESULT STDMETHODCALLTYPE InOperation(BOOL* value) override { if (!value) { return E_POINTER; } *value = active; return S_OK; }
        HRESULT STDMETHODCALLTYPE EndOperation(HRESULT, IBindCtx*, DWORD) override { ++end_calls; active = FALSE; return end_error; }
    };
    struct Bound
    {
        winrt::com_ptr<IDataObject> object;
        Microsoft::WRL::ComPtr<Shell> shell{ Make<Shell>() };
        std::vector<ClipboardSourceReport> reports;
        unsigned validated{};
        HRESULT validation{ S_OK }, result{};
        explicit Bound(ClipboardTransferIntent intent = ClipboardTransferIntent::Move)
        {
            const std::vector<std::wstring> paths{ L"D:\\资料\\甲.txt", L"D:\\资料\\甲.txt", L"D:\\资料\\目录" };
            result = WrapClipboardSourceData(shell.Get(), paths, intent,
                [this](ClipboardSourceReport value) { reports.push_back(value); },
                [this] { ++validated; return validation; }, object);
        }
    };
    struct Fixture
    {
        std::filesystem::path root;
        bool cleaned{};
        Fixture()
        {
            GUID id{}; CoCreateGuid(&id); wchar_t text[40]{}; StringFromGUID2(id, text, 40);
            root = std::filesystem::absolute(std::filesystem::path(L".build") / L"clipboard-send" / L"integration" /
                L"native-source-fixtures" / (L"owned-" + std::wstring(text + 1, 36)));
            std::filesystem::create_directories(root);
        }
        void Write(const std::filesystem::path& path, const char* data)
        { std::filesystem::create_directories(path.parent_path()); std::ofstream stream(path, std::ios::binary); stream << data; stream.close(); if (!stream) { throw std::runtime_error("write fixture"); } }
        bool Cleanup() noexcept
        {
            try
            {
                std::uintmax_t bytes{}; unsigned files{};
                for (const auto& item : std::filesystem::recursive_directory_iterator(root))
                { if (item.is_regular_file()) { bytes += item.file_size(); ++files; } }
                std::error_code error; const auto removed = std::filesystem::remove_all(root, error);
                cleaned = !error && !std::filesystem::exists(root);
                auto name = root.generic_u8string();
                std::ofstream log(std::filesystem::path(L".build") / L"clipboard-send" / L"integration" / L"native-source-cleanup.jsonl", std::ios::app);
                log << "{\"fixture\":\"" << std::string(reinterpret_cast<const char*>(name.data()), name.size())
                    << "\",\"removed_bytes\":" << bytes << ",\"removed_files\":" << files << ",\"removed_entries\":" << removed
                    << ",\"complete\":" << (cleaned ? "true" : "false") << "}\n";
                return cleaned && static_cast<bool>(log);
            }
            catch (...) { return false; }
        }
        ~Fixture() { if (!cleaned) { (void)Cleanup(); } }
    };
    bool Run()
    {
        bool passed = true;
        const auto performed = Id(CFSTR_PERFORMEDDROPEFFECT), logical = Id(CFSTR_LOGICALPERFORMEDDROPEFFECT), pasted = Id(CFSTR_PASTESUCCEEDED);
        {
            winrt::com_ptr<IDataObject> output;
            const std::vector<std::wstring> paths{ L"D:\\one.txt" };
            passed &= Check(WrapClipboardSourceData(nullptr, paths, ClipboardTransferIntent::Copy, {}, {}, output) == E_POINTER && !output,
                "clipboard_source_null_inner_does_not_create_a_publisher");
            auto shell = Make<Shell>();
            passed &= Check(WrapClipboardSourceData(shell.Get(), {}, ClipboardTransferIntent::Copy, {}, {}, output) == E_INVALIDARG && !output,
                "clipboard_source_empty_list_is_rejected_before_shell_changes");
            shell->fail_format = Id(CFSTR_PREFERREDDROPEFFECT);
            passed &= Check(WrapClipboardSourceData(shell.Get(), paths, ClipboardTransferIntent::Move, {}, {}, output) == E_ACCESSDENIED && !output,
                "clipboard_source_preference_failure_preserves_hresult_and_no_object");
            passed &= Check(CreateClipboardSourceData({ paths.data(), 0 }, ClipboardTransferIntent::Copy, {}, {}, output) == E_INVALIDARG,
                "clipboard_source_native_factory_rejects_empty_before_io");
            const std::vector<std::wstring> relative{ L"relative.txt" };
            passed &= Check(CreateClipboardSourceData(relative, ClipboardTransferIntent::Copy, {}, {}, output) == E_INVALIDARG,
                "clipboard_source_native_factory_requires_absolute_paths");
        }
        {
            Bound bound(ClipboardTransferIntent::Copy);
            auto read = ReadClipboardDataObject(bound.object.get());
            passed &= Check(bound.result == S_OK && read.result == S_OK && read.payload.paths.size() == 3 && read.payload.paths[0] == read.payload.paths[1] && read.payload.intent == ClipboardTransferIntent::Copy,
                "clipboard_source_immutable_unicode_order_and_duplicates_match_existing_receiver");
            auto format = Format(CF_HDROP); format.lindex = 0; STGMEDIUM output{};
            passed &= Check(bound.object->GetData(&format, &output) == DV_E_LINDEX && output.tymed == TYMED_NULL,
                "clipboard_source_invalid_index_produces_no_medium");
            format = Format(CF_HDROP); format.dwAspect = DVASPECT_ICON;
            passed &= Check(bound.object->QueryGetData(&format) == DV_E_DVASPECT,
                "clipboard_source_content_aspect_is_required");
            format = Format(CF_HDROP); format.tymed = TYMED_ISTREAM;
            passed &= Check(bound.object->QueryGetData(&format) == DV_E_TYMED,
                "clipboard_source_wrong_medium_does_not_fall_back_to_native_order");
            passed &= Check(Set(bound.object.get(), Id(CFSTR_PREFERREDDROPEFFECT), DROPEFFECT_MOVE).result == DV_E_FORMATETC,
                "clipboard_source_receiver_cannot_rewrite_captured_intent");
            const auto read_validations = bound.validated;
            const auto first = Set(bound.object.get(), performed, DROPEFFECT_COPY);
            const auto second = Set(bound.object.get(), logical, DROPEFFECT_COPY, FALSE);
            const auto third = Set(bound.object.get(), pasted, DROPEFFECT_COPY);
            passed &= Check(first.result == S_OK && first.before_release == 1 && first.total_release == 1 &&
                second.result == S_OK && second.before_release == 0 && second.total_release == 1 && third.total_release == 1,
                "clipboard_source_true_and_false_medium_ownership_each_release_once");
            passed &= Check(bound.reports.size() == 1 && bound.reports[0].completed && bound.reports[0].effect == DROPEFFECT_COPY && !bound.reports[0].source_cleanup && bound.validated == read_validations,
                "clipboard_source_copy_completion_never_requests_source_deletion");
            (void)Set(bound.object.get(), pasted, DROPEFFECT_COPY);
            passed &= Check(bound.reports.size() == 2 && ReadClipboardDataObject(bound.object.get()).result == S_OK,
                "clipboard_source_copy_can_be_pasted_repeatedly");
            winrt::com_ptr<IEnumFORMATETC> formats; auto error = bound.object->EnumFormatEtc(DATADIR_GET, formats.put());
            FORMATETC value{}; unsigned count{}; bool metadata_only = true;
            while (formats && formats->Next(1, &value, nullptr) == S_OK)
            { ++count; metadata_only &= value.cfFormat == CF_HDROP || value.cfFormat == Id(CFSTR_PREFERREDDROPEFFECT) || value.cfFormat == Id(CFSTR_SHELLIDLIST); }
            passed &= Check(error == S_OK && count == 3 && metadata_only,
                "clipboard_source_flush_advertises_only_bounded_path_metadata");
        }
        {
            Bound bound;
            (void)Set(bound.object.get(), performed, DROPEFFECT_NONE);
            (void)Set(bound.object.get(), logical, DROPEFFECT_MOVE);
            const auto done = Set(bound.object.get(), pasted, DROPEFFECT_MOVE);
            passed &= Check(done.result == S_OK && bound.validated == 0 && bound.reports.size() == 1 && bound.reports[0].completed && !bound.reports[0].source_cleanup,
                "clipboard_source_optimized_move_accepts_already_gone_originals_without_delete");
            const auto calls = bound.shell->writes;
            const auto repeated = Set(bound.object.get(), pasted, DROPEFFECT_MOVE);
            (void)Set(bound.object.get(), performed, DROPEFFECT_MOVE);
            passed &= Check(repeated.result == S_OK && repeated.total_release == 1 && calls == bound.shell->writes && bound.reports.size() == 1,
                "clipboard_source_repeated_move_receipt_never_deletes_twice");
            auto format = Format(CF_HDROP); STGMEDIUM medium{};
            passed &= Check(bound.object->GetData(&format, &medium) == DV_E_FORMATETC && bound.object->QueryGetData(&format) == DV_E_FORMATETC,
                "clipboard_source_consumed_cut_cannot_replay_a_replacement_at_the_old_path");
            winrt::com_ptr<IEnumFORMATETC> formats;
            passed &= Check(bound.object->EnumFormatEtc(DATADIR_GET, formats.put()) == S_OK && formats->Next(1, &format, nullptr) == S_FALSE,
                "clipboard_source_consumed_move_flush_exposes_no_stale_file_list");
        }
        {
            Bound bound;
            (void)Set(bound.object.get(), performed, DROPEFFECT_MOVE);
            (void)Set(bound.object.get(), pasted, DROPEFFECT_MOVE);
            passed &= Check(bound.validated == 2 && bound.reports.size() == 1 && bound.reports[0].source_cleanup && bound.reports[0].completed && bound.shell->writes == 3,
                "clipboard_source_normal_move_delegates_cleanup_once_after_identity_rechecks");
        }
        {
            Bound bound;
            (void)Set(bound.object.get(), performed, DROPEFFECT_MOVE);
            (void)Set(bound.object.get(), pasted, DROPEFFECT_NONE);
            (void)Set(bound.object.get(), pasted, DROPEFFECT_MOVE);
            passed &= Check(bound.validated == 1 && bound.reports.size() == 2 && bound.shell->effects[pasted] == DROPEFFECT_NONE &&
                !bound.reports.back().has_performed && !bound.reports.back().source_cleanup,
                "clipboard_source_cancelled_attempt_cannot_reuse_old_delete_effect_for_next_optimized_move");
        }
        {
            Bound bound; bound.validation = HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
            const auto read = ReadClipboardDataObject(bound.object.get());
            passed &= Check(read.result == bound.validation && read.payload.paths.empty() && bound.reports.size() == 1 && !bound.reports.front().completed,
                "clipboard_source_render_rechecks_original_identity_independently_of_clipboard_sequence");
        }
        {
            Bound bound; bound.validation = HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
            const auto rejected = Set(bound.object.get(), performed, DROPEFFECT_MOVE);
            passed &= Check(rejected.result == bound.validation && rejected.before_release == 0 && rejected.total_release == 1 && bound.shell->writes == 1 && !bound.reports.front().completed,
                "clipboard_source_replaced_original_blocks_shell_deletion_and_retains_caller_medium");
            bound.validation = S_FALSE;
            passed &= Check(Set(bound.object.get(), performed, DROPEFFECT_MOVE).result == E_UNEXPECTED && bound.shell->writes == 1,
                "clipboard_source_positive_validation_rejection_is_not_a_medium_ownership_success");
        }
        {
            Bound bound;
            (void)Set(bound.object.get(), performed, DROPEFFECT_MOVE);
            bound.shell->fail_format = pasted;
            const auto failed = Set(bound.object.get(), pasted, DROPEFFECT_MOVE);
            passed &= Check(failed.result == E_ACCESSDENIED && failed.before_release == 0 && failed.total_release == 1 && !bound.reports.front().completed,
                "clipboard_source_native_failure_is_not_promoted_to_move_completion");
            passed &= Check(ReadClipboardDataObject(bound.object.get()).result == S_OK,
                "clipboard_source_failed_move_does_not_consume_clipboard_files");
            const auto invalid = Set(bound.object.get(), logical, DROPEFFECT_COPY | DROPEFFECT_MOVE);
            passed &= Check(invalid.result == E_INVALIDARG && invalid.before_release == 0,
                "clipboard_source_ambiguous_effect_is_rejected_without_ownership_transfer");
        }
        {
            Bound bound; auto async = bound.object.as<IDataObjectAsyncCapability>(); BOOL mode{}, active{};
            const auto started = async->SetAsyncMode(FALSE) == S_OK && async->GetAsyncMode(&mode) == S_OK && !mode &&
                async->StartOperation(nullptr) == S_OK && async->InOperation(&active) == S_OK && active;
            passed &= Check(started, "clipboard_source_async_protocol_keeps_shell_lifetime_and_state");
            const auto ended = async->EndOperation(E_ABORT, nullptr, DROPEFFECT_NONE);
            passed &= Check(ended == S_OK && bound.reports.size() == 1 && bound.reports[0].result == E_ABORT && !bound.reports[0].completed,
                "clipboard_source_async_failure_restores_state_without_claiming_a_paste");
        }
        // Real Shell data-object integration on GUID-owned files. No Clipboard
        // is opened, replaced, flushed or used to transport these fixtures.
        {
            Fixture fixture;
            const auto file = fixture.root / L"first.txt", directory = fixture.root / L"目录";
            fixture.Write(file, "first-original"); fixture.Write(directory / L"nested.txt", "directory-original");
            const std::vector<std::wstring> paths{ file.native(), directory.native() };
            ClipboardCopySources sources; const auto captured = CaptureClipboardCopySources(paths, sources);
            winrt::com_ptr<IDataObject> object;
            auto created = CreateClipboardSourceData(paths, ClipboardTransferIntent::Move, {}, MakeClipboardSourceValidation(paths, sources), object);
            passed &= Check(captured == ERROR_SUCCESS && created == S_OK && ReadClipboardDataObject(object.get()).payload.paths == paths,
                "clipboard_source_native_shell_file_and_directory_offer_matches_captured_paths");
            if (created == S_OK)
            {
                const auto target = fixture.root / L"target"; std::filesystem::create_directory(target);
                std::filesystem::copy(file, target / L"first.txt");
                std::filesystem::copy(directory, target / L"目录", std::filesystem::copy_options::recursive);
                const auto before_paste = Set(object.get(), performed, DROPEFFECT_MOVE);
                passed &= Check(before_paste.result == S_OK && std::filesystem::exists(file) && std::filesystem::exists(directory),
                    "clipboard_source_native_performed_move_alone_does_not_remove_sources");
                const auto finished = Set(object.get(), pasted, DROPEFFECT_MOVE);
                passed &= Check(finished.result == S_OK && !std::filesystem::exists(file) && !std::filesystem::exists(directory) &&
                    std::filesystem::file_size(target / L"first.txt") == 14 && std::filesystem::file_size(target / L"目录" / L"nested.txt") == 18,
                    "clipboard_source_native_normal_paste_removes_only_originals_after_real_target_copy");
                fixture.Write(file, "replacement"); (void)Set(object.get(), pasted, DROPEFFECT_MOVE);
                passed &= Check(std::filesystem::exists(file), "clipboard_source_native_duplicate_receipt_preserves_new_file_at_consumed_path");
            }
            object = nullptr;
            const auto replaced = fixture.root / L"replace.txt", retained = fixture.root / L"retained.txt";
            fixture.Write(replaced, "saved-original"); const std::vector<std::wstring> one{ replaced.native() };
            sources = {}; const auto read = CaptureClipboardCopySources(one, sources);
            created = CreateClipboardSourceData(one, ClipboardTransferIntent::Move, {}, MakeClipboardSourceValidation(one, sources), object);
            std::filesystem::rename(replaced, retained); fixture.Write(replaced, "replacement-kept");
            const auto stale = ReadClipboardDataObject(object.get());
            passed &= Check(stale.result == HRESULT_FROM_WIN32(ERROR_FILE_INVALID) && stale.payload.paths.empty() && std::filesystem::exists(replaced),
                "clipboard_source_native_replaced_path_is_not_offered_to_an_external_receiver");
            const auto prevented = object ? Set(object.get(), performed, DROPEFFECT_MOVE).result : E_FAIL;
            passed &= Check(read == ERROR_SUCCESS && created == S_OK && prevented == HRESULT_FROM_WIN32(ERROR_FILE_INVALID) &&
                std::filesystem::exists(replaced) && std::filesystem::exists(retained),
                "clipboard_source_native_identity_change_is_vetoed_before_shell_cleanup");
            object = nullptr;
            const auto moved = fixture.root / L"optimized.txt", destination = fixture.root / L"target" / L"optimized.txt";
            fixture.Write(moved, "optimized-original"); const std::vector<std::wstring> optimized{ moved.native() };
            sources = {}; (void)CaptureClipboardCopySources(optimized, sources);
            created = CreateClipboardSourceData(optimized, ClipboardTransferIntent::Move, {}, MakeClipboardSourceValidation(optimized, sources), object);
            std::filesystem::rename(moved, destination); fixture.Write(moved, "new-file-kept");
            if (object) { (void)Set(object.get(), performed, DROPEFFECT_NONE); (void)Set(object.get(), pasted, DROPEFFECT_MOVE); }
            passed &= Check(created == S_OK && std::filesystem::exists(moved) && std::filesystem::file_size(destination) == 18,
                "clipboard_source_native_optimized_move_never_deletes_a_replacement_source");
            object = nullptr;
            const auto cancelled = fixture.root / L"cancelled.txt", retried = fixture.root / L"target" / L"retried.txt";
            fixture.Write(cancelled, "retry-original"); const std::vector<std::wstring> retry{ cancelled.native() };
            sources = {}; (void)CaptureClipboardCopySources(retry, sources);
            created = CreateClipboardSourceData(retry, ClipboardTransferIntent::Move, {}, MakeClipboardSourceValidation(retry, sources), object);
            if (object) { (void)Set(object.get(), performed, DROPEFFECT_MOVE); (void)Set(object.get(), pasted, DROPEFFECT_NONE); }
            std::filesystem::rename(cancelled, retried); fixture.Write(cancelled, "new-file-after-cancel");
            const auto retried_result = object ? Set(object.get(), pasted, DROPEFFECT_MOVE).result : E_FAIL;
            passed &= Check(created == S_OK && retried_result == S_OK && std::filesystem::exists(cancelled) && std::filesystem::exists(retried),
                "clipboard_source_native_cancel_then_optimized_retry_preserves_replacement_and_target");
            object = nullptr;
            passed &= Check(fixture.Cleanup(), "clipboard_source_owned_native_fixtures_are_fully_removed");
        }
        return passed;
    }
}
bool RunClipboardSourceDataTests()
{
    std::promise<bool> ready; auto future = ready.get_future();
    std::thread thread([&] {
        const auto initialized = OleInitialize(nullptr);
        bool passed = Check(SUCCEEDED(initialized), "clipboard_source_own_sta_initialized_without_clipboard_publication");
        try { if (passed) { passed = Run(); } }
        catch (...)
        {
            try { throw; } catch (const std::exception& error) { std::cerr << error.what() << '\n'; } catch (...) {}
            passed = Check(false, "clipboard_source_unexpected_exception");
        }
        if (SUCCEEDED(initialized)) { OleUninitialize(); }
        ready.set_value(passed);
    });
    thread.join(); return future.get();
}
