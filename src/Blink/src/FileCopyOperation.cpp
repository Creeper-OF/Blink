#include "Blink/App/FileCopyOperation.h"
#include "Blink/App/DirectoryLease.h"
#include "Blink/App/HandleRename.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace blink::app
{
    namespace
    {
        constexpr DWORD OrdinaryAttributes = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
            FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL |
            FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
        constexpr DWORD CopiedAttributes = OrdinaryAttributes & ~FILE_ATTRIBUTE_NORMAL;
        constexpr DWORD CopyBufferBytes = 1024 * 1024;
        constexpr std::size_t MaxStreamInformationBytes = 1024 * 1024;
        constexpr std::size_t MaxDataStreams = 256;
        constexpr unsigned KeepBothAttempts = 1000;
        constexpr std::wstring_view DefaultStream = L"::$DATA";

        struct DataStream
        {
            std::wstring suffix;
            std::uint64_t bytes{};
            blink::core::UniqueHandle source;
        };

        bool Stopping(const FileOperationCancellation& cancelled) noexcept
        {
            return cancelled && cancelled->load();
        }

        bool Report(const FileCopyProgressCallback& progress, const FileCopyProgress& value) noexcept
        {
            if (!progress) { return true; }
            try { progress(value); return true; }
            catch (...) { return false; }
        }

        bool IsDataStreamName(const std::wstring_view suffix) noexcept
        {
            if (suffix == DefaultStream) { return true; }
            if (suffix.size() <= DefaultStream.size() || suffix.front() != L':' || !suffix.ends_with(L":$DATA"))
            { return false; }
            const auto name = suffix.substr(1, suffix.size() - 7);
            return !name.empty() && name.find_first_of(L"\\/:") == std::wstring_view::npos &&
                name.find(L'\0') == std::wstring_view::npos;
        }

        DWORD ReadStreams(HANDLE source, std::vector<DataStream>& streams, std::uint64_t& total)
        {
            std::vector<ULONGLONG> storage(512);
            for (;;)
            {
                if (GetFileInformationByHandleEx(source, FileStreamInfo, storage.data(),
                    static_cast<DWORD>(storage.size() * sizeof(ULONGLONG)))) { break; }
                const auto error = GetLastError();
                if (error != ERROR_MORE_DATA && error != ERROR_INSUFFICIENT_BUFFER)
                { return error == ERROR_INVALID_PARAMETER || error == ERROR_INVALID_FUNCTION ? ERROR_NOT_SUPPORTED : error; }
                if (storage.size() * sizeof(ULONGLONG) >= MaxStreamInformationBytes) { return ERROR_NOT_SUPPORTED; }
                storage.resize((std::min)(storage.size() * 2, MaxStreamInformationBytes / sizeof(ULONGLONG)));
            }
            streams.clear(); total = 0;
            const auto bytes = storage.size() * sizeof(ULONGLONG);
            const auto* data = reinterpret_cast<const std::byte*>(storage.data());
            std::size_t offset{};
            bool default_found{};
            for (;;)
            {
                constexpr auto HeaderBytes = offsetof(FILE_STREAM_INFO, StreamName);
                if (offset > bytes || bytes - offset < HeaderBytes) { return ERROR_INVALID_DATA; }
                const auto* info = reinterpret_cast<const FILE_STREAM_INFO*>(data + offset);
                if ((info->StreamNameLength % sizeof(wchar_t)) != 0 ||
                    info->StreamNameLength > bytes - offset - HeaderBytes || info->StreamSize.QuadPart < 0)
                { return ERROR_INVALID_DATA; }
                std::wstring suffix(info->StreamName, info->StreamNameLength / sizeof(wchar_t));
                if (!IsDataStreamName(suffix) || streams.size() >= MaxDataStreams) { return ERROR_NOT_SUPPORTED; }
                const auto size = static_cast<std::uint64_t>(info->StreamSize.QuadPart);
                if ((std::numeric_limits<std::uint64_t>::max)() - total < size) { return ERROR_FILE_TOO_LARGE; }
                total += size;
                default_found |= suffix == DefaultStream;
                streams.push_back({ std::move(suffix), size, {} });
                if (!info->NextEntryOffset) { break; }
                if (info->NextEntryOffset < HeaderBytes + info->StreamNameLength ||
                    (info->NextEntryOffset % alignof(LONGLONG)) != 0 || info->NextEntryOffset > bytes - offset)
                { return ERROR_INVALID_DATA; }
                offset += info->NextEntryOffset;
            }
            if (!default_found) { return ERROR_NOT_SUPPORTED; }
            std::sort(streams.begin(), streams.end(), [](const DataStream& left, const DataStream& right) {
                if (left.suffix == DefaultStream) { return right.suffix != DefaultStream; }
                if (right.suffix == DefaultStream) { return false; }
                return left.suffix < right.suffix;
            });
            for (std::size_t index = 1; index < streams.size(); ++index)
            {
                if (CompareStringOrdinal(streams[index - 1].suffix.c_str(), -1, streams[index].suffix.c_str(), -1, TRUE) == CSTR_EQUAL)
                { return ERROR_NOT_SUPPORTED; }
            }
            return ERROR_SUCCESS;
        }

        bool SameStreams(const std::vector<DataStream>& left, const std::vector<DataStream>& right) noexcept
        {
            if (left.size() != right.size()) { return false; }
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (left[index].suffix != right[index].suffix || left[index].bytes != right[index].bytes) { return false; }
            }
            return true;
        }

        DWORD VerifyStream(HANDLE handle, const blink::core::FileIdentity& expected, std::uint64_t bytes)
        {
            FILE_ATTRIBUTE_TAG_INFO attributes{};
            FILE_STANDARD_INFO standard{};
            blink::core::FileIdentity identity;
            if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
                !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)) ||
                !blink::core::ReadFileIdentity(handle, identity)) { return GetLastError(); }
            if ((attributes.FileAttributes & ~OrdinaryAttributes) != 0 || attributes.ReparseTag != 0)
            { return ERROR_NOT_SUPPORTED; }
            if (identity != expected || standard.Directory || standard.DeletePending || standard.EndOfFile.QuadPart < 0 ||
                static_cast<std::uint64_t>(standard.EndOfFile.QuadPart) != bytes) { return ERROR_FILE_INVALID; }
            return ERROR_SUCCESS;
        }

        bool RemoveOwnedTarget(HANDLE destination) noexcept
        {
            FILE_DISPOSITION_INFO disposition{};
            disposition.DeleteFile = TRUE;
            if (SetFileInformationByHandle(destination, FileDispositionInfo, &disposition, sizeof(disposition)))
            { return true; }
            FILE_BASIC_INFO basic{};
            if (!GetFileInformationByHandleEx(destination, FileBasicInfo, &basic, sizeof(basic))) { return false; }
            if ((basic.FileAttributes & FILE_ATTRIBUTE_READONLY) == 0) { return false; }
            basic.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
            if (!basic.FileAttributes) { basic.FileAttributes = FILE_ATTRIBUTE_NORMAL; }
            if (!SetFileInformationByHandle(destination, FileBasicInfo, &basic, sizeof(basic))) { return false; }
            return SetFileInformationByHandle(destination, FileDispositionInfo, &disposition, sizeof(disposition)) != FALSE;
        }

        bool NameExistsWithoutFollowing(const std::wstring& path)
        {
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            // The isolated probe returns ERROR_BAD_ARGUMENTS when DISALLOW
            // is combined with FILE_SHARE_DELETE, for either READ_ATTRIBUTES
            // or GENERIC_READ. Source capture does not share DELETE. Here the
            // caller already holds its validated, non-reparse parent chain;
            // OPEN_REPARSE_POINT makes this leaf probe observe the object itself.
            parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS;
            blink::core::UniqueHandle existing(CreateFile3(path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, &parameters));
            return static_cast<bool>(existing);
        }

        bool Collision(DWORD error, const std::wstring& path)
        {
            return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ||
                ((error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION) && NameExistsWithoutFollowing(path));
        }

        std::wstring KeepBothName(const std::wstring& original, unsigned number, std::size_t maximum)
        {
            const auto dot = original.find_last_of(L'.');
            const auto extension = dot != std::wstring::npos && dot != 0 ? original.substr(dot) : std::wstring{};
            const auto stem = dot != std::wstring::npos && dot != 0 ? original.substr(0, dot) : original;
            const auto suffix = L" (" + std::to_wstring(number) + L")";
            if (suffix.size() + extension.size() >= maximum) { return {}; }
            auto shortened = stem.substr(0, maximum - suffix.size() - extension.size());
            if (!shortened.empty() && shortened.back() >= 0xd800 && shortened.back() <= 0xdbff) { shortened.pop_back(); }
            if (shortened.empty()) { return {}; }
            return shortened + suffix + extension;
        }

        DWORD SuppressAutomaticTimes(HANDLE destination)
        {
            // Stream writers stay held until metadata commit. Disable their
            // deferred timestamp updates so closing them cannot overwrite the
            // basic times explicitly copied on the default owned handle.
            const FILETIME unchanged{ MAXDWORD, MAXDWORD };
            return SetFileTime(destination, nullptr, &unchanged, &unchanged) ? ERROR_SUCCESS : GetLastError();
        }
    }

    static FileCopyResult CopyFileImpl(const FileCopyRequest& request, const FileOperationCancellation& cancelled,
        const FileCopyProgressCallback& progress, detail::FileMoveCopyResult* moving = nullptr,
        const std::function<DWORD()>& before_removal = {})
    {
        FileCopyResult result;
        const auto fail = [&result](DWORD error) {
            result.error = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
            return std::move(result);
        };
        if (request.conflict_policy == FileCopyConflictPolicy::Skip)
        {
            // A user explicitly skipped this file. Do not re-read either path:
            // the original conflict may have vanished while the dialog was up.
            result.path = blink::core::JoinPath(request.destination_parent, request.destination_name);
            result.error = ERROR_SUCCESS; result.skipped = true;
            return result;
        }
        if (request.conflict_policy == FileCopyConflictPolicy::Cancel) { return fail(ERROR_CANCELLED); }
        const bool replacing = request.conflict_policy == FileCopyConflictPolicy::Replace;
        if (request.conflict_policy != FileCopyConflictPolicy::Fail &&
            request.conflict_policy != FileCopyConflictPolicy::KeepBoth && !replacing) { return fail(ERROR_INVALID_PARAMETER); }
        if (const auto error = ValidateNewFolderName(request.source.name); error != ERROR_SUCCESS) { return fail(error); }
        if (const auto error = ValidateNewFolderName(request.destination_name); error != ERROR_SUCCESS) { return fail(error); }
        if (request.source.is_directory || (request.source.attributes & ~OrdinaryAttributes) != 0)
        { return fail(ERROR_NOT_SUPPORTED); }
        if (!request.source.identity.valid || !request.source_parent_identity.valid ||
            request.source.identity.volume_serial != request.source_parent_identity.volume_serial)
        { return fail(ERROR_FILE_INVALID); }

        auto source_parent = detail::AcquireDirectoryLease(request.source_parent, request.source_parent_identity, cancelled);
        if (source_parent.error != ERROR_SUCCESS) { return fail(source_parent.error); }
        auto destination_parent = detail::AcquireDirectoryLease(request.destination_parent,
            request.destination_parent_identity, cancelled);
        if (destination_parent.error != ERROR_SUCCESS) { return fail(destination_parent.error); }
        const auto source_path = blink::core::JoinPath(source_parent.native_path, request.source.name);
        if (source_path.size() >= 32767) { return fail(ERROR_FILENAME_EXCED_RANGE); }
        if (Stopping(cancelled)) { return fail(ERROR_CANCELLED); }

        CREATEFILE3_EXTENDED_PARAMETERS source_parameters{}; source_parameters.dwSize = sizeof(source_parameters);
        source_parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_DISALLOW_PATH_REDIRECTS | FILE_FLAG_SEQUENTIAL_SCAN;
        blink::core::UniqueHandle source(CreateFile3(source_path.c_str(), GENERIC_READ | (moving ? DELETE : 0),
            FILE_SHARE_READ, OPEN_EXISTING, &source_parameters));
        if (!source) { return fail(GetLastError()); }
        FILE_ATTRIBUTE_TAG_INFO attributes{};
        if (!GetFileInformationByHandleEx(source.get(), FileAttributeTagInfo, &attributes, sizeof(attributes)))
        { return fail(GetLastError()); }
        if ((attributes.FileAttributes & ~OrdinaryAttributes) != 0 || attributes.ReparseTag != 0)
        { return fail(ERROR_NOT_SUPPORTED); }
        blink::core::FileIdentity identity;
        if (!blink::core::ReadFileIdentity(source.get(), identity)) { return fail(GetLastError()); }
        if (identity != request.source.identity) { return fail(ERROR_FILE_INVALID); }
        FILE_STANDARD_INFO standard{};
        FILE_BASIC_INFO basic{};
        if (!GetFileInformationByHandleEx(source.get(), FileStandardInfo, &standard, sizeof(standard)) ||
            !GetFileInformationByHandleEx(source.get(), FileBasicInfo, &basic, sizeof(basic)))
        { return fail(GetLastError()); }
        if (standard.Directory || standard.DeletePending || standard.EndOfFile.QuadPart < 0)
        { return fail(ERROR_FILE_INVALID); }

        blink::core::FileIdentity replaced_identity;
        if (replacing)
        {
            if (const auto error = detail::ReadFileReplacementTarget(destination_parent, request.destination_name, identity, replaced_identity); error)
            { return fail(error); }
        }

        std::vector<DataStream> streams;
        std::uint64_t total{};
        if (const auto error = ReadStreams(source.get(), streams, total); error != ERROR_SUCCESS) { return fail(error); }
        if (streams.front().bytes != static_cast<std::uint64_t>(standard.EndOfFile.QuadPart)) { return fail(ERROR_FILE_INVALID); }
        std::size_t largest_suffix{};
        for (auto& stream : streams)
        {
            if (Stopping(cancelled)) { return fail(ERROR_CANCELLED); }
            if (stream.suffix == DefaultStream) { continue; }
            largest_suffix = (std::max)(largest_suffix, stream.suffix.size());
            if (source_path.size() + stream.suffix.size() >= 32767) { return fail(ERROR_FILENAME_EXCED_RANGE); }
            const auto stream_path = source_path + stream.suffix;
            auto stream_parameters = source_parameters;
            // Our default handle owns DELETE for a move, so ADS reads must
            // share DELETE. Parent and source handles stay pinned and each
            // opened stream is checked against the selected source identity.
            if (moving) { stream_parameters.dwFileFlags &= ~FILE_FLAG_DISALLOW_PATH_REDIRECTS; }
            stream.source = blink::core::UniqueHandle(CreateFile3(stream_path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | (moving ? FILE_SHARE_DELETE : 0), OPEN_EXISTING, &stream_parameters));
            if (!stream.source) { return fail(GetLastError()); }
            if (const auto error = VerifyStream(stream.source.get(), identity, stream.bytes); error != ERROR_SUCCESS) { return fail(error); }
        }
        std::vector<DataStream> check;
        std::uint64_t checked_total{};
        if (const auto error = ReadStreams(source.get(), check, checked_total); error != ERROR_SUCCESS) { return fail(error); }
        if (checked_total != total || !SameStreams(streams, check)) { return fail(ERROR_NOT_SUPPORTED); }
        if (total != 0 && !Report(progress, { total, 0 })) { return fail(ERROR_UNHANDLED_EXCEPTION); }
        if (Stopping(cancelled)) { return fail(ERROR_CANCELLED); }

        std::vector<std::byte> buffer(CopyBufferBytes);
        std::vector<blink::core::UniqueHandle> destination_streams;
        destination_streams.reserve(streams.size());
        CREATEFILE3_EXTENDED_PARAMETERS destination_parameters{}; destination_parameters.dwSize = sizeof(destination_parameters);
        destination_parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (replacing) { destination_parameters.dwFileAttributes = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY; }
        destination_parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DISALLOW_PATH_REDIRECTS | FILE_FLAG_SEQUENTIAL_SCAN;
        blink::core::UniqueHandle destination;
        std::wstring destination_path;
        auto final_path = blink::core::JoinPath(destination_parent.path, request.destination_name);
        static std::atomic_uint64_t staging_sequence{};
        const auto prefix_length = destination_parent.native_path.size() + (destination_parent.native_path.back() == L'\\' ? 0 : 1);
        if (prefix_length + largest_suffix + 1 >= 32767) { return fail(ERROR_FILENAME_EXCED_RANGE); }
        const auto maximum_leaf = (std::min)(std::size_t{ 255 }, std::size_t{ 32766 } - prefix_length - largest_suffix);
        for (unsigned attempt = 0; attempt < KeepBothAttempts; ++attempt)
        {
            if (Stopping(cancelled)) { return fail(ERROR_CANCELLED); }
            const auto name = replacing ? L".blink-replace-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(staging_sequence.fetch_add(1, std::memory_order_relaxed)) + L".tmp" :
                attempt == 0 ? request.destination_name : KeepBothName(request.destination_name, attempt + 1, maximum_leaf);
            if (name.empty() || name.size() > maximum_leaf) { return fail(ERROR_FILENAME_EXCED_RANGE); }
            destination_path = blink::core::JoinPath(destination_parent.native_path, name);
            result.path = blink::core::JoinPath(destination_parent.path, name);
            destination = blink::core::UniqueHandle(CreateFile3(destination_path.c_str(),
                GENERIC_WRITE | FILE_READ_ATTRIBUTES | DELETE, 0, CREATE_NEW, &destination_parameters));
            if (destination) { break; }
            const auto error = GetLastError();
            if (!Collision(error, destination_path)) { return fail(error); }
            if (!replacing && request.conflict_policy != FileCopyConflictPolicy::KeepBoth) { return fail(ERROR_FILE_EXISTS); }
        }
        if (!destination) { return fail(ERROR_FILE_EXISTS); }
        const auto abort = [&result, &destination, &destination_streams, &fail, &final_path, replacing](DWORD error) {
            // Named-stream handles must close before disposition on the owned
            // default handle. Never delete an object through its pathname.
            destination_streams.clear();
            result.partial_file_left = !RemoveOwnedTarget(destination.get());
            destination.reset();
            if (replacing && !result.partial_file_left) { result.path.swap(final_path); }
            return fail(error);
        };

        try
        {
            if (const auto error = SuppressAutomaticTimes(destination.get()); error != ERROR_SUCCESS) { return abort(error); }
            blink::core::FileIdentity destination_identity;
            if (!blink::core::ReadFileIdentity(destination.get(), destination_identity)) { return abort(GetLastError()); }
            for (const auto& stream : streams)
            {
                if (Stopping(cancelled)) { return abort(ERROR_CANCELLED); }
                if (stream.suffix == DefaultStream) { destination_streams.emplace_back(); continue; }
                const auto path = destination_path + stream.suffix;
                // Delete sharing is necessary because our own default-stream
                // handle already holds DELETE. Read/write are never shared.
                // DISALLOW combined with FILE_SHARE_DELETE returns
                // ERROR_BAD_ARGUMENTS in the isolated sharing probe. Use the
                // fixed, proven OPEN_REPARSE_POINT flag set instead:
                // parents and the CREATE_NEW default leaf remain pinned, the
                // suffix cannot escape that leaf, and file identity is checked.
                CREATEFILE3_EXTENDED_PARAMETERS stream_parameters{};
                stream_parameters.dwSize = sizeof(stream_parameters);
                stream_parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
                stream_parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT;
                blink::core::UniqueHandle target(CreateFile3(path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                    FILE_SHARE_DELETE, CREATE_NEW, &stream_parameters));
                if (!target) { return abort(GetLastError()); }
                blink::core::FileIdentity target_identity;
                if (!blink::core::ReadFileIdentity(target.get(), target_identity)) { return abort(GetLastError()); }
                if (target_identity != destination_identity) { return abort(ERROR_FILE_INVALID); }
                if (const auto error = SuppressAutomaticTimes(target.get()); error != ERROR_SUCCESS) { return abort(error); }
                destination_streams.push_back(std::move(target));
            }

            std::uint64_t transferred{};
            for (std::size_t index = 0; index < streams.size(); ++index)
            {
                const auto input = streams[index].suffix == DefaultStream ? source.get() : streams[index].source.get();
                const auto output = streams[index].suffix == DefaultStream ? destination.get() : destination_streams[index].get();
                std::uint64_t stream_transferred{};
                while (stream_transferred < streams[index].bytes)
                {
                    if (Stopping(cancelled)) { return abort(ERROR_CANCELLED); }
                    const auto requested = static_cast<DWORD>((std::min)(static_cast<std::uint64_t>(buffer.size()), streams[index].bytes - stream_transferred));
                    DWORD read{};
                    if (!ReadFile(input, buffer.data(), requested, &read, nullptr)) { return abort(GetLastError()); }
                    if (!read) { return abort(ERROR_HANDLE_EOF); }
                    DWORD written{};
                    while (written < read)
                    {
                        if (Stopping(cancelled)) { return abort(ERROR_CANCELLED); }
                        DWORD current{};
                        if (!WriteFile(output, buffer.data() + written, read - written, &current, nullptr)) { return abort(GetLastError()); }
                        if (!current) { return abort(ERROR_WRITE_FAULT); }
                        written += current;
                    }
                    stream_transferred += written; transferred += written;
                    if (transferred < total && !Report(progress, { total, transferred })) { return abort(ERROR_UNHANDLED_EXCEPTION); }
                }
                if (Stopping(cancelled)) { return abort(ERROR_CANCELLED); }
                if (!FlushFileBuffers(output)) { return abort(GetLastError()); }
            }
            if (Stopping(cancelled)) { return abort(ERROR_CANCELLED); }
            FILE_ATTRIBUTE_TAG_INFO final_attributes{};
            if (!GetFileInformationByHandleEx(source.get(), FileAttributeTagInfo, &final_attributes, sizeof(final_attributes)))
            { return abort(GetLastError()); }
            if ((final_attributes.FileAttributes & ~OrdinaryAttributes) != 0 || final_attributes.ReparseTag != 0)
            { return abort(ERROR_NOT_SUPPORTED); }
            if (const auto error = ReadStreams(source.get(), check, checked_total); error != ERROR_SUCCESS) { return abort(error); }
            if (checked_total != total || !SameStreams(streams, check)) { return abort(ERROR_NOT_SUPPORTED); }
            if (const auto error = ReadStreams(destination.get(), check, checked_total); error != ERROR_SUCCESS) { return abort(error); }
            if (checked_total != total || !SameStreams(streams, check)) { return abort(ERROR_NOT_SUPPORTED); }
            // Delete sharing is required on owned ADS, so another opener may
            // mark one for deletion while its writer is still held. StreamInfo
            // can continue listing that stream until close; inventory equality
            // alone must not turn a delete-pending stream into a successful copy.
            for (std::size_t index = 0; index < streams.size(); ++index)
            {
                if (Stopping(cancelled)) { return abort(ERROR_CANCELLED); }
                const auto handle = streams[index].suffix == DefaultStream ? destination.get() : destination_streams[index].get();
                if (const auto error = VerifyStream(handle, destination_identity, streams[index].bytes); error != ERROR_SUCCESS)
                { return abort(error); }
            }
            if (Stopping(cancelled)) { return abort(ERROR_CANCELLED); }

            basic.ChangeTime.QuadPart = 0;
            basic.FileAttributes &= CopiedAttributes;
            if (!basic.FileAttributes) { basic.FileAttributes = FILE_ATTRIBUTE_NORMAL; }
            if (!SetFileInformationByHandle(destination.get(), FileBasicInfo, &basic, sizeof(basic))) { return abort(GetLastError()); }
            if (replacing)
            {
                blink::core::FileIdentity current;
                if (const auto error = detail::ReadFileReplacementTarget(destination_parent, request.destination_name, identity, current); error)
                { return abort(error); }
                if (current != replaced_identity) { return abort(ERROR_FILE_INVALID); }
                if (Stopping(cancelled)) { return abort(ERROR_CANCELLED); }
                const auto error = detail::RenameRelativeByHandle(destination.get(), destination_parent.handles.back().get(), request.destination_name, true);
                if (error) { return abort(error); }
                result.path.swap(final_path);
            }
            result.error = ERROR_SUCCESS; result.copied = true;
            // This observer cannot turn an already committed copy into failure.
            (void)Report(progress, { total, total });
            if (moving)
            {
                // Copy has committed. Failure/cancellation from this point
                // retains that destination and reports the original source.
                try
                {
                    auto error = before_removal ? before_removal() : ERROR_SUCCESS;
                    if (!error && Stopping(cancelled)) { error = ERROR_CANCELLED; }
                    if (!error)
                    {
                        error = ReadStreams(source.get(), check, checked_total);
                        if (!error && (checked_total != total || !SameStreams(streams, check))) { error = ERROR_FILE_INVALID; }
                    }
                    for (std::size_t index = 0; !error && index < streams.size(); ++index)
                    {
                        const auto input = streams[index].suffix == DefaultStream ? source.get() : streams[index].source.get();
                        const auto output = streams[index].suffix == DefaultStream ? destination.get() : destination_streams[index].get();
                        error = VerifyStream(input, identity, streams[index].bytes);
                        if (!error) { error = VerifyStream(output, destination_identity, streams[index].bytes); }
                    }
                    if (!error && Stopping(cancelled)) { error = ERROR_CANCELLED; }
                    if (!error)
                    {
                        FILE_DISPOSITION_INFO_EX disposition{ FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                            FILE_DISPOSITION_FLAG_FORCE_IMAGE_SECTION_CHECK | FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE };
                        if (!SetFileInformationByHandle(source.get(), FileDispositionInfoEx, &disposition, sizeof(disposition)))
                        { error = GetLastError(); }
                        else
                        {
                            streams.clear(); source.reset();
                            moving->source_removed = true;
                        }
                    }
                    moving->removal_error = error;
                }
                catch (const std::bad_alloc&) { moving->removal_error = ERROR_NOT_ENOUGH_MEMORY; }
                catch (...) { moving->removal_error = ERROR_UNHANDLED_EXCEPTION; }
            }
            return result;
        }
        catch (const std::bad_alloc&) { return abort(ERROR_NOT_ENOUGH_MEMORY); }
        catch (...) { return abort(ERROR_UNHANDLED_EXCEPTION); }
    }

    FileCopyResult CopyFileSafely(const FileCopyRequest& request, const FileOperationCancellation& cancelled,
        const FileCopyProgressCallback& progress)
    { return CopyFileImpl(request, cancelled, progress); }

    detail::FileMoveCopyResult detail::CopyFileAndRemoveSource(const FileCopyRequest& request,
        const FileOperationCancellation& cancelled, const FileCopyProgressCallback& progress, const std::function<DWORD()>& before_removal)
    {
        FileMoveCopyResult result;
        result.copy = CopyFileImpl(request, cancelled, progress, &result, before_removal);
        return result;
    }
}
