#include "ExplorerCommandBarTests.h"
#include "Blink/App/ExplorerCommandBarModel.h"
#include "Blink/App/TabState.h"

#include <array>
#include <initializer_list>
#include <iostream>
#include <utility>

namespace
{
    using namespace blink::app;
    using Action = ExplorerCommandBarAction;
    using Status = FolderCommandStatus;
    using Surface = ExplorerCommandBarSurface;

    bool Check(const bool value, const char* name)
    {
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
        return value;
    }

    ExplorerCommandBarContext Folder(const std::size_t selected = 0)
    {
        ExplorerCommandBarContext context;
        context.surface = Surface::Folder;
        context.folder = { true, false, selected, selected != 0, true, false };
        context.has_file_drop = true;
        context.item_count = 3;
        return context;
    }

    constexpr std::array SelectedActions{ Action::Cut, Action::Copy, Action::Rename,
        Action::Delete, Action::Properties, Action::CopyFullPath, Action::CopyParentPath };
    constexpr std::array WritableSearchActions{ Action::Cut, Action::Copy, Action::Paste,
        Action::Rename, Action::Delete, Action::CopyFullPath, Action::CopyParentPath };
    constexpr std::array FolderOnlyActions{ Action::NewFolder, Action::Sort, Action::View,
        Action::SelectAll, Action::ClearSelection, Action::InvertSelection };
    constexpr std::array AllActions{ Action::NewFolder, Action::Cut, Action::Copy, Action::Paste,
        Action::Rename, Action::Delete, Action::Sort, Action::View, Action::More, Action::Properties,
        Action::Refresh, Action::SelectAll, Action::ClearSelection, Action::InvertSelection,
        Action::CopyFullPath, Action::CopyParentPath };

    template <std::size_t Count>
    bool AllAre(const std::array<Action, Count>& actions, const ExplorerCommandBarContext& context,
        const Status expected)
    {
        for (const auto action : actions)
        {
            if (EvaluateExplorerCommandBarAction(action, context) != expected) { return false; }
        }
        return true;
    }

    bool Ready(const Action action, const ExplorerCommandBarContext& context)
    {
        return EvaluateExplorerCommandBarAction(action, context) == Status::Ready;
    }

    blink::core::FileEntry ModelEntry(std::wstring name, const std::uint8_t id,
        const DWORD attributes = FILE_ATTRIBUTE_NORMAL)
    {
        blink::core::FileEntry entry;
        entry.name = std::move(name);
        entry.full_path = L"D:\\command-bar-model\\" + entry.name;
        entry.attributes = attributes;
        entry.is_directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.identity.volume_serial = 23023;
        entry.identity.file_id[0] = id;
        entry.identity.valid = id != 0;
        return entry;
    }

    blink::core::DirectoryListing ModelListing(
        std::initializer_list<blink::core::FileEntry> entries)
    {
        blink::core::DirectoryListing listing;
        listing.path = L"D:\\command-bar-model";
        listing.directory_identity = ModelEntry(L"", 99).identity;
        listing.entries = entries;
        return listing;
    }

    bool CommitModel(TabState& tab, const blink::core::DirectoryListing& listing)
    {
        const auto request = tab.Navigate(listing.path);
        if (!request) { return false; }
        const auto result = tab.CommitBrowse(*request, listing);
        return result.status == CommitBrowseStatus::Committed && result.error == ERROR_SUCCESS;
    }
}

