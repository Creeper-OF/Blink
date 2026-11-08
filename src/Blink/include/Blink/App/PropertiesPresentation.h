#pragma once
#include "Blink/App/ItemProperties.h"
#include "Blink/App/FolderViewFormatting.h"
#include <utility>

namespace blink::app
{
    using PropertiesLine = std::pair<std::wstring, std::wstring>;
    [[nodiscard]] inline bool PropertiesTotalsComplete(const ItemPropertiesSnapshot& value) noexcept
    { return value.finished && !value.cancelled && !value.error && value.read_count == value.selected_count; }
    [[nodiscard]] inline std::wstring FormatPropertiesTime(const FILETIME& value, const Localization& localization)
    {
        SYSTEMTIME utc{}, local{}; DYNAMIC_TIME_ZONE_INFORMATION zone{};
        if ((!value.dwLowDateTime && !value.dwHighDateTime) || !FileTimeToSystemTime(&value, &utc) ||
            GetDynamicTimeZoneInformation(&zone) == TIME_ZONE_ID_INVALID || !SystemTimeToTzSpecificLocalTimeEx(&zone, &utc, &local))
        { return localization.GetString(L"Common.NotAvailable"); }
        wchar_t text[40]{};
        const auto written = swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u", local.wYear, local.wMonth, local.wDay,
            local.wHour, local.wMinute, local.wSecond);
        return written > 0 ? std::wstring(text, written) : localization.GetString(L"Common.NotAvailable");
    }
    [[nodiscard]] inline std::wstring PropertiesAttributes(const DWORD attributes, const Localization& localization)
    {
        std::wstring text;
        for (const auto& [flag, key] : std::vector<std::pair<DWORD, const wchar_t*>>{
            { FILE_ATTRIBUTE_READONLY, L"Properties.ReadOnly" }, { FILE_ATTRIBUTE_HIDDEN, L"Properties.Hidden" },
            { FILE_ATTRIBUTE_SYSTEM, L"Properties.System" }, { FILE_ATTRIBUTE_ARCHIVE, L"Properties.Archive" },
            { FILE_ATTRIBUTE_COMPRESSED, L"Properties.Compressed" }, { FILE_ATTRIBUTE_ENCRYPTED, L"Properties.Encrypted" },
            { FILE_ATTRIBUTE_SPARSE_FILE, L"Properties.Sparse" }, { FILE_ATTRIBUTE_REPARSE_POINT, L"Properties.Link" },
            { FILE_ATTRIBUTE_OFFLINE, L"Properties.Offline" } })
        {
            if (attributes & flag) { if (!text.empty()) { text += L", "; } text += localization.GetString(key); }
        }
        return text.empty() ? localization.GetString(L"Properties.None") : text;
    }
    [[nodiscard]] inline std::wstring PropertiesStatus(const ItemPropertiesSnapshot& value, const Localization& localization)
    {
        auto text = localization.GetString(!value.finished ? L"Properties.Reading" : value.cancelled ? L"Properties.Cancelled" :
            value.error ? L"Properties.Partial" : L"Properties.Complete");
        if (value.unavailable_count) { text += L" " + localization.Format(L"Properties.UnavailableCount", std::to_wstring(value.unavailable_count)); }
        if (value.error && !value.cancelled) { text += L" " + blink::core::Win32ErrorMessage(value.error); }
        if (!value.failure_path.empty()) { text += L"\n" + value.failure_path; }
        return text;
    }
    [[nodiscard]] inline std::vector<PropertiesLine> BuildPropertiesLines(const ItemPropertiesRequest& request,
        const ItemPropertiesSnapshot& value, const Localization& localization)
    {
        std::vector<PropertiesLine> lines;
        const auto add = [&](const wchar_t* key, std::wstring text) { lines.emplace_back(localization.GetString(key), std::move(text)); };
        std::wstring names;
        for (std::size_t index = 0; index < request.entries.size() && index < 8; ++index)
        { if (index) { names += L"\n"; } names += request.entries[index].name; }
        if (request.entries.size() > 8) { names += L"\n…"; }
        add(L"Properties.Name", std::move(names)); add(L"Properties.Location", request.parent);
        add(L"Properties.Selection", localization.Format(L"Properties.SelectionValue", std::vector<std::wstring>{
            std::to_wstring(value.selected_count), std::to_wstring(value.selected_files), std::to_wstring(value.selected_folders) }));
        const auto unavailable = localization.GetString(L"Properties.Unavailable");
        const bool readable = !value.items.empty() && value.read_count == value.selected_count;
        std::wstring type = unavailable;
        if (readable)
        {
            type = FileTypeLabel(value.items.front().entry);
            for (const auto& item : value.items) { if (FileTypeLabel(item.entry) != type) { type = localization.GetString(L"Properties.MultipleTypes"); break; } }
        }
        add(L"Properties.Type", std::move(type));
        const auto size = [&](const std::uint64_t bytes) {
            if (!value.read_count) { return unavailable; }
            auto text = localization.Format(L"Properties.Bytes", std::vector<std::wstring>{ FormatFileSize(bytes), std::to_wstring(bytes) });
            if (!PropertiesTotalsComplete(value)) { text = localization.Format(L"Properties.Counted", text); }
            return text;
        };
        add(L"Properties.Size", size(value.bytes)); add(L"Properties.Allocated", size(value.allocated_bytes));
        add(L"Properties.Contains", localization.Format(L"Properties.ContainsValue", std::vector<std::wstring>{ std::to_wstring(value.files), std::to_wstring(value.folders) }));
        for (const auto& [key, member] : std::vector<std::pair<const wchar_t*, FILETIME blink::core::FileEntry::*>>{
            { L"Properties.Created", &blink::core::FileEntry::creation_time },
            { L"Properties.Modified", &blink::core::FileEntry::last_write_time },
            { L"Properties.Accessed", &blink::core::FileEntry::last_access_time } })
        {
            auto text = unavailable;
            if (readable)
            {
                const auto time = value.items.front().entry.*member; bool equal = true;
                for (const auto& item : value.items) { const auto other = item.entry.*member; equal &= CompareFileTime(&time, &other) == 0; }
                text = equal ? FormatPropertiesTime(time, localization) : localization.GetString(L"Properties.MultipleValues");
            }
            add(key, std::move(text));
        }
        DWORD common = ~DWORD{};
        if (readable) { for (const auto& item : value.items) { common &= item.entry.attributes; } }
        add(request.entries.size() == 1 ? L"Properties.Attributes" : L"Properties.CommonAttributes",
            readable ? PropertiesAttributes(common, localization) : unavailable);
        return lines;
    }
}
