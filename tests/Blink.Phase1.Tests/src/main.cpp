#include "IndexServiceSetupTests.h"
#include "IndexServiceLiveProbe.h"
#include "MoveCoordinatorTests.h"
#include "MoveBatchTests.h"
#include "MoveTransferTests.h"
#include "DeleteOperationTests.h"
#include "ItemPropertiesTests.h"
#include "FileDragDropTests.h"
#include "FileReplaceTests.h"
#include "DirectoryMergeTests.h"
#include "DirectoryMoveMergeTests.h"
#include "FolderRelocationTests.h"
#include "ExplorerCommandBarTests.h"
#include "AddressBarTests.h"
#include "FileNameSelectionTests.h"
#include "CrossVolumeMoveTests.h"
#include "FolderNavigationTests.h"
#include "MoveOperationTests.h"
#include "MixedCopyBatchTests.h"
#include "Blink/App/BrowseController.h"
#include "Blink/App/BrowseState.h"
#include "Blink/App/FileActivationController.h"
#include "Blink/App/FileOperations.h"
#include "Blink/App/RenameOperation.h"
#include "Blink/App/DeleteOperation.h"
#include "Blink/App/FileDragDropModel.h"
#include "Blink/App/FolderCommandPolicy.h"
#include "Blink/App/FolderChangeState.h"
#include "Blink/App/FolderWatcher.h"
#include "Blink/App/IndexServiceState.h"
#include "Blink/App/IndexServiceCoordinator.h"
#include "Blink/App/SearchState.h"
#include "Blink/App/SearchCoordinator.h"
#include "SearchIntegrationTests.h"
#include "SearchExactTests.h"
#include "SearchContextMenuTests.h"
#include "FileOperationTests.h"
#include "RenameOperationTests.h"
#include "FileCopyOperationTests.h"
#include "FileCopyConflictTests.h"
#include "FileCopyBatchTests.h"
#include "DirectoryCopyOperationTests.h"
#include "DirectoryCopyCoordinatorTests.h"
#include "ClipboardFileTransferTests.h"
#include "ClipboardCopySourceTests.h"
#include "CopyPresentationTests.h"
#include "FileOperationCoordinatorTests.h"
#include "Blink/App/FolderViewFormatting.h"
#include "Blink/App/Localization.h"
#include "Blink/App/NavigationModel.h"
#include "Blink/App/NavigationPane.h"
#include "Blink/App/NavigationTree.h"
#include "Blink/App/TabState.h"
#include "Blink/App/TabWindowState.h"
#include "Blink/Core/FileTypes.h"

#include <Windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "windowsapp")
#pragma comment(lib, "shell32")

namespace
{
    using blink::app::BrowseState;
    using blink::app::BrowseStateKind;

