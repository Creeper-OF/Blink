#include "DeleteOperationTests.h"
#include "Blink/App/DeletePresentation.h"
#include "Blink/App/FileOperationRelocation.h"
#include "Blink/App/FolderCommandPolicy.h"
#include "Blink/App/TabWindowState.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <wrl.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

bool CreateOperationTestJunction(const std::filesystem::path&, const std::filesystem::path&);
namespace
{
    using namespace blink::app;
    using Microsoft::WRL::ComPtr;
    namespace fs = std::filesystem;
    bool Check(bool value, const char* name) { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    fs::path Extended(const fs::path& value) { const auto path = value.wstring(); return path.starts_with(L"\\\\?\\") ? value : fs::path(L"\\\\?\\" + path); }
    bool Exists(const fs::path& value) { return GetFileAttributesW(Extended(value).c_str()) != INVALID_FILE_ATTRIBUTES; }
    void Write(const fs::path& path, const std::string& data) { std::ofstream file(Extended(path), std::ios::binary); file << data; if (!file) { throw std::runtime_error("delete fixture write"); } }
    std::string Read(const fs::path& path) { std::ifstream file(Extended(path), std::ios::binary); return { std::istreambuf_iterator<char>(file), {} }; }
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry entry; if (!blink::core::ReadEntryMetadata(Extended(path).wstring(), entry)) { throw std::runtime_error("delete fixture metadata"); }
        entry.name = path.filename().wstring(); entry.full_path = path.wstring(); return entry;
    }
    blink::core::FileIdentity Identity(unsigned char value)
    { blink::core::FileIdentity id; id.valid = true; id.volume_serial = 17; id.file_id[0] = value; return id; }
    DeleteRequest Fake(const wchar_t* name, unsigned char value)
    {
        DeleteRequest request; request.parent = L"D:\\delete-owned"; request.parent_identity = Identity(1);
        request.source.name = name; request.source.identity = Identity(value); request.source.attributes = FILE_ATTRIBUTE_NORMAL; return request;
    }
    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; } if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        auto done = std::make_shared<std::promise<Status>>(); auto future = done->get_future();
        action.Completed([done](auto&&, Status status) { done->set_value(status); });
        return future.wait_for(std::chrono::seconds(12)) == std::future_status::ready && future.get() == Status::Completed;
    }
    bool Arm(FolderWatcher& watcher, const fs::path& path)
    {
        auto done = std::make_shared<std::promise<bool>>(); auto future = done->get_future(); auto reported = std::make_shared<std::atomic_bool>();
        watcher.Start(path.wstring(), [done, reported](const FolderWatchEvent& event) {
            if ((event.kind == FolderWatchEventKind::Ready || event.kind == FolderWatchEventKind::Unavailable) && !reported->exchange(true))
            { done->set_value(event.kind == FolderWatchEventKind::Ready); }
        });
        return future.wait_for(std::chrono::seconds(8)) == std::future_status::ready && future.get();
    }
    winrt::Windows::Foundation::IAsyncAction FailedPreparation()
    { co_await winrt::resume_background(); winrt::throw_hresult(E_FAIL); }
    template<typename Function> void Sta(Function body)
    {
        std::exception_ptr failure;
        std::jthread work([&] {
            const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (FAILED(initialized)) { failure = std::make_exception_ptr(std::runtime_error("STA initialization")); return; }
            try { body(); } catch (...) { failure = std::current_exception(); }
            CoUninitialize();
        });
        work.join(); if (failure) { std::rethrow_exception(failure); }
    }
    bool Restore(const DeleteResult& result, const fs::path& path)
    {
        if (!result.recycled || result.recycle_item.empty()) { return false; }
        bool restored{};
        Sta([&] {
            constexpr DWORD flags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NO_CONNECTED_ELEMENTS | FOFX_EARLYFAILURE | FOFX_NOCOPYHOOKS;
            ComPtr<IShellItem> recycled, parent; ComPtr<IFileOperation> operation;
            auto error = SHCreateItemFromParsingName(result.recycle_item.c_str(), nullptr, IID_PPV_ARGS(&recycled));
            if (SUCCEEDED(error)) { error = SHCreateItemFromParsingName(path.parent_path().c_str(), nullptr, IID_PPV_ARGS(&parent)); }
            if (SUCCEEDED(error)) { error = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&operation)); }
            if (SUCCEEDED(error)) { error = operation->SetOperationFlags(flags); }
            if (SUCCEEDED(error)) { error = operation->MoveItem(recycled.Get(), parent.Get(), path.filename().c_str(), nullptr); }
            if (SUCCEEDED(error)) { error = operation->PerformOperations(); }
            BOOL aborted{};
            if (SUCCEEDED(error)) { error = operation->GetAnyOperationsAborted(&aborted); }
            restored = SUCCEEDED(error) && !aborted && Exists(path);
            if (!restored && recycled)
            {
                // This exact Shell parsing name came from this fixture's
                // deletion. Never enumerate/empty the user's Recycle Bin.
                ComPtr<IFileOperation> cleanup;
                auto removed = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&cleanup));
                if (SUCCEEDED(removed)) { removed = cleanup->SetOperationFlags(flags); }
                if (SUCCEEDED(removed)) { removed = cleanup->DeleteItem(recycled.Get(), nullptr); }
                if (SUCCEEDED(removed)) { removed = cleanup->PerformOperations(); }
                std::cout << "INFO delete_owned_recycle_cleanup hr=" << static_cast<long>(removed) << '\n';
            }
        });
        return restored;
    }
    void Cleanup(const fs::path& root) noexcept
    {
        WIN32_FIND_DATAW entry{}; const auto pattern = Extended(root) / L"*"; HANDLE find = FindFirstFileW(pattern.c_str(), &entry);
        if (find == INVALID_HANDLE_VALUE) { return; }
        do
        {
            if (!std::wcscmp(entry.cFileName, L".") || !std::wcscmp(entry.cFileName, L"..")) { continue; }
            const auto path = Extended(root) / entry.cFileName;
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { Cleanup(path); }
                SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); RemoveDirectoryW(path.c_str());
            }
            else { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(path.c_str()); }
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    struct Fixture
    {
        fs::path parent, source;
        bool directory{};
        DeleteRequest request;
        Fixture(const fs::path& root, const wchar_t* name, bool is_directory = false)
            : parent(root / name), source(parent / (is_directory ? L"来源目录" : L"来源.txt")), directory(is_directory)
        {
            fs::create_directories(Extended(parent));
            if (directory) { fs::create_directories(Extended(source / L"sub" / L"empty")); }
            const auto data = Data(); Write(data, "owned-delete-data"); Write(data.wstring() + L":Zone.Identifier", "owned-zone");
            request = { parent.wstring(), Entry(parent).identity, Entry(source) };
        }
        fs::path Data() const { return directory ? source / L"sub" / L"data.txt" : source; }
        bool Preserved() const { return Exists(source) && Read(Data()) == "owned-delete-data" && Read(Data().wstring() + L":Zone.Identifier") == "owned-zone"; }
    };
    DeleteResult Native(const DeleteRequest& request, const FileOperationCancellation& cancelled = {}, const DeleteProgressCallback& progress = {})
    { DeleteResult result; Sta([&] { result = DeleteEntrySafely(request, cancelled, progress); }); return result; }
    DeleteResult Removed(const DeleteRequest& request)
    { DeleteResult result; result.error = S_OK; result.removed = true; result.recycled = request.mode == DeleteMode::Recycle; return result; }
}

