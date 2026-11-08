#include "Blink/App/FileOperationCoordinator.h"
#include <winrt/Windows.System.Threading.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <string_view>
#include <utility>
#include <thread>
#include <objbase.h>

namespace blink::app
{
    namespace
    {
        constexpr DWORD OrdinaryAttributes = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
            FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL |
            FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
        bool Resolved(const FileCopyBatchItemStage stage) noexcept
        { return stage == FileCopyBatchItemStage::Copied || stage == FileCopyBatchItemStage::Skipped; }
        bool KnownPolicy(const FileCopyConflictPolicy policy) noexcept
        {
            switch (policy)
            {
            case FileCopyConflictPolicy::Fail: case FileCopyConflictPolicy::Skip:
            case FileCopyConflictPolicy::KeepBoth: case FileCopyConflictPolicy::Cancel:
            case FileCopyConflictPolicy::Replace: case FileCopyConflictPolicy::Merge: return true;
            default: return false;
            }
        }
        std::wstring LocalParent(std::wstring path)
        {
            if (path.starts_with(L"\\\\?\\")) { path.erase(0, 4); }
            if (path.size() < 3 || path.size() > 32760 || path[1] != L':' || path[2] != L'\\' ||
                !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
                path.find(L':', 2) != std::wstring::npos || path.find(L'/') != std::wstring::npos ||
                path.find(L'\0') != std::wstring::npos) { return {}; }
            while (path.size() > 3 && path.back() == L'\\') { path.pop_back(); }
            for (std::size_t offset = 3; offset < path.size();)
            {
                const auto end = path.find(L'\\', offset);
                const auto part = std::wstring_view(path).substr(offset,
                    end == std::wstring::npos ? path.size() - offset : end - offset);
                if (part.empty() || part == L"." || part == L"..") { return {}; }
                if (end == std::wstring::npos) { break; }
                offset = end + 1;
            }
            return path;
        }
        DWORD ValidateBatch(const FileCopyBatchSnapshot& batch, std::size_t& invalid_index)
        {
            invalid_index = 0;
            if (!batch.requested_count || batch.requested_count > MaximumFileCopyBatchItems ||
                batch.items.size() != batch.requested_count || batch.current_index >= batch.items.size())
            { return ERROR_INVALID_PARAMETER; }
            const auto& destination = batch.items[batch.current_index].request;
            const auto parent = LocalParent(destination.destination_parent);
            if (parent.empty()) { return ERROR_NOT_SUPPORTED; }
            for (std::size_t index = 0; index < batch.items.size(); ++index)
            {
                invalid_index = index;
                const auto& item = batch.items[index]; const auto& request = item.request;
                const bool directory = item.directory_limits.has_value();
                if (request.source.is_directory != directory ||
                    ((request.source.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory ||
                    (request.source.attributes & ~(OrdinaryAttributes | (directory ? FILE_ATTRIBUTE_DIRECTORY : 0))) != 0)
                { return ERROR_NOT_SUPPORTED; }
                if (directory)
                {
                    const auto& limits = *item.directory_limits;
                    if (limits.max_entries > MaximumDirectoryCopyEntries || !limits.max_directories || limits.max_directories > MaximumDirectoryCopyDirectories ||
                        limits.max_depth > MaximumDirectoryCopyDepth || limits.max_path_bytes > MaximumDirectoryCopyPathBytes)
                    { return ERROR_NOT_SUPPORTED; }
                }
                if (!request.source.identity.valid || !request.source_parent_identity.valid ||
                    !request.destination_parent_identity.valid ||
                    request.source.identity.volume_serial != request.source_parent_identity.volume_serial)
                { return ERROR_FILE_INVALID; }
                if (LocalParent(request.source_parent).empty() || LocalParent(request.destination_parent).empty())
                { return ERROR_NOT_SUPPORTED; }
                if (const auto error = ValidateNewFolderName(request.source.name); error != ERROR_SUCCESS) { return error; }
                if (const auto error = ValidateNewFolderName(request.destination_name); error != ERROR_SUCCESS) { return error; }
                if (!KnownPolicy(request.conflict_policy)) { return ERROR_INVALID_PARAMETER; }
                if (directory && request.conflict_policy == FileCopyConflictPolicy::Replace) { return ERROR_NOT_SUPPORTED; }
                if (!directory && request.conflict_policy == FileCopyConflictPolicy::Merge) { return ERROR_NOT_SUPPORTED; }
                // Completed entries keep their original paths after an explicit
                // same-identity destination-directory rename/rebind on resume.
                if (!Resolved(item.stage))
                {
                    const auto target = LocalParent(request.destination_parent);
                    if (request.destination_parent_identity != destination.destination_parent_identity ||
                        CompareStringOrdinal(parent.c_str(), -1, target.c_str(), -1, TRUE) != CSTR_EQUAL)
                    { return ERROR_INVALID_PARAMETER; }
                }
            }
            return ERROR_SUCCESS;
        }
        void NormalizeDirectoryResult(const DirectoryCopyRequest& request, DirectoryCopyResult& result) noexcept
        {
            if (result.copied) { result.error = ERROR_SUCCESS; result.skipped = false; }
            else if (result.skipped)
            {
                const bool valid = request.conflict_policy == FileCopyConflictPolicy::Skip && !result.error &&
                    !result.partial_tree_left && !result.directories_created && !result.files_copied;
                if (!valid)
                {
                    result.skipped = false;
                    result.partial_tree_left = result.partial_tree_left || result.directories_created || result.files_copied;
                    if (!result.error) { result.error = ERROR_GEN_FAILURE; }
                }
            }
            else if (!result.error) { result.error = ERROR_GEN_FAILURE; }
            if (!result.copied && (result.directories_created || result.files_copied)) { result.partial_tree_left = true; }
        }
        void DirectorySummary(std::optional<DirectoryCopyResult>& target, const DirectoryCopyResult& actual) noexcept
        {
            try { target = actual; return; } catch (...) {}
            // Summary allocation failure must not reverse a real commit or
            // discard the independently owned per-item filesystem facts.
            target.emplace(); auto& summary = *target;
            summary.error = actual.error; summary.copied = actual.copied; summary.skipped = actual.skipped;
            summary.partial_tree_left = actual.partial_tree_left;
            summary.files_copied = actual.files_copied; summary.directories_created = actual.directories_created;
            summary.files_skipped = actual.files_skipped; summary.directories_skipped = actual.directories_skipped;
            try { summary.path = actual.path; } catch (...) {}
            try { summary.failure_path = actual.failure_path; } catch (...) {}
        }
        bool AnyFilePartial(const FileCopyBatchSnapshot& batch) noexcept
        {
            return std::any_of(batch.items.begin(), batch.items.end(), [](const auto& item) {
                return item.result.partial_file_left || std::any_of(item.previous_results.begin(), item.previous_results.end(),
                    [](const auto& result) { return result.partial_file_left; });
            });
        }
        void NormalizeCopyResult(FileCopyResult& result) noexcept
        {
            if (result.copied) { result.error = ERROR_SUCCESS; result.skipped = false; }
            else if (result.skipped && (result.error != ERROR_SUCCESS || result.partial_file_left))
            { result.skipped = false; if (!result.error) { result.error = ERROR_GEN_FAILURE; } }
            else if (!result.skipped && !result.error) { result.error = ERROR_GEN_FAILURE; }
        }
        void NormalizeMoveResult(const MoveRequest* request, MoveResult& result) noexcept
        {
            if ((result.copy_result && (!result.copy_result->copied || !result.source_removed)) ||
                (result.directory_copy_result && (!result.directory_copy_result->copied || !result.source_removed)))
            {
                result.moved = false;
                if (MoveHasPartialDestination(result)) { result.skipped = result.unchanged = false; }
                if (!result.error && !result.skipped) { result.error = ERROR_PARTIAL_COPY; }
            }
            if (result.moved) { result.error = ERROR_SUCCESS; result.skipped = result.unchanged = false; }
            else if (result.skipped)
            {
                const bool valid = request && request->conflict_policy == FileCopyConflictPolicy::Skip && !result.error && !result.unchanged;
                if (!valid) { result.skipped = result.unchanged = false; if (!result.error) { result.error = ERROR_GEN_FAILURE; } }
            }
            else if (result.unchanged)
            {
                const bool valid = request && !result.error && request->source.identity.valid && request->source_parent_identity.valid &&
                    request->destination_parent_identity.valid && request->source_parent_identity == request->destination_parent_identity &&
                    !request->source.name.empty() && request->source.name == request->destination_name;
                if (!valid) { result.unchanged = false; if (!result.error) { result.error = ERROR_GEN_FAILURE; } }
            }
            else if (!result.error) { result.error = ERROR_GEN_FAILURE; }
        }
        void MoveSummary(std::optional<MoveResult>& target, const MoveResult& actual) noexcept
        {
            try { target = actual; return; } catch (...) {}
            target.emplace(); target->error = actual.error; target->moved = actual.moved;
            target->unchanged = actual.unchanged; target->skipped = actual.skipped;
            target->post_commit_error = actual.post_commit_error;
            target->source_removed = actual.source_removed;
            target->source_files_removed = actual.source_files_removed;
            target->source_directories_removed = actual.source_directories_removed;
            if (actual.directory_copy_result) { DirectorySummary(target->directory_copy_result, *actual.directory_copy_result); }
            if (actual.copy_result)
            {
                target->copy_result.emplace(); target->copy_result->error = actual.copy_result->error;
                target->copy_result->copied = actual.copy_result->copied;
                target->copy_result->partial_file_left = actual.copy_result->partial_file_left;
                target->copy_result->skipped = actual.copy_result->skipped;
                try { target->copy_result->path = actual.copy_result->path; } catch (...) {}
            }
            try { target->source_path = actual.source_path; target->path = actual.path; } catch (...) {}
        }
        bool MoveResolved(const MoveBatchItemStage stage) noexcept
        { return stage == MoveBatchItemStage::Moved || stage == MoveBatchItemStage::Unchanged || stage == MoveBatchItemStage::Skipped; }
        DWORD ValidateMoveBatch(const MoveBatchSnapshot& batch, std::size_t& invalid)
        {
            invalid = 0;
            if (!batch.requested_count || batch.requested_count > MaximumFileCopyBatchItems ||
                batch.items.size() != batch.requested_count || batch.current_index >= batch.items.size())
            { return ERROR_INVALID_PARAMETER; }
            const auto& destination = batch.items[batch.current_index].request;
            const auto parent = LocalParent(destination.destination_parent);
            if (parent.empty()) { return ERROR_NOT_SUPPORTED; }
            std::size_t characters{};
            for (std::size_t index = 0; index < batch.items.size(); ++index)
            {
                invalid = index; const auto& item = batch.items[index]; const auto& request = item.request;
                if (!request.source.identity.valid || !request.source_parent_identity.valid || !request.destination_parent_identity.valid ||
                    request.source.identity.volume_serial != request.source_parent_identity.volume_serial)
                { return ERROR_FILE_INVALID; }
                if (request.source.is_directory != ((request.source.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
                { return ERROR_FILE_INVALID; }
                if (request.source.attributes & ~(OrdinaryAttributes | (request.source.is_directory ? FILE_ATTRIBUTE_DIRECTORY : 0)))
                { return ERROR_NOT_SUPPORTED; }
                if (LocalParent(request.source_parent).empty() || LocalParent(request.destination_parent).empty()) { return ERROR_NOT_SUPPORTED; }
                if (const auto error = ValidateNewFolderName(request.source.name); error) { return error; }
                if (const auto error = ValidateNewFolderName(request.destination_name); error) { return error; }
                if (!KnownPolicy(request.conflict_policy)) { return ERROR_INVALID_PARAMETER; }
                if (request.source.is_directory && request.conflict_policy == FileCopyConflictPolicy::Replace) { return ERROR_NOT_SUPPORTED; }
                if (request.conflict_policy == FileCopyConflictPolicy::Merge) { return ERROR_NOT_SUPPORTED; }
                const auto size = request.source_parent.size() + request.source.name.size() + request.destination_parent.size() + request.destination_name.size() + 4;
                constexpr std::size_t maximum_characters = 4 * 1024 * 1024 / sizeof(wchar_t);
                if (size > maximum_characters - characters) { return ERROR_NOT_SUPPORTED; }
                characters += size;
                if (!MoveResolved(item.stage))
                {
                    if (request.destination_parent_identity != destination.destination_parent_identity ||
                        CompareStringOrdinal(parent.c_str(), -1, LocalParent(request.destination_parent).c_str(), -1, TRUE) != CSTR_EQUAL)
                    { return ERROR_INVALID_PARAMETER; }
                }
            }
            return ERROR_SUCCESS;
        }
    }

    struct FileOperationCoordinator::State : std::enable_shared_from_this<State>
    {
        mutable std::mutex mutex;
        std::condition_variable conflict_changed;
        std::optional<FileCopyConflictPolicy> conflict_decision;
        FileOperationSnapshot snapshot;
        FileOperationCancellation cancellation;
        std::chrono::steady_clock::time_point last_progress_publication{};
        bool progress_published{};
        std::wstring directory_root_path;

        std::optional<FileOperationSnapshot> Publication() const noexcept
        { try { return snapshot; } catch (...) { return {}; } }
        static void Recount(FileCopyBatchSnapshot& batch) noexcept
        {
            batch.copied_count = batch.skipped_count = batch.failed_count = batch.cancelled_count = 0;
            batch.remaining_count = batch.partial_count = 0;
            for (const auto& item : batch.items)
            {
                switch (item.stage)
                {
                case FileCopyBatchItemStage::Copied: ++batch.copied_count; break;
                case FileCopyBatchItemStage::Skipped: ++batch.skipped_count; break;
                case FileCopyBatchItemStage::Failed: ++batch.failed_count; break;
                case FileCopyBatchItemStage::Cancelled: ++batch.cancelled_count; break;
                default: ++batch.remaining_count; break;
                }
                if (item.directory_partial_attempts || (item.directory_result && item.directory_result->partial_tree_left) ||
                    item.result.partial_file_left || std::any_of(item.previous_results.begin(), item.previous_results.end(),
                    [](const FileCopyResult& result) { return result.partial_file_left; })) { ++batch.partial_count; }
            }
        }
        bool ProtectsPending() const noexcept
        { return (snapshot.copy_batch && snapshot.copy_batch->resumable && snapshot.copy_batch->remaining_count != 0) ||
            (snapshot.move_batch && snapshot.move_batch->resumable) || (snapshot.delete_batch && snapshot.delete_batch->resumable); }

        static DWORD DeleteErrorCode(const HRESULT error) noexcept
        { return SUCCEEDED(error) ? ERROR_SUCCESS : DeleteCancelled(error) ? ERROR_CANCELLED :
            HRESULT_FACILITY(error) == FACILITY_WIN32 ? HRESULT_CODE(error) : ERROR_GEN_FAILURE; }
        static bool DeleteResolved(const DeleteItemStage stage) noexcept
        { return stage == DeleteItemStage::Removed || stage == DeleteItemStage::Skipped; }
        static void RecountDeletes(DeleteBatchSnapshot& batch) noexcept
        {
            batch.removed_count = batch.recycled_count = batch.permanent_count = batch.skipped_count = batch.failed_count =
                batch.cancelled_count = batch.remaining_count = batch.partial_count = batch.warning_count = 0;
            for (const auto& item : batch.items)
            {
                if (item.stage == DeleteItemStage::Removed)
                {
                    ++batch.removed_count;
                    if (item.result.recycled) { ++batch.recycled_count; } else { ++batch.permanent_count; }
                    if (FAILED(item.result.post_commit_error)) { ++batch.warning_count; }
                }
                else if (item.stage == DeleteItemStage::Skipped) { ++batch.skipped_count; }
                else if (item.stage == DeleteItemStage::Failed) { ++batch.failed_count; }
                else if (item.stage == DeleteItemStage::Cancelled) { ++batch.cancelled_count; }
                else { ++batch.remaining_count; }
                if ((!item.result.removed && item.result.possible_partial) ||
                    (item.retained_result && !item.retained_result->removed && item.retained_result->possible_partial)) { ++batch.partial_count; }
            }
        }
        void SelectDeleteRequest(const std::size_t index)
        {
            auto& batch = *snapshot.delete_batch; batch.current_index = index;
            const auto& request = batch.items[index].request;
            snapshot.delete_request = request;
            snapshot.request = { request.parent, request.parent_identity, request.source.name };
        }
        void DeleteTop(const DeleteResult& actual) noexcept
        {
            try { snapshot.delete_result = actual; }
            catch (...)
            {
                snapshot.delete_result.emplace(); auto& result = *snapshot.delete_result;
                result.error = actual.error; result.removed = actual.removed; result.recycled = actual.recycled;
                result.skipped = actual.skipped; result.possible_partial = actual.possible_partial;
                result.observed_removed = actual.observed_removed; result.post_commit_error = actual.post_commit_error;
                try { result.path = actual.path; result.failure_path = actual.failure_path; result.recycle_item = actual.recycle_item; } catch (...) {}
            }
            snapshot.result = {}; snapshot.result.error = DeleteErrorCode(actual.error);
            snapshot.result.committed = actual.removed; snapshot.result.skipped = actual.skipped;
            try { snapshot.result.path = actual.path; } catch (...) {}
        }
        void DeleteException(const std::uint64_t id, const HRESULT error, const PublishCallback& publish,
            const std::optional<std::size_t> invalid = {}) noexcept
        {
            try
            {
                std::optional<FileOperationSnapshot> publication;
                {
                    std::scoped_lock lock(mutex);
                    if (id != snapshot.operation_id || !snapshot.delete_batch) { return; }
                    auto& batch = *snapshot.delete_batch; batch.resumable = false;
                    const auto index = invalid.value_or(batch.current_index);
                    if (index < batch.items.size() && !DeleteResolved(batch.items[index].stage))
                    {
                        auto& item = batch.items[index]; item.result.error = error; item.stage = DeleteItemStage::Failed;
                        try { SelectDeleteRequest(index); } catch (...) { batch.current_index = index; }
                        DeleteTop(item.result);
                    }
                    else { snapshot.result.error = DeleteErrorCode(error); }
                    snapshot.stage = FileOperationStage::Failed; RecountDeletes(batch);
                    publication = snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
                }
                Publish(publication, publish);
            }
            catch (...) {}
        }
        std::optional<FileOperationSnapshot> DeleteProgressUpdate(const std::uint64_t id, const std::size_t index, const DeleteProgress& progress)
        {
            std::scoped_lock lock(mutex);
            if (id != snapshot.operation_id || snapshot.closed || snapshot.kind != FileOperationKind::Delete || !snapshot.delete_batch ||
                index != snapshot.delete_batch->current_index || index >= snapshot.delete_batch->items.size() ||
                snapshot.delete_batch->items[index].stage != DeleteItemStage::Running ||
                (snapshot.stage != FileOperationStage::Running && snapshot.stage != FileOperationStage::Cancelling) || progress.path.size() > 32767)
            { return {}; }
            snapshot.delete_progress = progress;
            const auto now = std::chrono::steady_clock::now();
            if (progress_published && now - last_progress_publication < std::chrono::milliseconds(100)) { return {}; }
            progress_published = true; last_progress_publication = now;
            return Publication();
        }
        void CancelDeleteBatchUnlocked() noexcept
        {
            auto& batch = *snapshot.delete_batch; batch.resumable = false;
            snapshot.cancel_requested = true; snapshot.stage = FileOperationStage::Cancelled;
            snapshot.result.error = ERROR_CANCELLED;
            if (batch.current_index < batch.items.size())
            {
                auto& item = batch.items[batch.current_index];
                if (item.stage == DeleteItemStage::Pending || item.stage == DeleteItemStage::Running)
                { item.stage = DeleteItemStage::Cancelled; item.result.error = HRESULT_FROM_WIN32(ERROR_CANCELLED); DeleteTop(item.result); }
            }
            RecountDeletes(batch);
        }
        void RunDeleteItems(const std::uint64_t id, const FileOperationCancellation& cancelled,
            const DeleteExecuteFunction& execute, const PublishCallback& publish) noexcept
        {
            try
            {
                std::vector<DeleteRequest> requests; std::size_t index{}, invalid{};
                {
                    std::scoped_lock lock(mutex);
                    if (id != snapshot.operation_id || !snapshot.delete_batch) { return; }
                    index = snapshot.delete_batch->current_index;
                    for (const auto& item : snapshot.delete_batch->items) { requests.push_back(item.request); }
                }
                if (const auto error = ValidateDeleteRequests(requests, invalid); FAILED(error))
                { DeleteException(id, error, publish, invalid); return; }
                for (; index < requests.size(); ++index)
                {
                    DeleteRequest request; std::optional<FileOperationSnapshot> publication; bool stop{};
                    {
                        std::scoped_lock lock(mutex);
                        if (id != snapshot.operation_id || !snapshot.delete_batch) { return; }
                        SelectDeleteRequest(index); request = *snapshot.delete_request;
                        stop = snapshot.closed || cancelled->load() || snapshot.cancel_requested;
                        if (stop) { CancelDeleteBatchUnlocked(); }
                        else
                        {
                            snapshot.stage = FileOperationStage::Running; snapshot.delete_progress = {}; snapshot.delete_result.reset(); snapshot.result = {};
                            snapshot.delete_batch->items[index].stage = DeleteItemStage::Running; progress_published = false;
                        }
                        publication = snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
                    }
                    Publish(publication, publish); if (stop) { return; }
                    DeleteResult result;
                    try
                    {
                        if (request.skip)
                        { result.error = S_OK; result.skipped = true; result.path = blink::core::JoinPath(request.parent, request.source.name); }
                        else
                        {
                            const auto state = shared_from_this();
                            result = execute(request, cancelled, [state, id, index, publish](const DeleteProgress& value) noexcept {
                                try { state->Publish(state->DeleteProgressUpdate(id, index, value), publish); } catch (...) {}
                            });
                        }
                    }
                    catch (const std::bad_alloc&) { result.error = E_OUTOFMEMORY; }
                    catch (...) { result.error = E_FAIL; }
                    bool more{};
                    {
                        std::scoped_lock lock(mutex);
                        if (id != snapshot.operation_id || !snapshot.delete_batch) { return; }
                        auto& batch = *snapshot.delete_batch; auto& item = batch.items[index];
                        if (result.removed)
                        {
                            result.error = S_OK; result.skipped = result.possible_partial = false;
                            if ((request.mode == DeleteMode::Recycle) != result.recycled && SUCCEEDED(result.post_commit_error))
                            { result.post_commit_error = E_UNEXPECTED; }
                        }
                        else
                        {
                            result.possible_partial = result.possible_partial || item.previous_possible_partial || result.observed_removed != 0;
                            if (result.skipped && (!request.skip || FAILED(result.error) || result.possible_partial)) { result.skipped = false; }
                            if (!result.skipped && SUCCEEDED(result.error)) { result.error = E_FAIL; }
                        }
                        result.observed_removed += item.previous_observed_removed;
                        item.result = std::move(result);
                        item.stage = item.result.removed ? DeleteItemStage::Removed : item.result.skipped ? DeleteItemStage::Skipped :
                            DeleteCancelled(item.result.error) ? DeleteItemStage::Cancelled : DeleteItemStage::Failed;
                        DeleteTop(item.result); RecountDeletes(batch); batch.resumable = false;
                        more = index + 1 < requests.size();
                        if (!DeleteResolved(item.stage))
                        {
                            snapshot.stage = item.stage == DeleteItemStage::Cancelled ? FileOperationStage::Cancelled : FileOperationStage::Failed;
                            batch.resumable = item.stage == DeleteItemStage::Failed && !snapshot.closed && !snapshot.cancel_requested && !cancelled->load();
                            more = false;
                        }
                        else { snapshot.stage = more ? FileOperationStage::Running : FileOperationStage::Completed; }
                        publication = snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
                    }
                    Publish(publication, publish); if (!more) { return; }
                }
            }
            catch (const std::bad_alloc&) { DeleteException(id, E_OUTOFMEMORY, publish); }
            catch (...) { DeleteException(id, E_FAIL, publish); }
        }
        void RunDeleteBatch(const std::uint64_t id, const FileOperationCancellation& cancelled,
            const DeleteExecuteFunction& execute, const PublishCallback& publish) noexcept
        {
            try
            {
                // Shell IFileOperation requires STA. The existing owned action
                // waits for this temporary thread; there is no second queue.
                const auto state = shared_from_this();
                std::jthread sta([state, id, cancelled, execute, publish] {
                    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                    if (FAILED(initialized)) { state->DeleteException(id, initialized, publish); return; }
                    state->RunDeleteItems(id, cancelled, execute, publish);
                    CoUninitialize();
                });
            }
            catch (const std::bad_alloc&) { DeleteException(id, E_OUTOFMEMORY, publish); }
            catch (...) { DeleteException(id, E_FAIL, publish); }
        }
        void SelectRequest(const std::size_t index)
        {
            auto& batch = *snapshot.copy_batch; batch.current_index = index;
            if (index >= batch.items.size()) { return; }
            const auto& request = batch.items[index].request;
            snapshot.request = { request.destination_parent, request.destination_parent_identity, request.destination_name };
            snapshot.copy_request = request;
            const auto& item = batch.items[index];
            snapshot.directory_copy_request = item.directory_limits ?
                std::optional<DirectoryCopyRequest>{ DirectoryCopyProjection(request, *item.directory_limits) } : std::nullopt;
            if (item.directory_result) { DirectorySummary(snapshot.directory_copy_result, *item.directory_result); }
            else { snapshot.directory_copy_result.reset(); }
        }
        void CopyTopResult(const FileCopyResult& result) noexcept
        {
            snapshot.result.error = result.error; snapshot.result.committed = result.copied;
            snapshot.result.partial_file_left = result.partial_file_left; snapshot.result.skipped = result.skipped;
            try { snapshot.result.path = result.path; } catch (...) { snapshot.result.path.clear(); }
        }
        void CancelBatchUnlocked(const std::size_t next) noexcept
        {
            auto& batch = *snapshot.copy_batch;
            try { SelectRequest(next); } catch (...) { batch.current_index = next; }
            batch.resumable = false;
            snapshot.stage = FileOperationStage::Cancelled; snapshot.cancel_requested = true;
            snapshot.result.error = ERROR_CANCELLED; snapshot.result.committed = false; snapshot.result.skipped = false;
            snapshot.result.path.clear(); snapshot.copy_progress = {}; snapshot.directory_copy_progress = {};
            Recount(batch); snapshot.result.partial_file_left = AnyFilePartial(batch);
        }
        std::optional<FileOperationSnapshot> RejectBatch(const std::uint64_t id, const DWORD error,
            const std::size_t index, const bool resumable = false)
        {
            std::scoped_lock lock(mutex);
            if (id != snapshot.operation_id || !snapshot.copy_batch) { return {}; }
            auto& batch = *snapshot.copy_batch;
            try { SelectRequest(index); } catch (...) { batch.current_index = index; }
            batch.resumable = resumable && !snapshot.closed && !snapshot.cancel_requested && index < batch.items.size();
            snapshot.result = { error };
            Recount(batch); snapshot.result.partial_file_left = AnyFilePartial(batch);
            snapshot.stage = FileOperationStage::Failed;
            return snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
        }
        std::optional<FileOperationSnapshot> Progress(const std::uint64_t id, const std::size_t index,
            const FileCopyProgress& value)
        {
            std::scoped_lock lock(mutex);
            if (id != snapshot.operation_id || snapshot.closed || !snapshot.copy_batch ||
                index != snapshot.copy_batch->current_index || index >= snapshot.copy_batch->items.size() ||
                snapshot.copy_batch->items[index].stage != FileCopyBatchItemStage::Running ||
                (snapshot.stage != FileOperationStage::Running && snapshot.stage != FileOperationStage::Cancelling)) { return {}; }
            const auto total = (std::max)(snapshot.copy_progress.total_bytes, value.total_bytes);
            const auto transferred = (std::min)(total, (std::max)(snapshot.copy_progress.transferred_bytes, value.transferred_bytes));
            if (total == snapshot.copy_progress.total_bytes && transferred == snapshot.copy_progress.transferred_bytes) { return {}; }
            snapshot.copy_progress = { total, transferred };
            const auto now = std::chrono::steady_clock::now();
            if (progress_published && now - last_progress_publication < std::chrono::milliseconds(100)) { return {}; }
            progress_published = true; last_progress_publication = now;
            return Publication();
        }
        std::optional<FileOperationSnapshot> DirectoryProgress(const std::uint64_t id, const DirectoryCopyProgress& value,
            const std::size_t index = static_cast<std::size_t>(-1))
        {
            std::scoped_lock lock(mutex);
            const bool batch = snapshot.kind == FileOperationKind::Copy && snapshot.copy_batch;
            if (id != snapshot.operation_id || snapshot.closed ||
                (!batch && snapshot.kind != FileOperationKind::DirectoryCopy) || !snapshot.directory_copy_request ||
                (snapshot.stage != FileOperationStage::Running && snapshot.stage != FileOperationStage::Cancelling)) { return {}; }
            if (batch && (index != snapshot.copy_batch->current_index || index >= snapshot.copy_batch->items.size() ||
                !snapshot.copy_batch->items[index].directory_limits ||
                snapshot.copy_batch->items[index].stage != FileCopyBatchItemStage::Running)) { return {}; }
            const auto& limits = snapshot.directory_copy_request->limits;
            const auto& previous = snapshot.directory_copy_progress;
            const bool merging = snapshot.directory_copy_request->conflict_policy == FileCopyConflictPolicy::Merge;
            const auto maximum_entries = (std::min)(limits.max_entries, MaximumDirectoryCopyEntries);
            if (value.files_copied > maximum_entries ||
                value.files_skipped > maximum_entries || value.directories_skipped > maximum_entries ||
                value.directories_created > (std::min)(limits.max_directories, MaximumDirectoryCopyDirectories) ||
                (value.files_copied && !value.directories_created && !merging) ||
                (value.directories_created && (value.directories_created - 1 > maximum_entries ||
                    value.files_copied > maximum_entries - (value.directories_created - 1))) ||
                value.current_file.transferred_bytes > value.current_file.total_bytes ||
                (value.completed && !value.directories_created && !merging) ||
                value.files_copied < previous.files_copied || value.directories_created < previous.directories_created ||
                value.files_skipped < previous.files_skipped || value.directories_skipped < previous.directories_skipped ||
                previous.completed || value.current_source_path.size() > 32767 || value.current_destination_path.size() > 32767)
            { return {}; }
            auto accepted = value;
            // Bytes belong to the named current file, not to the whole tree.
            if (accepted.current_source_path == previous.current_source_path &&
                accepted.current_destination_path == previous.current_destination_path)
            {
                accepted.current_file.total_bytes = (std::max)(previous.current_file.total_bytes, accepted.current_file.total_bytes);
                accepted.current_file.transferred_bytes = (std::max)(previous.current_file.transferred_bytes, accepted.current_file.transferred_bytes);
            }
            if (accepted.files_copied == previous.files_copied && accepted.directories_created == previous.directories_created &&
                accepted.files_skipped == previous.files_skipped && accepted.directories_skipped == previous.directories_skipped &&
                accepted.current_source_path == previous.current_source_path && accepted.current_destination_path == previous.current_destination_path &&
                accepted.current_file.total_bytes == previous.current_file.total_bytes &&
                accepted.current_file.transferred_bytes == previous.current_file.transferred_bytes && accepted.completed == previous.completed)
            { return {}; }
            if (batch && directory_root_path.empty() && accepted.directories_created == 1 &&
                previous.directories_created == 0) { directory_root_path = accepted.current_destination_path; }
            snapshot.directory_copy_progress = std::move(accepted);
            const auto now = std::chrono::steady_clock::now();
            if (progress_published && now - last_progress_publication < std::chrono::milliseconds(100)) { return {}; }
            progress_published = true; last_progress_publication = now;
            return Publication();
        }
        void Publish(const std::optional<FileOperationSnapshot>& value, const PublishCallback& publish) noexcept
        {
            if (!value || !publish) { return; }
            try
            {
                { std::scoped_lock lock(mutex); if (snapshot.closed || snapshot.operation_id != value->operation_id) { return; } }
                publish(*value);
            }
            catch (...) {}
        }
        FileCopyConflictPolicy AwaitDirectoryConflict(const std::uint64_t id, const DirectoryCopyConflict& conflict,
            const PublishCallback& publish)
        {
            if (!publish) { return FileCopyConflictPolicy::Fail; }
            std::optional<FileOperationSnapshot> publication;
            std::uint64_t conflict_id{};
            {
                std::scoped_lock lock(mutex);
                if (snapshot.operation_id != id || snapshot.closed || snapshot.cancel_requested ||
                    !cancellation || cancellation->load() || snapshot.stage != FileOperationStage::Running)
                { return FileCopyConflictPolicy::Cancel; }
                snapshot.directory_conflict = conflict;
                conflict_id = ++snapshot.directory_conflict_id;
                conflict_decision.reset();
                publication = Publication();
            }
            if (!publication)
            {
                std::scoped_lock lock(mutex); snapshot.directory_conflict.reset();
                return FileCopyConflictPolicy::Cancel;
            }
            try { publish(std::move(*publication)); }
            catch (...)
            {
                std::scoped_lock lock(mutex); snapshot.directory_conflict.reset();
                return FileCopyConflictPolicy::Cancel;
            }
            std::unique_lock lock(mutex);
            conflict_changed.wait(lock, [&] {
                return snapshot.operation_id != id || snapshot.closed || snapshot.cancel_requested ||
                    cancellation->load() || conflict_decision.has_value();
            });
            const auto selected = snapshot.operation_id == id && !snapshot.closed && !snapshot.cancel_requested &&
                !cancellation->load() && conflict_decision ? *conflict_decision : FileCopyConflictPolicy::Cancel;
            if (snapshot.operation_id == id && snapshot.directory_conflict_id == conflict_id)
            { snapshot.directory_conflict.reset(); conflict_decision.reset(); }
            return selected;
        }
        std::optional<FileOperationSnapshot> Complete(const std::uint64_t id, NewFolderResult result)
        {
            std::scoped_lock lock(mutex);
            if (id != snapshot.operation_id) { return {}; }
            if (result.committed) { result.error = ERROR_SUCCESS; result.skipped = false; }
            else if (result.skipped) { result.skipped = false; if (!result.error) { result.error = ERROR_GEN_FAILURE; } }
            else if (!result.error && snapshot.kind != FileOperationKind::Rename) { result.error = ERROR_GEN_FAILURE; }
            snapshot.stage = !result.error ? FileOperationStage::Completed :
                result.error == ERROR_CANCELLED ? FileOperationStage::Cancelled : FileOperationStage::Failed;
            snapshot.result = std::move(result);
            return snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
        }
        std::optional<FileOperationSnapshot> CompleteDirectory(const std::uint64_t id, DirectoryCopyResult result)
        {
            std::scoped_lock lock(mutex);
            if (id != snapshot.operation_id || snapshot.kind != FileOperationKind::DirectoryCopy) { return {}; }
            if (!snapshot.directory_copy_request) { return {}; }
            NormalizeDirectoryResult(*snapshot.directory_copy_request, result);
            snapshot.directory_copy_result = std::move(result);
            const auto& actual = *snapshot.directory_copy_result;
            snapshot.stage = actual.copied ? FileOperationStage::Completed :
                actual.skipped ? FileOperationStage::Skipped :
                actual.error == ERROR_CANCELLED ? FileOperationStage::Cancelled : FileOperationStage::Failed;
            snapshot.result = {};
            snapshot.result.error = actual.error; snapshot.result.committed = actual.copied;
            snapshot.result.skipped = actual.skipped;
            // A partial tree is represented only by the independent directory
            // result; it never means a partial file in the generic projection.
            try { snapshot.result.path = actual.path; } catch (...) {}
            snapshot.directory_copy_progress.files_copied = actual.files_copied;
            snapshot.directory_copy_progress.directories_created = actual.directories_created;
            snapshot.directory_copy_progress.files_skipped = actual.files_skipped;
            snapshot.directory_copy_progress.directories_skipped = actual.directories_skipped;
            snapshot.directory_copy_progress.completed = actual.copied;
            return snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
        }
        std::optional<FileOperationSnapshot> MoveProgressUpdate(const std::uint64_t id, const MoveProgress& value,
            const std::size_t index = static_cast<std::size_t>(-1))
        {
            std::scoped_lock lock(mutex);
            if (id != snapshot.operation_id || snapshot.closed || snapshot.kind != FileOperationKind::Move ||
                !snapshot.move_request || (snapshot.stage != FileOperationStage::Running && snapshot.stage != FileOperationStage::Cancelling))
            { return {}; }
            if (snapshot.move_batch && (index != snapshot.move_batch->current_index || index >= snapshot.move_batch->items.size() ||
                snapshot.move_batch->items[index].stage != MoveBatchItemStage::Running)) { return {}; }
            if (value.phase != MovePhase::Preparing && value.phase != MovePhase::Copying && value.phase != MovePhase::Committing && value.phase != MovePhase::Completed)
            { return {}; }
            const auto& previous = snapshot.move_progress;
            if (previous.phase == MovePhase::Completed || value.phase < previous.phase ||
                value.source_path.size() > 32767 || value.destination_path.size() > 32767 ||
                (value.phase == previous.phase && value.source_path == previous.source_path && value.destination_path == previous.destination_path &&
                    value.copy_progress.total_bytes == previous.copy_progress.total_bytes &&
                    value.copy_progress.transferred_bytes == previous.copy_progress.transferred_bytes &&
                    value.files_copied == previous.files_copied && value.directories_created == previous.directories_created &&
                    value.source_files_removed == previous.source_files_removed &&
                    value.source_directories_removed == previous.source_directories_removed && value.directory == previous.directory))
            { return {}; }
            snapshot.move_progress = value;
            const auto now = std::chrono::steady_clock::now();
            if (progress_published && now - last_progress_publication < std::chrono::milliseconds(100)) { return {}; }
            progress_published = true; last_progress_publication = now;
            return Publication();
        }
        std::optional<FileOperationSnapshot> CompleteMove(const std::uint64_t id, MoveResult result)
        {
            std::scoped_lock lock(mutex);
            if (id != snapshot.operation_id || snapshot.kind != FileOperationKind::Move) { return {}; }
            NormalizeMoveResult(snapshot.move_request ? &*snapshot.move_request : nullptr, result);
            snapshot.move_result = std::move(result);
            const auto& actual = *snapshot.move_result;
            snapshot.stage = actual.moved || actual.unchanged ? FileOperationStage::Completed :
                actual.skipped ? FileOperationStage::Skipped : actual.error == ERROR_CANCELLED ? FileOperationStage::Cancelled : FileOperationStage::Failed;
            snapshot.result = {}; snapshot.result.error = actual.error;
            snapshot.result.committed = actual.moved; snapshot.result.skipped = actual.skipped;
            try { snapshot.result.path = actual.path; } catch (...) {}
            // A completed progress hint never supplies a missing native outcome.
            // Postcommit warning stays on move_result; moved remains committed.
            if (actual.moved || actual.unchanged) { snapshot.move_progress.phase = MovePhase::Completed; }
            else if (snapshot.move_progress.phase == MovePhase::Completed) { snapshot.move_progress.phase = MovePhase::Committing; }
            return snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
        }
        static void RecountMoves(MoveBatchSnapshot& batch) noexcept
        {
            batch.moved_count = batch.unchanged_count = batch.skipped_count = batch.failed_count = 0;
            batch.cancelled_count = batch.remaining_count = batch.warning_count = 0;
            batch.partial_count = 0;
            for (const auto& item : batch.items)
            {
                switch (item.stage)
                {
                case MoveBatchItemStage::Moved: ++batch.moved_count; break;
                case MoveBatchItemStage::Unchanged: ++batch.unchanged_count; break;
                case MoveBatchItemStage::Skipped: ++batch.skipped_count; break;
                case MoveBatchItemStage::Failed: ++batch.failed_count; break;
                case MoveBatchItemStage::Cancelled: ++batch.cancelled_count; break;
                default: ++batch.remaining_count; break;
                }
                if (item.result.moved && item.result.post_commit_error) { ++batch.warning_count; }
                if (MoveHasPartialDestination(item.result) || (item.retained_result && MoveHasPartialDestination(*item.retained_result)))
                { ++batch.partial_count; }
            }
        }
        void SelectMoveRequest(const std::size_t index)
        {
            auto& batch = *snapshot.move_batch; batch.current_index = index;
            if (index >= batch.items.size()) { return; }
            snapshot.move_request = batch.items[index].request;
            const auto& request = *snapshot.move_request;
            snapshot.request = { request.destination_parent, request.destination_parent_identity, request.destination_name };
        }
        void MoveTopResult(const MoveResult& result) noexcept
        {
            MoveSummary(snapshot.move_result, result);
            snapshot.result = {}; snapshot.result.error = result.error; snapshot.result.committed = result.moved;
            snapshot.result.skipped = result.skipped;
            try { snapshot.result.path = result.path; } catch (...) {}
            if (result.moved || result.unchanged) { snapshot.move_progress.phase = MovePhase::Completed; }
            else if (snapshot.move_progress.phase == MovePhase::Completed) { snapshot.move_progress.phase = MovePhase::Committing; }
        }
        void CancelMoveBatchUnlocked(const std::size_t index) noexcept
        {
            auto& batch = *snapshot.move_batch;
            try { SelectMoveRequest(index); } catch (...) { batch.current_index = index; }
            batch.resumable = false; snapshot.cancel_requested = true; snapshot.stage = FileOperationStage::Cancelled;
            snapshot.result = { ERROR_CANCELLED }; snapshot.move_result.reset(); snapshot.move_progress = {};
            RecountMoves(batch);
        }
        void MoveBatchException(const std::uint64_t id, const std::size_t index, const DWORD error,
            const PublishCallback& publish, const bool resumable = true) noexcept
        {
            try
            {
                std::optional<FileOperationSnapshot> publication;
                {
                    std::scoped_lock lock(mutex);
                    if (id != snapshot.operation_id || !snapshot.move_batch) { return; }
                    auto& batch = *snapshot.move_batch;
                    if (index < batch.items.size() && !MoveResolved(batch.items[index].stage))
                    {
                        auto& item = batch.items[index]; item.result = { error }; item.stage = MoveBatchItemStage::Failed;
                        try { SelectMoveRequest(index); } catch (...) { batch.current_index = index; }
                        MoveTopResult(item.result);
                    }
                    else { snapshot.result = { error }; }
                    RecountMoves(batch); snapshot.stage = FileOperationStage::Failed;
                    batch.resumable = resumable && index < batch.items.size() && !MoveResolved(batch.items[index].stage) &&
                        !snapshot.closed && !snapshot.cancel_requested && !cancellation->load();
                    publication = snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
                }
                Publish(publication, publish);
            }
            catch (...) {}
        }
        void StopMoveBatch(const std::uint64_t id, const std::size_t index, const PublishCallback& publish)
        {
            std::optional<FileOperationSnapshot> publication;
            {
                std::scoped_lock lock(mutex);
                if (id != snapshot.operation_id || !snapshot.move_batch) { return; }
                CancelMoveBatchUnlocked(index); publication = snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
            }
            Publish(publication, publish);
        }
        void RunMoveBatch(const std::uint64_t id, const FileOperationCancellation& cancelled,
            const MoveExecuteFunction& execute, const PublishCallback& publish) noexcept
        {
            std::size_t index{};
            try
            {
                DWORD error{};
                {
                    std::scoped_lock lock(mutex);
                    if (id != snapshot.operation_id || !snapshot.move_batch) { return; }
                    index = snapshot.move_batch->current_index;
                    std::size_t invalid{}; error = ValidateMoveBatch(*snapshot.move_batch, invalid);
                    if (error) { index = invalid; }
                }
                if (error) { MoveBatchException(id, index, error, publish, false); return; }
                for (;; ++index)
                {
                    MoveRequest request; std::optional<FileOperationSnapshot> publication;
                    bool stop{};
                    {
                        std::scoped_lock lock(mutex);
                        if (id != snapshot.operation_id || !snapshot.move_batch || index >= snapshot.move_batch->items.size()) { return; }
                        SelectMoveRequest(index); request = *snapshot.move_request;
                        stop = snapshot.closed || cancelled->load() || snapshot.cancel_requested || request.conflict_policy == FileCopyConflictPolicy::Cancel;
                        if (!stop)
                        {
                            snapshot.stage = FileOperationStage::Running; snapshot.move_progress = {}; snapshot.move_result.reset(); snapshot.result = {};
                            snapshot.move_batch->items[index].stage = MoveBatchItemStage::Running;
                            progress_published = false; RecountMoves(*snapshot.move_batch); publication = Publication();
                        }
                    }
                    if (stop) { StopMoveBatch(id, index, publish); return; }
                    Publish(publication, publish);
                    const auto state = shared_from_this();
                    const MoveProgressCallback progress = [state, id, index, publish](const MoveProgress& value) noexcept {
                        try { state->Publish(state->MoveProgressUpdate(id, value, index), publish); } catch (...) {}
                    };
                    auto result = cancelled->load() ? MoveResult{ ERROR_CANCELLED } : execute(request, cancelled, progress);
                    NormalizeMoveResult(&request, result);
                    bool more{};
                    {
                        std::scoped_lock lock(mutex);
                        if (id != snapshot.operation_id || !snapshot.move_batch) { return; }
                        auto& batch = *snapshot.move_batch; auto& item = batch.items[index];
                        item.result = std::move(result);
                        item.stage = item.result.moved ? MoveBatchItemStage::Moved : item.result.unchanged ? MoveBatchItemStage::Unchanged :
                            item.result.skipped ? MoveBatchItemStage::Skipped : item.result.error == ERROR_CANCELLED ?
                                MoveBatchItemStage::Cancelled : MoveBatchItemStage::Failed;
                        MoveTopResult(item.result); RecountMoves(batch); batch.resumable = false;
                        if (!MoveResolved(item.stage))
                        {
                            snapshot.stage = item.stage == MoveBatchItemStage::Cancelled ? FileOperationStage::Cancelled : FileOperationStage::Failed;
                            batch.resumable = item.stage == MoveBatchItemStage::Failed && !snapshot.closed && !snapshot.cancel_requested && !cancelled->load();
                        }
                        else if (index + 1 == batch.items.size())
                        {
                            batch.current_index = batch.items.size();
                            snapshot.stage = batch.moved_count || batch.unchanged_count ? FileOperationStage::Completed : FileOperationStage::Skipped;
                        }
                        else
                        {
                            more = true;
                            snapshot.stage = cancelled->load() ? FileOperationStage::Cancelling : FileOperationStage::Running;
                        }
                        publication = snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
                    }
                    Publish(publication, publish);
                    if (!more) { return; }
                }
            }
            catch (const std::bad_alloc&) { MoveBatchException(id, index, ERROR_NOT_ENOUGH_MEMORY, publish); }
            catch (...) { MoveBatchException(id, index, ERROR_GEN_FAILURE, publish); }
        }
        void MoveException(const std::uint64_t id, const DWORD error, const PublishCallback& publish) noexcept
        {
            try
            {
                MoveResult result; result.error = error;
                Publish(CompleteMove(id, std::move(result)), publish);
            }
            catch (...) {}
        }
        void RunMove(const std::uint64_t id, const FileOperationCancellation& cancelled,
            const MoveExecuteFunction& execute, const PublishCallback& publish) noexcept
        {
            try
            {
                MoveRequest request; bool valid{}; std::optional<FileOperationSnapshot> publication;
                {
                    std::scoped_lock lock(mutex);
                    if (id != snapshot.operation_id || snapshot.kind != FileOperationKind::Move) { return; }
                    valid = snapshot.move_request.has_value();
                    if (valid) { request = *snapshot.move_request; }
                    if (!cancelled->load()) { snapshot.stage = FileOperationStage::Running; }
                    publication = snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
                }
                Publish(publication, publish);
                if (!valid) { MoveException(id, ERROR_INVALID_PARAMETER, publish); return; }
                const auto state = shared_from_this();
                const MoveProgressCallback progress = [state, id, publish](const MoveProgress& value) noexcept {
                    try { state->Publish(state->MoveProgressUpdate(id, value), publish); } catch (...) {}
                };
                auto result = cancelled->load() ? MoveResult{ ERROR_CANCELLED } : execute(request, cancelled, progress);
                Publish(CompleteMove(id, std::move(result)), publish);
            }
            catch (const std::bad_alloc&) { MoveException(id, ERROR_NOT_ENOUGH_MEMORY, publish); }
            catch (...) { MoveException(id, ERROR_GEN_FAILURE, publish); }
        }
        void DirectoryException(const std::uint64_t id, const DWORD error, const PublishCallback& publish) noexcept
        {
            try
            {
                DirectoryCopyResult result; result.error = error;
                {
                    std::scoped_lock lock(mutex);
                    if (id != snapshot.operation_id || snapshot.kind != FileOperationKind::DirectoryCopy) { return; }
                    const auto& observed = snapshot.directory_copy_progress;
                    result.files_copied = observed.files_copied; result.directories_created = observed.directories_created;
                    result.files_skipped = observed.files_skipped; result.directories_skipped = observed.directories_skipped;
                    result.partial_tree_left = observed.directories_created != 0 || observed.files_copied != 0;
                    try { result.path = blink::core::JoinPath(snapshot.request.parent, snapshot.request.name); } catch (...) {}
                    try { result.failure_path = observed.current_destination_path.empty() ?
                        observed.current_source_path : observed.current_destination_path; } catch (...) {}
                }
                Publish(CompleteDirectory(id, std::move(result)), publish);
            }
            catch (...) {}
        }
        void RunDirectoryCopy(const std::uint64_t id, const FileOperationCancellation& cancelled,
            const DirectoryCopyExecuteFunction& execute, const PublishCallback& publish) noexcept
        {
            try
            {
                DirectoryCopyRequest request; std::optional<FileOperationSnapshot> publication;
                {
                    std::scoped_lock lock(mutex);
                    if (id != snapshot.operation_id || snapshot.kind != FileOperationKind::DirectoryCopy ||
                        !snapshot.directory_copy_request) { return; }
                    request = *snapshot.directory_copy_request;
                    if (!cancelled->load()) { snapshot.stage = FileOperationStage::Running; }
                    publication = snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
                }
                Publish(publication, publish);
                const auto state = shared_from_this();
                if (!request.resolve_conflict)
                { request.resolve_conflict = [state, id, publish](const DirectoryCopyConflict& conflict) {
                    return state->AwaitDirectoryConflict(id, conflict, publish);
                }; }
                const DirectoryCopyProgressCallback progress = [state, id, publish](const DirectoryCopyProgress& value) noexcept {
                    try { state->Publish(state->DirectoryProgress(id, value), publish); } catch (...) {}
                };
                auto result = cancelled->load() ? DirectoryCopyResult{ ERROR_CANCELLED } : execute(request, cancelled, progress);
                Publish(CompleteDirectory(id, std::move(result)), publish);
            }
            catch (const std::bad_alloc&) { DirectoryException(id, ERROR_NOT_ENOUGH_MEMORY, publish); }
            catch (...) { DirectoryException(id, ERROR_GEN_FAILURE, publish); }
        }
        bool BeginItem(const std::uint64_t id, const std::size_t index, FileCopyRequest& request,
            std::optional<DirectoryCopyRequest>& directory, std::optional<FileOperationSnapshot>& publication)
        {
            std::scoped_lock lock(mutex);
            if (id != snapshot.operation_id || !snapshot.copy_batch || index >= snapshot.copy_batch->items.size()) { return false; }
            SelectRequest(index);
            if (snapshot.closed || snapshot.cancel_requested || cancellation->load()) { return false; }
            request = snapshot.copy_batch->items[index].request; directory = snapshot.directory_copy_request;
            snapshot.stage = FileOperationStage::Running; snapshot.copy_progress = {}; snapshot.result = {};
            snapshot.directory_copy_progress = {}; snapshot.directory_copy_result.reset(); directory_root_path.clear();
            if (directory && directory->conflict_policy == FileCopyConflictPolicy::Merge)
            { directory_root_path = blink::core::JoinPath(request.destination_parent, request.destination_name); }
            snapshot.copy_batch->items[index].stage = FileCopyBatchItemStage::Running;
            progress_published = false; Recount(*snapshot.copy_batch);
            publication = Publication(); return true;
        }
        bool RecordItem(const std::uint64_t id, const std::size_t index, FileCopyResult result,
            std::optional<FileOperationSnapshot>& publication, std::optional<DirectoryCopyResult> directory_result = {})
        {
            NormalizeCopyResult(result);
            std::scoped_lock lock(mutex);
            if (id != snapshot.operation_id || !snapshot.copy_batch || index >= snapshot.copy_batch->items.size()) { return false; }
            auto& batch = *snapshot.copy_batch; auto& item = batch.items[index];
            item.stage = result.copied ? FileCopyBatchItemStage::Copied : result.skipped ? FileCopyBatchItemStage::Skipped :
                result.error == ERROR_CANCELLED ? FileCopyBatchItemStage::Cancelled : FileCopyBatchItemStage::Failed;
            item.result = std::move(result); item.directory_result = std::move(directory_result);
            if (item.directory_result)
            {
                if (item.directory_result->partial_tree_left) { ++item.directory_partial_attempts; }
                DirectorySummary(snapshot.directory_copy_result, *item.directory_result);
                snapshot.directory_copy_progress.files_copied = item.directory_result->files_copied;
                snapshot.directory_copy_progress.directories_created = item.directory_result->directories_created;
                snapshot.directory_copy_progress.files_skipped = item.directory_result->files_skipped;
                snapshot.directory_copy_progress.directories_skipped = item.directory_result->directories_skipped;
                snapshot.directory_copy_progress.completed = item.directory_result->copied;
            }
            CopyTopResult(item.result); Recount(batch);
            batch.current_index = index; batch.resumable = false;
            const bool success = Resolved(item.stage);
            if (!success)
            {
                snapshot.stage = item.stage == FileCopyBatchItemStage::Cancelled ? FileOperationStage::Cancelled : FileOperationStage::Failed;
                batch.resumable = item.stage == FileCopyBatchItemStage::Failed && !snapshot.closed && !snapshot.cancel_requested && !cancellation->load();
            }
            else if (index + 1 == batch.items.size())
            {
                batch.current_index = batch.items.size();
                snapshot.stage = batch.copied_count ? FileOperationStage::Completed : FileOperationStage::Skipped;
            }
            else { snapshot.stage = snapshot.cancel_requested || cancellation->load() ? FileOperationStage::Cancelling : FileOperationStage::Running; }
            publication = snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
            return success && index + 1 < batch.items.size();
        }
        void StopBatch(const std::uint64_t id, const std::size_t next, const PublishCallback& publish)
        {
            std::optional<FileOperationSnapshot> publication;
            {
                std::scoped_lock lock(mutex);
                if (id != snapshot.operation_id || !snapshot.copy_batch) { return; }
                CancelBatchUnlocked(next); publication = snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
            }
            Publish(publication, publish);
        }
        void BatchException(const std::uint64_t id, const std::size_t index, const DWORD error, const PublishCallback& publish) noexcept
        {
            try
            {
                std::optional<FileOperationSnapshot> publication;
                {
                    std::scoped_lock lock(mutex);
                    if (id != snapshot.operation_id || !snapshot.copy_batch) { return; }
                    auto& batch = *snapshot.copy_batch;
                    if (index < batch.items.size() && !Resolved(batch.items[index].stage))
                    {
                        auto& item = batch.items[index];
                        const bool running = item.stage == FileCopyBatchItemStage::Running && batch.current_index == index;
                        item.stage = FileCopyBatchItemStage::Failed; item.result = { error };
                        if (item.directory_limits)
                        {
                            if (!item.directory_result) { item.directory_result.emplace(); }
                            auto& tree = *item.directory_result; const bool previous_partial = tree.partial_tree_left;
                            tree.error = error; tree.copied = tree.skipped = false;
                            if (running)
                            {
                                tree.files_copied = (std::max)(tree.files_copied, snapshot.directory_copy_progress.files_copied);
                                tree.directories_created = (std::max)(tree.directories_created, snapshot.directory_copy_progress.directories_created);
                                if (tree.path.empty()) { try { tree.path = directory_root_path; } catch (...) {} }
                            }
                            tree.partial_tree_left = tree.partial_tree_left || tree.directories_created || tree.files_copied;
                            if (tree.partial_tree_left && !previous_partial) { ++item.directory_partial_attempts; }
                            DirectorySummary(snapshot.directory_copy_result, tree);
                            try { item.result.path = tree.path; } catch (...) {}
                        }
                        CopyTopResult(item.result); batch.current_index = index;
                    }
                    Recount(batch);
                    snapshot.stage = FileOperationStage::Failed;
                    batch.resumable = index < batch.items.size() && !snapshot.closed && !snapshot.cancel_requested;
                    publication = snapshot.closed ? std::optional<FileOperationSnapshot>{} : Publication();
                }
                Publish(publication, publish);
            }
            catch (...) {}
        }
        void RunCopyBatch(const std::uint64_t id, const FileOperationCancellation& cancelled,
            const CopyExecuteFunction& execute, const DirectoryCopyExecuteFunction& directory_execute,
            const PublishCallback& publish) noexcept
        {
            std::size_t index{};
            try
            {
                DWORD validation{};
                {
                    std::scoped_lock lock(mutex);
                    if (id != snapshot.operation_id || !snapshot.copy_batch) { return; }
                    index = snapshot.copy_batch->current_index;
                    std::size_t invalid{}; validation = ValidateBatch(*snapshot.copy_batch, invalid);
                    if (validation != ERROR_SUCCESS) { index = invalid; }
                }
                if (validation != ERROR_SUCCESS) { Publish(RejectBatch(id, validation, index), publish); return; }
                for (;; ++index)
                {
                    if (cancelled->load()) { StopBatch(id, index, publish); return; }
                    FileCopyRequest request; std::optional<FileOperationSnapshot> publication;
                    {
                        std::scoped_lock lock(mutex);
                        if (id != snapshot.operation_id || !snapshot.copy_batch || index >= snapshot.copy_batch->items.size()) { return; }
                        if (snapshot.copy_batch->items[index].request.conflict_policy == FileCopyConflictPolicy::Cancel)
                        { cancelled->store(true); }
                    }
                    if (cancelled->load()) { StopBatch(id, index, publish); return; }
                    std::optional<DirectoryCopyRequest> directory;
                    if (!BeginItem(id, index, request, directory, publication)) { StopBatch(id, index, publish); return; }
                    Publish(publication, publish);
                    const auto state = shared_from_this();
                    const FileCopyProgressCallback progress = [state, id, index, publish](const FileCopyProgress& value) noexcept {
                        try { state->Publish(state->Progress(id, index, value), publish); } catch (...) {}
                    };
                    FileCopyResult result; std::optional<DirectoryCopyResult> tree;
                    if (directory)
                    {
                        if (!directory->resolve_conflict)
                        { directory->resolve_conflict = [state, id, publish](const DirectoryCopyConflict& conflict) {
                            return state->AwaitDirectoryConflict(id, conflict, publish);
                        }; }
                        const DirectoryCopyProgressCallback directory_progress = [state, id, index, publish](const auto& value) noexcept {
                            try { state->Publish(state->DirectoryProgress(id, value, index), publish); } catch (...) {}
                        };
                        tree.emplace(); DWORD exception{};
                        try { *tree = cancelled->load() ? DirectoryCopyResult{ ERROR_CANCELLED } :
                            directory_execute(*directory, cancelled, directory_progress); }
                        catch (const std::bad_alloc&) { exception = ERROR_NOT_ENOUGH_MEMORY; }
                        catch (...) { exception = ERROR_GEN_FAILURE; }
                        if (exception)
                        {
                            std::scoped_lock lock(mutex);
                            tree->error = exception; tree->files_copied = snapshot.directory_copy_progress.files_copied;
                            tree->directories_created = snapshot.directory_copy_progress.directories_created;
                            tree->files_skipped = snapshot.directory_copy_progress.files_skipped;
                            tree->directories_skipped = snapshot.directory_copy_progress.directories_skipped;
                            tree->partial_tree_left = tree->directories_created != 0 || tree->files_copied != 0;
                            tree->path = directory_root_path.empty() ? blink::core::JoinPath(request.destination_parent,
                                request.destination_name) : directory_root_path;
                            tree->failure_path = snapshot.directory_copy_progress.current_destination_path;
                        }
                        NormalizeDirectoryResult(*directory, *tree);
                        result.error = tree->error; result.copied = tree->copied; result.skipped = tree->skipped;
                        try { result.path = tree->path; } catch (...) {}
                    }
                    else
                    {
                        try { result = cancelled->load() ? FileCopyResult{ ERROR_CANCELLED } : execute(request, cancelled, progress); }
                        catch (const std::bad_alloc&) { result.error = ERROR_NOT_ENOUGH_MEMORY; }
                        catch (...) { result.error = ERROR_GEN_FAILURE; }
                    }
                    const bool next = RecordItem(id, index, std::move(result), publication, std::move(tree));
                    Publish(publication, publish);
                    if (!next) { return; }
                }
            }
            catch (const std::bad_alloc&) { BatchException(id, index, ERROR_NOT_ENOUGH_MEMORY, publish); }
            catch (...) { BatchException(id, index, ERROR_GEN_FAILURE, publish); }
        }
    };

    FileOperationCoordinator::FileOperationCoordinator(ExecuteFunction execute, CopyExecuteFunction copy_execute,
        DirectoryCopyExecuteFunction directory_copy_execute, MoveExecuteFunction move_execute, DeleteExecuteFunction delete_execute)
        : state_(std::make_shared<State>()), execute_(execute ? std::move(execute) : ExecuteFunction{
            [](const NewFolderRequest& request, const FileOperationCancellation& cancelled) { return CreateFolderSafely(request, cancelled); } }),
        copy_execute_(copy_execute ? std::move(copy_execute) : CopyExecuteFunction{
            [](const FileCopyRequest& request, const FileOperationCancellation& cancelled, const FileCopyProgressCallback& progress) {
                return CopyFileSafely(request, cancelled, progress);
            } }),
        directory_copy_execute_(directory_copy_execute ? std::move(directory_copy_execute) : DirectoryCopyExecuteFunction{
            [](const DirectoryCopyRequest& request, const FileOperationCancellation& cancelled, const DirectoryCopyProgressCallback& progress) {
                return CopyDirectorySafely(request, cancelled, progress);
            } }),
        move_execute_(move_execute ? std::move(move_execute) : MoveExecuteFunction{
            [](const MoveRequest& request, const FileOperationCancellation& cancelled, const MoveProgressCallback& progress) {
                return MoveEntrySafely(request, cancelled, progress);
            } }), delete_execute_(delete_execute ? std::move(delete_execute) : DeleteExecuteFunction{
            [](const DeleteRequest& request, const FileOperationCancellation& cancelled, const DeleteProgressCallback& progress) {
                return DeleteEntrySafely(request, cancelled, progress);
            } }) {}
    FileOperationCoordinator::~FileOperationCoordinator() noexcept { Shutdown(); }

    std::uint64_t FileOperationCoordinator::MoveEntries(std::vector<MoveRequest> requests, PublishCallback publish,
        FileOperationPrerequisites prerequisites)
    {
        MoveBatchSnapshot batch; batch.requested_count = requests.size();
        if (requests.size() <= MaximumFileCopyBatchItems)
        {
            batch.items.reserve(requests.size());
            for (auto& request : requests) { MoveBatchItem item; item.request = std::move(request); batch.items.push_back(std::move(item)); }
        }
        State::RecountMoves(batch); NewFolderRequest target; std::optional<MoveRequest> first;
        if (!batch.items.empty())
        { first = batch.items.front().request; target = { first->destination_parent, first->destination_parent_identity, first->destination_name }; }
        return Submit(std::move(target), std::move(publish), FileOperationKind::Move, {}, {}, {}, {}, {}, {},
            std::move(first), std::move(batch), std::move(prerequisites));
    }
    std::uint64_t FileOperationCoordinator::ResumeMoveBatch(const std::uint64_t expected_operation_id, const FileCopyConflictPolicy policy,
        std::optional<std::wstring> new_name, PublishCallback publish, std::optional<std::wstring> destination_parent,
        FileOperationPrerequisites prerequisites)
    {
        if (!KnownPolicy(policy) || policy == FileCopyConflictPolicy::Merge ||
            (new_name && ValidateNewFolderName(*new_name) != ERROR_SUCCESS) ||
            (destination_parent && LocalParent(*destination_parent).empty())) { return 0; }
        MoveBatchSnapshot batch;
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->snapshot.closed || state_->snapshot.operation_id != expected_operation_id ||
                state_->snapshot.kind != FileOperationKind::Move || state_->snapshot.stage != FileOperationStage::Failed ||
                !state_->snapshot.move_batch || !state_->snapshot.move_batch->resumable) { return 0; }
            batch = *state_->snapshot.move_batch;
        }
        if (batch.current_index >= batch.items.size()) { return 0; }
        auto& current = batch.items[batch.current_index];
        if (policy == FileCopyConflictPolicy::Replace && current.request.source.is_directory) { return 0; }
        if (MoveHasPartialDestination(current.result))
        {
            if ((policy != FileCopyConflictPolicy::Skip && policy != FileCopyConflictPolicy::Cancel) || new_name) { return 0; }
            if (policy == FileCopyConflictPolicy::Skip) { current.retained_result = current.result; }
        }
        if (policy != FileCopyConflictPolicy::Cancel) { current.stage = MoveBatchItemStage::Pending; current.result = {}; }
        current.request.conflict_policy = policy;
        if (new_name) { current.request.destination_name = std::move(*new_name); }
        if (destination_parent)
        { for (auto& item : batch.items) { if (!MoveResolved(item.stage)) { item.request.destination_parent = *destination_parent; } } }
        batch.resumable = false; State::RecountMoves(batch);
        auto request = current.request;
        NewFolderRequest target{ request.destination_parent, request.destination_parent_identity, request.destination_name };
        return Submit(std::move(target), std::move(publish), FileOperationKind::Move, {}, {}, {}, {}, expected_operation_id, {},
            std::move(request), std::move(batch), std::move(prerequisites));
    }
    bool FileOperationCoordinator::HasPendingMoveBatch() const noexcept
    {
        try { std::scoped_lock lock(state_->mutex); return state_->snapshot.move_batch && state_->snapshot.move_batch->resumable; }
        catch (...) { return true; }
    }
    bool FileOperationCoordinator::HasPendingBatch() const noexcept
    { return HasPendingCopyBatch() || HasPendingMoveBatch() || HasPendingDeleteBatch(); }

    std::uint64_t FileOperationCoordinator::DeleteEntries(std::vector<DeleteRequest> requests, PublishCallback publish,
        FileOperationPrerequisites prerequisites)
    {
        DeleteBatchSnapshot batch; batch.requested_count = requests.size();
        if (requests.size() <= MaximumDeleteItems)
        {
            batch.items.reserve(requests.size());
            for (auto& request : requests) { DeleteBatchItem item; item.request = std::move(request); batch.items.push_back(std::move(item)); }
        }
        State::RecountDeletes(batch); NewFolderRequest target; std::optional<DeleteRequest> first;
        if (!batch.items.empty())
        { first = batch.items.front().request; target = { first->parent, first->parent_identity, first->source.name }; }
        return Submit(std::move(target), std::move(publish), FileOperationKind::Delete, {}, {}, {}, {}, {}, {}, {}, {},
            std::move(prerequisites), std::move(first), std::move(batch));
    }
    std::uint64_t FileOperationCoordinator::ResumeDeleteBatch(const std::uint64_t expected_operation_id, const DeleteRetryAction action,
        PublishCallback publish, FileOperationPrerequisites prerequisites, std::optional<std::wstring> parent)
    {
        if (action != DeleteRetryAction::Retry && action != DeleteRetryAction::Skip) { return 0; }
        DeleteBatchSnapshot batch;
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->snapshot.closed || state_->snapshot.operation_id != expected_operation_id || state_->snapshot.kind != FileOperationKind::Delete ||
                state_->snapshot.stage != FileOperationStage::Failed || !state_->snapshot.delete_batch || !state_->snapshot.delete_batch->resumable) { return 0; }
            batch = *state_->snapshot.delete_batch;
        }
        if (batch.current_index >= batch.items.size()) { return 0; }
        auto& current = batch.items[batch.current_index];
        if (parent)
        {
            const auto identity = current.request.parent_identity;
            for (auto& item : batch.items)
            { if (!State::DeleteResolved(item.stage) && item.request.parent_identity == identity) { item.request.parent = *parent; } }
        }
        if (action == DeleteRetryAction::Skip)
        { current.retained_result = current.result; current.previous_observed_removed = 0; current.previous_possible_partial = false; }
        else
        { current.previous_observed_removed = current.result.observed_removed; current.previous_possible_partial = current.result.possible_partial; }
        current.request.skip = action == DeleteRetryAction::Skip; current.stage = DeleteItemStage::Pending; current.result = {};
        batch.resumable = false; State::RecountDeletes(batch);
        auto request = current.request; NewFolderRequest target{ request.parent, request.parent_identity, request.source.name };
        return Submit(std::move(target), std::move(publish), FileOperationKind::Delete, {}, {}, {}, {}, expected_operation_id, {}, {}, {},
            std::move(prerequisites), std::move(request), std::move(batch));
    }
    bool FileOperationCoordinator::HasPendingDeleteBatch() const noexcept
    {
        try { std::scoped_lock lock(state_->mutex); return state_->snapshot.delete_batch && state_->snapshot.delete_batch->resumable; }
        catch (...) { return true; }
    }