    [[nodiscard]] bool Check(bool condition, const char* name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << "\n";
            return false;
        }
        std::cout << "PASS " << name << "\n";
        return true;
    }

    [[nodiscard]] blink::core::DirectoryListing Listing(const std::wstring& path, DWORD error = ERROR_SUCCESS)
    {
        blink::core::DirectoryListing listing;
        listing.path = path;
        listing.error = error;
        return listing;
    }

    [[nodiscard]] bool RunGenerationAndShutdownTests()
    {
        bool passed = true;

        BrowseState state;
        const std::wstring path_a = L"C:\\phase1-a";
        const std::wstring path_b = L"C:\\phase1-b";
        const auto generation_a = state.Begin(path_a);
        const auto generation_b = state.Begin(path_b);
        passed &= Check(generation_b > generation_a, "browse_generation_increments");
        passed &= Check(!state.Complete(generation_a, path_a, ERROR_SUCCESS), "slow_a_is_stale");
        passed &= Check(state.Complete(generation_b, path_b, ERROR_SUCCESS), "fast_b_completes");
        passed &= Check(state.Snapshot().requested_path == path_b, "fast_b_remains_current");

        const auto queued_generation = state.Begin(path_a);
        passed &= Check(state.IsCurrent(queued_generation, path_a), "queued_callback_initial_gate");
        (void)state.Begin(path_b);
        passed &= Check(!state.Complete(queued_generation, path_a, ERROR_SUCCESS), "queued_callback_second_gate");

        const auto pending_generation = state.Begin(path_a);
        state.Shutdown();
        passed &= Check(!state.Complete(pending_generation, path_a, ERROR_SUCCESS), "shutdown_drops_pending_result");
        passed &= Check(state.Snapshot().shutting_down, "shutdown_marks_lifetime_invalid");

        return passed;
    }

    struct ControllerProbe
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::vector<std::wstring> published_paths;
        std::vector<std::wstring> started_paths;
        std::vector<std::wstring> completed_paths;
    };

    [[nodiscard]] bool RunControllerOwnershipTests()
    {
        auto probe = std::make_shared<ControllerProbe>();
        auto state = std::make_shared<BrowseState>();
        const auto enumerate = [probe](const std::wstring& path) {
            {
                std::scoped_lock lock(probe->mutex);
                probe->started_paths.push_back(path);
                probe->changed.notify_all();
            }

            if (path == L"A")
            {
                Sleep(250);
            }
            else
            {
                Sleep(10);
            }

            {
                std::scoped_lock lock(probe->mutex);
                probe->completed_paths.push_back(path);
                probe->changed.notify_all();
            }
            return Listing(path);
        };
        blink::app::BrowseController controller(state, enumerate);
        const auto publish = [probe](blink::app::BrowseResult result) {
            std::scoped_lock lock(probe->mutex);
            probe->published_paths.push_back(result.requested_path);
            probe->changed.notify_all();
        };

        controller.Browse(L"A", publish);
        {
            std::unique_lock lock(probe->mutex);
            if (!probe->changed.wait_for(lock, std::chrono::seconds(2), [&]() {
                    return std::find(probe->started_paths.begin(), probe->started_paths.end(), L"A") !=
                        probe->started_paths.end();
                }))
            {
                return Check(false, "slow_a_started");
            }
        }
        controller.Browse(L"B", publish);

        bool passed = true;
        {
            std::unique_lock lock(probe->mutex);
            passed &= Check(probe->changed.wait_for(lock, std::chrono::seconds(2), [&]() {
                return std::find(probe->published_paths.begin(), probe->published_paths.end(), L"B") !=
                    probe->published_paths.end();
            }), "fast_b_published");
            passed &= Check(probe->changed.wait_for(lock, std::chrono::seconds(2), [&]() {
                return probe->completed_paths.size() >= 2;
            }), "slow_a_completed_without_join");
            passed &= Check(
                probe->published_paths.size() == 1 && probe->published_paths.front() == L"B",
                "controller_drops_slow_a");
        }
        passed &= Check(state->Snapshot().requested_path == L"B", "controller_state_is_b");
        controller.Shutdown();

        auto shutdown_probe = std::make_shared<ControllerProbe>();
        auto shutdown_state = std::make_shared<BrowseState>();
        const auto slow_enumerate = [shutdown_probe](const std::wstring& path) {
            {
                std::scoped_lock lock(shutdown_probe->mutex);
                shutdown_probe->started_paths.push_back(path);
                shutdown_probe->changed.notify_all();
            }
            Sleep(200);
            {
                std::scoped_lock lock(shutdown_probe->mutex);
                shutdown_probe->completed_paths.push_back(path);
                shutdown_probe->changed.notify_all();
            }
            return Listing(path);
        };
        blink::app::BrowseController shutdown_controller(shutdown_state, slow_enumerate);
        shutdown_controller.Browse(L"shutdown", [shutdown_probe](blink::app::BrowseResult result) {
            std::scoped_lock lock(shutdown_probe->mutex);
            shutdown_probe->published_paths.push_back(result.requested_path);
            shutdown_probe->changed.notify_all();
        });
        {
            std::unique_lock lock(shutdown_probe->mutex);
            passed &= Check(shutdown_probe->changed.wait_for(lock, std::chrono::seconds(2), [&]() {
                return !shutdown_probe->started_paths.empty();
            }), "shutdown_browse_started");
        }
        shutdown_controller.Shutdown();
        {
            std::unique_lock lock(shutdown_probe->mutex);
            passed &= Check(shutdown_probe->changed.wait_for(lock, std::chrono::seconds(2), [&]() {
                return !shutdown_probe->completed_paths.empty();
            }), "shutdown_browse_completed_naturally");
            passed &= Check(shutdown_probe->published_paths.empty(), "shutdown_drops_controller_result");
        }
        return passed;
    }

    [[nodiscard]] bool RunDirectoryResultTests()
    {
        const std::filesystem::path root =
            std::filesystem::path(L".build") / L"phase1-browse-tests" /
            (L"pid-" + std::to_wstring(GetCurrentProcessId())) /
            L"Blink中文路径测试_内蒙古_QWQ";
        const std::filesystem::path empty = root / L"empty";
        const std::filesystem::path unicode = root / L"中文目录";
        const std::filesystem::path unicode_file = unicode / L"测试文件_内蒙古_QWQ.txt";
        const std::filesystem::path missing = root / L"does-not-exist";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::filesystem::create_directories(empty, error);
        if (error)
        {
            std::cerr << "FAIL fixture_create_empty error=" << error.value() << "\n";
            return false;
        }
        std::filesystem::create_directories(unicode, error);
        std::ofstream file(unicode_file, std::ios::binary);
        file << "Blink Phase 1A browse UTF-8 fixture\n";
        file.close();

        bool passed = true;
        BrowseState empty_state;
        const auto empty_generation = empty_state.Begin(empty.wstring());
        const auto empty_listing = blink::core::EnumerateDirectory(empty.wstring());
        passed &= Check(empty_state.Complete(empty_generation, empty.wstring(), empty_listing.error), "empty_directory_completes");
        passed &= Check(empty_state.Snapshot().state == BrowseStateKind::Completed, "empty_directory_state");

        BrowseState missing_state;
        const auto missing_generation = missing_state.Begin(missing.wstring());
        const auto missing_listing = blink::core::EnumerateDirectory(missing.wstring());
        passed &= Check(missing_listing.error != ERROR_SUCCESS, "missing_directory_reports_error");
        passed &= Check(missing_state.Complete(missing_generation, missing.wstring(), missing_listing.error), "missing_directory_completes_error");
        passed &= Check(missing_state.Snapshot().state == BrowseStateKind::Failed, "missing_directory_state");

        BrowseState unicode_state;
        const auto unicode_generation = unicode_state.Begin(unicode.wstring());
        const auto unicode_listing = blink::core::EnumerateDirectory(unicode.wstring());
        const bool found_unicode = std::any_of(
            unicode_listing.entries.begin(),
            unicode_listing.entries.end(),
            [](const blink::core::FileEntry& entry) { return entry.name == L"测试文件_内蒙古_QWQ.txt"; });
        passed &= Check(found_unicode, "unicode_filename_enumerated");
        passed &= Check(unicode_state.Complete(unicode_generation, unicode.wstring(), unicode_listing.error), "unicode_directory_completes");

        std::filesystem::remove_all(root, error);
        return passed;
    }

    [[nodiscard]] bool RunLocationModelTests()
    {
        const auto this_pc = blink::app::Location::ThisPC();
        const auto folder_a = blink::app::Location::Folder(L"C:\\Blink\\Folder");
        const auto folder_a_case = blink::app::Location::Folder(L"c:\\blink\\folder");
        const auto child = blink::app::Location::Folder(L"C:\\Blink\\Folder\\Child");
        const auto root = blink::app::Location::Folder(L"C:\\");

        bool passed = true;
        passed &= Check(this_pc == blink::app::Location::ThisPC(),
            "location_this_pc_equality");
        passed &= Check(folder_a == folder_a_case,
            "location_folder_case_insensitive_equality");
        passed &= Check(this_pc != folder_a,
            "location_this_pc_differs_from_folder");
        passed &= Check(child.Up().has_value() && child.Up()->IsFolder() &&
                child.Up()->FolderPath() == blink::core::NormalizePath(L"C:\\Blink\\Folder"),
            "location_folder_up_parent");
        passed &= Check(root.Up().has_value() && root.Up()->IsThisPC(),
            "location_volume_root_up_this_pc");
        passed &= Check(!this_pc.Up().has_value(),
            "location_this_pc_up_unavailable");

        blink::app::NavigationModel model;
        const auto refresh = model.Refresh();
        passed &= Check(refresh && refresh->target.IsThisPC() && model.Commit(*refresh),
            "location_this_pc_refresh_is_navigation_request");
        const auto to_folder = model.Navigate(folder_a);
        passed &= Check(to_folder && model.Commit(*to_folder),
            "location_this_pc_to_folder");
        const auto back = model.Back();
        passed &= Check(back && back->target.IsThisPC() && model.Commit(*back),
            "location_back_folder_to_this_pc");
        const auto forward = model.Forward();
        passed &= Check(forward && forward->target == folder_a && model.Commit(*forward),
            "location_forward_this_pc_to_folder");
        const auto refresh_folder = model.Refresh();
        const auto before_refresh = model.Snapshot();
        passed &= Check(refresh_folder && model.Commit(*refresh_folder) &&
                model.Snapshot().back_history == before_refresh.back_history &&
                model.Snapshot().forward_history == before_refresh.forward_history,
            "location_folder_refresh_does_not_add_history");
        return passed;
    }

    [[nodiscard]] bool RunThisPcContentTests()
    {
        const auto content = blink::app::EnumerateThisPcContent();
        bool has_place = false;
        bool has_volume = false;
        bool has_c = false;
        bool has_d = false;
        bool volume_targets_are_folders = true;

        for (const auto& item : content.items)
        {
            if (item.kind == blink::app::ThisPcItemKind::Place)
            {
                has_place = true;
                continue;
            }

            has_volume = true;
            volume_targets_are_folders &= item.target.IsFolder() &&
                item.target.IsValid() && !item.display_name.empty() &&
                item.display_name == item.target.FolderPath();
            has_c |= item.target == blink::app::Location::Folder(L"C:\\");
            has_d |= item.target == blink::app::Location::Folder(L"D:\\");
        }

        bool passed = true;
        passed &= Check(has_place, "this_pc_content_has_places");
        passed &= Check(has_volume, "this_pc_content_has_local_volumes");
        passed &= Check(has_c && has_d, "this_pc_content_contains_c_and_d_volumes");
        passed &= Check(volume_targets_are_folders,
            "this_pc_volume_items_use_folder_locations");

        blink::app::TabState stale_tab;
        const auto first = stale_tab.Refresh();
        const auto second = stale_tab.Refresh();
        passed &= Check(first && second,
            "this_pc_population_requests_have_generation_gate");
        if (first && second)
        {
            auto empty_content = blink::app::ThisPcContent{};
            passed &= Check(
                stale_tab.CommitThisPC(*first, std::move(empty_content)).status ==
                    blink::app::CommitBrowseStatus::StaleOrRejected,
                "stale_this_pc_population_rejected");
            passed &= Check(
                stale_tab.CommitThisPC(*second, content).status ==
                    blink::app::CommitBrowseStatus::Committed,
                "new_this_pc_population_committed");
            const auto committed = stale_tab.CommittedThisPCSnapshot();
            passed &= Check(committed.items.size() == content.items.size() &&
                    std::any_of(committed.items.begin(), committed.items.end(), [](const auto& item) {
                        return item.kind == blink::app::ThisPcItemKind::Volume &&
                            item.target == blink::app::Location::Folder(L"D:\\");
                    }),
                "stale_this_pc_population_cannot_erase_new_volume_result");
        }
        return passed;
    }

    [[nodiscard]] bool RunNavigationModelTests()
    {
        using blink::app::NavigationAction;
        using blink::app::NavigationModel;

        const std::filesystem::path root =
            std::filesystem::absolute(std::filesystem::path(L".build") /
                L"phase1-navigation-tests" /
                L"Blink中文导航_内蒙古_QWQ");
        const std::wstring path_a = (root / L"A").wstring();
        const std::wstring path_b = (root / L"B").wstring();
        const std::wstring path_c = (root / L"C").wstring();
        const std::wstring path_branch = (root / L"分支").wstring();

        bool passed = true;
        NavigationModel model;

        const auto initial = model.Navigate(path_a);
        passed &= Check(initial.has_value(), "navigation_initial_intent");
        passed &= Check(initial && initial->action == NavigationAction::Navigate,
            "navigation_initial_action");
        passed &= Check(model.Snapshot().committed_location.IsThisPC() &&
                model.Snapshot().committed_current_path.empty(),
            "navigation_initial_is_this_pc");
        passed &= Check(initial && model.IsPending(*initial),
            "navigation_initial_pending");
        passed &= Check(initial && model.Commit(*initial), "navigation_initial_commit");

        const std::wstring normalized_a = blink::core::NormalizePath(path_a);
        passed &= Check(model.Snapshot().committed_current_path == normalized_a,
            "navigation_initial_normalized");

        const auto to_b = model.Navigate(path_b);
        const auto to_c = model.Navigate(path_c);
        passed &= Check(to_b.has_value() && to_c.has_value() &&
                to_c->token > to_b->token,
            "navigation_rapid_intents_increment_token");
        passed &= Check(!model.Commit(*to_b), "navigation_older_intent_rejected");
        passed &= Check(model.Commit(*to_c), "navigation_latest_intent_commits");
        passed &= Check(model.Snapshot().can_go_back, "navigation_back_available");

        const auto back_to_b = model.Back();
        passed &= Check(back_to_b.has_value() &&
                back_to_b->action == NavigationAction::Back,
            "navigation_back_intent");
        passed &= Check(back_to_b && model.Snapshot().committed_current_path ==
                blink::core::NormalizePath(path_c),
            "navigation_back_does_not_mutate_before_commit");
        passed &= Check(back_to_b && model.Reject(*back_to_b),
            "navigation_failed_back_rejected");
        passed &= Check(model.Snapshot().committed_current_path ==
                blink::core::NormalizePath(path_c) &&
                model.Snapshot().can_go_back,
            "navigation_failed_back_preserves_history");

        const auto back_retry = model.Back();
        passed &= Check(back_retry && model.Commit(*back_retry),
            "navigation_back_commit");
        passed &= Check(model.Snapshot().can_go_forward, "navigation_forward_available");

        const auto forward_to_c = model.Forward();
        passed &= Check(forward_to_c.has_value() &&
                forward_to_c->action == NavigationAction::Forward,
            "navigation_forward_intent");
        passed &= Check(forward_to_c && model.Reject(*forward_to_c),
            "navigation_failed_forward_rejected");
        passed &= Check(model.Snapshot().can_go_forward,
            "navigation_failed_forward_preserves_history");

        const auto forward_retry = model.Forward();
        passed &= Check(forward_retry && model.Commit(*forward_retry),
            "navigation_forward_commit");

        const auto back_again = model.Back();
        passed &= Check(back_again && model.Commit(*back_again),
            "navigation_back_before_refresh");
        const auto refresh = model.Refresh();
        const auto before_refresh = model.Snapshot();
        passed &= Check(refresh && refresh->action == NavigationAction::Refresh,
            "navigation_refresh_intent");
        passed &= Check(refresh && model.Commit(*refresh), "navigation_refresh_commit");
        const auto after_refresh = model.Snapshot();
        passed &= Check(after_refresh.committed_current_path ==
                before_refresh.committed_current_path &&
                after_refresh.can_go_forward,
            "navigation_refresh_preserves_history");

        const auto branch = model.Navigate(path_branch);
        passed &= Check(branch && model.Commit(*branch), "navigation_branch_commit");
        passed &= Check(!model.Snapshot().can_go_forward,
            "navigation_branch_clears_forward_history");
        const std::wstring invalid_input(L"\0invalid", 8);
        const auto invalid = model.Navigate(invalid_input);
        passed &= Check(!invalid.has_value(), "navigation_invalid_input_rejected");
        passed &= Check(model.Snapshot().committed_current_path ==
                blink::core::NormalizePath(path_branch),
            "navigation_invalid_input_preserves_committed_path");

        NavigationModel up_model;
        const std::wstring nested = (root / L"parent" / L"child").wstring();
        const auto nested_request = up_model.Navigate(nested);
        passed &= Check(nested_request && up_model.Commit(*nested_request),
            "navigation_up_fixture_commit");
        const auto up_parent = up_model.Up();
        passed &= Check(up_parent && up_parent->target.FolderPath() ==
                blink::core::NormalizePath(root / L"parent"),
            "navigation_up_parent_path");
        passed &= Check(up_parent && up_model.Commit(*up_parent),
            "navigation_up_parent_commit");
        const auto up_root = up_model.Up();
        passed &= Check(up_root && up_root->target.FolderPath() == blink::core::NormalizePath(root),
            "navigation_up_root_path");
        passed &= Check(up_root && up_model.Commit(*up_root), "navigation_up_root_commit");
        NavigationModel root_model;
        const std::wstring volume_root = std::filesystem::path(root).root_path().wstring();
        const auto volume_root_request = root_model.Navigate(volume_root);
        passed &= Check(volume_root_request && root_model.Commit(*volume_root_request),
            "navigation_volume_root_commit");
        const auto root_up = root_model.Up();
        passed &= Check(root_up && root_up->target.IsThisPC(),
            "navigation_up_root_reaches_this_pc");
        passed &= Check(root_up && root_model.Commit(*root_up),
            "navigation_up_root_refreshes_without_failure");
        const auto root_after = root_model.Snapshot();
        passed &= Check(root_after.committed_location.IsThisPC() &&
                root_after.can_go_back &&
                !root_after.can_go_forward &&
                root_after.back_history.back().FolderPath() ==
                    blink::core::NormalizePath(volume_root),
            "navigation_up_this_pc_preserves_history");

        NavigationModel shutdown_model;
        const auto shutdown_request = shutdown_model.Navigate(L"C:\\phase1-shutdown");
        shutdown_model.Shutdown();
        passed &= Check(shutdown_request && !shutdown_model.Commit(*shutdown_request),
            "navigation_shutdown_rejects_pending");
        passed &= Check(!shutdown_model.Navigate(L"C:\\phase1-after-shutdown").has_value(),
            "navigation_shutdown_rejects_new_intent");

        return passed;
    }

    [[nodiscard]] bool RunNavigationPaneTests()
    {
        const std::vector<std::wstring> places{
            L"C:\\Users\\BlinkUser",
            L"C:\\Users\\BlinkUser\\Desktop",
            L"C:\\Users\\BlinkUser\\Downloads",
            L"C:\\Users\\BlinkUser\\Documents" };
        const std::vector<std::wstring> drives{ L"C:\\", L"D:\\" };
        bool passed = true;

        const auto downloads = blink::app::ResolveNavigationPaneSelection(
            L"c:\\users\\blinkuser\\downloads",
            places,
            drives);
        passed &= Check(downloads.place_index == 2 && downloads.drive_index == -1,
            "navigation_pane_exact_downloads_selects_place");

        const auto documents = blink::app::ResolveNavigationPaneSelection(
            L"C:\\Users\\BlinkUser\\Documents",
            places,
            drives);
        passed &= Check(documents.place_index == 3 && documents.drive_index == -1,
            "navigation_pane_exact_documents_selects_place");

        const auto ordinary_subfolder = blink::app::ResolveNavigationPaneSelection(
            L"C:\\Users\\BlinkUser\\Downloads\\Project",
            places,
            drives);
        passed &= Check(ordinary_subfolder.place_index == -1 &&
                ordinary_subfolder.drive_index == -1,
            "navigation_pane_subfolder_does_not_select_parent_place");

        const auto drive = blink::app::ResolveNavigationPaneSelection(
            L"d:\\",
            places,
            drives);
        passed &= Check(drive.place_index == -1 && drive.drive_index == 1,
            "navigation_pane_exact_drive_root_selects_drive");

        const auto unknown = blink::app::ResolveNavigationPaneSelection(
            L"C:\\Other",
            places,
            drives);
        passed &= Check(unknown.place_index == -1 && unknown.drive_index == -1,
            "navigation_pane_unknown_location_clears_selection");
        return passed;
    }

    [[nodiscard]] blink::core::FileIdentity TestIdentity(std::uint8_t seed);
    [[nodiscard]] blink::core::FileEntry TestEntry(
        const std::wstring& name,
        const blink::core::FileIdentity& identity,
        std::uint64_t size,
        bool directory);

    [[nodiscard]] bool RunFolderViewFormattingTests()
    {
        blink::core::FileEntry folder;
        folder.name = L"Folder";
        folder.is_directory = true;
        blink::core::FileEntry text_file;
        text_file.name = L"readme.txt";
        text_file.extension = L".txt";
        blink::core::FileEntry unknown_file;
        unknown_file.name = L"data.xyz";
        unknown_file.extension = L".xyz";

        bool passed = true;
        passed &= Check(blink::app::FormatFileSize(0) == L"0 B", "format_size_bytes");
        passed &= Check(blink::app::FormatFileSize(1024) == L"1 KB", "format_size_kilobytes");
        passed &= Check(blink::app::FormatFileSize(1536) == L"1.5 KB", "format_size_fraction");
        passed &= Check(!blink::app::FileTypeLabel(folder).empty(), "format_folder_type_localized");
        passed &= Check(!blink::app::FileTypeLabel(text_file).empty(), "format_text_type_localized");
        passed &= Check(blink::app::FileTypeLabel(unknown_file).find(L"xyz") != std::wstring::npos,
            "format_unknown_type_preserves_extension");

        SYSTEMTIME known_time{ 2024, 1, 0, 2, 12, 34, 0, 0 };
        FILETIME file_time{};
        const bool converted = SystemTimeToFileTime(&known_time, &file_time) != FALSE;
        const auto formatted_time = blink::app::FormatModifiedTime(file_time);
        passed &= Check(converted && formatted_time.size() == 16 &&
                formatted_time[4] == L'-' && formatted_time[7] == L'-' &&
                formatted_time[10] == L' ',
            "format_modified_time");

        blink::app::BrowseSnapshot loading;
        loading.state = blink::app::BrowseStateKind::Loading;
        loading.requested_path = L"D:\\Loading";
        blink::app::TabPresentationSnapshot tab;
        tab.has_pending = true;
        tab.committed_location = blink::app::Location::Folder(L"D:\\Committed");
        tab.committed_current_path = L"D:\\Committed";
        tab.committed_item_count = 3;
        tab.folder_state = blink::app::FolderState::Ready;
        const auto loading_status = blink::app::FormatBrowseActivity(loading, tab);
        passed &= Check(!loading_status.empty() &&
                loading_status.find(L"D:\\Loading") != std::wstring::npos,
            "format_loading_activity");

        blink::app::BrowseSnapshot failed;
        failed.state = blink::app::BrowseStateKind::Failed;
        failed.error = ERROR_PATH_NOT_FOUND;
        const auto failed_status = blink::app::FormatBrowseActivity(failed, tab);
        passed &= Check(!failed_status.empty() &&
                failed_status.find(L"3") != std::wstring::npos,
            "format_failed_activity_preserves_page_message");

        tab.folder_state = blink::app::FolderState::Empty;
        tab.has_pending = false;
        const auto empty_status = blink::app::FormatBrowseActivity(failed, tab);
        passed &= Check(!empty_status.empty() &&
                empty_status.find(L"3") != std::wstring::npos,
            "format_failed_activity_precedes_empty_content_state");

        blink::app::TabPresentationSnapshot this_pc_tab;
        this_pc_tab.committed_location = blink::app::Location::ThisPC();
        this_pc_tab.latest_browse_error = ERROR_ACCESS_DENIED;
        blink::app::BrowseSnapshot idle;
        const auto failed_this_pc_status = blink::app::FormatBrowseActivity(idle, this_pc_tab);
        passed &= Check(!failed_this_pc_status.empty() &&
                failed_this_pc_status.find(L"5") != std::wstring::npos,
            "format_failed_this_pc_preserves_content_message");

        const blink::app::Localization localization;
        const auto selected = localization.Format(L"Status.Selected", 3);
        passed &= Check(!localization.GetString(L"App.Title").empty() &&
                selected.find(L"3") != std::wstring::npos,
            "localization_resource_boundary_and_formatting");

        const auto identity_a = TestIdentity(21);
        const auto identity_b = TestIdentity(22);
        auto directory_a = TestEntry(L"A", identity_a, 0, true);
        directory_a.full_path = L"D:\\phase1-click\\A";
        auto directory_b = TestEntry(L"B", identity_b, 0, true);
        directory_b.full_path = L"D:\\phase1-click\\B";
        const auto clicked_rows = blink::app::BuildFolderRowProjection(
            std::vector<blink::core::FileEntry>{ directory_a, directory_b });
        const std::size_t clicked_row_index = 1;
        passed &= Check(clicked_rows.size() == 2 &&
                clicked_rows[clicked_row_index].identity == identity_b &&
                clicked_rows[clicked_row_index].full_path == L"D:\\phase1-click\\B",
            "clicked_item_resolution_maps_selected_a_b_double_click_b_to_b");

        // The activated row is the clicked row, not the first selected row.
        // This mirrors the production routing boundary before BrowseAsync.
        blink::app::NavigationModel activation_navigation;
        const auto activation_request = activation_navigation.Navigate(
            clicked_rows[clicked_row_index].full_path);
        passed &= Check(activation_request.has_value() &&
                activation_request->action == blink::app::NavigationAction::Navigate &&
                activation_request->target.FolderPath() == L"D:\\phase1-click\\B",
            "directory_activation_routes_clicked_row_to_navigation");

        const auto multi_selection_clicked_directory = clicked_rows[clicked_row_index];
        const std::vector<std::size_t> selected_rows{ 0 };
        passed &= Check(!selected_rows.empty() &&
                multi_selection_clicked_directory.is_directory &&
                multi_selection_clicked_directory.full_path == L"D:\\phase1-click\\B",
            "directory_activation_ignores_other_multi_selection");
        return passed;
    }

    [[nodiscard]] bool RunLargeFolderProjectionEvidence()
    {
        bool passed = true;
        for (const std::size_t count : { std::size_t{ 10000 }, std::size_t{ 50000 } })
        {
            std::vector<blink::core::FileEntry> entries;
            entries.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                auto entry = TestEntry(
                    L"file-" + std::to_wstring(index) + L".txt",
                    TestIdentity(static_cast<std::uint8_t>(index % 251 + 1)),
                    index * 17 + 1,
                    false);
                entry.extension = L".txt";
                entries.push_back(std::move(entry));
            }

            const auto projection_start = std::chrono::steady_clock::now();
            const auto rows = blink::app::BuildFolderRowProjection(entries);
            const auto projection_end = std::chrono::steady_clock::now();

            std::vector<blink::core::FileEntry> sort_input = entries;
            const auto sort_start = std::chrono::steady_clock::now();
            std::stable_sort(sort_input.begin(), sort_input.end(),
                [](const auto& left, const auto& right) {
                    return left.name < right.name;
                });
            const auto sort_end = std::chrono::steady_clock::now();

            const auto projection_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                projection_end - projection_start).count();
            const auto sort_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                sort_end - sort_start).count();
            std::cout << "large_folder_projection count=" << count
                      << " projection_ms=" << projection_ms
                      << " sort_ms=" << sort_ms
                      << " row_models=" << rows.size() << "\n";
            passed &= Check(rows.size() == count, count == 10000 ?
                "large_folder_projection_10k" : "large_folder_projection_50k");
        }
        return passed;
    }

    [[nodiscard]] blink::core::FileIdentity TestIdentity(std::uint8_t seed)
    {
        blink::core::FileIdentity identity;
        identity.valid = true;
        identity.volume_serial = 0x12340000 + seed;
        identity.file_id[0] = seed;
        identity.file_id[15] = static_cast<std::uint8_t>(0xff - seed);
        return identity;
    }

    [[nodiscard]] blink::core::FileEntry TestEntry(
        const std::wstring& name,
        const blink::core::FileIdentity& identity,
        std::uint64_t size,
        bool directory = false)
    {
        blink::core::FileEntry entry;
        entry.name = name;
        entry.full_path = L"C:\\phase1-tab\\" + name;
        entry.identity = identity;
        entry.size = size;
        entry.is_directory = directory;
        entry.last_write_time.dwLowDateTime = static_cast<DWORD>(size);
        return entry;
    }

    [[nodiscard]] bool CommitSucceeded(const blink::app::CommitBrowseResult& result)
    {
        return result.status == blink::app::CommitBrowseStatus::Committed &&
            result.error == ERROR_SUCCESS;
    }

    [[nodiscard]] bool RunTabStateTests()
    {
        using blink::app::FolderState;
        using blink::app::SortDescriptor;
        using blink::app::SortDirection;
        using blink::app::SortField;
        using blink::app::TabState;

        const std::wstring path_a = L"C:\\phase1-tab\\Blink中文目录_A";
        const std::wstring path_b = L"C:\\phase1-tab\\B";
        const auto identity_a = TestIdentity(1);
        const auto identity_b = TestIdentity(2);
        const auto identity_same_name = TestIdentity(3);
        const auto entry_a = TestEntry(L"同名.txt", identity_a, 10);
        const auto entry_b = TestEntry(L"同名.txt", identity_same_name, 20);
        const auto entry_c = TestEntry(L"large.bin", identity_b, 30);

        auto listing_a = Listing(path_a);
        listing_a.entries = { entry_a, entry_b, entry_c };
        auto listing_a_refreshed = Listing(path_a);
        listing_a_refreshed.entries = { entry_a, entry_b, entry_c };
        listing_a_refreshed.entries[0].size = 99;

        bool passed = true;
        TabState tab;
        const auto initial = tab.Navigate(path_a);
        passed &= Check(initial.has_value(), "tab_initial_navigation_intent");
        passed &= Check(initial && CommitSucceeded(tab.CommitBrowse(*initial, listing_a)),
            "tab_initial_committed_page");
        {
            const auto snapshot = tab.Snapshot();
            passed &= Check(snapshot.navigation.committed_current_path ==
                    blink::core::NormalizePath(path_a) &&
                    snapshot.committed_listing.entries.size() == 3 &&
                    snapshot.folder_state == FolderState::Ready,
                "tab_path_listing_commit_is_consistent");
        }

        const auto pending_navigation = tab.Navigate(path_b);
        const auto loading_snapshot = tab.Snapshot();
        passed &= Check(pending_navigation.has_value() &&
                loading_snapshot.folder_state == FolderState::Ready &&
                loading_snapshot.navigation.committed_current_path ==
                    blink::core::NormalizePath(path_a) &&
                loading_snapshot.committed_listing.entries.size() == 3,
            "tab_browse_loading_does_not_change_committed_page");
        passed &= Check(pending_navigation &&
                tab.RejectBrowse(*pending_navigation, ERROR_PATH_NOT_FOUND),
            "tab_failed_navigate_rejected");
        {
            const auto failed_navigation_snapshot = tab.Snapshot();
            passed &= Check(
                failed_navigation_snapshot.folder_state == FolderState::Ready &&
                failed_navigation_snapshot.navigation.committed_current_path ==
                    blink::core::NormalizePath(path_a) &&
                failed_navigation_snapshot.committed_listing.entries.size() == 3 &&
                failed_navigation_snapshot.latest_browse_error == ERROR_PATH_NOT_FOUND,
                "tab_failed_navigate_preserves_ready_page");
        }

        passed &= Check(tab.SelectEntry(entry_a), "tab_selects_valid_identity");
        passed &= Check(tab.IsSelected(identity_a), "tab_selection_uses_identity");
        tab.SetSelection({ identity_a, identity_same_name, {}, identity_a });
        passed &= Check(tab.Snapshot().selection.size() == 2 &&
                tab.IsSelected(identity_a) && tab.IsSelected(identity_same_name),
            "tab_multi_selection_maps_valid_identities");
        const auto refresh = tab.Refresh();
        passed &= Check(refresh && CommitSucceeded(tab.CommitBrowse(*refresh, listing_a_refreshed)),
            "tab_success_refresh_commits_listing");
        {
            const auto snapshot = tab.Snapshot();
            passed &= Check(snapshot.navigation.committed_current_path ==
                    blink::core::NormalizePath(path_a) &&
                snapshot.navigation.back_history.size() == 1 &&
                snapshot.navigation.back_history.front().IsThisPC() &&
                    snapshot.navigation.forward_history.empty() &&
                    snapshot.committed_listing.entries[0].size == 99 &&
                    tab.IsSelected(identity_a),
                "tab_refresh_preserves_path_history_and_selection");
        }

        auto listing_without_a = Listing(path_a);
        listing_without_a.entries = { entry_b, entry_c };
        const auto remove_refresh = tab.Refresh();
        passed &= Check(remove_refresh && CommitSucceeded(tab.CommitBrowse(*remove_refresh, listing_without_a)),
            "tab_refresh_commits_removed_item");
        passed &= Check(!tab.IsSelected(identity_a), "tab_selection_removed_when_identity_missing");

        const auto to_b = tab.Navigate(path_b);
        auto listing_b = Listing(path_b);
        listing_b.entries = { TestEntry(L"中文文件.txt", identity_b, 7) };
        passed &= Check(to_b && CommitSucceeded(tab.CommitBrowse(*to_b, listing_b)),
            "tab_second_path_committed");
        const auto before_failed_back = tab.Snapshot();
        const auto failed_back = tab.Back();
        passed &= Check(failed_back && tab.RejectBrowse(*failed_back, ERROR_ACCESS_DENIED),
            "tab_failed_back_rejected");
        const auto after_failed_back = tab.Snapshot();
        passed &= Check(after_failed_back.navigation.committed_current_path ==
                before_failed_back.navigation.committed_current_path &&
                after_failed_back.committed_listing.entries.size() ==
                before_failed_back.committed_listing.entries.size() &&
                after_failed_back.navigation.can_go_back,
            "tab_failed_back_preserves_page_and_history");

        const auto successful_back = tab.Back();
        passed &= Check(successful_back && CommitSucceeded(tab.CommitBrowse(*successful_back, listing_a)),
            "tab_successful_back_committed");
        const auto failed_forward = tab.Forward();
        auto before_failed_forward = tab.Snapshot();
        passed &= Check(failed_forward && tab.RejectBrowse(*failed_forward, ERROR_PATH_NOT_FOUND),
            "tab_failed_forward_rejected");
        const auto after_failed_forward = tab.Snapshot();
        passed &= Check(after_failed_forward.navigation.committed_current_path ==
                before_failed_forward.navigation.committed_current_path &&
                after_failed_forward.committed_listing.entries.size() ==
                before_failed_forward.committed_listing.entries.size() &&
                after_failed_forward.navigation.can_go_forward,
            "tab_failed_forward_preserves_page_and_history");

        const auto failed_refresh = tab.Refresh();
        const auto old_listing_size = tab.Snapshot().committed_listing.entries.size();
        passed &= Check(failed_refresh && tab.RejectBrowse(*failed_refresh, ERROR_SHARING_VIOLATION),
            "tab_failed_refresh_rejected");
        const auto failed_refresh_snapshot = tab.Snapshot();
        passed &= Check(failed_refresh_snapshot.committed_listing.entries.size() == old_listing_size &&
                failed_refresh_snapshot.latest_browse_error == ERROR_SHARING_VIOLATION &&
                failed_refresh_snapshot.folder_state == FolderState::Ready,
            "tab_failed_refresh_preserves_listing_and_exposes_error");

        TabState empty_tab;
        const std::wstring empty_path = L"C:\\phase1-tab\\empty";
        auto empty_listing = Listing(empty_path);
        const auto empty_navigation = empty_tab.Navigate(empty_path);
        passed &= Check(empty_navigation &&
                CommitSucceeded(empty_tab.CommitBrowse(*empty_navigation, empty_listing)) &&
                empty_tab.Snapshot().folder_state == FolderState::Empty,
            "tab_successful_empty_navigation_commits_empty_state");
        const auto failed_empty_refresh = empty_tab.Refresh();
        passed &= Check(failed_empty_refresh &&
                empty_tab.RejectBrowse(*failed_empty_refresh, ERROR_ACCESS_DENIED),
            "tab_empty_refresh_failure_rejected");
        {
            const auto snapshot = empty_tab.Snapshot();
            passed &= Check(snapshot.folder_state == FolderState::Empty &&
                    snapshot.committed_listing.entries.empty() &&
                    snapshot.navigation.committed_current_path ==
                        blink::core::NormalizePath(empty_path) &&
                    snapshot.latest_browse_error == ERROR_ACCESS_DENIED,
                "tab_failed_empty_refresh_preserves_empty_page");
        }

        passed &= Check(blink::app::CommitBrowseErrorCode(
                    blink::app::CommitBrowseStatus::OutOfMemory) == ERROR_NOT_ENOUGH_MEMORY &&
                blink::app::CommitBrowseErrorCode(
                    blink::app::CommitBrowseStatus::StaleOrRejected) == ERROR_SUCCESS &&
                blink::app::CommitBrowseErrorCode(
                    blink::app::CommitBrowseStatus::Failed) == ERROR_GEN_FAILURE,
            "tab_commit_result_error_mapping");

        const auto presentation = tab.PresentationSnapshot();
        const auto full_snapshot = tab.Snapshot();
        passed &= Check(presentation.committed_item_count ==
                full_snapshot.committed_listing.entries.size() &&
                presentation.selected_count == full_snapshot.selection.size() &&
                presentation.committed_current_path ==
                    full_snapshot.navigation.committed_current_path,
            "tab_presentation_snapshot_has_lightweight_page_fields");

        tab.SetSortDescriptor(SortDescriptor{ SortField::Size, SortDirection::Descending });
        const auto sorted = tab.SortedEntries();
        passed &= Check(sorted.size() == 3 && sorted[0].size == 30 &&
                tab.IsSelected(identity_a) == false,
            "tab_sort_changes_projection_without_identity_selection_change");
        passed &= Check(sorted[1].name == L"同名.txt" && sorted[2].name == L"同名.txt" &&
                sorted[1].identity != sorted[2].identity,
            "tab_same_name_entries_keep_distinct_identity");

        const auto hard_link_identity = TestIdentity(4);
        const auto hard_link_a = TestEntry(L"hard-link-a.txt", hard_link_identity, 41);
        const auto hard_link_b = TestEntry(L"hard-link-b.txt", hard_link_identity, 41);
        auto hard_link_listing = Listing(path_a);
        hard_link_listing.directory_identity = TestIdentity(41);
        hard_link_listing.entries = { hard_link_a, hard_link_b };
        TabState hard_link_tab;
        const auto hard_link_navigation = hard_link_tab.Navigate(path_a);
        passed &= Check(hard_link_navigation &&
                CommitSucceeded(hard_link_tab.CommitBrowse(
                    *hard_link_navigation, hard_link_listing)),
            "tab_hard_link_page_committed");
        passed &= Check(hard_link_tab.SelectEntry(hard_link_b),
            "tab_select_real_hard_link_row");
        const auto hard_link_snapshot = hard_link_tab.Snapshot();
        const auto selected_hard_link = hard_link_tab.SelectedCommittedRow(
            blink::app::MakeRowIdentity(hard_link_b));
        passed &= Check(hard_link_snapshot.selection.size() == 1 &&
                !hard_link_tab.IsRowSelected(blink::app::MakeRowIdentity(hard_link_a)) &&
                hard_link_tab.IsRowSelected(blink::app::MakeRowIdentity(hard_link_b)) &&
                selected_hard_link.has_value() &&
                selected_hard_link->name == hard_link_b.name,
            "tab_hard_link_rows_keep_independent_selection_targets");

        const auto ghost_hard_link = TestEntry(
            L"ghost-hard-link.txt", hard_link_identity, 41);
        const bool ghost_selected = hard_link_tab.SelectEntry(ghost_hard_link);
        const auto ghost_snapshot = hard_link_tab.Snapshot();
        passed &= Check(!ghost_selected && ghost_snapshot.selection.empty() &&
                !hard_link_tab.IsRowSelected(blink::app::MakeRowIdentity(hard_link_a)) &&
                !hard_link_tab.IsRowSelected(blink::app::MakeRowIdentity(hard_link_b)),
            "tab_rejects_ghost_hard_link_row_without_identity_alias");

        const auto renamed_entry = TestEntry(L"hard-link-z.txt", hard_link_identity, 41);
        const auto renamed_row = blink::app::MakeRowIdentity(renamed_entry);
        const auto sibling_row = blink::app::MakeRowIdentity(hard_link_a);
        auto renamed_listing = hard_link_listing;
        renamed_listing.entries[1] = renamed_entry;
        for (const bool watcher_first : { false, true })
        {
            TabState rename_tab;
            const auto rename_navigation = rename_tab.Navigate(path_a);
            bool ready = rename_navigation && CommitSucceeded(rename_tab.CommitBrowse(*rename_navigation, hard_link_listing)) &&
                rename_tab.SelectEntry(hard_link_b);
            if (watcher_first && ready)
            {
                const auto watcher = rename_tab.Refresh();
                ready = watcher && CommitSucceeded(rename_tab.CommitBrowse(*watcher, renamed_listing)) &&
                    rename_tab.Snapshot().selection.empty();
            }
            const auto rename_refresh = rename_tab.Refresh();
            const bool committed = rename_refresh && CommitSucceeded(rename_tab.CommitBrowse(*rename_refresh, renamed_listing, &renamed_row));
            const auto page = rename_tab.Snapshot();
            passed &= Check(ready && committed && page.selection == std::vector{ renamed_row } &&
                    page.focused_item == renamed_row && !rename_tab.IsRowSelected(sibling_row),
                watcher_first ? "rename_refresh_restores_after_watcher_drops_old_row" : "rename_refresh_selects_new_link_not_same_file_sibling");
        }

        const auto retains_sibling = [&](blink::core::DirectoryListing listing) {
            TabState rename_tab;
            const auto rename_navigation = rename_tab.Navigate(path_a);
            if (!rename_navigation || !CommitSucceeded(rename_tab.CommitBrowse(*rename_navigation, hard_link_listing)) ||
                !rename_tab.SelectEntry(hard_link_a)) { return false; }
            const auto rename_refresh = rename_tab.Refresh();
            if (!rename_refresh || !CommitSucceeded(rename_tab.CommitBrowse(*rename_refresh, std::move(listing), &renamed_row))) { return false; }
            const auto page = rename_tab.Snapshot();
            return page.selection == std::vector{ sibling_row } && page.focused_item == sibling_row;
        };
        auto replacement = renamed_listing;
        replacement.entries[1].identity = TestIdentity(42);
        passed &= Check(retains_sibling(std::move(replacement)), "rename_refresh_does_not_select_a_replacement_with_the_same_name");
        auto hidden = renamed_listing;
        hidden.entries[1].attributes |= FILE_ATTRIBUTE_HIDDEN;
        passed &= Check(retains_sibling(std::move(hidden)), "rename_refresh_leaves_selection_when_destination_is_filtered");
        auto different_parent = renamed_listing;
        different_parent.directory_identity = TestIdentity(43);
        passed &= Check(retains_sibling(std::move(different_parent)), "rename_refresh_does_not_select_in_a_replacement_directory");

        auto invalid_listing = Listing(path_a);
        invalid_listing.entries = {
            TestEntry(L"invalid-1", {}, 1),
            TestEntry(L"invalid-1", {}, 2) };
        const auto invalid_navigation = tab.Navigate(path_a);
        passed &= Check(invalid_navigation && CommitSucceeded(tab.CommitBrowse(*invalid_navigation, invalid_listing)),
            "tab_invalid_identity_listing_commits");
        passed &= Check(tab.SortedEntries().size() == 2 &&
                !tab.SelectEntry(tab.SortedEntries().front()) &&
                tab.Snapshot().selection.empty(),
            "tab_invalid_identities_do_not_alias_selection");
        tab.SetSelection({ {}, identity_a });
        passed &= Check(tab.Snapshot().selection.empty(),
            "tab_mixed_invalid_selection_is_discarded");

        TabState rapid_tab;
        const auto rapid_a = rapid_tab.Navigate(path_a);
        const auto rapid_b = rapid_tab.Navigate(path_b);
        passed &= Check(rapid_a && rapid_b &&
                CommitSucceeded(rapid_tab.CommitBrowse(*rapid_b, listing_b)),
            "tab_rapid_latest_commit");
        blink::app::CommitBrowseResult stale_result{
            blink::app::CommitBrowseStatus::StaleOrRejected,
            ERROR_SUCCESS };
        if (rapid_a.has_value())
        {
            stale_result = rapid_tab.CommitBrowse(*rapid_a, listing_a);
        }
        passed &= Check(rapid_a &&
                stale_result.status == blink::app::CommitBrowseStatus::StaleOrRejected &&
                stale_result.error != ERROR_NOT_ENOUGH_MEMORY,
            "tab_rapid_late_result_dropped");
        passed &= Check(rapid_tab.Snapshot().navigation.committed_current_path ==
                blink::core::NormalizePath(path_b) &&
                rapid_tab.Snapshot().committed_listing.entries.size() == 1,
            "tab_rapid_committed_page_stays_latest");

        return passed;
    }

    [[nodiscard]] bool RunFolderViewStateTests()
    {
        using blink::app::FolderViewMode;
        using blink::app::Location;
        using blink::app::TabState;
        using blink::app::TabWindowState;

        bool passed = true;
        TabState tab;
        passed &= Check(tab.GetViewMode() == FolderViewMode::Details &&
                tab.Snapshot().view_mode == FolderViewMode::Details &&
                tab.PresentationSnapshot().view_mode == FolderViewMode::Details,
            "view_defaults_to_details_in_state_and_snapshots");

        const std::wstring path_a = L"C:\\phase1-view\\A";
        const std::wstring path_b = L"C:\\phase1-view\\中文目录_B";
        const std::wstring path_c = L"C:\\phase1-view\\C";
        auto listing_a = Listing(path_a);
        listing_a.entries = { TestEntry(L"initial.txt", TestIdentity(81), 12) };
        auto listing_b = Listing(path_b);
        const auto hard_link_identity = TestIdentity(82);
        listing_b.entries = {
            TestEntry(L"hard-link-a.txt", hard_link_identity, 42),
            TestEntry(L"hard-link-b.txt", hard_link_identity, 42) };
        auto listing_c = Listing(path_c);
        listing_c.entries = { TestEntry(L"next.txt", TestIdentity(83), 24) };
        const auto request_a = tab.Navigate(path_a);
        passed &= Check(request_a && CommitSucceeded(tab.CommitBrowse(*request_a, listing_a)),
            "view_fixture_first_page_committed");
        const auto request_b = tab.Navigate(path_b);
        passed &= Check(request_b && CommitSucceeded(tab.CommitBrowse(*request_b, listing_b)),
            "view_fixture_second_page_has_history");
        const auto history_c = tab.Navigate(path_c);
        passed &= Check(history_c && CommitSucceeded(tab.CommitBrowse(*history_c, listing_c)),
            "view_fixture_forward_page_committed");
        const auto history_back = tab.Back();
        passed &= Check(history_back && CommitSucceeded(tab.CommitBrowse(*history_back, listing_b)) &&
                tab.Snapshot().navigation.can_go_back &&
                tab.Snapshot().navigation.can_go_forward,
            "view_fixture_has_both_back_and_forward_history");
        tab.SetRowSelection({
            blink::app::MakeRowIdentity(listing_b.entries[0]),
            blink::app::MakeRowIdentity(listing_b.entries[1]) });
        tab.SetSortDescriptor({ blink::app::SortField::Size,
            blink::app::SortDirection::Descending });
        const auto pending = tab.Navigate(path_c);
        passed &= Check(pending.has_value(), "view_fixture_navigation_is_pending");
        const auto before = tab.Snapshot();

        const auto same_navigation = [](const auto& left, const auto& right) {
            return left.committed_location == right.committed_location &&
                left.committed_current_path == right.committed_current_path &&
                left.back_history == right.back_history &&
                left.forward_history == right.forward_history &&
                left.can_go_back == right.can_go_back &&
                left.can_go_forward == right.can_go_forward &&
                left.has_pending == right.has_pending &&
                left.pending_token == right.pending_token &&
                left.pending_location == right.pending_location;
        };
        const auto same_entry = [](const auto& left, const auto& right) {
            return left.name == right.name && left.full_path == right.full_path &&
                left.extension == right.extension && left.identity == right.identity &&
                left.is_directory == right.is_directory && left.size == right.size &&
                left.allocated_size == right.allocated_size &&
                left.attributes == right.attributes && left.reparse_tag == right.reparse_tag &&
                left.creation_time.dwLowDateTime == right.creation_time.dwLowDateTime &&
                left.creation_time.dwHighDateTime == right.creation_time.dwHighDateTime &&
                left.last_access_time.dwLowDateTime == right.last_access_time.dwLowDateTime &&
                left.last_access_time.dwHighDateTime == right.last_access_time.dwHighDateTime &&
                left.last_write_time.dwLowDateTime == right.last_write_time.dwLowDateTime &&
                left.last_write_time.dwHighDateTime == right.last_write_time.dwHighDateTime;
        };
        bool preserved = true;
        for (const auto mode : { FolderViewMode::List, FolderViewMode::Grid,
                FolderViewMode::Details, FolderViewMode::Grid })
        {
            tab.SetViewMode(mode);
            const auto after = tab.Snapshot();
            const auto presentation = tab.PresentationSnapshot();
            preserved &= tab.GetViewMode() == mode && after.view_mode == mode &&
                presentation.view_mode == mode &&
                same_navigation(before.navigation, after.navigation) &&
                before.committed_listing.path == after.committed_listing.path &&
                before.committed_listing.error == after.committed_listing.error &&
                before.committed_listing.used_extended_directory_info ==
                    after.committed_listing.used_extended_directory_info &&
                std::equal(before.committed_listing.entries.begin(),
                    before.committed_listing.entries.end(),
                    after.committed_listing.entries.begin(),
                    after.committed_listing.entries.end(), same_entry) &&
                before.folder_state == after.folder_state &&
                before.latest_browse_error == after.latest_browse_error &&
                before.selection == after.selection &&
                before.focused_item == after.focused_item && before.sort == after.sort &&
                presentation.selected_count == before.selection.size() &&
                pending && tab.IsPending(*pending);
        }
        passed &= Check(preserved,
            "view_switch_preserves_committed_rows_hardlink_selection_focus_sort_history_and_pending");
        passed &= Check(pending && CommitSucceeded(tab.CommitBrowse(*pending, listing_c)) &&
                tab.GetViewMode() == FolderViewMode::Grid,
            "view_mode_survives_pending_navigation_commit");
        passed &= Check(tab.SelectEntry(listing_c.entries.front()),
            "view_refresh_fixture_selects_committed_row");
        const auto refresh = tab.Refresh();
        tab.SetViewMode(FolderViewMode::List);
        passed &= Check(refresh && CommitSucceeded(tab.CommitBrowse(*refresh, listing_c)) &&
                tab.GetViewMode() == FolderViewMode::List &&
                tab.IsRowSelected(blink::app::MakeRowIdentity(listing_c.entries.front())),
            "view_mode_survives_refresh_with_selection");
        const auto back = tab.Back();
        passed &= Check(back && CommitSucceeded(tab.CommitBrowse(*back, listing_b)) &&
                tab.GetViewMode() == FolderViewMode::List,
            "view_mode_survives_history_navigation");
        const auto this_pc = tab.Navigate(Location::ThisPC());
        passed &= Check(this_pc && CommitSucceeded(tab.CommitThisPC(*this_pc, {})) &&
                tab.GetViewMode() == FolderViewMode::List,
            "view_mode_survives_this_pc_commit");
        const auto rejected = tab.Navigate(path_a);
        tab.SetViewMode(FolderViewMode::Grid);
        passed &= Check(rejected && tab.RejectBrowse(*rejected, ERROR_ACCESS_DENIED) &&
                tab.GetViewMode() == FolderViewMode::Grid &&
                tab.PresentationSnapshot().committed_location.IsThisPC(),
            "view_mode_survives_rejected_navigation");

        TabWindowState window;
        const auto tab_a = window.ActiveTabId();
        const auto tab_b = window.CreateTab();
        window.Find(tab_a)->state->SetViewMode(FolderViewMode::Grid);
        window.Find(tab_b)->state->SetViewMode(FolderViewMode::List);
        passed &= Check(window.ActivateTab(tab_b) &&
                window.Find(tab_b)->state->PresentationSnapshot().view_mode == FolderViewMode::List &&
                window.ActivateTab(tab_a) &&
                window.Find(tab_a)->state->PresentationSnapshot().view_mode == FolderViewMode::Grid &&
                window.Find(tab_b)->state->GetViewMode() == FolderViewMode::List,
            "view_modes_are_isolated_when_switching_tabs");
        const auto tab_c = window.CreateTab();
        passed &= Check(window.Find(tab_c)->state->GetViewMode() == FolderViewMode::Details &&
                window.Find(tab_a)->state->GetViewMode() == FolderViewMode::Grid,
            "new_tab_details_default_does_not_inherit_other_tab_view");
        return passed;
    }

    [[nodiscard]] bool RunTypeSortTests()
    {
        using blink::app::SortDirection;
        using blink::app::SortField;
        using blink::app::TabState;

        bool passed = true;
        std::vector<blink::core::FileEntry> entries;
        std::uint8_t seed = 100;
        const auto add = [&](const std::wstring& name, const bool directory = false) {
            auto entry = TestEntry(name, TestIdentity(seed++), 10, directory);
            const auto dot = name.find_last_of(L'.');
            if (!directory && dot != std::wstring::npos)
            {
                // Keep original extension case to exercise formatter semantics.
                entry.extension = name.substr(dot);
            }
            entries.push_back(std::move(entry));
        };
        add(L"zeta.TXT");
        add(L"Z-folder", true);
        add(L"unicode.资料");
        add(L"源码.HPP");
        add(L"image.PNG");
        add(L"alpha.log");
        add(L"custom-b.QQ");
        add(L"archive.ZIP");
        add(L"A-folder", true);
        add(L"photo.jpeg");
        add(L"Beta.cpp");
        add(L"README");
        add(L"app.ExE");
        add(L"custom-a.qq");

        const auto names = [](const auto& sorted) {
            std::vector<std::wstring> result;
            for (const auto& entry : sorted)
            {
                result.push_back(entry.name);
            }
            return result;
        };
        const std::vector<std::wstring> ascending_names{
            L"A-folder", L"Z-folder", L"app.ExE", L"Beta.cpp", L"源码.HPP",
            L"archive.ZIP", L"README", L"image.PNG", L"photo.jpeg",
            L"alpha.log", L"zeta.TXT", L"custom-a.qq", L"custom-b.QQ", L"unicode.资料" };
        const std::vector<std::wstring> descending_names{
            L"Z-folder", L"A-folder", L"unicode.资料", L"custom-b.QQ", L"custom-a.qq",
            L"zeta.TXT", L"alpha.log", L"photo.jpeg", L"image.PNG", L"README",
            L"archive.ZIP", L"源码.HPP", L"Beta.cpp", L"app.ExE" };
        const auto ascending = TabState::SortEntries(entries,
            { SortField::Type, SortDirection::Ascending });
        const auto descending = TabState::SortEntries(entries,
            { SortField::Type, SortDirection::Descending });
        passed &= Check(names(ascending) == ascending_names,
            "type_sort_ascending_groups_display_labels_and_ties_by_name");
        passed &= Check(names(descending) == descending_names,
            "type_sort_descending_keeps_directories_first_and_reverses_name_ties");
        passed &= Check(blink::app::FileTypeLabel(entries[0]) ==
                blink::app::FileTypeLabel(entries[5]) &&
                blink::app::FileTypeLabel(entries[3]) ==
                    blink::app::FileTypeLabel(entries[10]) &&
                blink::app::FileTypeLabel(entries[6]) ==
                    blink::app::FileTypeLabel(entries[13]),
            "type_sort_uses_shared_categories_and_case_insensitive_extension_semantics");
        const bool identities_preserved = std::all_of(ascending.begin(), ascending.end(),
            [&](const auto& sorted) {
                const auto original = std::find_if(entries.begin(), entries.end(),
                    [&](const auto& entry) { return entry.name == sorted.name; });
                return original != entries.end() && original->identity == sorted.identity &&
                    original->full_path == sorted.full_path &&
                    original->extension == sorted.extension;
            });
        passed &= Check(identities_preserved, "type_sort_preserves_file_identity_path_and_extension");

        const auto duplicate_a = TestEntry(L"same.bin", TestIdentity(121), 10);
        auto duplicate_b = TestEntry(L"same.bin", TestIdentity(122), 20);
        duplicate_b.extension = L".bin";
        auto duplicate_first = duplicate_a;
        duplicate_first.extension = L".BIN";
        bool stable_ties = true;
        for (const auto direction : { SortDirection::Ascending, SortDirection::Descending })
        {
            const auto duplicates = TabState::SortEntries({ duplicate_first, duplicate_b },
                { SortField::Type, direction });
            stable_ties &= duplicates.size() == 2 &&
                duplicates[0].identity == duplicate_first.identity &&
                duplicates[1].identity == duplicate_b.identity;
        }
        passed &= Check(stable_ties, "type_sort_equal_type_and_name_ties_remain_stable");
        passed &= Check(TabState::SortEntries({},
                { SortField::Type, SortDirection::Ascending }).empty(),
            "type_sort_handles_empty_listing");

        TabState tab;
        auto listing = Listing(L"C:\\phase1-type-sort");
        listing.entries = entries;
        const auto request = tab.Navigate(listing.path);
        passed &= Check(request && CommitSucceeded(tab.CommitBrowse(*request, listing)) &&
                tab.SelectEntry(entries[3]), "type_sort_state_fixture_committed_and_selected");
        const auto before = tab.Snapshot();
        tab.SetSortDescriptor({ SortField::Type, SortDirection::Descending });
        const auto projection = tab.SortedEntries();
        const auto after = tab.Snapshot();
        passed &= Check(names(projection) == descending_names &&
                before.selection == after.selection && before.focused_item == after.focused_item &&
                before.navigation.committed_location == after.navigation.committed_location &&
                before.navigation.back_history == after.navigation.back_history &&
                before.navigation.forward_history == after.navigation.forward_history &&
                names(before.committed_listing.entries) == names(after.committed_listing.entries),
            "type_sort_changes_projection_without_selection_history_or_listing_mutation");
        return passed;
    }

    [[nodiscard]] bool RunTabWindowStateTests()
    {
        using blink::app::CloseTabStatus;
        using blink::app::Location;
        using blink::app::SortDirection;
        using blink::app::SortField;
        using blink::app::TabWindowState;

        bool passed = true;
        {
            TabWindowState command_state;
            const auto initial_command_tab = command_state.ActiveTabId();
            const auto created_command_tab = command_state.CreateTab();
            passed &= Check(command_state.Size() == 2 &&
                    created_command_tab != initial_command_tab,
                "one_create_tab_command_adds_one_tab");
            passed &= Check(command_state.ActivateTab(created_command_tab) &&
                    command_state.CloseTab(created_command_tab) == CloseTabStatus::Closed &&
                    command_state.Size() == 1 &&
                    command_state.ActiveTabId() == initial_command_tab,
                "one_close_tab_command_removes_one_tab");
        }

        TabWindowState window_state;
        const auto tab_a = window_state.ActiveTabId();
        passed &= Check(window_state.Size() == 1, "tabs_start_with_one_tab");
        passed &= Check(window_state.Find(tab_a) != nullptr &&
                window_state.Find(tab_a)->state->PresentationSnapshot().committed_location.IsThisPC(),
            "tab_a_starts_at_this_pc");

        const auto tab_b = window_state.CreateTab();
        const auto tab_c = window_state.CreateTab();
        passed &= Check(tab_a != tab_b && tab_b != tab_c && tab_a != tab_c,
            "tabs_have_stable_unique_ids");
        passed &= Check(window_state.Size() == 3 && window_state.ActiveTabId() == tab_a,
            "creating_tab_does_not_mutate_active_tab");
        passed &= Check(window_state.Find(tab_b)->state->PresentationSnapshot().committed_location.IsThisPC(),
            "new_tab_starts_at_this_pc");

        const std::wstring folder_a = L"C:\\phase1-tabs\\A";
        const std::wstring folder_b = L"C:\\phase1-tabs\\B";
        auto listing_a = Listing(folder_a);
        listing_a.entries = { TestEntry(L"tab-a-entry.txt", TestIdentity(101), 1) };
        auto listing_b = Listing(folder_b);
        listing_b.entries = { TestEntry(L"tab-b-entry.txt", TestIdentity(102), 2) };
        const auto request_a = window_state.Find(tab_a)->state->Navigate(folder_a);
        const auto request_b = window_state.Find(tab_b)->state->Navigate(folder_b);
        passed &= Check(request_a.has_value() && request_b.has_value(),
            "tab_navigation_requests_are_independent");
        if (request_a && request_b)
        {
            passed &= Check(window_state.Find(tab_a)->state->CommitBrowse(
                    *request_a, listing_a).status ==
                    blink::app::CommitBrowseStatus::Committed,
                "tab_a_commits_its_folder");
            passed &= Check(window_state.Find(tab_b)->state->CommitBrowse(
                    *request_b, listing_b).status ==
                    blink::app::CommitBrowseStatus::Committed,
                "tab_b_commits_its_folder");
        }
        window_state.Find(tab_a)->state->SetSortDescriptor(
            { SortField::Name, SortDirection::Ascending });
        window_state.Find(tab_b)->state->SetSortDescriptor(
            { SortField::Modified, SortDirection::Descending });
        passed &= Check(window_state.Find(tab_a)->state->PresentationSnapshot().committed_location ==
                Location::Folder(folder_a) &&
                window_state.Find(tab_b)->state->PresentationSnapshot().committed_location ==
                Location::Folder(folder_b),
            "tab_locations_are_isolated");
        passed &= Check(window_state.Find(tab_a)->state->GetSortDescriptor().field == SortField::Name &&
                window_state.Find(tab_b)->state->GetSortDescriptor().field == SortField::Modified,
            "tab_sort_descriptors_are_isolated");

        const std::wstring folder_a_next = L"C:\\phase1-tabs\\A-next";
        auto listing_a_next = Listing(folder_a_next);
        listing_a_next.entries = { TestEntry(L"tab-a-next-entry.txt", TestIdentity(103), 3) };
        const auto request_a_next = window_state.Find(tab_a)->state->Navigate(folder_a_next);
        passed &= Check(request_a_next &&
                window_state.Find(tab_a)->state->CommitBrowse(
                    *request_a_next, listing_a_next).status ==
                    blink::app::CommitBrowseStatus::Committed &&
                window_state.Find(tab_a)->state->PresentationSnapshot().committed_location ==
                    Location::Folder(folder_a_next) &&
                window_state.Find(tab_b)->state->PresentationSnapshot().committed_location ==
                    Location::Folder(folder_b) &&
                window_state.Find(tab_a)->state->Snapshot().navigation.back_history.size() == 2 &&
                window_state.Find(tab_b)->state->Snapshot().navigation.back_history.size() == 1,
            "tab_histories_are_isolated");

        passed &= Check(window_state.Find(tab_a)->state->SelectEntry(listing_a_next.entries.front()) &&
                window_state.Find(tab_a)->state->Snapshot().selection.size() == 1 &&
                window_state.Find(tab_b)->state->Snapshot().selection.empty(),
            "tab_selections_are_isolated");
        passed &= Check(window_state.Find(tab_a)->browse_state != window_state.Find(tab_b)->browse_state &&
                window_state.Find(tab_a)->browse_controller != window_state.Find(tab_b)->browse_controller,
            "tab_async_execution_state_is_isolated");

        passed &= Check(window_state.ActivateTab(tab_b) &&
                window_state.ActiveTabId() == tab_b,
            "tab_activation_changes_active_id");
        passed &= Check(window_state.Find(tab_a)->state->PresentationSnapshot().committed_location ==
                Location::Folder(folder_a_next),
            "inactive_tab_state_is_preserved");

        passed &= Check(window_state.CloseTab(tab_b) == CloseTabStatus::Closed &&
                window_state.Find(tab_b) == nullptr &&
                window_state.ActiveTabId() == tab_c,
            "closing_active_middle_tab_selects_right_neighbor");
        passed &= Check(window_state.CloseTab(tab_c) == CloseTabStatus::Closed &&
                window_state.ActiveTabId() == tab_a && window_state.Size() == 1,
            "closing_active_rightmost_tab_selects_left_neighbor");
        passed &= Check(window_state.CloseTab(tab_a) ==
                CloseTabStatus::LastTabWouldCloseWindow && window_state.Size() == 1,
            "closing_last_tab_requests_window_close");

        const auto closed_tab = window_state.CreateTab();
        const auto closed_browse_state = window_state.Find(closed_tab)->browse_state;
        passed &= Check(window_state.CloseTab(closed_tab) == CloseTabStatus::Closed &&
                window_state.Find(closed_tab) == nullptr &&
                closed_browse_state->Snapshot().shutting_down,
            "closed_tab_invalidates_async_state");

        {
            TabWindowState search_window;
            const auto active_id = search_window.ActiveTabId();
            const auto background_id = search_window.CreateTab();
            const auto active_search = search_window.Find(active_id)->search_state;
            const auto background_search = search_window.Find(background_id)->search_state;
            const blink::app::SearchQuery query{ L"visibility-search" };
            search_window.Find(active_id)->EnterSearch();
            search_window.Find(background_id)->EnterSearch();
            const auto old_active = active_search->Begin(query);
            const auto old_background = background_search->Begin(query);
            search_window.SetVisibilityPolicy({ true });
            passed &= Check(old_active != 0 && old_background != 0 &&
                    !active_search->IsCurrent(old_active) && !background_search->IsCurrent(old_background) &&
                    active_search->Snapshot().stage == blink::app::SearchStage::Idle &&
                    background_search->Snapshot().stage == blink::app::SearchStage::Idle &&
                    !search_window.Find(active_id)->search_visible && !search_window.Find(background_id)->search_visible,
                "visibility_change_cancels_active_and_background_search_generations");

            const auto shown_active = active_search->Begin(query);
            const auto shown_background = background_search->Begin(query);
            (void)search_window.ActivateTab(background_id);
            search_window.SetVisibilityPolicy({ false });
            const bool shown_generations_cancelled = !active_search->IsCurrent(shown_active) &&
                !background_search->IsCurrent(shown_background);
            const auto hidden_active = active_search->Begin(query);
            const auto hidden_background = background_search->Begin(query);
            passed &= Check(shown_active != 0 && shown_background != 0 && shown_generations_cancelled &&
                    hidden_active != 0 && hidden_background != 0 &&
                    active_search->Snapshot().query == query && background_search->Snapshot().query == query,
                "visibility_show_hide_preserves_search_restart_in_both_existing_tabs");

            const auto close_status = search_window.CloseTab(background_id);
            const bool survivor_current = active_search->IsCurrent(hidden_active);
            if (auto* survivor = search_window.Find(active_id)) { survivor->LeaveSearch(); }
            const auto reopened_survivor = active_search->Begin(query);
            passed &= Check(close_status == CloseTabStatus::Closed &&
                    search_window.ActiveTabId() == active_id && search_window.Find(background_id) == nullptr &&
                    survivor_current && reopened_survivor != 0 &&
                    background_search->Snapshot().stage == blink::app::SearchStage::Closed &&
                    background_search->Begin(query) == 0,
                "visibility_tab_close_preserves_survivor_search_and_permanently_retires_closed_state");
        }

        return passed;
    }

    [[nodiscard]] bool RunNavigationTreeTests()
    {
        using blink::app::Location;
        using blink::app::NavigationTreeExpansionToken;
        bool passed = true;
        blink::app::NavigationTreeModel tree;
        const auto root = tree.ResetTopLevel(
            L"This PC",
            {
                { blink::app::NavigationTreeNodeKind::Volume,
                    L"D:", Location::Folder(L"D:\\"), true, false },
            });
        const auto* root_node = tree.Find(root);
        passed &= Check(root != 0 && root_node && root_node->child_ids.size() == 1,
            "navigation_tree_has_this_pc_and_top_level_children");
        passed &= Check(
            tree.FindExact(Location::Folder(L"C:\\Users\\Blink")) == 0,
            "navigation_tree_does_not_materialize_quick_place_nodes");

        const auto volume = tree.FindExact(Location::Folder(L"D:\\"));
        passed &= Check(volume != 0 && tree.FindBestAncestor(
            Location::Folder(L"D:\\Blink\\Child")) == volume,
            "navigation_tree_uses_location_ancestor_identity");

        auto expansion = tree.BeginExpansion(volume);
        passed &= Check(expansion.has_value(), "navigation_tree_starts_lazy_expansion");
        blink::core::DirectoryListing listing;
        listing.path = L"D:\\";
        listing.entries.push_back(blink::core::FileEntry{
            L"Blink", L"D:\\Blink", {}, true, 0, 0, {}, {}, {},
            FILE_ATTRIBUTE_DIRECTORY, 0, {} });
        listing.entries.push_back(blink::core::FileEntry{
            L"file.txt", L"D:\\file.txt", L".txt", false, 0, 0, {}, {}, {},
            0, 0, {} });
        listing.entries.push_back(blink::core::FileEntry{
            L"Hidden", L"D:\\Hidden", {}, true, 0, 0, {}, {}, {},
            FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN, 0, {} });
        listing.entries.push_back(blink::core::FileEntry{
            L"Link", L"D:\\Link", {}, true, 0, 0, {}, {}, {},
            FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT,
            IO_REPARSE_TAG_SYMLINK, {} });
        const auto children = blink::app::FilterNavigationTreeChildren(listing);
        passed &= Check(children.size() == 2 && children[1].reparse_point &&
                !children[1].can_expand,
            "navigation_tree_filters_files_hidden_and_non_expandable_reparse");
        passed &= Check(expansion.has_value() && tree.CompleteExpansion(*expansion, children),
            "navigation_tree_completes_lazy_expansion");

        const auto blink_node = tree.FindChild(volume, Location::Folder(L"D:\\Blink"));
        const auto loaded_child_ids = tree.Find(volume)->child_ids;
        bool loaded_child_ids_unique = true;
        for (std::size_t left = 0; left < loaded_child_ids.size(); ++left)
        {
            for (std::size_t right = left + 1; right < loaded_child_ids.size(); ++right)
            {
                if (loaded_child_ids[left] == loaded_child_ids[right])
                {
                    loaded_child_ids_unique = false;
                }
            }
        }
        passed &= Check(blink_node != 0 && tree.FindExact(Location::Folder(L"D:\\Blink")) == blink_node &&
                tree.Find(blink_node)->label == L"Blink",
            "navigation_tree_child_has_stable_app_identity");

        const auto second_expansion = tree.BeginExpansion(volume);
        passed &= Check(!second_expansion.has_value() &&
                tree.Find(volume)->load_state == blink::app::NavigationTreeLoadState::Loaded &&
                tree.Find(volume)->child_ids == loaded_child_ids &&
                loaded_child_ids_unique &&
                tree.FindChild(volume, Location::Folder(L"D:\\Blink")) == blink_node,
            "navigation_tree_loaded_reexpansion_reuses_children_without_enumeration");

        const auto failed_expansion = tree.BeginExpansion(blink_node);
        passed &= Check(failed_expansion.has_value(), "navigation_tree_starts_nested_expansion");
        if (failed_expansion.has_value())
        {
            passed &= Check(tree.FailExpansion(*failed_expansion, ERROR_ACCESS_DENIED) &&
                    tree.Find(blink_node)->load_state ==
                        blink::app::NavigationTreeLoadState::Failed,
                "navigation_tree_failure_is_visible_in_model");
        }
        const auto retry_expansion = tree.BeginExpansion(blink_node);
        passed &= Check(retry_expansion.has_value(), "navigation_tree_retry_after_failure");
        if (retry_expansion.has_value())
        {
            passed &= Check(!tree.CompleteExpansion(
                    NavigationTreeExpansionToken{
                        retry_expansion->node_id,
                        retry_expansion->generation - 1,
                        retry_expansion->target },
                    children),
                "navigation_tree_drops_stale_expansion_result");
            (void)tree.FailExpansion(*retry_expansion, ERROR_OPERATION_ABORTED);
        }

        {
            using blink::app::NavigationTreeLoadState;
            using blink::app::NavigationTreeNodeKind;
            blink::app::NavigationTreeModel refresh_tree;
            const auto refresh_root = refresh_tree.ResetTopLevel(L"This PC", {
                { NavigationTreeNodeKind::Volume, L"D:", Location::Folder(L"D:\\"), true, false },
                { NavigationTreeNodeKind::Volume, L"E:", Location::Folder(L"E:\\"), false, false },
                { NavigationTreeNodeKind::Volume, L"F:", Location::Folder(L"F:\\"), true, true },
            });
            const auto refresh_volume = refresh_tree.FindExact(Location::Folder(L"D:\\"));
            const auto seed = refresh_tree.BeginExpansion(refresh_volume);
            const bool seeded = seed && refresh_tree.CompleteExpansion(*seed, {
                { L"Keep", Location::Folder(L"D:\\Keep"), true, false },
                { L"Remove", Location::Folder(L"D:\\Remove"), true, false },
            });
            const auto keep_id = refresh_tree.FindChild(refresh_volume, Location::Folder(L"D:\\Keep"));
            const auto remove_id = refresh_tree.FindChild(refresh_volume, Location::Folder(L"D:\\Remove"));
            const auto keep_seed = refresh_tree.BeginExpansion(keep_id);
            const bool keep_loaded = keep_seed && refresh_tree.CompleteExpansion(*keep_seed, {
                { L"Grandchild", Location::Folder(L"D:\\Keep\\Grandchild"), true, false },
            });
            const auto grandchild_id = refresh_tree.FindChild(keep_id, Location::Folder(L"D:\\Keep\\Grandchild"));
            const auto remove_seed = refresh_tree.BeginExpansion(remove_id);
            const bool remove_loaded = remove_seed && refresh_tree.CompleteExpansion(*remove_seed, {
                { L"Gone", Location::Folder(L"D:\\Remove\\Gone"), true, false },
            });
            const auto removed_grandchild_id = refresh_tree.FindChild(remove_id, Location::Folder(L"D:\\Remove\\Gone"));
            const auto* before = refresh_tree.Find(refresh_volume);
            const auto old_ids = before ? before->child_ids : std::vector<blink::app::NavigationTreeNodeId>{};
            const auto first_refresh = refresh_tree.BeginExpansion(refresh_volume, true);
            passed &= Check(seeded && first_refresh && seed && first_refresh->generation > seed->generation &&
                    refresh_tree.IsExpansionCurrent(*first_refresh) && !refresh_tree.BeginExpansion(refresh_volume),
                "navigation_tree_explicit_refresh_starts_new_loaded_generation_while_default_loading_expansion_stays_rejected");
            const auto* pending = refresh_tree.Find(refresh_volume);
            const auto* kept = refresh_tree.Find(keep_id);
            passed &= Check(pending && pending->load_state == NavigationTreeLoadState::Loading &&
                    pending->child_ids == old_ids && keep_loaded && remove_loaded && kept &&
                    kept->load_state == NavigationTreeLoadState::Loaded && refresh_tree.Find(grandchild_id) &&
                    refresh_tree.Find(removed_grandchild_id),
                "navigation_tree_refresh_keeps_previous_children_and_loaded_descendants_visible_until_commit");
            const auto second_refresh = refresh_tree.BeginExpansion(refresh_volume, true);
            passed &= Check(first_refresh && second_refresh && second_refresh->generation > first_refresh->generation &&
                    !refresh_tree.IsExpansionCurrent(*first_refresh) && refresh_tree.IsExpansionCurrent(*second_refresh) &&
                    !refresh_tree.CompleteExpansion(*first_refresh, {}) &&
                    !refresh_tree.FailExpansion(*first_refresh, ERROR_ACCESS_DENIED) &&
                    refresh_tree.Find(refresh_volume)->child_ids == old_ids &&
                    refresh_tree.Find(refresh_volume)->error == ERROR_SUCCESS,
                "navigation_tree_refresh_supersedes_inflight_generation_and_rejects_old_success_and_failure");
            const bool completed = second_refresh && refresh_tree.CompleteExpansion(*second_refresh, {
                { L"Preserved", Location::Folder(L"d:\\KEEP"), true, false },
                { L"Added", Location::Folder(L"D:\\Added"), true, false },
            });
            kept = refresh_tree.Find(keep_id);
            passed &= Check(completed && refresh_tree.FindChild(refresh_volume, Location::Folder(L"D:\\Keep")) == keep_id &&
                    kept && kept->label == L"Preserved" && kept->load_state == NavigationTreeLoadState::Loaded &&
                    refresh_tree.FindChild(keep_id, Location::Folder(L"D:\\Keep\\Grandchild")) == grandchild_id,
                "navigation_tree_refresh_reuses_case_insensitive_child_id_and_its_loaded_grandchildren");
            const auto added_id = refresh_tree.FindChild(refresh_volume, Location::Folder(L"D:\\Added"));
            passed &= Check(completed && added_id && added_id != keep_id && added_id != remove_id &&
                    refresh_tree.Find(added_id)->load_state == NavigationTreeLoadState::Unloaded &&
                    !refresh_tree.Find(remove_id) && !refresh_tree.Find(removed_grandchild_id) &&
                    refresh_tree.Find(refresh_volume)->child_ids.size() == 2,
                "navigation_tree_refresh_applies_added_children_and_removes_obsolete_subtrees");
            const auto committed_ids = refresh_tree.Find(refresh_volume)->child_ids;
            const auto failed_refresh = refresh_tree.BeginExpansion(refresh_volume, true);
            passed &= Check(failed_refresh && refresh_tree.FailExpansion(*failed_refresh, ERROR_ACCESS_DENIED) &&
                    refresh_tree.Find(refresh_volume)->load_state == NavigationTreeLoadState::Failed &&
                    refresh_tree.Find(refresh_volume)->error == ERROR_ACCESS_DENIED &&
                    refresh_tree.Find(refresh_volume)->child_ids == committed_ids && refresh_tree.Find(grandchild_id),
                "navigation_tree_failed_refresh_reports_error_without_discarding_the_previous_snapshot");
            passed &= Check(!refresh_tree.BeginExpansion(refresh_root, true) &&
                    !refresh_tree.BeginExpansion(refresh_tree.FindExact(Location::Folder(L"E:\\")), true) &&
                    !refresh_tree.BeginExpansion(refresh_tree.FindExact(Location::Folder(L"F:\\")), true) &&
                    !refresh_tree.BeginExpansion(0, true) && !refresh_tree.BeginExpansion(keep_id),
                "navigation_tree_refresh_preserves_virtual_nonexpandable_reparse_and_default_loaded_boundaries");
            const auto closing_refresh = refresh_tree.BeginExpansion(refresh_volume, true);
            refresh_tree.Shutdown();
            passed &= Check(closing_refresh && !refresh_tree.IsExpansionCurrent(*closing_refresh) &&
                    !refresh_tree.CompleteExpansion(*closing_refresh, {}) &&
                    !refresh_tree.BeginExpansion(refresh_volume, true) && !refresh_tree.BeginExpansion(refresh_volume) &&
                    refresh_tree.RootId() == 0,
                "navigation_tree_shutdown_retires_explicit_refresh_as_well_as_ordinary_expansion");
        }

        tree.Shutdown();
        passed &= Check(tree.RootId() == 0 && !tree.BeginExpansion(volume).has_value(),
            "navigation_tree_shutdown_invalidates_async_work");
        return passed;
    }

    [[nodiscard]] bool CheckBrowseSourceContract()
    {
        std::ifstream source("src/Blink/src/main.cpp", std::ios::binary);
        const std::string text{
            std::istreambuf_iterator<char>(source),
            std::istreambuf_iterator<char>()};
        const auto browse_start = text.find("void BrowseAsync");
        const auto render_committed = text.find("void RenderCommittedFolder", browse_start);
        if (browse_start == std::string::npos || render_committed == std::string::npos)
        {
            return Check(false, "browse_source_region_found");
        }

        const std::string browse_region = text.substr(browse_start, render_committed - browse_start);
        bool passed = true;
        passed &= Check(browse_region.find("tab->browse_controller") != std::string::npos, "browse_uses_controller");
        passed &= Check(browse_region.find("get_weak()") != std::string::npos, "browse_uses_weak_app");
        passed &= Check(browse_region.find("TryEnqueue") != std::string::npos, "browse_checks_dispatch_queue");
        passed &= Check(browse_region.find("CommitBrowse") != std::string::npos, "browse_has_committed_page_gate");
        passed &= Check(browse_region.find("std::thread") == std::string::npos, "browse_has_no_std_thread");
        passed &= Check(browse_region.find(".detach()") == std::string::npos, "browse_has_no_detach");
        passed &= Check(browse_region.find("[this") == std::string::npos, "browse_has_no_raw_this_capture");

        const auto status_start = text.find("void UpdateSelectionStatus");
        const auto navigation_start = text.find("void UpdateNavigationButtons");
        const auto status_region = status_start != std::string::npos &&
                navigation_start != std::string::npos ?
            text.substr(status_start, navigation_start - status_start) : std::string{};
        passed &= Check(status_region.find("PresentationSnapshot") != std::string::npos &&
                status_region.find("tab_state_.Snapshot()") == std::string::npos,
            "ui_status_uses_lightweight_presentation_snapshot");

        const auto selection_start = text.find("void OnListSelectionChanged");
        const auto selection_end = text.find("SelectedEntryFromCommittedState", selection_start);
        if (selection_start == std::string::npos || selection_end == std::string::npos)
        {
            return Check(false, "selection_source_region_found");
        }
        const std::string selection_region = text.substr(
            selection_start,
            selection_end - selection_start);
        passed &= Check(
            selection_region.find("SetRowSelection") != std::string::npos &&
            selection_region.find("SelectedItems().Clear") != std::string::npos &&
            selection_region.find("selection_sync_suppressed_") != std::string::npos,
            "invalid_identity_ui_selection_is_cleared");

        const auto ui_start = text.find("void BuildUi");
        const auto browse_method = text.find("void BrowseAsync", ui_start);
        if (ui_start == std::string::npos || browse_method == std::string::npos)
        {
            return Check(false, "product_shell_source_region_found");
        }
        const std::string ui_region = text.substr(ui_start, browse_method - ui_start);
        const auto row_projection = text.find("void RebuildRowProjection");
        const auto row_render = text.find("void RenderEntries", row_projection);
        const bool uses_virtualized_items =
            ui_region.find("ItemsSource") != std::string::npos &&
            ui_region.find("ItemTemplate") != std::string::npos &&
            text.find("XamlReader::Load") != std::string::npos &&
            text.find("ItemsStackPanel") != std::string::npos;
        const bool no_eager_row_visuals =
            text.find("MakeDetailsRow") == std::string::npos &&
            row_projection != std::string::npos &&
            row_render != std::string::npos &&
            text.substr(row_projection, row_render - row_projection).find("Grid()") ==
                std::string::npos;
        const auto double_tapped = text.find("const auto double_tapped_handler");
        const auto double_tapped_end = text.find("content_grid_.Children().Append(entries_)", double_tapped);
        const std::string double_tapped_region = double_tapped != std::string::npos &&
                double_tapped_end != std::string::npos ?
            text.substr(double_tapped, double_tapped_end - double_tapped) : std::string{};
        const auto clicked_helper = text.find("std::optional<std::size_t> ResolveClickedRowIndex");
        const auto clicked_helper_end = text.find("void RenderUiProbeItems", clicked_helper);
        const std::string clicked_helper_region = clicked_helper != std::string::npos &&
                clicked_helper_end != std::string::npos ?
            text.substr(clicked_helper, clicked_helper_end - clicked_helper) : std::string{};
        passed &= Check(ui_region.find("TreeView") != std::string::npos &&
                ui_region.find("TreeViewSelectionMode::Single") != std::string::npos &&
                ui_region.find("navigation_tree_.ItemInvoked") != std::string::npos &&
                ui_region.find("navigation_tree_.Expanding") != std::string::npos &&
                ui_region.find("places_list_") == std::string::npos &&
                ui_region.find("drives_list_") == std::string::npos &&
                ui_region.find("ListViewSelectionMode::Extended") != std::string::npos &&
                ui_region.find("Build index") == std::string::npos &&
                ui_region.find("Start MFT/USN index") == std::string::npos &&
                uses_virtualized_items && no_eager_row_visuals &&
                double_tapped_region.find("UIElement::DoubleTappedEvent") != std::string::npos &&
                double_tapped_region.find("DoubleTappedEventHandler") != std::string::npos &&
                double_tapped_region.find("OnFolderDoubleTapped") != std::string::npos &&
                double_tapped_region.find("SelectedIndex") == std::string::npos &&
                clicked_helper_region.find("OriginalSource") != std::string::npos &&
                clicked_helper_region.find("DataContext") != std::string::npos,
            "product_shell_hides_phase0_controls");

        const auto activation_handler = text.find("void OnFolderDoubleTapped");
        const auto activation_handler_end = text.find("void UpdateSelectionStatus", activation_handler);
        const std::string activation_region = activation_handler != std::string::npos &&
                activation_handler_end != std::string::npos ?
            text.substr(activation_handler, activation_handler_end - activation_handler) : std::string{};
        passed &= Check(activation_region.find("ResolveClickedRowIndex") != std::string::npos &&
                activation_region.find("row.is_directory") != std::string::npos &&
                activation_region.find("BrowseAsync(row.full_path)") != std::string::npos &&
                activation_region.find("SelectedIndex") == std::string::npos,
            "directory_activation_uses_clicked_row_and_navigation_entry");
        passed &= Check(text.find("canonical IUnknown identity") != std::string::npos &&
                text.find("if (item == tab_ui->row_items[index])") != std::string::npos,
            "clicked_row_identity_uses_canonical_com_identity");
        passed &= Check(
            text.find("SyncNavigationPaneToCommittedLocation") != std::string::npos &&
            text.find("StartNavigationTreeReveal") != std::string::npos &&
            text.find("SelectNavigationTreeNode") != std::string::npos &&
            text.find("NavigationTreeModel") != std::string::npos &&
            text.find("navigation_quick_list_") != std::string::npos &&
            text.find("OnNavigationQuickItemClick") != std::string::npos &&
            text.find("ThisPcItemKind::Place") != std::string::npos &&
            text.find("NavigationTreeNodeKind::Place") == std::string::npos,
            "navigation_pane_separates_quick_items_from_tree");
        passed &= Check(
            text.find("navigation-tree.expanding.received") != std::string::npos &&
            text.find("navigation_tree_.Collapsed") != std::string::npos &&
            text.find("navigation-tree.expansion.intent-recorded.node=") !=
                std::string::npos &&
            text.find("navigation-tree.expansion.intent-cancelled.node=") !=
                std::string::npos &&
            text.find("navigation-tree.expansion.after-async.node=") !=
                std::string::npos &&
            text.find("navigation-tree.expansion.visual-expand.requested.node=") !=
                std::string::npos &&
            text.find("Content[Label]") != std::string::npos &&
            text.find("navigation_pane_ = ScrollViewer()") != std::string::npos &&
            text.find("auto navigation_content = StackPanel()") != std::string::npos &&
            text.find("navigation_pane_.VerticalScrollBarVisibility") != std::string::npos &&
            text.find("ScrollViewer::SetVerticalScrollBarVisibility(\n            navigation_quick_list_") !=
                std::string::npos &&
            text.find("ScrollViewer::SetVerticalScrollMode(\n            navigation_quick_list_") !=
                std::string::npos &&
            text.find("ScrollViewer::SetVerticalScrollBarVisibility(\n            navigation_tree_") !=
                std::string::npos &&
            text.find("ScrollViewer::SetVerticalScrollMode(\n            navigation_tree_") !=
                std::string::npos &&
            text.find("navigation_quick_list_.Height(100)") == std::string::npos &&
            text.find("navigation_quick_list_.Loaded") != std::string::npos &&
            text.find("navigation_tree_.Loaded") != std::string::npos &&
            text.find("DisableNavigationInternalScrolling") !=
                std::string::npos &&
            text.find("navigation-tree.enumeration.begin.node=") != std::string::npos &&
            text.find("navigation-tree.enumeration.end.node=") != std::string::npos &&
            text.find("navigation-tree.expansion.rejected.node=") !=
                std::string::npos &&
            text.find("navigation-tree.expansion.children-published.node=") !=
                std::string::npos &&
            text.find("navigation-tree.children-materialization.begin.parent=") !=
                std::string::npos &&
            text.find("navigation-tree.is-expanded.set.node=") != std::string::npos,
            "navigation_tree_runtime_chain_and_single_scroll_surface_are_traced");
        const auto expansion_start = text.find("void StartNavigationTreeExpansion");
        const auto expansion_end = text.find("void ApplyNavigationTreeReveal", expansion_start);
        const std::string expansion_region = expansion_start != std::string::npos &&
                expansion_end != std::string::npos ?
            text.substr(expansion_start, expansion_end - expansion_start) : std::string{};
        passed &= Check(
            expansion_region.find("navigation_tree_model_.BeginExpansion(node_id, refresh)") !=
                std::string::npos &&
            expansion_region.find("navigation_tree_pending_expansions_") !=
                std::string::npos &&
            expansion_region.find("user_intent_still_expand") !=
                std::string::npos &&
            expansion_region.find("if (apply_pending_visual_expansion &&") !=
                std::string::npos &&
            expansion_region.find("SetNavigationTreeExpanded(ui_iterator->second, true)") !=
                std::string::npos,
            "first_expand_intent_is_applied_once_and_loaded_nodes_do_not_reenumerate");
        const auto quick_activation_start = text.find(
            "void OnNavigationQuickItemClick");
        const auto quick_activation_end = text.find(
            "std::optional<blink::app::NavigationTreeNodeId>",
            quick_activation_start);
        const auto tree_activation_start = text.find(
            "void OnNavigationTreeItemInvoked");
        const auto tree_activation_end = text.find(
            "void OnNavigationTreeExpanding", tree_activation_start);
        const auto expansion_handler_start = text.find(
            "void OnNavigationTreeExpanding");
        const auto expansion_handler_end = text.find(
            "void OnFolderDoubleTapped", expansion_handler_start);
        const std::string quick_activation_region =
            quick_activation_start != std::string::npos &&
                quick_activation_end != std::string::npos ?
            text.substr(quick_activation_start,
                quick_activation_end - quick_activation_start) : std::string{};
        const std::string tree_activation_region =
            tree_activation_start != std::string::npos &&
                tree_activation_end != std::string::npos ?
            text.substr(tree_activation_start,
                tree_activation_end - tree_activation_start) : std::string{};
        const std::string expansion_handler_region =
            expansion_handler_start != std::string::npos &&
                expansion_handler_end != std::string::npos ?
            text.substr(expansion_handler_start,
                expansion_handler_end - expansion_handler_start) : std::string{};
        passed &= Check(
            quick_activation_region.find("NavigateToLocation") != std::string::npos &&
                quick_activation_region.find("StartNavigation") == std::string::npos &&
                tree_activation_region.find("NavigateToLocation") != std::string::npos &&
                tree_activation_region.find("StartNavigation") == std::string::npos &&
                expansion_handler_region.find("NavigateToLocation") == std::string::npos,
            "navigation_pane_activation_has_single_routes");
        const auto presentation_start = text.find("void BeginBatchedRowPresentation");
        const auto presentation_end = text.find("void RebuildRowProjection", presentation_start);
        const std::string presentation_region = presentation_start != std::string::npos &&
                presentation_end != std::string::npos ?
            text.substr(presentation_start, presentation_end - presentation_start) : std::string{};
        passed &= Check(
            presentation_region.find("TabState::SortEntries") != std::string::npos &&
            (presentation_region.find("BuildFolderRowProjection") != std::string::npos ||
                presentation_region.find("projection_builder.Build") != std::string::npos) &&
            presentation_region.find("AsyncStatus::Canceled") != std::string::npos &&
            presentation_region.find("PublishProjectedPresentationBatch") != std::string::npos,
            "large_folder_sort_and_projection_share_owned_background_pipeline");

        const auto this_pc_start = text.find("void StartThisPCNavigation");
        const auto folder_start = text.find("void StartFolderNavigation", this_pc_start);
        const std::string this_pc_region = this_pc_start != std::string::npos &&
                folder_start != std::string::npos ?
            text.substr(this_pc_start, folder_start - this_pc_start) : std::string{};
        passed &= Check(
            this_pc_region.find("EnumerateThisPcContent") != std::string::npos &&
            this_pc_region.find("generation != tab_ui->this_pc_generation") != std::string::npos &&
            this_pc_region.find("RenderThisPC") != std::string::npos &&
            this_pc_region.find("ApplyNavigationTreeContent") == std::string::npos &&
            this_pc_region.find("PresentActiveTab") == std::string::npos,
            "this_pc_uses_one_gated_model_for_main_and_tree");
        const auto render_this_pc_start = text.find("void RenderThisPC");
        const auto render_folder_start = text.find(
            "bool RenderCommittedFolderWithTiming", render_this_pc_start);
        const std::string render_this_pc_region = render_this_pc_start != std::string::npos &&
                render_folder_start != std::string::npos ?
            text.substr(render_this_pc_start, render_folder_start - render_this_pc_start) :
            std::string{};
        passed &= Check(
            render_this_pc_region.find("if (tab_id == tab_window_state_.ActiveTabId())") !=
                std::string::npos &&
            render_this_pc_region.find("PresentActiveTab()") != std::string::npos,
            "this_pc_render_owns_single_active_presentation");

        const auto new_accelerator = text.find("new_tab_accelerator.Invoked");
        const auto close_accelerator = text.find("close_tab_accelerator.Invoked");
        const std::string new_accelerator_region = new_accelerator != std::string::npos &&
                close_accelerator != std::string::npos ?
            text.substr(new_accelerator, close_accelerator - new_accelerator) : std::string{};
        const auto close_region_end = text.find(".KeyboardAccelerators().Append(close_tab_accelerator)", close_accelerator);
        const std::string close_accelerator_region = close_accelerator != std::string::npos &&
                close_region_end != std::string::npos ?
            text.substr(close_accelerator, close_region_end - close_accelerator) : std::string{};
        passed &= Check(
            new_accelerator_region.find("CreateAndActivateTab()") != std::string::npos &&
            new_accelerator_region.find("args.Handled(true)") != std::string::npos &&
            close_accelerator_region.find("CloseTab(") != std::string::npos &&
            close_accelerator_region.find("args.Handled(true)") != std::string::npos,
            "tab_shortcuts_stop_after_one_authoritative_command_route");
        return passed;
    }

    [[nodiscard]] blink::core::FileIdentity BenchmarkIdentity(std::size_t id)
    {
        blink::core::FileIdentity identity;
        identity.valid = true;
        identity.volume_serial = 0x12345678;
        identity.file_id[0] = static_cast<std::uint8_t>(id & 0xff);
        identity.file_id[1] = static_cast<std::uint8_t>((id >> 8) & 0xff);
        identity.file_id[2] = static_cast<std::uint8_t>((id >> 16) & 0xff);
        identity.file_id[3] = static_cast<std::uint8_t>((id >> 24) & 0xff);
        return identity;
    }

    [[nodiscard]] bool RunLargeSelectionComplexityBenchmark()
    {
        using blink::app::CommitBrowseStatus;
        using blink::app::MakeRowIdentity;
        using blink::app::RowIdentity;
        using blink::app::TabState;

        bool passed = true;
        const std::wstring test_path = L"C:\\benchmark-large-selection";

        const std::vector<std::size_t> folder_sizes = { 10000, 50000 };
        const std::vector<std::size_t> selection_sizes = { 10, 1000, 10000, 50000 };

        for (const auto N : folder_sizes)
        {
            auto listing = Listing(test_path);
            listing.entries.reserve(N);
            std::vector<RowIdentity> all_rows;
            all_rows.reserve(N);
            for (std::size_t i = 0; i < N; ++i)
            {
                auto entry = TestEntry(L"file_" + std::to_wstring(i) + L".dat", BenchmarkIdentity(1000000 + i), 1024);
                all_rows.push_back(MakeRowIdentity(entry));
                listing.entries.push_back(std::move(entry));
            }

            TabState tab;
            const auto nav = tab.Navigate(test_path);
            passed &= Check(nav.has_value(), "benchmark_nav_has_value");
            if (!nav) continue;

            const auto initial_commit_start = std::chrono::steady_clock::now();
            const auto commit_res = tab.CommitBrowse(*nav, listing);
            const auto initial_commit_end = std::chrono::steady_clock::now();
            const auto initial_commit_us = std::chrono::duration_cast<
                std::chrono::microseconds>(initial_commit_end - initial_commit_start).count();
            std::cout << "large_selection_initial_commit N=" << N
                      << " status=" << static_cast<int>(commit_res.status)
                      << " error=" << commit_res.error
                      << " us=" << initial_commit_us << "\n";
            passed &= Check(commit_res.status == CommitBrowseStatus::Committed, "benchmark_initial_commit");

            for (const auto M : selection_sizes)
            {
                if (M > N) continue;

                std::vector<RowIdentity> test_selection;
                test_selection.reserve(M);
                for (std::size_t i = 0; i < M; ++i)
                {
                    test_selection.push_back(all_rows[i]);
                }

                const auto t0 = std::chrono::steady_clock::now();
                tab.SetRowSelection(test_selection);
                const auto t1 = std::chrono::steady_clock::now();
                const auto set_selection_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

                passed &= Check(tab.Snapshot().selection.size() == M, "benchmark_selection_count_matches");

                const auto t2 = std::chrono::steady_clock::now();
                std::size_t hit_count = 0;
                for (std::size_t i = 0; i < M; ++i)
                {
                    if (tab.IsRowSelected(test_selection[i]))
                    {
                        ++hit_count;
                    }
                }
                const auto t3 = std::chrono::steady_clock::now();
                const auto is_selected_us = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
                passed &= Check(hit_count == M, "benchmark_is_selected_all_hits");

                const auto refresh_nav = tab.Navigate(test_path);
                passed &= Check(refresh_nav.has_value(), "benchmark_refresh_nav");
                const auto t4 = std::chrono::steady_clock::now();
                if (refresh_nav)
                {
                    const auto refresh_commit = tab.CommitBrowse(*refresh_nav, listing);
                    passed &= Check(refresh_commit.status == CommitBrowseStatus::Committed, "benchmark_refresh_commit");
                }
                const auto t5 = std::chrono::steady_clock::now();
                const auto refresh_commit_us = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();

                passed &= Check(tab.Snapshot().selection.size() == M, "benchmark_preserved_selection_count");

                std::cout << "[Benchmark] N=" << N << ", M=" << M
                          << " | SetRowSelection: " << set_selection_us << " us"
                          << " | " << M << "x IsRowSelected: " << is_selected_us << " us"
                          << " | CommitBrowse (Preserve): " << refresh_commit_us << " us\n";
            }
        }

        return passed;
    }

    [[nodiscard]] bool RunThisPcAddressBarRoundTripTests()
    {
        using blink::app::Localization;

        bool passed = true;
        Localization localization;
        const auto expected_display_text = localization.GetString(L"Location.ThisPC");

        passed &= Check(localization.IsThisPcDisplayText(expected_display_text), "roundtrip_this_pc_current_localized");
        passed &= Check(localization.IsThisPcDisplayText(L"  " + expected_display_text + L"  "), "roundtrip_this_pc_whitespace");
        passed &= Check(localization.IsThisPcDisplayText(L"\t" + expected_display_text + L"\t"), "roundtrip_this_pc_tab_whitespace");
        passed &= Check(!localization.IsThisPcDisplayText(L"C:\\"), "roundtrip_folder_not_this_pc");
        passed &= Check(!localization.IsThisPcDisplayText(expected_display_text + L" Subfolder"), "roundtrip_subfolder_not_this_pc");
        passed &= Check(!localization.IsThisPcDisplayText(L""), "roundtrip_empty_not_this_pc");
        passed &= Check(!localization.IsThisPcDisplayText(L"   "), "roundtrip_whitespace_only_not_this_pc");

        return passed;
    }

    [[nodiscard]] bool RunItemVisibilityTests()
    {
        using namespace blink::app;
        bool passed = true;
        auto visible = TestEntry(L"visible-link.txt", TestIdentity(130), 1);
        auto hidden = TestEntry(L"隐藏项目.txt", TestIdentity(131), 2);
        hidden.attributes = FILE_ATTRIBUTE_HIDDEN;
        auto system = TestEntry(L"system.txt", TestIdentity(132), 3);
        system.attributes = FILE_ATTRIBUTE_SYSTEM;
        auto protected_item = TestEntry(L"protected.txt", TestIdentity(133), 4);
        protected_item.attributes = FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN;
        auto second_link = TestEntry(L"second-link.txt", visible.identity, 1);
        auto hidden_directory = TestEntry(L"隐藏目录", TestIdentity(134), 0, true);
        hidden_directory.attributes |= FILE_ATTRIBUTE_HIDDEN;
        auto visible_directory = TestEntry(L"visible-folder", TestIdentity(135), 0, true);
        auto system_directory = TestEntry(L"system-folder", TestIdentity(136), 0, true);
        system_directory.attributes |= FILE_ATTRIBUTE_SYSTEM;
        auto listing = Listing(L"C:\\phase1-visibility");
        listing.entries = { visible, hidden, system, protected_item, second_link,
            hidden_directory, visible_directory, system_directory };

        TabState tab;
        const auto request = tab.Navigate(listing.path);
        passed &= Check(request && CommitSucceeded(tab.CommitBrowse(*request, listing)),
            "visibility_commits_unfiltered_directory_facts");
        passed &= Check(!tab.GetVisibilityPolicy().show_hidden_items &&
                tab.VisibleEntriesSnapshot().size() == 3 &&
                tab.CommittedEntriesSnapshot().size() == 8 &&
                tab.PresentationSnapshot().committed_item_count == 3,
            "visibility_defaults_hidden_and_system_off_without_losing_raw_entries");
        const auto hidden_row = MakeRowIdentity(hidden);
        passed &= Check(!tab.SelectEntry(hidden) && !tab.CommittedRow(hidden_row),
            "visibility_excluded_row_cannot_be_selected_or_activated");
        passed &= Check(tab.SelectAllRows() && tab.PresentationSnapshot().selected_count == 3,
            "visibility_select_all_only_includes_visible_rows");
        const auto navigation_before = tab.Snapshot().navigation;
        tab.SetSortDescriptor({ SortField::Size, SortDirection::Descending });
        tab.SetViewMode(FolderViewMode::Grid);
        const auto pending = tab.Navigate(L"C:\\phase1-next");
        tab.SetVisibilityPolicy({ true });
        passed &= Check(tab.VisibleEntriesSnapshot().size() == 5 &&
                tab.PresentationSnapshot().committed_item_count == 5 &&
                tab.PresentationSnapshot().selected_count == 3,
            "visibility_show_hidden_adds_rows_without_selecting_them");
        const auto navigation_after = tab.Snapshot().navigation;
        passed &= Check(navigation_before.committed_location == navigation_after.committed_location &&
                navigation_before.back_history == navigation_after.back_history &&
                navigation_before.forward_history == navigation_after.forward_history &&
                pending && tab.IsPending(*pending) &&
                tab.GetViewMode() == FolderViewMode::Grid &&
                tab.GetSortDescriptor() == SortDescriptor{ SortField::Size, SortDirection::Descending },
            "visibility_preserves_history_pending_navigation_view_and_sort");
        passed &= Check(tab.CommittedRow(hidden_row).has_value() &&
                !tab.CommittedRow(MakeRowIdentity(system)) &&
                !tab.CommittedRow(MakeRowIdentity(protected_item)),
            "visibility_show_hidden_does_not_expose_system_rows");
        tab.SetRowSelection({ MakeRowIdentity(visible), MakeRowIdentity(second_link), hidden_row });
        tab.SetVisibilityPolicy({ false });
        const auto after_hide = tab.Snapshot();
        passed &= Check(after_hide.selection.size() == 2 && !after_hide.focused_item &&
                tab.IsRowSelected(MakeRowIdentity(visible)) &&
                tab.IsRowSelected(MakeRowIdentity(second_link)) && !tab.IsRowSelected(hidden_row),
            "visibility_hide_prunes_hidden_selection_focus_and_preserves_hardlinks");
        tab.SetVisibilityPolicy({ true });
        passed &= Check(!tab.IsRowSelected(hidden_row), "visibility_reshow_does_not_resurrect_selection");
        auto empty_listing = Listing(L"C:\\phase1-hidden-only");
        empty_listing.entries = { hidden, protected_item };
        const auto hidden_only_request = tab.Navigate(empty_listing.path);
        tab.SetVisibilityPolicy({ false });
        passed &= Check(hidden_only_request && CommitSucceeded(tab.CommitBrowse(*hidden_only_request, empty_listing)) &&
                tab.PresentationSnapshot().folder_state == FolderState::Empty,
            "visibility_late_browse_commit_uses_current_policy_and_visible_empty_state");
        tab.SetVisibilityPolicy({ true });
        passed &= Check(tab.PresentationSnapshot().folder_state == FolderState::Ready &&
                tab.SortedEntries().size() == 1,
            "visibility_hidden_only_directory_becomes_ready_without_refresh");
        passed &= Check(FilterNavigationTreeChildren(listing).size() == 1 &&
                FilterNavigationTreeChildren(listing, { true }).size() == 2,
            "visibility_tree_and_file_view_use_same_policy");

        TabWindowState window;
        const auto first = window.ActiveTabId();
        const auto second = window.CreateTab();
        window.SetVisibilityPolicy({ true });
        const auto third = window.CreateTab();
        passed &= Check(window.GetVisibilityPolicy().show_hidden_items &&
                window.Find(first)->state->GetVisibilityPolicy().show_hidden_items &&
                window.Find(second)->state->GetVisibilityPolicy().show_hidden_items &&
                window.Find(third)->state->GetVisibilityPolicy().show_hidden_items,
            "visibility_window_policy_applies_to_inactive_and_new_tabs");
        window.SetVisibilityPolicy({ false });
        passed &= Check(!window.Find(first)->state->GetVisibilityPolicy().show_hidden_items &&
                !window.Find(second)->state->GetVisibilityPolicy().show_hidden_items &&
                !window.Find(third)->state->GetVisibilityPolicy().show_hidden_items,
            "visibility_window_policy_resets_all_tabs_together");

        const auto root = std::filesystem::absolute(std::filesystem::path(L".build") /
            L"phase1-visibility-tests" / (L"pid-" + std::to_wstring(GetCurrentProcessId())));
        std::filesystem::create_directories(root);
        struct FixtureCleanup
        {
            std::filesystem::path path;
            ~FixtureCleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
        } cleanup{ root };
        const auto file_path = root / L"隐藏文件.txt";
        { std::ofstream file(file_path, std::ios::binary); file << "owned visibility fixture"; }
        passed &= Check(SetFileAttributesW(file_path.c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE,
            "visibility_real_unicode_fixture_sets_hidden_attribute");
        const auto real_listing = blink::core::EnumerateDirectory(root.wstring());
        const auto real_request = tab.Navigate(root.wstring());
        tab.SetVisibilityPolicy({ false });
        passed &= Check(real_request && CommitSucceeded(tab.CommitBrowse(*real_request, real_listing)) &&
                tab.VisibleEntriesSnapshot().empty() && tab.PresentationSnapshot().folder_state == FolderState::Empty,
            "visibility_real_hidden_attribute_is_excluded_by_default");
        tab.SetVisibilityPolicy({ true });
        passed &= Check(tab.VisibleEntriesSnapshot().size() == 1 && tab.SelectAllRows() &&
                tab.PresentationSnapshot().selected_count == 1,
            "visibility_real_hidden_file_can_be_shown_and_selected");
        passed &= Check(SetFileAttributesW(file_path.c_str(), FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN) != FALSE,
            "visibility_real_fixture_sets_protected_attributes");
        const auto refresh = tab.Refresh();
        passed &= Check(refresh && CommitSucceeded(tab.CommitBrowse(*refresh,
                    blink::core::EnumerateDirectory(root.wstring()))) &&
                tab.VisibleEntriesSnapshot().empty() && tab.PresentationSnapshot().selected_count == 0,
            "visibility_real_attribute_change_prunes_selection_on_refresh");
        (void)SetFileAttributesW(file_path.c_str(), FILE_ATTRIBUTE_NORMAL);
        return passed;
    }

    [[nodiscard]] bool RunSelectAllRowsTests()
    {
        blink::app::TabState tab;
        const blink::app::Localization localization;
        const std::wstring literal_path = L"C:\\literal-{1}\\文件.txt";
        const auto formatted = localization.Format(L"Status.OpenFailed",
            std::vector<std::wstring>{ literal_path, L"0x80070005" });
        bool passed = Check(formatted.find(literal_path) != std::wstring::npos &&
                formatted.find(L"0x80070005") != std::wstring::npos,
            "file_open_error_preserves_literal_braces_in_path");
        const auto identity = TestIdentity(88);
        const auto a = TestEntry(L"link-a.txt", identity, 1);
        const auto b = TestEntry(L"link-b.txt", identity, 1);
        auto invalid = TestEntry(L"unavailable.txt", {}, 1);
        auto listing = Listing(L"C:\\phase1-select-all");
        listing.entries = { a, b, invalid };
        const auto request = tab.Navigate(listing.path);
        passed &= Check(request && CommitSucceeded(tab.CommitBrowse(*request, listing)),
            "select_all_committed_fixture");
        const auto before = tab.Snapshot().navigation;
        const auto unselected_b = tab.CommittedRow(blink::app::MakeRowIdentity(b));
        passed &= Check(unselected_b && unselected_b->name == b.name &&
                !tab.SelectedCommittedRow(blink::app::MakeRowIdentity(b)),
            "activation_resolves_exact_unselected_hardlink_row");
        passed &= Check(tab.SelectAllRows() && tab.PresentationSnapshot().selected_count == 2 &&
                tab.IsRowSelected(blink::app::MakeRowIdentity(a)) &&
                tab.IsRowSelected(blink::app::MakeRowIdentity(b)) &&
                !tab.IsRowSelected(blink::app::MakeRowIdentity(invalid)),
            "select_all_keeps_hardlink_names_distinct_and_excludes_invalid_identity");
        const auto copy_rows = tab.SelectedCommittedRows(2);
        passed &= Check(copy_rows && copy_rows->size() == 2 &&
            (*copy_rows)[0].name == a.name && (*copy_rows)[1].name == b.name &&
            (*copy_rows)[0].identity == (*copy_rows)[1].identity,
            "copy_selection_capture_preserves_distinct_hardlink_names_in_committed_order");
        passed &= Check(!tab.SelectedCommittedRows(1) && !tab.SelectedCommittedRows(0),
            "copy_selection_capture_rejects_the_whole_oversized_selection");
        tab.SetSortDescriptor({ blink::app::SortField::Name, blink::app::SortDirection::Descending });
        const auto after = tab.Snapshot().navigation;
        passed &= Check(before.committed_location == after.committed_location &&
                before.back_history == after.back_history && before.forward_history == after.forward_history &&
                tab.SelectAllRows() && tab.PresentationSnapshot().selected_count == 2,
            "select_all_is_idempotent_and_preserves_navigation_and_sort");
        const auto this_pc = tab.Navigate(blink::app::Location::ThisPC());
        passed &= Check(this_pc && CommitSucceeded(tab.CommitThisPC(*this_pc, {})) &&
                tab.SelectAllRows() && tab.PresentationSnapshot().selected_count == 0,
            "select_all_cannot_reuse_previous_folder_rows_in_this_pc");
        passed &= Check(!tab.SelectedCommittedRows(2), "copy_selection_capture_has_no_stale_rows_after_this_pc_navigation");
        return passed;
    }

    [[nodiscard]] bool RunFileActivationCommandLineTests()
    {
        const std::wstring host = L"C:\\Blink Host Space\\Blink.FileActivationHost.exe";
        bool passed = Check(blink::app::FileActivationHostResult(0) == S_OK &&
                FAILED(blink::app::FileActivationHostResult(3)) &&
                FAILED(blink::app::FileActivationHostResult(0xC0000374u)) &&
                blink::app::FileActivationHostResult(
                    static_cast<DWORD>(HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION))) ==
                    HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION),
            "file_activation_helper_crash_and_nonzero_exit_are_never_success");
        for (const auto& path : std::vector<std::wstring>{
                L"C:\\Blink 空格\\file.txt", L"C:\\literal-\"quote\".txt", L"C:\\trailing\\" })
        {
            auto entry = TestEntry(L"file.txt", TestIdentity(87), 1);
            entry.full_path = path;
            const auto command = blink::app::BuildFileActivationCommandLine(host, entry);
            int count{};
            auto arguments = CommandLineToArgvW(command.c_str(), &count);
            passed &= Check(arguments && count == 3 && arguments[0] == host &&
                    arguments[1] == entry.full_path && arguments[2] == entry.identity.ToString(),
                "file_activation_helper_arguments_roundtrip_path_and_full_identity");
            if (arguments) { LocalFree(arguments); }
        }
        return passed;
    }

    [[nodiscard]] bool RunDirectoryOperationTargetTests()
    {
        namespace fs = std::filesystem;
        using namespace blink::app;
        const auto root = fs::absolute(fs::path(L".build") / L"file-operation-fixtures" /
            (L"target-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
        struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{ root };
        bool passed = true;
        try
        {
            const auto parent = root / L"真实父目录", source = parent / L"目录 {目标}", behind = root / L"当前页面";
            const auto relative_nested = fs::path(L"后代") / L"保留 {0}.bin";
            const auto nested = source / relative_nested, behind_file = behind / L"实际选中文件.txt";
            fs::create_directories(nested.parent_path()); fs::create_directory(behind);
            const auto write = [](const fs::path& path, const std::string& data) {
                std::ofstream stream(path, std::ios::binary | std::ios::trunc);
                stream.write(data.data(), static_cast<std::streamsize>(data.size()));
                if (!stream) { throw std::runtime_error("directory target fixture write"); }
            };
            const auto read = [](const fs::path& path) {
                std::ifstream stream(path, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(stream), {});
            };
            const auto entry = [](const fs::path& path) {
                blink::core::FileEntry value;
                if (!blink::core::ReadEntryMetadata(path.wstring(), value))
                { throw std::runtime_error("directory target fixture metadata"); }
                value.full_path = path.wstring(); value.name = path.filename().wstring(); return value;
            };
            const auto empty = [](const DirectoryOperationTarget& value) {
                return value.parent.empty() && !value.parent_identity.valid && !value.source.identity.valid &&
                    value.source.name.empty() && value.source.full_path.empty();
            };
            constexpr char Payload[] = "owned-directory-target\0nested-payload";
            const std::string data(Payload, sizeof(Payload) - 1), stream_data = "[ZoneTransfer]\r\nZoneId=3\r\n";
            write(nested, data); write(nested.wstring() + L":Zone.Identifier:$DATA", stream_data);
            write(behind_file, "unrelated-visible-selection");
            const auto parent_entry = entry(parent), source_entry = entry(source), nested_entry = entry(nested);
            DirectoryOperationTarget captured;
            auto error = CaptureDirectoryOperationTarget(source.wstring(), captured);
            passed &= Check(error == ERROR_SUCCESS && captured.parent == parent.wstring() &&
                    captured.parent_identity == parent_entry.identity && captured.source.identity == source_entry.identity &&
                    captured.source.full_path == source.wstring() && captured.source.name == source.filename().wstring() &&
                    captured.source.is_directory && (captured.source.attributes & FILE_ATTRIBUTE_DIRECTORY),
                "directory_operation_target_captures_exact_unicode_directory_and_containing_parent_identities");
            const auto original = captured;
            passed &= Check(read(nested) == data && entry(nested).identity == nested_entry.identity &&
                    read(nested.wstring() + L":Zone.Identifier:$DATA") == stream_data &&
                    read(behind_file) == "unrelated-visible-selection" && entry(source).identity == source_entry.identity,
                "directory_operation_target_metadata_capture_preserves_nested_bytes_streams_and_unrelated_selection_payload");
            error = CaptureDirectoryOperationTarget(source.wstring() + L"\\\\", captured);
            passed &= Check(error == ERROR_SUCCESS && captured.source.identity == source_entry.identity &&
                    captured.parent == parent.wstring() && captured.parent_identity == parent_entry.identity &&
                    captured.source.name == source.filename().wstring(),
                "directory_operation_target_trailing_separators_keep_the_real_leaf_and_parent");
            error = CaptureDirectoryOperationTarget(L"\\\\?\\" + source.wstring() + L"\\", captured);
            passed &= Check(error == ERROR_SUCCESS && captured.source.identity == source_entry.identity &&
                    captured.source.full_path == source.wstring() && captured.parent_identity == parent_entry.identity,
                "directory_operation_target_extended_drive_spelling_projects_the_same_directory_link");
            captured = original;
            error = CaptureDirectoryOperationTarget((parent / L"not-opened-after-cancel").wstring(), captured,
                std::make_shared<std::atomic_bool>(true));
            passed &= Check(error == ERROR_CANCELLED && empty(captured),
                "directory_operation_target_precancel_clears_previous_target_before_missing_path_io");
            captured = original;
            error = CaptureDirectoryOperationTarget((parent / L"missing-directory").wstring(), captured);
            passed &= Check(error == ERROR_FILE_NOT_FOUND && empty(captured),
                "directory_operation_target_missing_directory_returns_real_error_and_clears_previous_target");
            captured = original;
            error = CaptureDirectoryOperationTarget(behind_file.wstring(), captured);
            passed &= Check(error == ERROR_DIRECTORY && empty(captured),
                "directory_operation_target_rejects_an_actual_file_instead_of_reusing_directory_intent");
            captured = original;
            const auto root_error = CaptureDirectoryOperationTarget(root.root_path().wstring(), captured);
            const bool root_empty = empty(captured);
            const auto extended_root_error = CaptureDirectoryOperationTarget(L"\\\\?\\" + root.root_path().wstring(), captured);
            blink::core::FileIdentity root_identity;
            passed &= Check(root_error == ERROR_NOT_SUPPORTED && root_empty && extended_root_error == ERROR_NOT_SUPPORTED &&
                    empty(captured) && SUCCEEDED(ReadFileDropTarget(root.root_path().wstring(), root_identity)) && root_identity.valid,
                "directory_operation_target_rejects_transfer_roots_while_root_destination_identity_remains_available");

            const auto retained = parent / L"保留的原目录";
            fs::rename(source, retained); fs::create_directory(source);
            const auto replacement_file = source / L"替换内容.txt";
            write(replacement_file, "owned-replacement-directory");
            const auto replacement = entry(source);
            bool rename_called = false;
            const auto renamed = RenameEntrySafely({ original.parent, original.parent_identity, original.source, L"不得误改名" }, {},
                [&](HANDLE, const std::wstring&, DWORD* result) { rename_called = true; *result = ERROR_GEN_FAILURE; return false; });
            passed &= Check(replacement.identity != original.source.identity && !renamed.renamed &&
                    renamed.error == ERROR_FILE_INVALID && !rename_called && !fs::exists(parent / L"不得误改名") &&
                    entry(source).identity == replacement.identity && entry(retained).identity == original.source.identity &&
                    read(retained / relative_nested) == data && read(replacement_file) == "owned-replacement-directory",
                "directory_operation_target_stale_source_identity_is_rejected_before_any_rename_commit");
            const DeleteRequest deletion{ original.parent, original.parent_identity, original.source, DeleteMode::Permanent, true };
            const auto deleted = std::async(std::launch::async, [deletion] {
                const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                if (FAILED(initialized)) { return DeleteResult{ initialized }; }
                const auto result = DeleteEntrySafely(deletion); CoUninitialize(); return result;
            }).get();
            passed &= Check(deleted.error == HRESULT_FROM_WIN32(ERROR_FILE_INVALID) && !deleted.removed &&
                    !deleted.possible_partial && !deleted.observed_removed && entry(source).identity == replacement.identity &&
                    entry(retained).identity == original.source.identity && read(retained / relative_nested) == data &&
                    read(replacement_file) == "owned-replacement-directory",
                "directory_operation_target_stale_source_identity_is_rejected_before_shell_delete");

            TabWindowState folder_window;
            auto* folder_tab = folder_window.ActiveTab();
            const auto browse = folder_tab->state->Navigate(behind.wstring());
            const bool committed = browse && folder_tab->state->CommitBrowse(*browse,
                blink::core::EnumerateDirectory(behind.wstring())).status == CommitBrowseStatus::Committed;
            const auto selected = MakeRowIdentity(entry(behind_file));
            folder_tab->state->SetRowSelection({ selected });
            const auto owner = CaptureFileOperationOrigin(*folder_tab);
            const auto detached = FileDropOrigin(owner, Location::Folder(original.parent), original.parent_identity);
            TabWindowState pc_window;
            const auto virtual_owner = CaptureFileOperationOrigin(*pc_window.ActiveTab());
            const auto pc_detached = FileDropOrigin(virtual_owner, Location::Folder(original.parent), original.parent_identity);
            passed &= Check(committed && IsFileOperationOriginCurrent(detached, folder_tab) &&
                    detached.location != owner.location && detached.owner_location == owner.location &&
                    detached.identity == original.parent_identity && folder_tab->state->IsRowSelected(selected) &&
                    virtual_owner.location.IsThisPC() && !virtual_owner.identity.valid &&
                    IsFileOperationOriginCurrent(pc_detached, pc_window.ActiveTab()),
                "directory_operation_target_detached_parent_origin_supports_folder_and_virtual_this_pc_owners_without_selection_rebinding");
            const bool closed_rejected = !IsFileOperationOriginCurrent(detached, folder_tab, true);
            ++folder_tab->file_operation_generation;
            const bool departure_rejected = !IsFileOperationOriginCurrent(detached, folder_tab);
            const auto after_departure = FileDropOrigin(CaptureFileOperationOrigin(*folder_tab),
                Location::Folder(original.parent), original.parent_identity);
            folder_tab->EnterSearch();
            passed &= Check(closed_rejected && departure_rejected && !IsFileOperationOriginCurrent(after_departure, folder_tab),
                "directory_operation_target_detached_work_keeps_owner_departure_search_and_close_gates");
        }
        catch (const std::exception& failure)
        {
            std::cerr << "INFO directory target fixture exception " << failure.what() << '\n';
            passed &= Check(false, "directory_operation_target_fixture_completed_without_exception");
        }
        return passed;
    }

    [[nodiscard]] bool RunFileActivationValidationTests()
    {
        const auto root = std::filesystem::path(L".build") / L"phase1-activation-tests" /
            (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::error_code error;
        std::filesystem::create_directories(root.parent_path(), error);
        if (error || !std::filesystem::create_directory(root, error))
        {
            return Check(false, "activation_create_owned_fixture");
        }
        struct FixtureCleanup
        {
            std::filesystem::path path;
            ~FixtureCleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
        } cleanup{ root };
        const auto path = root / L"Blink打开测试.txt";
        { std::ofstream file(path, std::ios::binary); file << "original"; }
        blink::core::FileEntry original;
        original.full_path = blink::core::NormalizePath(path.wstring());
        bool passed = Check(blink::core::ReadEntryMetadata(
                original.full_path, original) && original.identity.valid,
            "activation_reads_real_unicode_file_identity");
        if (!original.identity.valid)
        {
            return false;
        }
        passed &= Check(blink::app::ValidateFileActivation(original) == ERROR_SUCCESS,
            "activation_accepts_original_identity");

        const auto link = root / L"hardlink.txt";
        std::filesystem::create_hard_link(path, link, error);
        blink::core::FileEntry linked;
        linked.full_path = blink::core::NormalizePath(link.wstring());
        passed &= Check(!error && blink::core::ReadEntryMetadata(
                linked.full_path, linked) &&
                linked.identity == original.identity &&
                blink::app::ValidateFileActivation(linked) == ERROR_SUCCESS,
            "activation_accepts_each_real_hardlink_path");
        std::filesystem::remove(link, error);
        passed &= Check(blink::app::ValidateFileActivation(linked) != ERROR_SUCCESS &&
                blink::app::ValidateFileActivation(original) == ERROR_SUCCESS,
            "activation_rejects_deleted_link_without_confusing_surviving_link");

        std::filesystem::rename(path, root / L"retained-original.txt", error);
        passed &= Check(!error && blink::app::ValidateFileActivation(original) != ERROR_SUCCESS,
            "activation_rejects_missing_path");
        { std::ofstream replacement(path, std::ios::binary); replacement << "replacement"; }
        // Retain the original object so the replacement cannot reuse its File ID.
        passed &= Check(blink::app::ValidateFileActivation(original) == ERROR_FILE_INVALID,
            "activation_rejects_same_name_replacement_identity");
        auto directory = original;
        directory.is_directory = true;
        passed &= Check(blink::app::ValidateFileActivation(directory) == ERROR_DIRECTORY,
            "activation_never_dispatches_directory_to_file_association");
        auto invalid = original;
        invalid.identity.valid = false;
        passed &= Check(blink::app::ValidateFileActivation(invalid) == ERROR_INVALID_DATA,
            "activation_requires_authoritative_file_identity");

        // Exercise the real controller/helper boundary without opening a user app.
        // A process/tick-specific extension has no registered association.
        const auto unassociated_path = root / (L"中文-helper.blink-unassociated-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        { std::ofstream file(unassociated_path, std::ios::binary); file << "owned helper fixture"; }
        blink::core::FileEntry unassociated;
        unassociated.full_path = blink::core::NormalizePath(unassociated_path.wstring());
        const bool metadata_ready = blink::core::ReadEntryMetadata(unassociated.full_path, unassociated);
        passed &= Check(metadata_ready && unassociated.identity.valid,
            "activation_real_helper_fixture_has_full_path_and_identity");
        if (metadata_ready && unassociated.identity.valid)
        {
            auto completed = std::make_shared<std::promise<void>>();
            auto completion = completed->get_future();
            blink::app::FileActivationController controller;
            (void)controller.Open(unassociated, [completed](std::uint64_t) { completed->set_value(); });
            const bool ready = completion.wait_for(std::chrono::seconds(35)) == std::future_status::ready;
            const auto snapshot = controller.Snapshot();
            std::cout << "activation_real_helper ready=" << ready
                      << " stage=" << static_cast<int>(snapshot.stage)
                      << " error=0x" << std::hex << static_cast<std::uint32_t>(snapshot.error)
                      << std::dec << "\n";
            passed &= Check(ready && snapshot.stage == blink::app::FileActivationStage::Failed &&
                    snapshot.error == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION),
                "activation_real_helper_preserves_path_and_reports_missing_association");
            controller.Shutdown();
        }
        return passed;
    }

    struct ActivationProbe
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered{};
        bool release{};
        bool exited{};
        std::size_t launches{};
        std::size_t publications{};
        DWORD worker_thread{};
    };

    [[nodiscard]] bool RunFileActivationControllerTests()
    {
        using blink::app::FileActivationController;
        using blink::app::FileActivationStage;
        const auto entry = TestEntry(L"file.txt", TestIdentity(89), 1);
        bool passed = true;
        auto probe = std::make_shared<ActivationProbe>();
        const auto publish = [probe](std::uint64_t) {
            std::scoped_lock lock(probe->mutex);
            ++probe->publications;
            probe->changed.notify_all();
        };
        FileActivationController denied(
            [](const auto&) { return DWORD{ ERROR_ACCESS_DENIED }; },
            [probe](const auto&) {
                std::scoped_lock lock(probe->mutex);
                ++probe->launches;
                return S_OK;
            });
        (void)denied.Open(entry, publish);
        {
            std::unique_lock lock(probe->mutex);
            passed &= Check(probe->changed.wait_for(lock, std::chrono::seconds(3),
                    [&] { return probe->publications == 1; }) && probe->launches == 0,
                "activation_validation_failure_never_launches");
        }
        passed &= Check(denied.Snapshot().stage == FileActivationStage::Failed &&
                denied.Snapshot().error == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED),
            "activation_reports_permission_failure_without_success");

        probe = std::make_shared<ActivationProbe>();
        FileActivationController no_association(
            [](const auto&) { return DWORD{ ERROR_SUCCESS }; },
            [probe](const auto&) {
                std::scoped_lock lock(probe->mutex);
                probe->worker_thread = GetCurrentThreadId();
                ++probe->launches;
                return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
            });
        (void)no_association.Open(entry, [probe](std::uint64_t) {
            std::scoped_lock lock(probe->mutex);
            ++probe->publications;
            probe->changed.notify_all();
        });
        {
            std::unique_lock lock(probe->mutex);
            passed &= Check(probe->changed.wait_for(lock, std::chrono::seconds(3),
                    [&] { return probe->publications == 1; }) &&
                    probe->worker_thread != GetCurrentThreadId() && probe->launches == 1,
                "activation_dispatch_runs_on_worker_once");
        }
        passed &= Check(no_association.Snapshot().stage == FileActivationStage::Failed &&
                no_association.Snapshot().error == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION),
            "activation_no_association_error_is_visible");

        probe = std::make_shared<ActivationProbe>();
        FileActivationController success(
            [](const auto&) { return DWORD{ ERROR_SUCCESS }; },
            [probe](const auto&) {
                std::scoped_lock lock(probe->mutex);
                ++probe->launches;
                return S_OK;
            });
        const auto success_generation = success.Open(entry, [probe](std::uint64_t) {
            std::scoped_lock lock(probe->mutex);
            ++probe->publications;
            probe->changed.notify_all();
        });
        {
            std::unique_lock lock(probe->mutex);
            passed &= Check(probe->changed.wait_for(lock, std::chrono::seconds(3),
                    [&] { return probe->publications == 1; }) && probe->launches == 1,
                "activation_success_dispatches_and_publishes_once");
        }
        passed &= Check(success.IsCurrent(success_generation) &&
                success.Snapshot().stage == FileActivationStage::Dispatched &&
                success.Snapshot().path == entry.full_path && success.Snapshot().error == S_OK,
            "activation_success_reports_dispatch_without_claiming_application_readiness");

        for (const bool close_during_launch : { false, true })
        {
            probe = std::make_shared<ActivationProbe>();
            const auto blocked = [probe] {
                std::unique_lock lock(probe->mutex);
                probe->entered = true;
                probe->changed.notify_all();
                const bool released = probe->changed.wait_for(lock, std::chrono::seconds(5),
                    [&] { return probe->release; });
                probe->exited = true;
                probe->changed.notify_all();
                return released;
            };
            FileActivationController controller(
                [blocked, close_during_launch](const auto&) {
                    return static_cast<DWORD>(
                        close_during_launch || blocked() ? ERROR_SUCCESS : ERROR_TIMEOUT);
                },
                [blocked, probe, close_during_launch](const auto&) {
                    { std::scoped_lock lock(probe->mutex); ++probe->launches; }
                    return !close_during_launch || blocked() ? S_OK : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
                });
            const auto generation = controller.Open(entry, [probe](std::uint64_t) {
                std::scoped_lock lock(probe->mutex);
                ++probe->publications;
                probe->changed.notify_all();
            });
            {
                std::unique_lock lock(probe->mutex);
                passed &= Check(probe->changed.wait_for(lock, std::chrono::seconds(3),
                        [&] { return probe->entered; }),
                    "activation_submission_returns_before_blocking_worker_finishes");
            }
            const auto before = std::chrono::steady_clock::now();
            if (close_during_launch) { controller.Shutdown(); }
            else { controller.CancelPending(); }
            passed &= Check(std::chrono::steady_clock::now() - before < std::chrono::seconds(1) &&
                    !controller.IsCurrent(generation) &&
                    controller.Snapshot().stage == FileActivationStage::Idle,
                "activation_cancel_or_close_never_joins_blocking_worker");
            {
                std::unique_lock lock(probe->mutex);
                probe->release = true;
                probe->changed.notify_all();
                passed &= Check(probe->changed.wait_for(lock, std::chrono::seconds(3),
                        [&] { return probe->exited; }), "activation_owned_worker_is_released");
                const bool late = probe->changed.wait_for(lock, std::chrono::milliseconds(200),
                    [&] { return probe->publications != 0 || (!close_during_launch && probe->launches != 0); });
                passed &= Check(!late, "activation_cancel_drops_late_result_and_undispatched_launch");
            }
            if (close_during_launch)
            {
                passed &= Check(controller.Open(entry, {}) == 0,
                    "activation_closed_controller_cannot_start_new_work");
            }
        }
        return passed;
    }

    [[nodiscard]] bool RunRowIdentityHashAndEqualityTests()
    {
        using blink::app::RowIdentity;
        using blink::app::RowIdentityHash;

        bool passed = true;

        const auto file_id_1 = TestIdentity(10);
        const auto file_id_2 = TestIdentity(20);

        const RowIdentity row_a{ file_id_1, L"document.txt" };
        const RowIdentity row_a_copy{ file_id_1, L"document.txt" };
        const RowIdentity row_hardlink{ file_id_1, L"document_hardlink.txt" };
        const RowIdentity row_different_file_same_name{ file_id_2, L"document.txt" };
        const RowIdentity row_case_diff{ file_id_1, L"DOCUMENT.TXT" };

        std::unordered_set<RowIdentity, RowIdentityHash> set;

        // 1. Identical RowIdentity equality and hash membership
        passed &= Check(row_a == row_a_copy, "row_identity_equality_identical");
        set.insert(row_a);
        passed &= Check(set.size() == 1, "row_set_insert_initial");
        passed &= Check(set.count(row_a_copy) == 1, "row_set_find_identical_copy");
        set.insert(row_a_copy);
        passed &= Check(set.size() == 1, "row_set_deduplicates_identical");

        // 2. Same FileIdentity + different hard-link names remain distinct
        passed &= Check(row_a != row_hardlink, "row_identity_distinct_hardlink_names");
        set.insert(row_hardlink);
        passed &= Check(set.size() == 2, "row_set_hardlinks_distinct_size");
        passed &= Check(set.count(row_a) == 1 && set.count(row_hardlink) == 1, "row_set_both_hardlinks_present");

        // 3. Different FileIdentity + same name remain distinct
        passed &= Check(row_a != row_different_file_same_name, "row_identity_distinct_different_files_same_name");
        set.insert(row_different_file_same_name);
        passed &= Check(set.size() == 3, "row_set_different_files_same_name_size");
        passed &= Check(set.count(row_different_file_same_name) == 1, "row_set_different_files_same_name_present");

        // 4. Current case-sensitive row-name semantics
        passed &= Check(row_a != row_case_diff, "row_identity_case_sensitive_names_distinct");
        set.insert(row_case_diff);
        passed &= Check(set.size() == 4, "row_set_case_sensitive_size");
        passed &= Check(set.count(row_case_diff) == 1, "row_set_case_sensitive_present");

        return passed;
    }

    [[nodiscard]] bool RunInactiveTabThisPcTreeSyncRegression()
    {
        using blink::app::Location;
        using blink::app::NavigationTreeModel;
        using blink::app::NavigationTreeNodeKind;
        using blink::app::NavigationTreeNodeSpec;
        using blink::app::TabWindowState;
        using blink::app::ThisPcContent;
        using blink::app::ThisPcItem;
        using blink::app::ThisPcItemKind;

        bool passed = true;

        TabWindowState window_state;
        const auto tab_a_id = window_state.ActiveTabId();
        const auto tab_b_id = window_state.CreateTab();

        // 1. Tab A is active in a deep folder
        const std::wstring deep_path = L"C:\\deep_folder\\subfolder";
        const auto nav_a = window_state.Find(tab_a_id)->state->Navigate(deep_path);
        passed &= Check(nav_a.has_value(), "tree_sync_nav_a");
        if (nav_a)
        {
            (void)window_state.Find(tab_a_id)->state->CommitBrowse(*nav_a, Listing(deep_path));
        }

        // Shared tree is initialized via production SynchronizeThisPcContent
        NavigationTreeModel tree;
        const std::vector<NavigationTreeNodeSpec> initial_specs = {
            NavigationTreeNodeSpec{ NavigationTreeNodeKind::Volume, L"C:\\", Location::Folder(L"C:\\") },
            NavigationTreeNodeSpec{ NavigationTreeNodeKind::Volume, L"D:\\", Location::Folder(L"D:\\") }
        };
        const bool initial_built = tree.SynchronizeThisPcContent(L"This PC", initial_specs);
        passed &= Check(initial_built, "tree_sync_initial_built");
        const auto root_id = tree.RootId();
        passed &= Check(root_id != 0, "tree_sync_root_valid");

        // Production reveal expands ancestors for Tab A
        const auto c_node_id = tree.FindChild(root_id, Location::Folder(L"C:\\"));
        passed &= Check(c_node_id != 0, "tree_sync_c_node_valid");
        const auto token_c = tree.BeginExpansion(c_node_id);
        if (token_c)
        {
            (void)tree.CompleteExpansion(*token_c, {
                { L"deep_folder", Location::Folder(L"C:\\deep_folder") }
            });
        }
        const auto deep_folder_id = tree.FindChild(c_node_id, Location::Folder(L"C:\\deep_folder"));
        passed &= Check(deep_folder_id != 0, "tree_sync_deep_folder_valid");
        const auto token_deep = tree.BeginExpansion(deep_folder_id);
        if (token_deep)
        {
            (void)tree.CompleteExpansion(*token_deep, {
                { L"subfolder", Location::Folder(deep_path) }
            });
        }
        const auto exact_leaf_id = tree.FindExact(Location::Folder(deep_path));
        passed &= Check(exact_leaf_id != 0, "tree_sync_tab_a_node_expanded");

        // 2. Tab B finishes ThisPC while inactive
        const auto nav_b = window_state.Find(tab_b_id)->state->Navigate(Location::ThisPC());
        passed &= Check(nav_b.has_value(), "tree_sync_nav_b");
        ThisPcContent content_b;
        content_b.items = {
            ThisPcItem{ ThisPcItemKind::Volume, L"", L"C:\\", Location::Folder(L"C:\\") },
            ThisPcItem{ ThisPcItemKind::Volume, L"", L"D:\\", Location::Folder(L"D:\\") },
        };
        if (nav_b)
        {
            (void)window_state.Find(tab_b_id)->state->CommitThisPC(*nav_b, content_b);
        }

        // 3. Tab A is still active; shared tree remains unchanged
        passed &= Check(window_state.ActiveTabId() == tab_a_id, "tree_sync_tab_a_still_active");
        passed &= Check(tree.FindExact(Location::Folder(deep_path)) == exact_leaf_id,
            "tree_sync_tab_a_tree_preserved_while_b_inactive");

        // 4. Switch to Tab B
        (void)window_state.ActivateTab(tab_b_id);
        passed &= Check(window_state.ActiveTabId() == tab_b_id, "tree_sync_switch_to_b");
        passed &= Check(window_state.Find(tab_b_id)->state->CommittedThisPCSnapshot().items.size() == 2,
            "tree_sync_tab_b_items_correct");

        // Shared tree synchronizes from Tab B's committed ThisPC content
        std::vector<NavigationTreeNodeSpec> b_tree_specs;
        for (const auto& item : window_state.Find(tab_b_id)->state->CommittedThisPCSnapshot().items)
        {
            if (item.kind == ThisPcItemKind::Volume)
            {
                b_tree_specs.push_back(NavigationTreeNodeSpec{
                    NavigationTreeNodeKind::Volume,
                    item.display_name,
                    item.target
                });
            }
        }
        // First sync for Tab B: top-level specs match, so no tree reset/churn occurs
        const bool b_sync_1 = tree.SynchronizeThisPcContent(L"This PC", b_tree_specs);
        passed &= Check(!b_sync_1, "tree_sync_b_idempotent_no_reset");
        passed &= Check(tree.RootId() == root_id, "tree_sync_b_root_preserved");
        passed &= Check(tree.FindExact(Location::Folder(deep_path)) == exact_leaf_id,
            "tree_sync_b_preserves_expanded_subtree_in_shared_tree");

        // 5. Presenting Tab B a second time is strictly idempotent
        const bool b_sync_2 = tree.SynchronizeThisPcContent(L"This PC", b_tree_specs);
        passed &= Check(!b_sync_2, "tree_sync_b_second_call_idempotent");

        // 6. Switch back to Tab A
        (void)window_state.ActivateTab(tab_a_id);
        passed &= Check(window_state.ActiveTabId() == tab_a_id, "tree_sync_switch_back_to_a");
        passed &= Check(window_state.Find(tab_a_id)->state->PresentationSnapshot().committed_location.FolderPath() == deep_path,
            "tree_sync_tab_a_location_preserved");

        // Production reveal path: FindBestAncestor immediately returns the exact leaf node without re-enumeration
        const auto best_ancestor_id = tree.FindBestAncestor(Location::Folder(deep_path));
        passed &= Check(best_ancestor_id == exact_leaf_id, "tree_sync_tab_a_best_ancestor_is_exact_leaf");
        const auto* leaf_node = tree.Find(best_ancestor_id);
        passed &= Check(leaf_node != nullptr && leaf_node->target == Location::Folder(deep_path),
            "tree_sync_tab_a_tree_restored_without_reenumeration");

        return passed;
    }
}

