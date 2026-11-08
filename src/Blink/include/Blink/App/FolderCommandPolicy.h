#pragma once

#include <cstddef>
#include <string_view>

namespace blink::app
{
    enum class FolderCommand
    {
        Open, Cut, Copy, Rename, Delete, PermanentDelete, Properties,
        Paste, NewFolder, Refresh, CopyFullPath, CopyParentPath,
        ViewDetails, ViewList, ViewGrid,
        SortName, SortType, SortSize, SortModified, SortAscending, SortDescending,
    };

    enum class FolderCommandStatus
    {
        Ready, Busy, FolderRequired, SelectionRequired, SingleSelectionRequired,
        TargetUnavailable, NotImplemented,
    };

    // Derived command capabilities, never a second file/selection model.
    struct FolderCommandCapabilities
    {
        bool is_folder{};
        bool busy{};
        std::size_t selected_count{};
        bool has_open_target{};
        bool has_directory_identity{};
        bool mutation_busy{};
    };

    [[nodiscard]] constexpr FolderCommandStatus EvaluateFolderCommand(
        const FolderCommand command, const FolderCommandCapabilities& state) noexcept
    {
        if (state.busy)
        {
            return FolderCommandStatus::Busy;
        }
        switch (command)
        {
        case FolderCommand::Refresh:
        case FolderCommand::ViewDetails:
        case FolderCommand::ViewList:
        case FolderCommand::ViewGrid:
            return FolderCommandStatus::Ready;
        case FolderCommand::SortName:
        case FolderCommand::SortType:
        case FolderCommand::SortSize:
        case FolderCommand::SortModified:
        case FolderCommand::SortAscending:
        case FolderCommand::SortDescending:
            return state.is_folder ? FolderCommandStatus::Ready :
                FolderCommandStatus::FolderRequired;
        case FolderCommand::Open:
            if (state.selected_count == 0)
            {
                return FolderCommandStatus::SelectionRequired;
            }
            if (state.selected_count != 1)
            {
                return FolderCommandStatus::SingleSelectionRequired;
            }
            return state.has_open_target ? FolderCommandStatus::Ready :
                FolderCommandStatus::TargetUnavailable;
        case FolderCommand::NewFolder:
            if (!state.is_folder) { return FolderCommandStatus::FolderRequired; }
            if (state.mutation_busy) { return FolderCommandStatus::Busy; }
            return state.has_directory_identity ? FolderCommandStatus::Ready : FolderCommandStatus::TargetUnavailable;
        case FolderCommand::Paste:
            if (!state.is_folder) { return FolderCommandStatus::FolderRequired; }
            if (state.mutation_busy) { return FolderCommandStatus::Busy; }
            return state.has_directory_identity ? FolderCommandStatus::Ready : FolderCommandStatus::TargetUnavailable;
        case FolderCommand::Cut:
        case FolderCommand::Copy:
        case FolderCommand::CopyFullPath:
        case FolderCommand::CopyParentPath:
        case FolderCommand::Delete:
        case FolderCommand::PermanentDelete:
            if (!state.is_folder) { return FolderCommandStatus::FolderRequired; }
            if (state.selected_count == 0) { return FolderCommandStatus::SelectionRequired; }
            if (state.mutation_busy) { return FolderCommandStatus::Busy; }
            return state.has_directory_identity && (state.selected_count > 1 || state.has_open_target) ?
                FolderCommandStatus::Ready : FolderCommandStatus::TargetUnavailable;
        case FolderCommand::Rename:
            if (!state.is_folder) { return FolderCommandStatus::FolderRequired; }
            if (state.selected_count == 0) { return FolderCommandStatus::SelectionRequired; }
            if (state.selected_count != 1) { return FolderCommandStatus::SingleSelectionRequired; }
            if (state.mutation_busy) { return FolderCommandStatus::Busy; }
            return state.has_directory_identity && state.has_open_target ? FolderCommandStatus::Ready : FolderCommandStatus::TargetUnavailable;
        case FolderCommand::Properties:
            if (!state.is_folder) { return FolderCommandStatus::FolderRequired; }
            if (!state.selected_count) { return FolderCommandStatus::SelectionRequired; }
            return state.has_directory_identity && (state.selected_count > 1 || state.has_open_target) ?
                FolderCommandStatus::Ready : FolderCommandStatus::TargetUnavailable;
        default:
            if (!state.is_folder)
            {
                return FolderCommandStatus::FolderRequired;
            }
            if (command != FolderCommand::Paste && command != FolderCommand::NewFolder)
            {
                if (state.selected_count == 0)
                {
                    return FolderCommandStatus::SelectionRequired;
                }
                if ((command == FolderCommand::Rename || command == FolderCommand::Properties) &&
                    state.selected_count != 1)
                {
                    return FolderCommandStatus::SingleSelectionRequired;
                }
            }
            // Phase 1B exposes intent and feedback. Phase 1D owns actual
            // clipboard publication, mutations and product properties.
            return FolderCommandStatus::NotImplemented;
        }
    }

    [[nodiscard]] constexpr std::wstring_view FolderCommandLabelKey(
        const FolderCommand command) noexcept
    {
        switch (command)
        {
        case FolderCommand::Open: return L"Command.Open";
        case FolderCommand::Cut: return L"Command.Cut";
        case FolderCommand::Copy: return L"Command.Copy";
        case FolderCommand::CopyFullPath: return L"Search.MenuCopyPath";
        case FolderCommand::CopyParentPath: return L"Search.MenuCopyParentPath";
        case FolderCommand::Rename: return L"Command.Rename";
        case FolderCommand::Delete: return L"Command.Delete";
        case FolderCommand::PermanentDelete: return L"Command.PermanentDelete";
        case FolderCommand::Properties: return L"Command.Properties";
        case FolderCommand::Paste: return L"Command.Paste";
        case FolderCommand::NewFolder: return L"Command.NewFolder";
        case FolderCommand::Refresh: return L"Toolbar.Refresh";
        case FolderCommand::ViewDetails: return L"View.Details";
        case FolderCommand::ViewList: return L"View.List";
        case FolderCommand::ViewGrid: return L"View.Grid";
        case FolderCommand::SortName: return L"Column.Name";
        case FolderCommand::SortType: return L"Column.Type";
        case FolderCommand::SortSize: return L"Column.Size";
        case FolderCommand::SortModified: return L"Column.Modified";
        case FolderCommand::SortAscending: return L"Sort.Ascending";
        case FolderCommand::SortDescending: return L"Sort.Descending";
        }
        return L"Command.Open";
    }
}
