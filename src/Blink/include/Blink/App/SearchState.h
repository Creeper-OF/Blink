#pragma once

#include "Blink/App/IndexServiceState.h"
#include <mutex>

namespace blink::app
{
    enum class SearchScope { AllLocalVolumes = 0, CurrentFolder = 1, FollowLocation, CurrentVolume, SelectedVolumes, SelectedFolder };
    enum class SearchMode { Substring, Prefix, Extension, Exact };
    inline constexpr std::uint32_t kSearchCandidateLimit = 1000;
    enum class SearchStage { Idle, Searching, Complete, Partial, Unavailable, Closed };

    struct SearchQuery
    {
        std::wstring text;
        SearchScope scope{ SearchScope::AllLocalVolumes };
        SearchMode mode{ SearchMode::Substring };
        std::wstring folder;
        std::uint32_t limit{ 200 };
        std::wstring selected_volumes;
        bool include_subfolders{ true };
        friend bool operator==(const SearchQuery&, const SearchQuery&) = default;
    };
    struct SearchResult
    {
        std::wstring source_volume;
        blink::index::ipc::SearchHit hit;
    };
    struct SearchVolumeResult
    {
        std::wstring root;
        VolumeIndexStage stage{ VolumeIndexStage::Unconfirmed };
        DWORD error{ ERROR_SUCCESS };
        bool searched{};
        bool limit_reached{};
        bool query_incomplete{};
        std::uint64_t request_id{};
        std::vector<SearchResult> results;
    };
    struct SearchSnapshot
    {
        std::uint64_t generation{};
        SearchStage stage{ SearchStage::Idle };
        SearchQuery query;
        DWORD error{ ERROR_SUCCESS };
        blink::core::FileIdentity scope_identity;
        std::vector<SearchVolumeResult> volumes;
    };

    [[nodiscard]] std::wstring SearchDriveRoot(const std::wstring& path);
    [[nodiscard]] DWORD ParseSearchVolumeRoots(const std::wstring& text, std::vector<std::wstring>& roots);
    [[nodiscard]] DWORD ValidateSearchQuery(const SearchQuery& query);
    [[nodiscard]] blink::index::ipc::Request MakeSearchRequest(const SearchQuery& query, const std::wstring& root);
    [[nodiscard]] SearchVolumeResult MapSearchReply(const SearchQuery& query, std::wstring root, const IndexRpcResult& reply);
    [[nodiscard]] bool SameSearchLink(const SearchResult& left, const SearchResult& right) noexcept;

    class SearchState
    {
    public:
        [[nodiscard]] std::uint64_t Begin(SearchQuery query, bool retry = false);
        [[nodiscard]] bool IsCurrent(std::uint64_t generation) const noexcept;
        [[nodiscard]] bool Complete(std::uint64_t generation, SearchSnapshot snapshot);
        [[nodiscard]] SearchSnapshot Snapshot() const;
        void Cancel() noexcept;
        void Shutdown() noexcept;
    private:
        mutable std::mutex mutex_;
        SearchSnapshot snapshot_;
        bool closed_{};
    };
}