namespace
{
    bool RunFolderCommandPolicyTests()
    {
        using blink::app::EvaluateFolderCommand;
        using blink::app::FolderCommand;
        using blink::app::FolderCommandCapabilities;
        using blink::app::FolderCommandStatus;
        bool passed = true;
        FolderCommandCapabilities folder{ true, false, 1, true };
        passed &= Check(EvaluateFolderCommand(FolderCommand::Open, folder) ==
            FolderCommandStatus::Ready, "command_open_selected_committed_row");
        folder.has_open_target = false;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Open, folder) ==
            FolderCommandStatus::TargetUnavailable, "command_open_requires_resolved_visible_target");
        folder.selected_count = 0;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Open, folder) ==
            FolderCommandStatus::SelectionRequired, "command_open_without_selection_rejected");
        folder.selected_count = 2;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Open, folder) ==
            FolderCommandStatus::SingleSelectionRequired, "command_open_multi_selection_not_implicitly_dispatched");
        passed &= Check(EvaluateFolderCommand(FolderCommand::Rename, folder) ==
            FolderCommandStatus::SingleSelectionRequired, "command_rename_rejects_multiple_rows");
        passed &= Check(EvaluateFolderCommand(FolderCommand::Properties, folder) ==
            FolderCommandStatus::TargetUnavailable, "command_properties_multi_row_requires_directory_identity");
        for (const auto command : { FolderCommand::Cut,
            FolderCommand::Delete, FolderCommand::PermanentDelete })
        {
            passed &= Check(EvaluateFolderCommand(command, folder) ==
                FolderCommandStatus::TargetUnavailable,
                "command_multi_row_mutation_reports_unavailable_capability");
        }
        folder.selected_count = 0;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Delete, folder) ==
            FolderCommandStatus::SelectionRequired, "command_delete_requires_selection");
        passed &= Check(EvaluateFolderCommand(FolderCommand::Paste, folder) ==
            FolderCommandStatus::TargetUnavailable, "command_background_paste_requires_directory_identity");
        passed &= Check(EvaluateFolderCommand(FolderCommand::NewFolder, folder) ==
            FolderCommandStatus::TargetUnavailable, "command_new_folder_requires_committed_directory_identity");
        passed &= Check(EvaluateFolderCommand(FolderCommand::Copy, folder) == FolderCommandStatus::SelectionRequired,
            "command_copy_requires_selection");
        folder.has_directory_identity = true;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Paste, folder) == FolderCommandStatus::Ready,
            "command_single_file_paste_ready_without_selection");
        folder.selected_count = 1; folder.has_open_target = true;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Copy, folder) == FolderCommandStatus::Ready,
            "command_single_file_copy_ready");
        folder.selected_count = 2;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Copy, folder) == FolderCommandStatus::Ready,
            "command_copy_accepts_multiple_selected_committed_rows");
        folder.has_open_target = false;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Copy, folder) == FolderCommandStatus::Ready,
            "command_multi_copy_uses_committed_selection_without_a_single_open_target");
        passed &= Check(EvaluateFolderCommand(FolderCommand::CopyFullPath, folder) == FolderCommandStatus::Ready &&
            EvaluateFolderCommand(FolderCommand::CopyParentPath, folder) == FolderCommandStatus::Ready &&
            EvaluateFolderCommand(FolderCommand::Rename, folder) == FolderCommandStatus::SingleSelectionRequired,
            "command_multi_paths_keep_both_rows_without_enabling_multi_rename");
        folder.mutation_busy = true;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Copy, folder) == FolderCommandStatus::Busy,
            "command_multi_copy_respects_owned_queue_busy_state");
        passed &= Check(EvaluateFolderCommand(FolderCommand::CopyFullPath, folder) == FolderCommandStatus::Busy &&
            EvaluateFolderCommand(FolderCommand::CopyParentPath, folder) == FolderCommandStatus::Busy,
            "command_path_clipboard_respects_existing_publication_busy_state");
        folder.mutation_busy = false;
        folder.selected_count = 0;
        passed &= Check(EvaluateFolderCommand(FolderCommand::NewFolder, folder) == FolderCommandStatus::Ready,
            "command_new_folder_ready_without_selection");
        folder.mutation_busy = true;
        passed &= Check(EvaluateFolderCommand(FolderCommand::NewFolder, folder) == FolderCommandStatus::Busy &&
            EvaluateFolderCommand(FolderCommand::Refresh, folder) == FolderCommandStatus::Ready,
            "command_new_folder_mutation_busy_does_not_block_navigation_commands");
        folder.mutation_busy = false;
        folder.is_folder = false;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Paste, folder) ==
            FolderCommandStatus::FolderRequired, "command_this_pc_rejects_filesystem_paste");
        passed &= Check(EvaluateFolderCommand(FolderCommand::SortName, folder) ==
            FolderCommandStatus::FolderRequired, "command_this_pc_sort_disabled");
        passed &= Check(EvaluateFolderCommand(FolderCommand::ViewGrid, folder) ==
            FolderCommandStatus::Ready, "command_this_pc_view_supported");
        passed &= Check(EvaluateFolderCommand(FolderCommand::Refresh, folder) ==
            FolderCommandStatus::Ready, "command_this_pc_refresh_supported");
        folder.selected_count = 1;
        folder.has_open_target = true;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Open, folder) ==
            FolderCommandStatus::Ready, "command_this_pc_opens_own_location_target");
        folder.is_folder = true;
        passed &= Check(EvaluateFolderCommand(FolderCommand::SortDescending, folder) ==
            FolderCommandStatus::Ready, "command_folder_sort_supported");
        passed &= Check(EvaluateFolderCommand(FolderCommand::Rename, folder) ==
            FolderCommandStatus::Ready, "command_single_rename_ready_with_valid_directory_and_target");
        folder.busy = true;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Open, folder) ==
            FolderCommandStatus::Busy &&
            EvaluateFolderCommand(FolderCommand::Refresh, folder) == FolderCommandStatus::Busy &&
            EvaluateFolderCommand(FolderCommand::ViewList, folder) == FolderCommandStatus::Busy &&
            EvaluateFolderCommand(FolderCommand::Delete, folder) == FolderCommandStatus::Busy,
            "command_pending_or_projection_work_blocks_dispatch");
        const blink::app::Localization localization;
        passed &= Check(localization.GetString(L"Command.Copy") == L"Copy" &&
            localization.Format(L"Status.CommandNotImplemented", L"Copy") ==
                L"Copy is not available yet.", "command_feedback_fallback_is_localized");
        return passed;
    }
}

