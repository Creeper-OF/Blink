#include "Blink/Core/FileTypes.h"

#include <fileapi.h>
#include <winbase.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "Kernel32.lib")

namespace blink::core
{
    namespace
    {
        [[nodiscard]] bool IsInvalidHandle(HANDLE handle) noexcept
        {
            return handle == nullptr || handle == INVALID_HANDLE_VALUE;
        }

        [[nodiscard]] FILETIME ToFileTime(const LARGE_INTEGER& value) noexcept
        {
            FILETIME result{};
            result.dwLowDateTime = value.LowPart;
            result.dwHighDateTime = static_cast<DWORD>(value.HighPart);
            return result;
        }

        [[nodiscard]] std::uint64_t ToUInt64(const LARGE_INTEGER& value) noexcept
        {
            return value.QuadPart < 0 ? 0 : static_cast<std::uint64_t>(value.QuadPart);
        }

        void SetError(DWORD* error, DWORD value) noexcept
        {
            if (error != nullptr)
            {
                *error = value;
            }
        }

        [[nodiscard]] FileEntry EntryFromExtendedInfo(
            const FILE_ID_EXTD_DIR_INFO& info,
            const std::wstring& parent,
            std::uint64_t volume_serial)
        {
            FileEntry entry;
            entry.name.assign(info.FileName, info.FileNameLength / sizeof(wchar_t));
            entry.full_path = JoinPath(parent, entry.name);
            entry.extension = FileExtension(entry.name);
            entry.is_directory = (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            entry.size = ToUInt64(info.EndOfFile);
            entry.allocated_size = ToUInt64(info.AllocationSize);
            entry.creation_time = ToFileTime(info.CreationTime);
            entry.last_access_time = ToFileTime(info.LastAccessTime);
            entry.last_write_time = ToFileTime(info.LastWriteTime);
            entry.attributes = info.FileAttributes;
            entry.reparse_tag = info.ReparsePointTag;
            entry.identity.volume_serial = volume_serial;
            std::copy(std::begin(info.FileId.Identifier), std::end(info.FileId.Identifier), entry.identity.file_id.begin());
            entry.identity.valid = true;
            return entry;
        }

        [[nodiscard]] DirectoryListing EnumerateWithFindFirstFile(const std::wstring& path, DWORD first_error)
        {
            DirectoryListing listing;
            listing.path = path;
            listing.error = first_error;

            WIN32_FIND_DATAW find_data{};
            const std::wstring pattern = JoinPath(path, L"*");
            const HANDLE find_handle = FindFirstFileExW(
                pattern.c_str(),
                FindExInfoBasic,
                &find_data,
                FindExSearchNameMatch,
                nullptr,
                FIND_FIRST_EX_LARGE_FETCH);

            if (IsInvalidHandle(find_handle))
            {
                listing.error = GetLastError();
                return listing;
            }

            listing.error = ERROR_SUCCESS;
            do
            {
                const std::wstring name(find_data.cFileName);
                if (name == L"." || name == L"..")
                {
                    continue;
                }

                FileEntry entry;
                entry.name = name;
                entry.full_path = JoinPath(path, name);
                entry.extension = FileExtension(name);
                entry.is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                entry.attributes = find_data.dwFileAttributes;
                entry.creation_time = find_data.ftCreationTime;
                entry.last_access_time = find_data.ftLastAccessTime;
                entry.last_write_time = find_data.ftLastWriteTime;
                entry.size = (static_cast<std::uint64_t>(find_data.nFileSizeHigh) << 32) | find_data.nFileSizeLow;
                entry.reparse_tag = find_data.dwReserved0;

                if (!ReadEntryMetadata(entry.full_path, entry))
                {
                    // A directory can disappear between enumeration and metadata read.
                    // Keep the entry with the enumeration data instead of failing the whole listing.
                    entry.identity = {};
                }
                listing.entries.push_back(std::move(entry));
            } while (FindNextFileW(find_handle, &find_data));

            const DWORD final_error = GetLastError();
            FindClose(find_handle);
            if (final_error != ERROR_NO_MORE_FILES)
            {
                listing.error = final_error;
            }
            return listing;
        }
    }

