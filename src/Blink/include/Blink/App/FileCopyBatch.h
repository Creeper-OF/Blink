#pragma once

#include "Blink/App/DirectoryCopyOperation.h"
#include <optional>
#include <variant>
#include <cstddef>
#include <vector>

namespace blink::app
{
    using CopyItemRequest = std::variant<FileCopyRequest, DirectoryCopyRequest>;
    using CopyItemRequests = std::vector<CopyItemRequest>;
    [[nodiscard]] inline FileCopyRequest FileCopyProjection(const DirectoryCopyRequest& request)
    { return { request.source_parent, request.source_parent_identity, request.source, request.destination_parent,
        request.destination_parent_identity, request.destination_name, request.conflict_policy }; }
    [[nodiscard]] inline DirectoryCopyRequest DirectoryCopyProjection(const FileCopyRequest& request, DirectoryCopyLimits limits = {})
    { return { request.source_parent, request.source_parent_identity, request.source, request.destination_parent,
        request.destination_parent_identity, request.destination_name, limits, request.conflict_policy }; }
    inline constexpr std::size_t MaximumFileCopyBatchItems = 1024;
    enum class FileCopyBatchItemStage { Pending, Running, Copied, Skipped, Failed, Cancelled };
    struct FileCopyBatchItem
    {
        FileCopyRequest request;
        FileCopyBatchItemStage stage{ FileCopyBatchItemStage::Pending };
        FileCopyResult result;
        // Prior attempts are evidence, not additional completed queue items.
        // In particular a retry cannot erase an earlier residual-file report.
        std::vector<FileCopyResult> previous_results;
        // Present only for an explicitly typed directory entry. The common
        // request projection must retain matching directory type/attributes.
        std::optional<DirectoryCopyLimits> directory_limits;
        std::optional<DirectoryCopyResult> directory_result;
        // Retain earliest32 partial attempts /65536 root+failure characters;
        // the total count includes current and all historical partial attempts.
        std::vector<DirectoryCopyResult> previous_directory_results;
        std::uint64_t directory_partial_attempts{};
        std::size_t directory_history_characters{};
    };
    struct FileCopyBatchSnapshot
    {
        std::vector<FileCopyBatchItem> items;
        std::size_t requested_count{};
        // Active/failed item; items.size() after every item resolved. A queue
        // cancelled between items points at its first untouched pending item.
        std::size_t current_index{};
        std::size_t copied_count{};
        std::size_t skipped_count{};
        std::size_t failed_count{};
        std::size_t cancelled_count{};
        // Pending + Running, never fabricated as skipped/cancelled outcomes.
        std::size_t remaining_count{};
        // Items with at least one current/historical partial-file or tree report.
        // This is not an assertion that those paths still exist on disk.
        std::size_t partial_count{};
        bool resumable{};
    };
}
