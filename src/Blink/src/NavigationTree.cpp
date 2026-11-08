#include "Blink/App/NavigationTree.h"

#include <algorithm>
#include <utility>

namespace blink::app
{
    namespace
    {
        [[nodiscard]] bool SameOrAncestor(
            const Location& ancestor,
            const Location& target) noexcept
        {
            if (!ancestor.IsFolder() || !target.IsFolder())
            {
                return false;
            }

            const auto& ancestor_path = ancestor.FolderPath();
            const auto& target_path = target.FolderPath();
            if (ancestor == target)
            {
                return true;
            }
            if (ancestor_path.empty() || target_path.size() <= ancestor_path.size())
            {
                return false;
            }
            if (CompareStringOrdinal(
                    ancestor_path.c_str(),
                    -1,
                    target_path.c_str(),
                    static_cast<int>(ancestor_path.size()),
                    TRUE) != CSTR_EQUAL)
            {
                return false;
            }

            if (!ancestor_path.empty() &&
                (ancestor_path.back() == L'\\' || ancestor_path.back() == L'/'))
            {
                return true;
            }
            const wchar_t separator = target_path[ancestor_path.size()];
            return separator == L'\\' || separator == L'/';
        }

        [[nodiscard]] bool SortChildren(
            const NavigationTreeChildSpec& left,
            const NavigationTreeChildSpec& right) noexcept
        {
            return CompareStringOrdinal(
                       left.label.c_str(),
                       -1,
                       right.label.c_str(),
                       -1,
                       TRUE) == CSTR_LESS_THAN;
        }
    }

    NavigationTreeNodeId NavigationTreeModel::CreateNode(
        const NavigationTreeNodeKind kind,
        std::wstring label,
        Location target,
        const NavigationTreeNodeId parent_id,
        const bool can_expand,
        const bool reparse_point)
    {
        const NavigationTreeNodeId id = next_id_++;
        nodes_.emplace(id, NavigationTreeNode{
            id,
            kind,
            std::move(label),
            std::move(target),
            parent_id,
            {},
            NavigationTreeLoadState::Unloaded,
            ERROR_SUCCESS,
            can_expand,
            reparse_point,
            0 });
        return id;
    }

    NavigationTreeNodeId NavigationTreeModel::ResetTopLevel(
        std::wstring this_pc_label,
        const std::vector<NavigationTreeNodeSpec>& children)
    {
        nodes_.clear();
        root_id_ = 0;
        if (shutdown_)
        {
            return 0;
        }

        root_id_ = CreateNode(
            NavigationTreeNodeKind::ThisPC,
            std::move(this_pc_label),
            Location::ThisPC(),
            0,
            true,
            false);
        auto* root = Find(root_id_);
        if (!root)
        {
            return 0;
        }
        root->load_state = NavigationTreeLoadState::Loaded;
        root->error = ERROR_SUCCESS;
        root->expansion_generation++;

        std::vector<NavigationTreeNodeId> child_ids;
        child_ids.reserve(children.size());
        for (const auto& child : children)
        {
            if (!child.target.IsValid())
            {
                continue;
            }
            const auto child_id = CreateNode(
                child.kind,
                child.label,
                child.target,
                root_id_,
                child.can_expand,
                child.reparse_point);
            child_ids.push_back(child_id);
        }
        // CreateNode may rehash nodes_, so reacquire the root before publishing
        // the collected child IDs.
        if (auto* root_after = Find(root_id_))
        {
            root_after->child_ids = std::move(child_ids);
        }
        return root_id_;
    }

    bool NavigationTreeModel::SynchronizeThisPcContent(
        std::wstring this_pc_label,
        const std::vector<NavigationTreeNodeSpec>& children)
    {
        if (shutdown_)
        {
            return false;
        }

        if (root_id_ != 0)
        {
            const auto* root = Find(root_id_);
            if (root && root->label == this_pc_label)
            {
                std::vector<NavigationTreeNodeSpec> valid_specs;
                valid_specs.reserve(children.size());
                for (const auto& child : children)
                {
                    if (child.target.IsValid())
                    {
                        valid_specs.push_back(child);
                    }
                }

                if (root->child_ids.size() == valid_specs.size())
                {
                    bool matches = true;
                    for (std::size_t index = 0; index < valid_specs.size(); ++index)
                    {
                        const auto* child = Find(root->child_ids[index]);
                        if (!child ||
                            child->kind != valid_specs[index].kind ||
                            child->label != valid_specs[index].label ||
                            child->target != valid_specs[index].target ||
                            child->can_expand != valid_specs[index].can_expand ||
                            child->reparse_point != valid_specs[index].reparse_point)
                        {
                            matches = false;
                            break;
                        }
                    }
                    if (matches)
                    {
                        return false;
                    }
                }
            }
        }

        (void)ResetTopLevel(std::move(this_pc_label), children);
        return true;
    }