    std::uint64_t FileOperationCoordinator::MoveEntry(MoveRequest request, PublishCallback publish, FileOperationPrerequisites prerequisites)
    {
        NewFolderRequest target{ request.destination_parent, request.destination_parent_identity, request.destination_name };
        return Submit(std::move(target), std::move(publish), FileOperationKind::Move, {}, {}, {}, {}, {}, {}, std::move(request), {}, std::move(prerequisites));
    }
    std::uint64_t FileOperationCoordinator::CreateFolder(NewFolderRequest request, PublishCallback publish)
    {
        WorkerExecuteFunction execute = [executor = execute_](const NewFolderRequest& target,
            const FileOperationCancellation& cancelled, const FileCopyProgressCallback&) { return executor(target, cancelled); };
        return Submit(std::move(request), std::move(publish), FileOperationKind::NewFolder, {}, {}, std::move(execute));
    }
    std::uint64_t FileOperationCoordinator::Rename(RenameRequest request, PublishCallback publish, FileOperationPrerequisites prerequisites)
    {
        NewFolderRequest target{ request.parent, request.parent_identity, request.new_name }; auto source = request.source;
        WorkerExecuteFunction execute = [request = std::move(request)](const NewFolderRequest&,
            const FileOperationCancellation& cancelled, const FileCopyProgressCallback&) {
            auto result = RenameEntrySafely(request, cancelled);
            return FileOperationResult{ result.error, result.renamed, std::move(result.path) };
        };
        return Submit(std::move(target), std::move(publish), FileOperationKind::Rename, std::move(source), {}, std::move(execute), {}, {}, {}, {}, {}, std::move(prerequisites));
    }
    std::uint64_t FileOperationCoordinator::Copy(FileCopyRequest request, PublishCallback publish)
    {
        std::vector<FileCopyRequest> requests; requests.push_back(std::move(request));
        return CopyBatch(std::move(requests), std::move(publish));
    }
    std::uint64_t FileOperationCoordinator::CopyBatch(std::vector<FileCopyRequest> requests, PublishCallback publish)
    {
        FileCopyBatchSnapshot batch; batch.requested_count = requests.size();
        if (requests.size() <= MaximumFileCopyBatchItems)
        {
            batch.items.reserve(requests.size());
            for (auto& request : requests) { FileCopyBatchItem item; item.request = std::move(request); batch.items.push_back(std::move(item)); }
        }
        State::Recount(batch);
        NewFolderRequest target; std::optional<FileCopyRequest> first;
        if (!batch.items.empty())
        { first = batch.items.front().request; target = { first->destination_parent, first->destination_parent_identity, first->destination_name }; }
        return Submit(std::move(target), std::move(publish), FileOperationKind::Copy, {}, std::move(first), {}, std::move(batch));
    }
    std::uint64_t FileOperationCoordinator::CopyItems(CopyItemRequests requests, PublishCallback publish)
    {
        FileCopyBatchSnapshot batch; batch.requested_count = requests.size();
        if (requests.size() <= MaximumFileCopyBatchItems)
        {
            batch.items.reserve(requests.size());
            for (auto& source : requests)
            {
                FileCopyBatchItem item;
                if (auto* file = std::get_if<FileCopyRequest>(&source)) { item.request = std::move(*file); }
                else
                {
                    const auto& directory = std::get<DirectoryCopyRequest>(source);
                    item.request = FileCopyProjection(directory); item.directory_limits = directory.limits;
                }
                batch.items.push_back(std::move(item));
            }
        }
        State::Recount(batch); NewFolderRequest target; std::optional<FileCopyRequest> first;
        std::optional<DirectoryCopyRequest> directory;
        if (!batch.items.empty())
        {
            first = batch.items.front().request;
            target = { first->destination_parent, first->destination_parent_identity, first->destination_name };
            if (batch.items.front().directory_limits) { directory = DirectoryCopyProjection(*first, *batch.items.front().directory_limits); }
        }
        return Submit(std::move(target), std::move(publish), FileOperationKind::Copy, {}, std::move(first), {},
            std::move(batch), {}, std::move(directory));
    }
    std::uint64_t FileOperationCoordinator::CopyDirectory(DirectoryCopyRequest request, PublishCallback publish)
    {
        NewFolderRequest target{ request.destination_parent, request.destination_parent_identity, request.destination_name };
        return Submit(std::move(target), std::move(publish), FileOperationKind::DirectoryCopy, {}, {}, {}, {}, {}, std::move(request));
    }
    bool FileOperationCoordinator::ResolveDirectoryConflict(const std::uint64_t operation_id, const std::uint64_t conflict_id,
        const FileCopyConflictPolicy policy) noexcept
    {
        try
        {
            std::scoped_lock lock(state_->mutex);
            auto& snapshot = state_->snapshot;
            if (snapshot.closed || snapshot.operation_id != operation_id || snapshot.directory_conflict_id != conflict_id ||
                !snapshot.directory_conflict || state_->conflict_decision || snapshot.cancel_requested ||
                snapshot.stage != FileOperationStage::Running) { return false; }
            const auto& conflict = *snapshot.directory_conflict;
            switch (policy)
            {
            case FileCopyConflictPolicy::Skip: case FileCopyConflictPolicy::KeepBoth: case FileCopyConflictPolicy::Cancel:
                break;
            case FileCopyConflictPolicy::Replace:
                if (conflict.request.source.is_directory || conflict.destination_is_directory) { return false; }
                break;
            case FileCopyConflictPolicy::Merge:
                if (!conflict.request.source.is_directory || !conflict.destination_is_directory) { return false; }
                break;
            default: return false;
            }
            state_->conflict_decision = policy;
            state_->conflict_changed.notify_all();
            return true;
        }
        catch (...) { return false; }
    }
    std::uint64_t FileOperationCoordinator::ResumeCopyBatch(const std::uint64_t expected_operation_id,
        const FileCopyConflictPolicy policy, std::optional<std::wstring> new_name, PublishCallback publish,
        std::optional<std::wstring> destination_parent)
    {
        if (!KnownPolicy(policy) ||
            (new_name && ValidateNewFolderName(*new_name) != ERROR_SUCCESS) ||
            (destination_parent && LocalParent(*destination_parent).empty())) { return 0; }
        FileCopyBatchSnapshot batch;
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->snapshot.closed || state_->snapshot.operation_id != expected_operation_id ||
                state_->snapshot.stage != FileOperationStage::Failed || !state_->snapshot.copy_batch ||
                !state_->snapshot.copy_batch->resumable) { return 0; }
            batch = *state_->snapshot.copy_batch;
        }
        if (batch.current_index >= batch.items.size()) { return 0; }
        auto& current = batch.items[batch.current_index];
        if (policy == FileCopyConflictPolicy::Replace && current.directory_limits) { return 0; }
        if (policy == FileCopyConflictPolicy::Merge && !current.directory_limits) { return 0; }
        if (current.request.conflict_policy == FileCopyConflictPolicy::Merge && current.directory_result &&
            current.directory_result->partial_tree_left && policy != FileCopyConflictPolicy::Skip &&
            policy != FileCopyConflictPolicy::Cancel) { return 0; }
        if (policy != FileCopyConflictPolicy::Cancel)
        {
            if (current.stage == FileCopyBatchItemStage::Failed)
            {
                if (current.directory_result && current.directory_result->partial_tree_left)
                {
                    const auto& previous = *current.directory_result;
                    const auto characters = previous.path.size() + previous.failure_path.size();
                    if (current.previous_directory_results.size() < 32 && characters <= 65536 - current.directory_history_characters)
                    {
                        current.previous_directory_results.push_back(previous);
                        current.directory_history_characters += characters;
                    }
                }
                else if (!current.directory_limits) { current.previous_results.push_back(current.result); }
            }
            current.directory_result.reset(); current.result = {}; current.stage = FileCopyBatchItemStage::Pending;
        }
        current.request.conflict_policy = policy;
        if (new_name) { current.request.destination_name = std::move(*new_name); }
        if (destination_parent)
        {
            for (std::size_t index = batch.current_index; index < batch.items.size(); ++index)
            {
                if (!Resolved(batch.items[index].stage)) { batch.items[index].request.destination_parent = *destination_parent; }
            }
        }
        batch.resumable = false; State::Recount(batch);
        auto request = current.request;
        NewFolderRequest target{ request.destination_parent, request.destination_parent_identity, request.destination_name };
        auto directory = current.directory_limits ?
            std::optional<DirectoryCopyRequest>{ DirectoryCopyProjection(request, *current.directory_limits) } : std::nullopt;
        return Submit(std::move(target), std::move(publish), FileOperationKind::Copy, {}, std::move(request), {},
            std::move(batch), expected_operation_id, std::move(directory));
    }
    std::uint64_t FileOperationCoordinator::Submit(NewFolderRequest request, PublishCallback publish, const FileOperationKind kind,
        std::optional<blink::core::FileEntry> source, std::optional<FileCopyRequest> copy_request, WorkerExecuteFunction execute,
        std::optional<FileCopyBatchSnapshot> copy_batch, const std::optional<std::uint64_t> expected_resume_id,
        std::optional<DirectoryCopyRequest> directory_copy_request, std::optional<MoveRequest> move_request,
        std::optional<MoveBatchSnapshot> move_batch, FileOperationPrerequisites prerequisites,
        std::optional<DeleteRequest> delete_request, std::optional<DeleteBatchSnapshot> delete_batch)
    {
        const bool move_queue = move_batch.has_value();
        std::unique_lock work_lock(work_mutex_);
        if (work_ && work_.Status() == winrt::Windows::Foundation::AsyncStatus::Started) { return 0; }
        const auto cancellation = std::make_shared<std::atomic_bool>(false);
        FileOperationSnapshot next; next.request = std::move(request); next.kind = kind;
        next.rename_source = std::move(source); next.copy_request = std::move(copy_request); next.copy_batch = std::move(copy_batch);
        next.directory_copy_request = std::move(directory_copy_request); next.move_request = std::move(move_request);
        next.move_batch = std::move(move_batch);
        next.delete_request = std::move(delete_request); next.delete_batch = std::move(delete_batch);
        next.stage = FileOperationStage::Queued;
        std::uint64_t id{};
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->snapshot.closed) { return 0; }
            if (expected_resume_id)
            {
                if (state_->snapshot.operation_id != *expected_resume_id || state_->snapshot.stage != FileOperationStage::Failed ||
                    state_->snapshot.kind != kind ||
                    (kind == FileOperationKind::Copy ? !state_->snapshot.copy_batch || !state_->snapshot.copy_batch->resumable :
                        kind == FileOperationKind::Delete ? !state_->snapshot.delete_batch || !state_->snapshot.delete_batch->resumable :
                        !state_->snapshot.move_batch || !state_->snapshot.move_batch->resumable)) { return 0; }
            }
            else if (state_->ProtectsPending()) { return 0; }
            id = next.operation_id = state_->snapshot.operation_id + 1;
            state_->snapshot = std::move(next); state_->cancellation = cancellation; state_->progress_published = false;
            state_->conflict_decision.reset();
        }
        const auto state = state_; const bool batch = kind == FileOperationKind::Copy;
        const bool directory = kind == FileOperationKind::DirectoryCopy;
        const bool moving = kind == FileOperationKind::Move;
        const bool deleting = kind == FileOperationKind::Delete;
        try
        {
            work_ = winrt::Windows::System::Threading::ThreadPool::RunAsync(
                [state, cancellation, id, batch, directory, moving, move_queue, deleting, execute = std::move(execute), copy_execute = copy_execute_,
                    directory_copy_execute = directory_copy_execute_, move_execute = move_execute_, publish,
                    delete_execute = delete_execute_, prerequisites = std::move(prerequisites)](auto&&) {
                    // This is the existing operation worker, never the UI.
                    // Watcher Stop completion proves its native read/handle
                    // has actually drained before a directory rename begins.
                    DWORD preparation_error{};
                    try { for (const auto& action : prerequisites) { if (action) { action.get(); } } }
                    catch (...) { preparation_error = ERROR_GEN_FAILURE; }
                    if (preparation_error)
                    {
                        if (deleting) { state->DeleteException(id, HRESULT_FROM_WIN32(preparation_error), publish); }
                        else if (move_queue)
                        {
                            std::size_t index{};
                            { std::scoped_lock lock(state->mutex); if (state->snapshot.move_batch) { index = state->snapshot.move_batch->current_index; } }
                            state->MoveBatchException(id, index, preparation_error, publish, false);
                        }
                        else if (moving) { state->MoveException(id, preparation_error, publish); }
                        else { state->Publish(state->Complete(id, { preparation_error }), publish); }
                        return;
                    }
                    if (deleting) { state->RunDeleteBatch(id, cancellation, delete_execute, publish); return; }
                    if (batch) { state->RunCopyBatch(id, cancellation, copy_execute, directory_copy_execute, publish); return; }
                    if (directory) { state->RunDirectoryCopy(id, cancellation, directory_copy_execute, publish); return; }
                    if (move_queue) { state->RunMoveBatch(id, cancellation, move_execute, publish); return; }
                    if (moving) { state->RunMove(id, cancellation, move_execute, publish); return; }
                    NewFolderResult result;
                    try
                    {
                        NewFolderRequest request;
                        {
                            std::scoped_lock lock(state->mutex); request = state->snapshot.request;
                            if (!cancellation->load()) { state->snapshot.stage = FileOperationStage::Running; }
                        }
                        result = cancellation->load() ? NewFolderResult{ ERROR_CANCELLED } : execute(request, cancellation, {});
                    }
                    catch (const std::bad_alloc&) { result.error = ERROR_NOT_ENOUGH_MEMORY; }
                    catch (...) { result.error = ERROR_GEN_FAILURE; }
                    state->Publish(state->Complete(id, std::move(result)), publish);
                });
        }
        catch (const std::bad_alloc&)
        {
            work_lock.unlock();
            if (deleting) { state->DeleteException(id, E_OUTOFMEMORY, publish); }
            else if (move_queue) { state->MoveBatchException(id, 0, ERROR_NOT_ENOUGH_MEMORY, publish, false); }
            else if (moving) { state->MoveException(id, ERROR_NOT_ENOUGH_MEMORY, publish); }
            else if (directory) { state->DirectoryException(id, ERROR_NOT_ENOUGH_MEMORY, publish); }
            else if (batch) { state->Publish(state->RejectBatch(id, ERROR_GEN_FAILURE, 0, true), publish); }
            else { state->Publish(state->Complete(id, { ERROR_GEN_FAILURE }), publish); }
        }
        catch (...)
        {
            work_lock.unlock();
            if (deleting) { state->DeleteException(id, E_FAIL, publish); }
            else if (move_queue) { state->MoveBatchException(id, 0, ERROR_GEN_FAILURE, publish, false); }
            else if (moving) { state->MoveException(id, ERROR_GEN_FAILURE, publish); }
            else if (batch) { state->Publish(state->RejectBatch(id, ERROR_GEN_FAILURE, 0, true), publish); }
            else if (directory) { state->DirectoryException(id, ERROR_GEN_FAILURE, publish); }
            else { state->Publish(state->Complete(id, { ERROR_GEN_FAILURE }), publish); }
        }
        return id;
    }
    FileOperationSnapshot FileOperationCoordinator::Snapshot() const
    { std::scoped_lock lock(state_->mutex); return state_->snapshot; }
    bool FileOperationCoordinator::IsBusy() const noexcept
    {
        try { std::scoped_lock lock(work_mutex_); return work_ && work_.Status() == winrt::Windows::Foundation::AsyncStatus::Started; }
        catch (...) { return true; }
    }
    bool FileOperationCoordinator::HasPendingCopyBatch() const noexcept
    {
        try { std::scoped_lock lock(state_->mutex); return state_->snapshot.copy_batch && state_->snapshot.copy_batch->resumable && state_->snapshot.copy_batch->remaining_count != 0; }
        catch (...) { return true; }
    }
    winrt::Windows::Foundation::IAsyncAction FileOperationCoordinator::CurrentAction() const
    { std::scoped_lock lock(work_mutex_); return work_; }
    void FileOperationCoordinator::Cancel() noexcept
    {
        std::scoped_lock lock(state_->mutex);
        if (state_->snapshot.delete_batch && state_->snapshot.delete_batch->resumable && state_->snapshot.stage == FileOperationStage::Failed)
        {
            if (state_->cancellation) { state_->cancellation->store(true); }
            state_->CancelDeleteBatchUnlocked(); return;
        }
        if (state_->snapshot.move_batch && state_->snapshot.move_batch->resumable && state_->snapshot.stage == FileOperationStage::Failed)
        {
            if (state_->cancellation) { state_->cancellation->store(true); }
            state_->CancelMoveBatchUnlocked(state_->snapshot.move_batch->current_index); return;
        }
        if (state_->snapshot.copy_batch && state_->snapshot.copy_batch->resumable && state_->snapshot.stage == FileOperationStage::Failed)
        {
            if (state_->cancellation) { state_->cancellation->store(true); }
            state_->CancelBatchUnlocked(state_->snapshot.copy_batch->current_index); return;
        }
        if (state_->cancellation && (state_->snapshot.stage == FileOperationStage::Queued ||
            state_->snapshot.stage == FileOperationStage::Running || state_->snapshot.stage == FileOperationStage::Cancelling))
        {
            state_->cancellation->store(true); state_->snapshot.cancel_requested = true;
            state_->snapshot.stage = FileOperationStage::Cancelling;
            state_->conflict_changed.notify_all();
        }
    }
    void FileOperationCoordinator::Shutdown() noexcept
    {
        std::scoped_lock lock(state_->mutex); state_->snapshot.closed = true;
        if (state_->cancellation) { state_->cancellation->store(true); }
        state_->conflict_changed.notify_all();
        if (state_->snapshot.copy_batch) { state_->snapshot.copy_batch->resumable = false; }
        if (state_->snapshot.move_batch) { state_->snapshot.move_batch->resumable = false; }
        if (state_->snapshot.delete_batch) { state_->snapshot.delete_batch->resumable = false; }
        // The action owns its worker/state until return. No UI wait or force cancellation.
    }
}