    UniqueHandle::~UniqueHandle() noexcept
    {
        reset();
    }

    UniqueHandle::UniqueHandle(UniqueHandle&& other) noexcept : value_(other.release())
    {
    }

    UniqueHandle& UniqueHandle::operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.release());
        }
        return *this;
    }

    bool UniqueHandle::valid() const noexcept
    {
        return !IsInvalidHandle(value_);
    }

    void UniqueHandle::reset(HANDLE value) noexcept
    {
        if (valid())
        {
            CloseHandle(value_);
        }
        value_ = value;
    }

    HANDLE UniqueHandle::release() noexcept
    {
        HANDLE value = value_;
        value_ = INVALID_HANDLE_VALUE;
        return value;
    }

    std::wstring FileIdentity::ToString() const
    {
        if (!valid)
        {
            return L"unavailable";
        }

        std::wstringstream stream;
        stream << std::hex << std::setfill(L'0') << std::setw(16) << volume_serial << L":";
        for (const auto byte : file_id)
        {
            stream << std::setw(2) << static_cast<unsigned int>(byte);
        }
        return stream.str();
    }

    std::wstring Win32ErrorMessage(DWORD error)
    {
        if (error == ERROR_SUCCESS)
        {
            return L"OK";
        }

        wchar_t* buffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error,
            0,
            reinterpret_cast<wchar_t*>(&buffer),
            0,
            nullptr);
        if (length == 0 || buffer == nullptr)
        {
            return L"Win32 error " + std::to_wstring(error);
        }

        std::wstring message(buffer, length);
        LocalFree(buffer);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
        {
            message.pop_back();
        }
        return message;
    }

    std::wstring NormalizePath(const std::wstring& path)
    {
        if (path.empty())
        {
            return {};
        }

        std::wstring result(32768, L'\0');
        const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(result.size()), result.data(), nullptr);
        if (length == 0)
        {
            return path;
        }
        if (length >= result.size())
        {
            result.resize(length + 1);
            const DWORD retry_length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(result.size()), result.data(), nullptr);
            if (retry_length == 0)
            {
                return path;
            }
            result.resize(retry_length);
            return result;
        }
        result.resize(length);
        return result;
    }

    std::wstring JoinPath(const std::wstring& parent, const std::wstring& child)
    {
        if (parent.empty())
        {
            return child;
        }
        if (parent.back() == L'\\' || parent.back() == L'/')
        {
            return parent + child;
        }
        return parent + L"\\" + child;
    }

    std::wstring FileExtension(const std::wstring& name)
    {
        const std::size_t separator = name.find_last_of(L"\\/");
        const std::size_t dot = name.find_last_of(L'.');
        if (dot == std::wstring::npos || (separator != std::wstring::npos && dot < separator) || dot == 0)
        {
            return {};
        }
        std::wstring extension = name.substr(dot);
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        return extension;
    }

    UniqueHandle OpenPathForMetadata(const std::wstring& path, bool directory)
    {
        CREATEFILE3_EXTENDED_PARAMETERS parameters{};
        parameters.dwSize = sizeof(parameters);
        parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT;
        if (directory)
        {
            parameters.dwFileFlags |= FILE_FLAG_BACKUP_SEMANTICS;
        }

        const DWORD desired_access = directory ? (FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES) : FILE_READ_ATTRIBUTES;
        const HANDLE handle = CreateFile3(
            path.c_str(),
            desired_access,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            OPEN_EXISTING,
            &parameters);
        return UniqueHandle(handle);
    }

    bool ReadFileIdentity(HANDLE handle, FileIdentity& identity)
    {
        identity = {};
        if (IsInvalidHandle(handle))
        {
            return false;
        }

        FILE_ID_INFO file_id{};
        if (!GetFileInformationByHandleEx(handle, FileIdInfo, &file_id, sizeof(file_id)))
        {
            return false;
        }

        identity.volume_serial = file_id.VolumeSerialNumber;
        std::copy(std::begin(file_id.FileId.Identifier), std::end(file_id.FileId.Identifier), identity.file_id.begin());
        identity.valid = true;
        return true;
    }

    bool ReadEntryMetadata(const std::wstring& path, FileEntry& entry)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            return false;
        }

        const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        UniqueHandle handle = OpenPathForMetadata(path, directory);
        if (!handle)
        {
            return false;
        }

        FILE_BASIC_INFO basic{};
        FILE_STANDARD_INFO standard{};
        FILE_ATTRIBUTE_TAG_INFO tag{};
        const bool basic_ok = GetFileInformationByHandleEx(handle.get(), FileBasicInfo, &basic, sizeof(basic));
        const bool standard_ok = GetFileInformationByHandleEx(handle.get(), FileStandardInfo, &standard, sizeof(standard));
        const bool tag_ok = GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &tag, sizeof(tag));

        entry.attributes = attributes;
        entry.is_directory = directory;
        entry.size = standard_ok ? ToUInt64(standard.EndOfFile) : entry.size;
        entry.allocated_size = standard_ok ? ToUInt64(standard.AllocationSize) : entry.allocated_size;
        if (basic_ok)
        {
            entry.creation_time = ToFileTime(basic.CreationTime);
            entry.last_access_time = ToFileTime(basic.LastAccessTime);
            entry.last_write_time = ToFileTime(basic.LastWriteTime);
        }
        entry.reparse_tag = tag_ok ? tag.ReparseTag : entry.reparse_tag;
        return ReadFileIdentity(handle.get(), entry.identity);
    }

    DirectoryListing EnumerateDirectory(const std::wstring& path)
    {
        DirectoryListing listing;
        listing.path = NormalizePath(path);
        if (listing.path.empty())
        {
            listing.error = ERROR_INVALID_NAME;
            return listing;
        }

        UniqueHandle directory = OpenPathForMetadata(listing.path, true);
        if (!directory)
        {
            listing.error = GetLastError();
            return listing;
        }

        FILE_ID_INFO parent_identity{};
        if (!GetFileInformationByHandleEx(directory.get(), FileIdInfo, &parent_identity, sizeof(parent_identity)))
        {
            return EnumerateWithFindFirstFile(listing.path, GetLastError());
        }

        listing.directory_identity.volume_serial = parent_identity.VolumeSerialNumber;
        std::copy(std::begin(parent_identity.FileId.Identifier), std::end(parent_identity.FileId.Identifier),
            listing.directory_identity.file_id.begin());
        listing.directory_identity.valid = true;

        std::vector<std::byte> buffer(128 * 1024);
        DWORD returned = 0;
        bool restart = true;
        while (true)
        {
            const FILE_INFO_BY_HANDLE_CLASS info_class = restart ? FileIdExtdDirectoryRestartInfo : FileIdExtdDirectoryInfo;
            if (!GetFileInformationByHandleEx(directory.get(), info_class, buffer.data(), static_cast<DWORD>(buffer.size())))
            {
                const DWORD error = GetLastError();
                if (error == ERROR_NO_MORE_FILES)
                {
                    listing.error = ERROR_SUCCESS;
                    listing.used_extended_directory_info = true;
                    return listing;
                }
                return EnumerateWithFindFirstFile(listing.path, error);
            }

            returned = 0;
            // GetFileInformationByHandleEx does not expose a byte count for directory info.
            // The buffer is therefore walked until NextEntryOffset is zero; the last record
            // is bounded by the allocated buffer and has a valid FileNameLength.
            while (returned < buffer.size())
            {
                auto* info = reinterpret_cast<const FILE_ID_EXTD_DIR_INFO*>(buffer.data() + returned);
                const std::size_t record_size = sizeof(FILE_ID_EXTD_DIR_INFO) - sizeof(wchar_t) + info->FileNameLength;
                if (record_size > buffer.size() - returned || info->FileNameLength % sizeof(wchar_t) != 0)
                {
                    return EnumerateWithFindFirstFile(listing.path, ERROR_INVALID_DATA);
                }

                const std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
                if (name != L"." && name != L"..")
                {
                    listing.entries.push_back(EntryFromExtendedInfo(*info, listing.path, parent_identity.VolumeSerialNumber));
                }

                if (info->NextEntryOffset == 0)
                {
                    break;
                }
                if (info->NextEntryOffset > buffer.size() - returned)
                {
                    return EnumerateWithFindFirstFile(listing.path, ERROR_INVALID_DATA);
                }
                returned += info->NextEntryOffset;
            }
            restart = false;
        }
    }

    bool CreateDirectoryModern(const std::wstring& path, DWORD* error)
    {
        const std::wstring normalized = NormalizePath(path);
        const HANDLE handle = CreateDirectory2W(
            normalized.c_str(),
            FILE_LIST_DIRECTORY | SYNCHRONIZE,
            FILE_SHARE_READ,
            DIRECTORY_FLAGS_DISALLOW_PATH_REDIRECTS,
            nullptr);
        if (!IsInvalidHandle(handle))
        {
            CloseHandle(handle);
            SetError(error, ERROR_SUCCESS);
            return true;
        }
        SetError(error, GetLastError());
        return false;
    }

    bool RenameFileModern(const std::wstring& source, const std::wstring& destination, DWORD* error)
    {
        const std::wstring normalized_source = NormalizePath(source);
        const std::wstring normalized_destination = NormalizePath(destination);
        CREATEFILE3_EXTENDED_PARAMETERS parameters{};
        parameters.dwSize = sizeof(parameters);
        parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT;
        const HANDLE raw_handle = CreateFile3(
            normalized_source.c_str(),
            DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            OPEN_EXISTING,
            &parameters);
        UniqueHandle handle(raw_handle);
        if (!handle)
        {
            SetError(error, GetLastError());
            return false;
        }

        const std::size_t bytes = sizeof(FILE_RENAME_INFO) + (normalized_destination.size() == 0 ? 0 : (normalized_destination.size() - 1) * sizeof(wchar_t));
        std::vector<std::byte> buffer(bytes);
        auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
        rename->Flags = 0;
        rename->RootDirectory = nullptr;
        rename->FileNameLength = static_cast<DWORD>(normalized_destination.size() * sizeof(wchar_t));
        std::copy(normalized_destination.begin(), normalized_destination.end(), rename->FileName);
        if (!SetFileInformationByHandle(handle.get(), FileRenameInfoEx, rename, static_cast<DWORD>(buffer.size())))
        {
            SetError(error, GetLastError());
            return false;
        }
        SetError(error, ERROR_SUCCESS);
        return true;
    }

    bool DeleteFileModern(const std::wstring& path, DWORD* error)
    {
        const std::wstring normalized = NormalizePath(path);
        const BOOL result = DeleteFile2W(normalized.c_str(), FILE_FLAG_DISALLOW_PATH_REDIRECTS);
        if (result)
        {
            SetError(error, ERROR_SUCCESS);
            return true;
        }
        SetError(error, GetLastError());
        return false;
    }

    bool RemoveDirectoryModern(const std::wstring& path, DWORD* error)
    {
        const std::wstring normalized = NormalizePath(path);
        const BOOL result = RemoveDirectory2W(normalized.c_str(), DIRECTORY_FLAGS_DISALLOW_PATH_REDIRECTS);
        if (result)
        {
            SetError(error, ERROR_SUCCESS);
            return true;
        }
        SetError(error, GetLastError());
        return false;
    }
}