namespace
{
    bool WaitForWatchStop(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        if (!action) { return true; }
        if (action.Status() != winrt::Windows::Foundation::AsyncStatus::Started)
        { return action.Status() == winrt::Windows::Foundation::AsyncStatus::Completed; }
        auto completion = std::make_shared<std::promise<winrt::Windows::Foundation::AsyncStatus>>();
        auto finished = completion->get_future();
        action.Completed([completion](auto&&, const auto status) { completion->set_value(status); });
        return finished.wait_for(std::chrono::seconds(5)) == std::future_status::ready &&
            finished.get() == winrt::Windows::Foundation::AsyncStatus::Completed;
    }

    bool RunNativeFolderWatcherTests()
    {
        using blink::app::FolderWatchEvent;
        using blink::app::FolderWatchEventKind;
        using namespace std::chrono_literals;
        bool passed = true;
        const auto root = std::filesystem::absolute(std::filesystem::path(L".build") /
            L"phase1-watcher-tests" / (L"pid-" + std::to_wstring(GetCurrentProcessId())));
        std::filesystem::create_directories(root / L"中文目录");
        std::filesystem::create_directories(root / L"other");
        struct Cleanup
        {
            std::filesystem::path path;
            ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
        } cleanup{ root };
        struct Probe
        {
            std::mutex mutex;
            std::condition_variable changed;
            std::size_t notifications{};
            std::size_t ready{};
            DWORD error{};
            void Publish(FolderWatchEvent event)
            {
                std::scoped_lock lock(mutex);
                if (event.kind == FolderWatchEventKind::Ready) { ++ready; }
                else if (event.kind == FolderWatchEventKind::Unavailable) { error = event.error; }
                else { ++notifications; }
                changed.notify_all();
            }
            std::size_t Count() { std::scoped_lock lock(mutex); return notifications; }
            bool WaitChange(std::size_t before)
            {
                std::unique_lock lock(mutex);
                return changed.wait_for(lock, 5s, [&] { return notifications > before || error; }) && !error;
            }
            bool WaitReady(std::size_t before = 0)
            {
                std::unique_lock lock(mutex);
                return changed.wait_for(lock, 5s, [&] { return ready > before || error; }) && !error;
            }
        };
        const auto probe = std::make_shared<Probe>();
        blink::app::FolderWatcher watcher;
        const auto folder = root / L"中文目录";
        watcher.Start(folder.wstring(), [probe](auto event) { probe->Publish(event); });
        if (!Check(probe->WaitReady(), "watch_native_unicode_directory_arms_async_read"))
        {
            auto stopped = watcher.Stop(); (void)WaitForWatchStop(stopped);
            return false;
        }
        auto before = probe->Count();
        const auto file = folder / L"新增文件.txt";
        { std::ofstream output(file); output << "initial"; }
        passed &= Check(probe->WaitChange(before), "watch_native_external_create_invalidates_snapshot");
        // Re-enumeration is the truth; preserve a valid selected row.
        blink::app::TabState tab;
        auto request = tab.Navigate(folder.wstring());
        auto listing = blink::core::EnumerateDirectory(folder.wstring());
        passed &= Check(request && CommitSucceeded(tab.CommitBrowse(*request, listing)) &&
            !listing.entries.empty() && tab.SelectEntry(listing.entries.front()), "watch_reconcile_fixture_has_valid_selection");
        before = probe->Count();
        const auto extra = folder / L"second.txt";
        { std::ofstream output(extra); output << "second"; }
        passed &= Check(probe->WaitChange(before), "watch_native_second_create_received");
        request = tab.Refresh();
        passed &= Check(request && CommitSucceeded(tab.CommitBrowse(*request,
            blink::core::EnumerateDirectory(folder.wstring()))) &&
            tab.PresentationSnapshot().selected_count == 1 &&
            tab.PresentationSnapshot().committed_item_count == 2,
            "watch_reconcile_preserves_existing_row_and_committed_page");
        before = probe->Count();
        const auto renamed = folder / L"重命名.txt";
        std::filesystem::rename(file, renamed);
        passed &= Check(probe->WaitChange(before) && std::filesystem::exists(renamed), "watch_native_external_rename_received");
        before = probe->Count();
        passed &= Check(SetFileAttributesW(renamed.c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE,
            "watch_native_attribute_fixture_updated");
        passed &= Check(probe->WaitChange(before), "watch_native_hidden_attribute_change_received");
        (void)SetFileAttributesW(renamed.c_str(), FILE_ATTRIBUTE_NORMAL);
        before = probe->Count();
        { std::ofstream output(extra, std::ios::app); output << "size change"; }
        passed &= Check(probe->WaitChange(before), "watch_native_size_and_last_write_change_received");
        before = probe->Count();
        std::filesystem::remove(renamed);
        passed &= Check(probe->WaitChange(before), "watch_native_external_delete_received");
        request = tab.Refresh();
        passed &= Check(request && CommitSucceeded(tab.CommitBrowse(*request,
            blink::core::EnumerateDirectory(folder.wstring()))) && tab.PresentationSnapshot().selected_count == 0,
            "watch_reconcile_prunes_deleted_or_renamed_row_selection");
        auto retired = watcher.Stop();
        if (!WaitForWatchStop(retired)) { return Check(false, "watch_stop_has_bounded_native_completion"); }
        passed &= Check(!retired || retired.Status() == winrt::Windows::Foundation::AsyncStatus::Completed,
            "watch_stop_drains_pending_read_before_releasing_native_storage");
        const auto other = std::make_shared<Probe>();
        watcher.Start((root / L"other").wstring(), [other](auto event) { other->Publish(event); });
        passed &= Check(other->WaitReady(), "watch_replacement_directory_arms_independently");
        before = probe->Count();
        { std::ofstream output(folder / L"after-stop.txt"); output << "old target"; }
        passed &= Check(probe->Count() == before, "watch_stopped_session_does_not_republish_changes");
        auto other_before = other->Count();
        { std::ofstream output(root / L"other" / L"new.txt"); output << "new target"; }
        passed &= Check(other->WaitChange(other_before), "watch_replacement_receives_only_new_directory_changes");
        retired = watcher.Stop(); if (!WaitForWatchStop(retired)) { return Check(false, "watch_stop_has_bounded_native_completion"); }
        const auto failure = std::make_shared<Probe>();
        watcher.Start((root / L"missing").wstring(), [failure](auto event) { failure->Publish(event); });
        {
            std::unique_lock lock(failure->mutex);
            passed &= Check(failure->changed.wait_for(lock, 5s, [&] { return failure->error != ERROR_SUCCESS; }),
                "watch_unavailable_directory_reports_failure_without_fallback");
        }
        retired = watcher.Stop(); if (!WaitForWatchStop(retired)) { return Check(false, "watch_stop_has_bounded_native_completion"); }
        // A moved directory may retain a valid old handle. Reopening the same
        // path must subscribe to its replacement rather than that old object.
        const auto reused_path = root / L"same-path";
        std::filesystem::create_directories(reused_path);
        const auto old_object = std::make_shared<Probe>();
        watcher.Start(reused_path.wstring(), [old_object](auto event) { old_object->Publish(event); });
        passed &= Check(old_object->WaitReady(), "watch_same_path_replacement_fixture_ready");
        std::filesystem::rename(reused_path, root / L"moved-object");
        std::filesystem::create_directories(reused_path);
        const auto replacement = std::make_shared<Probe>();
        watcher.Start(reused_path.wstring(), [replacement](auto event) { replacement->Publish(event); });
        const bool replacement_ready = replacement->WaitReady();
        before = replacement->Count();
        { std::ofstream output(reused_path / L"new-object.txt"); output << "replacement directory"; }
        passed &= Check(replacement_ready && replacement->WaitChange(before),
            "watch_reopening_same_path_subscribes_to_replacement_directory");
        retired = watcher.Stop(); if (!WaitForWatchStop(retired)) { return Check(false, "watch_rebound_stop_completes"); }
        // A parent subscription never descends into an already-existing child.
        const auto child = folder / L"child";
        std::filesystem::create_directories(child);
        const auto nonrecursive = std::make_shared<Probe>();
        watcher.Start(folder.wstring(), [nonrecursive](auto event) { nonrecursive->Publish(event); });
        passed &= Check(nonrecursive->WaitReady(), "watch_nonrecursive_parent_fixture_ready");
        { std::ofstream output(child / L"nested.txt"); output << "nested"; }
        {
            std::unique_lock lock(nonrecursive->mutex);
            passed &= Check(!nonrecursive->changed.wait_for(lock, 250ms,
                [&] { return nonrecursive->notifications > 0 || nonrecursive->error; }),
                "watch_parent_does_not_subscribe_to_nested_file_changes");
        }
        retired = watcher.Stop();
        if (!WaitForWatchStop(retired)) { return Check(false, "watch_nonrecursive_stop_completes"); }
        bool cancellations_completed = true;
        for (int repeat = 0; repeat < 20; ++repeat)
        {
            watcher.Start(folder.wstring(), [](auto) {});
            if (!WaitForWatchStop(watcher.Stop())) { cancellations_completed = false; break; }
        }
        passed &= Check(cancellations_completed, "watch_repeated_start_immediate_cancel_has_bounded_cleanup");
        blink::app::TabWindowState tabs;
        const auto first = tabs.ActiveTabId();
        const auto second = tabs.CreateTab();
        const auto closing = std::make_shared<Probe>();
        auto* record = tabs.Find(second);
        const auto closing_token = record->folder_changes.Begin(folder.wstring());
        record->folder_watcher->Start(folder.wstring(), [closing](auto event) { closing->Publish(event); });
        passed &= Check(closing->WaitReady() && record->folder_changes.IsCurrent(closing_token) &&
            tabs.CloseTab(second) == blink::app::CloseTabStatus::Closed &&
            tabs.Find(second) == nullptr && tabs.Find(first) != nullptr,
            "watch_inactive_tab_close_owns_and_cancels_subscription");
        const auto window_token = tabs.Find(first)->folder_changes.Begin(folder.wstring());
        const auto active_closing = std::make_shared<Probe>();
        tabs.Find(first)->folder_watcher->Start(folder.wstring(),
            [active_closing](auto event) { active_closing->Publish(event); });
        passed &= Check(active_closing->WaitReady(), "watch_window_close_fixture_has_live_subscription");
        tabs.Shutdown();
        passed &= Check(!tabs.Find(first)->folder_changes.IsCurrent(window_token) &&
            !tabs.Find(first)->folder_watcher->Stop(),
            "watch_window_shutdown_invalidates_all_tab_tokens");
        // Stop before the coroutine has opened a directory is also a valid lifecycle.
        watcher.Start(folder.wstring(), [probe](auto event) { probe->Publish(event); });
        retired = watcher.Stop(); if (!WaitForWatchStop(retired)) { return Check(false, "watch_stop_has_bounded_native_completion"); }
        passed &= Check(!retired || retired.Status() == winrt::Windows::Foundation::AsyncStatus::Completed,
            "watch_immediate_stop_completes_without_waiting_for_directory_changes");
        return passed;
    }

    bool RunFolderChangeStateTests()
    {
        using blink::app::FolderChangeState;
        using namespace std::chrono_literals;
        bool passed = true;
        FolderChangeState state;
        const auto now = FolderChangeState::Clock::time_point{};
        const auto a = state.Begin(L"D:\\watch-a");
        passed &= Check(!state.TakeRefresh(a, now + 1s, false), "watch_clean_folder_does_not_poll");
        passed &= Check(state.Notify(a, now), "watch_current_notification_marks_dirty");
        passed &= Check(!state.TakeRefresh(a, now + 199ms, false), "watch_debounce_waits_for_quiet_period");
        passed &= Check(state.Notify(a, now + 150ms), "watch_burst_is_coalesced");
        passed &= Check(!state.TakeRefresh(a, now + 300ms, false), "watch_new_change_extends_quiet_period");
        passed &= Check(!state.TakeRefresh(a, now + 500ms, true) && state.HasChanges(), "watch_busy_browse_preserves_dirty");
        passed &= Check(state.TakeRefresh(a, now + 500ms, false), "watch_ready_page_reconciles_once");
        passed &= Check(!state.TakeRefresh(a, now + 1s, false), "watch_consumed_changes_do_not_repeat");
        passed &= Check(state.Notify(a, now + 600ms), "watch_change_during_refresh_is_retained");
        passed &= Check(state.TakeRefresh(a, now + 800ms, false), "watch_changes_after_refresh_start_reconcile_again");
        (void)state.Notify(a, now + 1s);
        (void)state.Notify(a, now + 1900ms);
        passed &= Check(state.TakeRefresh(a, now + 2s, false), "watch_continuous_changes_have_one_second_bound");
        const auto b = state.Begin(L"D:\\watch-b");
        passed &= Check(!state.Notify(a, now + 2s) && !state.TakeRefresh(a, now + 3s, false), "watch_old_directory_callbacks_are_rejected");
        passed &= Check(!state.HasChanges() && b.generation > a.generation, "watch_navigation_clears_old_dirty_and_advances_generation");
        const auto a2 = state.Begin(a.path);
        passed &= Check(!state.Notify(a, now + 3s) && state.Notify(a2, now + 3s), "watch_a_b_a_does_not_reuse_callback_generation");
        state.Stop();
        passed &= Check(!state.Notify(a2, now + 4s) && !state.HasChanges() && !state.IsCurrent(a2), "watch_close_invalidates_dirty_and_queued_callback");
        return passed;
    }
}

namespace
{
    blink::app::IndexRpcResult IndexReply(const blink::index::ipc::IndexState state)
    {
        blink::app::IndexRpcResult reply;
        reply.received = true;
        reply.expected_request_id = 12;
        reply.response.request_id = 12;
        reply.response.service_state = blink::index::ipc::ServiceState::Running;
        reply.response.status = blink::index::ipc::ResponseStatus::Ok;
        reply.response.error = ERROR_SUCCESS;
        reply.response.index_state = state;
        return reply;
    }

