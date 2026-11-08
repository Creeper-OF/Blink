#pragma once
#include "Blink/App/FileOperationCoordinator.h"
#include "Blink/App/Location.h"
#include <algorithm>
#include <set>
#include <vector>

namespace blink::app
{
    [[nodiscard]] inline bool FileOperationIsTerminal(const FileOperationStage stage) noexcept
    {
        return stage == FileOperationStage::Completed || stage == FileOperationStage::Skipped ||
            stage == FileOperationStage::Cancelled || stage == FileOperationStage::Failed;
    }
    struct FolderRelocation
    {
        std::uint64_t operation_id{};
        std::size_t item_index{};
        Location source, destination;
        blink::core::FileIdentity source_identity;
        bool preserve_suffix{ true };
        std::size_t subitem_index{};
    };

    [[nodiscard]] inline std::optional<FolderRelocation> CompletedFolderRelocation(const FileOperationSnapshot& value)
    {
        if (!value.operation_id || value.closed) { return {}; }
        FolderRelocation relocation; relocation.operation_id = value.operation_id;
        if (value.kind == FileOperationKind::Move)
        {
            // Merge may leave skipped sources or rename individual children;
            // only its actual removed-directory mappings may rebase tabs.
            if (value.move_result && value.move_result->merged) { return {}; }
            if (!value.move_request || !value.move_request->source.is_directory || !value.move_result || !value.move_result->moved ||
                value.move_result->skipped || value.move_result->unchanged || value.move_result->path.empty()) { return {}; }
            relocation.source = Location::Folder(blink::core::JoinPath(value.move_request->source_parent, value.move_request->source.name));
            relocation.destination = Location::Folder(value.move_result->path);
            relocation.source_identity = value.move_request->source.identity;
            relocation.item_index = value.move_batch ? value.move_batch->current_index : 0;
        }
        else if (value.kind == FileOperationKind::Delete)
        {
            if (!value.delete_request || !value.delete_request->source.is_directory || !value.delete_result || !value.delete_result->removed) { return {}; }
            relocation.source = Location::Folder(blink::core::JoinPath(value.delete_request->parent, value.delete_request->source.name));
            relocation.destination = Location::Folder(value.delete_request->parent);
            relocation.source_identity = value.delete_request->source.identity;
            relocation.item_index = value.delete_batch ? value.delete_batch->current_index : 0;
            relocation.preserve_suffix = false;
        }
        else if (value.kind == FileOperationKind::Rename)
        {
            if (!value.rename_source || !value.rename_source->is_directory || !value.result.committed || value.result.path.empty()) { return {}; }
            relocation.source = Location::Folder(blink::core::JoinPath(value.request.parent, value.rename_source->name));
            relocation.destination = Location::Folder(value.result.path);
            relocation.source_identity = value.rename_source->identity;
        }
        else { return {}; }
        if (!relocation.source.IsValid() || !relocation.destination.IsValid()) { return {}; }
        return relocation;
    }

    [[nodiscard]] inline std::vector<FolderRelocation> CompletedFolderRelocations(const FileOperationSnapshot& value)
    {
        if (value.kind != FileOperationKind::Move || !value.move_result || !value.move_result->merged)
        {
            if (auto single = CompletedFolderRelocation(value)) { return { std::move(*single) }; }
            return {};
        }
        if (!value.operation_id || value.closed || !value.move_request || !value.move_request->source.is_directory) { return {}; }
        std::vector<FolderRelocation> result;
        result.reserve(value.move_result->directory_relocations.size());
        for (const auto& path : value.move_result->directory_relocations)
        {
            FolderRelocation relocation;
            relocation.operation_id = value.operation_id;
            relocation.item_index = value.move_batch ? value.move_batch->current_index : 0;
            relocation.source = Location::Folder(path.source_path);
            relocation.destination = Location::Folder(path.destination_path);
            relocation.source_identity = path.source_identity;
            if (relocation.source.IsValid() && relocation.destination.IsValid()) { result.push_back(std::move(relocation)); }
        }
        // An independently renamed child must follow its actual destination
        // before a broader source-root mapping rewrites its old prefix.
        std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
            return left.source.FolderPath().size() > right.source.FolderPath().size();
        });
        for (std::size_t index = 0; index < result.size(); ++index) { result[index].subitem_index = index + 1; }
        return result;
    }

    // Native publications are ordered by the window's single coordinator.
    // Each completed item may appear again in a terminal publication.
    class FolderRelocationTracker
    {
    public:
        [[nodiscard]] bool Accept(const FolderRelocation& value)
        {
            if (!value.operation_id || value.operation_id < operation_id_) { return false; }
            if (value.operation_id != operation_id_) { items_.clear(); operation_id_ = value.operation_id; }
            return items_.insert({ value.item_index, value.subitem_index }).second;
        }
    private:
        std::uint64_t operation_id_{};
        std::set<std::pair<std::size_t, std::size_t>> items_;
    };
}
