#pragma once

#include "Blink/App/BrowseState.h"
#include "Blink/App/Localization.h"
#include "Blink/App/TabState.h"
#include "Blink/Core/FileTypes.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace blink::app
{
    struct FolderRowViewData
    {
        blink::core::FileIdentity identity;
        RowIdentity row_identity;
        std::wstring full_path;
        std::wstring name;
        std::wstring type;
        std::wstring size;
        std::wstring modified;
        std::wstring icon;
        bool is_directory{};
    };

    // App-private projection context. One context is reused across
    // progressive batches so localized labels remain cached for the whole
    // presentation instead of reinitializing resource lookup per batch.
    class FolderRowProjectionBuilder
    {
    public:
        [[nodiscard]] std::vector<FolderRowViewData> Build(
            const std::vector<blink::core::FileEntry>& entries,
            std::size_t begin,
            std::size_t end);

    private:
        Localization localization_;
        std::unordered_map<std::wstring, std::wstring> strings_;
    };

    [[nodiscard]] std::wstring FormatFileSize(std::uint64_t size);
    [[nodiscard]] std::wstring FormatModifiedTime(const FILETIME& file_time);
    [[nodiscard]] std::wstring FileTypeLabel(const blink::core::FileEntry& entry);
    [[nodiscard]] std::vector<FolderRowViewData> BuildFolderRowProjection(
        const std::vector<blink::core::FileEntry>& entries);
    [[nodiscard]] std::vector<FolderRowViewData> BuildFolderRowProjection(
        const std::vector<blink::core::FileEntry>& entries,
        std::size_t begin,
        std::size_t end);
    [[nodiscard]] std::wstring FormatBrowseActivity(
        const BrowseSnapshot& browse,
        const TabPresentationSnapshot& tab);
}
