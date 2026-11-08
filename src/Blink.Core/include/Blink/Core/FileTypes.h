#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace blink::core
{
    class UniqueHandle
    {
    public:
        UniqueHandle() noexcept = default;
        explicit UniqueHandle(HANDLE value) noexcept : value_(value) {}
        ~UniqueHandle() noexcept;

        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle& operator=(const UniqueHandle&) = delete;

        UniqueHandle(UniqueHandle&& other) noexcept;
        UniqueHandle& operator=(UniqueHandle&& other) noexcept;

        [[nodiscard]] HANDLE get() const noexcept { return value_; }
        [[nodiscard]] bool valid() const noexcept;
        explicit operator bool() const noexcept { return valid(); }
        void reset(HANDLE value = INVALID_HANDLE_VALUE) noexcept;
        HANDLE release() noexcept;

    private:
        HANDLE value_{ INVALID_HANDLE_VALUE };
    };

    struct FileIdentity
    {
        std::uint64_t volume_serial{};
        std::array<std::uint8_t, 16> file_id{};
        bool valid{};

        [[nodiscard]] std::wstring ToString() const;
        friend bool operator==(const FileIdentity&, const FileIdentity&) = default;
    };

    struct FileEntry
    {
        std::wstring name;
        std::wstring full_path;
        std::wstring extension;
        bool is_directory{};
        std::uint64_t size{};
        std::uint64_t allocated_size{};
        FILETIME creation_time{};
        FILETIME last_access_time{};
        FILETIME last_write_time{};
        DWORD attributes{};
        DWORD reparse_tag{};
        FileIdentity identity{};
    };

    struct DirectoryListing
    {
        std::wstring path;
        std::vector<FileEntry> entries;
        DWORD error{};
        bool used_extended_directory_info{};
        FileIdentity directory_identity{};
    };

    [[nodiscard]] std::wstring Win32ErrorMessage(DWORD error);
    [[nodiscard]] std::wstring NormalizePath(const std::wstring& path);
    [[nodiscard]] std::wstring JoinPath(const std::wstring& parent, const std::wstring& child);
    [[nodiscard]] std::wstring FileExtension(const std::wstring& name);

    [[nodiscard]] UniqueHandle OpenPathForMetadata(const std::wstring& path, bool directory);
    [[nodiscard]] bool ReadFileIdentity(HANDLE handle, FileIdentity& identity);
    [[nodiscard]] bool ReadEntryMetadata(const std::wstring& path, FileEntry& entry);
    [[nodiscard]] DirectoryListing EnumerateDirectory(const std::wstring& path);

    [[nodiscard]] bool CreateDirectoryModern(const std::wstring& path, DWORD* error = nullptr);
    [[nodiscard]] bool RenameFileModern(const std::wstring& source, const std::wstring& destination, DWORD* error = nullptr);
    [[nodiscard]] bool DeleteFileModern(const std::wstring& path, DWORD* error = nullptr);
    [[nodiscard]] bool RemoveDirectoryModern(const std::wstring& path, DWORD* error = nullptr);
}