bool RunExplorerCommandBarTests()
{
    bool passed = true;
    auto context = Folder();
    passed &= Check(AllAre(SelectedActions, context, Status::SelectionRequired) &&
        Ready(Action::NewFolder, context) && Ready(Action::Paste, context),
        "command_bar_empty_selection_keeps_destination_commands_without_item_actions");

    context = Folder(1);
    passed &= Check(AllAre(SelectedActions, context, Status::Ready),
        "command_bar_single_authoritative_target_enables_existing_item_actions");

    context = Folder(2); context.folder.has_open_target = false;
    passed &= Check(Ready(Action::Cut, context) && Ready(Action::Copy, context) &&
        Ready(Action::Delete, context) && Ready(Action::Properties, context) &&
        Ready(Action::CopyFullPath, context) && Ready(Action::CopyParentPath, context) &&
        EvaluateExplorerCommandBarAction(Action::Rename, context) == Status::SingleSelectionRequired,
        "command_bar_multiple_rows_allow_bulk_actions_without_single_row_rename");

    context = Folder(1); context.folder.has_open_target = false;
    const bool missing_target = AllAre(SelectedActions, context, Status::TargetUnavailable);
    context = Folder(2); context.folder.has_directory_identity = false;
    passed &= Check(missing_target &&
        EvaluateExplorerCommandBarAction(Action::Copy, context) == Status::TargetUnavailable &&
        EvaluateExplorerCommandBarAction(Action::Properties, context) == Status::TargetUnavailable &&
        EvaluateExplorerCommandBarAction(Action::NewFolder, context) == Status::TargetUnavailable &&
        EvaluateExplorerCommandBarAction(Action::Paste, context) == Status::TargetUnavailable,
        "command_bar_missing_committed_target_or_directory_identity_never_uses_visible_row");

    context = Folder(1); context.has_file_drop = false;
    passed &= Check(EvaluateExplorerCommandBarAction(Action::Paste, context) == Status::TargetUnavailable &&
        Ready(Action::Copy, context) && Ready(Action::NewFolder, context),
        "command_bar_paste_requires_file_drop_without_disabling_unrelated_commands");

    context = Folder(1); context.folder.busy = true;
    passed &= Check(AllAre(AllActions, context, Status::Busy),
        "command_bar_pending_navigation_or_projection_blocks_current_page_commands");

    context = Folder(1); context.folder.mutation_busy = true;
    passed &= Check(AllAre(WritableSearchActions, context, Status::Busy) &&
        Ready(Action::Properties, context) && Ready(Action::View, context) &&
        Ready(Action::Sort, context) && Ready(Action::Refresh, context),
        "command_bar_active_transfer_preserves_existing_nonmutating_capabilities");

    bool modal_blocks = true;
    for (const auto surface : { Surface::Folder, Surface::ThisPc, Surface::Search })
    {
        context = Folder(1); context.surface = surface; context.search_ready = true;
        context.has_search_selection = true; context.modal_open = true;
        modal_blocks &= AllAre(AllActions, context, Status::Busy);
    }
    passed &= Check(modal_blocks, "command_bar_modal_dialog_blocks_all_surfaces_including_more");

    context = Folder();
    passed &= Check(Ready(Action::SelectAll, context) && Ready(Action::InvertSelection, context) &&
        EvaluateExplorerCommandBarAction(Action::ClearSelection, context) == Status::SelectionRequired,
        "command_bar_nonempty_folder_selection_actions_do_not_require_an_existing_selection");

    context.item_count = 0;
    passed &= Check(EvaluateExplorerCommandBarAction(Action::SelectAll, context) == Status::TargetUnavailable &&
        EvaluateExplorerCommandBarAction(Action::InvertSelection, context) == Status::TargetUnavailable &&
        Ready(Action::NewFolder, context) && Ready(Action::Paste, context) && Ready(Action::More, context),
        "command_bar_empty_folder_disables_selection_but_retains_real_destination_commands");

    context = Folder(1); context.folder.mutation_busy = true;
    passed &= Check(Ready(Action::ClearSelection, context) && Ready(Action::SelectAll, context) &&
        Ready(Action::InvertSelection, context),
        "command_bar_selection_changes_remain_available_during_nonmodal_transfer");

    context = Folder(2); context.surface = Surface::ThisPc;
    bool this_pc_whitelist = true;
    for (const auto action : AllActions)
    {
        const auto expected = action == Action::View || action == Action::Refresh || action == Action::More ?
            Status::Ready : Status::FolderRequired;
        this_pc_whitelist &= EvaluateExplorerCommandBarAction(action, context) == expected;
    }
    passed &= Check(this_pc_whitelist,
        "command_bar_this_pc_never_inherits_file_actions_from_old_folder_selection");

    context.folder.busy = true;
    passed &= Check(EvaluateExplorerCommandBarAction(Action::View, context) == Status::Busy &&
        EvaluateExplorerCommandBarAction(Action::Refresh, context) == Status::Busy &&
        EvaluateExplorerCommandBarAction(Action::More, context) == Status::Busy,
        "command_bar_this_pc_pending_projection_disables_its_real_commands");

    context = Folder(2); context.surface = Surface::Search; context.search_ready = true;
    passed &= Check(AllAre(SelectedActions, context, Status::SelectionRequired) &&
        EvaluateExplorerCommandBarAction(Action::Paste, context) == Status::SelectionRequired,
        "command_bar_search_never_borrows_the_browsing_page_selection_or_destination");

    context.has_search_selection = true;
    passed &= Check(AllAre(SelectedActions, context, Status::Ready),
        "command_bar_current_single_search_result_uses_existing_result_actions");

    context.has_file_drop = false;
    passed &= Check(EvaluateExplorerCommandBarAction(Action::Paste, context) == Status::TargetUnavailable &&
        Ready(Action::Copy, context) && Ready(Action::Properties, context),
        "command_bar_search_paste_parent_requires_clipboard_files");

    context.has_file_drop = true; context.folder.mutation_busy = true;
    passed &= Check(AllAre(WritableSearchActions, context, Status::Busy) &&
        Ready(Action::Properties, context) && Ready(Action::Refresh, context) && Ready(Action::More, context),
        "command_bar_search_transfer_busy_matches_context_menu_without_disabling_properties");

    context.search_ready = false; context.folder.busy = true;
    passed &= Check(AllAre(SelectedActions, context, Status::Busy) &&
        EvaluateExplorerCommandBarAction(Action::Paste, context) == Status::Busy &&
        Ready(Action::Refresh, context) && Ready(Action::More, context),
        "command_bar_search_refresh_remains_available_without_a_completed_result_generation");

    context.search_ready = true; context.folder.busy = false; context.folder.mutation_busy = false;
    passed &= Check(AllAre(FolderOnlyActions, context, Status::FolderRequired),
        "command_bar_search_does_not_offer_fake_folder_projection_or_selection_modes");

    const auto visible = ModelEntry(L"visible.txt", 1);
    const auto hidden = ModelEntry(L"hidden.txt", 2, FILE_ATTRIBUTE_HIDDEN);
    const auto system = ModelEntry(L"system.txt", 3, FILE_ATTRIBUTE_SYSTEM);
    const auto protected_item = ModelEntry(L"protected.txt", 4,
        FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);
    const auto invalid = ModelEntry(L"unavailable.txt", 0);
    TabState filtered;
    const bool filtered_commit = CommitModel(filtered,
        ModelListing({ visible, hidden, system, protected_item, invalid }));
    const bool normal_inversion = filtered_commit && filtered.InvertRowSelection() &&
        filtered.PresentationSnapshot().selected_count == 1 &&
        filtered.IsRowSelected(MakeRowIdentity(visible)) &&
        !filtered.IsRowSelected(MakeRowIdentity(hidden)) &&
        !filtered.IsRowSelected(MakeRowIdentity(invalid));
    filtered.ClearSelection();
    filtered.SetVisibilityPolicy({ true });
    passed &= Check(normal_inversion && filtered.InvertRowSelection() &&
        filtered.PresentationSnapshot().selected_count == 2 &&
        filtered.IsRowSelected(MakeRowIdentity(visible)) &&
        filtered.IsRowSelected(MakeRowIdentity(hidden)) &&
        !filtered.IsRowSelected(MakeRowIdentity(system)) &&
        !filtered.IsRowSelected(MakeRowIdentity(protected_item)) &&
        !filtered.IsRowSelected(MakeRowIdentity(invalid)),
        "command_bar_invert_uses_visibility_and_excludes_system_and_invalid_rows");

    const auto first_link = ModelEntry(L"first-link.txt", 10);
    const auto second_link = ModelEntry(L"second-link.txt", 10);
    const auto independent = ModelEntry(L"other.txt", 11);
    TabState links;
    const bool links_commit = CommitModel(links,
        ModelListing({ first_link, second_link, independent }));
    links.SetRowSelection({ MakeRowIdentity(first_link) });
    passed &= Check(links_commit && links.InvertRowSelection() &&
        links.PresentationSnapshot().selected_count == 2 &&
        !links.IsRowSelected(MakeRowIdentity(first_link)) &&
        links.IsRowSelected(MakeRowIdentity(second_link)) &&
        links.IsRowSelected(MakeRowIdentity(independent)),
        "command_bar_invert_keeps_distinct_hardlink_names_independent");

    links.SetSortDescriptor({ SortField::Size, SortDirection::Descending });
    links.SetViewMode(FolderViewMode::Grid);
    const auto before = links.Snapshot();
    const bool restored = links.InvertRowSelection() && links.InvertRowSelection();
    const auto after = links.Snapshot();
    passed &= Check(restored && after.selection == before.selection &&
        after.sort == before.sort && after.view_mode == before.view_mode &&
        after.navigation.committed_location == before.navigation.committed_location &&
        after.navigation.back_history == before.navigation.back_history &&
        after.navigation.forward_history == before.navigation.forward_history &&
        after.committed_listing.entries.size() == before.committed_listing.entries.size() &&
        after.focused_item && links.IsRowSelected(*after.focused_item),
        "command_bar_two_inversions_restore_selection_without_resetting_navigation_view_or_sort");

    TabState empty;
    passed &= Check(CommitModel(empty, ModelListing({})) && empty.InvertRowSelection() &&
        empty.PresentationSnapshot().selected_count == 0 && !empty.Snapshot().focused_item,
        "command_bar_invert_empty_committed_folder_succeeds_without_a_focus_target");
    return passed;
}
