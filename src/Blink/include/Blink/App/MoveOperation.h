#pragma once
#include "Blink/App/DirectoryCopyOperation.h"
#include <vector>
#include <optional>
namespace blink::app
{
    using MoveRequest = FileCopyRequest;
    enum class MovePhase { Preparing, Copying, Committing, Completed };
    struct MoveProgress
    {
        MovePhase phase{ MovePhase::Preparing };
        std::wstring source_path, destination_path;
        FileCopyProgress copy_progress;
        std::uint64_t files_copied{}, directories_created{}, source_files_removed{}, source_directories_removed{};
        bool directory{};
    };
    struct MoveResult
    {
        DWORD error{ ERROR_GEN_FAILURE };
        bool moved{}, skipped{}, unchanged{};
        std::wstring source_path, path;
        DWORD post_commit_error{};
        // Cross-volume copying may have completed while the original source
        // remains. This outcome must never be described as a completed move.
        std::optional<FileCopyResult> copy_result;
        bool source_removed{};
        std::optional<DirectoryCopyResult> directory_copy_result;
        std::uint64_t source_files_removed{}, source_directories_removed{};
        bool merged{};
        std::uint64_t files_skipped{}, directories_skipped{}, source_items_remaining{};
        std::vector<DirectoryMoveRelocation> directory_relocations;
        std::optional<bool> conflict_destination_is_directory;
    };
    enum class MoveBatchItemStage { Pending, Running, Moved, Unchanged, Skipped, Failed, Cancelled };
    struct MoveBatchItem
    {
        MoveRequest request;
        MoveBatchItemStage stage{ MoveBatchItemStage::Pending };
        MoveResult result;
        std::optional<MoveResult> retained_result;
    };
    struct MoveBatchSnapshot
    {
        std::vector<MoveBatchItem> items;
        std::size_t requested_count{}, current_index{};
        std::size_t moved_count{}, unchanged_count{}, skipped_count{}, failed_count{}, cancelled_count{}, remaining_count{}, warning_count{};
        bool resumable{};
        std::size_t partial_count{};
    };
    [[nodiscard]] inline bool MoveHasPartialDestination(const MoveResult& result) noexcept
    {
        return !result.moved && ((result.copy_result && (result.copy_result->copied || result.copy_result->partial_file_left)) ||
            (result.directory_copy_result && (((!result.merged || !result.skipped) && result.directory_copy_result->copied) || result.directory_copy_result->partial_tree_left ||
                result.directory_copy_result->directories_created || result.directory_copy_result->files_copied)));
    }
    [[nodiscard]] inline bool MoveHasSourceChanges(const MoveResult& result) noexcept
    { return result.moved || result.source_removed || result.source_files_removed || result.source_directories_removed; }
    using MoveProgressCallback = std::function<void(const MoveProgress&)>;
    using NativeMoveFunction = std::function<DWORD(HANDLE, HANDLE, const std::wstring&)>;
    [[nodiscard]] MoveResult MoveEntrySafely(const MoveRequest& request, const FileOperationCancellation& cancelled = {},
        const MoveProgressCallback& progress = {}, const NativeMoveFunction& native = {}) noexcept;
    using BeforeMoveSourceRemoval = std::function<DWORD()>;
    // Merge moves every accepted leaf once, preserves existing destination
    // directories, and only removes source directories that are actually empty.
    // A normally completed traversal with explicit Skip returns success/skipped,
    // never moved. Remaining planned descendants exclude the retained root;
    // unexpected additions can raise this lower bound on failure. Completed
    // directory relocations survive cancellation/partial results.
    [[nodiscard]] MoveResult MoveDirectoryMergeSafely(const DirectoryCopyRequest& request,
        const FileOperationCancellation& cancelled = {}, const MoveProgressCallback& progress = {},
        const BeforeMoveSourceRemoval& before_removal = {}) noexcept;
    [[nodiscard]] MoveResult MoveFileAcrossVolumesSafely(const MoveRequest& request,
        const FileOperationCancellation& cancelled = {}, const MoveProgressCallback& progress = {},
        const BeforeMoveSourceRemoval& before_removal = {}) noexcept;
    [[nodiscard]] MoveResult MoveDirectoryAcrossVolumesSafely(const MoveRequest& request,
        const FileOperationCancellation& cancelled = {}, const MoveProgressCallback& progress = {},
        const BeforeMoveSourceRemoval& before_removal = {}) noexcept;
}