    const NavigationTreeNode* NavigationTreeModel::Find(
        const NavigationTreeNodeId id) const noexcept
    {
        const auto iterator = nodes_.find(id);
        return iterator == nodes_.end() ? nullptr : &iterator->second;
    }

    NavigationTreeNode* NavigationTreeModel::Find(
        const NavigationTreeNodeId id) noexcept
    {
        const auto iterator = nodes_.find(id);
        return iterator == nodes_.end() ? nullptr : &iterator->second;
    }

    NavigationTreeNodeId NavigationTreeModel::FindChild(
        const NavigationTreeNodeId parent_id,
        const Location& target) const noexcept
    {
        const auto* parent = Find(parent_id);
        if (!parent)
        {
            return 0;
        }
        for (const auto child_id : parent->child_ids)
        {
            const auto* child = Find(child_id);
            if (child && child->target == target)
            {
                return child_id;
            }
        }
        return 0;
    }

    NavigationTreeNodeId NavigationTreeModel::FindExact(
        const Location& target) const noexcept
    {
        for (const auto& [id, node] : nodes_)
        {
            if (node.target == target)
            {
                return id;
            }
        }
        return 0;
    }

    NavigationTreeNodeId NavigationTreeModel::FindBestAncestor(
        const Location& target) const noexcept
    {
        NavigationTreeNodeId best_id = 0;
        std::size_t best_length = 0;
        for (const auto& [id, node] : nodes_)
        {
            if (node.target.IsThisPC() && target.IsThisPC())
            {
                return id;
            }
            if (!SameOrAncestor(node.target, target))
            {
                continue;
            }
            const auto length = node.target.FolderPath().size();
            if (best_id == 0 || length > best_length)
            {
                best_id = id;
                best_length = length;
            }
        }
        return best_id;
    }

    std::optional<NavigationTreeExpansionToken> NavigationTreeModel::BeginExpansion(
        const NavigationTreeNodeId node_id)
    {
        auto* node = Find(node_id);
        if (shutdown_ || !node || !node->can_expand || node->reparse_point ||
            node->load_state == NavigationTreeLoadState::Loading ||
            node->load_state == NavigationTreeLoadState::Loaded ||
            !node->target.IsFolder())
        {
            return std::nullopt;
        }

        node->load_state = NavigationTreeLoadState::Loading;
        node->error = ERROR_SUCCESS;
        ++node->expansion_generation;
        return NavigationTreeExpansionToken{ node->id, node->expansion_generation, node->target };
    }

    bool NavigationTreeModel::IsExpansionCurrent(
        const NavigationTreeExpansionToken& token) const noexcept
    {
        const auto* node = Find(token.node_id);
        return !shutdown_ && node && node->target == token.target &&
            node->load_state == NavigationTreeLoadState::Loading &&
            node->expansion_generation == token.generation;
    }

    bool NavigationTreeModel::CompleteExpansion(
        const NavigationTreeExpansionToken& token,
        const std::vector<NavigationTreeChildSpec>& children)
    {
        if (!IsExpansionCurrent(token))
        {
            return false;
        }
        auto* node = Find(token.node_id);
        if (!node)
        {
            return false;
        }
        ReplaceChildren(*node, children);
        // ReplaceChildren may create nodes and rehash nodes_, so reacquire the
        // expanded node before publishing its final state.
        if (auto* completed = Find(token.node_id))
        {
            completed->load_state = NavigationTreeLoadState::Loaded;
            completed->error = ERROR_SUCCESS;
            return true;
        }
        return false;
    }

