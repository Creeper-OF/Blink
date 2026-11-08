#include "Blink/App/SearchState.h"
#include <algorithm>
#include <cwctype>
#include <utility>

namespace blink::app
{
    namespace
    {
        bool EqualPath(const std::wstring& a, const std::wstring& b) noexcept
        {
            return a.size() == b.size() && CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
        }
        std::wstring CanonicalPath(std::wstring path)
        {
            if (path.starts_with(L"\\\\?\\")) { path.erase(0, 4); }
            if (path.size() > 32767 || path.find(L'\0') != std::wstring::npos ||
                path.size() < 3 || path[1] != L':' || path[2] != L'\\' ||
                !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
                path.find(L':', 2) != std::wstring::npos) { return {}; }
            path = blink::core::NormalizePath(path);
            while (path.size() > 3 && path.back() == L'\\') { path.pop_back(); }
            return path;
        }
        bool InsideFolder(const std::wstring& path, const std::wstring& folder, const bool include_subfolders)
        {
            auto prefix = folder;
            if (prefix.back() != L'\\') { prefix += L'\\'; }
            return path.size() > prefix.size() &&
                EqualPath(path.substr(0, prefix.size()), prefix) &&
                (include_subfolders || path.find(L'\\', prefix.size()) == std::wstring::npos);
        }
    }

    std::wstring SearchDriveRoot(const std::wstring& path)
    {
        const auto canonical = CanonicalPath(path);
        if (canonical.empty()) { return {}; }
        auto root = canonical.substr(0, 3);
        if (root[0] >= L'a' && root[0] <= L'z') { root[0] -= L'a' - L'A'; }
        return root;
    }

    DWORD ParseSearchVolumeRoots(const std::wstring& text, std::vector<std::wstring>& roots)
    {
        roots.clear();
        if (text.empty() || text.size() > 32767 || text.find(L'\0') != std::wstring::npos)
        { return ERROR_INVALID_PARAMETER; }
        std::vector<std::wstring> parsed;
        const auto separator = [](const wchar_t value) {
            return value == L';' || value == L',' || std::iswspace(value) != 0;
        };
        for (std::size_t offset = 0; offset < text.size();)
        {
            while (offset < text.size() && separator(text[offset])) { ++offset; }
            if (offset == text.size()) { break; }
            auto end = offset;
            while (end < text.size() && !separator(text[end])) { ++end; }
            const auto size = end - offset;
            auto letter = text[offset];
            if (letter >= L'a' && letter <= L'z') { letter -= L'a' - L'A'; }
            if (letter < L'A' || letter > L'Z' || size > 3 ||
                (size >= 2 && text[offset + 1] != L':') ||
                (size == 3 && text[offset + 2] != L'\\')) { return ERROR_INVALID_NAME; }
            std::wstring root{ letter, L':', L'\\' };
            if (std::find(parsed.begin(), parsed.end(), root) == parsed.end()) { parsed.push_back(std::move(root)); }
            offset = end;
        }
        if (parsed.empty()) { return ERROR_INVALID_PARAMETER; }
        roots = std::move(parsed);
        return ERROR_SUCCESS;
    }

    DWORD ValidateSearchQuery(const SearchQuery& query)
    {
        if (query.text.empty() || query.text.size() > 32767 || query.text.find(L'\0') != std::wstring::npos ||
            std::all_of(query.text.begin(), query.text.end(), [](wchar_t c) { return std::iswspace(c) != 0; }) ||
            query.limit == 0 || query.limit > 1000 ||
            (query.scope != SearchScope::AllLocalVolumes && query.scope != SearchScope::CurrentFolder &&
                query.scope != SearchScope::CurrentVolume && query.scope != SearchScope::SelectedVolumes &&
                query.scope != SearchScope::SelectedFolder) ||
            (query.mode != SearchMode::Substring && query.mode != SearchMode::Prefix && query.mode != SearchMode::Extension &&
                query.mode != SearchMode::Exact))
        { return ERROR_INVALID_PARAMETER; }
        if ((query.scope == SearchScope::CurrentFolder || query.scope == SearchScope::SelectedFolder ||
            query.scope == SearchScope::CurrentVolume) && CanonicalPath(query.folder).empty())
        { return ERROR_NOT_SUPPORTED; }
        if (query.scope == SearchScope::SelectedVolumes)
        {
            std::vector<std::wstring> roots;
            return ParseSearchVolumeRoots(query.selected_volumes, roots);
        }
        return ERROR_SUCCESS;
    }

