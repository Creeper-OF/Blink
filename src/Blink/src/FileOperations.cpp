#include "Blink/App/FileOperations.h"
#include <algorithm>
#include <vector>

namespace blink::app
{
    namespace
    {
        bool Equal(std::wstring_view left, std::wstring_view right) noexcept
        {
            return left.size() == right.size() && CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
        }
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
        std::wstring Extended(const std::wstring& path) { return L"\\\\?\\" + path; }
        bool Cancelled(const FileOperationCancellation& cancelled) noexcept
        { return cancelled && cancelled->load(); }
    }
    DWORD ValidateNewFolderName(const std::wstring_view name) noexcept
    {
        if (name.empty() || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ')
        { return ERROR_INVALID_NAME; }
        if (name.size() > 255) { return ERROR_FILENAME_EXCED_RANGE; }
        if (std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 32; }) ||
            name.find_first_of(L"\\/:*?\"<>|") != std::wstring_view::npos) { return ERROR_INVALID_NAME; }
        auto base = name.substr(0, name.find(L'.'));
        while (!base.empty() && base.back() == L' ') { base.remove_suffix(1); }
        for (const auto device : { L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$" })
        { if (Equal(base, device)) { return ERROR_INVALID_NAME; } }
        if (base.size() == 4 && (Equal(base.substr(0, 3), L"COM") || Equal(base.substr(0, 3), L"LPT")))
        {
            const auto digit = base[3];
            if ((digit >= L'1' && digit <= L'9') || digit == L'\x00b9' || digit == L'\x00b2' || digit == L'\x00b3')
            { return ERROR_INVALID_NAME; }
        }
        return ERROR_SUCCESS;
    }
    NewFolderResult CreateFolderSafely(const NewFolderRequest& request,
        const FileOperationCancellation& cancelled, const CreateDirectoryFunction& create)
    {
        NewFolderResult result;
        const auto fail = [&result](DWORD error) {
            result.error = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
            return result;
        };
        if (const auto error = ValidateNewFolderName(request.name); error != ERROR_SUCCESS) { return fail(error); }
        const auto parent = LocalFolder(request.parent);
        if (parent.empty()) { return fail(ERROR_NOT_SUPPORTED); }
        if (!request.parent_identity.valid) { return fail(ERROR_FILE_INVALID); }
        result.path = blink::core::JoinPath(parent, request.name);
        if (result.path.size() + 4 >= 32767) { return fail(ERROR_FILENAME_EXCED_RANGE); }
        if (Cancelled(cancelled)) { return fail(ERROR_CANCELLED); }
        const auto type = GetDriveTypeW(parent.substr(0, 3).c_str());
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) { return fail(ERROR_NOT_SUPPORTED); }

        std::vector<blink::core::UniqueHandle> chain;
        std::size_t end = 3;
        for (;;)
        {
            if (Cancelled(cancelled)) { return fail(ERROR_CANCELLED); }
            CREATEFILE3_EXTENDED_PARAMETERS parameters{};
            parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
            blink::core::UniqueHandle directory(CreateFile3(Extended(parent.substr(0, end)).c_str(),
                FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, &parameters));
            if (!directory) { return fail(GetLastError()); }
            FILE_ATTRIBUTE_TAG_INFO attributes{};
            if (!GetFileInformationByHandleEx(directory.get(), FileAttributeTagInfo, &attributes, sizeof(attributes)))
            { return fail(GetLastError()); }
            if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { return fail(ERROR_NOT_SUPPORTED); }
            if ((attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) { return fail(ERROR_DIRECTORY); }
            blink::core::FileIdentity identity;
            if (!blink::core::ReadFileIdentity(directory.get(), identity)) { return fail(GetLastError()); }
            chain.push_back(std::move(directory));
            if (end == parent.size())
            {
                if (identity != request.parent_identity) { return fail(ERROR_FILE_INVALID); }
                break;
            }
            const auto next = parent.find(L'\\', end == 3 ? 3 : end + 1);
            end = next == std::wstring::npos ? parent.size() : next;
        }
        // Every named ancestor is still held without DELETE sharing. The
        // modern create call additionally refuses reparse redirection.
        if (Cancelled(cancelled)) { return fail(ERROR_CANCELLED); }
        std::wstring final_parent(32768, L'\0');
        const auto length = GetFinalPathNameByHandleW(chain.back().get(), final_parent.data(),
            static_cast<DWORD>(final_parent.size()), FILE_NAME_OPENED | VOLUME_NAME_DOS);
        if (!length) { return fail(GetLastError()); }
        if (length >= final_parent.size()) { return fail(ERROR_FILENAME_EXCED_RANGE); }
        final_parent.resize(length);
        const auto resolved = LocalFolder(final_parent);
        if (resolved.empty() || !Equal(std::wstring_view(resolved).substr(0, 3), std::wstring_view(parent).substr(0, 3)))
        { return fail(ERROR_NOT_SUPPORTED); }
        const auto path = blink::core::JoinPath(final_parent, request.name);
        if (path.size() >= 32767) { return fail(ERROR_FILENAME_EXCED_RANGE); }
        if (Cancelled(cancelled)) { return fail(ERROR_CANCELLED); }
        DWORD error{};
        result.committed = create ? create(path, &error) : blink::core::CreateDirectoryModern(path, &error);
        result.error = result.committed ? ERROR_SUCCESS : error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
        return result;
    }
}
