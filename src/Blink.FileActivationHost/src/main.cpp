#include "Blink/App/FileActivation.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <filesystem>
#include <new>

#pragma comment(lib, "shell32")
#pragma comment(lib, "ole32")
#pragma comment(lib, "shlwapi")

namespace
{
    HRESULT DispatchFile(int argc, wchar_t** argv)
    {
        if (argc != 3) { return E_INVALIDARG; }
        blink::core::FileEntry entry;
        // ReadEntryMetadata updates metadata; callers retain ownership of paths.
        entry.full_path = blink::core::NormalizePath(argv[1]);
        if (!blink::core::ReadEntryMetadata(entry.full_path, entry))
        {
            const DWORD error = GetLastError();
            return HRESULT_FROM_WIN32(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
        }
        if (entry.identity.ToString() != argv[2])
        {
            return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
        }
        const DWORD error = blink::app::ValidateFileActivation(entry);
        if (error != ERROR_SUCCESS) { return HRESULT_FROM_WIN32(error); }

        // Shell verb handlers can execute third-party COM code. STA and Shell
        // execution belong to this ordinary-user isolated process, never Blink.exe.
        const HRESULT initialized = CoInitializeEx(
            nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(initialized)) { return initialized; }
        struct ComScope { ~ComScope() { CoUninitialize(); } } com;

        const auto extension = std::filesystem::path(entry.full_path).extension().wstring();
        if (extension.empty()) { return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION); }
        DWORD association_characters{};
        const HRESULT association = AssocQueryStringW(
            ASSOCF_INIT_IGNOREUNKNOWN | ASSOCF_NOFIXUPS, ASSOCSTR_PROGID,
            extension.c_str(), L"open", nullptr, &association_characters);
        if (FAILED(association)) { return association; }
        if (association_characters <= 1) { return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION); }
        // Query the current user's ProgID, including packaged-app associations.
        // IGNOREUNKNOWN prevents the generic chooser from becoming an open request.
        const auto directory = std::filesystem::path(entry.full_path).parent_path().wstring();
        SHELLEXECUTEINFOW request{};
        request.cbSize = sizeof(request);
        request.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        request.lpVerb = L"open";
        request.lpFile = entry.full_path.c_str();
        request.lpDirectory = directory.c_str();
        request.nShow = SW_SHOWNORMAL;
        // NOASYNC keeps this short-lived helper alive until dispatch completes.
        // Windows security prompts remain enabled; target-app readiness is not awaited.
        if (!ShellExecuteExW(&request))
        {
            const DWORD last_error = GetLastError();
            return HRESULT_FROM_WIN32(last_error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : last_error);
        }
        return S_OK;
    }
}

int wmain(int argc, wchar_t** argv)
{
    try { return static_cast<int>(DispatchFile(argc, argv)); }
    catch (const std::bad_alloc&) { return static_cast<int>(E_OUTOFMEMORY); }
    catch (...) { return static_cast<int>(E_FAIL); }
}