    blink::index::ipc::Request MakeSearchRequest(const SearchQuery& query, const std::wstring& root)
    {
        blink::index::ipc::Request request;
        const bool folder_scope = query.scope == SearchScope::CurrentFolder || query.scope == SearchScope::SelectedFolder;
        request.type = folder_scope && !query.include_subfolders ?
            blink::index::ipc::RequestType::SearchDirectChildren : blink::index::ipc::RequestType::Search;
        request.volume_root = SearchDriveRoot(root);
        request.limit = query.mode == SearchMode::Exact ? kSearchCandidateLimit : query.limit;
        request.prefix = query.mode == SearchMode::Prefix || query.mode == SearchMode::Exact;
        if (query.mode == SearchMode::Extension)
        {
            request.extension = query.text.starts_with(L".") ? query.text : L"." + query.text;
        }
        else { request.text = query.text; }
        if (folder_scope)
        {
            request.path_prefix = CanonicalPath(query.folder);
            if (!request.path_prefix.empty() && request.path_prefix.back() != L'\\') { request.path_prefix += L'\\'; }
        }
        return request;
    }

    bool SameSearchLink(const SearchResult& left, const SearchResult& right) noexcept
    {
        return EqualPath(left.source_volume, right.source_volume) && left.hit.identity == right.hit.identity &&
            left.hit.parent_identity == right.hit.parent_identity && left.hit.name == right.hit.name &&
            EqualPath(left.hit.full_path, right.hit.full_path);
    }