    bool RunIndexServiceStateTests()
    {
        using namespace blink::app;
        namespace ipc = blink::index::ipc;
        bool passed = true;
        auto reply = IndexReply(ipc::IndexState::Healthy);
        reply.response.files = 20;
        reply.response.directories = 3;
        passed &= Check(ValidateIndexReply(reply) == ERROR_SUCCESS, "index_status_accepts_validated_wire_reply");
        auto volume = MapVolumeIndexStatus(L"C:\\", reply);
        passed &= Check(volume.root == L"C:\\" && volume.stage == VolumeIndexStage::Healthy &&
            volume.files == 20 && volume.directories == 3 && volume.request_id == 12,
            "index_status_healthy_preserves_volume_counters_and_request_id");
        reply.response.request_id = 13;
        passed &= Check(ValidateIndexReply(reply) == ERROR_REVISION_MISMATCH &&
            MapVolumeIndexStatus(L"C:\\", reply).stage == VolumeIndexStage::Unavailable,
            "index_status_rejects_wrong_request_id");
        reply = IndexReply(ipc::IndexState::Healthy);
        reply.response.protocol_version = 2;
        passed &= Check(ValidateIndexReply(reply) == ERROR_REVISION_MISMATCH, "index_status_rejects_wrong_protocol");
        reply = IndexReply(ipc::IndexState::Healthy);
        reply.expected_request_id = 0;
        passed &= Check(ValidateIndexReply(reply) == ERROR_INVALID_DATA, "index_status_requires_transport_validated_request_id");
        reply = IndexReply(ipc::IndexState::Healthy);
        reply.received = false;
        reply.error = ERROR_ACCESS_DENIED;
        volume = MapVolumeIndexStatus(L"D:\\", reply);
        passed &= Check(volume.stage == VolumeIndexStage::Unavailable && volume.error == ERROR_ACCESS_DENIED,
            "index_status_transport_failure_is_visible_without_fake_healthy");
        reply = IndexReply(ipc::IndexState::Healthy);
        reply.response.status = ipc::ResponseStatus::Failed;
        reply.response.error = ERROR_INVALID_DATA;
        passed &= Check(MapVolumeIndexStatus(L"C:\\", reply).stage == VolumeIndexStage::Unavailable,
            "index_status_failed_healthy_payload_is_not_healthy");
        for (const auto pair : { std::pair{ipc::IndexState::CatchingUp, VolumeIndexStage::CatchingUp},
            std::pair{ipc::IndexState::RebuildRequired, VolumeIndexStage::RebuildRequired},
            std::pair{ipc::IndexState::Rebuilding, VolumeIndexStage::Rebuilding},
            std::pair{ipc::IndexState::Unavailable, VolumeIndexStage::Unavailable},
            std::pair{ipc::IndexState::Corrupted, VolumeIndexStage::Corrupted} })
        {
            reply = IndexReply(pair.first);
            reply.response.status = ipc::ResponseStatus::Failed;
            passed &= Check(MapVolumeIndexStatus(L"C:\\", reply).stage == pair.second,
                "index_status_preserves_nonhealthy_reported_state");
        }
        reply = IndexReply(ipc::IndexState::Rebuilding);
        reply.response.status = ipc::ResponseStatus::Busy;
        reply.response.error = ERROR_IO_PENDING;
        reply.response.operation_id = 999;
        reply.response.records_seen = 2000;
        volume = MapVolumeIndexStatus(L"D:\\", reply);
        passed &= Check(volume.stage == VolumeIndexStage::Unconfirmed && volume.records_seen == 0,
            "index_status_global_busy_progress_is_not_assigned_to_every_volume");
        reply = IndexReply(static_cast<ipc::IndexState>(99));
        passed &= Check(ValidateIndexReply(reply) == ERROR_INVALID_DATA, "index_status_rejects_unknown_index_state");
        reply = IndexReply(ipc::IndexState::Healthy);
        reply.response.service_state = static_cast<ipc::ServiceState>(99);
        passed &= Check(ValidateIndexReply(reply) == ERROR_INVALID_DATA, "index_status_rejects_unknown_service_state");
        reply = IndexReply(ipc::IndexState::Healthy);
        reply.response.status = static_cast<ipc::ResponseStatus>(99);
        passed &= Check(ValidateIndexReply(reply) == ERROR_INVALID_DATA, "index_status_rejects_unknown_response_status");
        IndexServiceState state;
        const auto generation = state.Begin();
        passed &= Check(generation != 0 && state.IsCurrent(generation) &&
            state.Snapshot().stage == IndexConnectionStage::Refreshing,
            "index_status_begin_marks_background_refresh");
        passed &= Check(state.Begin() == 0, "index_status_duplicate_refresh_does_not_spawn_another_request");
        IndexServiceSnapshot snapshot;
        snapshot.stage = IndexConnectionStage::Available;
        snapshot.service_state = ipc::ServiceState::Running;
        snapshot.volumes.push_back(MapVolumeIndexStatus(L"C:\\", IndexReply(ipc::IndexState::Healthy)));
        passed &= Check(state.Complete(generation, snapshot) && state.Snapshot().volumes.size() == 1,
            "index_status_background_result_commits_owned_snapshot");
        const auto next = state.Begin();
        passed &= Check(next > generation && !state.Complete(generation, snapshot),
            "index_status_old_generation_cannot_overwrite_new_refresh");
        snapshot.stage = IndexConnectionStage::Unavailable;
        snapshot.error = ERROR_BROKEN_PIPE;
        passed &= Check(state.Complete(next, snapshot) && state.Snapshot().error == ERROR_BROKEN_PIPE,
            "index_status_disconnect_has_explainable_snapshot");
        const auto closing = state.Begin();
        state.Shutdown();
        passed &= Check(!state.IsCurrent(closing) && !state.Complete(closing, snapshot) && state.Begin() == 0 &&
            state.Snapshot().stage == IndexConnectionStage::Closed,
            "index_status_window_close_rejects_queued_results_and_restarts");
        return passed;
    }

