#include "Blink/App/ClipboardFileTransfer.h"
#include "Blink/App/DirectoryLease.h"
#include <shellapi.h>
#include <shlobj_core.h>
#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace blink::app
{
    ClipboardDropEffect DecodeClipboardDropEffect(const std::span<const std::byte> data, const bool present)
    {
        if (!present) { return {}; }
        if (data.size() < sizeof(DWORD)) { return { ERROR_INVALID_DATA }; }
        DWORD effect{}; std::memcpy(&effect, data.data(), sizeof(effect));
        if (effect == DROPEFFECT_COPY) { return {}; }
        if (effect == DROPEFFECT_MOVE) { return { ERROR_SUCCESS, ClipboardTransferIntent::Move }; }
        return { ERROR_NOT_SUPPORTED };
    }
    DWORD ClipboardDropEffectValue(const ClipboardTransferIntent intent) noexcept
    {
        return intent == ClipboardTransferIntent::Copy ? DROPEFFECT_COPY :
            intent == ClipboardTransferIntent::Move ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
    }
    namespace
    {
        struct ClipboardScope { ~ClipboardScope() { CloseClipboard(); } };
        struct GlobalUnlockScope
        {
            HGLOBAL memory{};
            ~GlobalUnlockScope() { GlobalUnlock(memory); }
        };
        struct GlobalMemory
        {
            HGLOBAL value{};
            ~GlobalMemory() { if (value) { GlobalFree(value); } }
        };
        DWORD LastError() { const auto error = GetLastError(); return error ? error : ERROR_GEN_FAILURE; }
        bool EncodedSize(const std::span<const std::wstring> paths, std::size_t& bytes) noexcept
        {
            if (paths.empty() || paths.size() > MaxClipboardFileCount) { return false; }
            bytes = sizeof(DROPFILES) + sizeof(wchar_t); // The final list NUL.
            for (const auto& path : paths)
            {
                if (path.empty() || path.size() >= 32767 || path.find(L'\0') != std::wstring::npos) { return false; }
                const auto item_bytes = (path.size() + 1) * sizeof(wchar_t);
                if (item_bytes > MaxClipboardFilePayloadBytes - bytes) { return false; }
                bytes += item_bytes;
            }
            return true;
        }
        HGLOBAL CopyGlobal(const void* data, const std::size_t bytes)
        {
            const auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
            if (!memory) { return nullptr; }
            auto* target = GlobalLock(memory);
            if (!target) { GlobalFree(memory); return nullptr; }
            const auto capacity = GlobalSize(memory);
            if (capacity < bytes) { GlobalUnlock(memory); GlobalFree(memory); return nullptr; }
            // GlobalSize may include allocator padding. Its bytes must also be
            // zero, otherwise our strict list decoder would see foreign tails.
            std::memset(target, 0, capacity);
            std::memcpy(target, data, bytes);
            GlobalUnlock(memory);
            return memory;
        }
        ClipboardFilePath Single(ClipboardFilePaths files)
        {
            ClipboardFilePath result{ files.error, {}, files.sequence };
            if (files.error != ERROR_SUCCESS) { return result; }
            if (files.paths.size() != 1) { result.error = ERROR_NOT_SUPPORTED; return result; }
            result.path = std::move(files.paths.front());
            return result;
        }
    }
    std::vector<std::byte> EncodeClipboardFilePaths(const std::span<const std::wstring> paths)
    {
        std::size_t bytes{};
        if (!EncodedSize(paths, bytes)) { return {}; }
        std::vector<std::byte> data(bytes);
        const DROPFILES header{ sizeof(DROPFILES), {}, FALSE, TRUE };
        std::memcpy(data.data(), &header, sizeof(header));
        std::size_t offset = sizeof(header);
        for (const auto& path : paths)
        {
            std::memcpy(data.data() + offset, path.data(), path.size() * sizeof(wchar_t));
            offset += (path.size() + 1) * sizeof(wchar_t);
        }
        return data;
    }
    ClipboardFilePaths DecodeClipboardFilePaths(const std::span<const std::byte> data)
    {
        ClipboardFilePaths result;
        if (data.size() > MaxClipboardFilePayloadBytes) { return result; }
        if (data.size() < sizeof(DROPFILES)) { result.error = ERROR_INVALID_DATA; return result; }
        DROPFILES header{}; std::memcpy(&header, data.data(), sizeof(header));
        if (!header.fWide) { return result; }
        if (header.pFiles < sizeof(header) || header.pFiles > data.size() ||
            (header.pFiles % sizeof(wchar_t)) != 0 || (data.size() - header.pFiles) % sizeof(wchar_t) != 0)
        { result.error = ERROR_INVALID_DATA; return result; }
        const auto count = (data.size() - header.pFiles) / sizeof(wchar_t);
        if (count < 2) { result.error = ERROR_INVALID_DATA; return result; }
        const auto unit = [&data, &header](const std::size_t index) {
            wchar_t value{};
            std::memcpy(&value, data.data() + header.pFiles + index * sizeof(wchar_t), sizeof(value));
            return value;
        };
        std::vector<std::wstring> paths;
        std::size_t cursor{}, path_count{};
        for (;;)
        {
            const auto begin = cursor;
            while (cursor < count && unit(cursor) != L'\0')
            {
                ++cursor;
                if (cursor - begin >= 32767) { result.error = ERROR_INVALID_DATA; return result; }
            }
            if (cursor == count) { result.error = ERROR_INVALID_DATA; return result; }
            if (cursor == begin)
            {
                // A list ends only after a nonempty, terminated path. Remaining
                // allocator padding may be zero, never another hidden path.
                if (!path_count) { result.error = ERROR_INVALID_DATA; return result; }
                for (auto tail = cursor; tail < count; ++tail)
                { if (unit(tail) != L'\0') { result.error = ERROR_INVALID_DATA; return result; } }
                if (path_count > MaxClipboardFileCount) { return result; }
                result.paths = std::move(paths); result.error = ERROR_SUCCESS;
                return result;
            }
            ++path_count;
            if (path_count <= MaxClipboardFileCount)
            {
                std::wstring path(cursor - begin, L'\0');
                std::memcpy(path.data(), data.data() + header.pFiles + begin * sizeof(wchar_t), path.size() * sizeof(wchar_t));
                paths.push_back(std::move(path));
            }
            ++cursor; // Consume this path's terminator; the list needs another.
        }
    }
    std::vector<std::byte> EncodeClipboardFilePath(const std::wstring& path)
    {
        return EncodeClipboardFilePaths({ &path, 1 });
    }
    ClipboardFilePath DecodeClipboardFilePath(const std::span<const std::byte> data)
    {
        return Single(DecodeClipboardFilePaths(data));
    }
    DWORD PublishClipboardText(HWND owner, const std::wstring& text, DWORD& sequence)
    {
        sequence = 0;
        if (!owner || text.empty() || text.find(L'\0') != std::wstring::npos) { return ERROR_INVALID_PARAMETER; }
        GlobalMemory memory{ CopyGlobal(text.c_str(), (text.size() + 1) * sizeof(wchar_t)) };
        if (!memory.value) { return ERROR_NOT_ENOUGH_MEMORY; }
        if (!OpenClipboard(owner)) { return LastError(); }
        const ClipboardScope scope;
        if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory.value)) { return LastError(); }
        memory.value = nullptr;
        sequence = GetClipboardSequenceNumber();
        return ERROR_SUCCESS;
    }
    DWORD PublishClipboardFilesTransfer(HWND owner, const std::span<const std::wstring> paths,
        const ClipboardTransferIntent intent, DWORD& sequence)
    {
        sequence = 0;
        const auto data = EncodeClipboardFilePaths(paths);
        if (!owner || data.empty() || !ClipboardDropEffectValue(intent)) { return ERROR_INVALID_PARAMETER; }
        const UINT format = RegisterClipboardFormatW(L"Preferred DropEffect");
        if (!format) { return LastError(); }
        const DWORD effect = ClipboardDropEffectValue(intent);
        GlobalMemory files{ CopyGlobal(data.data(), data.size()) }, preference{ CopyGlobal(&effect, sizeof(effect)) };
        if (!files.value || !preference.value) { return ERROR_NOT_ENOUGH_MEMORY; }
        if (!OpenClipboard(owner)) { return LastError(); }
        const ClipboardScope scope;
        if (!EmptyClipboard()) { return LastError(); }
        if (!SetClipboardData(CF_HDROP, files.value)) { return LastError(); }
        files.value = nullptr;
        if (!SetClipboardData(format, preference.value)) { return LastError(); }
        preference.value = nullptr;
        // Capture the sequence while this eagerly rendered, complete list is
        // owned by the open clipboard. Its ordered paths are never normalized.
        sequence = GetClipboardSequenceNumber();
        return ERROR_SUCCESS;
    }
    DWORD PublishClipboardFilesCopy(HWND owner, const std::span<const std::wstring> paths, DWORD& sequence)
    { return PublishClipboardFilesTransfer(owner, paths, ClipboardTransferIntent::Copy, sequence); }
    DWORD PublishClipboardFileCopy(HWND owner, const std::wstring& path, DWORD& sequence)
    {
        return PublishClipboardFilesCopy(owner, { &path, 1 }, sequence);
    }
    ClipboardFilePaths ReadClipboardFilesTransfer()
    {
        ClipboardFilePaths result;
        if (!OpenClipboard(nullptr)) { result.error = LastError(); return result; }
        const ClipboardScope scope;
        const UINT format = RegisterClipboardFormatW(L"Preferred DropEffect");
        if (!format) { result.error = LastError(); return result; }
        ClipboardTransferIntent intent = ClipboardTransferIntent::Copy;
        if (IsClipboardFormatAvailable(format))
        {
            const auto effect_data = GetClipboardData(format);
            if (!effect_data || GlobalSize(effect_data) < sizeof(DWORD)) { result.error = ERROR_INVALID_DATA; return result; }
            const auto* bytes = GlobalLock(effect_data);
            if (!bytes) { result.error = LastError(); return result; }
            const GlobalUnlockScope unlock{ effect_data };
            const auto effect = DecodeClipboardDropEffect({ static_cast<const std::byte*>(bytes), sizeof(DWORD) });
            if (effect.error) { result.error = effect.error; return result; }
            intent = effect.intent;
        }
        if (!IsClipboardFormatAvailable(CF_HDROP)) { return result; }
        const auto memory = GetClipboardData(CF_HDROP);
        if (!memory) { result.error = LastError(); return result; }
        const auto bytes = GlobalSize(memory);
        if (bytes > MaxClipboardFilePayloadBytes) { return result; }
        const auto* data = static_cast<const std::byte*>(GlobalLock(memory));
        if (!data) { result.error = LastError(); return result; }
        const GlobalUnlockScope unlock{ memory };
        result = DecodeClipboardFilePaths({ data, bytes });
        result.sequence = GetClipboardSequenceNumber();
        result.intent = intent;
        return result;
    }
    ClipboardFilePaths ReadClipboardFilesCopy()
    {
        auto result = ReadClipboardFilesTransfer();
        if (result.intent != ClipboardTransferIntent::Copy) { result.error = ERROR_NOT_SUPPORTED; result.paths.clear(); }
        return result;
    }
    ClipboardFilePath ReadClipboardFileCopy()
    {
        return Single(ReadClipboardFilesCopy());
    }
    namespace
    {
        DWORD CaptureSourceIdentity(const std::wstring& input, FileCopyRequest& request,
            const bool allow_directory = false)
        {
            auto path = input;
            if (path.starts_with(L"\\\\?\\")) { path.erase(0, 4); }
            if (path.size() < 4 || path.size() >= 32767 || path[1] != L':' || path[2] != L'\\' ||
                path.find(L'\0') != std::wstring::npos || path.find(L'/') != std::wstring::npos || path.find(L':', 2) != std::wstring::npos)
            { return ERROR_NOT_SUPPORTED; }
            const auto split = path.find_last_of(L'\\');
            const auto name = path.substr(split + 1);
            if (const auto error = ValidateNewFolderName(name); error != ERROR_SUCCESS) { return error; }
            const auto parent_path = path.substr(0, split == 2 ? 3 : split);
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
            const auto native_parent = L"\\\\?\\" + parent_path;
            blink::core::UniqueHandle initial(CreateFile3(native_parent.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, &parameters));
            if (!initial) { return LastError(); }
            blink::core::FileIdentity parent_identity;
            if (!blink::core::ReadFileIdentity(initial.get(), parent_identity)) { return LastError(); }
            initial.reset();
            auto lease = detail::AcquireDirectoryLease(parent_path, parent_identity);
            if (lease.error != ERROR_SUCCESS) { return lease.error; }
            const auto source_path = blink::core::JoinPath(lease.native_path, name);
            parameters.dwFileFlags |= FILE_FLAG_DISALLOW_PATH_REDIRECTS;
            blink::core::UniqueHandle file(CreateFile3(source_path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ, OPEN_EXISTING, &parameters));
            if (!file) { return LastError(); }
            FILE_ATTRIBUTE_TAG_INFO tag{}; FILE_STANDARD_INFO standard{};
            blink::core::FileEntry source;
            if (!GetFileInformationByHandleEx(file.get(), FileAttributeTagInfo, &tag, sizeof(tag)) ||
                !GetFileInformationByHandleEx(file.get(), FileStandardInfo, &standard, sizeof(standard)) ||
                !blink::core::ReadFileIdentity(file.get(), source.identity)) { return LastError(); }
            const bool directory = (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (standard.DeletePending || standard.EndOfFile.QuadPart < 0 ||
                directory != (standard.Directory != FALSE) || (directory && !allow_directory))
            { return ERROR_NOT_SUPPORTED; }
            if (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { return ERROR_NOT_SUPPORTED; }
            source.name = name; source.full_path = path; source.extension = blink::core::FileExtension(name);
            source.is_directory = directory;
            source.attributes = tag.FileAttributes; source.size = standard.EndOfFile.QuadPart;
            request.source_parent = lease.path; request.source_parent_identity = parent_identity;
            request.source = std::move(source);
            return ERROR_SUCCESS;
        }
    }
    DWORD CaptureFileCopySource(const std::wstring& path, FileCopyRequest& request)
    {
        return CaptureSourceIdentity(path, request);
    }

    DWORD CaptureFileCopySources(const std::span<const std::wstring> paths, std::vector<FileCopyRequest>& requests,
        const FileOperationCancellation& cancelled)
    {
        requests.clear();
        std::size_t bytes{};
        if (!EncodedSize(paths, bytes)) { return ERROR_INVALID_PARAMETER; }
        const auto stopping = [&cancelled] { return cancelled && cancelled->load(); };
        if (stopping()) { return ERROR_CANCELLED; }
        try
        {
            std::vector<FileCopyRequest> pending;
            pending.reserve(paths.size());
            for (const auto& path : paths)
            {
                if (stopping()) { return ERROR_CANCELLED; }
                FileCopyRequest captured;
                const auto error = CaptureSourceIdentity(path, captured);
                if (error != ERROR_SUCCESS) { return error; }
                pending.push_back(std::move(captured));
            }
            if (stopping()) { return ERROR_CANCELLED; }
            requests = std::move(pending);
            return ERROR_SUCCESS;
        }
        catch (const std::bad_alloc&) { return ERROR_NOT_ENOUGH_MEMORY; }
        catch (...) { return ERROR_GEN_FAILURE; }
    }

    DWORD CaptureClipboardCopySources(const std::span<const std::wstring> paths, ClipboardCopySources& output,
        const FileOperationCancellation& cancelled)
    {
        output.emplace<std::monostate>();
        std::size_t bytes{};
        if (!EncodedSize(paths, bytes)) { return ERROR_INVALID_PARAMETER; }
        const auto stopping = [&cancelled] { return cancelled && cancelled->load(); };
        if (stopping()) { return ERROR_CANCELLED; }
        try
        {
            constexpr DWORD ordinary_attributes = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
                FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NORMAL |
                FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
            std::vector<FileCopyRequest> pending;
            pending.reserve(paths.size());
            std::size_t directories{};
            for (const auto& path : paths)
            {
                if (stopping()) { return ERROR_CANCELLED; }
                FileCopyRequest captured;
                const auto error = CaptureSourceIdentity(path, captured, true);
                if (error != ERROR_SUCCESS) { return error; }
                if (stopping()) { return ERROR_CANCELLED; }
                const auto allowed = ordinary_attributes |
                    (captured.source.is_directory ? FILE_ATTRIBUTE_DIRECTORY : 0);
                if (captured.source.attributes & ~allowed) { return ERROR_NOT_SUPPORTED; }
                if (captured.source.is_directory) { ++directories; }
                pending.push_back(std::move(captured));
            }
            if (stopping()) { return ERROR_CANCELLED; }
            if (!directories)
            {
                output.emplace<std::vector<FileCopyRequest>>(std::move(pending));
            }
            else
            {
                if (pending.size() == 1)
                { output.emplace<DirectoryCopyRequest>(DirectoryCopyProjection(pending.front())); }
                else
                {
                    CopyItemRequests items; items.reserve(pending.size());
                    for (auto& captured : pending)
                    {
                        if (captured.source.is_directory) { items.emplace_back(DirectoryCopyProjection(captured)); }
                        else { items.emplace_back(std::move(captured)); }
                    }
                    output.emplace<CopyItemRequests>(std::move(items));
                }
            }
            return ERROR_SUCCESS;
        }
        catch (const std::bad_alloc&)
        { output.emplace<std::monostate>(); return ERROR_NOT_ENOUGH_MEMORY; }
        catch (...)
        { output.emplace<std::monostate>(); return ERROR_GEN_FAILURE; }
    }
}