    SearchVolumeResult MapSearchReply(const SearchQuery& query, std::wstring root, const IndexRpcResult& reply)
    {
        SearchVolumeResult volume;
        volume.root = SearchDriveRoot(root);
        const auto mapped = MapVolumeIndexStatus(volume.root, reply);
        volume.stage = mapped.stage;
        volume.error = mapped.error;
        volume.request_id = mapped.request_id;
        const auto query_error = ValidateSearchQuery(query);
        const bool folder_scope = query.scope == SearchScope::CurrentFolder || query.scope == SearchScope::SelectedFolder;
        std::vector<std::wstring> selected_roots;
        if (query.scope == SearchScope::SelectedVolumes) { (void)ParseSearchVolumeRoots(query.selected_volumes, selected_roots); }
        if (volume.root.empty() || query_error != ERROR_SUCCESS ||
            ((folder_scope || query.scope == SearchScope::CurrentVolume) && volume.root != SearchDriveRoot(query.folder)) ||
            (query.scope == SearchScope::SelectedVolumes &&
                std::find(selected_roots.begin(), selected_roots.end(), volume.root) == selected_roots.end()))
        {
            volume.error = query_error == ERROR_SUCCESS ? ERROR_INVALID_PARAMETER : query_error;
            volume.stage = VolumeIndexStage::Unavailable;
            return volume;
        }
        if (ValidateIndexReply(reply) != ERROR_SUCCESS || reply.response.status != blink::index::ipc::ResponseStatus::Ok ||
            reply.response.error != ERROR_SUCCESS ||
            (volume.stage != VolumeIndexStage::Healthy && volume.stage != VolumeIndexStage::CatchingUp))
        {
            if (volume.error == ERROR_SUCCESS) { volume.error = ERROR_INVALID_STATE; }
            return volume;
        }
        const auto wire_limit = query.mode == SearchMode::Exact ? kSearchCandidateLimit : query.limit;
        if (reply.response.results.size() > wire_limit)
        { volume.error = ERROR_INVALID_DATA; volume.stage = VolumeIndexStage::Unavailable; return volume; }
        volume.searched = true;
        volume.query_incomplete = query.mode == SearchMode::Exact && reply.response.results.size() >= kSearchCandidateLimit;
        volume.limit_reached = query.mode != SearchMode::Exact && reply.response.results.size() >= query.limit;
        const auto folder = CanonicalPath(query.folder);
        for (const auto& hit : reply.response.results)
        {
            const auto path = CanonicalPath(hit.full_path);
            const auto slash = path.find_last_of(L'\\');
            if (!hit.identity.valid || !hit.parent_identity.valid ||
                hit.identity.volume_serial != hit.parent_identity.volume_serial ||
                hit.name.empty() || hit.name.find_first_of(L"\\/:\0", 0, 4) != std::wstring::npos ||
                path.empty() || SearchDriveRoot(path) != volume.root || slash == std::wstring::npos ||
                path.substr(slash + 1) != hit.name ||
                hit.is_directory != ((hit.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
            {
                volume.error = ERROR_INVALID_DATA;
                volume.stage = VolumeIndexStage::Unavailable;
                volume.searched = false;
                volume.results.clear();
                return volume;
            }
            if (query.mode == SearchMode::Exact && !EqualPath(hit.name, query.text)) { continue; }
            if (folder_scope && !InsideFolder(path, folder, query.include_subfolders)) { continue; }
            SearchResult result{ volume.root, hit };
            result.hit.full_path = path;
            if (std::none_of(volume.results.begin(), volume.results.end(),
                [&result](const auto& existing) { return SameSearchLink(existing, result); }))
            { volume.results.push_back(std::move(result)); }
            if (volume.results.size() == query.limit) { volume.limit_reached = true; break; }
        }
        return volume;
    }

    std::uint64_t SearchState::Begin(SearchQuery query, const bool retry)
    {
        std::scoped_lock lock(mutex_);
        if (closed_ || snapshot_.stage == SearchStage::Searching ||
            (retry && (query != snapshot_.query || snapshot_.stage == SearchStage::Idle))) { return 0; }
        ++snapshot_.generation;
        snapshot_.stage = SearchStage::Searching;
        snapshot_.error = ERROR_SUCCESS;
        if (!retry) { snapshot_.volumes.clear(); snapshot_.scope_identity = {}; }
        snapshot_.query = std::move(query);
        return snapshot_.generation;
    }
    bool SearchState::IsCurrent(const std::uint64_t generation) const noexcept
    {
        std::scoped_lock lock(mutex_);
        return !closed_ && generation != 0 && generation == snapshot_.generation;
    }
    bool SearchState::Complete(const std::uint64_t generation, SearchSnapshot snapshot)
    {
        std::scoped_lock lock(mutex_);
        if (closed_ || generation == 0 || generation != snapshot_.generation || snapshot_.stage != SearchStage::Searching ||
            snapshot.query != snapshot_.query ||
            (snapshot.stage != SearchStage::Complete && snapshot.stage != SearchStage::Partial && snapshot.stage != SearchStage::Unavailable))
        { return false; }
        snapshot.generation = generation;
        snapshot_ = std::move(snapshot);
        return true;
    }
    SearchSnapshot SearchState::Snapshot() const { std::scoped_lock lock(mutex_); return snapshot_; }
    void SearchState::Cancel() noexcept
    {
        std::scoped_lock lock(mutex_);
        if (!closed_) { ++snapshot_.generation; snapshot_.stage = SearchStage::Idle; snapshot_.volumes.clear(); }
    }
    void SearchState::Shutdown() noexcept
    {
        std::scoped_lock lock(mutex_);
        closed_ = true; ++snapshot_.generation; snapshot_.stage = SearchStage::Closed;
    }
}
