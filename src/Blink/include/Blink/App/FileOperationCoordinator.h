#pragma once

#include "Blink/App/FileOperations.h"
#include "Blink/App/RenameOperation.h"
#include "Blink/App/FileCopyOperation.h"
#include "Blink/App/FileCopyBatch.h"
#include "Blink/App/DirectoryCopyOperation.h"
#include "Blink/App/MoveOperation.h"
#include "Blink/App/DeleteOperation.h"
#include <optional>
#include <winrt/Windows.Foundation.h>
#include <mutex>

namespace blink::app
{
    using FileOperationPrerequisites = std::vector<winrt::Windows::Foundation::IAsyncAction>;
    enum class FileOperationKind { NewFolder, Rename, Copy, DirectoryCopy, Move, Delete };
    enum class FileOperationStage { Idle, Queued, Running, Cancelling, Completed, Skipped, Cancelled, Failed };
    struct FileOperationSnapshot
    {
        std::uint64_t operation_id{};
        FileOperationStage stage{ FileOperationStage::Idle };
        FileOperationKind kind{ FileOperationKind::NewFolder };
        NewFolderRequest request;
        std::optional<blink::core::FileEntry> rename_source;
        std::optional<FileCopyRequest> copy_request;
        FileCopyProgress copy_progress;
        std::optional<FileCopyBatchSnapshot> copy_batch;
        NewFolderResult result;
        bool cancel_requested{};
        bool closed{};
        std::optional<DirectoryCopyRequest> directory_copy_request;
        DirectoryCopyProgress directory_copy_progress;
        // Tree partial/counts/failure path stay independent of result.partial_file_left.
        std::optional<DirectoryCopyResult> directory_copy_result;
        std::optional<MoveRequest> move_request;
        MoveProgress move_progress;
        std::optional<MoveResult> move_result;
        std::optional<MoveBatchSnapshot> move_batch;
        std::optional<DeleteRequest> delete_request;
        DeleteProgress delete_progress;
        std::optional<DeleteResult> delete_result;
        std::optional<DeleteBatchSnapshot> delete_batch;
        std::optional<DirectoryCopyConflict> directory_conflict;
        std::uint64_t directory_conflict_id{};
    };

    // Window-owned ordinary-user mutations. A cancellation request never
    // claims that a committed filesystem effect was rolled back.
    class FileOperationCoordinator
    {
    public:
        using ExecuteFunction = std::function<NewFolderResult(const NewFolderRequest&, const FileOperationCancellation&)>;
        using PublishCallback = std::function<void(FileOperationSnapshot)>;
        using CopyExecuteFunction = std::function<FileCopyResult(const FileCopyRequest&,
            const FileOperationCancellation&, const FileCopyProgressCallback&)>;
        using DirectoryCopyExecuteFunction = std::function<DirectoryCopyResult(const DirectoryCopyRequest&,
            const FileOperationCancellation&, const DirectoryCopyProgressCallback&)>;
        using MoveExecuteFunction = std::function<MoveResult(const MoveRequest&, const FileOperationCancellation&, const MoveProgressCallback&)>;
        using DeleteExecuteFunction = std::function<DeleteResult(const DeleteRequest&, const FileOperationCancellation&, const DeleteProgressCallback&)>;
        explicit FileOperationCoordinator(ExecuteFunction execute = {}, CopyExecuteFunction copy_execute = {},
            DirectoryCopyExecuteFunction directory_copy_execute = {}, MoveExecuteFunction move_execute = {}, DeleteExecuteFunction delete_execute = {});
        ~FileOperationCoordinator() noexcept;
        FileOperationCoordinator(const FileOperationCoordinator&) = delete;
        FileOperationCoordinator& operator=(const FileOperationCoordinator&) = delete;