    bool RunIndexServiceCoordinatorTests()
    {
        using namespace blink::app;
        namespace ipc = blink::index::ipc;
        using namespace std::chrono_literals;
        bool passed = true;
        struct Calls
        {
            std::mutex mutex;
            std::vector<ipc::Request> requests;
            std::atomic_bool disconnected{};
        };
        const auto calls = std::make_shared<Calls>();
        const auto exchange = [calls](const ipc::Request request) {
            std::scoped_lock lock(calls->mutex);
            calls->requests.push_back(request);
            auto reply = IndexReply(ipc::IndexState::Healthy);
            reply.expected_request_id = calls->requests.size();
            reply.response.request_id = reply.expected_request_id;
            if (calls->disconnected.load()) { reply.received = false; reply.error = ERROR_BROKEN_PIPE; return reply; }
            if (request.type == ipc::RequestType::QueryIndexProgress)
            { reply.response.status = ipc::ResponseStatus::NotFound; reply.response.error = ERROR_NOT_FOUND; }
            if (request.type == ipc::RequestType::QueryVolumeStatus)
            {
                reply.response.files = 20;
                if (request.volume_root == L"D:\\")
                {
                    reply.response.status = ipc::ResponseStatus::Failed;
                    reply.response.index_state = ipc::IndexState::Unavailable;
                    reply.response.error = ERROR_NOT_READY;
                }
            }
            return reply;
        };
        const auto state = std::make_shared<IndexServiceState>();
        IndexServiceCoordinator coordinator(state, exchange, [] { return std::vector<std::wstring>{L"C:\\", L"D:\\"}; });
        const auto publish = std::make_shared<std::promise<std::uint64_t>>();
        auto completed = publish->get_future();
        const auto generation = coordinator.Refresh(L"C:\\", [publish](auto id) { publish->set_value(id); });
        if (!Check(generation != 0 && completed.wait_for(5s) == std::future_status::ready,
                "index_coordinator_publishes_from_background_with_fake_rpc"))
        {
            (void)WaitForWatchStop(coordinator.Shutdown());
            return false;
        }
        auto snapshot = state->Snapshot();
        passed &= Check(completed.get() == generation && snapshot.stage == IndexConnectionStage::Available &&
            snapshot.selected_root == L"C:\\" && snapshot.volumes.size() == 2 &&
            snapshot.volumes[0].stage == VolumeIndexStage::Healthy &&
            snapshot.volumes[1].stage == VolumeIndexStage::Unconfirmed,
            "index_coordinator_queries_selected_root_without_switching_every_store");
        {
            std::scoped_lock lock(calls->mutex);
            passed &= Check(calls->requests.size() == 3 &&
                calls->requests[0].type == ipc::RequestType::QueryIndexProgress &&
                calls->requests[1].type == ipc::RequestType::QueryServiceStatus &&
                calls->requests[2].type == ipc::RequestType::QueryVolumeStatus,
                "index_coordinator_uses_only_read_commands_and_independent_progress_first");
        }
        const auto second = std::make_shared<std::promise<std::uint64_t>>();
        completed = second->get_future();
        (void)coordinator.Refresh(L"D:\\", [second](auto id) { second->set_value(id); });
        passed &= Check(completed.wait_for(5s) == std::future_status::ready &&
            state->Snapshot().stage == IndexConnectionStage::Available &&
            state->Snapshot().volumes[1].stage == VolumeIndexStage::Unavailable &&
            state->Snapshot().volumes[1].error == ERROR_NOT_READY,
            "index_coordinator_volume_failure_keeps_service_connection_available");
        calls->disconnected.store(true);
        const auto third = std::make_shared<std::promise<std::uint64_t>>();
        completed = third->get_future();
        (void)coordinator.Refresh(L"C:\\", [third](auto id) { third->set_value(id); });
        passed &= Check(completed.wait_for(5s) == std::future_status::ready &&
            state->Snapshot().stage == IndexConnectionStage::Unavailable &&
            state->Snapshot().error == ERROR_BROKEN_PIPE, "index_coordinator_disconnect_is_bounded_and_visible");
        calls->disconnected.store(false);
        const auto fourth = std::make_shared<std::promise<std::uint64_t>>();
        completed = fourth->get_future();
        (void)coordinator.Refresh(L"C:\\", [fourth](auto id) { fourth->set_value(id); });
        passed &= Check(completed.wait_for(5s) == std::future_status::ready &&
            state->Snapshot().stage == IndexConnectionStage::Available, "index_coordinator_retry_recovers_without_browse_dependency");
        (void)WaitForWatchStop(coordinator.Shutdown());
        passed &= Check(coordinator.Refresh(L"C:\\", {}) == 0 && state->Snapshot().stage == IndexConnectionStage::Closed,
            "index_coordinator_close_prevents_new_requests");
        const auto busy_state = std::make_shared<IndexServiceState>();
        const auto busy_calls = std::make_shared<std::atomic_int>(0);
        IndexServiceCoordinator busy(busy_state, [busy_calls](auto) {
            ++*busy_calls;
            auto reply = IndexReply(ipc::IndexState::Rebuilding);
            reply.response.error = ERROR_IO_PENDING;
            reply.response.operation_id = 99;
            reply.response.records_seen = 500;
            return reply;
        }, [] { return std::vector<std::wstring>{L"C:\\",L"D:\\"}; });
        const auto busy_pub = std::make_shared<std::promise<std::uint64_t>>();
        completed = busy_pub->get_future();
        (void)busy.Refresh(L"D:\\", [busy_pub](auto id) { busy_pub->set_value(id); });
        passed &= Check(completed.wait_for(5s) == std::future_status::ready &&
            busy_state->Snapshot().operation_running && busy_state->Snapshot().operation_id == 99 &&
            busy_state->Snapshot().operation_records == 500 && *busy_calls == 1,
            "index_coordinator_running_operation_uses_independent_snapshot_only");
        passed &= Check(busy_state->Snapshot().volumes.size() == 2 &&
            busy_state->Snapshot().volumes[0].stage == VolumeIndexStage::Unconfirmed &&
            busy_state->Snapshot().volumes[1].stage == VolumeIndexStage::Unconfirmed,
            "index_coordinator_busy_global_progress_does_not_fabricate_volume_ownership");
        (void)WaitForWatchStop(busy.Shutdown());

        struct Blocked
        {
            std::mutex mutex;
            std::condition_variable changed;
            bool entered{};
            bool release{};
            std::atomic_int published{};
        };
        const auto blocked = std::make_shared<Blocked>();
        const auto closing_state = std::make_shared<IndexServiceState>();
        IndexServiceCoordinator closing(closing_state, [blocked](auto) {
            std::unique_lock lock(blocked->mutex);
            blocked->entered = true;
            blocked->changed.notify_all();
            (void)blocked->changed.wait_for(lock, 5s, [&] { return blocked->release; });
            return IndexReply(ipc::IndexState::Healthy);
        }, [] { return std::vector<std::wstring>{L"C:\\"}; });
        (void)closing.Refresh(L"C:\\", [blocked](auto) { ++blocked->published; });
        {
            std::unique_lock lock(blocked->mutex);
            passed &= Check(blocked->changed.wait_for(lock, 5s, [&] { return blocked->entered; }),
                "index_coordinator_close_fixture_has_inflight_rpc");
        }
        passed &= Check(closing.Refresh(L"D:\\", {}) == 0,
            "index_coordinator_inflight_refresh_does_not_queue_more_clients");
        auto closed_work = closing.Shutdown();
        passed &= Check(closing_state->Snapshot().stage == IndexConnectionStage::Closed,
            "index_coordinator_close_invalidates_state_before_rpc_returns");
        {
            std::scoped_lock lock(blocked->mutex);
            blocked->release = true;
            blocked->changed.notify_all();
        }
        passed &= Check(WaitForWatchStop(closed_work) && blocked->published == 0,
            "index_coordinator_closed_window_discards_late_rpc_result");
        const auto unsupported_state = std::make_shared<IndexServiceState>();
        IndexServiceCoordinator unsupported(unsupported_state, [](const auto request) {
            auto reply = IndexReply(ipc::IndexState::Healthy);
            if (request.type == ipc::RequestType::QueryIndexProgress)
            {
                reply.response.status = ipc::ResponseStatus::Unsupported;
                reply.response.error = ERROR_NOT_SUPPORTED;
            }
            return reply;
        }, [] { return std::vector<std::wstring>{L"C:\\"}; });
        const auto unsupported_pub = std::make_shared<std::promise<std::uint64_t>>();
        completed = unsupported_pub->get_future();
        (void)unsupported.Refresh(L"C:\\", [unsupported_pub](auto id) { unsupported_pub->set_value(id); });
        passed &= Check(completed.wait_for(5s) == std::future_status::ready &&
            unsupported_state->Snapshot().stage == IndexConnectionStage::Unavailable &&
            unsupported_state->Snapshot().error == ERROR_NOT_SUPPORTED,
            "index_coordinator_unsupported_progress_reply_is_not_silently_skipped");
        (void)WaitForWatchStop(unsupported.Shutdown());
        return passed;
    }
}

namespace
{
    bool RunSearchStateTests()
    {
        using namespace blink::app;
        namespace ipc = blink::index::ipc;
        bool passed = true;
        SearchQuery query{ L"文件", SearchScope::CurrentFolder, SearchMode::Substring, L"D:\\Folder", 200 };
        passed &= Check(ValidateSearchQuery(query) == ERROR_SUCCESS, "search_query_local_folder_valid");
        auto invalid = query; invalid.folder = L"\\\\server\\share";
        passed &= Check(ValidateSearchQuery(invalid) == ERROR_NOT_SUPPORTED, "search_query_unc_scope_explicitly_unsupported");
        invalid = query; invalid.folder = L"relative";
        passed &= Check(ValidateSearchQuery(invalid) == ERROR_NOT_SUPPORTED, "search_query_relative_scope_rejected");
        invalid = query; invalid.text = L"   ";
        passed &= Check(ValidateSearchQuery(invalid) == ERROR_INVALID_PARAMETER, "search_query_empty_predicate_rejected");
        invalid = query; invalid.limit = 1001;
        passed &= Check(ValidateSearchQuery(invalid) == ERROR_INVALID_PARAMETER, "search_query_limit_must_fit_existing_contract");
        passed &= Check(SearchDriveRoot(L"d:\\Folder") == L"D:\\", "search_scope_volume_root_normalized");
        const auto request = MakeSearchRequest(query, L"D:\\");
        passed &= Check(request.type == ipc::RequestType::Search && request.text == query.text &&
            request.path_prefix == L"D:\\Folder\\" && request.limit == query.limit,
            "search_request_uses_existing_readonly_scope_boundary");
        auto extension = query; extension.mode = SearchMode::Extension; extension.text = L"txt";
        const auto ext_request = MakeSearchRequest(extension, L"D:\\");
        passed &= Check(ext_request.text.empty() && ext_request.extension == L".txt",
            "search_extension_request_matches_existing_dot_semantics");
        auto prefix = query; prefix.mode = SearchMode::Prefix;
        passed &= Check(MakeSearchRequest(prefix, L"D:\\").prefix, "search_prefix_flag_preserved");
        auto reply = IndexReply(ipc::IndexState::Healthy);
        ipc::SearchHit hit;
        hit.identity = { 77, { 9 }, true }; hit.parent_identity = { 77, { 7 }, true };
        hit.name = L"文件.txt"; hit.full_path = L"D:\\Folder\\文件.txt"; hit.attributes = FILE_ATTRIBUTE_NORMAL;
        reply.response.results = { hit };
        auto volume = MapSearchReply(query, L"D:\\", reply);
        passed &= Check(volume.searched && volume.results.size() == 1 &&
            volume.results[0].source_volume == L"D:\\" && volume.results[0].hit.parent_identity == hit.parent_identity,
            "search_known_hit_keeps_source_volume_and_link_identity");
        reply.response.results.clear();
        passed &= Check(MapSearchReply(query, L"D:\\", reply).searched &&
            MapSearchReply(query, L"D:\\", reply).results.empty(), "search_known_miss_is_successful_empty_result");
        auto sibling = hit; sibling.full_path = L"D:\\Folder2\\文件.txt";
        reply.response.results = { hit, sibling };
        passed &= Check(MapSearchReply(query, L"D:\\", reply).results.size() == 1,
            "search_current_folder_filters_sibling_prefix");
        auto nested = hit; nested.full_path = L"d:\\folder\\nested\\文件.txt";
        reply.response.results = { nested };
        passed &= Check(MapSearchReply(query, L"D:\\", reply).results.size() == 1,
            "search_current_folder_includes_descendants_case_insensitively");
        auto hardlink = hit; hardlink.name = L"link.txt"; hardlink.full_path = L"D:\\Folder\\link.txt";
        reply.response.results = { hit, hit, hardlink };
        volume = MapSearchReply(query, L"D:\\", reply);
        passed &= Check(volume.results.size() == 2 && !SameSearchLink(volume.results[0], volume.results[1]),
            "search_duplicate_rows_dedup_but_hardlinks_remain_distinct");
        reply.response.results = { hit }; reply.response.results[0].identity.valid = false;
        volume = MapSearchReply(query, L"D:\\", reply);
        passed &= Check(!volume.searched && volume.error == ERROR_INVALID_DATA && volume.results.empty(),
            "search_invalid_identity_cannot_be_healthy_result");
        reply.response.results = { hit }; reply.response.results[0].full_path = L"C:\\Folder\\文件.txt";
        passed &= Check(MapSearchReply(query, L"D:\\", reply).error == ERROR_INVALID_DATA,
            "search_cross_volume_payload_rejected");
        reply.response.results = { hit }; reply.response.results[0].name = L"other.txt";
        passed &= Check(MapSearchReply(query, L"D:\\", reply).error == ERROR_INVALID_DATA,
            "search_link_name_must_match_path");
        reply = IndexReply(ipc::IndexState::Healthy); reply.response.status = ipc::ResponseStatus::Busy;
        reply.response.error = ERROR_IO_PENDING; reply.response.index_state = ipc::IndexState::Rebuilding;
        volume = MapSearchReply(query, L"D:\\", reply);
        passed &= Check(!volume.searched && volume.stage == VolumeIndexStage::Unconfirmed && volume.results.empty(),
            "search_global_busy_is_not_attributed_to_requested_volume");
        reply = IndexReply(ipc::IndexState::Healthy); reply.error = ERROR_BROKEN_PIPE; reply.received = false;
        passed &= Check(MapSearchReply(query, L"D:\\", reply).error == ERROR_BROKEN_PIPE,
            "search_disconnect_error_preserved");
        reply = IndexReply(ipc::IndexState::Healthy); reply.response.index_state = ipc::IndexState::Corrupted;
        passed &= Check(!MapSearchReply(query, L"D:\\", reply).searched,
            "search_corrupt_state_does_not_publish_successful_results");
        reply = IndexReply(ipc::IndexState::Unknown);
        passed &= Check(!MapSearchReply(query, L"D:\\", reply).searched &&
            MapSearchReply(query, L"D:\\", reply).error != ERROR_SUCCESS,
            "search_unknown_success_payload_has_explicit_unusable_error");
        reply = IndexReply(ipc::IndexState::Healthy);
        passed &= Check(!MapSearchReply(query, L"C:\\", reply).searched &&
            MapSearchReply(query, L"C:\\", reply).error == ERROR_INVALID_PARAMETER,
            "search_current_folder_cannot_report_wrong_volume_as_successful_miss");
        reply = IndexReply(ipc::IndexState::Healthy); reply.response.results = { hit }; query.limit = 1;
        passed &= Check(MapSearchReply(query, L"D:\\", reply).limit_reached,
            "search_limit_reached_is_visible");
        SearchState state;
        const auto generation = state.Begin(query);
        passed &= Check(generation != 0 && state.Snapshot().stage == SearchStage::Searching && state.Begin(query) == 0,
            "search_state_rejects_duplicate_inflight_requests");
        SearchSnapshot done; done.stage = SearchStage::Complete; done.query = query;
        done.volumes = { MapSearchReply(query, L"D:\\", reply) };
        passed &= Check(state.Complete(generation, done) && state.IsCurrent(generation),
            "search_state_commits_only_current_query");
        passed &= Check(!state.Complete(generation, done), "search_state_terminal_completion_is_once");
        const auto retry = state.Begin(query, true);
        passed &= Check(retry > generation && state.Snapshot().volumes.size() == 1,
            "search_retry_preserves_same_query_completed_volumes");
        state.Cancel();
        passed &= Check(!state.IsCurrent(retry) && !state.Complete(retry, done) && state.Snapshot().stage == SearchStage::Idle,
            "search_cancel_rejects_late_reply_without_committing_browse");
        state.Shutdown();
        passed &= Check(state.Begin(query) == 0 && state.Snapshot().stage == SearchStage::Closed,
            "search_closed_state_cannot_start_new_work");
        return passed;
    }
}

namespace
{
    bool RunSearchCoordinatorTests()
    {
        using namespace blink::app;
        using namespace std::chrono_literals;
        namespace ipc = blink::index::ipc;
        bool passed = true;
        struct Probe
        {
            mutable std::mutex mutex;
            std::vector<ipc::Request> requests;
            std::atomic<bool> ready{};
            std::atomic<bool> background{};
            std::vector<ipc::Request> Requests() const
            { std::scoped_lock lock(mutex); return requests; }
        };
        const auto probe = std::make_shared<Probe>();
        const auto caller_thread = GetCurrentThreadId();
        const auto state = std::make_shared<SearchState>();
        SearchCoordinator controller(state, [probe, caller_thread](ipc::Request request) {
            probe->background = GetCurrentThreadId() != caller_thread;
            { std::scoped_lock lock(probe->mutex); probe->requests.push_back(request); }
            auto reply = IndexReply(ipc::IndexState::Healthy);
            if (request.volume_root == L"D:\\" && !probe->ready)
            {
                reply.response.status = ipc::ResponseStatus::Busy;
                reply.response.error = ERROR_IO_PENDING;
                reply.response.index_state = ipc::IndexState::Rebuilding;
            }
            else
            {
                ipc::SearchHit hit;
                hit.identity = { 77, { 9 }, true }; hit.parent_identity = { 77, { 7 }, true };
                hit.name = L"fixture.txt"; hit.full_path = request.volume_root + L"fixture.txt";
                hit.attributes = FILE_ATTRIBUTE_NORMAL; reply.response.results = { hit };
            }
            return reply;
        }, [] { return std::vector<std::wstring>{ L"C:\\", L"D:\\", L"c:\\", L"\\\\host\\share" }; },
            [](const auto& root) { return SearchFolderScope{ root, { 77, { 1 }, true }, ERROR_SUCCESS }; });
        SearchQuery query{ L"fixture" };
        auto completed = std::make_shared<std::promise<void>>(); auto future = completed->get_future();
        const auto generation = controller.Search(query, [completed](auto) { completed->set_value(); });
        const auto first_ready = future.wait_for(5s) == std::future_status::ready;
        passed &= Check(generation != 0 && first_ready && probe->background,
            "search_coordinator_queries_on_owned_background_action");
        const auto action_done = WaitForWatchStop(controller.CurrentAction());
        passed &= Check(action_done, "search_coordinator_native_action_has_bounded_completion");
        auto snapshot = state->Snapshot();
        passed &= Check(snapshot.stage == SearchStage::Partial && snapshot.volumes.size() == 2 &&
            snapshot.volumes[0].searched && snapshot.volumes[0].results.size() == 1 &&
            !snapshot.volumes[1].searched && snapshot.volumes[1].stage == VolumeIndexStage::Unconfirmed,
            "search_coordinator_busy_volume_preserves_other_volume_results");
        const auto initial_requests = probe->Requests();
        passed &= Check(initial_requests.size() == 2 && std::all_of(initial_requests.begin(), initial_requests.end(),
            [](const auto& request) { return request.type == ipc::RequestType::Search; }),
            "search_coordinator_only_sends_existing_readonly_search_commands");
        if (action_done) { probe->ready = true; }
        completed = std::make_shared<std::promise<void>>(); future = completed->get_future();
        const auto retry = controller.Search(query, [completed](auto) { completed->set_value(); }, true);
        const auto retry_ready = future.wait_for(5s) == std::future_status::ready;
        passed &= Check(retry > generation && retry_ready && WaitForWatchStop(controller.CurrentAction()) &&
            probe->Requests().size() == 3 && probe->Requests().back().volume_root == L"D:\\" &&
            state->Snapshot().stage == SearchStage::Complete,
            "search_coordinator_retry_only_unfinished_volumes_avoids_reload_loop");
        (void)WaitForWatchStop(controller.Shutdown());
        passed &= Check(controller.Search(query, {}) == 0, "search_coordinator_closed_rejects_new_work");

        const auto folder_state = std::make_shared<SearchState>();
        const auto requests = std::make_shared<std::vector<ipc::Request>>();
        SearchCoordinator folder(folder_state, [requests](ipc::Request request) {
            requests->push_back(request); return IndexReply(ipc::IndexState::Healthy);
        }, []() -> std::vector<std::wstring> { throw std::runtime_error("Folder scope must not discover all volumes"); },
            [](const auto& path) { return SearchFolderScope{ path, { 77, { 7 }, true }, ERROR_SUCCESS }; },
            [](const SearchFolderScope&, SearchVolumeResult&) {});
        query.scope = SearchScope::CurrentFolder; query.folder = L"D:\\Folder";
        completed = std::make_shared<std::promise<void>>(); future = completed->get_future();
        (void)folder.Search(query, [completed](auto) { completed->set_value(); });
        const auto folder_ready = future.wait_for(5s) == std::future_status::ready;
        passed &= Check(folder_ready && WaitForWatchStop(folder.CurrentAction()) && requests->size() == 1 &&
            requests->front().volume_root == L"D:\\" && requests->front().path_prefix == L"D:\\Folder\\" &&
            folder_state->Snapshot().stage == SearchStage::Complete && folder_state->Snapshot().volumes[0].results.empty(),
            "search_coordinator_folder_scope_known_miss_keeps_local_boundary");
        (void)WaitForWatchStop(folder.Shutdown());

        const auto blocked_state = std::make_shared<SearchState>();
        const auto entered = std::make_shared<std::promise<void>>(); auto entered_future = entered->get_future();
        const auto release = std::make_shared<std::promise<void>>(); auto released = release->get_future().share();
        const auto published = std::make_shared<std::atomic<unsigned>>(0);
        SearchCoordinator blocked(blocked_state, [entered, released](ipc::Request) {
            entered->set_value(); (void)released.wait_for(5s); return IndexReply(ipc::IndexState::Healthy);
        }, [] { return std::vector<std::wstring>{ L"D:\\" }; });
        query = { L"fixture" };
        (void)blocked.Search(query, [published](auto) { ++*published; });
        passed &= Check(entered_future.wait_for(5s) == std::future_status::ready,
            "search_coordinator_blocked_rpc_enters_owned_worker");
        blocked.Cancel();
        passed &= Check(blocked.Search(query, {}) == 0 && blocked_state->Snapshot().stage == SearchStage::Idle,
            "search_coordinator_cancel_keeps_rpc_owned_and_rejects_duplicate_worker");
        const auto pending = blocked.Shutdown();
        passed &= Check(blocked_state->Snapshot().stage == SearchStage::Closed,
            "search_coordinator_close_is_immediate_while_rpc_blocked");
        release->set_value();
        passed &= Check(WaitForWatchStop(pending) && published->load() == 0,
            "search_coordinator_close_suppresses_stale_publish_after_rpc_returns");
        return passed;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc > 1 && std::wstring(argv[1]) == L"--live-index-probe")
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        const int result = RunIndexServiceLiveProbe(argc, argv);
        winrt::uninit_apartment();
        return result;
    }
    if (argc > 1)
    {
        if (argc == 3 && std::wstring(argv[1]) == L"--suite")
        {
            const std::wstring suite = argv[2];
            if (suite == L"search-response" || suite == L"search-scope")
            {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                const bool passed = suite == L"search-response" ? RunSearchResponseTests() : RunSearchScopeCacheTests();
                winrt::uninit_apartment();
                return passed ? 0 : 1;
            }
        }
        std::cerr << "Usage: Blink.Phase1.Tests.exe [--suite search-response|search-scope]"
            " or --live-index-probe <options>\n";
        return 2;
    }
    bool passed = true;
    // Run the allocation-sensitive performance gate before the other large
    // model/projection tests. Its result should measure CommitBrowse behavior,
    // not allocator state left behind by unrelated tests in this process.
    passed &= RunLargeSelectionComplexityBenchmark();
    passed &= RunGenerationAndShutdownTests();
    passed &= RunControllerOwnershipTests();
    passed &= RunDirectoryResultTests();
    passed &= RunLocationModelTests();
    passed &= RunThisPcContentTests();
    passed &= RunNavigationModelTests();
    passed &= RunNavigationPaneTests();
    passed &= RunFolderViewFormattingTests();
    passed &= RunLargeFolderProjectionEvidence();
    passed &= RunTabStateTests();
    passed &= RunFolderViewStateTests();
    passed &= RunFolderCommandPolicyTests();
    passed &= RunIndexServiceStateTests();
    passed &= RunIndexServiceCoordinatorTests();
    passed &= RunIndexServiceSetupTests();
    passed &= RunSearchStateTests();
    passed &= RunSearchCoordinatorTests();
    passed &= RunSearchIntegrationTests();
    passed &= RunSearchExactTests();
    passed &= RunSearchResponseTests();
    passed &= RunSearchScopeCacheTests();
    passed &= RunSearchContextMenuTests();
    passed &= RunFileOperationTests();
    passed &= RunDirectoryOperationTargetTests();
    passed &= RunRenameOperationTests();
    passed &= RunFileCopyOperationTests();
    passed &= RunFileCopyConflictTests();
    passed &= RunFileCopyBatchTests();
    passed &= RunMixedCopyBatchTests();
    passed &= RunMoveOperationTests();
    passed &= RunMoveCoordinatorTests();
    passed &= RunMoveBatchTests();
    passed &= RunMoveTransferTests();
    passed &= RunCrossVolumeMoveTests();
    passed &= RunFolderRelocationTests();
    passed &= RunDeleteOperationTests();
    passed &= RunItemPropertiesTests();
    passed &= RunFileDragDropTests();
    passed &= RunFileReplaceTests();
    passed &= RunDirectoryMergeTests();
    passed &= RunDirectoryMoveMergeTests();
    passed &= RunExplorerCommandBarTests();
    passed &= RunAddressBarTests();
    passed &= RunFileNameSelectionTests();
    passed &= RunDirectoryCopyOperationTests();
    passed &= RunDirectoryCopyConflictTests();
    passed &= RunDirectoryCopyCoordinatorTests();
    passed &= RunDirectoryCopyConflictCoordinatorTests();
    passed &= RunClipboardFileTransferTests();
    passed &= RunClipboardCopySourceTests();
    passed &= RunCopyPresentationTests();
    passed &= RunFileOperationCoordinatorTests();
    passed &= RunFolderChangeStateTests();
    passed &= RunNativeFolderWatcherTests();
    passed &= RunFolderNavigationTests();
    passed &= RunTypeSortTests();
    passed &= RunTabWindowStateTests();
    passed &= RunNavigationTreeTests();
    passed &= RunItemVisibilityTests();
    passed &= RunSelectAllRowsTests();
    passed &= RunFileActivationCommandLineTests();
    passed &= RunFileActivationValidationTests();
    passed &= RunFileActivationControllerTests();
    passed &= RunRowIdentityHashAndEqualityTests();
    passed &= RunInactiveTabThisPcTreeSyncRegression();
    passed &= RunThisPcAddressBarRoundTripTests();
    passed &= CheckBrowseSourceContract();
    std::cout << "phase1a_browse_tests=" << (passed ? "PASS" : "FAIL") << "\n";
    return passed ? 0 : 1;
}
