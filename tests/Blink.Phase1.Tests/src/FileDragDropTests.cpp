#include "FileDragDropTests.h"
#include "Blink/App/FileDragDropModel.h"
#include "Blink/App/Localization.h"
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wrl.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

namespace
{
    namespace fs = std::filesystem;
    using namespace blink::app;
    using namespace std::chrono_literals;
    bool Check(bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry result;
        if (!blink::core::ReadEntryMetadata(path.wstring(), result)) { throw std::runtime_error("drag fixture metadata"); }
        result.name = path.filename().wstring(); result.full_path = path.wstring(); return result;
    }
    std::string Read(const fs::path& path)
    { std::ifstream stream(path, std::ios::binary); return { std::istreambuf_iterator<char>(stream), {} }; }
    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        auto done = std::make_shared<std::promise<Status>>(); auto future = done->get_future();
        action.Completed([done](auto&&, Status value) { done->set_value(value); });
        return future.wait_for(8s) == std::future_status::ready && future.get() == Status::Completed;
    }
    // A separate non-async external source: no Blink token and independent
    // HGLOBAL storage. Its only filesystem responsibility is to supply names.
    class ExternalFiles final : public Microsoft::WRL::RuntimeClass<
        Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDataObject, Microsoft::WRL::FtmBase>
    {
    public:
        std::vector<std::byte> bytes;
        DWORD performed{ 99 }, logical{ 99 };
        unsigned notifications{};
        HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* output) override
        {
            if (!output || !format) { return E_POINTER; } *output = {};
            if (format->cfFormat != CF_HDROP) { return DV_E_FORMATETC; }
            const auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes.size());
            const auto buffer = memory ? GlobalLock(memory) : nullptr;
            if (!buffer) { if (memory) { GlobalFree(memory); } return E_OUTOFMEMORY; }
            std::memcpy(buffer, bytes.data(), bytes.size()); GlobalUnlock(memory);
            output->tymed = TYMED_HGLOBAL; output->hGlobal = memory; return S_OK;
        }
        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override
        { return format && format->cfFormat == CF_HDROP ? S_OK : DV_E_FORMATETC; }
        HRESULT STDMETHODCALLTYPE SetData(FORMATETC* format, STGMEDIUM* medium, BOOL release) override
        {
            if (!format || !medium || medium->tymed != TYMED_HGLOBAL || GlobalSize(medium->hGlobal) < sizeof(DWORD)) { return DV_E_FORMATETC; }
            const auto pointer = GlobalLock(medium->hGlobal); if (!pointer) { return E_FAIL; }
            DWORD value{}; std::memcpy(&value, pointer, sizeof(value)); GlobalUnlock(medium->hGlobal);
            if (format->cfFormat == RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT)) { performed = value; }
            else if (format->cfFormat == RegisterClipboardFormatW(CFSTR_LOGICALPERFORMEDDROPEFFECT)) { logical = value; }
            else { return DV_E_FORMATETC; }
            ++notifications; if (release) { ReleaseStgMedium(medium); } return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* result) override
        { if (!result) { return E_POINTER; } result->ptd = nullptr; return DATA_S_SAMEFORMATETC; }
        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
    };
    CopyItemRequests CopyItems(const ClipboardCopySources& sources, const fs::path& destination)
    {
        ClipboardCopySources bound;
        if (PrepareCopyDestination(sources, destination.wstring(), Entry(destination).identity, bound)) { throw std::runtime_error("drag destination"); }
        CopyItemRequests result;
        if (const auto files = std::get_if<std::vector<FileCopyRequest>>(&bound))
        { for (const auto& file : *files) { result.emplace_back(file); } }
        else if (const auto directory = std::get_if<DirectoryCopyRequest>(&bound)) { result.emplace_back(*directory); }
        else if (const auto mixed = std::get_if<CopyItemRequests>(&bound)) { result = *mixed; }
        return result;
    }
}

