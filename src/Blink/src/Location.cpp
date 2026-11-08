#include "Blink/App/Location.h"

#include "Blink/Core/FileTypes.h"

#include <Windows.h>
#include <pathcch.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <string_view>

#pragma comment(lib, "pathcch")
#pragma comment(lib, "Shell32.lib")

namespace blink::app
{
    namespace
    {
        std::wstring RelocationPath(std::wstring value)
        {
            std::replace(value.begin(), value.end(), L'/', L'\\');
            if (value.size() >= 8 && CompareStringOrdinal(value.data(), 8, L"\\\\?\\UNC\\", 8, TRUE) == CSTR_EQUAL)
            { value = L"\\\\" + value.substr(8); }
            else if (value.size() >= 7 && value.starts_with(L"\\\\?\\") && value[5] == L':' && value[6] == L'\\')
            { value.erase(0, 4); }
            while (value.size() > 3 && value.back() == L'\\') { value.pop_back(); }
            return value;
        }
        bool ContainsPath(const std::wstring& current, const std::wstring& root)
        {
            return !root.empty() && root.size() <= 32767 && current.size() >= root.size() &&
                CompareStringOrdinal(current.data(), static_cast<int>(root.size()), root.data(), static_cast<int>(root.size()), TRUE) == CSTR_EQUAL &&
                (current.size() == root.size() || root.back() == L'\\' || current[root.size()] == L'\\');
        }
        [[nodiscard]] std::wstring KnownFolderPath(const KNOWNFOLDERID& folder_id)
        {
            PWSTR raw_path = nullptr;
            if (FAILED(SHGetKnownFolderPath(
                    folder_id,
                    KF_FLAG_DEFAULT,
                    nullptr,
                    &raw_path)) || raw_path == nullptr)
            {
                return {};
            }

            std::wstring path(raw_path);
            CoTaskMemFree(raw_path);
            return path;
        }

        [[nodiscard]] std::vector<std::wstring> LogicalDriveRoots()
        {
            // A logical-drive root is at most four UTF-16 code units (for
            // example, "C:\\"). Even with every drive letter present, 256
            // code units leaves ample room for the required double terminator.
            std::array<wchar_t, 256> buffer{};
            const DWORD length = GetLogicalDriveStringsW(
                static_cast<DWORD>(buffer.size()),
                buffer.data());
            if (length == 0 || length >= buffer.size())
            {
                return {};
            }

            std::vector<std::wstring> roots;
            for (const wchar_t* current = buffer.data(); *current != L'\0';
                current += std::wcslen(current) + 1)
            {
                const UINT drive_type = GetDriveTypeW(current);
                if (drive_type != DRIVE_FIXED && drive_type != DRIVE_REMOVABLE)
                {
                    continue;
                }

                const DWORD attributes = GetFileAttributesW(current);
                if (attributes == INVALID_FILE_ATTRIBUTES ||
                    (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                    continue;
                }

                roots.emplace_back(current);
            }
            return roots;
        }
    }

    ThisPcContent EnumerateThisPcContent()
    {
        ThisPcContent content;
        const auto add_place = [&content](
            const std::wstring_view label_key,
            const KNOWNFOLDERID& folder_id) {
            const auto path = KnownFolderPath(folder_id);
            if (path.empty())
            {
                return;
            }

            const auto target = Location::Folder(path);
            if (target.IsValid())
            {
                content.items.push_back(ThisPcItem{
                    ThisPcItemKind::Place,
                    std::wstring(label_key),
                    {},
                    target });
            }
        };

        add_place(L"Nav.Home", FOLDERID_Profile);
        add_place(L"Nav.Desktop", FOLDERID_Desktop);
        add_place(L"Nav.Downloads", FOLDERID_Downloads);
        add_place(L"Nav.Documents", FOLDERID_Documents);

        for (const auto& root : LogicalDriveRoots())
        {
            const auto target = Location::Folder(root);
            if (target.IsValid())
            {
                content.items.push_back(ThisPcItem{
                    ThisPcItemKind::Volume,
                    {},
                    target.FolderPath(),
                    target });
            }
        }
        return content;
    }

    bool IsFolderAtOrBelow(const Location& location, const Location& root)
    {
        return location.IsFolder() && root.IsFolder() && location.IsValid() && root.IsValid() &&
            ContainsPath(RelocationPath(location.FolderPath()), RelocationPath(root.FolderPath()));
    }

    std::optional<Location> RebaseFolderLocation(const Location& location, const Location& source, const Location& destination)
    {
        if (!location.IsFolder() || !source.IsFolder() || !destination.IsFolder() ||
            !location.IsValid() || !source.IsValid() || !destination.IsValid()) { return {}; }
        const auto current = RelocationPath(location.FolderPath()), from = RelocationPath(source.FolderPath()), to = RelocationPath(destination.FolderPath());
        if (!ContainsPath(current, from)) { return {}; }
        auto suffix = current.substr(from.size());
        if (!suffix.empty() && suffix.front() == L'\\') { suffix.erase(0, 1); }
        auto rebased = suffix.empty() ? to : blink::core::JoinPath(to, suffix);
        if (rebased.size() > 32760 || rebased == location.FolderPath()) { return {}; }
        return Location::Folder(std::move(rebased));
    }

    Location Location::ThisPC() noexcept
    {
        return Location{};
    }

    Location Location::Folder(std::wstring path)
    {
        if (path.empty() || path.find(L'\0') != std::wstring::npos)
        {
            return Location(LocationKind::Folder, {});
        }

        return Location(LocationKind::Folder, blink::core::NormalizePath(path));
    }

    bool Location::IsValid() const noexcept
    {
        return IsThisPC() || !folder_path_.empty();
    }

    std::optional<Location> Location::Up() const
    {
        if (IsThisPC() || !IsValid())
        {
            return std::nullopt;
        }

        std::wstring parent = folder_path_;
        constexpr std::size_t kPathBufferSize = 32768;
        parent.resize(kPathBufferSize, L'\0');

        PCWSTR root_end = nullptr;
        if (FAILED(PathCchSkipRoot(parent.c_str(), &root_end)) || root_end == nullptr)
        {
            return std::nullopt;
        }

        // A volume root has no folder component above its root.
        if (*root_end == L'\0')
        {
            return ThisPC();
        }

        if (FAILED(PathCchRemoveFileSpec(parent.data(), parent.size())))
        {
            return std::nullopt;
        }

        parent.resize(std::wcslen(parent.c_str()));
        if (parent.empty())
        {
            return ThisPC();
        }
        return Folder(std::move(parent));
    }

    bool operator==(const Location& left, const Location& right) noexcept
    {
        if (left.kind_ != right.kind_)
        {
            return false;
        }
        if (left.IsThisPC())
        {
            return true;
        }
        return CompareStringOrdinal(
            left.folder_path_.c_str(),
            -1,
            right.folder_path_.c_str(),
            -1,
            TRUE) == CSTR_EQUAL;
    }
}
