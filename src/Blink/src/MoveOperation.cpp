#include "Blink/App/MoveOperation.h"
#include "Blink/App/HandleRename.h"
#include "Blink/App/DirectoryLease.h"
#include <algorithm>

namespace blink::app
{
    namespace
    {
        constexpr DWORD Ordinary = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
            FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
        bool Stopping(const FileOperationCancellation& cancelled) noexcept { return cancelled && cancelled->load(); }
        DWORD CheckObject(HANDLE handle, const blink::core::FileIdentity* expected, bool directory, bool ordinary)
        {
            FILE_ATTRIBUTE_TAG_INFO tag{}; FILE_STANDARD_INFO standard{}; blink::core::FileIdentity identity;
            if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) ||
                !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
                !blink::core::ReadFileIdentity(handle, identity)) { return GetLastError(); }
            if (expected && identity != *expected) { return ERROR_FILE_INVALID; }
            if (standard.DeletePending || directory != (standard.Directory != FALSE) ||
                directory != ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)) { return ERROR_FILE_INVALID; }
            if (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { return ERROR_NOT_SUPPORTED; }
            if (ordinary && (tag.FileAttributes & ~(Ordinary | (directory ? FILE_ATTRIBUTE_DIRECTORY : 0)))) { return ERROR_NOT_SUPPORTED; }
            return ERROR_SUCCESS;
        }
        DWORD CheckChain(const detail::DirectoryLease& lease)
        {
            for (const auto& handle : lease.handles)
            { if (const auto error = CheckObject(handle.get(), nullptr, true, false); error != ERROR_SUCCESS) { return error; } }
            return ERROR_SUCCESS;
        }
        bool Report(const MoveProgressCallback& callback, const MoveProgress& value) noexcept
        { try { if (callback) { callback(value); } return true; } catch (...) { return false; } }
        std::wstring NumberedName(const std::wstring& original, unsigned number, std::size_t maximum, bool directory)
        {
            const auto dot = directory ? std::wstring::npos : original.find_last_of(L'.');
            const bool extension_exists = dot != std::wstring::npos && dot != 0;
            const auto extension = extension_exists ? original.substr(dot) : std::wstring{};
            const auto stem = extension_exists ? original.substr(0, dot) : original;
            const auto suffix = L" (" + std::to_wstring(number) + L")";
            if (suffix.size() + extension.size() >= maximum) { return {}; }
            auto prefix = stem.substr(0, maximum - suffix.size() - extension.size());
            if (!prefix.empty() && prefix.back() >= 0xd800 && prefix.back() <= 0xdbff) { prefix.pop_back(); }
            return prefix.empty() ? std::wstring{} : prefix + suffix + extension;
        }
    }
    MoveResult MoveFileAcrossVolumesSafely(const MoveRequest& request, const FileOperationCancellation& cancelled,
        const MoveProgressCallback& progress, const BeforeMoveSourceRemoval& before_removal) noexcept
    {
        MoveResult result;
        try
        {
            result.source_path = blink::core::JoinPath(request.source_parent, request.source.name);
            if (request.source.is_directory) { result.error = ERROR_NOT_SUPPORTED; return result; }
            if (request.source.identity.valid && request.destination_parent_identity.valid &&
                request.source.identity.volume_serial == request.destination_parent_identity.volume_serial)
            { result.error = ERROR_INVALID_PARAMETER; return result; }
            bool observer_failed{};
            auto copy = detail::CopyFileAndRemoveSource(request, cancelled,
                [&](const FileCopyProgress& value) {
                    MoveProgress update{ MovePhase::Copying, result.source_path,
                        blink::core::JoinPath(request.destination_parent, request.destination_name), value };
                    if (!Report(progress, update)) { observer_failed = true; throw 1; }
                }, [&]() -> DWORD {
                    if (observer_failed) { return ERROR_UNHANDLED_EXCEPTION; }
                    if (!Report(progress, { MovePhase::Committing, result.source_path, {} })) { return ERROR_UNHANDLED_EXCEPTION; }
                    return before_removal ? before_removal() : ERROR_SUCCESS;
                });
            result.source_removed = copy.source_removed;
            result.moved = copy.source_removed && copy.copy.copied;
            result.skipped = copy.copy.skipped;
            result.error = result.moved || result.skipped ? ERROR_SUCCESS :
                copy.copy.copied ? copy.removal_error : copy.copy.error;
            result.copy_result = std::move(copy.copy);
            result.path = result.copy_result->path;
            if (result.moved) { (void)Report(progress, { MovePhase::Completed, result.source_path, result.path }); }
        }
        catch (const std::bad_alloc&) { if (result.moved) { result.post_commit_error = ERROR_NOT_ENOUGH_MEMORY; } else { result.error = ERROR_NOT_ENOUGH_MEMORY; } }
        catch (...) { if (result.moved) { result.post_commit_error = ERROR_GEN_FAILURE; } else { result.error = ERROR_GEN_FAILURE; } }
        return result;
    }

    MoveResult MoveDirectoryAcrossVolumesSafely(const MoveRequest& request, const FileOperationCancellation& cancelled,
        const MoveProgressCallback& progress, const BeforeMoveSourceRemoval& before_removal) noexcept
    {
        MoveResult result;
        try
        {
            result.source_path = blink::core::JoinPath(request.source_parent, request.source.name);
            if (!request.source.is_directory) { result.error = ERROR_NOT_SUPPORTED; return result; }
            if (request.source.identity.valid && request.destination_parent_identity.valid &&
                request.source.identity.volume_serial == request.destination_parent_identity.volume_serial)
            { result.error = ERROR_INVALID_PARAMETER; return result; }
            DirectoryCopyRequest directory{ request.source_parent, request.source_parent_identity, request.source,
                request.destination_parent, request.destination_parent_identity, request.destination_name, {}, request.conflict_policy };
            auto copy = detail::CopyDirectoryAndRemoveSource(directory, cancelled, [&](const DirectoryCopyProgress& value) {
                MoveProgress update{ value.completed ? MovePhase::Completed : value.removing_source_directories ?
                    MovePhase::Committing : MovePhase::Copying, value.current_source_path, value.current_destination_path,
                    value.current_file, value.files_copied, value.directories_created, value.source_files_removed,
                    value.source_directories_removed, true };
                if (!Report(progress, update)) { throw 1; }
            }, before_removal);
            // Record native effects before allocating presentation strings.
            result.source_removed = copy.source_removed;
            result.source_files_removed = copy.files_removed;
            result.source_directories_removed = copy.directories_removed;
            result.moved = copy.source_removed && copy.copy.copied;
            result.skipped = copy.copy.skipped;
            result.error = result.moved || result.skipped ? ERROR_SUCCESS :
                copy.copy.copied ? copy.removal_error : copy.copy.error;
            result.directory_copy_result = std::move(copy.copy);
            result.path = result.directory_copy_result->path;
        }
        catch (const std::bad_alloc&) { if (result.moved) { result.post_commit_error = ERROR_NOT_ENOUGH_MEMORY; } else { result.error = ERROR_NOT_ENOUGH_MEMORY; } }
        catch (...) { if (result.moved) { result.post_commit_error = ERROR_GEN_FAILURE; } else { result.error = ERROR_GEN_FAILURE; } }
        return result;
    }

    MoveResult MoveEntrySafely(const MoveRequest& request, const FileOperationCancellation& cancelled,
        const MoveProgressCallback& progress, const NativeMoveFunction& native) noexcept
    {
        MoveResult result;
        try
        {
            if (request.conflict_policy == FileCopyConflictPolicy::Skip)
            { result.error = ERROR_SUCCESS; result.skipped = true; return result; }
            if (request.conflict_policy == FileCopyConflictPolicy::Cancel) { result.error = ERROR_CANCELLED; return result; }
            const bool replacing = request.conflict_policy == FileCopyConflictPolicy::Replace;
            if (replacing && request.source.is_directory) { result.error = ERROR_NOT_SUPPORTED; return result; }
            if (request.conflict_policy != FileCopyConflictPolicy::Fail && request.conflict_policy != FileCopyConflictPolicy::KeepBoth && !replacing)
            { result.error = ERROR_INVALID_PARAMETER; return result; }
            if (Stopping(cancelled)) { result.error = ERROR_CANCELLED; return result; }
            if (const auto error = ValidateNewFolderName(request.source.name); error != ERROR_SUCCESS) { result.error = error; return result; }
            if (const auto error = ValidateNewFolderName(request.destination_name); error != ERROR_SUCCESS) { result.error = error; return result; }
            if (!request.source.identity.valid || !request.source_parent_identity.valid || !request.destination_parent_identity.valid ||
                request.source.identity.volume_serial != request.source_parent_identity.volume_serial)
            { result.error = ERROR_FILE_INVALID; return result; }
            if (request.source.is_directory != ((request.source.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
            { result.error = ERROR_FILE_INVALID; return result; }
            if ((request.source.attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                (request.source.attributes & ~(Ordinary | (request.source.is_directory ? FILE_ATTRIBUTE_DIRECTORY : 0))))
            { result.error = ERROR_NOT_SUPPORTED; return result; }
            auto source_parent = detail::AcquireDirectoryLease(request.source_parent, request.source_parent_identity, cancelled);
            if (source_parent.error) { result.error = source_parent.error; return result; }
            auto destination_parent = detail::AcquireDirectoryLease(request.destination_parent, request.destination_parent_identity, cancelled);
            if (destination_parent.error) { result.error = destination_parent.error; return result; }
            result.source_path = blink::core::JoinPath(source_parent.path, request.source.name);
            result.path = blink::core::JoinPath(destination_parent.path, request.destination_name);
            if (result.source_path.size() + 4 >= 32767 || result.path.size() + 4 >= 32767)
            { result.error = ERROR_FILENAME_EXCED_RANGE; return result; }
            const auto source_path = blink::core::JoinPath(source_parent.native_path, request.source.name);
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_DISALLOW_PATH_REDIRECTS;
            // Metadata-only first: validate actual identity before an ancestor
            // decision, including when the target lease contains the source root.
            blink::core::UniqueHandle probe(CreateFile3(source_path.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, &parameters));
            if (!probe) { result.error = GetLastError(); return result; }
            if (const auto error = CheckObject(probe.get(), &request.source.identity, request.source.is_directory, true); error != ERROR_SUCCESS)
            { result.error = error; return result; }
            if (request.source.identity.volume_serial != request.destination_parent_identity.volume_serial)
            {
                probe.reset();
                return request.source.is_directory ? MoveDirectoryAcrossVolumesSafely(request, cancelled, progress) :
                    MoveFileAcrossVolumesSafely(request, cancelled, progress);
            }
            if (request.source.is_directory)
            {
                for (const auto& ancestor : destination_parent.handles)
                {
                    blink::core::FileIdentity identity;
                    if (!blink::core::ReadFileIdentity(ancestor.get(), identity)) { result.error = GetLastError(); return result; }
                    if (identity == request.source.identity) { result.error = ERROR_INVALID_PARAMETER; return result; }
                }
            }
            if (const auto error = CheckChain(source_parent); error != ERROR_SUCCESS) { result.error = error; return result; }
            if (const auto error = CheckChain(destination_parent); error != ERROR_SUCCESS) { result.error = error; return result; }
            if (Stopping(cancelled)) { result.error = ERROR_CANCELLED; return result; }
            if (request.source_parent_identity == request.destination_parent_identity && request.source.name == request.destination_name)
            { result.error = ERROR_SUCCESS; result.unchanged = true; return result; }
            probe.reset();
            // Reopen for DELETE and recheck the same captured source ID across
            // the metadata-to-mutation transition. Parent leases remain held.
            blink::core::UniqueHandle source(CreateFile3(source_path.c_str(), DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE |
                (request.source.is_directory ? FILE_LIST_DIRECTORY : 0), FILE_SHARE_READ, OPEN_EXISTING, &parameters));
            if (!source) { result.error = GetLastError(); return result; }
            if (const auto error = CheckObject(source.get(), &request.source.identity, request.source.is_directory, true); error != ERROR_SUCCESS)
            { result.error = error; return result; }
            blink::core::FileIdentity replaced_identity;
            if (replacing)
            {
                if (const auto error = detail::ReadFileReplacementTarget(destination_parent, request.destination_name, request.source.identity, replaced_identity); error)
                { result.error = error; return result; }
            }
            MoveProgress update{ MovePhase::Preparing, result.source_path, result.path };
            if (!Report(progress, update)) { result.error = ERROR_UNHANDLED_EXCEPTION; return result; }
            const auto prefix = destination_parent.path.size() + 1 + 4;
            if (prefix >= 32766) { result.error = ERROR_FILENAME_EXCED_RANGE; return result; }
            const auto maximum = (std::min)(std::size_t{ 255 }, std::size_t{ 32766 } - prefix);
            for (unsigned number = 1; number <= 1000; ++number)
            {
                const auto name = number == 1 ? request.destination_name : NumberedName(request.destination_name, number, maximum, request.source.is_directory);
                if (name.empty() || name.size() > maximum) { result.error = ERROR_FILENAME_EXCED_RANGE; return result; }
                result.path = blink::core::JoinPath(destination_parent.path, name);
                update.destination_path = result.path; update.phase = MovePhase::Committing;
                if (!Report(progress, update)) { result.error = ERROR_UNHANDLED_EXCEPTION; return result; }
                if (Stopping(cancelled)) { result.error = ERROR_CANCELLED; return result; }
                if (const auto error = CheckObject(source.get(), &request.source.identity, request.source.is_directory, true); error != ERROR_SUCCESS)
                { result.error = error; return result; }
                if (const auto error = CheckChain(source_parent); error != ERROR_SUCCESS) { result.error = error; return result; }
                if (const auto error = CheckChain(destination_parent); error != ERROR_SUCCESS) { result.error = error; return result; }
                if (replacing)
                {
                    blink::core::FileIdentity current;
                    const auto error = detail::ReadFileReplacementTarget(destination_parent, name, request.source.identity, current);
                    if (error || current != replaced_identity) { result.error = error ? error : ERROR_FILE_INVALID; return result; }
                }
                const auto error = native ? native(source.get(), destination_parent.handles.back().get(), name) :
                    detail::RenameRelativeByHandle(source.get(), destination_parent.handles.back().get(), name, replacing);
                if (!error)
                {
                    // Paths were allocated before native rename. No subsequent
                    // observer, warning or late stop can reverse this fact.
                    result.moved = true; result.error = ERROR_SUCCESS;
                    result.post_commit_error = CheckChain(destination_parent);
                    if (!result.post_commit_error) { result.post_commit_error = CheckObject(source.get(), &request.source.identity, request.source.is_directory, true); }
                    update.phase = MovePhase::Completed; (void)Report(progress, update); return result;
                }
                result.error = error;
                if (request.conflict_policy != FileCopyConflictPolicy::KeepBoth ||
                    (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)) { return result; }
            }
        }
        catch (const std::bad_alloc&) { if (!result.moved) { result.error = ERROR_NOT_ENOUGH_MEMORY; } }
        catch (...) { if (!result.moved) { result.error = ERROR_GEN_FAILURE; } }
        return result;
    }
}