bool RunDeleteOperationTests()
{
    bool passed = true;
    const auto root = fs::absolute(fs::path(L".build") / L"delete-operation-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    fs::create_directories(root);
    struct Final { fs::path root; ~Final() { Cleanup(root); RemoveDirectoryW(Extended(root).c_str()); } } final{ root };
    try
    {
        for (bool directory : { false, true })
        {
            Fixture fixture(root, directory ? L"recycle-directory" : L"recycle-file", directory);
            const auto result = Native(fixture.request); const bool removed = !Exists(fixture.source);
            const bool restored = Restore(result, fixture.source);
            passed &= Check(SUCCEEDED(result.error) && result.removed && result.recycled && !result.possible_partial && removed &&
                restored && fixture.Preserved(), directory ? "delete_recycle_real_nonempty_folder_and_ads_restore_from_system_bin" :
                    "delete_recycle_real_file_and_ads_restore_from_system_bin");
        }
        for (bool directory : { false, true })
        {
            Fixture fixture(root, directory ? L"permanent-directory" : L"permanent-file", directory);
            fixture.request.mode = DeleteMode::Permanent;
            const auto unconfirmed = Native(fixture.request);
            passed &= Check(unconfirmed.error == E_ACCESSDENIED && !unconfirmed.removed && fixture.Preserved(), directory ?
                "delete_permanent_folder_requires_confirmation_before_filesystem_work" : "delete_permanent_file_requires_confirmation_before_filesystem_work");
            fixture.request.permanent_confirmed = true; const auto result = Native(fixture.request);
            passed &= Check(SUCCEEDED(result.error) && result.removed && !result.recycled && result.recycle_item.empty() && !Exists(fixture.source), directory ?
                "delete_confirmed_permanent_nonempty_directory_has_no_recycle_item" : "delete_confirmed_permanent_file_has_no_recycle_item");
        }
        {
            Fixture fixture(root, L"permanent-empty-directory", true); Cleanup(fixture.source);
            fixture.request.mode = DeleteMode::Permanent; fixture.request.permanent_confirmed = true;
            const auto result = Native(fixture.request);
            passed &= Check(result.removed && SUCCEEDED(result.error) && !result.recycled && !Exists(fixture.source),
                "delete_confirmed_empty_directory_requires_actual_final_root_removal");
        }
        {
            Fixture fixture(root, L"permanent-cancel-children", true);
            for (unsigned index = 0; index < 8; ++index) { Write(fixture.source / (L"item-" + std::to_wstring(index)), "child"); }
            fixture.request.mode = DeleteMode::Permanent; fixture.request.permanent_confirmed = true;
            const auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = Native(fixture.request, cancel, [cancel](const DeleteProgress& value) {
                if (value.observed_removed) { cancel->store(true); }
            });
            passed &= Check(DeleteCancelled(result.error) && !result.removed && result.possible_partial && result.observed_removed && Exists(fixture.source),
                "delete_child_cancel_does_not_treat_early_folder_post_callback_as_root_commit");
        }
        {
            Fixture fixture(root, L"mta"); const auto result = DeleteEntrySafely(fixture.request);
            passed &= Check(result.error == RPC_E_WRONG_THREAD && !result.removed && fixture.Preserved(), "delete_shell_backend_rejects_mta_without_filesystem_mutation");
        }
        {
            Fixture fixture(root, L"identity"); auto request = fixture.request; request.source.identity.file_id[0] ^= 1;
            const auto result = Native(request); request = fixture.request; request.parent_identity.file_id[0] ^= 1; const auto parent = Native(request);
            passed &= Check(result.error == HRESULT_FROM_WIN32(ERROR_FILE_INVALID) && parent.error == HRESULT_FROM_WIN32(ERROR_FILE_INVALID) && fixture.Preserved(),
                "delete_original_source_and_parent_ids_are_checked_before_shell_deletion");
        }
        {
            Fixture fixture(root, L"replacement"); const auto old = fixture.parent / L"original.txt"; fs::rename(fixture.source, old); Write(fixture.source, "replacement");
            const auto result = Native(fixture.request);
            passed &= Check(!result.removed && result.error == HRESULT_FROM_WIN32(ERROR_FILE_INVALID) && Read(fixture.source) == "replacement" && Read(old) == "owned-delete-data",
                "delete_source_replacement_is_preserved_instead_of_deleting_new_object");
        }
        {
            Fixture fixture(root, L"cancel-before"); auto cancel = std::make_shared<std::atomic_bool>(true);
            const auto result = Native(fixture.request, cancel);
            passed &= Check(DeleteCancelled(result.error) && !result.removed && fixture.Preserved(), "delete_cancel_before_start_preserves_complete_source");
        }
        {
            Fixture fixture(root, L"cancel-progress"); auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = Native(fixture.request, cancel, [cancel](const DeleteProgress&) { cancel->store(true); });
            passed &= Check(DeleteCancelled(result.error) && !result.removed && fixture.Preserved(), "delete_cancel_from_initial_progress_prevents_source_removal");
        }
        {
            Fixture fixture(root, L"observer-failed");
            const auto result = Native(fixture.request, {}, [](const DeleteProgress&) { throw 1; });
            passed &= Check(FAILED(result.error) && !result.removed && fixture.Preserved(), "delete_initial_observer_failure_preserves_source_and_is_not_success");
        }
        {
            Fixture fixture(root, L"late-cancel"); auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = Native(fixture.request, cancel, [cancel](const DeleteProgress& value) { if (value.observed_removed) { cancel->store(true); throw 1; } });
            const bool gone = !Exists(fixture.source); const bool restored = Restore(result, fixture.source);
            passed &= Check(result.removed && result.recycled && SUCCEEDED(result.error) && cancel->load() && gone && restored && fixture.Preserved(),
                "delete_late_cancel_and_post_callback_exception_preserve_actual_recycle_commit");
        }
        for (bool recycle : { false, true })
        {
            Fixture fixture(root, recycle ? L"readonly-recycle" : L"readonly-permanent");
            SetFileAttributesW(Extended(fixture.source).c_str(), FILE_ATTRIBUTE_READONLY); fixture.request.source = Entry(fixture.source);
            fixture.request.mode = recycle ? DeleteMode::Recycle : DeleteMode::Permanent; fixture.request.permanent_confirmed = !recycle;
            const auto result = Native(fixture.request); const bool gone = !Exists(fixture.source);
            const bool restored = !recycle || Restore(result, fixture.source);
            passed &= Check(result.removed && SUCCEEDED(result.error) && gone && restored && (!recycle || fixture.Preserved()), recycle ?
                "delete_readonly_file_recycles_and_restores" : "delete_confirmed_readonly_file_deletes_without_extra_ui");
        }
        for (bool recycle : { false, true })
        {
            Fixture fixture(root, recycle ? L"hardlink-recycle" : L"hardlink-permanent"); const auto alias = fixture.parent / L"unselected.txt";
            fs::create_hard_link(fixture.source, alias); fixture.request.mode = recycle ? DeleteMode::Recycle : DeleteMode::Permanent;
            fixture.request.permanent_confirmed = !recycle; const auto result = Native(fixture.request); const bool gone = !Exists(fixture.source);
            const bool restored = !recycle || Restore(result, fixture.source);
            passed &= Check(result.removed && gone && restored && Read(alias) == "owned-delete-data" && Read(alias.wstring() + L":Zone.Identifier") == "owned-zone",
                recycle ? "delete_recycle_removes_only_selected_hardlink_name" : "delete_permanent_removes_only_selected_hardlink_name");
        }
        {
            Fixture fixture(root, L"busy"); HANDLE reader = CreateFileW(Extended(fixture.source).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            const auto result = Native(fixture.request); if (reader != INVALID_HANDLE_VALUE) { CloseHandle(reader); }
            if (result.recycled) { (void)Restore(result, fixture.source); }
            passed &= Check(reader != INVALID_HANDLE_VALUE && !result.removed && FAILED(result.error) && fixture.Preserved(),
                "delete_open_file_without_delete_sharing_reports_failure_and_retains_source");
        }
        for (bool recycle : { false, true })
        {
            Fixture fixture(root, recycle ? L"junction-recycle" : L"junction-permanent"); DeleteFileW(Extended(fixture.source).c_str());
            const auto target = fixture.parent / L"unselected-target"; fs::create_directory(target); Write(target / L"sentinel.txt", "keep-target");
            const bool linked = CreateOperationTestJunction(fixture.source, target); fixture.request.source = Entry(fixture.source);
            fixture.request.mode = recycle ? DeleteMode::Recycle : DeleteMode::Permanent; fixture.request.permanent_confirmed = !recycle;
            const auto result = Native(fixture.request); const bool gone = !Exists(fixture.source); const bool restored = !recycle || Restore(result, fixture.source);
            passed &= Check(linked && result.removed && gone && restored && Read(target / L"sentinel.txt") == "keep-target", recycle ?
                "delete_recycle_junction_object_does_not_delete_target_contents" : "delete_permanent_junction_object_does_not_delete_target_contents");
        }
        {
            auto parent = root / L"long-path"; for (unsigned n = 0; n < 9; ++n) { parent /= L"owned-long-directory-segment-012345"; }
            fs::create_directories(Extended(parent)); const auto source = parent / L"长文件名.txt"; Write(source, "long-data");
            DeleteRequest request{ parent.wstring(), Entry(parent).identity, Entry(source), DeleteMode::Permanent, true };
            const auto result = Native(request);
            passed &= Check(source.wstring().size() > MAX_PATH && result.removed && SUCCEEDED(result.error) && !Exists(source),
                "delete_confirmed_permanent_long_unicode_path_uses_native_shell_item");
        }
        {
            Fixture fixture(root, L"skip"); fixture.request.skip = true; fixture.request.parent = L"Q:\\not-present";
            const auto result = DeleteEntrySafely(fixture.request);
            passed &= Check(result.skipped && SUCCEEDED(result.error) && !result.removed && fixture.Preserved(), "delete_explicit_skip_performs_no_filesystem_io");
        }
        {
            Fixture fixture(root, L"native-queue"); const auto tail = fixture.parent / L"tail.txt"; Write(tail, "tail");
            auto second = fixture.request; second.source = Entry(tail);
            FileOperationCoordinator operation; const auto id = operation.DeleteEntries({ fixture.request, second });
            const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            bool restored = value.delete_batch && value.delete_batch->items.size() == 2;
            if (value.delete_batch) { for (const auto& item : value.delete_batch->items) { restored &= Restore(item.result, fs::path(item.request.parent) / item.request.source.name); } }
            passed &= Check(id && done && value.stage == FileOperationStage::Completed && value.delete_batch && value.delete_batch->recycled_count == 2 &&
                !value.delete_batch->permanent_count && restored && fixture.Preserved() && Read(tail) == "tail", "delete_default_coordinator_recycles_real_multi_item_queue_on_sta");
        }
        {
            Fixture fixture(root, L"watched-directory", true); Write(fixture.parent / L"keep.txt", "keep-parent");
            fixture.request.mode = DeleteMode::Permanent; fixture.request.permanent_confirmed = true;
            TabWindowState tabs; auto* first = tabs.ActiveTab(); auto* second = tabs.Find(tabs.CreateTab());
            const auto open = [](TabState& state, const fs::path& path) {
                const auto request = state.Navigate(Location::Folder(path.wstring()));
                return request && state.CommitBrowse(*request, blink::core::EnumerateDirectory(path.wstring())).status == CommitBrowseStatus::Committed;
            };
            const bool opened = open(*first->state, fixture.source) && open(*second->state, fixture.source / L"sub") &&
                Arm(*first->folder_watcher, fixture.source) && Arm(*second->folder_watcher, fixture.source / L"sub");
            auto stopped = tabs.SuspendFolderWatches({ Location::Folder(fixture.source.wstring()) });
            FileOperationCoordinator operation; const auto id = operation.DeleteEntries({ fixture.request }, {}, std::move(stopped.completion));
            const bool done = Finished(operation.CurrentAction()); const auto relocation = CompletedFolderRelocation(operation.Snapshot());
            auto follow = relocation ? tabs.RelocateFolder(relocation->source, relocation->destination, relocation->source_identity, relocation->preserve_suffix) : std::vector<RelocatedTabNavigation>{};
            bool committed = follow.size() == 2;
            for (const auto& request : follow)
            { committed &= tabs.Find(request.tab_id)->state->CommitBrowse(request.request, blink::core::EnumerateDirectory(request.request.target.FolderPath())).status == CommitBrowseStatus::Committed; }
            const bool armed = Arm(*first->folder_watcher, fixture.parent) && Arm(*second->folder_watcher, fixture.parent);
            const auto a = first->folder_watcher->Stop(), b = second->folder_watcher->Stop();
            const bool ended_a = !a || Finished(a), ended_b = !b || Finished(b);
            passed &= Check(opened && stopped.tabs.size() == 2 && id && done && relocation && !relocation->preserve_suffix && committed && armed && ended_a && ended_b &&
                !Exists(fixture.source) && first->state->PresentationSnapshot().committed_location == Location::Folder(fixture.parent.wstring()) &&
                second->state->PresentationSnapshot().committed_location == Location::Folder(fixture.parent.wstring()) && Read(fixture.parent / L"keep.txt") == "keep-parent",
                "delete_watched_directory_drains_reads_and_returns_root_descendant_tabs_to_parent");
        }
        {
            Fixture fixture(root, L"partial-directory", true); const auto busy = fixture.source / L"sub" / L"busy.txt"; Write(busy, "busy");
            HANDLE reader = CreateFileW(Extended(busy).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            fixture.request.mode = DeleteMode::Permanent; fixture.request.permanent_confirmed = true;
            FileOperationCoordinator operation; const auto id = operation.DeleteEntries({ fixture.request }); const bool done = Finished(operation.CurrentAction());
            const auto failed = operation.Snapshot(); if (reader != INVALID_HANDLE_VALUE) { CloseHandle(reader); }
            passed &= Check(id && done && reader != INVALID_HANDLE_VALUE && failed.stage == FileOperationStage::Failed && failed.delete_result &&
                !failed.delete_result->removed && failed.delete_result->possible_partial && !failed.result.partial_file_left &&
                Exists(fixture.source) && Read(busy) == "busy", "delete_permanent_folder_failure_reports_possible_partial_without_fake_file_partial_flag");
            const auto resumed = operation.ResumeDeleteBatch(id, DeleteRetryAction::Retry); const bool continued = Finished(operation.CurrentAction());
            passed &= Check(resumed > id && continued && operation.Snapshot().stage == FileOperationStage::Completed && !Exists(fixture.source),
                "delete_explicit_retry_finishes_original_remaining_directory_after_busy_handle_closes");
        }
    }
    catch (const std::exception& error) { std::cout << "INFO delete_fixture_exception " << error.what() << '\n'; passed &= Check(false, "delete_owned_native_fixture_unexpected_failure"); }

    const auto requests = std::vector<DeleteRequest>{ Fake(L"first", 2), Fake(L"failed", 3), Fake(L"tail", 4) };
    {
        std::size_t invalid{}; auto changed = requests; changed[1].mode = DeleteMode::Permanent;
        passed &= Check(FAILED(ValidateDeleteRequests(changed, invalid)) && invalid == 1, "delete_queue_validation_rejects_mixed_intents_or_missing_confirmation");
        changed = requests; changed[1].parent = L"D:\\owned\\..\\other";
        passed &= Check(FAILED(ValidateDeleteRequests(changed, invalid)) && invalid == 1, "delete_queue_validation_rejects_ambiguous_later_parent_before_prefix_executes");
    }
    {
        std::atomic_uint calls{}; APTTYPE observed{}; const auto caller = GetCurrentThreadId(); DWORD worker{};
        FileOperationCoordinator operation({}, {}, {}, {}, [&](const auto& request, const auto&, const auto&) {
            ++calls; APTTYPEQUALIFIER qualifier{}; CoGetApartmentType(&observed, &qualifier); worker = GetCurrentThreadId(); return Removed(request);
        });
        const auto id = operation.DeleteEntries(requests); const bool done = Finished(operation.CurrentAction());
        passed &= Check(id && done && calls == 3 && (observed == APTTYPE_STA || observed == APTTYPE_MAINSTA) && worker != caller && operation.Snapshot().delete_batch->removed_count == 3,
            "delete_coordinator_executes_serial_batch_in_background_sta_and_counts_actual_root_results");
    }
    {
        std::atomic_uint calls{}; auto invalid = requests; invalid[2].source.identity.valid = false;
        FileOperationCoordinator operation({}, {}, {}, {}, [&](const auto& request, const auto&, const auto&) { ++calls; return Removed(request); });
        const auto id = operation.DeleteEntries(invalid); const bool done = Finished(operation.CurrentAction());
        passed &= Check(id && done && !calls && operation.Snapshot().stage == FileOperationStage::Failed, "delete_invalid_later_request_rejects_whole_queue_before_any_native_call");
    }
    {
        std::atomic_uint calls{}; auto unconfirmed = requests;
        for (auto& request : unconfirmed) { request.mode = DeleteMode::Permanent; request.permanent_confirmed = true; }
        unconfirmed.back().permanent_confirmed = false;
        FileOperationCoordinator operation({}, {}, {}, {}, [&](const auto& request, const auto&, const auto&) { ++calls; return Removed(request); });
        const auto id = operation.DeleteEntries(unconfirmed); const bool done = Finished(operation.CurrentAction());
        passed &= Check(id && done && !calls && operation.Snapshot().delete_result && operation.Snapshot().delete_result->error == E_ACCESSDENIED,
            "delete_unconfirmed_later_permanent_item_stops_entire_batch_before_prefix");
    }
    {
        std::atomic_uint calls{};
        FileOperationCoordinator operation({}, {}, {}, {}, [&](const auto& request, const auto&, const auto&) { ++calls; return Removed(request); });
        const auto id = operation.DeleteEntries(requests, {}, { FailedPreparation() }); const bool done = Finished(operation.CurrentAction());
        passed &= Check(id && done && !calls && operation.Snapshot().stage == FileOperationStage::Failed,
            "delete_failed_watcher_preparation_never_starts_shell_or_native_executor");
    }
    {
        std::vector<std::wstring> calls;
        FileOperationCoordinator operation({}, {}, {}, {}, [&](const auto& request, const auto&, const auto&) {
            calls.push_back(request.source.name); if (request.source.name == L"failed") { DeleteResult result; result.error = E_ACCESSDENIED; return result; } return Removed(request);
        });
        const auto id = operation.DeleteEntries(requests); const bool done = Finished(operation.CurrentAction()); const auto failed = operation.Snapshot();
        passed &= Check(id && done && calls.size() == 2 && failed.stage == FileOperationStage::Failed && failed.delete_batch && failed.delete_batch->removed_count == 1 &&
            failed.delete_batch->remaining_count == 1 && failed.delete_batch->resumable && operation.HasPendingBatch() &&
            !operation.CreateFolder({ L"D:\\other", Identity(8), L"blocked" }), "delete_failed_queue_preserves_prefix_and_pending_tail_and_blocks_other_mutations");
        const auto resumed = operation.ResumeDeleteBatch(id, DeleteRetryAction::Skip); const bool continued = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
        passed &= Check(resumed > id && continued && calls == std::vector<std::wstring>{ L"first", L"failed", L"tail" } && value.stage == FileOperationStage::Completed &&
            value.delete_batch->removed_count == 2 && value.delete_batch->skipped_count == 1 && value.delete_batch->items[1].retained_result,
            "delete_skip_current_failure_continues_tail_without_replaying_completed_prefix");
    }
    {
        std::atomic_uint calls{}; FileOperationCoordinator operation({}, {}, {}, {}, [&](const auto&, const auto&, const auto&) { ++calls; return DeleteResult{ S_OK }; });
        const auto id = operation.DeleteEntries({ requests[0] }); const bool done = Finished(operation.CurrentAction());
        passed &= Check(id && done && calls == 1 && operation.Snapshot().stage == FileOperationStage::Failed && !operation.Snapshot().result.committed,
            "delete_success_hresult_without_native_removal_fact_does_not_claim_completion");
    }
    {
        FileOperationCoordinator operation({}, {}, {}, {}, [](const auto& request, const auto&, const auto&) {
            auto result = Removed(request); result.post_commit_error = E_FAIL; return result;
        });
        const auto id = operation.DeleteEntries({ requests[0] }); const bool done = Finished(operation.CurrentAction());
        passed &= Check(id && done && operation.Snapshot().stage == FileOperationStage::Completed && operation.Snapshot().delete_batch->warning_count == 1 &&
            operation.Snapshot().result.committed, "delete_post_commit_warning_does_not_replay_or_undo_removed_item");
    }
    {
        FileOperationCoordinator* current{}; std::atomic_uint calls{};
        FileOperationCoordinator operation({}, {}, {}, {}, [&](const auto& request, const auto&, const auto&) { ++calls; current->Cancel(); return Removed(request); }); current = &operation;
        const auto id = operation.DeleteEntries(requests); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
        passed &= Check(id && done && calls == 1 && value.stage == FileOperationStage::Cancelled && value.delete_batch->removed_count == 1 &&
            value.delete_batch->remaining_count == 1 && !value.delete_batch->resumable, "delete_cancel_after_prefix_preserves_committed_item_and_unstarted_tail");
    }
    {
        FileOperationCoordinator* current{};
        FileOperationCoordinator operation({}, {}, {}, {}, [&](const auto& request, const auto&, const auto&) { current->Cancel(); return Removed(request); }); current = &operation;
        const auto id = operation.DeleteEntries({ requests[0] }); const bool done = Finished(operation.CurrentAction());
        passed &= Check(id && done && operation.Snapshot().stage == FileOperationStage::Completed && operation.Snapshot().delete_batch->removed_count == 1,
            "delete_late_cancel_after_last_commit_keeps_completed_queue");
    }
    {
        FileOperationCoordinator operation({}, {}, {}, {}, [](const auto&, const auto&, const auto&) { return DeleteResult{ E_ACCESSDENIED }; });
        const auto id = operation.DeleteEntries(requests); const bool done = Finished(operation.CurrentAction()); operation.Cancel();
        passed &= Check(id && done && operation.Snapshot().stage == FileOperationStage::Cancelled && !operation.HasPendingBatch(),
            "delete_cancel_failed_queue_releases_pending_mutation_gate");
    }
    {
        DeleteProgressCallback saved; unsigned calls{};
        FileOperationCoordinator operation({}, {}, {}, {}, [&](const auto& request, const auto&, const auto& progress) {
            ++calls; if (!saved) { saved = progress; } else { saved({ L"late-old-item", 1, 1, 1 }); }
            progress({ request.source.name, 1, 0, 0 }); return Removed(request);
        });
        const auto id = operation.DeleteEntries(requests); const bool done = Finished(operation.CurrentAction()); const auto before = operation.Snapshot();
        saved({ L"after-finish", 1, 1, 1 }); const auto after = operation.Snapshot();
        passed &= Check(id && done && calls == 3 && before.delete_progress.path == after.delete_progress.path && after.stage == FileOperationStage::Completed,
            "delete_old_item_and_terminal_progress_callbacks_cannot_change_current_operation");
    }
    {
        FileOperationCoordinator* current{}; unsigned publications{};
        FileOperationCoordinator operation({}, {}, {}, {}, [&](const auto& request, const auto&, const auto&) { current->Shutdown(); return Removed(request); }); current = &operation;
        const auto id = operation.DeleteEntries(requests, [&](auto) { ++publications; }); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
        passed &= Check(id && done && value.closed && value.delete_batch->removed_count == 1 && !value.delete_batch->resumable,
            "delete_shutdown_keeps_recorded_native_prefix_and_suppresses_future_work");
    }
    {
        TabState state; const auto path = Location::Folder(L"D:\\selection"); const auto navigation = state.Navigate(path);
        blink::core::DirectoryListing listing; listing.path = path.FolderPath(); listing.directory_identity = Identity(1);
        auto first = requests[0].source, alias = first; first.name = L"first.txt"; alias.name = L"alias.txt"; listing.entries = { first, alias };
        if (navigation) { (void)state.CommitBrowse(*navigation, listing); } (void)state.SelectAllRows();
        std::vector<DeleteRequest> captured; const auto error = CaptureDeleteRequests(state, DeleteMode::Permanent, captured); std::size_t invalid{};
        passed &= Check(SUCCEEDED(error) && captured.size() == 2 && captured[0].source.name != captured[1].source.name &&
            captured[0].source.identity == captured[1].source.identity && !captured[0].permanent_confirmed && FAILED(ValidateDeleteRequests(captured, invalid)),
            "delete_selection_capture_keeps_hardlink_rows_separate_and_does_not_forge_confirmation");
        FolderCommandCapabilities capabilities{ true, false, 2, true, true, false };
        passed &= Check(EvaluateFolderCommand(FolderCommand::Delete, capabilities) == FolderCommandStatus::Ready &&
            EvaluateFolderCommand(FolderCommand::PermanentDelete, capabilities) == FolderCommandStatus::Ready,
            "delete_folder_commands_enable_multi_selection_with_committed_identity");
    }
    {
        NavigationModel model; const auto source = Location::Folder(L"D:\\parent\\gone"), child = Location::Folder(L"D:\\parent\\gone\\sub"), parent = Location::Folder(L"D:\\parent");
        for (const auto& location : { source, child }) { const auto request = model.Navigate(location); if (request) { (void)model.Commit(*request); } }
        const auto follow = model.RelocateFolder(source, parent, false);
        passed &= Check(follow && follow->action == NavigationAction::DeleteLocation && follow->target == parent && model.Commit(*follow) &&
            model.Snapshot().committed_location == parent && model.Snapshot().back_history.back() == parent,
            "delete_completed_folder_collapses_root_descendant_and_history_locations_to_existing_parent");
        FileOperationSnapshot snapshot; snapshot.kind = FileOperationKind::Delete; snapshot.operation_id = 1; snapshot.delete_request = requests[0];
        snapshot.delete_request->source.is_directory = true; snapshot.delete_result = Removed(*snapshot.delete_request);
        const auto relocation = CompletedFolderRelocation(snapshot);
        passed &= Check(relocation && !relocation->preserve_suffix && relocation->destination == Location::Folder(requests[0].parent) && DeleteRefreshParent(snapshot),
            "delete_completed_directory_publishes_parent_fallback_and_refresh_facts");
        snapshot.delete_result->removed = false; snapshot.delete_result->possible_partial = true;
        passed &= Check(!CompletedFolderRelocation(snapshot) && DeleteRefreshParent(snapshot),
            "delete_partial_directory_refreshes_without_navigating_away_from_remaining_source");
    }
    {
        TabState state; const auto source = Location::Folder(L"D:\\parent\\gone"), parent = Location::Folder(L"D:\\parent");
        auto request = state.Navigate(source); blink::core::DirectoryListing listing; listing.path = source.FolderPath(); listing.directory_identity = Identity(2);
        auto entry = requests[0].source; entry.name = L"same-link-name"; listing.entries = { entry };
        if (request) { (void)state.CommitBrowse(*request, listing); } (void)state.SelectEntry(entry);
        const auto fallback = state.RelocateFolder(source, parent, false); listing.path = parent.FolderPath(); listing.directory_identity = Identity(1);
        const auto committed = fallback ? state.CommitBrowse(*fallback, listing).status : CommitBrowseStatus::Failed;
        passed &= Check(committed == CommitBrowseStatus::Committed && state.PresentationSnapshot().selected_count == 0,
            "delete_parent_fallback_does_not_select_an_unrelated_parent_hardlink_with_same_name_and_id");
    }
    return passed;
}
