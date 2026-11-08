#include "Blink/App/FolderViewFormatting.h"
#include "Blink/App/Localization.h"

#include <cwctype>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

namespace blink::app
{
    namespace
    {
        [[nodiscard]] std::wstring TrimTrailingZeroes(std::wstring value)
        {
            const std::size_t decimal = value.find(L'.');
            if (decimal == std::wstring::npos)
            {
                return value;
            }
            while (!value.empty() && value.back() == L'0')
            {
                value.pop_back();
            }
            if (!value.empty() && value.back() == L'.')
            {
                value.pop_back();
            }
            return value;
        }

        [[nodiscard]] std::wstring Lowercase(std::wstring value)
        {
            for (auto& character : value)
            {
                character = static_cast<wchar_t>(std::towlower(character));
            }
            return value;
        }

        class ProjectionLocalization
        {
        public:
            ProjectionLocalization(
                const Localization& localization,
                std::unordered_map<std::wstring, std::wstring>& strings)
                : localization_(localization),
                  strings_(strings)
            {
            }

            [[nodiscard]] std::wstring GetString(const std::wstring_view key) const
            {
                const std::wstring cache_key(key);
                const auto cached = strings_.find(cache_key);
                if (cached != strings_.end())
                {
                    return cached->second;
                }

                auto value = localization_.GetString(key);
                const auto [inserted, added] = strings_.emplace(
                    cache_key,
                    std::move(value));
                (void)added;
                return inserted->second;
            }

            [[nodiscard]] std::wstring Format(
                const std::wstring_view key,
                const std::wstring_view value) const
            {
                auto result = GetString(key);
                constexpr std::wstring_view placeholder = L"{0}";
                const auto position = result.find(placeholder);
                if (position != std::wstring::npos)
                {
                    result.replace(position, placeholder.size(), value);
                }
                return result;
            }

        private:
            const Localization& localization_;
            std::unordered_map<std::wstring, std::wstring>& strings_;
        };

        template<typename LocalizationLike>
        [[nodiscard]] std::wstring FormatFileSizeWith(
            const std::uint64_t size,
            const LocalizationLike& localization)
        {
            const std::wstring units[] = {
                localization.GetString(L"FileSize.Bytes"),
                localization.GetString(L"FileSize.Kilobytes"),
                localization.GetString(L"FileSize.Megabytes"),
                localization.GetString(L"FileSize.Gigabytes"),
                localization.GetString(L"FileSize.Terabytes") };
            std::size_t unit = 0;
            double value = static_cast<double>(size);
            while (value >= 1024.0 && unit + 1 < std::size(units))
            {
                value /= 1024.0;
                ++unit;
            }

            if (unit == 0)
            {
                return std::to_wstring(size) + L" " + units[unit];
            }

            std::wostringstream output;
            output << std::fixed << std::setprecision(value < 10.0 ? 1 : 0) << value;
            return TrimTrailingZeroes(output.str()) + L" " + units[unit];
        }

        template<typename LocalizationLike>
        [[nodiscard]] std::wstring FormatModifiedTimeWith(
            const FILETIME& file_time,
            const LocalizationLike& localization)
        {
            FILETIME local_time{};
            SYSTEMTIME system_time{};
            if (!FileTimeToLocalFileTime(&file_time, &local_time) ||
                !FileTimeToSystemTime(&local_time, &system_time))
            {
                return localization.GetString(L"Common.NotAvailable");
            }

            wchar_t buffer[32]{};
            const int written = swprintf_s(
                buffer,
                L"%04u-%02u-%02u %02u:%02u",
                system_time.wYear,
                system_time.wMonth,
                system_time.wDay,
                system_time.wHour,
                system_time.wMinute);
            return written > 0 ? std::wstring(buffer, static_cast<std::size_t>(written)) :
                localization.GetString(L"Common.NotAvailable");
        }
    }

    std::wstring FormatFileSize(const std::uint64_t size)
    {
        const Localization localization;
        return FormatFileSizeWith(size, localization);
    }

    std::wstring FormatModifiedTime(const FILETIME& file_time)
    {
        const Localization localization;
        return FormatModifiedTimeWith(file_time, localization);
    }

