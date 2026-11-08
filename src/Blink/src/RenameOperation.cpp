#include "Blink/App/RenameOperation.h"
#include "Blink/App/DirectoryLease.h"
#include <algorithm>
#include <cstddef>

namespace blink::app
{
    namespace
    {
        bool RenameHandle(HANDLE source, const std::wstring& destination, DWORD* error)
        {
            const auto bytes = sizeof(FILE_RENAME_INFO) + destination.size() * sizeof(wchar_t);
            std::vector<std::byte> buffer(bytes);
            auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
            info->Flags = 0;
            info->RootDirectory = nullptr;
            info->FileNameLength = static_cast<DWORD>(destination.size() * sizeof(wchar_t));
            std::copy(destination.begin(), destination.end(), info->FileName);
            const bool renamed = SetFileInformationByHandle(source, FileRenameInfoEx, info, static_cast<DWORD>(bytes)) != FALSE;
            *error = renamed ? ERROR_SUCCESS : GetLastError();
            return renamed;
        }
    }
    RenameResult RenameEntrySafely(const RenameRequest& request, const FileOperationCancellation& cancelled,
        const RenameByHandleFunction& rename)
    {
        RenameResult result;
        const auto fail = [&result](DWORD error) { result.error = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error; return result; };
        if (const auto error = ValidateNewFolderName(request.new_name); error != ERROR_SUCCESS) { return fail(error); }
        if (const auto error = ValidateNewFolderName(request.source.name); error != ERROR_SUCCESS) { return fail(error); }
        if ((request.source.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { return fail(ERROR_NOT_SUPPORTED); }
        if (!request.source.identity.valid || request.source.identity.volume_serial != request.parent_identity.volume_serial)
        { return fail(ERROR_FILE_INVALID); }
        auto parent = detail::AcquireDirectoryLease(request.parent, request.parent_identity, cancelled);
        if (parent.error != ERROR_SUCCESS) { return fail(parent.error); }
        result.path = blink::core::JoinPath(parent.path, request.new_name);
        const auto destination = blink::core::JoinPath(parent.native_path, request.new_name);
        const auto source_path = blink::core::JoinPath(parent.native_path, request.source.name);
        if (destination.size() >= 32767 || source_path.size() >= 32767) { return fail(ERROR_FILENAME_EXCED_RANGE); }
        CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
        parameters.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
        DWORD access = DELETE | FILE_READ_ATTRIBUTES;
        if (request.source.is_directory) { access |= FILE_LIST_DIRECTORY; }
        blink::core::UniqueHandle source(CreateFile3(source_path.c_str(), access, FILE_SHARE_READ, OPEN_EXISTING, &parameters));
        if (!source) { return fail(GetLastError()); }
        FILE_ATTRIBUTE_TAG_INFO attributes{};
        if (!GetFileInformationByHandleEx(source.get(), FileAttributeTagInfo, &attributes, sizeof(attributes))) { return fail(GetLastError()); }
        if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) { return fail(ERROR_NOT_SUPPORTED); }
        blink::core::FileIdentity identity;
        if (!blink::core::ReadFileIdentity(source.get(), identity)) { return fail(GetLastError()); }
        if (identity != request.source.identity || request.source.is_directory != ((attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
        { return fail(ERROR_FILE_INVALID); }
        if (cancelled && cancelled->load()) { return fail(ERROR_CANCELLED); }
        if (request.new_name == request.source.name) { result.error = ERROR_SUCCESS; return result; }
        DWORD error{};
        result.renamed = rename ? rename(source.get(), destination, &error) : RenameHandle(source.get(), destination, &error);
        result.error = result.renamed ? ERROR_SUCCESS : error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
        return result;
    }
}
