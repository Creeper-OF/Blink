#include "Blink/App/SearchResultValidation.h"
#include <algorithm>
namespace blink::app
{
    namespace
    {
        DWORD Read(const SearchMetadataReader& read, const std::wstring& path, blink::core::FileEntry& entry)
        {
            if (read) { return read(path, entry); }
            if (blink::core::ReadEntryMetadata(path, entry)) { return ERROR_SUCCESS; }
            const auto error = GetLastError();
            return error == ERROR_SUCCESS ? ERROR_FILE_INVALID : error;
        }
        std::wstring Canonical(std::wstring path)
        {
            if (path.starts_with(L"\\\\?\\")) { path.erase(0, 4); }
            if (SearchDriveRoot(path).empty()) { return {}; }
            path = blink::core::NormalizePath(path);
            while (path.size() > 3 && path.back() == L'\\') { path.pop_back(); }
            return path;
        }
        void Fail(SearchVolumeResult& volume, DWORD error)
        {
            volume.results.clear(); volume.searched = false;
            volume.stage = VolumeIndexStage::Unavailable; volume.error = error;
        }
    }
    SearchFolderScope ResolveSearchFolderScope(const std::wstring& folder, const SearchMetadataReader& read)
    {
        SearchFolderScope scope;
        scope.path = Canonical(folder);
        if (scope.path.empty()) { scope.error = ERROR_NOT_SUPPORTED; return scope; }
        if (!read)
        {
            const auto type = GetDriveTypeW(SearchDriveRoot(scope.path).c_str());
            if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
            { scope.error = ERROR_NOT_SUPPORTED; return scope; }
        }
        std::size_t end = 3;
        for (;;)
        {
            blink::core::FileEntry entry;
            scope.error = Read(read, scope.path.substr(0, end), entry);
            if (scope.error != ERROR_SUCCESS) { return scope; }
            if (!entry.is_directory || !entry.identity.valid)
            { scope.error = ERROR_DIRECTORY; return scope; }
            if ((entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            { scope.error = ERROR_NOT_SUPPORTED; return scope; }
            scope.identity = entry.identity;
            if (end == scope.path.size()) { return scope; }
            const auto next = scope.path.find(L'\\', end == 3 ? 3 : end + 1);
            end = next == std::wstring::npos ? scope.path.size() : next;
        }
    }
    void BindSearchVolumeIdentity(SearchVolumeResult& volume, const SearchFolderScope& actual_volume)
    {
        if (!volume.searched) { return; }
        if (actual_volume.error != ERROR_SUCCESS)
        { Fail(volume, actual_volume.error); return; }
        const auto root = SearchDriveRoot(volume.root);
        const auto actual_root = Canonical(actual_volume.path);
        if (root.empty() || volume.root.size() != 3 || !actual_volume.identity.valid || actual_root.size() != 3 ||
            SearchDriveRoot(actual_root) != root)
        { Fail(volume, ERROR_FILE_INVALID); return; }
        const auto full_serial = actual_volume.identity.volume_serial;
        const auto canonical_serial = static_cast<DWORD>(full_serial);
        const auto matches = [full_serial, canonical_serial](const blink::core::FileIdentity& identity) {
            return identity.valid && (identity.volume_serial == full_serial ||
                identity.volume_serial == static_cast<std::uint64_t>(canonical_serial));
        };
        for (const auto& result : volume.results)
        {
            if (result.source_volume != root || !matches(result.hit.identity) || !matches(result.hit.parent_identity))
            { Fail(volume, ERROR_FILE_INVALID); return; }
        }
        for (auto& result : volume.results)
        {
            result.hit.identity.volume_serial = full_serial;
            result.hit.parent_identity.volume_serial = full_serial;
        }
    }
    void FilterSearchFolderResults(const SearchFolderScope& scope, SearchVolumeResult& volume, const SearchMetadataReader& read)
    {
        if (!volume.searched) { return; }
        if (scope.error != ERROR_SUCCESS) { Fail(volume, scope.error); return; }
        const auto current = ResolveSearchFolderScope(scope.path, read);
        if (current.error != ERROR_SUCCESS || !scope.identity.valid || current.identity != scope.identity)
        { Fail(volume, current.error == ERROR_SUCCESS ? ERROR_FILE_INVALID : current.error); return; }
        std::vector<SearchResult> filtered;
        for (const auto& result : volume.results)
        {
            const auto path = Canonical(result.hit.full_path);
            const auto size = scope.path.size();
            const auto boundary = size == 3 ? size - 1 : size;
            if (path.size() <= boundary + 1 || path[boundary] != L'\\' ||
                CompareStringOrdinal(path.c_str(), static_cast<int>(size), scope.path.c_str(),
                    static_cast<int>(size), TRUE) != CSTR_EQUAL) { continue; }
            const auto ancestor = ResolveSearchFolderScope(path.substr(0, size), read);
            if (ancestor.error != ERROR_SUCCESS) { Fail(volume, ancestor.error); return; }
            if (ancestor.identity == scope.identity)
            {
                const auto slash = path.find_last_of(L'\\');
                const auto parent_path = path.substr(0, slash == 2 ? 3 : slash);
                const auto parent = ResolveSearchFolderScope(parent_path, read);
                if (parent.error != ERROR_SUCCESS || parent.identity != result.hit.parent_identity)
                { Fail(volume, parent.error == ERROR_SUCCESS ? ERROR_FILE_INVALID : parent.error); return; }
                filtered.push_back(result);
            }
        }
        volume.results = std::move(filtered);
    }
    SearchActionTarget ValidateSearchResult(const SearchResult& result, const SearchMetadataReader& read)
    {
        SearchActionTarget target;
        const auto path = Canonical(result.hit.full_path);
        const auto slash = path.find_last_of(L'\\');
        if (path.empty() || path.size() <= 3 || slash == std::wstring::npos ||
            path.substr(slash + 1) != result.hit.name || result.hit.name.find(L'\0') != std::wstring::npos ||
            SearchDriveRoot(path) != SearchDriveRoot(result.source_volume) || result.source_volume.size() != 3 ||
            !result.hit.identity.valid || !result.hit.parent_identity.valid)
        { target.error = ERROR_INVALID_DATA; return target; }
        target.parent = path.substr(0, slash == 2 ? 3 : slash);
        const auto parent = ResolveSearchFolderScope(target.parent, read);
        if (parent.error != ERROR_SUCCESS) { target.error = parent.error; return target; }
        if (parent.identity != result.hit.parent_identity) { target.error = ERROR_FILE_INVALID; return target; }
        target.parent_identity = parent.identity;
        target.error = Read(read, path, target.entry);
        if (target.error != ERROR_SUCCESS) { return target; }
        if (target.entry.identity != result.hit.identity || target.entry.is_directory != result.hit.is_directory)
        { target.error = ERROR_FILE_INVALID; return target; }
        if ((target.entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        { target.error = ERROR_NOT_SUPPORTED; return target; }
        const auto parent_after = ResolveSearchFolderScope(target.parent, read);
        if (parent_after.error != ERROR_SUCCESS || parent_after.identity != result.hit.parent_identity)
        { target.error = parent_after.error == ERROR_SUCCESS ? ERROR_FILE_INVALID : parent_after.error; return target; }
        target.entry.full_path = path; target.entry.name = result.hit.name;
        target.entry.extension = blink::core::FileExtension(result.hit.name);
        return target;
    }
}
