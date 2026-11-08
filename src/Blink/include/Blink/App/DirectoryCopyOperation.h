#pragma once

#include "Blink/App/FileCopyOperation.h"
#include <cstddef>

namespace blink::app
{
    inline constexpr std::size_t MaximumDirectoryCopyEntries = 10000;
    inline constexpr std::size_t MaximumDirectoryCopyDirectories = 256;
    inline constexpr std::size_t MaximumDirectoryCopyDepth = 128;
    inline constexpr std::size_t MaximumDirectoryCopyPathBytes = 16 * 1024 * 1024;
    struct DirectoryCopyLimits
    {
        std::size_t max_entries{ MaximumDirectoryCopyEntries }; // Descendants; root excluded.
        std::size_t max_directories{ MaximumDirectoryCopyDirectories }; // Root included.
        std::size_t max_depth{ MaximumDirectoryCopyDepth }; // Root 0, every child adds 1.
        std::size_t max_path_bytes{ MaximumDirectoryCopyPathBytes }; // Conservative UTF-16 plan/inventory + path scratch budget.
    };
    struct DirectoryCopyConflict
    {
        FileCopyRequest request;
        bool destination_is_directory{};
    };
    using DirectoryCopyConflictCallback = std::function<FileCopyConflictPolicy(const DirectoryCopyConflict&)>;
    struct DirectoryCopyRequest
    {
        std::wstring source_parent;
        blink::core::FileIdentity source_parent_identity;
        blink::core::FileEntry source;
        std::wstring destination_parent;
        blink::core::FileIdentity destination_parent_identity;
        std::wstring destination_name;
        DirectoryCopyLimits limits;
        FileCopyConflictPolicy conflict_policy{ FileCopyConflictPolicy::Fail };
        DirectoryCopyConflictCallback resolve_conflict;
    };
    struct DirectoryCopyResult
    {
        DWORD error{ ERROR_GEN_FAILURE };
        bool copied{};
        bool partial_tree_left{};
        std::wstring path;
        std::uint64_t files_copied{};
        std::uint64_t directories_created{};
        std::wstring failure_path;
        bool skipped{};
        std::uint64_t files_skipped{}, directories_skipped{};
    };
    struct DirectoryCopyProgress
    {
        std::uint64_t files_copied{};
        std::uint64_t directories_created{};
        std::wstring current_source_path;
        std::wstring current_destination_path;
        FileCopyProgress current_file;
        // True only after every planned object and directory metadata commit.
        bool completed{};
        std::uint64_t source_files_removed{}, source_directories_removed{};
        bool removing_source_directories{};
        std::uint64_t files_skipped{}, directories_skipped{};
    };
    using DirectoryCopyProgressCallback = std::function<void(const DirectoryCopyProgress&)>;

    // Worker-only bounded ordinary directory copy into a new, atomically
    // claimed root. Skip/Cancel make no filesystem calls; KeepBoth tries up to
    // 1000 atomically claimed root names using the same held plan. No new name
    // after any creation. Merge retains existing directories and their metadata;
    // each conflicting child file/type is resolved synchronously on the worker.
    // Without a resolver, a child conflict fails without overwriting it. Merge
    // does not replace whole directories and is not available for directory Move.
    // No recursive rollback or UI work.
    // This is not an atomic filesystem snapshot. A final same-handle source
    // inventory check rejects observed name/identity/type changes. Source
    // directories stay held through planning/execution; each file is
    // copied only by CopyFileSafely and rechecks its original captured identity.
    // Failure/cancellation after any create retains this operation's tree and
    // reports partial_tree_left, even if zero files were copied. Final completed
    // callback cancellation/exception cannot reverse an already committed tree.
    [[nodiscard]] DirectoryCopyResult CopyDirectorySafely(const DirectoryCopyRequest& request,
        const FileOperationCancellation& cancelled = {}, const DirectoryCopyProgressCallback& progress = {}) noexcept;

    namespace detail
    {
        struct DirectoryMoveCopyResult
        {
            DirectoryCopyResult copy;
            std::uint64_t files_removed{}, directories_removed{};
            bool source_removed{};
            DWORD removal_error{ ERROR_GEN_FAILURE };
        };
        // Reuses the directory plan and file copy implementation. Each copied
        // file is removed through its original handle; empty source directories
        // are removed last, in reverse order, after checking their captured IDs.
        // Failure preserves completed destination entries and remaining sources.
        [[nodiscard]] DirectoryMoveCopyResult CopyDirectoryAndRemoveSource(const DirectoryCopyRequest& request,
            const FileOperationCancellation& cancelled, const DirectoryCopyProgressCallback& progress,
            const std::function<DWORD()>& before_removal = {}) noexcept;
    }
}
