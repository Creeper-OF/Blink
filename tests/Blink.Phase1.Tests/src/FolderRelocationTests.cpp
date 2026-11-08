#include "FolderRelocationTests.h"
#include "Blink/App/FileOperationRelocation.h"
#include "Blink/App/TabWindowState.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <atomic>
#include <chrono>
#include <future>

namespace
{
    using namespace blink::app;
    namespace fs = std::filesystem;
    bool Check(bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    Location Folder(const wchar_t* path) { return Location::Folder(path); }
    blink::core::FileIdentity Identity(unsigned char value, std::uint64_t volume = 17)
    { blink::core::FileIdentity id; id.valid = true; id.volume_serial = volume; id.file_id[0] = value; return id; }
    bool Visit(NavigationModel& model, const Location& path)
    { auto request = model.Navigate(path); return request && model.Commit(*request); }
    blink::core::DirectoryListing Listing(const Location& path, blink::core::FileIdentity identity, unsigned char item = 0)
    {
        blink::core::DirectoryListing listing; listing.path = path.FolderPath(); listing.directory_identity = identity;
        if (item)
        {
            blink::core::FileEntry entry; entry.name = L"file.txt"; entry.attributes = FILE_ATTRIBUTE_NORMAL;
            entry.full_path = blink::core::JoinPath(listing.path, entry.name); entry.identity = Identity(item, identity.volume_serial);
            listing.entries.push_back(std::move(entry));
        }
        return listing;
    }
    bool Visit(TabState& state, const Location& path, blink::core::FileIdentity identity, unsigned char item = 0)
    {
        auto request = state.Navigate(path);
        return request && state.CommitBrowse(*request, Listing(path, identity, item)).status == CommitBrowseStatus::Committed;
    }
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry entry;
        if (!blink::core::ReadEntryMetadata(path.wstring(), entry)) { throw std::runtime_error("relocation fixture metadata"); }
        entry.name = path.filename().wstring(); entry.full_path = path.wstring(); return entry;
    }
    FileOperationSnapshot Moved(const MoveRequest& request, MoveResult result, std::uint64_t id = 19)
    {
        FileOperationSnapshot snapshot; snapshot.operation_id = id; snapshot.kind = FileOperationKind::Move;
        snapshot.stage = FileOperationStage::Running; snapshot.move_request = request; snapshot.move_result = std::move(result);
        return snapshot;
    }
    bool Arm(FolderWatcher& watcher, const fs::path& path)
    {
        auto done = std::make_shared<std::promise<bool>>(); auto future = done->get_future();
        auto reported = std::make_shared<std::atomic_bool>();
        watcher.Start(path.wstring(), [done, reported](const FolderWatchEvent& value) {
            if ((value.kind == FolderWatchEventKind::Ready || value.kind == FolderWatchEventKind::Unavailable) && !reported->exchange(true))
            { done->set_value(value.kind == FolderWatchEventKind::Ready); }
        });
        return future.wait_for(std::chrono::seconds(8)) == std::future_status::ready && future.get();
    }
    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return true; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        auto done = std::make_shared<std::promise<Status>>(); auto future = done->get_future();
        action.Completed([done](auto&&, Status status) { done->set_value(status); });
        return future.wait_for(std::chrono::seconds(8)) == std::future_status::ready && future.get() == Status::Completed;
    }
    winrt::Windows::Foundation::IAsyncAction FailedPreparation()
    { co_await winrt::resume_background(); winrt::throw_hresult(E_FAIL); }
    winrt::Windows::Foundation::IAsyncAction AwaitPreparation(std::shared_ptr<winrt::handle> gate)
    { co_await winrt::resume_on_signal(gate->get()); }
}