    namespace
    {
        template<typename LocalizationLike>
        [[nodiscard]] std::wstring FileTypeLabelWith(
            const blink::core::FileEntry& entry,
            const LocalizationLike& localization)
        {
            if (entry.is_directory)
            {
                return localization.GetString(L"FileType.Folder");
            }

            const std::wstring extension = Lowercase(entry.extension);
            if (extension == L".txt" || extension == L".log")
            {
                return localization.GetString(L"FileType.TextDocument");
            }
            if (extension == L".cpp" || extension == L".h" || extension == L".hpp")
            {
                return localization.GetString(L"FileType.CppSource");
            }
            if (extension == L".jpg" || extension == L".jpeg" || extension == L".png" ||
                extension == L".gif" || extension == L".bmp")
            {
                return localization.GetString(L"FileType.Image");
            }
            if (extension == L".zip" || extension == L".7z" || extension == L".rar")
            {
                return localization.GetString(L"FileType.CompressedArchive");
            }
            if (extension == L".exe" || extension == L".msi")
            {
                return localization.GetString(L"FileType.Application");
            }
            if (extension.empty())
            {
                return localization.GetString(L"FileType.File");
            }
            return localization.Format(L"FileType.FileSuffix", extension.substr(1));
        }
    }

    std::wstring FileTypeLabel(const blink::core::FileEntry& entry)
    {
        const Localization localization;
        return FileTypeLabelWith(entry, localization);
    }

    std::vector<FolderRowViewData> BuildFolderRowProjection(
        const std::vector<blink::core::FileEntry>& entries)
    {
        return BuildFolderRowProjection(entries, 0, entries.size());
    }

    namespace
    {
        template<typename LocalizationLike>
        [[nodiscard]] std::vector<FolderRowViewData> BuildFolderRowsWith(
            const std::vector<blink::core::FileEntry>& entries,
            const std::size_t begin,
            const std::size_t end,
            const LocalizationLike& localization)
        {
            const std::size_t first = std::min(begin, entries.size());
            const std::size_t last = std::min(std::max(begin, end), entries.size());
            std::vector<FolderRowViewData> rows;
            rows.reserve(last - first);
            for (std::size_t index = first; index < last; ++index)
            {
                const auto& entry = entries[index];
                FolderRowViewData row;
                row.identity = entry.identity;
                row.row_identity = MakeRowIdentity(entry);
                row.full_path = entry.full_path;
                row.name = entry.name;
                row.type = FileTypeLabelWith(entry, localization);
                row.size = entry.is_directory ? L"" : FormatFileSizeWith(entry.size, localization);
                row.modified = FormatModifiedTimeWith(entry.last_write_time, localization);
                row.icon = entry.is_directory ? L"\xE8B7" : L"\xE7C3";
                row.is_directory = entry.is_directory;
                rows.push_back(std::move(row));
            }
            return rows;
        }
    }

    std::vector<FolderRowViewData> BuildFolderRowProjection(
        const std::vector<blink::core::FileEntry>& entries,
        const std::size_t begin,
        const std::size_t end)
    {
        const Localization base_localization;
        std::unordered_map<std::wstring, std::wstring> strings;
        const ProjectionLocalization localization(base_localization, strings);
        return BuildFolderRowsWith(entries, begin, end, localization);
    }

    std::vector<FolderRowViewData> FolderRowProjectionBuilder::Build(
        const std::vector<blink::core::FileEntry>& entries,
        const std::size_t begin,
        const std::size_t end)
    {
        const ProjectionLocalization localization(localization_, strings_);
        return BuildFolderRowsWith(entries, begin, end, localization);
    }

    std::wstring FormatBrowseActivity(
        const BrowseSnapshot& browse,
        const TabPresentationSnapshot& tab)
    {
        const Localization localization;
        if (tab.has_pending && tab.pending_location.has_value() &&
            tab.pending_location->IsThisPC())
        {
            return localization.GetString(L"Status.LoadingThisPC");
        }
        if (browse.state == BrowseStateKind::Loading && tab.has_pending)
        {
            return localization.Format(L"Status.Loading", browse.requested_path);
        }
        if (browse.state == BrowseStateKind::Failed)
        {
            const std::wstring_view key = browse.error == ERROR_FILE_NOT_FOUND ||
                    browse.error == ERROR_PATH_NOT_FOUND ?
                L"Browse.PathNotFound" :
                browse.error == ERROR_ACCESS_DENIED ?
                L"Browse.AccessDenied" : L"Browse.Failed";
            return localization.Format(key, browse.error);
        }
        if (tab.committed_location.IsThisPC())
        {
            if (tab.latest_browse_error != ERROR_SUCCESS)
            {
                return localization.Format(
                    L"Status.ThisPCFailed",
                    tab.latest_browse_error);
            }
            return localization.Format(L"Status.ThisPCReadyCount", tab.this_pc_item_count);
        }
        if (tab.folder_state == FolderState::Empty)
        {
            return localization.GetString(L"Status.Empty");
        }
        if (tab.committed_current_path.empty())
        {
            return localization.GetString(L"Status.ChooseFolder");
        }
        return localization.Format(L"Status.ReadyCount", tab.committed_item_count);
    }
}
