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

    bool PlanIs(const AddressCompletionPlan& plan, const AddressCompletionSource source,
        const std::wstring_view directory = {}, const std::wstring_view prefix = {})
    { return plan.source == source && plan.directory == directory && plan.prefix == prefix; }

    bool CompletionTargets(const std::vector<AddressBreadcrumb>& actual,
        const std::initializer_list<Location> expected)
    {
        if (actual.size() != expected.size()) { return false; }
        return std::equal(actual.begin(), actual.end(), expected.begin(),
            [](const AddressBreadcrumb& item, const Location& target) { return item.location == target; });
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

    const auto base = Location::Folder(L"D:\\completion-model\\项目 文档");
    using Source = AddressCompletionSource;
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"", this_pc, L"此电脑"), Source::ThisPC) &&
        PlanIs(BuildAddressCompletionPlan(L"", base, L"此电脑"), Source::Directory, base.FolderPath()),
        "address_completion_empty_uses_virtual_places_or_committed_directory");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"c:", base, L"此电脑"), Source::ThisPC, {}, L"c:") &&
        PlanIs(BuildAddressCompletionPlan(L"C:relative", base, L"此电脑"), Source::None),
        "address_completion_drive_prefix_never_reads_per_drive_current_directory");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"C:\\", this_pc, L"此电脑"), Source::Directory, L"C:\\") &&
        PlanIs(BuildAddressCompletionPlan(L"C:\\Win", base, L"此电脑"), Source::Directory, L"C:\\", L"Win"),
        "address_completion_drive_root_and_root_child_split_correctly");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"D:/completion-model/项目 文档/资", this_pc, L"此电脑"),
        Source::Directory, base.FolderPath(), L"资"),
        "address_completion_absolute_unicode_spaces_and_forward_separators");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"资料", base, L"此电脑"),
        Source::Directory, base.FolderPath(), L"资料") &&
        PlanIs(BuildAddressCompletionPlan(L"nested\\资", base, L"此电脑"),
            Source::Directory, base.FolderPath() + L"\\nested", L"资"),
        "address_completion_relative_prefix_resolves_from_committed_folder");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"资料", this_pc, L"此电脑"), Source::ThisPC, {}, L"资料") &&
        PlanIs(BuildAddressCompletionPlan(L"nested\\资", this_pc, L"此电脑"), Source::None) &&
        PlanIs(BuildAddressCompletionPlan(L"..", this_pc, L"此电脑"), Source::None),
        "address_completion_this_pc_relative_text_does_not_fabricate_process_cwd");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L".", base, L"此电脑"), Source::Directory, base.FolderPath()) &&
        PlanIs(BuildAddressCompletionPlan(L"..", base, L"此电脑"), Source::Directory, L"D:\\completion-model") &&
        PlanIs(BuildAddressCompletionPlan(L"..\\资", base, L"此电脑"), Source::Directory, L"D:\\completion-model", L"资"),
        "address_completion_dot_and_parent_are_resolved_lexically");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"nested\\..\\资料\\", base, L"此电脑"),
        Source::Directory, base.FolderPath() + L"\\资料") &&
        PlanIs(BuildAddressCompletionPlan(L"..\\..\\..\\资", base, L"此电脑"), Source::Directory, L"D:\\", L"资"),
        "address_completion_directory_normalization_removes_dot_segments_and_stops_at_volume_root");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"\\Win", base, L"此电脑"), Source::Directory, L"D:\\", L"Win") &&
        PlanIs(BuildAddressCompletionPlan(L"\\Win", this_pc, L"此电脑"), Source::None),
        "address_completion_rooted_input_uses_committed_volume_not_process_drive");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"\\\\completion-model.invalid", base, L"此电脑"), Source::None) &&
        PlanIs(BuildAddressCompletionPlan(L"\\\\completion-model.invalid\\", base, L"此电脑"), Source::None) &&
        PlanIs(BuildAddressCompletionPlan(L"\\\\completion-model.invalid\\\\share", base, L"此电脑"), Source::None),
        "address_completion_incomplete_unc_server_never_becomes_directory_enumeration");
    const std::wstring unc_root = L"\\\\completion-model.invalid\\share";
    passed &= Check(PlanIs(BuildAddressCompletionPlan(unc_root, base, L"此电脑"), Source::Directory, unc_root) &&
        PlanIs(BuildAddressCompletionPlan(unc_root + L"\\", base, L"此电脑"), Source::Directory, unc_root) &&
        PlanIs(BuildAddressCompletionPlan(unc_root + L"\\资料\\子", base, L"此电脑"),
            Source::Directory, unc_root + L"\\资料", L"子"),
        "address_completion_unc_share_root_and_child_keep_real_share_boundary");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"..\\子", Location::Folder(unc_root + L"\\资料"), L"此电脑"),
        Source::Directory, unc_root, L"子") &&
        PlanIs(BuildAddressCompletionPlan(L"\\资料\\子", Location::Folder(unc_root + L"\\other"), L"此电脑"),
            Source::Directory, unc_root + L"\\资料", L"子"),
        "address_completion_relative_and_rooted_unc_use_committed_share");
    const auto long_completion = BuildAddressCompletionPlan(long_path + L"\\资料-😀", this_pc, L"This PC");
    passed &= Check(long_completion.directory == long_path && long_completion.directory.size() > 260 &&
        long_completion.source == Source::Directory && long_completion.prefix == L"资料-😀",
        "address_completion_long_directory_and_unicode_prefix_remain_complete");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"\\\\?\\D:\\资料\\子", this_pc, L"This PC"),
        Source::Directory, L"\\\\?\\D:\\资料", L"子") &&
        PlanIs(BuildAddressCompletionPlan(L"\\\\?\\UNC\\completion-model.invalid\\share\\子", this_pc, L"This PC"),
            Source::Directory, L"\\\\?\\UNC\\completion-model.invalid\\share", L"子"),
        "address_completion_extended_drive_and_unc_paths_keep_namespace");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L" \t此电脑\t ", base, L"此电脑"), Source::None) &&
        PlanIs(BuildAddressCompletionPlan(L"tHIS pC", base, L"This PC"), Source::None),
        "address_completion_complete_localized_virtual_alias_does_not_become_folder_path");
    bool unsupported = true;
    for (const auto text : { L"https://example.invalid/path", L"sftp://host/path", L"webdav://host/path",
        L"shell:Downloads", L"%USERPROFILE%\\资料", L"$env:USERPROFILE", L"\\\\.\\C:\\", L"\\\\?\\GLOBALROOT\\Device" })
    { unsupported &= PlanIs(BuildAddressCompletionPlan(text, base, L"此电脑"), Source::None); }
    passed &= Check(unsupported, "address_completion_does_not_expand_uri_shell_environment_or_device_syntax");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(std::wstring(L"a\0b", 3), base, L"此电脑"), Source::None) &&
        PlanIs(BuildAddressCompletionPlan(L"prefix", Location::Folder(L""), L"此电脑"), Source::None),
        "address_completion_invalid_draft_or_committed_folder_has_no_plan");
    passed &= Check(PlanIs(BuildAddressCompletionPlan(L"100%", base, L"此电脑"),
        Source::Directory, base.FolderPath(), L"100%") &&
        PlanIs(BuildAddressCompletionPlan(L"D:\\literal%folder%\\子", this_pc, L"此电脑"),
            Source::Directory, L"D:\\literal%folder%", L"子"),
        "address_completion_literal_percent_folder_names_are_not_environment_expansion");

    const auto alpha = Location::Folder(L"D:\\completion-model\\Alpha");
    const auto alpine = Location::Folder(L"D:\\completion-model\\ALPine");
    const auto beta = Location::Folder(L"D:\\completion-model\\Beta");
    const std::vector<AddressBreadcrumb> candidates{
        { L"ALPine", alpine }, { L"Alpha", alpha }, { L"Beta", beta },
        { L"Alpha duplicate", Location::Folder(L"d:\\COMPLETION-MODEL\\ALPHA") },
        { L"Alpha virtual", this_pc }, { L"Alpha invalid", Location::Folder(L"") },
        { L"", Location::Folder(L"D:\\completion-model\\unlabelled") } };
    const AddressCompletionPlan partial{ Source::Directory, L"D:\\completion-model", L"aLp" };
    const auto matched = FilterAddressCompletions(partial, candidates);
    passed &= Check(CompletionTargets(matched, { alpine, alpha }) &&
        matched[0].label == L"ALPine" && matched[1].label == L"Alpha",
        "address_completion_filters_case_insensitive_prefix_and_preserves_supplied_order_and_labels");
    passed &= Check(CompletionTargets(FilterAddressCompletions({ Source::Directory, partial.directory, {} }, candidates),
        { alpine, alpha, beta }),
        "address_completion_excludes_virtual_invalid_blank_and_duplicate_target_candidates");
    passed &= Check(CompletionTargets(FilterAddressCompletions(partial, candidates, 1), { alpine }) &&
        FilterAddressCompletions(partial, candidates, 0).empty() &&
        FilterAddressCompletions({}, candidates).empty() &&
        FilterAddressCompletions({ Source::Directory, partial.directory, L"missing" }, candidates).empty(),
        "address_completion_limit_zero_none_and_no_match_do_not_fabricate_choices");
    const std::vector<AddressBreadcrumb> places{
        { L"Documents", Location::Folder(L"C:\\Users\\completion\\Documents") },
        { L"System volume", Location::Folder(L"C:\\") },
        { L"Data volume", Location::Folder(L"D:\\") } };
    passed &= Check(CompletionTargets(FilterAddressCompletions({ Source::ThisPC, {}, L"c:" }, places),
        { Location::Folder(L"C:\\") }) &&
        CompletionTargets(FilterAddressCompletions({ Source::ThisPC, {}, L"doc" }, places),
            { Location::Folder(L"C:\\Users\\completion\\Documents") }),
        "address_completion_this_pc_matches_root_drive_prefix_and_localized_place_labels_separately");
    const std::vector<AddressBreadcrumb> unicode_candidates{
        { L"资料-😀", Location::Folder(L"D:\\资料-😀") },
        { L"École", Location::Folder(L"D:\\École") },
        { L"Other", Location::Folder(L"D:\\Other") } };
    passed &= Check(CompletionTargets(FilterAddressCompletions({ Source::Directory, L"D:\\", L"资料" }, unicode_candidates),
        { Location::Folder(L"D:\\资料-😀") }) &&
        CompletionTargets(FilterAddressCompletions({ Source::Directory, L"D:\\", L"éC" }, unicode_candidates),
            { Location::Folder(L"D:\\École") }),
        "address_completion_ordinal_unicode_case_and_surrogate_names_keep_exact_targets");
    std::vector<AddressBreadcrumb> many;
    for (std::size_t index = 0; index < 16; ++index)
    { many.push_back({ L"folder-" + std::to_wstring(index), Location::Folder(L"D:\\folder-" + std::to_wstring(index)) }); }
    const auto bounded = FilterAddressCompletions({ Source::Directory, L"D:\\", {} }, many);
    passed &= Check(bounded.size() == 12 && bounded.front().location == many.front().location &&
        bounded.back().location == many[11].location,
        "address_completion_default_limit_is_twelve_without_reordering_backend_folders");
    passed &= Check(candidates.size() == 7 && candidates[0].label == L"ALPine" &&
        candidates[3].label == L"Alpha duplicate" && partial.prefix == L"aLp" &&
        partial.directory == L"D:\\completion-model",
        "address_completion_filter_keeps_available_snapshot_and_request_unchanged");
    return passed;
}
