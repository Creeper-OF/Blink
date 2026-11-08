#include "Blink/App/HandleRename.h"
#include "Blink/App/DirectoryLease.h"
#include <winternl.h>
#include <algorithm>
#include <cstddef>

namespace blink::app::detail
{
    DWORD RenameRelativeByHandle(HANDLE source, HANDLE parent, const std::wstring& leaf, const bool replace_existing) noexcept
    {
        try
        {
            if (const auto error = ValidateNewFolderName(leaf); error != ERROR_SUCCESS) { return error; }
            if (!source || source == INVALID_HANDLE_VALUE || !parent || parent == INVALID_HANDLE_VALUE) { return ERROR_INVALID_HANDLE; }
            using SetInformation = NTSTATUS (NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);
            using MapStatus = ULONG (WINAPI*)(NTSTATUS);
            struct Exports { SetInformation set; MapStatus map; };
            static const auto exports = [] {
                const auto module = GetModuleHandleW(L"ntdll.dll");
                return Exports{ module ? reinterpret_cast<SetInformation>(GetProcAddress(module, "NtSetInformationFile")) : nullptr,
                    module ? reinterpret_cast<MapStatus>(GetProcAddress(module, "RtlNtStatusToDosError")) : nullptr };
            }();
            if (!exports.set || !exports.map) { return ERROR_NOT_SUPPORTED; }
            // FILE_RENAME_INFORMATION and FILE_RENAME_INFO share this documented
            // layout. Only explicit file replacement sets flag1; no POSIX
            // replacement, readonly override, or bypass-access-check class.
            static_assert(offsetof(FILE_RENAME_INFO, FileName) == 20);
            std::vector<std::byte> buffer(sizeof(FILE_RENAME_INFO) + leaf.size() * sizeof(wchar_t));
            auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
            info->Flags = replace_existing ? FILE_RENAME_FLAG_REPLACE_IF_EXISTS : 0; info->RootDirectory = parent;
            info->FileNameLength = static_cast<DWORD>(leaf.size() * sizeof(wchar_t));
            std::copy(leaf.begin(), leaf.end(), info->FileName);
            IO_STATUS_BLOCK io{};
            auto status = exports.set(source, &io, info, static_cast<ULONG>(buffer.size()), static_cast<FILE_INFORMATION_CLASS>(65));
            // Caller owns a synchronous source handle with SYNCHRONIZE until
            // return. There is no UI wait or forced cancellation during rename.
            if (status == static_cast<NTSTATUS>(0x103))
            {
                const auto waited = WaitForSingleObject(source, INFINITE);
                if (waited != WAIT_OBJECT_0) { return waited == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE; }
                status = io.Status;
            }
            return status >= 0 ? ERROR_SUCCESS : exports.map(status);
        }
        catch (const std::bad_alloc&) { return ERROR_NOT_ENOUGH_MEMORY; }
        catch (...) { return ERROR_GEN_FAILURE; }
    }

    DWORD ReadFileReplacementTarget(const DirectoryLease& parent, const std::wstring& leaf,
        const blink::core::FileIdentity& source, blink::core::FileIdentity& target) noexcept
    {
        target = {};
        try
        {
            if (const auto error = ValidateNewFolderName(leaf); error) { return error; }
            if (parent.error || parent.handles.empty()) { return parent.error ? parent.error : ERROR_INVALID_HANDLE; }
            for (const auto& handle : parent.handles)
            {
                FILE_ATTRIBUTE_TAG_INFO tag{};
                if (!GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &tag, sizeof(tag))) { return GetLastError(); }
                if (!(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                { return ERROR_CANT_RESOLVE_FILENAME; }
            }
            const auto path = blink::core::JoinPath(parent.native_path, leaf);
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_DISALLOW_PATH_REDIRECTS;
            blink::core::UniqueHandle file(CreateFile3(path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, &parameters));
            if (!file) { const auto error = GetLastError(); return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error; }
            FILE_ATTRIBUTE_TAG_INFO tag{}; FILE_STANDARD_INFO standard{}; blink::core::FileIdentity identity;
            if (!GetFileInformationByHandleEx(file.get(), FileAttributeTagInfo, &tag, sizeof(tag)) ||
                !GetFileInformationByHandleEx(file.get(), FileStandardInfo, &standard, sizeof(standard)) ||
                !blink::core::ReadFileIdentity(file.get(), identity)) { return GetLastError(); }
            if (standard.Directory || (tag.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || tag.ReparseTag)
            { return ERROR_NOT_SUPPORTED; }
            if (standard.DeletePending || !identity.valid) { return ERROR_FILE_INVALID; }
            if (identity == source) { return ERROR_INVALID_PARAMETER; }
            if (tag.FileAttributes & FILE_ATTRIBUTE_READONLY) { return ERROR_ACCESS_DENIED; }
            target = identity; return ERROR_SUCCESS;
        }
        catch (const std::bad_alloc&) { return ERROR_NOT_ENOUGH_MEMORY; }
        catch (...) { return ERROR_GEN_FAILURE; }
    }
}
