#include "Blink/App/ShellContextMenu.h"

#include <charconv>
#include <filesystem>
#include <pathcch.h>
#include <set>

#pragma comment(lib, "pathcch")

namespace blink::app
{
    namespace
    {
        std::wstring Quote(const std::wstring& value)
        {
            std::wstring result{ L"\"" };
            std::size_t slashes{};
            for (const auto ch : value)
            {
                if (ch == L'\\') { ++slashes; continue; }
                result.append(slashes * (ch == L'\"' ? 2 : 1), L'\\');
                slashes = 0;
                if (ch == L'\"') { result.push_back(L'\\'); }
                result.push_back(ch);
            }
            result.append(slashes * 2, L'\\');
            result.push_back(L'\"');
            return result;
        }
        struct PathLess
        {
            bool operator()(const std::wstring& left, const std::wstring& right) const noexcept
            { return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN; }
        };
        bool SamePath(const std::wstring& left, const std::wstring& right) noexcept
        { return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL; }
        std::wstring Extended(const std::wstring& path)
        { return path.starts_with(L"\\\\?\\") || path.starts_with(L"\\\\") ? path : L"\\\\?\\" + path; }
    }

    std::wstring BuildShellMenuCommandLine(const std::wstring& host,
        const ShellMenuRequest& request, const std::uintptr_t cancel_handle)
    {
        auto result = Quote(host) + L" --path " + Quote(request.entry.full_path) +
            L" --identity " + Quote(request.entry.identity.ToString()) +
            L" --parent-identity " + Quote(request.parent_identity.ToString()) +
            L" --x " + std::to_wstring(request.position.x) +
            L" --y " + std::to_wstring(request.position.y) +
            L" --owner " + std::to_wstring(reinterpret_cast<std::uintptr_t>(request.owner)) +
            L" --locale " + Quote(request.locale);
        if (request.extended) { result += L" --extended"; }
        if (!request.verb.empty()) { result += L" --verb " + Quote(request.verb); }
        for (const auto& entry : request.additional_entries)
        {
            result += L" --additional-path " + Quote(entry.full_path) +
                L" --additional-identity " + Quote(entry.identity.ToString());
        }
        if (cancel_handle) { result += L" --cancel-handle " + std::to_wstring(cancel_handle); }
        return result;
    }

    DWORD CaptureDirectoryShellMenuRequest(const std::wstring& path,
        ShellMenuRequest& request)
    {
        request = {};
        if (path.empty() || path.find(L'\0') != path.npos || !std::filesystem::path(path).is_absolute())
        { return ERROR_INVALID_PARAMETER; }
        auto full_path = blink::core::NormalizePath(path);
        // Shell parsing uses the ordinary absolute spelling. Metadata reads
        // retain extended-length support through Extended() below.
        if (full_path.size() >= 8 && CompareStringOrdinal(full_path.data(), 8, L"\\\\?\\UNC\\", 8, TRUE) == CSTR_EQUAL)
        { full_path = L"\\\\" + full_path.substr(8); }
        else if (full_path.size() >= 7 && full_path.starts_with(L"\\\\?\\") && full_path[5] == L':' && full_path[6] == L'\\')
        { full_path.erase(0, 4); }
        PCWSTR root_end{};
        if (FAILED(PathCchSkipRoot(full_path.c_str(), &root_end)) || !root_end)
        { return ERROR_INVALID_PARAMETER; }
        const auto root_length = static_cast<std::size_t>(root_end - full_path.c_str());
        while (full_path.size() > root_length && full_path.back() == L'\\') { full_path.pop_back(); }
        if (full_path.size() == root_length && full_path.back() != L'\\') { full_path += L'\\'; }
        const auto parent_path = std::filesystem::path(full_path).parent_path().wstring();
        if (parent_path.empty()) { return ERROR_INVALID_PARAMETER; }
        blink::core::FileEntry entry, parent;
        if (!blink::core::ReadEntryMetadata(Extended(full_path), entry))
        { const auto error = GetLastError(); return error ? error : ERROR_GEN_FAILURE; }
        if (!entry.is_directory) { return ERROR_DIRECTORY; }
        if (!blink::core::ReadEntryMetadata(Extended(parent_path), parent))
        { const auto error = GetLastError(); return error ? error : ERROR_GEN_FAILURE; }
        if (!parent.is_directory) { return ERROR_DIRECTORY; }
        entry.full_path = std::move(full_path);
        entry.name = std::filesystem::path(entry.full_path).filename().wstring();
        if (entry.name.empty()) { entry.name = entry.full_path; }
        request.entry = std::move(entry);
        request.parent_identity = parent.identity;
        return ERROR_SUCCESS;
    }

