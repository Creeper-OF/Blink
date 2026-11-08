#pragma once
#include "Blink/App/TabWindowState.h"

namespace blink::app
{
    struct FileOperationOrigin
    {
        TabId tab_id{};
        Location location;
        blink::core::FileIdentity identity;
        std::uint64_t generation{};
        // A drop on a child folder/sidebar location can target a folder
        // other than the page which owns its command and conflict dialogs.
        std::optional<Location> owner_location;
        blink::core::FileIdentity owner_identity;
    };

    [[nodiscard]] inline FileOperationOrigin CaptureFileOperationOrigin(const TabRecord& tab)
    { return { tab.id, tab.state->PresentationSnapshot().committed_location,
        tab.state->CommittedDirectoryIdentity(), tab.file_operation_generation }; }

    // A user departure permanently invalidates queued preparation/dialog work,
    // including a failed navigation or a return to the same path and identity.
    [[nodiscard]] inline bool IsFileOperationOwnerCurrent(const FileOperationOrigin& origin,
        const TabRecord* tab, bool closed = false)
    {
        if (closed || !tab || tab->id != origin.tab_id || tab->search_visible ||
            origin.generation != tab->file_operation_generation) { return false; }
        const auto page = tab->state->PresentationSnapshot();
        const auto& location = origin.owner_location ? *origin.owner_location : origin.location;
        const auto& identity = origin.owner_location ? origin.owner_identity : origin.identity;
        if (location.IsThisPC())
        { return origin.owner_location && origin.location.IsFolder() && page.committed_location.IsThisPC() && !page.has_pending; }
        return identity.valid && page.committed_location == location &&
            tab->state->CommittedDirectoryIdentity() == identity &&
            (!page.has_pending || (page.pending_location && *page.pending_location == location));
    }
    [[nodiscard]] inline bool IsFileOperationOriginCurrent(const FileOperationOrigin& origin,
        const TabRecord* tab, bool closed = false)
    { return origin.identity.valid && origin.location.IsFolder() && IsFileOperationOwnerCurrent(origin, tab, closed); }
}
