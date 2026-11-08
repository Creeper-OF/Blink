#pragma once
#include "Blink/App/FileOperations.h"
#include <optional>
#include <vector>

namespace blink::app
{
    inline constexpr std::size_t MaximumDeleteItems = 1024;
    enum class DeleteMode { Recycle, Permanent };
    enum class DeleteRetryAction { Retry, Skip };
    struct DeleteRequest
    {
        std::wstring parent;
        blink::core::FileIdentity parent_identity;
        blink::core::FileEntry source;
        DeleteMode mode{ DeleteMode::Recycle };
        bool permanent_confirmed{};
        bool skip{};
    };
    struct DeleteProgress
    {
        std::wstring path;
        std::uint64_t total_work{}, completed_work{}, observed_removed{};
    };
    struct DeleteResult
    {
        HRESULT error{ E_FAIL };
        bool removed{}, recycled{}, skipped{}, possible_partial{};
        std::wstring path, failure_path;
        std::uint64_t observed_removed{};
        HRESULT post_commit_error{ S_OK };
        // Transient Shell parsing name for the actual recycled item, never
        // a custom trash directory/database. Product UI does not expose it.
        std::wstring recycle_item;
    };
    enum class DeleteItemStage { Pending, Running, Removed, Skipped, Failed, Cancelled };
    struct DeleteBatchItem
    {
        DeleteRequest request;
        DeleteItemStage stage{ DeleteItemStage::Pending };
        DeleteResult result;
        std::optional<DeleteResult> retained_result;
        std::uint64_t previous_observed_removed{};
        bool previous_possible_partial{};
    };
    struct DeleteBatchSnapshot
    {
        std::vector<DeleteBatchItem> items;
        std::size_t requested_count{}, current_index{}, removed_count{}, recycled_count{}, permanent_count{}, skipped_count{},
            failed_count{}, cancelled_count{}, remaining_count{}, partial_count{}, warning_count{};
        bool resumable{};
    };
    using DeleteProgressCallback = std::function<void(const DeleteProgress&)>;
    [[nodiscard]] inline bool DeleteCancelled(const HRESULT error) noexcept
    { return error == E_ABORT || error == HRESULT_FROM_WIN32(ERROR_CANCELLED); }
    [[nodiscard]] inline bool DeleteHasSourceChanges(const DeleteResult& result) noexcept
    { return result.removed || result.possible_partial || result.observed_removed; }
    [[nodiscard]] HRESULT ValidateDeleteRequests(const std::vector<DeleteRequest>& requests, std::size_t& invalid) noexcept;
    // STA worker only. Uses the system Shell implementation, explicitly
    // requests recycling and vetoes a non-recycle PreDelete in Recycle mode.
    // Permanent mode requires caller confirmation. No permanent fallback,
    // private trash, recursive rollback, elevated service or UI prompts.
    [[nodiscard]] DeleteResult DeleteEntrySafely(const DeleteRequest& request,
        const FileOperationCancellation& cancelled = {}, const DeleteProgressCallback& progress = {}) noexcept;
}