bool RunFolderRelocationTests()
{
    bool passed = true;
    const auto source = Folder(L"D:\\old\\资料"), destination = Folder(L"C:\\new\\资料 (2)");
    const auto child = Folder(L"D:\\old\\资料\\sub"), new_child = Folder(L"C:\\new\\资料 (2)\\sub");
    const auto elsewhere = Folder(L"D:\\unrelated");
    passed &= Check(RebaseFolderLocation(source, source, destination) == destination,
        "relocation_exact_folder_uses_actual_destination");
    passed &= Check(RebaseFolderLocation(child, source, destination) == new_child,
        "relocation_nested_unicode_folder_preserves_relative_suffix");
    passed &= Check(!RebaseFolderLocation(Folder(L"D:\\old\\资料2\\sub"), source, destination) &&
        !RebaseFolderLocation(Location::ThisPC(), source, destination) && !RebaseFolderLocation(elsewhere, source, destination),
        "relocation_path_boundary_excludes_sibling_prefix_thispc_and_unrelated_folders");
    passed &= Check(RebaseFolderLocation(Folder(L"\\\\?\\d:\\OLD\\资料\\sub"), source, destination) == new_child,
        "relocation_extended_drive_paths_and_ordinal_case_match");
    passed &= Check(!RebaseFolderLocation(source, Location::Folder({}), destination) &&
        !RebaseFolderLocation(source, source, Location::ThisPC()) && !RebaseFolderLocation(source, source, source),
        "relocation_invalid_virtual_and_unchanged_targets_do_not_rewrite");
    {
        NavigationModel model; (void)Visit(model, source);
        const auto request = model.RelocateFolder(source, destination);
        passed &= Check(request && request->action == NavigationAction::Relocate && request->target == destination &&
            model.Snapshot().committed_location == source, "relocation_navigation_stages_new_page_without_premature_path_commit");
        const bool committed = request && model.Commit(*request);
        const auto snapshot = model.Snapshot();
        passed &= Check(committed && snapshot.committed_location == destination && snapshot.back_history.size() == 1 &&
            snapshot.back_history[0].IsThisPC() && snapshot.forward_history.empty(),
            "relocation_commit_replaces_current_history_entry_without_adding_vanished_source");
        passed &= Check(!model.RelocateFolder(source, destination), "relocation_repeated_fact_does_not_restart_committed_navigation");
    }
    {
        NavigationModel model; (void)Visit(model, source); (void)Visit(model, child); (void)Visit(model, elsewhere);
        if (const auto back = model.Back()) { (void)model.Commit(*back); }
        const auto request = model.RelocateFolder(source, destination); const bool committed = request && model.Commit(*request);
        auto snapshot = model.Snapshot();
        passed &= Check(committed && snapshot.committed_location == new_child && snapshot.back_history.size() == 2 &&
            snapshot.back_history.back() == destination && snapshot.forward_history.size() == 1 && snapshot.forward_history[0] == elsewhere,
            "relocation_rebases_back_history_and_keeps_unrelated_forward_history");
        const auto back = model.Back(); const bool returned = back && back->target == destination && model.Commit(*back);
        const auto forward = model.Forward();
        passed &= Check(returned && forward && forward->target == new_child && model.Commit(*forward),
            "relocation_back_and_forward_visit_new_folder_paths");
    }
    {
        NavigationModel model; (void)Visit(model, source); const auto old = model.Refresh();
        const auto relocated = model.RelocateFolder(source, destination);
        passed &= Check(old && relocated && old->token != relocated->token && !model.IsPending(*old) &&
            !model.Commit(*old) && model.Commit(*relocated), "relocation_replaces_pending_old_refresh_and_rejects_its_late_result");
    }
    {
        NavigationModel model; (void)Visit(model, source); const auto old = model.Navigate(child);
        const auto relocated = model.RelocateFolder(source, destination);
        passed &= Check(old && relocated && relocated->action == NavigationAction::Navigate && relocated->target == new_child &&
            !model.IsPending(*old) && model.Commit(*relocated) && model.Snapshot().back_history.back() == destination,
            "relocation_pending_navigation_into_moved_tree_follows_new_path_and_history");
    }
    {
        NavigationModel model; (void)Visit(model, source); (void)Visit(model, elsewhere); const auto old = model.Back();
        const auto relocated = model.RelocateFolder(source, destination);
        passed &= Check(old && relocated && relocated->action == NavigationAction::Back && relocated->target == destination &&
            model.Snapshot().committed_location == elsewhere && !model.Commit(*old) && model.Commit(*relocated),
            "relocation_pending_back_navigation_restarts_at_rebased_history_target");
    }
    {
        NavigationModel model; (void)Visit(model, source); const auto user = model.Navigate(elsewhere);
        const auto relocated = model.RelocateFolder(source, destination);
        passed &= Check(user && !relocated && model.IsPending(*user) && model.Commit(*user) &&
            model.Snapshot().committed_location == elsewhere && model.Snapshot().back_history.back() == destination,
            "relocation_honors_pending_user_navigation_elsewhere_and_updates_its_back_history");
    }
    {
        NavigationModel model; (void)Visit(model, elsewhere); (void)Visit(model, source);
        if (const auto back = model.Back()) { (void)model.Commit(*back); }
        const auto old = model.Forward(); const auto relocated = model.RelocateFolder(source, destination);
        passed &= Check(old && relocated && relocated->action == NavigationAction::Forward && relocated->target == destination &&
            !model.IsPending(*old) && model.Commit(*relocated), "relocation_pending_forward_navigation_restarts_at_new_history_target");
    }
    {
        NavigationModel model; (void)Visit(model, source); const auto follow = model.RelocateFolder(source, destination);
        const auto explicit_address = model.Navigate(source);
        passed &= Check(follow && explicit_address && !model.IsPending(*follow) && model.Commit(*explicit_address) &&
            model.Snapshot().committed_location == source && model.Snapshot().back_history.back() == destination,
            "relocation_new_explicit_visit_to_reused_old_path_is_not_permanently_redirected");
    }
    {
        NavigationModel model; (void)Visit(model, source); const auto follow = model.RelocateFolder(source, destination);
        if (follow) { (void)model.Reject(*follow); }
        const auto up = model.Up();
        passed &= Check(follow && up && up->target == Folder(L"C:\\new") && model.Commit(*up),
            "relocation_up_after_failed_follow_uses_new_folder_parent");
    }
    {
        NavigationModel model; (void)Visit(model, source); const auto user = model.Navigate(elsewhere);
        (void)model.RelocateFolder(source, destination); const bool rejected = user && model.Reject(*user);
        const auto retry = model.Refresh();
        passed &= Check(rejected && model.Snapshot().committed_location == source && retry && retry->target == destination &&
            model.Commit(*retry), "relocation_failed_user_navigation_retains_old_page_but_refresh_uses_new_known_location");
    }
    {
        NavigationModel model; (void)Visit(model, source); const auto first = model.RelocateFolder(source, destination);
        const auto final = Folder(L"D:\\final\\资料"); const auto second = model.RelocateFolder(destination, final);
        passed &= Check(first && second && !model.IsPending(*first) && !model.Commit(*first) && model.Commit(*second) &&
            model.Snapshot().committed_location == final && model.Snapshot().back_history.size() == 1,
            "relocation_multiple_moves_before_browse_commit_follow_latest_destination_once");
    }
    {
        NavigationModel model; (void)Visit(model, elsewhere); (void)Visit(model, source); (void)Visit(model, elsewhere);
        passed &= Check(!model.RelocateFolder(source, destination) && model.Snapshot().committed_location == elsewhere &&
            model.Snapshot().back_history.back() == destination, "relocation_history_only_change_does_not_navigate_unrelated_current_page");
        model.Shutdown();
        passed &= Check(!model.RelocateFolder(elsewhere, source), "relocation_shutdown_model_starts_no_follow_request");
    }
    {
        TabState state; (void)Visit(state, source, Identity(3), 4); state.SetViewMode(FolderViewMode::Grid);
        state.SetSortDescriptor({ SortField::Size, SortDirection::Descending });
        (void)state.SelectEntry(state.CommittedEntriesSnapshot()[0]); const auto old = state.Refresh();
        const auto request = state.RelocateFolder(source, destination);
        const auto late = old ? state.CommitBrowse(*old, Listing(source, Identity(3), 4)) : CommitBrowseResult{};
        const auto status = request ? state.CommitBrowse(*request, Listing(destination, Identity(3), 4)).status : CommitBrowseStatus::Failed;
        passed &= Check(request && late.status == CommitBrowseStatus::StaleOrRejected && status == CommitBrowseStatus::Committed &&
            state.PresentationSnapshot().committed_location == destination && state.PresentationSnapshot().selected_count == 1 &&
            state.GetViewMode() == FolderViewMode::Grid && state.GetSortDescriptor().field == SortField::Size &&
            state.CommittedEntriesSnapshot()[0].full_path == blink::core::JoinPath(destination.FolderPath(), L"file.txt"),
            "relocation_tab_commit_preserves_same_identity_selection_preferences_and_updates_row_paths");
    }
    {
        TabState state; (void)Visit(state, source, Identity(3), 4); (void)state.SelectEntry(state.CommittedEntriesSnapshot()[0]);
        const auto request = state.RelocateFolder(source, destination);
        const auto status = request ? state.CommitBrowse(*request, Listing(destination, Identity(9, 29), 8)).status : CommitBrowseStatus::Failed;
        passed &= Check(status == CommitBrowseStatus::Committed && state.CommittedDirectoryIdentity() == Identity(9, 29) &&
            state.PresentationSnapshot().selected_count == 0, "relocation_cross_volume_listing_replaces_directory_id_and_drops_old_source_selection");
    }
    {
        TabState state; (void)Visit(state, source, Identity(3), 4); const auto request = state.RelocateFolder(source, destination);
        const bool rejected = request && state.RejectBrowse(*request, ERROR_ACCESS_DENIED); const auto retry = state.Refresh();
        passed &= Check(rejected && state.PresentationSnapshot().committed_location == source && state.CommittedDirectoryIdentity() == Identity(3) &&
            state.CommittedEntriesSnapshot()[0].full_path == blink::core::JoinPath(source.FolderPath(), L"file.txt") &&
            retry && retry->target == destination, "relocation_failed_destination_browse_keeps_committed_page_and_retry_target");
    }
    {
        NavigationModel model; const auto lower = Folder(L"D:\\case\\folder"), upper = Folder(L"D:\\case\\Folder"); (void)Visit(model, lower);
        const auto request = model.RelocateFolder(lower, upper);
        passed &= Check(request && request->target.FolderPath() == upper.FolderPath() && model.Commit(*request) &&
            model.Snapshot().committed_location.FolderPath() == upper.FolderPath(), "relocation_case_only_folder_rename_updates_display_spelling");
    }
    {
        MoveRequest request; request.source_parent = L"D:\\old"; request.source.name = L"资料"; request.source.is_directory = true; request.source.identity = Identity(3);
        MoveResult result; result.moved = true; result.error = ERROR_SUCCESS; result.path = destination.FolderPath(); result.post_commit_error = ERROR_ACCESS_DENIED;
        auto snapshot = Moved(request, result); snapshot.move_batch.emplace(); snapshot.move_batch->current_index = 2;
        const auto fact = CompletedFolderRelocation(snapshot);
        passed &= Check(fact && fact->source == source && fact->destination == destination && fact->source_identity == Identity(3) && fact->item_index == 2,
            "relocation_native_moved_folder_fact_uses_actual_numbered_path_despite_followup_warning");
        snapshot.move_result->moved = false; snapshot.move_result->directory_copy_result.emplace(); snapshot.move_result->directory_copy_result->copied = true;
        snapshot.move_result->source_files_removed = 2;
        passed &= Check(!CompletedFolderRelocation(snapshot), "relocation_partial_cross_volume_tree_does_not_relocate_remaining_source_folder");
        snapshot.move_result = result; snapshot.move_request->source.is_directory = false;
        passed &= Check(!CompletedFolderRelocation(snapshot), "relocation_moved_ordinary_file_does_not_rebase_folder_paths");
        snapshot.kind = FileOperationKind::Rename; snapshot.rename_source = request.source; snapshot.rename_source->is_directory = true;
        snapshot.request.parent = L"D:\\old"; snapshot.result.committed = true; snapshot.result.path = L"D:\\old\\newname";
        const auto renamed = CompletedFolderRelocation(snapshot);
        passed &= Check(renamed && renamed->source == source && renamed->destination == Folder(L"D:\\old\\newname"),
            "relocation_committed_folder_rename_supplies_same_follow_flow");
        snapshot.result.committed = false;
        passed &= Check(!CompletedFolderRelocation(snapshot), "relocation_uncommitted_rename_creates_no_follow_fact");
        FolderRelocationTracker tracker;
        if (fact)
        {
            const bool first = tracker.Accept(*fact), duplicate = tracker.Accept(*fact); auto next = *fact; ++next.item_index;
            const bool tail = tracker.Accept(next); ++next.operation_id;
            passed &= Check(first && !duplicate && tail && tracker.Accept(next) && !tracker.Accept(*fact),
                "relocation_publication_tracker_applies_each_item_once_and_rejects_old_operations");
        }
        else { passed &= Check(false, "relocation_publication_tracker_applies_each_item_once_and_rejects_old_operations"); }
    }
    {
        MoveRequest request; request.source_parent = L"D:\\old"; request.source.name = L"资料";
        request.source.is_directory = true; request.source.identity = Identity(3);
        MoveResult result; result.merged = true; result.skipped = true; result.error = ERROR_SUCCESS;
        result.source_path = source.FolderPath(); result.path = destination.FolderPath();
        const auto original_child = Folder(L"D:\\old\\资料\\child");
        const auto actual_child = Folder(L"D:\\target\\资料\\child (2)");
        result.directory_relocations.push_back({ original_child.FolderPath(), actual_child.FolderPath(), Identity(4) });
        auto snapshot = Moved(request, result); snapshot.move_batch.emplace(); snapshot.move_batch->current_index = 3;
        const auto partial = CompletedFolderRelocations(snapshot);
        passed &= Check(!CompletedFolderRelocation(snapshot) && partial.size() == 1 && partial[0].source == original_child &&
            partial[0].destination == actual_child && partial[0].source_identity == Identity(4),
            "relocation_merge_skip_follows_removed_child_actual_name_without_rebasing_retained_root");
        TabWindowState tabs; const auto root = tabs.ActiveTabId(), nested = tabs.CreateTab(), kept = tabs.CreateTab();
        (void)Visit(*tabs.Find(root)->state, source, Identity(3));
        (void)Visit(*tabs.Find(nested)->state, original_child, Identity(4));
        const auto skipped = Folder(L"D:\\old\\资料\\skipped"); (void)Visit(*tabs.Find(kept)->state, skipped, Identity(5));
        const auto changed = partial.empty() ? std::vector<RelocatedTabNavigation>{} :
            tabs.RelocateFolder(partial[0].source, partial[0].destination, partial[0].source_identity);
        passed &= Check(changed.size() == 1 && changed[0].tab_id == nested && changed[0].request.target == actual_child &&
            tabs.Find(root)->state->PresentationSnapshot().committed_location == source &&
            tabs.Find(kept)->state->PresentationSnapshot().committed_location == skipped,
            "relocation_partial_merge_keeps_source_root_and_skipped_directory_tabs_in_place");
        snapshot.move_result->moved = true; snapshot.move_result->skipped = false;
        snapshot.move_result->directory_relocations.insert(snapshot.move_result->directory_relocations.begin(),
            { source.FolderPath(), destination.FolderPath(), Identity(3) });
        const auto complete = CompletedFolderRelocations(snapshot);
        passed &= Check(complete.size() == 2 && complete[0].source == original_child && complete[1].source == source &&
            complete[0].subitem_index != complete[1].subitem_index,
            "relocation_merge_orders_renamed_children_before_parent_prefix_and_assigns_distinct_facts");
        FolderRelocationTracker tracker;
        passed &= Check(complete.size() == 2 && tracker.Accept(complete[0]) && tracker.Accept(complete[1]) &&
            !tracker.Accept(complete[0]) && !tracker.Accept(complete[1]),
            "relocation_merge_deduplicates_each_removed_directory_within_one_batch_item");
        snapshot.closed = true;
        passed &= Check(CompletedFolderRelocations(snapshot).empty(), "relocation_closed_merge_does_not_retarget_tabs");
    }
    {
        TabWindowState tabs; const auto root = tabs.ActiveTabId(), nested = tabs.CreateTab(), unrelated = tabs.CreateTab(), replacement = tabs.CreateTab();
        (void)Visit(*tabs.Find(root)->state, source, Identity(3)); (void)Visit(*tabs.Find(nested)->state, child, Identity(4));
        (void)Visit(*tabs.Find(unrelated)->state, elsewhere, Identity(5)); (void)Visit(*tabs.Find(replacement)->state, source, Identity(99));
        const auto requests = tabs.RelocateFolder(source, destination, Identity(3));
        passed &= Check(requests.size() == 2 && requests[0].tab_id == root && requests[0].request.target == destination &&
            requests[1].tab_id == nested && requests[1].request.target == new_child &&
            !tabs.Find(unrelated)->state->PresentationSnapshot().has_pending && !tabs.Find(replacement)->state->PresentationSnapshot().has_pending,
            "relocation_window_follows_root_and_descendant_tabs_without_touching_unrelated_or_replaced_root");
    }
    {
        TabWindowState tabs; const auto closed = tabs.CreateTab(); (void)Visit(*tabs.Find(closed)->state, child, Identity(4));
        const bool removed = tabs.CloseTab(closed) == CloseTabStatus::Closed;
        passed &= Check(removed && tabs.RelocateFolder(source, destination, Identity(3)).empty() && tabs.Size() == 1,
            "relocation_closed_tabs_do_not_receive_follow_requests");
    }
    {
        TabWindowState tabs; const auto root = tabs.ActiveTabId(), nested = tabs.CreateTab(), other = tabs.CreateTab();
        (void)Visit(*tabs.Find(root)->state, source, Identity(3)); (void)Visit(*tabs.Find(nested)->state, child, Identity(4));
        (void)Visit(*tabs.Find(other)->state, Folder(L"D:\\old\\资料2"), Identity(5));
        const auto token = tabs.Find(other)->folder_changes.Begin(L"D:\\old\\资料2");
        auto stopped = tabs.SuspendFolderWatches({ source });
        passed &= Check(stopped.tabs.size() == 2 && stopped.tabs[0] == root && stopped.tabs[1] == nested &&
            tabs.Find(root)->folder_changes.Token().path.empty() && tabs.Find(nested)->folder_changes.Token().path.empty() &&
            tabs.Find(other)->folder_changes.IsCurrent(token), "relocation_watcher_pause_selects_only_source_tree_tabs_and_invalidates_old_tokens");
    }
    {
        MoveRequest request; request.source_parent = L"D:\\old"; request.source_parent_identity = Identity(1);
        request.source.name = L"资料"; request.source.is_directory = true; request.source.attributes = FILE_ATTRIBUTE_DIRECTORY; request.source.identity = Identity(3);
        request.destination_parent = L"D:\\target"; request.destination_parent_identity = Identity(2); request.destination_name = L"资料";
        std::atomic_uint calls{};
        const auto execute = [&calls](const auto&, const auto&, const auto&) { ++calls; MoveResult result; result.moved = true; result.error = ERROR_SUCCESS; return result; };
        FileOperationCoordinator failed({}, {}, {}, execute);
        const auto failed_id = failed.MoveEntries({ request }, {}, { FailedPreparation() }); const bool failed_done = Finished(failed.CurrentAction());
        passed &= Check(failed_id && failed_done && !calls.load() && failed.Snapshot().stage == FileOperationStage::Failed &&
            failed.Snapshot().result.error == ERROR_GEN_FAILURE, "relocation_failed_watcher_prerequisite_stops_move_before_native_execution");
        FileOperationCoordinator cancelled({}, {}, {}, execute);
        auto gate = std::make_shared<winrt::handle>(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        const auto cancelled_id = cancelled.MoveEntries({ request }, {}, { AwaitPreparation(gate) }); cancelled.Cancel(); SetEvent(gate->get());
        const bool cancelled_done = Finished(cancelled.CurrentAction());
        passed &= Check(cancelled_id && cancelled_done && !calls.load() && cancelled.Snapshot().stage == FileOperationStage::Cancelled,
            "relocation_cancel_while_waiting_for_watcher_cleanup_never_enters_native_move");
        FileOperationCoordinator rename;
        const auto rename_id = rename.Rename({ request.source_parent, request.source_parent_identity, request.source, L"newname" }, {}, { FailedPreparation() });
        const bool rename_done = Finished(rename.CurrentAction());
        passed &= Check(rename_id && rename_done && rename.Snapshot().stage == FileOperationStage::Failed &&
            rename.Snapshot().result.error == ERROR_GEN_FAILURE && !rename.Snapshot().result.committed,
            "relocation_failed_watcher_prerequisite_stops_rename_before_native_execution");
    }
    try
    {
        const auto suffix = L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        const auto local = fs::absolute(fs::path(L".build") / L"folder-relocation-fixtures" / suffix);
        wchar_t temp[32768]{}; if (!GetTempPathW(static_cast<DWORD>(std::size(temp)), temp)) { throw std::runtime_error("temp path"); }
        const auto other = fs::path(temp) / L"BlinkFolderRelocation" / suffix;
        fs::create_directories(local); fs::create_directories(other);
        struct Cleanup { fs::path local, other; ~Cleanup() { std::error_code error; fs::remove_all(local, error); fs::remove_all(other, error); } } cleanup{ local, other };
        for (bool cross : { false, true })
        {
            const auto base = local / (cross ? L"cross" : L"same"), selected = base / L"selected", nested = selected / L"sub";
            const auto target = (cross ? other : local) / (cross ? L"cross-target" : L"same-target");
            fs::create_directories(nested); fs::create_directories(target); std::ofstream(nested / L"file.txt") << "owned-data";
            if (cross && Entry(base).identity.volume_serial == Entry(target).identity.volume_serial)
            { std::cout << "SKIP relocation_real_other_volume_unavailable\n"; continue; }
            TabWindowState tabs; auto* root_tab = tabs.ActiveTab(); auto* child_tab = tabs.Find(tabs.CreateTab());
            const auto open = [](TabState& state, const fs::path& path) {
                const auto request = state.Navigate(Location::Folder(path.wstring()));
                return request && state.CommitBrowse(*request, blink::core::EnumerateDirectory(path.wstring())).status == CommitBrowseStatus::Committed;
            };
            const bool opened = open(*root_tab->state, selected) && open(*child_tab->state, nested) &&
                Arm(*root_tab->folder_watcher, selected) && Arm(*child_tab->folder_watcher, nested);
            const auto old_identity = root_tab->state->CommittedDirectoryIdentity();
            MoveRequest move{ base.wstring(), Entry(base).identity, Entry(selected), target.wstring(), Entry(target).identity, L"renamed" };
            auto stopped = tabs.SuspendFolderWatches({ Location::Folder(selected.wstring()) });
            FileOperationCoordinator moving; const auto move_id = moving.MoveEntries({ move }, {}, std::move(stopped.completion));
            const bool move_done = Finished(moving.CurrentAction()); const auto moved = moving.Snapshot();
            const auto result = moved.move_result.value_or(MoveResult{}); const auto relocation = CompletedFolderRelocation(moved);
            std::cout << "INFO relocation_watched_move cross=" << cross << " opened=" << opened << " moved=" << result.moved << " error=" << result.error << '\n';
            auto requests = relocation ? tabs.RelocateFolder(relocation->source, relocation->destination, relocation->source_identity) : std::vector<RelocatedTabNavigation>{};
            bool committed = requests.size() == 2;
            for (const auto& request : requests)
            {
                committed &= tabs.Find(request.tab_id)->state->CommitBrowse(request.request,
                    blink::core::EnumerateDirectory(request.request.target.FolderPath())).status == CommitBrowseStatus::Committed;
            }
            passed &= Check(opened && move_id && move_done && stopped.tabs.size() == 2 && result.moved && relocation && committed && !fs::exists(selected) &&
                root_tab->state->PresentationSnapshot().committed_location == Location::Folder((target / L"renamed").wstring()) &&
                child_tab->state->PresentationSnapshot().committed_location == Location::Folder((target / L"renamed" / L"sub").wstring()) &&
                child_tab->state->CommittedEntriesSnapshot().size() == 1 &&
                child_tab->state->CommittedEntriesSnapshot()[0].full_path == (target / L"renamed" / L"sub" / L"file.txt").wstring() &&
                (cross ? root_tab->state->CommittedDirectoryIdentity() != old_identity : root_tab->state->CommittedDirectoryIdentity() == old_identity),
                cross ? "relocation_real_cross_volume_move_reenumerates_root_and_descendant_tabs_with_new_ids" :
                    "relocation_real_same_volume_move_reenumerates_root_and_descendant_tabs_with_same_ids");
            const bool rearmed = Arm(*root_tab->folder_watcher, target / L"renamed") && Arm(*child_tab->folder_watcher, target / L"renamed" / L"sub");
            const auto root_stop = root_tab->folder_watcher->Stop(), child_stop = child_tab->folder_watcher->Stop();
            const bool root_stopped = Finished(root_stop), child_stopped = Finished(child_stop);
            passed &= Check(rearmed && root_stopped && child_stopped, cross ?
                "relocation_real_cross_volume_rearms_root_and_descendant_watchers" : "relocation_real_same_volume_rearms_root_and_descendant_watchers");
            const auto rename_source = target / L"renamed";
            const bool rename_watches = Arm(*root_tab->folder_watcher, rename_source) && Arm(*child_tab->folder_watcher, rename_source / L"sub");
            auto rename_stopped = tabs.SuspendFolderWatches({ Location::Folder(rename_source.wstring()) });
            FileOperationCoordinator renaming;
            const auto rename_id = renaming.Rename({ target.wstring(), Entry(target).identity, Entry(rename_source), L"again" }, {},
                std::move(rename_stopped.completion));
            const bool rename_done = Finished(renaming.CurrentAction()); const auto renamed = CompletedFolderRelocation(renaming.Snapshot());
            auto rename_requests = renamed ? tabs.RelocateFolder(renamed->source, renamed->destination, renamed->source_identity) : std::vector<RelocatedTabNavigation>{};
            bool rename_committed = rename_requests.size() == 2;
            for (const auto& request : rename_requests)
            { rename_committed &= tabs.Find(request.tab_id)->state->CommitBrowse(request.request,
                blink::core::EnumerateDirectory(request.request.target.FolderPath())).status == CommitBrowseStatus::Committed; }
            const bool rename_rearmed = Arm(*root_tab->folder_watcher, target / L"again") && Arm(*child_tab->folder_watcher, target / L"again" / L"sub");
            const auto rename_root_stop = root_tab->folder_watcher->Stop(), rename_child_stop = child_tab->folder_watcher->Stop();
            const bool rename_root_stopped = Finished(rename_root_stop), rename_child_stopped = Finished(rename_child_stop);
            passed &= Check(rename_watches && rename_stopped.tabs.size() == 2 && rename_id && rename_done && renamed && rename_committed &&
                rename_rearmed && rename_root_stopped && rename_child_stopped && !fs::exists(rename_source) &&
                root_tab->state->PresentationSnapshot().committed_location == Location::Folder((target / L"again").wstring()) &&
                child_tab->state->PresentationSnapshot().committed_location == Location::Folder((target / L"again" / L"sub").wstring()),
                cross ? "relocation_real_rename_after_cross_move_drains_watches_and_follows_root_and_descendant_tabs" :
                    "relocation_real_rename_drains_watches_and_follows_root_and_descendant_tabs");
        }
    }
    catch (...) { passed &= Check(false, "relocation_real_owned_fixture_unexpected_failure"); }
    return passed;
}
