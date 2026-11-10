#include "AddressBarTests.h"
#include "Blink/App/AddressBarModel.h"

#include <algorithm>
#include <initializer_list>
#include <iostream>

namespace
{
    using namespace blink::app;

    bool Check(const bool value, const char* name)
    {
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
        return value;
    }

    bool HistoryIs(const std::vector<Location>& actual,
        const std::initializer_list<Location> expected)
    {
        return actual.size() == expected.size() &&
            std::equal(actual.begin(), actual.end(), expected.begin());
    }

    bool Commit(NavigationModel& navigation, const Location& target)
    {
        const auto request = navigation.Navigate(target);
        return request && navigation.Commit(*request);
    }

    bool CommitBack(NavigationModel& navigation)
    {
        const auto request = navigation.Back();
        return request && navigation.Commit(*request);
    }
}

bool RunAddressBarTests()
{
    bool passed = true;
    const auto this_pc = Location::ThisPC();
    const auto virtual_crumbs = BuildAddressBreadcrumbs(this_pc, L"此电脑");
    passed &= Check(virtual_crumbs.size() == 1 &&
        virtual_crumbs[0].label == L"此电脑" &&
        virtual_crumbs[0].location.IsThisPC() &&
        virtual_crumbs[0].location.FolderPath().empty(),
        "address_breadcrumb_this_pc_preserves_virtual_target_and_localized_label");

    const auto drive = Location::Folder(L"C:\\");
    const auto drive_crumbs = BuildAddressBreadcrumbs(drive, L"This PC");
    passed &= Check(drive_crumbs.size() == 2 &&
        drive_crumbs[0].location.IsThisPC() &&
        drive_crumbs[1].label == L"C:\\" && drive_crumbs[1].location == drive,
        "address_breadcrumb_drive_root_has_real_this_pc_parent");

    const auto nested = Location::Folder(L"C:\\工程 文档\\子目录\\leaf");
    const auto nested_crumbs = BuildAddressBreadcrumbs(nested, L"此电脑");
    passed &= Check(nested_crumbs.size() == 5 &&
        nested_crumbs[2].label == L"工程 文档" &&
        nested_crumbs[2].location == Location::Folder(L"C:\\工程 文档") &&
        nested_crumbs[3].label == L"子目录" &&
        nested_crumbs[3].location == Location::Folder(L"C:\\工程 文档\\子目录") &&
        nested_crumbs[4].label == L"leaf" && nested_crumbs[4].location == nested,
        "address_breadcrumb_unicode_and_spaces_keep_exact_ancestor_targets");

    const auto trailing = Location::Folder(L"C:\\工程 文档\\子目录\\");
    const auto trailing_crumbs = BuildAddressBreadcrumbs(trailing, L"此电脑");
    passed &= Check(trailing_crumbs.size() == 4 &&
        trailing_crumbs.back().label == L"子目录" &&
        trailing_crumbs.back().location == trailing,
        "address_breadcrumb_trailing_separator_does_not_duplicate_leaf_level");

    const auto share = Location::Folder(L"\\\\blink-address-model.invalid\\share");
    const auto remote = Location::Folder(L"\\\\blink-address-model.invalid\\share\\资料\\子目录");
    const auto remote_crumbs = BuildAddressBreadcrumbs(remote, L"此电脑");
    passed &= Check(remote_crumbs.size() == 4 &&
        remote_crumbs[0].location.IsThisPC() &&
        remote_crumbs[1].location == share &&
        remote_crumbs[1].label == share.FolderPath() &&
        remote_crumbs[2].label == L"资料" &&
        remote_crumbs[3].location == remote,
        "address_breadcrumb_unc_stops_at_share_without_fabricated_server_location");

    passed &= Check(BuildAddressBreadcrumbs(Location::Folder(L""), L"此电脑").empty(),
        "address_breadcrumb_invalid_folder_does_not_turn_into_virtual_this_pc");

    std::wstring long_path = L"C:\\";
    for (std::size_t index = 0; index < 30; ++index)
    {
        if (index != 0) { long_path += L'\\'; }
        long_path += L"long-folder-" + std::to_wstring(index);
    }
    const auto long_location = Location::Folder(long_path);
    const auto long_crumbs = BuildAddressBreadcrumbs(long_location, L"This PC");
    passed &= Check(long_location.FolderPath().size() > 260 && long_crumbs.size() == 32 &&
        long_crumbs.front().location.IsThisPC() &&
        long_crumbs.back().label == L"long-folder-29" &&
        long_crumbs.back().location == long_location,
        "address_breadcrumb_long_path_retains_all_levels_for_native_overflow");

    NavigationModel navigation;
    passed &= Check(HistoryIs(BuildAddressHistory(navigation.Snapshot()), { this_pc }),
        "address_history_initial_entry_is_real_virtual_this_pc");

    const auto a = Location::Folder(L"D:\\address-model\\A");
    const auto b = Location::Folder(L"D:\\address-model\\B");
    const auto c = Location::Folder(L"D:\\address-model\\C");
    const auto d = Location::Folder(L"D:\\address-model\\D");
    const bool timeline = Commit(navigation, a) && Commit(navigation, b) &&
        Commit(navigation, c) && Commit(navigation, d) &&
        CommitBack(navigation) && CommitBack(navigation);
    passed &= Check(timeline &&
        HistoryIs(BuildAddressHistory(navigation.Snapshot()), { b, a, this_pc, c, d }),
        "address_history_orders_current_then_nearest_back_and_forward_commits");

    const auto pending = navigation.Navigate(Location::Folder(L"Z:\\uncommitted-address-input"));
    passed &= Check(pending && navigation.Snapshot().has_pending &&
        HistoryIs(BuildAddressHistory(navigation.Snapshot()), { b, a, this_pc, c, d }),
        "address_history_pending_typed_location_never_enters_f4_choices");
    passed &= Check(pending && navigation.Reject(*pending) &&
        HistoryIs(BuildAddressHistory(navigation.Snapshot()), { b, a, this_pc, c, d }),
        "address_history_failed_navigation_keeps_only_successful_locations");

    const auto a_again = Location::Folder(L"d:\\ADDRESS-MODEL\\a");
    passed &= Check(Commit(navigation, a_again) &&
        HistoryIs(BuildAddressHistory(navigation.Snapshot()), { a_again, b, this_pc }),
        "address_history_case_insensitive_dedup_keeps_current_spelling_and_discards_branch");
    passed &= Check(HistoryIs(BuildAddressHistory(navigation.Snapshot(), 2), { a_again, b }) &&
        BuildAddressHistory(navigation.Snapshot(), 0).empty(),
        "address_history_limit_applies_after_dedup_and_zero_hides_choices");

    auto snapshot = navigation.Snapshot();
    snapshot.committed_current_path = L"Z:\\compatibility-display-text";
    snapshot.has_pending = true;
    snapshot.pending_location = Location::Folder(L"Z:\\pending-display-text");
    passed &= Check(HistoryIs(BuildAddressHistory(snapshot), { a_again, b, this_pc }),
        "address_history_uses_location_truth_instead_of_display_or_pending_paths");

    AddressBarState state;
    state.Present(7, L"C:\\committed");
    passed &= Check(!state.IsEditing() && state.Owner() == 7 &&
        state.CommittedText() == L"C:\\committed" && state.DraftText() == state.CommittedText(),
        "address_edit_present_starts_from_owner_committed_text");
    state.BeginEdit();
    state.SetDraft(L"D:\\尚未提交");
    passed &= Check(state.IsEditing() && state.DraftText() == L"D:\\尚未提交" &&
        state.CommittedText() == L"C:\\committed",
        "address_edit_typing_never_changes_committed_navigation_text");
    state.BeginEdit();
    passed &= Check(state.DraftText() == L"D:\\尚未提交",
        "address_edit_repeated_shortcut_keeps_current_draft");
    state.Present(7, L"C:\\renamed", true);
    passed &= Check(state.IsEditing() && state.DraftText() == L"D:\\尚未提交" &&
        state.CommittedText() == L"C:\\renamed",
        "address_edit_same_owner_refresh_preserves_draft_and_updates_committed_truth");
    state.Cancel();
    passed &= Check(!state.IsEditing() && state.DraftText() == L"C:\\renamed" &&
        state.CommittedText() == L"C:\\renamed",
        "address_edit_escape_restores_latest_committed_text");
    state.SetDraft(L"late-text-changed-event");
    passed &= Check(!state.IsEditing() && state.DraftText() == L"C:\\renamed",
        "address_edit_late_text_event_cannot_reopen_cancelled_draft");
    state.BeginEdit();
    state.SetDraft(L"draft-from-old-tab");
    state.Present(8, L"C:\\renamed", true);
    passed &= Check(state.Owner() == 8 && !state.IsEditing() &&
        state.DraftText() == L"C:\\renamed",
        "address_edit_tab_switch_discards_old_draft_even_when_committed_paths_match");
    state.BeginEdit();
    state.SetDraft(L"draft-before-navigation");
    state.Present(8, L"E:\\newly-committed");
    passed &= Check(!state.IsEditing() && state.DraftText() == L"E:\\newly-committed" &&
        state.CommittedText() == L"E:\\newly-committed",
        "address_edit_successful_navigation_finishes_edit_with_new_committed_text");
    state.Present(8, L"E:\\refreshed", true);
    passed &= Check(!state.IsEditing() && state.DraftText() == L"E:\\refreshed" &&
        state.CommittedText() == L"E:\\refreshed",
        "address_edit_preserving_idle_refresh_does_not_enter_edit_mode");
    return passed;
}