    DWORD ValidateShellMenuRequest(const ShellMenuRequest& request)
    {
        if (!request.parent_identity.valid || !request.entry.identity.valid || request.entry.full_path.empty())
        { return ERROR_INVALID_PARAMETER; }
        const auto path = blink::core::NormalizePath(request.entry.full_path);
        const auto directory = std::filesystem::path(path).parent_path().wstring();
        if (directory.empty()) { return ERROR_INVALID_PARAMETER; }
        std::set<std::wstring, PathLess> paths;
        const auto validate = [&](const blink::core::FileEntry& captured) -> DWORD {
            if (captured.full_path.empty() || !captured.identity.valid) { return ERROR_INVALID_PARAMETER; }
            const auto full_path = blink::core::NormalizePath(captured.full_path);
            if (!SamePath(std::filesystem::path(full_path).parent_path().wstring(), directory) ||
                !paths.insert(full_path).second) { return ERROR_INVALID_PARAMETER; }
            blink::core::FileEntry current;
            if (!blink::core::ReadEntryMetadata(Extended(full_path), current))
            { const auto error = GetLastError(); return error ? error : ERROR_GEN_FAILURE; }
            return current.identity == captured.identity ? ERROR_SUCCESS : ERROR_FILE_INVALID;
        };
        if (const auto error = validate(request.entry)) { return error; }
        for (const auto& entry : request.additional_entries)
        { if (const auto error = validate(entry)) { return error; } }
        blink::core::FileEntry parent;
        if (!blink::core::ReadEntryMetadata(Extended(directory), parent))
        { const auto error = GetLastError(); return error ? error : ERROR_GEN_FAILURE; }
        return parent.is_directory && parent.identity == request.parent_identity ? ERROR_SUCCESS : ERROR_FILE_INVALID;
    }

    bool ParseShellMenuReply(std::string_view line, ShellMenuResult& result) noexcept
    {
        result = {};
        if (!line.empty() && line.back() == '\n')
        {
            line.remove_suffix(1);
            if (!line.empty() && line.back() == '\r') { line.remove_suffix(1); }
        }
        constexpr std::string_view prefix{ "BLINK_SHELL_MENU_V1 " };
        if (!line.starts_with(prefix)) { return false; }
        line.remove_prefix(prefix.size());
        const auto kind_end = line.find(' ');
        if (kind_end == line.npos) { return false; }
        const auto kind = line.substr(0, kind_end);
        line.remove_prefix(kind_end + 1);
        const auto id_end = line.find(' ');
        if (id_end == line.npos) { return false; }
        const auto id_text = line.substr(0, id_end);
        line.remove_prefix(id_end + 1);
        if (id_text.empty() || line.size() != 8) { return false; }
        std::uint32_t id{}, error{};
        const auto id_parse = std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
        const auto error_parse = std::from_chars(line.data(), line.data() + line.size(), error, 16);
        if (id_parse.ec != std::errc{} || id_parse.ptr != id_text.data() + id_text.size() ||
            error_parse.ec != std::errc{} || error_parse.ptr != line.data() + line.size()) { return false; }
        const auto hr = static_cast<HRESULT>(error);
        if (kind == "blink")
        {
            if (id < 1 || id > 10 || hr != S_OK) { return false; }
            result = { ShellMenuResultKind::BlinkCommand, static_cast<ShellMenuCommand>(id), S_OK };
        }
        else if (kind == "native" || kind == "cancel")
        {
            if (id || hr != S_OK) { return false; }
            result = { kind == "native" ? ShellMenuResultKind::NativeCommand : ShellMenuResultKind::Cancelled,
                ShellMenuCommand::None, S_OK };
        }
        else if (kind == "error")
        {
            if (id || !FAILED(hr)) { return false; }
            result = { ShellMenuResultKind::Error, ShellMenuCommand::None, hr };
        }
        else { return false; }
        return true;
    }
}
