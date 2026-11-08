#include "Blink/App/DirectoryCopyOperation.h"
#include "Blink/App/DirectoryLease.h"
#include "Blink/App/HandleRename.h"
#include "Blink/App/MoveOperation.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>

namespace blink::app
{
    namespace
    {
        constexpr DWORD OrdinaryAttributes = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
            FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL |
            FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
        constexpr DWORD DirectoryAttributes = OrdinaryAttributes | FILE_ATTRIBUTE_DIRECTORY;
        constexpr DWORD ReadWriteSharing = FILE_SHARE_READ | FILE_SHARE_WRITE;
        constexpr DWORD SourceDirectoryAccess = FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES;
        constexpr DWORD DirectoryOpenFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_DISALLOW_PATH_REDIRECTS;
        constexpr std::size_t MaximumStreamInformationBytes = 1024 * 1024;
        constexpr std::size_t MaximumFileStreams = 256;
        bool Stopping(const FileOperationCancellation& cancelled) noexcept { return cancelled && cancelled->load(); }
        DWORD LastError() noexcept { const auto error = GetLastError(); return error ? error : ERROR_GEN_FAILURE; }
        std::wstring Native(const std::wstring& path) { return path.starts_with(L"\\\\?\\") ? path : L"\\\\?\\" + path; }
        std::wstring LocalParent(std::wstring path)
        {
            if (path.starts_with(L"\\\\?\\")) { path.erase(0, 4); }
            if (path.size() < 3 || path.size() > 32760 || path[1] != L':' || path[2] != L'\\' ||
                !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
                path.find(L':', 2) != std::wstring::npos || path.find(L'/') != std::wstring::npos ||
                path.find(L'\0') != std::wstring::npos) { return {}; }
            while (path.size() > 3 && path.back() == L'\\') { path.pop_back(); }
            std::size_t depth{};
            for (std::size_t offset = 3; offset < path.size();)
            {
                const auto end = path.find(L'\\', offset);
                const auto part = std::wstring_view(path).substr(offset, end == std::wstring::npos ? path.size() - offset : end - offset);
                if (part.empty() || part == L"." || part == L".." || ++depth > MaximumDirectoryCopyDepth) { return {}; }
                if (end == std::wstring::npos) { break; }
                offset = end + 1;
            }
            return path;
        }
        struct Budget
        {
            std::size_t limit{}, used{};
            bool Add(const std::size_t bytes) noexcept
            { if (bytes > limit - used) { return false; } used += bytes; return true; }
            bool String(const std::wstring& value) noexcept
            { return Add((value.capacity() + 1) * sizeof(wchar_t)); }
        };
        struct BudgetRestore
        {
            Budget& budget; std::size_t previous;
            ~BudgetRestore() { budget.used = previous; }
        };
        struct PlannedDirectory
        {
            std::wstring source_path, destination_path;
            blink::core::FileIdentity source_identity, destination_identity;
            FILE_BASIC_INFO basic{};
            blink::core::UniqueHandle source, destination, destination_metadata;
            std::vector<blink::core::FileEntry> children;
            std::vector<bool> removed_children;
            std::size_t depth{}, parent{}, parent_child{};
            bool merged_existing{}, skipped{};
        };
        struct PlannedFile { std::size_t parent{}, child{}; };
        bool LessName(const blink::core::FileEntry& left, const blink::core::FileEntry& right) noexcept
        { return CompareStringOrdinal(left.name.c_str(), -1, right.name.c_str(), -1, TRUE) == CSTR_LESS_THAN; }
        bool EqualName(const std::wstring& left, const std::wstring& right) noexcept
        { return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL; }
        DWORD CheckDirectoryStreams(HANDLE directory)
        {
            std::array<ULONGLONG, 512> storage{};
            if (GetFileInformationByHandleEx(directory, FileStreamInfo, storage.data(), static_cast<DWORD>(sizeof(storage))))
            { return ERROR_NOT_SUPPORTED; } // A directory stream list, including an empty named stream, is not copied.
            const auto error = GetLastError();
            // This exact no-stream outcome is tied to the owned ordinary-NTFS
            // capability probe; every other outcome remains unsupported.
            return error == ERROR_HANDLE_EOF ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
        }
        DWORD CheckDirectory(HANDLE handle, const blink::core::FileIdentity* expected,
            const bool ordinary, const bool streams, FILE_BASIC_INFO* basic = nullptr)
        {
            FILE_ATTRIBUTE_TAG_INFO tag{}; FILE_STANDARD_INFO standard{}; blink::core::FileIdentity identity;
            if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) ||
                !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
                !blink::core::ReadFileIdentity(handle, identity)) { return LastError(); }
            if (!(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !standard.Directory || standard.DeletePending)
            { return ERROR_FILE_INVALID; }
            if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || tag.ReparseTag ||
                (ordinary && (tag.FileAttributes & ~DirectoryAttributes))) { return ERROR_NOT_SUPPORTED; }
            if (expected && identity != *expected) { return ERROR_FILE_INVALID; }
            if (basic && !GetFileInformationByHandleEx(handle, FileBasicInfo, basic, sizeof(*basic))) { return LastError(); }
            return streams ? CheckDirectoryStreams(handle) : ERROR_SUCCESS;
        }
        DWORD CheckChain(const detail::DirectoryLease& lease)
        {
            for (const auto& handle : lease.handles)
            { if (const auto error = CheckDirectory(handle.get(), nullptr, false, false); error != ERROR_SUCCESS) { return error; } }
            return ERROR_SUCCESS;
        }
        DWORD OpenSourceDirectory(PlannedDirectory& directory, const FileOperationCancellation& cancelled)
        {
            if (Stopping(cancelled)) { return ERROR_CANCELLED; }
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters); parameters.dwFileFlags = DirectoryOpenFlags;
            directory.source = blink::core::UniqueHandle(CreateFile3(Native(directory.source_path).c_str(), SourceDirectoryAccess,
                FILE_SHARE_READ, OPEN_EXISTING, &parameters));
            if (!directory.source) { return LastError(); }
            return CheckDirectory(directory.source.get(), &directory.source_identity, true, true, &directory.basic);
        }
        DWORD ReadChildren(PlannedDirectory& directory, const std::size_t maximum_entries,
            const std::size_t maximum_depth, Budget& budget, const FileOperationCancellation& cancelled,
            std::vector<blink::core::FileEntry>& children)
        {
            std::array<ULONGLONG, 8192> storage{};
            bool first = true;
            for (;;)
            {
                if (Stopping(cancelled)) { return ERROR_CANCELLED; }
                std::fill(storage.begin(), storage.end(), 0);
                const bool read = GetFileInformationByHandleEx(directory.source.get(),
                    first ? FileIdExtdDirectoryRestartInfo : FileIdExtdDirectoryInfo,
                    storage.data(), static_cast<DWORD>(sizeof(storage))) != FALSE;
                if (!read)
                {
                    const auto error = GetLastError();
                    if (error == ERROR_NO_MORE_FILES) { break; }
                    return error == ERROR_INVALID_PARAMETER || error == ERROR_INVALID_FUNCTION || error == ERROR_INVALID_LEVEL ?
                        ERROR_NOT_SUPPORTED : error;
                }
                first = false;
                const auto* bytes = reinterpret_cast<const std::byte*>(storage.data());
                std::size_t offset{};
                for (;;)
                {
                    constexpr auto header = offsetof(FILE_ID_EXTD_DIR_INFO, FileName);
                    if (offset > sizeof(storage) || sizeof(storage) - offset < header) { return ERROR_INVALID_DATA; }
                    const auto* info = reinterpret_cast<const FILE_ID_EXTD_DIR_INFO*>(bytes + offset);
                    if (!info->FileNameLength || info->FileNameLength % sizeof(wchar_t) ||
                        info->FileNameLength > sizeof(storage) - offset - header) { return ERROR_INVALID_DATA; }
                    std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
                    if (name != L"." && name != L"..")
                    {
                        if (children.size() >= maximum_entries || directory.depth + 1 > maximum_depth) { return ERROR_NOT_SUPPORTED; }
                        if (const auto error = ValidateNewFolderName(name); error != ERROR_SUCCESS) { return error; }
                        if ((info->FileAttributes & ~DirectoryAttributes) || (info->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                            (!(info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (info->FileAttributes & ~OrdinaryAttributes)))
                        { return ERROR_NOT_SUPPORTED; }
                        blink::core::FileEntry entry; entry.name = std::move(name);
                        entry.is_directory = (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                        entry.attributes = info->FileAttributes;
                        // ReparsePointTag is undefined for non-reparse entries.
                        entry.reparse_tag = 0;
                        entry.identity = { directory.source_identity.volume_serial, {}, true };
                        std::copy(std::begin(info->FileId.Identifier), std::end(info->FileId.Identifier), entry.identity.file_id.begin());
                        if (info->EndOfFile.QuadPart < 0) { return ERROR_INVALID_DATA; }
                        entry.size = static_cast<std::uint64_t>(info->EndOfFile.QuadPart);
                        if (!budget.String(entry.name)) { return ERROR_NOT_SUPPORTED; }
                        children.push_back(std::move(entry));
                    }
                    if (!info->NextEntryOffset) { break; }
                    if (info->NextEntryOffset < header + info->FileNameLength || info->NextEntryOffset % alignof(LONGLONG) ||
                        info->NextEntryOffset > sizeof(storage) - offset) { return ERROR_INVALID_DATA; }
                    offset += info->NextEntryOffset;
                }
            }
            std::sort(children.begin(), children.end(), LessName);
            for (std::size_t index = 1; index < children.size(); ++index)
            { if (EqualName(children[index - 1].name, children[index].name)) { return ERROR_NOT_SUPPORTED; } }
            return ERROR_SUCCESS;
        }
        DWORD VerifyInventory(PlannedDirectory& directory, const DirectoryCopyLimits& limits,
            Budget& budget, const FileOperationCancellation& cancelled)
        {
            if (const auto error = CheckDirectory(directory.source.get(), &directory.source_identity, true, true); error != ERROR_SUCCESS)
            { return error; }
            const BudgetRestore restore{ budget, budget.used };
            std::vector<blink::core::FileEntry> current;
            if (const auto error = ReadChildren(directory, limits.max_entries, limits.max_depth, budget, cancelled, current); error != ERROR_SUCCESS)
            { return error; }
            if (current.size() != directory.children.size()) { return ERROR_FILE_INVALID; }
            for (std::size_t index = 0; index < current.size(); ++index)
            {
                if (current[index].name != directory.children[index].name || current[index].identity != directory.children[index].identity ||
                    current[index].is_directory != directory.children[index].is_directory) { return ERROR_FILE_INVALID; }
            }
            return ERROR_SUCCESS;
        }
        DWORD VerifyMoveInventory(PlannedDirectory& directory, const DirectoryCopyLimits& limits,
            Budget& budget, const FileOperationCancellation& cancelled, detail::DirectoryMoveCopyResult& moving)
        {
            if (const auto error = CheckDirectory(directory.source.get(), &directory.source_identity, true, true); error)
            { return error; }
            const BudgetRestore restore{ budget, budget.used };
            std::vector<blink::core::FileEntry> current;
            if (const auto error = ReadChildren(directory, limits.max_entries, limits.max_depth, budget, cancelled, current); error)
            { return error; }
            const auto expected = static_cast<std::size_t>(std::count(directory.removed_children.begin(), directory.removed_children.end(), false));
            if (current.size() > expected) { moving.source_items_remaining += current.size() - expected; }
            if (current.size() != expected) { return ERROR_FILE_INVALID; }
            std::size_t next{};
            for (std::size_t index = 0; index < directory.children.size(); ++index)
            {
                if (directory.removed_children[index]) { continue; }
                const auto& entry = directory.children[index];
                if (current[next].name != entry.name || current[next].identity != entry.identity ||
                    current[next].is_directory != entry.is_directory) { return ERROR_FILE_INVALID; }
                ++next;
            }
            return ERROR_SUCCESS;
        }
        DWORD ValidateFileStreams(HANDLE file)
        {
            std::vector<ULONGLONG> storage(512);
            for (;;)
            {
                if (GetFileInformationByHandleEx(file, FileStreamInfo, storage.data(), static_cast<DWORD>(storage.size() * sizeof(ULONGLONG)))) { break; }
                const auto error = GetLastError();
                if (error != ERROR_MORE_DATA && error != ERROR_INSUFFICIENT_BUFFER) { return ERROR_NOT_SUPPORTED; }
                if (storage.size() * sizeof(ULONGLONG) >= MaximumStreamInformationBytes) { return ERROR_NOT_SUPPORTED; }
                storage.resize((std::min)(storage.size() * 2, MaximumStreamInformationBytes / sizeof(ULONGLONG)));
            }
            const auto capacity = storage.size() * sizeof(ULONGLONG); std::size_t offset{}, count{}; bool default_stream{};
            const auto* bytes = reinterpret_cast<const std::byte*>(storage.data());
            for (;;)
            {
                constexpr auto header = offsetof(FILE_STREAM_INFO, StreamName);
                if (offset > capacity || capacity - offset < header) { return ERROR_INVALID_DATA; }
                const auto* info = reinterpret_cast<const FILE_STREAM_INFO*>(bytes + offset);
                if (++count > MaximumFileStreams || info->StreamNameLength % sizeof(wchar_t) ||
                    info->StreamNameLength > capacity - offset - header || info->StreamSize.QuadPart < 0) { return ERROR_NOT_SUPPORTED; }
                const std::wstring_view name(info->StreamName, info->StreamNameLength / sizeof(wchar_t));
                if (name == L"::$DATA") { default_stream = true; }
                else
                {
                    if (name.size() <= 7 || name.front() != L':' || !name.ends_with(L":$DATA")) { return ERROR_NOT_SUPPORTED; }
                    const auto leaf = name.substr(1, name.size() - 7);
                    if (leaf.empty() || leaf.find_first_of(L"\\/:") != std::wstring_view::npos || leaf.find(L'\0') != std::wstring_view::npos)
                    { return ERROR_NOT_SUPPORTED; }
                }
                if (!info->NextEntryOffset) { break; }
                if (info->NextEntryOffset < header + info->StreamNameLength || info->NextEntryOffset % alignof(LONGLONG) ||
                    info->NextEntryOffset > capacity - offset) { return ERROR_INVALID_DATA; }
                offset += info->NextEntryOffset;
            }
            return default_stream ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
        }
        DWORD ValidateSourceFile(const PlannedDirectory& parent, const blink::core::FileEntry& entry)
        {
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = DirectoryOpenFlags;
            blink::core::UniqueHandle file(CreateFile3(Native(blink::core::JoinPath(parent.source_path, entry.name)).c_str(),
                GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, &parameters));
            if (!file) { return LastError(); }
            FILE_ATTRIBUTE_TAG_INFO tag{}; FILE_STANDARD_INFO standard{}; blink::core::FileIdentity identity;
            if (!GetFileInformationByHandleEx(file.get(), FileAttributeTagInfo, &tag, sizeof(tag)) ||
                !GetFileInformationByHandleEx(file.get(), FileStandardInfo, &standard, sizeof(standard)) ||
                !blink::core::ReadFileIdentity(file.get(), identity)) { return LastError(); }
            if ((tag.FileAttributes & ~OrdinaryAttributes) || tag.ReparseTag || standard.Directory || standard.DeletePending)
            { return ERROR_NOT_SUPPORTED; }
            if (identity != entry.identity) { return ERROR_FILE_INVALID; }
            return ValidateFileStreams(file.get());
        }
        DWORD CheckTarget(PlannedDirectory& directory)
        { return CheckDirectory(directory.destination.get(), &directory.destination_identity,
            !directory.merged_existing, !directory.merged_existing); }
        bool NameConflict(const DWORD error) noexcept
        { return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS; }
        DWORD ReadDestination(const std::wstring& path, bool& exists, bool& directory)
        {
            exists = directory = false;
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = DirectoryOpenFlags;
            blink::core::UniqueHandle target(CreateFile3(Native(path).c_str(), FILE_READ_ATTRIBUTES,
                ReadWriteSharing, OPEN_EXISTING, &parameters));
            if (!target)
            {
                const auto error = LastError();
                return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ERROR_SUCCESS : error;
            }
            FILE_ATTRIBUTE_TAG_INFO tag{};
            if (!GetFileInformationByHandleEx(target.get(), FileAttributeTagInfo, &tag, sizeof(tag))) { return LastError(); }
            if ((tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || tag.ReparseTag) { return ERROR_NOT_SUPPORTED; }
            exists = true; directory = (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            return ERROR_SUCCESS;
        }
        DWORD OpenMergedDirectory(PlannedDirectory& directory, const std::vector<PlannedDirectory>& plan)
        {
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = DirectoryOpenFlags;
            directory.destination = blink::core::UniqueHandle(CreateFile3(Native(directory.destination_path).c_str(),
                FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE, ReadWriteSharing, OPEN_EXISTING, &parameters));
            if (!directory.destination) { return LastError(); }
            if (!blink::core::ReadFileIdentity(directory.destination.get(), directory.destination_identity)) { return LastError(); }
            if (std::any_of(plan.begin(), plan.end(), [&directory](const PlannedDirectory& source) {
                return source.source_identity == directory.destination_identity;
            })) { return ERROR_INVALID_PARAMETER; }
            directory.merged_existing = true;
            return CheckTarget(directory);
        }
        DWORD ResolveConflict(const DirectoryCopyRequest& request, DirectoryCopyConflict conflict,
            const FileOperationCancellation& cancelled, FileCopyConflictPolicy& policy)
        {
            if (Stopping(cancelled)) { return ERROR_CANCELLED; }
            if (!request.resolve_conflict) { return ERROR_ALREADY_EXISTS; }
            try { policy = request.resolve_conflict(conflict); }
            catch (...) { return ERROR_UNHANDLED_EXCEPTION; }
            if (Stopping(cancelled) || policy == FileCopyConflictPolicy::Cancel) { return ERROR_CANCELLED; }
            if (policy == FileCopyConflictPolicy::Fail) { return ERROR_ALREADY_EXISTS; }
            if (policy == FileCopyConflictPolicy::Skip || policy == FileCopyConflictPolicy::KeepBoth) { return ERROR_SUCCESS; }
            if (policy == FileCopyConflictPolicy::Replace && !conflict.request.source.is_directory && !conflict.destination_is_directory)
            { return ERROR_SUCCESS; }
            if (policy == FileCopyConflictPolicy::Merge && conflict.request.source.is_directory && conflict.destination_is_directory)
            { return ERROR_SUCCESS; }
            return ERROR_NOT_SUPPORTED;
        }
        DWORD CreateOwnedDirectory(PlannedDirectory& directory, DirectoryCopyResult& result, const bool native_files)
        {
            // Fixed, already supported create shape; no release/reopen ownership
            // gap. The separate metadata handle must refer to this same ID.
            directory.destination = blink::core::UniqueHandle(CreateDirectory2W(Native(directory.destination_path).c_str(),
                FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                FILE_SHARE_READ, DIRECTORY_FLAGS_DISALLOW_PATH_REDIRECTS, nullptr));
            if (!directory.destination) { return LastError(); }
            ++result.directories_created; result.partial_tree_left = true;
            if (!blink::core::ReadFileIdentity(directory.destination.get(), directory.destination_identity)) { return LastError(); }
            if (const auto error = CheckTarget(directory); error != ERROR_SUCCESS) { return error; }
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters); parameters.dwFileFlags = DirectoryOpenFlags;
            directory.destination_metadata = blink::core::UniqueHandle(CreateFile3(Native(directory.destination_path).c_str(),
                FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, ReadWriteSharing, OPEN_EXISTING, &parameters));
            if (!directory.destination_metadata) { return LastError(); }
            if (const auto error = CheckDirectory(directory.destination_metadata.get(), &directory.destination_identity, true, true); error)
            { return error; }
            if (native_files)
            {
                // Keep the original LIST_DIRECTORY guard until its read-only
                // replacement is open and checked. Only the replacement's
                // sharing changes; a LIST guard is held throughout conversion.
                blink::core::UniqueHandle list(CreateFile3(Native(directory.destination_path).c_str(),
                    FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                    ReadWriteSharing, OPEN_EXISTING, &parameters));
                if (!list) { return LastError(); }
                if (const auto error = CheckDirectory(list.get(), &directory.destination_identity, true, true); error)
                { return error; }
                directory.destination = std::move(list);
            }
            return ERROR_SUCCESS;
        }
        std::wstring KeepBothDirectoryName(const std::wstring& original, unsigned number, std::size_t maximum)
        {
            const auto suffix = L" (" + std::to_wstring(number) + L")";
            if (suffix.size() >= maximum) { return {}; }
            auto stem = original.substr(0, maximum - suffix.size());
            if (!stem.empty() && stem.back() >= 0xd800 && stem.back() <= 0xdbff) { stem.pop_back(); }
            return stem.empty() ? std::wstring{} : stem + suffix;
        }
        DWORD RebindRoot(std::vector<PlannedDirectory>& directories, DirectoryCopyResult& result,
            Budget& budget, const std::wstring& parent, const std::wstring& name,
            std::size_t maximum_suffix, std::size_t& maximum_path)
        {
            // Allocate every replacement before any create. Peak old+new path
            // storage and larger progress/native scratch remain budgeted.
            auto next_root = blink::core::JoinPath(parent, name);
            const auto old_root_size = directories.front().destination_path.size();
            const auto next_maximum = (std::max)(maximum_path, next_root.size() + maximum_suffix + 4);
            Budget prepared = budget;
            if (!prepared.String(next_root) || !prepared.Add(8 * (next_maximum - maximum_path) * sizeof(wchar_t)))
            { return ERROR_NOT_SUPPORTED; }
            std::size_t old_bytes = (result.path.capacity() + 1) * sizeof(wchar_t);
            std::vector<std::wstring> replacements; replacements.reserve(directories.size());
            for (const auto& directory : directories)
            {
                std::wstring path;
                path.reserve(next_root.size() + directory.destination_path.size() - old_root_size);
                path.assign(next_root); path.append(directory.destination_path, old_root_size, std::wstring::npos);
                if (path.size() + 4 >= 32767) { return ERROR_FILENAME_EXCED_RANGE; }
                if (!prepared.String(path)) { return ERROR_NOT_SUPPORTED; }
                old_bytes += (directory.destination_path.capacity() + 1) * sizeof(wchar_t);
                replacements.push_back(std::move(path));
            }
            if (old_bytes > budget.used) { return ERROR_INVALID_DATA; }
            for (std::size_t index = 0; index < directories.size(); ++index)
            { directories[index].destination_path.swap(replacements[index]); }
            result.path.swap(next_root);
            budget.used = prepared.used - old_bytes; maximum_path = next_maximum;
            return ERROR_SUCCESS;
        }
        bool WithinSubtree(const std::vector<PlannedDirectory>& directories, std::size_t index, const std::size_t root) noexcept
        {
            while (index > root) { index = directories[index].parent; }
            return index == root;
        }
        DWORD RebindSubtree(std::vector<PlannedDirectory>& directories, const std::size_t root,
            DirectoryCopyResult& result, Budget& budget, const std::wstring& parent, const std::wstring& name,
            const std::size_t maximum_suffix, std::size_t& maximum_path)
        {
            if (!root) { return RebindRoot(directories, result, budget, parent, name, maximum_suffix, maximum_path); }
            const auto next_root = blink::core::JoinPath(parent, name);
            const auto previous_size = directories[root].destination_path.size();
            const auto next_maximum = (std::max)(maximum_path, next_root.size() + maximum_suffix + 4);
            Budget prepared = budget;
            if (!prepared.Add(8 * (next_maximum - maximum_path) * sizeof(wchar_t))) { return ERROR_NOT_SUPPORTED; }
            std::vector<std::pair<std::size_t, std::wstring>> replacements;
            std::size_t old_bytes{};
            for (std::size_t index = root; index < directories.size(); ++index)
            {
                if (!WithinSubtree(directories, index, root)) { continue; }
                const auto& directory = directories[index];
                if (directory.destination) { return ERROR_INVALID_DATA; }
                auto path = next_root + directory.destination_path.substr(previous_size);
                if (path.size() + 4 >= 32767) { return ERROR_FILENAME_EXCED_RANGE; }
                if (!prepared.String(path)) { return ERROR_NOT_SUPPORTED; }
                old_bytes += (directory.destination_path.capacity() + 1) * sizeof(wchar_t);
                replacements.emplace_back(index, std::move(path));
            }
            if (old_bytes > budget.used) { return ERROR_INVALID_DATA; }
            for (auto& replacement : replacements) { directories[replacement.first].destination_path.swap(replacement.second); }
            budget.used = prepared.used - old_bytes; maximum_path = next_maximum;
            return ERROR_SUCCESS;
        }
        bool Report(const DirectoryCopyProgressCallback& callback, const DirectoryCopyProgress& progress) noexcept
        { if (!callback) { return true; } try { callback(progress); return true; } catch (...) { return false; } }
    }

    static DirectoryCopyResult CopyDirectoryImpl(const DirectoryCopyRequest& request,
        const FileOperationCancellation& cancelled, const DirectoryCopyProgressCallback& progress,
        detail::DirectoryMoveCopyResult* moving = nullptr, const std::function<DWORD()>& before_removal = {}) noexcept
    {
        DirectoryCopyResult result;
        const auto report = [&result, &progress](DirectoryCopyProgress& update) noexcept {
            update.files_skipped = result.files_skipped; update.directories_skipped = result.directories_skipped;
            return Report(progress, update);
        };
        const auto fail = [&result, moving](const DWORD error, const std::wstring& path) noexcept {
            if (moving && result.copied)
            {
                moving->removal_error = error ? error : ERROR_GEN_FAILURE;
                try { result.failure_path = path; } catch (...) {}
                return; // A committed destination tree stays a successful copy.
            }
            result.error = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
            result.copied = false;
            result.partial_tree_left = result.partial_tree_left || result.directories_created != 0 || result.files_copied != 0;
            try { result.failure_path = path; } catch (...) {}
        };
        try
        {
            if (request.conflict_policy == FileCopyConflictPolicy::Skip)
            {
                result.path = blink::core::JoinPath(request.destination_parent, request.destination_name);
                result.error = ERROR_SUCCESS; result.skipped = true; return result;
            }
            if (request.conflict_policy == FileCopyConflictPolicy::Cancel) { fail(ERROR_CANCELLED, {}); return result; }
            if (request.conflict_policy == FileCopyConflictPolicy::Replace) { fail(ERROR_NOT_SUPPORTED, {}); return result; }
            const bool merging = request.conflict_policy == FileCopyConflictPolicy::Merge;
            if (request.conflict_policy != FileCopyConflictPolicy::Fail && request.conflict_policy != FileCopyConflictPolicy::KeepBoth && !merging)
            { fail(ERROR_INVALID_PARAMETER, {}); return result; }
            if (request.limits.max_entries > MaximumDirectoryCopyEntries || !request.limits.max_directories ||
                request.limits.max_directories > MaximumDirectoryCopyDirectories || request.limits.max_depth > MaximumDirectoryCopyDepth ||
                request.limits.max_path_bytes > MaximumDirectoryCopyPathBytes)
            { fail(ERROR_INVALID_PARAMETER, {}); return result; }
            if (const auto error = ValidateNewFolderName(request.source.name); error != ERROR_SUCCESS) { fail(error, {}); return result; }
            if (const auto error = ValidateNewFolderName(request.destination_name); error != ERROR_SUCCESS) { fail(error, {}); return result; }
            if (!request.source.is_directory || (request.source.attributes & ~DirectoryAttributes) ||
                (request.source.attributes & FILE_ATTRIBUTE_REPARSE_POINT)) { fail(ERROR_NOT_SUPPORTED, {}); return result; }
            if (!request.source.identity.valid || !request.source_parent_identity.valid || !request.destination_parent_identity.valid ||
                request.source.identity.volume_serial != request.source_parent_identity.volume_serial)
            { fail(ERROR_FILE_INVALID, {}); return result; }
            const auto source_parent_path = LocalParent(request.source_parent), destination_parent_path = LocalParent(request.destination_parent);
            if (source_parent_path.empty() || destination_parent_path.empty()) { fail(ERROR_NOT_SUPPORTED, {}); return result; }
            if (Stopping(cancelled)) { fail(ERROR_CANCELLED, {}); return result; }
            auto source_parent = detail::AcquireDirectoryLease(source_parent_path, request.source_parent_identity, cancelled);
            if (source_parent.error != ERROR_SUCCESS) { fail(source_parent.error, source_parent_path); return result; }
            auto destination_parent = detail::AcquireDirectoryLease(destination_parent_path, request.destination_parent_identity, cancelled);
            if (destination_parent.error != ERROR_SUCCESS) { fail(destination_parent.error, destination_parent_path); return result; }
            const bool native_files = moving && merging &&
                request.source.identity.volume_serial == request.destination_parent_identity.volume_serial;
            result.path = blink::core::JoinPath(destination_parent.path, request.destination_name);
            Budget budget{ request.limits.max_path_bytes };
            if (!budget.String(result.path) || !budget.String(source_parent_path) || !budget.String(destination_parent_path) ||
                !budget.String(source_parent.path) || !budget.String(source_parent.native_path) ||
                !budget.String(destination_parent.path) || !budget.String(destination_parent.native_path))
            { fail(ERROR_NOT_SUPPORTED, {}); return result; }
            std::vector<PlannedDirectory> directories; directories.reserve(request.limits.max_directories);
            std::vector<PlannedFile> files; files.reserve(request.limits.max_entries);
            PlannedDirectory root;
            root.source_path = blink::core::JoinPath(source_parent.path, request.source.name); root.destination_path = result.path;
            root.source_identity = request.source.identity;
            if (root.source_path.size() + 4 >= 32767 || root.destination_path.size() + 4 >= 32767)
            { fail(ERROR_FILENAME_EXCED_RANGE, result.path); return result; }
            if (!budget.String(root.source_path) || !budget.String(root.destination_path)) { fail(ERROR_NOT_SUPPORTED, {}); return result; }
            if (const auto error = OpenSourceDirectory(root, cancelled); error != ERROR_SUCCESS) { fail(error, root.source_path); return result; }
            // Validate the source object before using its supplied identity
            // in ancestry decisions. A wrong expected ID may name an ancestor.
            const bool same_source_root = request.source_parent_identity == request.destination_parent_identity &&
                EqualName(request.source.name, request.destination_name);
            if (same_source_root && request.conflict_policy != FileCopyConflictPolicy::KeepBoth)
            { fail(ERROR_INVALID_PARAMETER, {}); return result; }
            for (const auto& ancestor : destination_parent.handles)
            {
                blink::core::FileIdentity identity;
                if (!blink::core::ReadFileIdentity(ancestor.get(), identity)) { fail(LastError(), destination_parent.path); return result; }
                if (identity == request.source.identity) { fail(ERROR_INVALID_PARAMETER, destination_parent.path); return result; }
            }
            directories.push_back(std::move(root));
            std::size_t entries{}, maximum_path = (std::max)(directories[0].source_path.size(), directories[0].destination_path.size()) + 4;
            for (std::size_t index = 0; index < directories.size(); ++index)
            {
                auto& directory = directories[index];
                if (const auto error = ReadChildren(directory, request.limits.max_entries - entries, request.limits.max_depth,
                    budget, cancelled, directory.children); error != ERROR_SUCCESS) { fail(error, directory.source_path); return result; }
                if (moving) { directory.removed_children.resize(directory.children.size()); }
                entries += directory.children.size();
                for (std::size_t child = 0; child < directory.children.size(); ++child)
                {
                    if (Stopping(cancelled)) { fail(ERROR_CANCELLED, directory.source_path); return result; }
                    const auto& entry = directory.children[child];
                    const auto source_path = blink::core::JoinPath(directory.source_path, entry.name);
                    const auto destination_path = blink::core::JoinPath(directory.destination_path, entry.name);
                    if (source_path.size() + 4 >= 32767 || destination_path.size() + 4 >= 32767)
                    { fail(ERROR_FILENAME_EXCED_RANGE, source_path); return result; }
                    maximum_path = (std::max)({ maximum_path, source_path.size() + 4, destination_path.size() + 4 });
                    if (entry.is_directory)
                    {
                        if (directories.size() >= request.limits.max_directories || std::any_of(directories.begin(), directories.end(),
                            [&entry](const PlannedDirectory& candidate) { return candidate.source_identity == entry.identity; }))
                        { fail(ERROR_NOT_SUPPORTED, source_path); return result; }
                        PlannedDirectory next; next.source_path = source_path; next.destination_path = destination_path;
                        next.source_identity = entry.identity; next.depth = directory.depth + 1; next.parent = index; next.parent_child = child;
                        if (!budget.String(next.source_path) || !budget.String(next.destination_path)) { fail(ERROR_NOT_SUPPORTED, source_path); return result; }
                        if (const auto error = OpenSourceDirectory(next, cancelled); error != ERROR_SUCCESS) { fail(error, source_path); return result; }
                        directories.push_back(std::move(next));
                    }
                    else
                    {
                        if (const auto error = ValidateSourceFile(directory, entry); error != ERROR_SUCCESS) { fail(error, source_path); return result; }
                        files.push_back({ index, child });
                    }
                }
            }
            // Budget temporary inventories as well as retained names/paths.
            // Reserve path copies used by progress, failure and native requests.
            if (moving)
            {
                moving->source_items_remaining = entries;
                moving->directory_relocations.reserve(directories.size());
            }
            if (!budget.Add(8 * (maximum_path + 1) * sizeof(wchar_t))) { fail(ERROR_NOT_SUPPORTED, result.path); return result; }
            for (auto& directory : directories)
            { if (const auto error = VerifyInventory(directory, request.limits, budget, cancelled); error != ERROR_SUCCESS)
                { fail(error, directory.source_path); return result; } }
            const auto prepare_merge_directory = [&](const std::size_t index) -> DWORD {
                auto& directory = directories[index];
                bool exists{}, target_directory{};
                if (const auto error = ReadDestination(directory.destination_path, exists, target_directory); error) { return error; }
                if (!exists)
                {
                    const auto error = CreateOwnedDirectory(directory, result, native_files);
                    if (!error || directory.destination || !NameConflict(error)) { return error; }
                    if (const auto checked = ReadDestination(directory.destination_path, exists, target_directory); checked) { return checked; }
                    if (!exists) { return error; }
                }
                if (target_directory) { return OpenMergedDirectory(directory, directories); }
                blink::core::FileEntry entry;
                entry.name = directory.source_path.substr(directory.source_path.find_last_of(L'\\') + 1);
                entry.identity = directory.source_identity; entry.is_directory = true;
                entry.attributes = directory.basic.FileAttributes;
                FileCopyRequest child{ index ? directories[directory.parent].source_path : source_parent.path,
                    index ? directories[directory.parent].source_identity : request.source_parent_identity, std::move(entry),
                    index ? directories[directory.parent].destination_path : destination_parent.path,
                    index ? directories[directory.parent].destination_identity : request.destination_parent_identity,
                    directory.destination_path.substr(directory.destination_path.find_last_of(L'\\') + 1) };
                FileCopyConflictPolicy policy{};
                if (const auto error = ResolveConflict(request, { child, false }, cancelled, policy); error) { return error; }
                if (policy == FileCopyConflictPolicy::Skip)
                { directory.skipped = true; ++result.directories_skipped; return ERROR_SUCCESS; }
                if (policy != FileCopyConflictPolicy::KeepBoth) { return ERROR_NOT_SUPPORTED; }
                std::size_t maximum_suffix{};
                const auto root_size = directory.destination_path.size();
                for (std::size_t current = index; current < directories.size(); ++current)
                {
                    if (!WithinSubtree(directories, current, index)) { continue; }
                    const auto& planned = directories[current];
                    maximum_suffix = (std::max)(maximum_suffix, planned.destination_path.size() - root_size);
                    for (const auto& file : planned.children)
                    { if (!file.is_directory) { maximum_suffix = (std::max)(maximum_suffix,
                        planned.destination_path.size() - root_size + 1 + file.name.size()); } }
                }
                const auto prefix = root_size - child.destination_name.size();
                const auto maximum_leaf = prefix + maximum_suffix < 32762 ?
                    (std::min)(std::size_t{ 255 }, std::size_t{ 32762 } - prefix - maximum_suffix) : 0;
                for (unsigned number = 2; number <= 1000; ++number)
                {
                    if (Stopping(cancelled)) { return ERROR_CANCELLED; }
                    const auto name = KeepBothDirectoryName(child.destination_name, number, maximum_leaf);
                    if (name.empty()) { return ERROR_FILENAME_EXCED_RANGE; }
                    if (const auto error = RebindSubtree(directories, index, result, budget, child.destination_parent,
                        name, maximum_suffix, maximum_path); error) { return error; }
                    const auto error = CreateOwnedDirectory(directory, result, native_files);
                    if (!error || directory.destination || !NameConflict(error)) { return error; }
                }
                return ERROR_ALREADY_EXISTS;
            };
            DirectoryCopyProgress update; update.current_source_path = directories[0].source_path; update.current_destination_path = result.path;
            if (!report(update)) { fail(ERROR_UNHANDLED_EXCEPTION, update.current_source_path); return result; }
            if (Stopping(cancelled)) { fail(ERROR_CANCELLED, update.current_source_path); return result; }
            for (std::size_t index = 0; index < directories.size(); ++index)
            {
                auto& directory = directories[index];
                if (Stopping(cancelled)) { fail(ERROR_CANCELLED, directory.destination_path); return result; }
                if (index && directories[directory.parent].skipped)
                { directory.skipped = true; ++result.directories_skipped; continue; }
                if (const auto error = CheckChain(source_parent); error != ERROR_SUCCESS) { fail(error, source_parent.path); return result; }
                if (const auto error = CheckChain(destination_parent); error != ERROR_SUCCESS) { fail(error, destination_parent.path); return result; }
                // Recheck every created directory before following its path to
                // create descendants; the guards are not a filesystem snapshot.
                for (auto& created : directories)
                { if (created.destination) { if (const auto error = CheckTarget(created); error != ERROR_SUCCESS)
                    { fail(error, created.destination_path); return result; } } }
                if (const auto error = CheckDirectory(directory.source.get(), &directory.source_identity, true, true); error != ERROR_SUCCESS)
                { fail(error, directory.source_path); return result; }
                auto error = merging ? prepare_merge_directory(index) :
                    &directory == &directories.front() && same_source_root ? ERROR_ALREADY_EXISTS : CreateOwnedDirectory(directory, result, native_files);
                if (&directory == &directories.front() && request.conflict_policy == FileCopyConflictPolicy::KeepBoth)
                {
                    std::size_t maximum_suffix{};
                    const auto initial_root_size = directory.destination_path.size();
                    for (const auto& planned : directories)
                    { maximum_suffix = (std::max)(maximum_suffix, planned.destination_path.size() - initial_root_size); }
                    for (const auto& file : files)
                    { maximum_suffix = (std::max)(maximum_suffix, directories[file.parent].destination_path.size() - initial_root_size +
                        1 + directories[file.parent].children[file.child].name.size()); }
                    const auto prefix = initial_root_size - request.destination_name.size();
                    const auto maximum_leaf = prefix + maximum_suffix < 32762 ?
                        (std::min)(std::size_t{ 255 }, std::size_t{ 32762 } - prefix - maximum_suffix) : 0;
                    for (unsigned number = 2; number <= 1000 && !directory.destination && !result.directories_created &&
                        (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS); ++number)
                    {
                        if (Stopping(cancelled)) { fail(ERROR_CANCELLED, result.path); return result; }
                        if (const auto checked = CheckChain(source_parent); checked != ERROR_SUCCESS) { fail(checked, source_parent.path); return result; }
                        if (const auto checked = CheckChain(destination_parent); checked != ERROR_SUCCESS) { fail(checked, destination_parent.path); return result; }
                        if (const auto checked = CheckDirectory(directory.source.get(), &directory.source_identity, true, true); checked != ERROR_SUCCESS)
                        { fail(checked, directory.source_path); return result; }
                        const auto name = KeepBothDirectoryName(request.destination_name, number, maximum_leaf);
                        if (name.empty()) { fail(ERROR_FILENAME_EXCED_RANGE, result.path); return result; }
                        if (const auto rebound = RebindRoot(directories, result, budget, destination_parent.path,
                            name, maximum_suffix, maximum_path); rebound != ERROR_SUCCESS) { fail(rebound, result.path); return result; }
                        error = CreateOwnedDirectory(directory, result, native_files);
                    }
                }
                if (error != ERROR_SUCCESS) { fail(error, directory.destination_path); return result; }
                update = { result.files_copied, result.directories_created, directory.source_path, directory.destination_path, {}, false };
                if (!report(update)) { fail(ERROR_UNHANDLED_EXCEPTION, directory.destination_path); return result; }
            }
            for (const auto& file : files)
            {
                auto& directory = directories[file.parent]; const auto& entry = directory.children[file.child];
                if (Stopping(cancelled)) { fail(ERROR_CANCELLED, directory.destination_path); return result; }
                if (directory.skipped) { ++result.files_skipped; continue; }
                if (const auto error = CheckTarget(directory); error != ERROR_SUCCESS) { fail(error, directory.destination_path); return result; }
                if (const auto error = CheckDirectory(directory.source.get(), &directory.source_identity, true, true); error != ERROR_SUCCESS)
                { fail(error, directory.source_path); return result; }
                update = { result.files_copied, result.directories_created, blink::core::JoinPath(directory.source_path, entry.name),
                    blink::core::JoinPath(directory.destination_path, entry.name), {}, false };
                FileCopyRequest copy{ directory.source_path, directory.source_identity, entry, directory.destination_path,
                    directory.destination_identity, entry.name };
                const auto resolve_file_conflict = [&]() -> DWORD {
                    bool exists{}, target_directory{};
                    if (const auto error = ReadDestination(update.current_destination_path, exists, target_directory); error) { return error; }
                    if (!exists) { return ERROR_SUCCESS; }
                    return ResolveConflict(request, { copy, target_directory }, cancelled, copy.conflict_policy);
                };
                if (merging)
                { if (const auto error = resolve_file_conflict(); error) { fail(error, update.current_destination_path); return result; } }
                bool observer_failed{};
                const auto on_bytes = [&](const FileCopyProgress& bytes) {
                    update.current_file = bytes;
                    if (moving) { update.source_files_removed = moving->files_removed; }
                    if (!report(update)) { observer_failed = true; throw ERROR_UNHANDLED_EXCEPTION; }
                };
                FileCopyResult copied;
                if (moving)
                {
                    const auto transfer = [&]() -> FileCopyResult {
                        FileCopyResult value;
                        bool removed{};
                        if (native_files)
                        {
                            auto moved = MoveEntrySafely(copy, cancelled, [&](const MoveProgress& child) {
                                if (!child.destination_path.empty()) { update.current_destination_path = child.destination_path; }
                                on_bytes(child.copy_progress);
                            }, [&](HANDLE source, HANDLE destination, const std::wstring& name) -> DWORD {
                                if (observer_failed) { return ERROR_UNHANDLED_EXCEPTION; }
                                if (const auto error = before_removal ? before_removal() : ERROR_SUCCESS; error) { return error; }
                                if (Stopping(cancelled)) { return ERROR_CANCELLED; }
                                return detail::RenameRelativeByHandle(source, destination, name,
                                    copy.conflict_policy == FileCopyConflictPolicy::Replace);
                            });
                            removed = moved.moved;
                            moving->removal_error = moved.moved ? moved.post_commit_error : moved.error;
                            value.error = moved.error; value.copied = moved.moved; value.skipped = moved.skipped;
                            value.path = std::move(moved.path);
                        }
                        else
                        {
                            auto moved = detail::CopyFileAndRemoveSource(copy, cancelled, on_bytes, [&]() -> DWORD {
                                if (observer_failed) { return ERROR_UNHANDLED_EXCEPTION; }
                                return before_removal ? before_removal() : ERROR_SUCCESS;
                            });
                            removed = moved.source_removed;
                            moving->removal_error = moved.removal_error;
                            value = std::move(moved.copy);
                        }
                        if (removed)
                        {
                            directory.removed_children[file.child] = true;
                            ++moving->files_removed;
                            if (moving->source_items_remaining) { --moving->source_items_remaining; }
                        }
                        return value;
                    };
                    copied = transfer();
                    if (merging && NameConflict(copied.error) && copy.conflict_policy == FileCopyConflictPolicy::Fail &&
                        !copied.partial_file_left && !directory.removed_children[file.child])
                    {
                        if (const auto error = resolve_file_conflict(); error) { fail(error, update.current_destination_path); return result; }
                        copied = transfer();
                    }
                }
                else
                {
                    copied = CopyFileSafely(copy, cancelled, on_bytes);
                    if (merging && NameConflict(copied.error) && copy.conflict_policy == FileCopyConflictPolicy::Fail && !copied.partial_file_left)
                    {
                        if (const auto error = resolve_file_conflict(); error) { fail(error, update.current_destination_path); return result; }
                        copied = CopyFileSafely(copy, cancelled, on_bytes);
                    }
                }
                if (copied.copied) { ++result.files_copied; }
                result.partial_tree_left = result.partial_tree_left || copied.partial_file_left;
                if (copied.skipped && !copied.error)
                {
                    ++result.files_skipped;
                    if (!report(update)) { fail(ERROR_UNHANDLED_EXCEPTION, update.current_destination_path); return result; }
                    continue;
                }
                if (copied.error != ERROR_SUCCESS || !copied.copied)
                { fail(copied.error, copied.path.empty() ? update.current_source_path : copied.path); return result; }
                if (moving && moving->removal_error != ERROR_SUCCESS)
                { fail(moving->removal_error, update.current_source_path); return result; }
                if (observer_failed) { fail(ERROR_UNHANDLED_EXCEPTION, update.current_destination_path); return result; }
                update.files_copied = result.files_copied;
                if (!copied.path.empty()) { update.current_destination_path = copied.path; }
                if (moving) { update.source_files_removed = moving->files_removed; }
                if (!report(update)) { fail(ERROR_UNHANDLED_EXCEPTION, update.current_destination_path); return result; }
            }
            // Final same-handle inventory check detects observed additions,
            // removals, case changes and identity/type replacements.
            for (auto& directory : directories)
            {
                if (moving && !merging) { continue; }
                const auto error = moving ? VerifyMoveInventory(directory, request.limits, budget, cancelled, *moving) :
                    VerifyInventory(directory, request.limits, budget, cancelled);
                if (error) { fail(error, directory.source_path); return result; }
            }
            for (auto iterator = directories.rbegin(); iterator != directories.rend(); ++iterator)
            {
                auto& directory = *iterator;
                if (Stopping(cancelled)) { fail(ERROR_CANCELLED, directory.destination_path); return result; }
                if (directory.skipped) { continue; }
                if (const auto error = CheckTarget(directory); error != ERROR_SUCCESS) { fail(error, directory.destination_path); return result; }
                if (directory.merged_existing) { continue; }
                if (const auto error = CheckDirectory(directory.destination_metadata.get(), &directory.destination_identity, true, true); error != ERROR_SUCCESS)
                { fail(error, directory.destination_path); return result; }
                auto basic = directory.basic; basic.ChangeTime.QuadPart = 0;
                basic.FileAttributes &= OrdinaryAttributes & ~FILE_ATTRIBUTE_NORMAL;
                if (!basic.FileAttributes) { basic.FileAttributes = FILE_ATTRIBUTE_NORMAL; }
                if (!SetFileInformationByHandle(directory.destination_metadata.get(), FileBasicInfo, &basic, sizeof(basic)))
                { fail(LastError(), directory.destination_path); return result; }
                if (const auto error = CheckTarget(directory); error != ERROR_SUCCESS)
                { fail(error, directory.destination_path); return result; }
                if (const auto error = CheckDirectory(directory.destination_metadata.get(), &directory.destination_identity, true, true);
                    error != ERROR_SUCCESS) { fail(error, directory.destination_path); return result; }
            }
            // A retained directory handle is an identity guard, not a reparse
            // freeze. Reject observable changes even during metadata commits.
            for (auto& directory : directories)
            { if (!directory.skipped) { if (const auto error = CheckTarget(directory); error != ERROR_SUCCESS)
                { fail(error, directory.destination_path); return result; } } }
            result.error = ERROR_SUCCESS; result.copied = true; result.partial_tree_left = false;
            if (moving)
            {
                // Directory guards used by Copy deny DELETE. Release them once
                // all planned files have moved, then reopen each original empty
                // directory by its captured identity under a checked parent.
                // New/unplanned children make native empty-directory removal
                // fail; they are never recursively deleted.
                for (auto& directory : directories) { directory.source.reset(); }
                for (std::size_t index = directories.size(); index-- > 0;)
                {
                    auto& directory = directories[index];
                    // Explicitly skipped subtrees and their nonempty ancestors
                    // remain in place. Do not demand DELETE access for objects
                    // which this operation has decided to retain.
                    if (directory.skipped || std::find(directory.removed_children.begin(),
                        directory.removed_children.end(), false) != directory.removed_children.end()) { continue; }
                    update = { result.files_copied, result.directories_created, directory.source_path,
                        directory.destination_path, {}, false, moving->files_removed, moving->directories_removed, true };
                    if (!report(update)) { fail(ERROR_UNHANDLED_EXCEPTION, directory.source_path); return result; }
                    if (Stopping(cancelled)) { fail(ERROR_CANCELLED, directory.source_path); return result; }
                    if (const auto error = CheckTarget(directory); error) { fail(error, directory.destination_path); return result; }
                    const auto& parent_path = index ? directories[directory.parent].source_path : source_parent.path;
                    const auto& parent_identity = index ? directories[directory.parent].source_identity : request.source_parent_identity;
                    auto parent = detail::AcquireDirectoryLease(parent_path, parent_identity, cancelled);
                    if (parent.error) { fail(parent.error, directory.source_path); return result; }
                    CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
                    parameters.dwFileFlags = DirectoryOpenFlags;
                    directory.source = blink::core::UniqueHandle(CreateFile3(Native(directory.source_path).c_str(),
                        DELETE | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ, OPEN_EXISTING, &parameters));
                    if (!directory.source) { fail(LastError(), directory.source_path); return result; }
                    const auto source_error = merging ? VerifyMoveInventory(directory, request.limits, budget, cancelled, *moving) :
                        CheckDirectory(directory.source.get(), &directory.source_identity, true, true);
                    if (source_error)
                    { fail(source_error, directory.source_path); return result; }
                    DirectoryMoveRelocation relocation{ directory.source_path, directory.destination_path, directory.source_identity };
                    if (const auto error = before_removal ? before_removal() : ERROR_SUCCESS; error)
                    { fail(error, directory.source_path); return result; }
                    if (Stopping(cancelled)) { fail(ERROR_CANCELLED, directory.source_path); return result; }
                    FILE_DISPOSITION_INFO_EX disposition{ FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                        FILE_DISPOSITION_FLAG_FORCE_IMAGE_SECTION_CHECK | FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE };
                    if (!SetFileInformationByHandle(directory.source.get(), FileDispositionInfoEx, &disposition, sizeof(disposition)))
                    {
                        const auto error = LastError();
                        // The planned source was empty immediately above. A new
                        // child observed by the native empty-delete is retained.
                        if (error == ERROR_DIR_NOT_EMPTY) { ++moving->source_items_remaining; }
                        fail(error, directory.source_path); return result;
                    }
                    directory.source.reset(); ++moving->directories_removed;
                    moving->directory_relocations.push_back(std::move(relocation)); // Capacity reserved before native changes.
                    if (index)
                    {
                        directories[directory.parent].removed_children[directory.parent_child] = true;
                        if (moving->source_items_remaining) { --moving->source_items_remaining; }
                    }
                    if (!index) { moving->source_removed = true; }
                }
                moving->removal_error = ERROR_SUCCESS;
            }
            try
            {
                update = { result.files_copied, result.directories_created, directories[0].source_path, result.path, {}, true };
                if (moving)
                {
                    update.source_files_removed = moving->files_removed;
                    update.source_directories_removed = moving->directories_removed;
                }
                (void)report(update); // The complete tree is already committed.
            }
            catch (...) {} // Observer preparation failure cannot reverse the committed tree.
            return result;
        }
        catch (const std::bad_alloc&) { fail(ERROR_NOT_ENOUGH_MEMORY, result.path); }
        catch (...) { fail(ERROR_GEN_FAILURE, result.path); }
        return result;
    }

    DirectoryCopyResult CopyDirectorySafely(const DirectoryCopyRequest& request,
        const FileOperationCancellation& cancelled, const DirectoryCopyProgressCallback& progress) noexcept
    { return CopyDirectoryImpl(request, cancelled, progress); }

    detail::DirectoryMoveCopyResult detail::CopyDirectoryAndRemoveSource(const DirectoryCopyRequest& request,
        const FileOperationCancellation& cancelled, const DirectoryCopyProgressCallback& progress,
        const std::function<DWORD()>& before_removal) noexcept
    {
        DirectoryMoveCopyResult result;
        result.copy = CopyDirectoryImpl(request, cancelled, progress, &result, before_removal);
        return result;
    }
}