    bool NavigationTreeModel::FailExpansion(
        const NavigationTreeExpansionToken& token,
        const DWORD error)
    {
        auto* node = Find(token.node_id);
        if (!IsExpansionCurrent(token) || !node)
        {
            return false;
        }
        node->load_state = NavigationTreeLoadState::Failed;
        node->error = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
        return true;
    }

    void NavigationTreeModel::EraseSubtree(const NavigationTreeNodeId node_id) noexcept
    {
        auto iterator = nodes_.find(node_id);
        if (iterator == nodes_.end())
        {
            return;
        }
        const auto children = iterator->second.child_ids;
        for (const auto child_id : children)
        {
            EraseSubtree(child_id);
        }
        nodes_.erase(node_id);
    }

    void NavigationTreeModel::ReplaceChildren(
        NavigationTreeNode& parent,
        const std::vector<NavigationTreeChildSpec>& children)
    {
        // Location intentionally remains a value type without a hash because
        // its equality is case-insensitive. A linear lookup also keeps this
        // small direct-child operation simple and avoids a second identity
        // policy in the tree model.
        const auto parent_id = parent.id;
        std::vector<NavigationTreeNodeId> old_children = parent.child_ids;
        std::vector<bool> reused(old_children.size(), false);
        std::vector<NavigationTreeNodeId> new_children;
        new_children.reserve(children.size());

        std::vector<NavigationTreeChildSpec> sorted = children;
        std::sort(sorted.begin(), sorted.end(), SortChildren);
        for (const auto& child_spec : sorted)
        {
            if (!child_spec.target.IsValid())
            {
                continue;
            }
            NavigationTreeNodeId child_id = 0;
            for (std::size_t index = 0; index < old_children.size(); ++index)
            {
                if (reused[index])
                {
                    continue;
                }
                const auto* old_child = Find(old_children[index]);
                if (old_child && old_child->target == child_spec.target)
                {
                    child_id = old_children[index];
                    reused[index] = true;
                    break;
                }
            }

            if (child_id == 0)
            {
                child_id = CreateNode(
                    NavigationTreeNodeKind::Folder,
                    child_spec.label,
                    child_spec.target,
                    parent_id,
                    child_spec.can_expand,
                    child_spec.reparse_point);
            }
            else if (auto* existing_child = Find(child_id))
            {
                existing_child->label = child_spec.label;
                existing_child->can_expand = child_spec.can_expand;
                existing_child->reparse_point = child_spec.reparse_point;
                if (existing_child->reparse_point)
                {
                    for (const auto grandchild_id : existing_child->child_ids)
                    {
                        EraseSubtree(grandchild_id);
                    }
                    existing_child->child_ids.clear();
                    existing_child->load_state = NavigationTreeLoadState::Unloaded;
                }
            }
            new_children.push_back(child_id);
        }

        for (std::size_t index = 0; index < old_children.size(); ++index)
        {
            if (!reused[index])
            {
                EraseSubtree(old_children[index]);
            }
        }

        // CreateNode may rehash nodes_, so reacquire the parent before
        // publishing the replacement child list.
        if (auto* parent_after = Find(parent_id))
        {
            parent_after->child_ids = std::move(new_children);
        }
    }

    void NavigationTreeModel::Shutdown() noexcept
    {
        shutdown_ = true;
        nodes_.clear();
        root_id_ = 0;
    }

    std::vector<NavigationTreeChildSpec> FilterNavigationTreeChildren(
        const blink::core::DirectoryListing& listing,
        const ItemVisibilityPolicy policy)
    {
        std::vector<NavigationTreeChildSpec> children;
        children.reserve(listing.entries.size());
        for (const auto& entry : listing.entries)
        {
            if (!entry.is_directory ||
                !policy.Includes(entry.attributes))
            {
                continue;
            }

            const auto path = entry.full_path.empty() ?
                blink::core::JoinPath(listing.path, entry.name) : entry.full_path;
            const auto target = Location::Folder(path);
            if (!target.IsValid())
            {
                continue;
            }
            const bool reparse =
                (entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                entry.reparse_tag != 0;
            children.push_back(NavigationTreeChildSpec{
                entry.name,
                target,
                !reparse,
                reparse });
        }
        std::sort(children.begin(), children.end(), SortChildren);
        return children;
    }
}