        [[nodiscard]] std::uint64_t CreateFolder(NewFolderRequest request, PublishCallback publish = {});
        [[nodiscard]] std::uint64_t Rename(RenameRequest request, PublishCallback publish = {}, FileOperationPrerequisites prerequisites = {});
        [[nodiscard]] std::uint64_t Copy(FileCopyRequest request, PublishCallback publish = {});
        [[nodiscard]] std::uint64_t CopyBatch(std::vector<FileCopyRequest> requests, PublishCallback publish = {});
        // Explicitly typed mixed queue; serial existing engines, one owned action.
        [[nodiscard]] std::uint64_t CopyItems(CopyItemRequests requests, PublishCallback publish = {});
        // One directory through the shared action; not a file-batch entry or resume.
        [[nodiscard]] std::uint64_t CopyDirectory(DirectoryCopyRequest request, PublishCallback publish = {});
        // Resolves one child collision on the existing worker; committed
        // entries are never replayed to obtain the next decision.
        [[nodiscard]] bool ResolveDirectoryConflict(std::uint64_t operation_id, std::uint64_t conflict_id,
            FileCopyConflictPolicy policy) noexcept;
        [[nodiscard]] std::uint64_t ResumeCopyBatch(std::uint64_t expected_operation_id, FileCopyConflictPolicy policy,
            std::optional<std::wstring> new_name = {}, PublishCallback publish = {},
            std::optional<std::wstring> destination_parent = {});
        // Ordinary file/directory Move through the unique owned action.
        // Optional prerequisites wait for this app's stopped directory watches.
        // Single-entry callers remain independent of the ordered move queue.
        [[nodiscard]] std::uint64_t MoveEntry(MoveRequest request, PublishCallback publish = {}, FileOperationPrerequisites prerequisites = {});
        [[nodiscard]] std::uint64_t MoveEntries(std::vector<MoveRequest> requests, PublishCallback publish = {}, FileOperationPrerequisites prerequisites = {});
        [[nodiscard]] std::uint64_t ResumeMoveBatch(std::uint64_t expected_operation_id, FileCopyConflictPolicy policy,
            std::optional<std::wstring> new_name = {}, PublishCallback publish = {},
            std::optional<std::wstring> destination_parent = {}, FileOperationPrerequisites prerequisites = {});
        [[nodiscard]] bool HasPendingMoveBatch() const noexcept;
        [[nodiscard]] bool HasPendingBatch() const noexcept;
        [[nodiscard]] std::uint64_t DeleteEntries(std::vector<DeleteRequest> requests, PublishCallback publish = {},
            FileOperationPrerequisites prerequisites = {});
        [[nodiscard]] std::uint64_t ResumeDeleteBatch(std::uint64_t expected_operation_id, DeleteRetryAction action,
            PublishCallback publish = {}, FileOperationPrerequisites prerequisites = {}, std::optional<std::wstring> parent = {});
        [[nodiscard]] bool HasPendingDeleteBatch() const noexcept;
        [[nodiscard]] FileOperationSnapshot Snapshot() const;
        [[nodiscard]] bool IsBusy() const noexcept;
        [[nodiscard]] bool HasPendingCopyBatch() const noexcept;
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction CurrentAction() const;
        void Cancel() noexcept;
        void Shutdown() noexcept;
    private:
        using WorkerExecuteFunction = std::function<NewFolderResult(const NewFolderRequest&,
            const FileOperationCancellation&, const FileCopyProgressCallback&)>;
        [[nodiscard]] std::uint64_t Submit(NewFolderRequest request, PublishCallback publish, FileOperationKind kind,
            std::optional<blink::core::FileEntry> source, std::optional<FileCopyRequest> copy_request,
            WorkerExecuteFunction execute, std::optional<FileCopyBatchSnapshot> copy_batch = {},
            std::optional<std::uint64_t> expected_resume_id = {},
            std::optional<DirectoryCopyRequest> directory_copy_request = {}, std::optional<MoveRequest> move_request = {},
            std::optional<MoveBatchSnapshot> move_batch = {}, FileOperationPrerequisites prerequisites = {},
            std::optional<DeleteRequest> delete_request = {}, std::optional<DeleteBatchSnapshot> delete_batch = {});
        struct State;
        std::shared_ptr<State> state_;
        ExecuteFunction execute_;
        CopyExecuteFunction copy_execute_;
        DirectoryCopyExecuteFunction directory_copy_execute_;
        MoveExecuteFunction move_execute_;
        DeleteExecuteFunction delete_execute_;
        mutable std::mutex work_mutex_;
        winrt::Windows::Foundation::IAsyncAction work_{ nullptr };
    };
}