bool RunFileDragDropTests()
{
    bool passed = true;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Apartment { HRESULT hr; ~Apartment() { if (SUCCEEDED(hr)) { CoUninitialize(); } } } apartment{ initialized };
    const auto sequence = GetClipboardSequenceNumber();
    const auto root = fs::absolute(fs::path(L".build") / L"file-drag-drop-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    fs::create_directories(root);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{ root };
    try
    {
        const auto source = root / L"来源"; const auto target = root / L"目标";
        fs::create_directory(source); fs::create_directory(target);
        std::vector<blink::core::FileEntry> entries; std::vector<std::wstring> paths;
        for (unsigned index = 0; index < 40; ++index)
        {
            const auto path = source / (L"文件-" + std::to_wstring(index) + L".txt");
            std::ofstream(path) << "payload-" << index;
            entries.push_back(Entry(path)); paths.push_back(path.wstring());
        }
        const FileOperationOrigin origin{ 7, Location::Folder(source.wstring()), Entry(source).identity, 3 };
        FileDragSelection selected;
        passed &= Check(!CaptureFileDragSelection(origin, entries, selected) && selected.paths == paths &&
            std::get<std::vector<FileCopyRequest>>(selected.expected).size() == 40 && !selected.token.empty(),
            "drag_capture_preserves_forty_unicode_names_and_original_link_identities");
        FileDragData data;
        const auto created = CreateFileDragData(selected.paths, selected.token, data);
        const auto decoded = ReadFileDragData(data.object.get());
        passed &= Check(SUCCEEDED(created) && SUCCEEDED(decoded.error) && decoded.paths == paths && decoded.token == selected.token,
            "drag_real_data_object_transfers_forty_paths_without_storageitems_truncation");
        FORMATETC format{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM first{}, second{};
        const auto first_hr = data.object->GetData(&format, &first); const auto second_hr = data.object->GetData(&format, &second);
        const bool independent = SUCCEEDED(first_hr) && SUCCEEDED(second_hr) && first.hGlobal != second.hGlobal &&
            DragQueryFileW(static_cast<HDROP>(first.hGlobal), 0xffffffff, nullptr, 0) == 40;
        if (first.tymed) { ReleaseStgMedium(&first); } if (second.tymed) { ReleaseStgMedium(&second); }
        passed &= Check(independent && ReadFileDragData(data.object.get()).paths == paths,
            "drag_getdata_returns_independent_caller_owned_standard_hdrop_buffers");
        winrt::com_ptr<IEnumFORMATETC> formats;
        const auto enumerated = data.object->EnumFormatEtc(DATADIR_GET, formats.put());
        FORMATETC offered{}; ULONG fetched{};
        passed &= Check(SUCCEEDED(enumerated) && formats->Next(1, &offered, &fetched) == S_OK && offered.cfFormat == CF_HDROP,
            "drag_formats_prefer_standard_filesystem_names");
        auto wrong = format; wrong.tymed = TYMED_ISTREAM;
        passed &= Check(data.object->QueryGetData(&wrong) == DV_E_TYMED && data.object->GetData(nullptr, &first) == E_POINTER &&
            data.object->GetData(&format, nullptr) == E_POINTER && ReadFileDragData(nullptr).error == E_POINTER,
            "drag_data_object_rejects_unusable_formats_and_null_arguments");
        FileDragData invalid;
        passed &= Check(FAILED(CreateFileDragData({}, L"token", invalid)) && !invalid.object &&
            FAILED(CreateFileDragData(paths, L"", invalid)) && FAILED(CreateFileDragData(paths, std::wstring(97, L'x'), invalid)),
            "drag_publication_rejects_empty_or_oversized_payload_without_partial_output");
        FileDragDataReference reference;
        passed &= Check(SUCCEEDED(reference.Capture(data.object.get())), "drag_raw_data_object_can_be_retained_for_worker_preparation");
        const auto prepared = PrepareFileDrag(reference, selected);
        passed &= Check(SUCCEEDED(prepared.error) && prepared.payload.paths == paths && SameCapturedCopySources(selected.expected, prepared.sources),
            "drag_worker_captures_actual_metadata_and_matches_original_internal_source");
        {
            using namespace winrt::Windows::ApplicationModel::DataTransfer;
            DataPackage package; const auto provider = package.as<IDataObjectProvider>();
            const auto attached = provider->SetDataObject(data.object.get()); package.RequestedOperation(DataPackageOperation::Copy | DataPackageOperation::Move);
            winrt::com_ptr<IDataObject> one, two;
            const auto queried = QueryFileDragDataObject(winrt::get_unknown(package), one);
            const auto repeated = QueryFileDragDataObject(winrt::get_unknown(package), two);
            passed &= Check(SUCCEEDED(attached) && SUCCEEDED(queried) && SUCCEEDED(repeated) && one.as<IUnknown>() == two.as<IUnknown>() &&
                ReadFileDragData(one.get()).paths == paths, "drag_datapackage_documented_provider_preserves_full_payload_and_stable_identity");
            winrt::com_ptr<IDataObject> view_object;
            const auto view_hr = QueryFileDragDataObject(winrt::get_unknown(package.GetView()), view_object);
            passed &= Check(SUCCEEDED(view_hr) && ReadFileDragData(view_object.get()).paths == paths,
                "drag_datapackage_view_interop_reads_all_forty_paths_on_current_runtime");
            FileDragDataReference package_reference; const auto captured = package_reference.Capture(winrt::get_unknown(package));
            auto background = std::async(std::launch::async, [package_reference, selected] {
                winrt::init_apartment(); const auto value = PrepareFileDrag(package_reference, selected); winrt::uninit_apartment();
                return SUCCEEDED(value.error) && value.payload.paths == selected.paths;
            });
            passed &= Check(SUCCEEDED(captured) && background.get(), "drag_agile_reference_reads_complete_data_from_separate_worker_apartment");
        }
        {
            auto own = selected; own.token += L"different-session";
            passed &= Check(SUCCEEDED(PrepareFileDrag(reference, own).error), "drag_external_token_does_not_reuse_an_unrelated_internal_selection");
            own = selected; own.paths[0] += L"changed";
            const auto mismatch = PrepareFileDrag(reference, own);
            passed &= Check(mismatch.error == HRESULT_FROM_WIN32(ERROR_FILE_INVALID) && std::holds_alternative<std::monostate>(mismatch.sources),
                "drag_internal_token_with_different_names_rejects_stale_preparation");
            own = selected; std::get<std::vector<FileCopyRequest>>(own.expected)[0].source.identity.file_id[0] ^= 1;
            passed &= Check(PrepareFileDrag(reference, own).error == HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
                "drag_internal_identity_mismatch_never_recaptures_a_replacement_as_original");
            const auto cancelled = std::make_shared<std::atomic_bool>(true); const auto stopped = PrepareFileDrag(reference, selected, cancelled);
            passed &= Check(stopped.error == HRESULT_FROM_WIN32(ERROR_CANCELLED) && std::holds_alternative<std::monostate>(stopped.sources),
                "drag_preparation_cancelled_before_read_keeps_output_empty");
        }
        const auto target_id = Entry(target).identity; const auto both = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        passed &= Check(ChooseFileDropEffect(prepared.sources, target.wstring(), target_id, both, {}) == DROPEFFECT_MOVE,
            "drag_same_volume_defaults_to_move");
        auto other_volume = target_id; other_volume.volume_serial ^= 1;
        passed &= Check(ChooseFileDropEffect(prepared.sources, target.wstring(), other_volume, both, {}) == DROPEFFECT_COPY,
            "drag_different_volume_policy_defaults_to_copy");
        passed &= Check(ChooseFileDropEffect(prepared.sources, target.wstring(), target_id, both, { true }) == DROPEFFECT_COPY &&
            ChooseFileDropEffect(prepared.sources, target.wstring(), other_volume, both, { false, true }) == DROPEFFECT_MOVE,
            "drag_control_and_shift_explicitly_choose_copy_and_move");
        passed &= Check(!ChooseFileDropEffect(prepared.sources, target.wstring(), target_id, both, { false, false, true }) &&
            !ChooseFileDropEffect(prepared.sources, target.wstring(), target_id, both, { true, true }),
            "drag_unsupported_link_modifiers_never_silently_copy_or_move");
        passed &= Check(!ChooseFileDropEffect(prepared.sources, target.wstring(), target_id, DROPEFFECT_COPY, { false, true }) &&
            !ChooseFileDropEffect(prepared.sources, target.wstring(), target_id, DROPEFFECT_MOVE, { true }) &&
            ChooseFileDropEffect(prepared.sources, target.wstring(), target_id, DROPEFFECT_COPY, {}) == DROPEFFECT_COPY,
            "drag_effect_respects_source_capabilities_and_explicit_modifier_intent");
        passed &= Check(!ChooseFileDropEffect(prepared.sources, source.wstring(), origin.identity, both, {}) &&
            ChooseFileDropEffect(prepared.sources, source.wstring(), origin.identity, both, { true }) == DROPEFFECT_COPY,
            "drag_drop_back_on_source_disallows_noop_move_but_allows_explicit_copy");
        passed &= Check(!ChooseFileDropEffect({}, target.wstring(), target_id, both, {}) &&
            !ChooseFileDropEffect(prepared.sources, target.wstring(), {}, both, {}), "drag_missing_sources_or_target_identity_rejects_drop");
        blink::core::FileIdentity read_target;
        passed &= Check(SUCCEEDED(ReadFileDropTarget(target.wstring(), read_target)) && read_target == target_id &&
            FAILED(ReadFileDropTarget(paths[0], read_target)) && !read_target.valid,
            "drag_target_metadata_requires_actual_available_folder_and_clears_failed_output");
        {
            const auto folder = source / L"目录"; fs::create_directory(folder); std::ofstream(folder / L"nested.txt") << "nested";
            const std::vector<blink::core::FileEntry> mixed_entries{ entries[0], Entry(folder) };
            FileDragSelection mixed, directory;
            const auto mixed_hr = CaptureFileDragSelection(origin, mixed_entries, mixed);
            const auto directory_hr = CaptureFileDragSelection(origin, std::span(mixed_entries).subspan(1), directory);
            passed &= Check(!mixed_hr && std::get<CopyItemRequests>(mixed.expected).size() == 2 &&
                !directory_hr && std::holds_alternative<DirectoryCopyRequest>(directory.expected),
                "drag_source_capture_retains_mixed_and_single_directory_types");
            passed &= Check(!ChooseFileDropEffect(directory.expected, folder.wstring(), Entry(folder).identity, both, {}) &&
                !ChooseFileDropEffect(directory.expected, (folder / L"child").wstring(), target_id, both, { true }),
                "drag_directory_cannot_be_dropped_into_itself_or_a_descendant");
            FileDragData mixed_data; const auto mixed_created = CreateFileDragData(mixed.paths, mixed.token, mixed_data);
            FileDragDataReference mixed_reference; const auto mixed_captured = mixed_reference.Capture(mixed_data.object.get());
            const auto mixed_prepared = PrepareFileDrag(mixed_reference, mixed);
            FileOperationCoordinator operation;
            const auto id = operation.CopyItems(CopyItems(mixed_prepared.sources, target)); const bool done = Finished(operation.CurrentAction());
            const auto outcome = CompletedFileDrop(operation.Snapshot());
            passed &= Check(SUCCEEDED(mixed_created) && SUCCEEDED(mixed_captured) && SUCCEEDED(mixed_prepared.error) && id && done && outcome &&
                SUCCEEDED(outcome->error) && outcome->logical_effect == DROPEFFECT_COPY && Read(target / entries[0].name) == "payload-0" &&
                Read(target / L"目录" / L"nested.txt") == "nested" && Read(folder / L"nested.txt") == "nested",
                "drag_mixed_drop_uses_real_serial_copy_coordinator_and_preserves_sources");
            auto lease = IncomingFileDrag(mixed_reference, false); const auto begun = lease.Begin(); const auto ended = lease.Finish(outcome ? outcome->error : E_FAIL, outcome ? outcome->logical_effect : 0);
            passed &= Check(SUCCEEDED(begun) && lease.Asynchronous() && SUCCEEDED(ended) && mixed_data.feedback->performed == DROPEFFECT_COPY &&
                mixed_data.feedback->logical == DROPEFFECT_COPY && !mixed_data.feedback->in_operation,
                "drag_completed_copy_ends_async_source_with_copy_feedback");
        }
        {
            const auto move_source = root / L"移动来源"; const auto move_target = root / L"移动目标";
            fs::create_directory(move_source); fs::create_directory(move_target); std::ofstream(move_source / L"move.txt") << "move";
            const std::vector<std::wstring> names{ (move_source / L"move.txt").wstring() };
            auto external = Microsoft::WRL::Make<ExternalFiles>(); external->bytes = EncodeClipboardFilePaths(names);
            FileDragDataReference ref; const auto captured = ref.Capture(static_cast<IDataObject*>(external.Get())); const auto pending = PrepareFileDrag(ref, {});
            std::vector<MoveRequest> requests; const auto bound = PrepareMoveDestination(pending.sources, move_target.wstring(), Entry(move_target).identity, requests);
            IncomingFileDrag lease(ref, true); const auto begun = lease.Begin();
            FileOperationCoordinator operation; const auto id = operation.MoveEntries(std::move(requests)); const bool done = Finished(operation.CurrentAction());
            const auto outcome = CompletedFileDrop(operation.Snapshot()); const auto ended = lease.Finish(outcome ? outcome->error : E_FAIL, outcome ? outcome->logical_effect : 0);
            passed &= Check(SUCCEEDED(captured) && SUCCEEDED(pending.error) && pending.payload.token.empty() && !bound && SUCCEEDED(begun) &&
                !lease.Asynchronous() && id && done && outcome && outcome->logical_effect == DROPEFFECT_MOVE && SUCCEEDED(ended) &&
                !fs::exists(move_source / L"move.txt") && Read(move_target / L"move.txt") == "move",
                "drag_external_nonasync_source_moves_through_real_coordinator");
            passed &= Check(external->performed == DROPEFFECT_NONE && external->logical == DROPEFFECT_MOVE && external->notifications == 2 &&
                lease.Finish(S_OK, DROPEFFECT_MOVE) == S_FALSE && external->notifications == 2,
                "drag_optimized_move_reports_no_source_delete_and_exactly_once_completion");
            external->bytes.resize(4);
            passed &= Check(FAILED(ReadFileDragData(external.Get()).error), "drag_malformed_external_hdrop_is_rejected_without_native_io");
        }
        {
            FileDragData retained; const auto made = CreateFileDragData(paths, L"async-feedback", retained);
            FileDragDataReference ref; const auto captured = ref.Capture(retained.object.get()); IncomingFileDrag lease(ref, true);
            const auto begun = lease.Begin(); const auto repeated = lease.Begin(); const bool active = retained.feedback->in_operation;
            const auto ended = lease.Finish(S_OK, DROPEFFECT_MOVE);
            passed &= Check(SUCCEEDED(made) && SUCCEEDED(captured) && SUCCEEDED(begun) && repeated == S_FALSE && active &&
                lease.Asynchronous() && SUCCEEDED(ended) && retained.feedback->logical == DROPEFFECT_MOVE &&
                retained.feedback->performed == DROPEFFECT_NONE && !retained.feedback->in_operation && Read(paths[0]) == "payload-0",
                "drag_async_move_feedback_never_deletes_original_source_and_begin_is_idempotent");
            IncomingFileDrag abandoned(ref, true); const auto abandoned_result = abandoned.Finish(E_ABORT, DROPEFFECT_NONE);
            passed &= Check(SUCCEEDED(abandoned_result) && abandoned.Begin() == E_ABORT && abandoned.Finish(S_OK, DROPEFFECT_MOVE) == S_FALSE &&
                !retained.feedback->in_operation && retained.feedback->logical == DROPEFFECT_NONE && Read(paths[0]) == "payload-0",
                "drag_cancel_before_async_begin_prevents_late_start_and_false_move_feedback");
            IncomingFileDrag cancelled(ref, true); const auto cancelled_begin = cancelled.Begin();
            const auto cancelled_end = cancelled.Finish(E_ABORT, DROPEFFECT_MOVE);
            passed &= Check(SUCCEEDED(cancelled_begin) && SUCCEEDED(cancelled_end) && retained.feedback->error == E_ABORT &&
                !retained.feedback->in_operation && retained.feedback->logical == DROPEFFECT_NONE,
                "drag_async_cancel_ends_source_operation_with_no_claimed_transfer");
        }
        {
            FileOperationSnapshot value; value.kind = FileOperationKind::Copy; value.stage = FileOperationStage::Failed;
            value.copy_batch.emplace(); value.copy_batch->resumable = true; value.result.error = ERROR_FILE_EXISTS;
            passed &= Check(!CompletedFileDrop(value), "drag_resumable_conflict_keeps_source_operation_open_for_user_choice");
            value.stage = FileOperationStage::Completed; value.copy_batch->resumable = false; value.copy_batch->copied_count = 2;
            const auto copied = CompletedFileDrop(value);
            passed &= Check(copied && SUCCEEDED(copied->error) && copied->logical_effect == DROPEFFECT_COPY,
                "drag_terminal_copy_uses_actual_queue_commit_counts");
            value.copy_batch->copied_count = 0; value.stage = FileOperationStage::Skipped;
            const auto skipped = CompletedFileDrop(value);
            passed &= Check(skipped && SUCCEEDED(skipped->error) && !skipped->logical_effect,
                "drag_all_skipped_items_report_no_logical_transfer");
            value.stage = FileOperationStage::Cancelled; value.copy_batch->copied_count = 1;
            const auto cancelled = CompletedFileDrop(value);
            passed &= Check(cancelled && cancelled->error == E_ABORT && !cancelled->logical_effect,
                "drag_cancelled_partial_queue_does_not_report_full_success");
            value.kind = FileOperationKind::Rename;
            passed &= Check(!CompletedFileDrop(value), "drag_other_operation_kinds_cannot_complete_transfer_feedback");
        }
        {
            TabRecord tab; tab.id = 7; tab.file_operation_generation = 3; tab.state = std::make_unique<TabState>();
            const auto navigation = tab.state->Navigate(origin.location);
            if (navigation) { (void)tab.state->CommitBrowse(*navigation, blink::core::EnumerateDirectory(source.wstring())); }
            const auto child = FileDropOrigin(CaptureFileOperationOrigin(tab), Location::Folder(target.wstring()), target_id);
            passed &= Check(IsFileOperationOriginCurrent(child, &tab) && child.location != origin.location && child.owner_location == origin.location,
                "drag_child_or_sidebar_target_preserves_owning_page_for_conflict_and_retry");
            ++tab.file_operation_generation;
            passed &= Check(!IsFileOperationOriginCurrent(child, &tab), "drag_navigation_generation_invalidates_old_child_target");
            const auto request = tab.state->Navigate(Location::ThisPC());
            if (request) { (void)tab.state->CommitThisPC(*request, {}); }
            const auto drive = FileDropOrigin(CaptureFileOperationOrigin(tab), Location::Folder(target.wstring()), target_id);
            passed &= Check(IsFileOperationOriginCurrent(drive, &tab) && !IsFileOperationOriginCurrent(CaptureFileOperationOrigin(tab), &tab),
                "drag_thispc_page_can_own_drive_drop_without_becoming_a_filesystem_target_itself");
            tab.search_visible = true;
            passed &= Check(!IsFileOperationOriginCurrent(drive, &tab), "drag_search_results_remain_unavailable_as_drop_targets");
        }
        const Localization text;
        passed &= Check(text.Format(L"DragDrop.CopyTo", L"D:\\a{1}") == L"Copy to D:\\a{1}" &&
            text.GetString(L"DragDrop.Preparing") != L"DragDrop.Preparing", "drag_localized_captions_preserve_literal_path_placeholders");
        std::cout << "INFO drag_clipboard_sequence before=" << sequence << " after=" << GetClipboardSequenceNumber() << '\n';
        passed &= Check(Read(paths[0]) == "payload-0" && Read(paths[39]) == "payload-39",
            "drag_publication_preparation_and_feedback_preserve_all_untouched_sources");
    }
    catch (const winrt::hresult_error& error)
    { std::cout << "FAIL drag_unexpected_hresult " << std::hex << static_cast<unsigned>(error.code()) << std::dec << '\n'; passed = false; }
    catch (const std::exception& error) { std::cout << "FAIL drag_unexpected_exception " << error.what() << '\n'; passed = false; }
    catch (...) { passed &= Check(false, "drag_unexpected_exception"); }
    return passed;
}
