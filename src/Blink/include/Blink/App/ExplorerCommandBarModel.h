#pragma once

#include "Blink/App/FolderCommandPolicy.h"

#include <cstddef>

namespace blink::app
{
    enum class ExplorerCommandBarAction
    {
        NewFolder, Cut, Copy, Paste, Rename, Delete,
        Sort, View, More, Properties, Refresh,
        SelectAll, ClearSelection, InvertSelection,
        CopyFullPath, CopyParentPath,
    };

    enum class ExplorerCommandBarSurface
    {
        Folder,
        ThisPc,
        Search,
    };

    // Derived inputs only. The owning tab/search models retain the actual
    // location, selection, identity, query generation and operation state.
    struct ExplorerCommandBarContext
    {
        ExplorerCommandBarSurface surface{ ExplorerCommandBarSurface::ThisPc };
        FolderCommandCapabilities folder;
        bool search_ready{};
        bool has_search_selection{};
        bool has_file_drop{};
        std::size_t item_count{};
        bool modal_open{};
    };

    [[nodiscard]] constexpr FolderCommandStatus EvaluateExplorerCommandBarAction(
        const ExplorerCommandBarAction action, const ExplorerCommandBarContext& context) noexcept
    {
        using Action = ExplorerCommandBarAction;
        using Status = FolderCommandStatus;
        using Surface = ExplorerCommandBarSurface;
        if (context.modal_open) { return Status::Busy; }

        if (context.surface == Surface::Search)
        {
            switch (action)
            {
            case Action::Refresh:
            case Action::More:
                // RefreshNavigation re-submits the current search, including
                // pending/failed queries; it is not a selected-result action.
                return Status::Ready;
            case Action::NewFolder:
            case Action::Sort:
            case Action::View:
            case Action::SelectAll:
            case Action::ClearSelection:
            case Action::InvertSelection:
                return Status::FolderRequired;
            case Action::Cut:
            case Action::Copy:
            case Action::Paste:
            case Action::Rename:
            case Action::Delete:
            case Action::Properties:
            case Action::CopyFullPath:
            case Action::CopyParentPath:
                if (!context.search_ready) { return Status::Busy; }
                if (!context.has_search_selection) { return Status::SelectionRequired; }
                if (action == Action::Properties) { return Status::Ready; }
                if (context.folder.mutation_busy) { return Status::Busy; }
                // Paste is explicitly PasteParent for the one current result;
                // an old browsing-page folder is never the search destination.
                return action == Action::Paste && !context.has_file_drop ?
                    Status::TargetUnavailable : Status::Ready;
            }
            return Status::NotImplemented;
        }

        if (context.surface == Surface::ThisPc)
        {
            switch (action)
            {
            case Action::View:
                return EvaluateFolderCommand(FolderCommand::ViewDetails, context.folder);
            case Action::Refresh:
            case Action::More:
                return EvaluateFolderCommand(FolderCommand::Refresh, context.folder);
            default:
                return Status::FolderRequired;
            }
        }
        if (context.surface != Surface::Folder) { return Status::NotImplemented; }

        switch (action)
        {
        case Action::SelectAll:
        case Action::InvertSelection:
        case Action::ClearSelection:
            if (context.folder.busy) { return Status::Busy; }
            if (!context.folder.is_folder) { return Status::FolderRequired; }
            if (action == Action::ClearSelection)
            {
                return context.folder.selected_count ? Status::Ready : Status::SelectionRequired;
            }
            return context.item_count ? Status::Ready : Status::TargetUnavailable;
        default:
            break;
        }

        FolderCommand command{};
        switch (action)
        {
        case Action::NewFolder: command = FolderCommand::NewFolder; break;
        case Action::Cut: command = FolderCommand::Cut; break;
        case Action::Copy: command = FolderCommand::Copy; break;
        case Action::Paste: command = FolderCommand::Paste; break;
        case Action::Rename: command = FolderCommand::Rename; break;
        case Action::Delete: command = FolderCommand::Delete; break;
        case Action::Sort: command = FolderCommand::SortName; break;
        case Action::View: command = FolderCommand::ViewDetails; break;
        case Action::More:
        case Action::Refresh: command = FolderCommand::Refresh; break;
        case Action::Properties: command = FolderCommand::Properties; break;
        case Action::CopyFullPath: command = FolderCommand::CopyFullPath; break;
        case Action::CopyParentPath: command = FolderCommand::CopyParentPath; break;
        default: return Status::NotImplemented;
        }
        const auto status = EvaluateFolderCommand(command, context.folder);
        return status == Status::Ready && action == Action::Paste && !context.has_file_drop ?
            Status::TargetUnavailable : status;
    }
}
