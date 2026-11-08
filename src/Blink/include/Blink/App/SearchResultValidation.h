#pragma once
#include "Blink/App/SearchState.h"
#include <functional>

namespace blink::app
{
    using SearchMetadataReader = std::function<DWORD(const std::wstring&, blink::core::FileEntry&)>;
    struct SearchFolderScope
    {
        std::wstring path;
        blink::core::FileIdentity identity;
        DWORD error{ ERROR_SUCCESS };
    };
    struct SearchActionTarget
    {
        blink::core::FileEntry entry;
        std::wstring parent;
        blink::core::FileIdentity parent_identity;
        DWORD error{ ERROR_SUCCESS };
    };
    // Filesystem metadata only. Call from an owned worker, never the UI.
    [[nodiscard]] SearchFolderScope ResolveSearchFolderScope(const std::wstring& folder,
        const SearchMetadataReader& read = {});
    // USN replies use the canonical DWORD serial; app identities retain the
    // complete FILE_ID_INFO serial from this volume's actual root.
    void BindSearchVolumeIdentity(SearchVolumeResult& volume, const SearchFolderScope& actual_volume);
    void FilterSearchFolderResults(const SearchFolderScope& scope, SearchVolumeResult& volume,
        const SearchMetadataReader& read = {});
    [[nodiscard]] SearchActionTarget ValidateSearchResult(const SearchResult& result,
        const SearchMetadataReader& read = {});
}
