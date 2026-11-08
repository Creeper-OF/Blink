#include "CrossVolumeMoveTests.h"
#include "Blink/App/MoveOperation.h"
#include "Blink/App/FileOperationCoordinator.h"
#include "Blink/App/MoveTransferPresentation.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <stdexcept>
#include <future>

namespace
{
    using namespace blink::app;
    namespace fs = std::filesystem;
    bool Check(bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry entry;
        if (!blink::core::ReadEntryMetadata(path.wstring(), entry)) { throw std::runtime_error("metadata"); }
        entry.name = path.filename().wstring(); entry.full_path = path.wstring(); return entry;
    }
    std::string Read(const fs::path& path)
    { std::ifstream file(path, std::ios::binary); return { std::istreambuf_iterator<char>(file), {} }; }
    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        auto done = std::make_shared<std::promise<Status>>(); auto future = done->get_future();
        action.Completed([done](auto&&, Status status) { done->set_value(status); });
        return future.wait_for(std::chrono::seconds(8)) == std::future_status::ready && future.get() == Status::Completed;
    }
    struct Fixture
    {
        fs::path root, target, source;
        MoveRequest request;
        Fixture(const fs::path& local, const fs::path& other, const wchar_t* name)
            : root(local / name), target(other / name), source(root / L"来源.txt")
        {
            fs::create_directories(root); fs::create_directories(target);
            std::ofstream(source) << "owned-data";
            std::ofstream(source.wstring() + L":Zone.Identifier") << "owned-zone";
            request = { root.wstring(), Entry(root).identity, Entry(source), target.wstring(), Entry(target).identity, L"目标.txt" };
        }
        fs::path Destination() const { return target / request.destination_name; }
        bool Preserved() const { return fs::exists(source) && Read(source) == "owned-data" && Read(source.wstring() + L":Zone.Identifier") == "owned-zone"; }
        bool Copied() const { return Read(Destination()) == "owned-data" && Read(Destination().wstring() + L":Zone.Identifier") == "owned-zone"; }
    };
    struct DirectoryFixture
    {
        fs::path root, target, source;
        MoveRequest request;
        DirectoryFixture(const fs::path& local, const fs::path& other, const wchar_t* name, bool empty = false)
            : root(local / name), target(other / name), source(root / L"来源目录")
        {
            fs::create_directories(source); fs::create_directories(target);
            if (!empty)
            {
                fs::create_directories(source / L"子目录" / L"空目录");
                std::ofstream(source / L"a.txt") << "root-data";
                std::ofstream(source / L"子目录" / L"b.txt") << "nested-data";
                std::ofstream((source / L"子目录" / L"b.txt").wstring() + L":Zone.Identifier") << "nested-zone";
            }
            request = { root.wstring(), Entry(root).identity, Entry(source), target.wstring(), Entry(target).identity, L"目标目录" };
        }
        fs::path Destination() const { return target / request.destination_name; }
        bool Copied(const fs::path& path) const
        {
            return Read(path / L"a.txt") == "root-data" && Read(path / L"子目录" / L"b.txt") == "nested-data" &&
                Read((path / L"子目录" / L"b.txt").wstring() + L":Zone.Identifier") == "nested-zone" &&
                fs::is_directory(path / L"子目录" / L"空目录");
        }
    };
}
bool RunCrossVolumeMoveTests()
{
    bool passed = true;
    const auto suffix = L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto local = fs::absolute(fs::path(L".build") / L"cross-volume-move-fixtures" / suffix);
    wchar_t temp[32768]{};
    if (!GetTempPathW(static_cast<DWORD>(std::size(temp)), temp)) { return Check(false, "cross_move_other_volume_temp_path"); }
    const auto other = fs::path(temp) / L"BlinkCrossVolumeMove" / suffix;
    fs::create_directories(local); fs::create_directories(other);
    struct Cleanup { fs::path a, b; ~Cleanup() { std::error_code error; fs::remove_all(a, error); fs::remove_all(b, error); } } cleanup{ local, other };
    if (Entry(local).identity.volume_serial == Entry(other).identity.volume_serial)
    { std::cout << "SKIP cross_move_real_other_volume_unavailable\n"; return true; }
    try
    {
        {
            Fixture f(local, other, L"success");
            const auto result = MoveFileAcrossVolumesSafely(f.request);
            passed &= Check(result.moved && result.source_removed && !result.error && result.copy_result && result.copy_result->copied &&
                !fs::exists(f.source) && f.Copied(), "cross_move_real_file_and_ads_move_between_distinct_volumes");
        }
        {
            Fixture f(local, other, L"collision"); std::ofstream(f.Destination()) << "rival";
            const auto result = MoveFileAcrossVolumesSafely(f.request);
            passed &= Check(!result.moved && result.error == ERROR_FILE_EXISTS && f.Preserved() && Read(f.Destination()) == "rival",
                "cross_move_existing_destination_preserves_both_files");
            f.request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            const auto retry = MoveFileAcrossVolumesSafely(f.request);
            passed &= Check(retry.moved && !fs::exists(f.source) && Read(f.target / L"目标 (2).txt") == "owned-data" &&
                Read(f.Destination()) == "rival", "cross_move_keep_both_uses_actual_numbered_destination");
        }
        {
            Fixture f(local, other, L"cancel-copy"); auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = MoveFileAcrossVolumesSafely(f.request, cancel, [cancel](const MoveProgress& value) {
                if (value.phase == MovePhase::Copying) { cancel->store(true); }
            });
            passed &= Check(!result.moved && result.error == ERROR_CANCELLED && f.Preserved() && !fs::exists(f.Destination()),
                "cross_move_cancel_during_copy_cleans_owned_target_and_keeps_source");
        }
        {
            Fixture f(local, other, L"cancel-remove"); auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = MoveFileAcrossVolumesSafely(f.request, cancel, {}, [cancel] { cancel->store(true); return ERROR_SUCCESS; });
            passed &= Check(!result.moved && !result.source_removed && result.error == ERROR_CANCELLED &&
                result.copy_result && result.copy_result->copied && f.Preserved() && f.Copied(),
                "cross_move_cancel_after_copy_reports_copied_destination_and_retained_source");
        }
        {
            Fixture f(local, other, L"remove-failed");
            const auto result = MoveFileAcrossVolumesSafely(f.request, {}, {}, [] { return ERROR_ACCESS_DENIED; });
            passed &= Check(!result.moved && result.error == ERROR_ACCESS_DENIED && result.copy_result && result.copy_result->copied &&
                f.Preserved() && f.Copied(), "cross_move_removal_failure_does_not_hide_successful_copy_or_delete_source");
        }
        {
            Fixture f(local, other, L"replaced"); fs::rename(f.source, f.root / L"original.txt");
            std::ofstream(f.source) << "replacement";
            const auto result = MoveFileAcrossVolumesSafely(f.request);
            passed &= Check(!result.moved && result.error == ERROR_FILE_INVALID && Read(f.source) == "replacement" &&
                Read(f.root / L"original.txt") == "owned-data" && !fs::exists(f.Destination()),
                "cross_move_source_replacement_is_not_moved_or_deleted");
        }
        {
            Fixture f(local, other, L"late-cancel"); auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = MoveFileAcrossVolumesSafely(f.request, cancel, [cancel](const MoveProgress& value) {
                if (value.phase == MovePhase::Completed) { cancel->store(true); throw 1; }
            });
            passed &= Check(result.moved && cancel->load() && !fs::exists(f.source) && f.Copied(),
                "cross_move_late_cancel_or_final_observer_exception_keeps_committed_move");
        }
        {
            Fixture f(local, other, L"readonly"); SetFileAttributesW(f.source.c_str(), FILE_ATTRIBUTE_READONLY);
            f.request.source = Entry(f.source);
            const auto result = MoveFileAcrossVolumesSafely(f.request);
            passed &= Check(result.moved && !fs::exists(f.source) && f.Copied(), "cross_move_readonly_file_moves_without_changing_source_attributes_first");
            SetFileAttributesW(f.Destination().c_str(), FILE_ATTRIBUTE_NORMAL);
            SetFileAttributesW(f.source.c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        {
            Fixture f(local, other, L"hardlink"); fs::create_hard_link(f.source, f.root / L"alias.txt");
            const auto result = MoveFileAcrossVolumesSafely(f.request);
            passed &= Check(result.moved && !fs::exists(f.source) && Read(f.root / L"alias.txt") == "owned-data" && f.Copied(),
                "cross_move_only_selected_hardlink_name_is_removed");
        }
        {
            Fixture f(local, other, L"skip"); f.request.conflict_policy = FileCopyConflictPolicy::Skip;
            const auto result = MoveFileAcrossVolumesSafely(f.request);
            passed &= Check(result.skipped && !result.moved && !result.error && f.Preserved() && fs::is_empty(f.target),
                "cross_move_explicit_skip_has_no_copy_or_delete");
        }
        {
            Fixture f(local, other, L"source-busy");
            HANDLE writer = CreateFileW(f.source.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, 0, nullptr);
            const auto result = MoveFileAcrossVolumesSafely(f.request);
            if (writer != INVALID_HANDLE_VALUE) { CloseHandle(writer); }
            passed &= Check(writer != INVALID_HANDLE_VALUE && !result.moved && result.error == ERROR_SHARING_VIOLATION &&
                f.Preserved() && !fs::exists(f.Destination()), "cross_move_open_writer_preserves_source_and_creates_no_target");
        }
        {
            Fixture f(local, other, L"entry"); const auto result = MoveEntrySafely(f.request);
            passed &= Check(result.moved && result.source_removed && f.Copied() && !fs::exists(f.source),
                "cross_move_shared_move_entry_dispatches_real_cross_volume_file");
        }
        {
            Fixture f(local, other, L"queue");
            const auto tail = f.root / L"tail.txt"; std::ofstream(tail) << "tail";
            auto tail_request = f.request; tail_request.source = Entry(tail); tail_request.destination_name = L"tail.txt";
            FileOperationCoordinator operation({}, {}, {}, [](const auto& request, const auto& cancel, const auto& progress) {
                return MoveFileAcrossVolumesSafely(request, cancel, progress, request.source.name == L"来源.txt" ?
                    BeforeMoveSourceRemoval{ [] { return ERROR_ACCESS_DENIED; } } : BeforeMoveSourceRemoval{});
            });
            const auto id = operation.MoveEntries({ f.request, tail_request });
            const bool done = Finished(operation.CurrentAction()); const auto failed = operation.Snapshot();
            passed &= Check(id && done && failed.stage == FileOperationStage::Failed && failed.move_batch &&
                failed.move_batch->moved_count == 0 && failed.move_batch->partial_count == 1 && failed.move_batch->remaining_count == 1 &&
                failed.move_batch->resumable && f.Preserved() && f.Copied() && fs::exists(tail),
                "cross_move_queue_retains_completed_copy_and_pending_tail_after_source_removal_failure");
            const auto refresh = MoveRefreshParents(failed);
            passed &= Check(refresh.size() == 1 && refresh.front() == f.request.destination_parent_identity,
                "cross_move_copy_only_result_refreshes_destination_without_claiming_source_mutation");
            passed &= Check(operation.ResumeMoveBatch(id, FileCopyConflictPolicy::KeepBoth) == 0 &&
                operation.ResumeMoveBatch(id, FileCopyConflictPolicy::Fail, std::wstring(L"again.txt")) == 0 &&
                !fs::exists(f.target / L"again.txt"), "cross_move_partial_item_cannot_silently_start_another_copy_on_retry");
            const auto resumed = operation.ResumeMoveBatch(id, FileCopyConflictPolicy::Skip);
            const bool continued = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(resumed > id && continued && value.stage == FileOperationStage::Completed && value.move_batch &&
                value.move_batch->moved_count == 1 && value.move_batch->skipped_count == 1 && value.move_batch->partial_count == 1 &&
                value.move_batch->items[0].retained_result && f.Preserved() && f.Copied() &&
                !fs::exists(tail) && Read(f.target / L"tail.txt") == "tail",
                "cross_move_explicit_skip_continues_tail_and_retains_first_copy_outcome");
        }
        {
            DirectoryFixture f(local, other, L"directory-success");
            MoveProgress observed;
            const auto result = MoveEntrySafely(f.request, {}, [&](const MoveProgress& value) { observed = value; });
            passed &= Check(result.moved && result.source_removed && !result.error && !fs::exists(f.source) && f.Copied(f.Destination()),
                "cross_directory_real_nested_files_ads_and_empty_directories_move");
            passed &= Check(result.directory_copy_result && result.directory_copy_result->copied && !result.copy_result &&
                result.directory_copy_result->files_copied == 2 && result.directory_copy_result->directories_created == 3 &&
                result.source_files_removed == 2 && result.source_directories_removed == 3 && observed.phase == MovePhase::Completed &&
                observed.directory && observed.source_files_removed == 2 && observed.source_directories_removed == 3,
                "cross_directory_success_reports_independent_tree_effects_and_completed_progress");
        }
        {
            DirectoryFixture f(local, other, L"directory-empty", true);
            const auto result = MoveEntrySafely(f.request);
            passed &= Check(result.moved && !result.error && !fs::exists(f.source) && fs::is_directory(f.Destination()) && fs::is_empty(f.Destination()),
                "cross_directory_empty_root_moves_between_distinct_volumes");
        }
        {
            DirectoryFixture f(local, other, L"directory-collision"); fs::create_directory(f.Destination());
            std::ofstream(f.Destination() / L"rival.txt") << "rival";
            const auto result = MoveEntrySafely(f.request);
            passed &= Check(!result.moved && (result.error == ERROR_ALREADY_EXISTS || result.error == ERROR_FILE_EXISTS) &&
                f.Copied(f.source) && Read(f.Destination() / L"rival.txt") == "rival",
                "cross_directory_root_conflict_preserves_source_and_existing_target");
            f.request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            const auto retry = MoveEntrySafely(f.request);
            passed &= Check(retry.moved && !fs::exists(f.source) && f.Copied(f.target / L"目标目录 (2)") &&
                Read(f.Destination() / L"rival.txt") == "rival", "cross_directory_keep_both_moves_to_actual_numbered_root");
        }
        {
            DirectoryFixture f(local, other, L"directory-cancel-before-file"); auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = MoveDirectoryAcrossVolumesSafely(f.request, cancel, [cancel](const MoveProgress& p) {
                if (p.directories_created == 1) { cancel->store(true); }
            });
            passed &= Check(!result.moved && result.error == ERROR_CANCELLED && result.directory_copy_result &&
                result.directory_copy_result->partial_tree_left && !result.source_files_removed && !result.source_directories_removed &&
                f.Copied(f.source) && fs::is_empty(f.Destination()),
                "cross_directory_cancel_before_first_file_keeps_entire_source_and_reports_created_root");
        }
        {
            DirectoryFixture f(local, other, L"directory-cancel-prefix"); auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = MoveDirectoryAcrossVolumesSafely(f.request, cancel, [cancel](const MoveProgress& p) {
                if (p.source_files_removed == 1) { cancel->store(true); }
            });
            passed &= Check(!result.moved && result.error == ERROR_CANCELLED && result.source_files_removed == 1 &&
                !result.source_directories_removed && MoveHasPartialDestination(result) && MoveHasSourceChanges(result) &&
                result.directory_copy_result && result.directory_copy_result->files_copied == 1 &&
                !fs::exists(f.source / L"a.txt") && Read(f.Destination() / L"a.txt") == "root-data" &&
                Read(f.source / L"子目录" / L"b.txt") == "nested-data" && !fs::exists(f.Destination() / L"子目录" / L"b.txt"),
                "cross_directory_cancel_keeps_moved_file_prefix_and_unstarted_source_tail");
            FileOperationSnapshot snapshot; snapshot.kind = FileOperationKind::Move;
            snapshot.move_request = f.request; snapshot.move_result = result;
            const auto refresh = MoveRefreshParents(snapshot);
            passed &= Check(refresh.size() == 2 && refresh[0] == f.request.source_parent_identity && refresh[1] == f.request.destination_parent_identity,
                "cross_directory_partial_source_removal_refreshes_both_parent_locations");
        }
        {
            DirectoryFixture f(local, other, L"directory-first-removal-failed");
            const auto result = MoveDirectoryAcrossVolumesSafely(f.request, {}, {}, [] { return ERROR_ACCESS_DENIED; });
            passed &= Check(!result.moved && result.error == ERROR_ACCESS_DENIED && result.directory_copy_result &&
                result.directory_copy_result->files_copied == 1 && !result.source_files_removed && f.Copied(f.source) &&
                Read(f.Destination() / L"a.txt") == "root-data" && MoveHasPartialDestination(result),
                "cross_directory_failed_first_file_removal_keeps_source_and_committed_destination_file");
        }
        {
            DirectoryFixture f(local, other, L"directory-cancel-empty-removal"); auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = MoveDirectoryAcrossVolumesSafely(f.request, cancel, [cancel](const MoveProgress& p) {
                if (p.phase == MovePhase::Committing) { cancel->store(true); }
            });
            passed &= Check(!result.moved && result.error == ERROR_CANCELLED && result.directory_copy_result &&
                result.directory_copy_result->copied && !result.directory_copy_result->partial_tree_left && result.source_files_removed == 2 &&
                !result.source_directories_removed && fs::is_directory(f.source / L"子目录" / L"空目录") &&
                !fs::exists(f.source / L"a.txt") && f.Copied(f.Destination()),
                "cross_directory_cancel_during_empty_directory_removal_keeps_completed_copy_fact");
        }
        {
            DirectoryFixture f(local, other, L"directory-removal-prefix"); unsigned calls{};
            const auto result = MoveDirectoryAcrossVolumesSafely(f.request, {}, {}, [&calls] { return ++calls == 4 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS; });
            passed &= Check(!result.moved && result.error == ERROR_ACCESS_DENIED && result.directory_copy_result && result.directory_copy_result->copied &&
                result.source_files_removed == 2 && result.source_directories_removed == 1 &&
                !fs::exists(f.source / L"子目录" / L"空目录") && fs::is_directory(f.source / L"子目录") && f.Copied(f.Destination()),
                "cross_directory_empty_directory_failure_preserves_exact_removal_counts");
        }
        {
            DirectoryFixture f(local, other, L"directory-new-child"); bool added{};
            const auto result = MoveDirectoryAcrossVolumesSafely(f.request, {}, [&](const MoveProgress& p) {
                if (!added && p.source_files_removed == 1) { std::ofstream(f.source / L"later.txt") << "new-source-data"; added = true; }
            });
            passed &= Check(added && !result.moved && result.error == ERROR_DIR_NOT_EMPTY && result.source_files_removed == 2 &&
                result.source_directories_removed == 2 && result.directory_copy_result && result.directory_copy_result->copied &&
                Read(f.source / L"later.txt") == "new-source-data" && !fs::exists(f.Destination() / L"later.txt") && f.Copied(f.Destination()),
                "cross_directory_new_unplanned_source_child_is_preserved_by_empty_directory_removal");
        }
        {
            DirectoryFixture f(local, other, L"directory-replaced-tail"); bool attempted{}, changed{}; DWORD change_error{};
            const auto result = MoveDirectoryAcrossVolumesSafely(f.request, {}, [&](const MoveProgress& p) {
                if (!attempted && p.source_files_removed == 1)
                {
                    attempted = true;
                    changed = MoveFileExW((f.source / L"子目录" / L"b.txt").c_str(), (f.source / L"子目录" / L"original.txt").c_str(), 0) != FALSE;
                    change_error = changed ? ERROR_SUCCESS : GetLastError();
                    if (changed) { std::ofstream(f.source / L"子目录" / L"b.txt") << "replacement"; }
                }
            });
            std::cout << "INFO cross_directory_replaced_tail attempted=" << attempted << " changed=" << changed << " change_error=" << change_error
                << " error=" << result.error << " moved=" << result.moved << " removed=" << result.source_files_removed << '\n';
            const bool preserved = changed ? !result.moved && result.error == ERROR_FILE_INVALID && result.source_files_removed == 1 &&
                Read(f.source / L"子目录" / L"b.txt") == "replacement" && Read(f.source / L"子目录" / L"original.txt") == "nested-data" &&
                Read(f.Destination() / L"a.txt") == "root-data" && !fs::exists(f.Destination() / L"子目录" / L"b.txt") :
                (change_error == ERROR_ACCESS_DENIED || change_error == ERROR_SHARING_VIOLATION) && result.moved && !fs::exists(f.source) && f.Copied(f.Destination());
            passed &= Check(attempted && preserved, "cross_directory_planned_later_file_replacement_is_blocked_or_detected_without_moving_replacement");
        }
        {
            DirectoryFixture f(local, other, L"directory-observer-failure");
            const auto result = MoveDirectoryAcrossVolumesSafely(f.request, {}, [](const MoveProgress& p) {
                if (p.source_files_removed == 1) { throw 1; }
            });
            passed &= Check(!result.moved && result.error == ERROR_UNHANDLED_EXCEPTION && result.source_files_removed == 1 &&
                result.directory_copy_result && result.directory_copy_result->partial_tree_left &&
                !fs::exists(f.source / L"a.txt") && Read(f.Destination() / L"a.txt") == "root-data" &&
                Read(f.source / L"子目录" / L"b.txt") == "nested-data" && !fs::exists(f.Destination() / L"子目录" / L"b.txt"),
                "cross_directory_intermediate_observer_failure_preserves_native_prefix_and_unstarted_sources");
        }
        {
            DirectoryFixture f(local, other, L"directory-readonly");
            SetFileAttributesW(f.source.c_str(), FILE_ATTRIBUTE_READONLY);
            SetFileAttributesW((f.source / L"子目录" / L"b.txt").c_str(), FILE_ATTRIBUTE_READONLY);
            f.request.source = Entry(f.source);
            const auto result = MoveDirectoryAcrossVolumesSafely(f.request);
            passed &= Check(result.moved && !fs::exists(f.source) && f.Copied(f.Destination()) &&
                (GetFileAttributesW(f.Destination().c_str()) & FILE_ATTRIBUTE_READONLY),
                "cross_directory_readonly_source_tree_moves_and_preserves_destination_attributes");
            SetFileAttributesW(f.Destination().c_str(), FILE_ATTRIBUTE_NORMAL);
            SetFileAttributesW((f.Destination() / L"子目录" / L"b.txt").c_str(), FILE_ATTRIBUTE_NORMAL);
            SetFileAttributesW(f.source.c_str(), FILE_ATTRIBUTE_NORMAL);
            SetFileAttributesW((f.source / L"子目录" / L"b.txt").c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        {
            DirectoryFixture f(local, other, L"directory-late-cancel"); auto cancel = std::make_shared<std::atomic_bool>();
            const auto result = MoveDirectoryAcrossVolumesSafely(f.request, cancel, [cancel](const MoveProgress& p) {
                if (p.phase == MovePhase::Completed) { cancel->store(true); throw 1; }
            });
            passed &= Check(result.moved && result.source_removed && !result.error && cancel->load() &&
                result.source_directories_removed == 3 && !fs::exists(f.source) && f.Copied(f.Destination()),
                "cross_directory_late_cancel_and_observer_exception_keep_native_move_commit");
        }
        {
            DirectoryFixture f(local, other, L"directory-skip"); f.request.conflict_policy = FileCopyConflictPolicy::Skip;
            const auto skipped = MoveDirectoryAcrossVolumesSafely(f.request);
            f.request.conflict_policy = FileCopyConflictPolicy::Cancel;
            const auto cancelled = MoveDirectoryAcrossVolumesSafely(f.request);
            passed &= Check(skipped.skipped && !skipped.error && !skipped.moved && cancelled.error == ERROR_CANCELLED &&
                !cancelled.moved && f.Copied(f.source) && fs::is_empty(f.target), "cross_directory_skip_and_cancel_do_not_copy_or_remove");
        }
        {
            DirectoryFixture f(local, other, L"directory-queue");
            const auto prefix = f.root / L"prefix.txt", tail = f.root / L"tail.txt";
            std::ofstream(prefix) << "prefix"; std::ofstream(tail) << "tail";
            auto prefix_request = f.request; prefix_request.source = Entry(prefix); prefix_request.destination_name = L"prefix.txt";
            auto tail_request = f.request; tail_request.source = Entry(tail); tail_request.destination_name = L"tail.txt";
            FileOperationCoordinator operation({}, {}, {}, [](const auto& request, const auto& cancel, const auto& progress) {
                if (!request.source.is_directory) { return MoveEntrySafely(request, cancel, progress); }
                unsigned calls{};
                return MoveDirectoryAcrossVolumesSafely(request, cancel, progress, [&calls] { return ++calls == 2 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS; });
            });
            const auto id = operation.MoveEntries({ prefix_request, f.request, tail_request });
            const bool done = Finished(operation.CurrentAction()); const auto failed = operation.Snapshot();
            passed &= Check(id && done && failed.stage == FileOperationStage::Failed && failed.move_batch &&
                failed.move_batch->moved_count == 1 && failed.move_batch->partial_count == 1 && failed.move_batch->remaining_count == 1 &&
                failed.move_result && failed.move_result->source_files_removed == 1 && failed.move_result->directory_copy_result &&
                !failed.move_result->copy_result && !fs::exists(prefix) && fs::exists(tail) && f.Copied(f.Destination()) &&
                !fs::exists(f.source / L"a.txt") && Read(f.source / L"子目录" / L"b.txt") == "nested-data",
                "cross_directory_mixed_queue_retains_native_prefix_partial_tree_and_pending_tail");
            passed &= Check(!MoveQueueHasRootConflict(failed) && operation.ResumeMoveBatch(id, FileCopyConflictPolicy::KeepBoth) == 0 &&
                operation.ResumeMoveBatch(id, FileCopyConflictPolicy::Fail, std::wstring(L"duplicate")) == 0,
                "cross_directory_partial_tree_cannot_be_replayed_as_root_conflict_or_recopied");
            const auto resumed = operation.ResumeMoveBatch(id, FileCopyConflictPolicy::Skip);
            const bool continued = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(resumed > id && continued && value.stage == FileOperationStage::Completed && value.move_batch &&
                value.move_batch->moved_count == 2 && value.move_batch->skipped_count == 1 && value.move_batch->partial_count == 1 &&
                value.move_batch->items[1].retained_result && value.move_batch->items[1].retained_result->source_files_removed == 1 &&
                value.move_batch->items[1].retained_result->directory_copy_result && !fs::exists(tail) && Read(f.target / L"tail.txt") == "tail" &&
                fs::exists(f.source) && !fs::exists(f.target / L"目标目录 (2)"),
                "cross_directory_explicit_skip_continues_tail_without_recopying_and_keeps_removal_history");
        }
        {
            DirectoryFixture f(local, other, L"directory-queue-success"); const auto file = f.root / L"loose.txt"; std::ofstream(file) << "loose";
            auto request = f.request; request.source = Entry(file); request.destination_name = L"loose.txt";
            FileOperationCoordinator operation; const auto id = operation.MoveEntries({ f.request, request });
            const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Completed && value.move_batch && value.move_batch->moved_count == 2 &&
                !value.move_batch->partial_count && !fs::exists(f.source) && !fs::exists(file) && f.Copied(f.Destination()) &&
                Read(f.target / L"loose.txt") == "loose", "cross_directory_default_mixed_cross_volume_queue_moves_files_and_directories");
        }
    }
    catch (...) { passed &= Check(false, "cross_move_fixture_unexpected_failure"); }
    return passed;
}
