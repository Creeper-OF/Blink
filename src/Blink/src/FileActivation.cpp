#include "Blink/App/FileActivation.h"

namespace blink::app
{
    namespace
    {
        std::wstring QuoteArgument(const std::wstring& value)
        {
            std::wstring result = L"\"";
            std::size_t slashes{};
            for (const wchar_t character : value)
            {
                if (character == L'\\') { ++slashes; continue; }
                result.append(character == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
                slashes = 0;
                result.push_back(character);
            }
            result.append(slashes * 2, L'\\');
            result.push_back(L'\"');
            return result;
        }
    }

    std::wstring BuildFileActivationCommandLine(
        const std::wstring& host, const blink::core::FileEntry& entry)
    {
        return QuoteArgument(host) + L" " + QuoteArgument(entry.full_path) +
            L" " + QuoteArgument(entry.identity.ToString());
    }

    HRESULT FileActivationHostResult(const DWORD exit_code) noexcept
    {
        if (exit_code == 0) { return S_OK; }
        if ((exit_code & 0xC0000000u) == 0xC0000000u)
        {
            return HRESULT_FROM_NT(exit_code);
        }
        const auto error = static_cast<HRESULT>(exit_code);
        // CRT abort and other unexpected positive exit codes are not success.
        return FAILED(error) ? error : E_FAIL;
    }

    DWORD ValidateFileActivation(const blink::core::FileEntry& entry)
    {
        if (entry.is_directory) { return ERROR_DIRECTORY; }
        if (entry.full_path.empty() || !entry.identity.valid) { return ERROR_INVALID_DATA; }
        auto handle = blink::core::OpenPathForMetadata(entry.full_path, false);
        if (!handle) { return GetLastError(); }
        blink::core::FileIdentity actual;
        if (!blink::core::ReadFileIdentity(handle.get(), actual)) { return GetLastError(); }
        if (actual != entry.identity) { return ERROR_FILE_INVALID; }
        FILE_STANDARD_INFO standard{};
        if (!GetFileInformationByHandleEx(
                handle.get(), FileStandardInfo, &standard, sizeof(standard)))
        {
            return GetLastError();
        }
        return standard.Directory ? ERROR_DIRECTORY : ERROR_SUCCESS;
    }
}
