#pragma once

#include "Blink/App/ItemVisibilityPolicy.h"

#include "Blink/App/Location.h"
#include "Blink/Core/FileTypes.h"

#include <Windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace blink::app
{
    using NavigationTreeNodeId = std::uint64_t;

    enum class NavigationTreeNodeKind
    {
        ThisPC,
        Volume,
        Folder,
    };

    enum class NavigationTreeLoadState
    {
        Unloaded,
        Loading,
        Loaded,
        Failed,
    };

    struct NavigationTreeNodeSpec
    {
        NavigationTreeNodeKind kind{ NavigationTreeNodeKind::Folder };
        std::wstring label;
        Location target;
        bool can_expand{ true };
        bool reparse_point{};
    };

    struct NavigationTreeChildSpec
    {
        std::wstring label;
        Location target;
        bool can_expand{ true };
        bool reparse_point{};
    };

    struct NavigationTreeNode
    {
        NavigationTreeNodeId id{};
        NavigationTreeNodeKind kind{ NavigationTreeNodeKind::Folder };
        std::wstring label;
        Location target;
        NavigationTreeNodeId parent_id{};
        std::vector<NavigationTreeNodeId> child_ids;
        NavigationTreeLoadState load_state{ NavigationTreeLoadState::Unloaded };
        DWORD error{ ERROR_SUCCESS };
        bool can_expand{ true };
        bool reparse_point{};
        std::uint64_t expansion_generation{};
    };

    struct NavigationTreeExpansionToken
    {
        NavigationTreeNodeId node_id{};
        std::uint64_t generation{};
        Location target;
    };

    // App-private authoritative tree state. WinUI TreeViewNode objects are
    // only a presentation of this model and are never used as business
    // identity. The model is window-owned, so expansion state is shared by
    // tabs while the selected/revealed node follows the active tab.
    class NavigationTreeModel
    {
    public:
        NavigationTreeModel() = default;

        NavigationTreeModel(const NavigationTreeModel&) = delete;
        NavigationTreeModel& operator=(const NavigationTreeModel&) = delete;

        [[nodiscard]] NavigationTreeNodeId ResetTopLevel(
            std::wstring this_pc_label,
            const std::vector<NavigationTreeNodeSpec>& children);

        // Synchronizes the top-level tree with the given ThisPC content.
        // If the tree is already initialized with matching top-level volume specs,
        // this is a no-op and returns false (idempotent, no tree reset or generation churn).
        // Returns true if the top-level tree was created or replaced.
        [[nodiscard]] bool SynchronizeThisPcContent(
            std::wstring this_pc_label,
            const std::vector<NavigationTreeNodeSpec>& children);

        [[nodiscard]] NavigationTreeNodeId RootId() const noexcept
        {
            return root_id_;
        }

        [[nodiscard]] const NavigationTreeNode* Find(
            NavigationTreeNodeId id) const noexcept;

        [[nodiscard]] NavigationTreeNode* Find(
            NavigationTreeNodeId id) noexcept;

        [[nodiscard]] NavigationTreeNodeId FindChild(
            NavigationTreeNodeId parent_id,
            const Location& target) const noexcept;

        [[nodiscard]] NavigationTreeNodeId FindExact(
            const Location& target) const noexcept;

        // Returns the longest already-present ancestor. It never fabricates
        // a node for an unrepresented location.
        [[nodiscard]] NavigationTreeNodeId FindBestAncestor(
            const Location& target) const noexcept;

        [[nodiscard]] std::optional<NavigationTreeExpansionToken> BeginExpansion(
            NavigationTreeNodeId node_id);

        [[nodiscard]] bool CompleteExpansion(
            const NavigationTreeExpansionToken& token,
            const std::vector<NavigationTreeChildSpec>& children);

        [[nodiscard]] bool FailExpansion(
            const NavigationTreeExpansionToken& token,
            DWORD error);

        [[nodiscard]] bool IsExpansionCurrent(
            const NavigationTreeExpansionToken& token) const noexcept;

        void Shutdown() noexcept;

    private:
        [[nodiscard]] NavigationTreeNodeId CreateNode(
            NavigationTreeNodeKind kind,
            std::wstring label,
            Location target,
            NavigationTreeNodeId parent_id,
            bool can_expand,
            bool reparse_point);

        void EraseSubtree(NavigationTreeNodeId node_id) noexcept;
        void ReplaceChildren(
            NavigationTreeNode& parent,
            const std::vector<NavigationTreeChildSpec>& children);

        std::unordered_map<NavigationTreeNodeId, NavigationTreeNode> nodes_;
        NavigationTreeNodeId root_id_{};
        NavigationTreeNodeId next_id_{ 1 };
        bool shutdown_{};
    };

    // Converts one direct directory listing into safe tree children. Files,
    // excluded entries and invalid locations are omitted. Reparse-point
    // directories are represented but marked non-expandable; their targets
    // are never followed by the tree.
    [[nodiscard]] std::vector<NavigationTreeChildSpec> FilterNavigationTreeChildren(
        const blink::core::DirectoryListing& listing,
        ItemVisibilityPolicy policy = {});
}
