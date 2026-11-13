#include "ClipboardPasteControllerTests.h"
#include "Blink/App/ClipboardPasteController.h"
#include <wrl.h>
#include <cstring>
#include <iostream>
#include <mutex>

namespace
{
    using namespace blink::app;
    bool Check(bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    blink::core::FileIdentity Identity(unsigned char byte)
    { blink::core::FileIdentity id; id.valid = true; id.volume_serial = 17; id.file_id[0] = byte; return id; }
    FileCopyRequest Request(const wchar_t* name, unsigned char byte, bool directory = false)
    {
        FileCopyRequest request; request.source_parent = L"D:\\source"; request.source_parent_identity = Identity(1);
        request.source.name = name; request.source.identity = Identity(byte); request.source.is_directory = directory;
        request.source.attributes = directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        request.destination_parent = L"D:\\target"; request.destination_parent_identity = Identity(99);
        request.destination_name = name; return request;
    }
    struct Stats
    {
        HANDLE done{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        std::atomic<unsigned> calls{}, pasted{};
        std::atomic<DWORD> performed{}, logical{}, paste_effect{};
        std::mutex mutex;
        ClipboardPasteSessionReport report;
        ~Stats() { if (done) { CloseHandle(done); } }
    };
    class Source final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDataObject>
    {
    public:
        Source(std::vector<std::wstring> paths, ClipboardTransferIntent intent, std::shared_ptr<Stats> stats)
            : bytes_(EncodeClipboardFilePaths(paths)), effect_(ClipboardDropEffectValue(intent)), stats_(std::move(stats)) {}
        IFACEMETHODIMP GetData(FORMATETC* format, STGMEDIUM* medium) override
        {
            if (!format || !medium) { return E_POINTER; }
            *medium = {};
            const bool preferred = format->cfFormat == RegisterClipboardFormatW(L"Preferred DropEffect");
            if (format->cfFormat != CF_HDROP && !preferred) { return DV_E_FORMATETC; }
            if (!(format->tymed & TYMED_HGLOBAL)) { return DV_E_TYMED; }
            const void* data = preferred ? static_cast<const void*>(&effect_) : bytes_.data();
            const auto size = preferred ? sizeof(effect_) : bytes_.size();
            auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
            if (!memory) { return E_OUTOFMEMORY; }
            auto target = GlobalLock(memory);
            if (!target) { GlobalFree(memory); return E_OUTOFMEMORY; }
            std::memcpy(target, data, size); GlobalUnlock(memory);
            medium->tymed = TYMED_HGLOBAL; medium->hGlobal = memory; return S_OK;
        }
        IFACEMETHODIMP QueryGetData(FORMATETC* format) override
        { return format && (format->cfFormat == CF_HDROP || format->cfFormat == RegisterClipboardFormatW(L"Preferred DropEffect")) ? S_OK : DV_E_FORMATETC; }
        IFACEMETHODIMP SetData(FORMATETC* format, STGMEDIUM* medium, BOOL release) override
        {
            if (!format || !medium || medium->tymed != TYMED_HGLOBAL || GlobalSize(medium->hGlobal) < sizeof(DWORD)) { return E_INVALIDARG; }
            auto data = GlobalLock(medium->hGlobal); if (!data) { return E_FAIL; }
            DWORD effect{}; std::memcpy(&effect, data, sizeof(effect)); GlobalUnlock(medium->hGlobal);
            ++stats_->calls;
            if (format->cfFormat == RegisterClipboardFormatW(L"Performed DropEffect")) { stats_->performed = effect; }
            if (format->cfFormat == RegisterClipboardFormatW(L"Logical Performed DropEffect")) { stats_->logical = effect; }
            if (format->cfFormat == RegisterClipboardFormatW(L"Paste Succeeded")) { ++stats_->pasted; stats_->paste_effect = effect; }
            if (release) { ReleaseStgMedium(medium); }
            return S_OK;
        }
        IFACEMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        IFACEMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC* result) override { if (result) { result->ptd = nullptr; } return E_NOTIMPL; }
        IFACEMETHODIMP EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
        IFACEMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
        IFACEMETHODIMP DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
        IFACEMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
    private:
        std::vector<std::byte> bytes_;
        DWORD effect_{};
        std::shared_ptr<Stats> stats_;
    };
    struct Captured
    {
        std::shared_ptr<Stats> stats{ std::make_shared<Stats>() };
        ClipboardPasteCapture value;
        Captured(const std::vector<FileCopyRequest>& requests, ClipboardTransferIntent intent)
        {
            std::vector<std::wstring> paths;
            for (const auto& request : requests) { paths.push_back(request.source_parent + L"\\" + request.source.name); }
            const auto target = stats;
            ClipboardPasteSessionApis apis;
            apis.sequence = [] { return 71u; };
            apis.acquire = [paths, intent, target](IDataObject** output) -> HRESULT {
                auto object = Microsoft::WRL::Make<Source>(paths, intent, target);
                if (!object) { return E_OUTOFMEMORY; }
                return object.CopyTo(output);
            };
            value = CaptureClipboardPaste({}, [target](ClipboardPasteSessionReport report) {
                { const std::scoped_lock lock(target->mutex); target->report = std::move(report); }
                SetEvent(target->done);
            }, std::move(apis));
        }
        bool Wait() const { return stats->done && WaitForSingleObject(stats->done, 5000) == WAIT_OBJECT_0; }
        bool Full(DWORD effect) const { return Wait() && stats->calls == 3 && stats->pasted == 1 && stats->paste_effect == effect; }
        bool NoFeedback() const { return Wait() && stats->calls == 0 && stats->pasted == 0; }
    };
    FileOperationSnapshot MoveSnapshot(const std::vector<FileCopyRequest>& files, std::uint64_t id)
    {
        FileOperationSnapshot value; value.operation_id = id; value.kind = FileOperationKind::Move;
        value.stage = FileOperationStage::Running; value.result.error = ERROR_SUCCESS;
        auto& batch = value.move_batch.emplace(); batch.requested_count = files.size();
        for (const auto& file : files) { MoveBatchItem item; item.request = file; batch.items.push_back(std::move(item)); }
        return value;
    }
    void Moved(MoveBatchItem& item)
    { item.stage = MoveBatchItemStage::Moved; item.result.error = ERROR_SUCCESS; item.result.moved = true; }
    void CompleteMove(FileOperationSnapshot& value)
    { for (auto& item : value.move_batch->items) { Moved(item); } value.move_batch->resumable = false; value.stage = FileOperationStage::Completed; value.result.error = ERROR_SUCCESS; }
    FileOperationSnapshot DirectorySnapshot(const FileCopyRequest& directory, std::uint64_t id, bool conflict)
    {
        FileOperationSnapshot value; value.operation_id = id; value.kind = FileOperationKind::DirectoryCopy;
        value.directory_copy_request = DirectoryCopyProjection(directory); auto& result = value.directory_copy_result.emplace();
        value.stage = conflict ? FileOperationStage::Failed : FileOperationStage::Completed;
        result.error = value.result.error = conflict ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS;
        if (!conflict) { result.copied = true; result.files_copied = 1; result.directories_created = 1; }
        return value;
    }
}

bool RunClipboardPasteControllerTests()
{
    bool passed = true;
    const auto first = Request(L"first.txt", 2), second = Request(L"second.txt", 3), directory = Request(L"目录", 4, true);
    const std::vector<FileCopyRequest> files{ first, second };
    {
        Captured capture(files, ClipboardTransferIntent::Move); ClipboardPasteController controller;
        const auto context = std::make_shared<ClipboardPasteFeedbackContext>();
        controller.Track(ClipboardCopySources{ files }, capture.value.payload, 10, capture.value.session, context);
        passed &= Check(SUCCEEDED(capture.value.error) && controller.PendingCount() == 1 && context->operation_id == 10,
            "clipboard_paste_controller_tracks_original_object_and_actual_operation");
        auto value = MoveSnapshot(files, 10); value.move_progress.copy_progress = { 100, 100 };
        controller.Observe(value);
        passed &= Check(controller.Pending(10), "clipboard_paste_controller_progress_does_not_finish_original_object");
        auto unrelated = value; unrelated.operation_id = 99; CompleteMove(unrelated); controller.Observe(unrelated);
        passed &= Check(controller.Pending(10), "clipboard_paste_controller_unrelated_terminal_does_not_finish_this_receipt");
        Moved(value.move_batch->items[0]); controller.Observe(value);
        passed &= Check(controller.Pending(10), "clipboard_paste_controller_completed_prefix_is_not_whole_move_success");
        value.stage = FileOperationStage::Failed; value.result.error = ERROR_ALREADY_EXISTS;
        value.move_batch->resumable = true; controller.Observe(value);
        passed &= Check(controller.Pending(10), "clipboard_paste_controller_resumable_conflict_keeps_original_object");
        controller.Advance(10, 11); value.operation_id = 11;
        passed &= Check(!controller.Pending(10) && controller.Pending(11) && context->operation_id == 11,
            "clipboard_paste_controller_resume_advances_receipt_and_ui_warning_owner");
        auto old = value; old.operation_id = 10; CompleteMove(old); controller.Observe(old);
        passed &= Check(controller.Pending(11), "clipboard_paste_controller_late_old_attempt_cannot_finish_resumed_task");
        CompleteMove(value); controller.Observe(value);
        passed &= Check(!controller.PendingCount() && capture.Full(DROPEFFECT_MOVE) && capture.stats->performed == DROPEFFECT_NONE &&
            capture.stats->logical == DROPEFFECT_MOVE, "clipboard_paste_controller_full_optimized_move_notifies_original_without_source_delete");
        controller.Observe(value);
        passed &= Check(capture.stats->calls == 3 && capture.stats->pasted == 1,
            "clipboard_paste_controller_duplicate_terminal_cannot_repeat_completion");
    }
    {
        Captured old({ first }, ClipboardTransferIntent::Move), next({ second }, ClipboardTransferIntent::Move);
        ClipboardPasteController controller; controller.Track(ClipboardCopySources{ std::vector<FileCopyRequest>{ first } }, old.value.payload, 20, old.value.session);
        controller.Track(ClipboardCopySources{ std::vector<FileCopyRequest>{ second } }, next.value.payload, 21, next.value.session);
        auto value = MoveSnapshot({ first }, 20); CompleteMove(value); controller.Observe(value);
        passed &= Check(old.Full(DROPEFFECT_MOVE) && controller.Pending(21) && controller.PendingCount() == 1,
            "clipboard_paste_controller_new_task_does_not_discard_late_original_terminal");
        value = MoveSnapshot({ second }, 21); CompleteMove(value); controller.Observe(value);
        passed &= Check(next.Full(DROPEFFECT_MOVE) && !controller.PendingCount(),
            "clipboard_paste_controller_each_task_replies_only_to_its_original_object");
    }
    {
        Captured capture({ directory }, ClipboardTransferIntent::Copy); ClipboardPasteController controller;
        const ClipboardCopySources source = DirectoryCopyProjection(directory);
        controller.Track(source, capture.value.payload, 30, capture.value.session);
        auto value = DirectorySnapshot(directory, 30, true); controller.Observe(value);
        passed &= Check(controller.Pending(30), "clipboard_paste_controller_single_directory_root_conflict_waits_for_ui_decision");
        controller.Advance(30, 31); value = DirectorySnapshot(directory, 31, false); controller.Observe(value);
        passed &= Check(capture.Full(DROPEFFECT_COPY) && !controller.PendingCount(),
            "clipboard_paste_controller_single_directory_retry_preserves_original_completion_object");
    }
    {
        Captured capture({ directory }, ClipboardTransferIntent::Copy); ClipboardPasteController controller;
        controller.Track(ClipboardCopySources{ DirectoryCopyProjection(directory) }, capture.value.payload, 32, capture.value.session);
        controller.Observe(DirectorySnapshot(directory, 32, true)); controller.CancelDecisions();
        passed &= Check(!controller.PendingCount() && capture.NoFeedback(),
            "clipboard_paste_controller_departure_cancels_directory_decision_without_success_feedback");
    }
    {
        Captured capture({ directory }, ClipboardTransferIntent::Copy); ClipboardPasteController controller;
        controller.Track(ClipboardCopySources{ DirectoryCopyProjection(directory) }, capture.value.payload, 33, capture.value.session);
        auto value = DirectorySnapshot(directory, 33, false); value.stage = FileOperationStage::Failed;
        value.result.error = value.directory_copy_result->error = ERROR_DISK_FULL;
        value.directory_copy_result->copied = false; value.directory_copy_result->partial_tree_left = true;
        controller.Observe(value);
        passed &= Check(!controller.PendingCount() && capture.NoFeedback(),
            "clipboard_paste_controller_partial_single_directory_never_reports_whole_paste_success");
    }
    {
        Captured capture({ directory }, ClipboardTransferIntent::Copy); ClipboardPasteController controller;
        controller.Track(ClipboardCopySources{ DirectoryCopyProjection(directory) }, capture.value.payload, 34, capture.value.session);
        controller.CancelDecisions(); // Navigation can precede the queued root-conflict event.
        const bool running_preserved = controller.Pending(34);
        controller.Observe(DirectorySnapshot(directory, 34, true)); controller.CancelDecision(34);
        passed &= Check(running_preserved && !controller.PendingCount() && capture.NoFeedback(),
            "clipboard_paste_controller_late_root_conflict_is_cancelled_when_ui_origin_has_departed");
    }
    {
        Captured capture({ first }, ClipboardTransferIntent::Move); ClipboardPasteController controller;
        controller.Track(ClipboardCopySources{ std::vector<FileCopyRequest>{ first } }, capture.value.payload, 40, capture.value.session);
        auto value = MoveSnapshot({ first }, 40); value.stage = FileOperationStage::Completed;
        value.move_batch->items[0].stage = MoveBatchItemStage::Unchanged; value.move_batch->items[0].result.unchanged = true;
        controller.Observe(value);
        passed &= Check(capture.NoFeedback(), "clipboard_paste_controller_unchanged_source_is_not_a_completed_paste");
    }
    {
        Captured capture({ first }, ClipboardTransferIntent::Move); ClipboardPasteController controller;
        controller.Track(ClipboardCopySources{ std::vector<FileCopyRequest>{ first } }, capture.value.payload, 41, capture.value.session);
        auto value = MoveSnapshot({ first }, 41); value.stage = FileOperationStage::Failed; value.result.error = ERROR_ACCESS_DENIED;
        auto& item = value.move_batch->items[0]; item.stage = MoveBatchItemStage::Failed;
        item.result.copy_result.emplace(); item.result.copy_result->copied = true; item.result.source_items_remaining = 1;
        controller.Observe(value);
        passed &= Check(capture.NoFeedback(), "clipboard_paste_controller_cross_volume_copy_with_source_remaining_is_not_move_success");
    }
    {
        Captured capture({ first }, ClipboardTransferIntent::Move); ClipboardPasteController controller;
        controller.Track(ClipboardCopySources{ std::vector<FileCopyRequest>{ first } }, capture.value.payload, 42, capture.value.session);
        auto value = MoveSnapshot({ first }, 42); CompleteMove(value); value.move_batch->items[0].result.post_commit_error = ERROR_ACCESS_DENIED;
        controller.Observe(value);
        passed &= Check(capture.Full(DROPEFFECT_MOVE), "clipboard_paste_controller_postcommit_warning_preserves_actual_full_completion");
    }
    {
        Captured capture({ first }, ClipboardTransferIntent::Move); ClipboardPasteController controller;
        controller.Track(ClipboardCopySources{ std::vector<FileCopyRequest>{ first } }, capture.value.payload, 43, capture.value.session);
        auto value = MoveSnapshot({ first }, 43); CompleteMove(value); value.stage = FileOperationStage::Cancelled;
        value.cancel_requested = true; value.result.error = ERROR_CANCELLED; controller.Observe(value);
        passed &= Check(capture.Full(DROPEFFECT_MOVE), "clipboard_paste_controller_late_cancel_does_not_erase_all_committed_items");
    }
    {
        Captured capture({ first }, ClipboardTransferIntent::Move); ClipboardPasteController controller;
        controller.Track(ClipboardCopySources{ std::vector<FileCopyRequest>{ first } }, capture.value.payload, 44, capture.value.session);
        auto value = MoveSnapshot({ first }, 44); value.stage = FileOperationStage::Cancelled; value.result.error = ERROR_CANCELLED;
        value.move_batch->items[0].stage = MoveBatchItemStage::Cancelled; controller.Observe(value);
        passed &= Check(capture.NoFeedback() && !controller.PendingCount(),
            "clipboard_paste_controller_cancel_before_commit_releases_without_paste_succeeded");
    }
    {
        Captured capture({ first }, ClipboardTransferIntent::Move); ClipboardPasteController controller;
        controller.Track(ClipboardCopySources{ std::vector<FileCopyRequest>{ first } }, capture.value.payload, 45, capture.value.session);
        controller.Close();
        passed &= Check(!controller.PendingCount() && capture.NoFeedback(),
            "clipboard_paste_controller_close_cancels_metadata_without_waiting_on_ui");
        Captured after({ first }, ClipboardTransferIntent::Move);
        controller.Track(ClipboardCopySources{ std::vector<FileCopyRequest>{ first } }, after.value.payload, 46, std::move(after.value.session));
        passed &= Check(!controller.PendingCount() && after.NoFeedback(),
            "clipboard_paste_controller_closed_owner_does_not_adopt_another_object");
    }
    return passed;
}
