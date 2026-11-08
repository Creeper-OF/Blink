#include "Blink/Index/DirectoryObserver.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#pragma comment(lib, "Kernel32.lib")

namespace blink::index
{
    namespace
    {
        void SetError(DWORD* error, DWORD value) noexcept
        {
            if (error != nullptr)
            {
                *error = value;
            }
        }

        [[nodiscard]] blink::core::UniqueHandle OpenDirectory(const std::wstring& path)
        {
            CREATEFILE3_EXTENDED_PARAMETERS parameters{};
            parameters.dwSize = sizeof(parameters);
            parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            parameters.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED;
            const HANDLE handle = CreateFile3(
                path.c_str(),
                FILE_LIST_DIRECTORY | SYNCHRONIZE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                OPEN_EXISTING,
                &parameters);
            return blink::core::UniqueHandle(handle);
        }
    }

    std::vector<DirectoryChange> WatchDirectoryOnce(
        const std::wstring& path,
        std::chrono::milliseconds timeout,
        bool watch_subtree,
        DWORD* error)
    {
        SetError(error, ERROR_SUCCESS);
        const std::wstring normalized = blink::core::NormalizePath(path);
        auto directory = OpenDirectory(normalized);
        if (!directory)
        {
            SetError(error, GetLastError());
            return {};
        }

        const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr)
        {
            SetError(error, GetLastError());
            return {};
        }
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;

        std::vector<BYTE> buffer(64 * 1024);
        const DWORD filter =
            FILE_NOTIFY_CHANGE_FILE_NAME |
            FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE |
            FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_ATTRIBUTES |
            FILE_NOTIFY_CHANGE_SECURITY;
        const BOOL started = ReadDirectoryChangesExW(
            directory.get(),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            watch_subtree ? TRUE : FALSE,
            filter,
            nullptr,
            &overlapped,
            nullptr,
            ReadDirectoryNotifyExtendedInformation);
        if (!started)
        {
            const DWORD start_error = GetLastError();
            CloseHandle(event);
            SetError(error, start_error);
            return {};
        }

        const auto wait_ms = timeout.count() <= 0
            ? 0
            : static_cast<DWORD>(std::min<std::int64_t>(timeout.count(), MAXDWORD));
        const DWORD wait_result = WaitForSingleObject(event, wait_ms);
        if (wait_result == WAIT_TIMEOUT)
        {
            (void)CancelIoEx(directory.get(), &overlapped);
            (void)WaitForSingleObject(event, 100);
            CloseHandle(event);
            SetError(error, ERROR_TIMEOUT);
            return {};
        }
        if (wait_result != WAIT_OBJECT_0)
        {
            const DWORD wait_error = GetLastError();
            (void)CancelIoEx(directory.get(), &overlapped);
            CloseHandle(event);
            SetError(error, wait_error);
            return {};
        }

        DWORD bytes = 0;
        if (!GetOverlappedResult(directory.get(), &overlapped, &bytes, FALSE))
        {
            const DWORD result_error = GetLastError();
            CloseHandle(event);
            SetError(error, result_error);
            return {};
        }
        CloseHandle(event);

        std::vector<DirectoryChange> changes;
        std::size_t offset = 0;
        while (offset + offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, FileName) <= bytes)
        {
            const auto* information = reinterpret_cast<const FILE_NOTIFY_EXTENDED_INFORMATION*>(buffer.data() + offset);
            const std::size_t name_bytes = information->FileNameLength;
            const std::size_t name_offset = offsetof(FILE_NOTIFY_EXTENDED_INFORMATION, FileName);
            if (name_offset + name_bytes > bytes - offset)
            {
                SetError(error, ERROR_INVALID_DATA);
                return changes;
            }

            DirectoryChange change;
            change.action = information->Action;
            change.name.assign(
                information->FileName,
                information->FileNameLength / sizeof(wchar_t));
            change.full_path = blink::core::JoinPath(normalized, change.name);
            change.file_id = static_cast<std::uint64_t>(information->FileId.QuadPart);
            change.parent_file_id = static_cast<std::uint64_t>(information->ParentFileId.QuadPart);
            change.size = static_cast<std::uint64_t>(information->FileSize.QuadPart);
            change.attributes = information->FileAttributes;
            changes.push_back(std::move(change));

            if (information->NextEntryOffset == 0)
            {
                break;
            }
            if (information->NextEntryOffset > bytes - offset)
            {
                SetError(error, ERROR_INVALID_DATA);
                return changes;
            }
            offset += information->NextEntryOffset;
        }
        return changes;
    }
}
