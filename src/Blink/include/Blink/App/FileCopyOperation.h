#pragma once

#include "Blink/App/FileOperations.h"
#include <cstdint>

namespace blink::app
{
    enum class FileCopyConflictPolicy { Fail, Skip, KeepBoth, Cancel, Replace, Merge };

    struct FileCopyRequest
    {
        std::wstring source_parent;
        blink::core::FileIdentity source_parent_identity;
        blink::core::FileEntry source;
        std::wstring destination_parent;
        blink::core::FileIdentity destination_parent_identity;
        std::wstring destination_name;
        FileCopyConflictPolicy conflict_policy{ FileCopyConflictPolicy::Fail };
    };

    struct FileCopyProgress
    {
        std::uint64_t total_bytes{};
        std::uint64_t transferred_bytes{};
    };

    struct FileCopyResult
    {
        DWORD error{ ERROR_GEN_FAILURE };
        bool copied{};
        bool partial_file_left{};
        std::wstring path;
        bool skipped{};
    };

    using FileCopyProgressCallback = std::function<void(const FileCopyProgress&)>;

    // Worker-only single ordinary-file copy. Both directory chains and the
    // source identity stay protected until I/O and owned-target cleanup finish.
    // Explicit Replace copies to an owned sibling and commits with one rename;
    // it replaces the destination name, not an atomic expected-target-ID CAS.
    // Cancellation before that commit preserves the old file. No pause support.
    // Fail preserves existing targets. Skip/Cancel perform no filesystem I/O.
    // KeepBoth generates a visible numbered name. Replace's staging sibling is
    // removed on failure, or reported as a partial path if cleanup fails.
    // The default and named $DATA streams plus basic attributes/times are copied.
    // Reparse points, unsupported stream types and special storage are rejected.
    // ACLs inherit from the destination parent; this is not a metadata backup.
    // Intermediate callbacks may cancel/fail the operation. The final 100%
    // callback runs after commit; late cancellation/exception preserves success.
    [[nodiscard]] FileCopyResult CopyFileSafely(const FileCopyRequest& request,
        const FileOperationCancellation& cancelled = {}, const FileCopyProgressCallback& progress = {});

    namespace detail
    {
        struct FileMoveCopyResult
        {
            FileCopyResult copy;
            bool source_removed{};
            DWORD removal_error{ ERROR_GEN_FAILURE };
        };
        // The same copy implementation retains its original source/target
        // handles through source removal. It never reopens a path to delete it.
        [[nodiscard]] FileMoveCopyResult CopyFileAndRemoveSource(const FileCopyRequest& request,
            const FileOperationCancellation& cancelled, const FileCopyProgressCallback& progress,
            const std::function<DWORD()>& before_removal = {});
    }
}
