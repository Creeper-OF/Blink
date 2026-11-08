#include "Blink/App/DirectoryLease.h"
#include <utility>
#include <string_view>

namespace blink::app::detail
{
    namespace
    {
        std::wstring LocalFolder(std::wstring path)
        {
            if (path.starts_with(L"\\\\?\\")) { path.erase(0, 4); }
            if (path.size() < 3 || path.size() > 32760 || path[1] != L':' || path[2] != L'\\' ||
                !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
                path.find(L':', 2) != std::wstring::npos || path.find(L'/') != std::wstring::npos ||
                path.find(L'\0') != std::wstring::npos) { return {}; }
            while (path.size() > 3 && path.back() == L'\\') { path.pop_back(); }
            std::size_t offset = 3;
            while (offset < path.size())
            {
                const auto end = path.find(L'\\', offset);
                const auto component = std::wstring_view(path).substr(offset,
                    end == std::wstring::npos ? path.size() - offset : end - offset);
                if (component.empty() || component == L"." || component == L"..") { return {}; }
                if (end == std::wstring::npos) { break; }
                offset = end + 1;
            }
            return path;
        }
    }
    DirectoryLease AcquireDirectoryLease(const std::wstring& path, const blink::core::FileIdentity& expected,
        const std::shared_ptr<std::atomic_bool>& cancelled)
    {
        DirectoryLease lease;
        const auto fail = [&lease](DWORD error) {
            lease.error = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
            return std::move(lease);
        };
        const auto stopping = [&cancelled] { return cancelled && cancelled->load(); };
        lease.path = LocalFolder(path);
        if (lease.path.empty()) { return fail(ERROR_NOT_SUPPORTED); }
        if (!expected.valid) { return fail(ERROR_FILE_INVALID); }
        if (stopping()) { return fail(ERROR_CANCELLED); }
        const auto type = GetDriveTypeW(lease.path.substr(0, 3).c_str());
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) { return fail(ERROR_NOT_SUPPORTED); }
        std::size_t end = 3;
        for (;;)
        {
            if (stopping()) { return fail(ERROR_CANCELLED); }
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
            const auto native = L"\\\\?\\" + lease.path.substr(0, end);
            blink::core::UniqueHandle directory(CreateFile3(native.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, &parameters));
            if (!directory) { return fail(GetLastError()); }
            FILE_ATTRIBUTE_TAG_INFO attributes{};
            if (!GetFileInformationByHandleEx(directory.get(), FileAttributeTagInfo, &attributes, sizeof(attributes)))
            { return fail(GetLastError()); }
            if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { return fail(ERROR_NOT_SUPPORTED); }
            if ((attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) { return fail(ERROR_DIRECTORY); }
            blink::core::FileIdentity identity;
            if (!blink::core::ReadFileIdentity(directory.get(), identity)) { return fail(GetLastError()); }
            lease.handles.push_back(std::move(directory));
            if (end == lease.path.size())
            {
                if (identity != expected) { return fail(ERROR_FILE_INVALID); }
                break;
            }
            const auto next = lease.path.find(L'\\', end == 3 ? 3 : end + 1);
            end = next == std::wstring::npos ? lease.path.size() : next;
        }
        lease.native_path.assign(32768, L'\0');
        const auto length = GetFinalPathNameByHandleW(lease.handles.back().get(), lease.native_path.data(),
            static_cast<DWORD>(lease.native_path.size()), FILE_NAME_OPENED | VOLUME_NAME_DOS);
        if (!length) { return fail(GetLastError()); }
        if (length >= lease.native_path.size()) { return fail(ERROR_FILENAME_EXCED_RANGE); }
        lease.native_path.resize(length);
        const auto resolved = LocalFolder(lease.native_path);
        if (resolved.empty() || CompareStringOrdinal(resolved.c_str(), 3, lease.path.c_str(), 3, TRUE) != CSTR_EQUAL)
        { return fail(ERROR_NOT_SUPPORTED); }
        if (stopping()) { return fail(ERROR_CANCELLED); }
        return lease;
    }
}
