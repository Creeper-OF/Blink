#pragma once

#include "Blink/Core/FileTypes.h"
#include "Blink/Index/UsnJournal.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace blink::index
{
#ifdef BLINK_INDEX_ENABLE_TEST_ACCESS
    class FastIndexTestAccess;
#endif

    enum class FastIndexHealth : std::uint32_t
    {
        Unknown = 0,
        Rebuilding,
        CatchingUp,
        Healthy,
        RebuildRequired,
        Unavailable,
        Corrupted,
    };

    [[nodiscard]] const wchar_t* FastIndexHealthName(FastIndexHealth health) noexcept;

    struct FastIndexSnapshot
    {
        std::wstring volume_root;
        std::uint64_t volume_serial{};
        std::uint64_t journal_id{};
        std::int64_t first_usn{};
        std::int64_t next_usn{};
        std::int64_t lowest_valid_usn{};
        std::int64_t max_usn{};
        std::int64_t last_usn{};
        core::FileIdentity root_identity{};
        FastIndexHealth health{ FastIndexHealth::Unknown };
        DWORD error{};
    };

    struct FastIndexStats
    {
        std::uint64_t records_seen{};
        std::uint64_t records_applied{};
        std::uint64_t records_deleted{};
        std::uint64_t records_renamed{};
        std::uint64_t links{};
        std::uint64_t unique_files{};
        std::uint64_t directories{};
        std::uint64_t files{};
        std::uint64_t index_bytes{};
        std::uint64_t resident_bytes{};
        std::uint64_t mft_records{};
        std::uint64_t usn_records{};
        std::uint64_t user_mutations_applied{};
        double build_seconds{};
        double mft_enumeration_seconds{};
        double parent_tree_build_seconds{};
        double usn_read_seconds{};
        double save_seconds{};
        std::int64_t catchup_start_usn{};
        std::int64_t catchup_target_usn{};
        std::int64_t catchup_completed_usn{};
        std::uint32_t catchup_batch_count{};
        std::wstring failure_stage;
        bool used_mft{};
        bool used_usn{};
        FastIndexHealth health{ FastIndexHealth::Unknown };
        DWORD error{};
    };

    enum class FastIndexProgressStage : std::uint32_t
    {
        EnumeratingMft = 0,
        CatchingUpUsn,
    };

    using FastIndexProgressCallback = std::function<void(
        FastIndexProgressStage stage,
        const FastIndexStats& stats)>;

    struct FastSearchOptions
    {
        std::wstring text;
        bool prefix{};
        std::wstring extension;
        std::wstring path_prefix;
        std::size_t limit{ 200 };
        // Test/reference switch only. It is not part of the IPC or BLK5
        // formats; production callers leave the derived extension index on.
        bool use_derived_extension_index{ true };
        // Restrict path_prefix to the immediate parent before applying limit.
        bool direct_children{};
        // Exclude these attribute bits before path resolution and result limit.
        // Zero retains the existing unfiltered Search contract.
        DWORD excluded_attributes{};
    };

    struct FastSearchResult
    {
        core::FileIdentity identity{};
        core::FileIdentity parent_identity{};
        std::wstring name;
        std::wstring full_path;
        DWORD attributes{};
        bool is_directory{};
        std::int64_t last_usn{};
    };

    struct FastSearchTiming
    {
        explicit FastSearchTiming(std::chrono::steady_clock::time_point received) noexcept
            : request_received(received) {}

        std::chrono::steady_clock::time_point request_received;
        std::atomic<std::int64_t> lock_wait_start_us{ -1 };
        std::atomic<std::int64_t> lock_acquired_us{ -1 };
        std::atomic<std::int64_t> search_start_us{ -1 };
        std::atomic<std::int64_t> search_end_us{ -1 };
        std::atomic<std::int64_t> candidate_filter_start_us{ -1 };
        std::atomic<std::int64_t> candidate_filter_end_us{ -1 };
        std::atomic<std::int64_t> path_resolution_start_us{ -1 };
        std::atomic<std::int64_t> path_resolution_end_us{ -1 };
        std::atomic<std::int64_t> result_construction_start_us{ -1 };
        std::atomic<std::int64_t> result_construction_end_us{ -1 };
        std::atomic<std::int64_t> sorting_start_us{ -1 };
        std::atomic<std::int64_t> sorting_end_us{ -1 };
        std::atomic<std::uint64_t> result_count{};
    };

    class FastIndex
    {
    public:
        FastIndex() = default;

        void SetInternalExcludedRoots(std::vector<std::wstring> roots);

        [[nodiscard]] bool BuildInitial(
            const std::wstring& path,
            const UsnCancelPredicate& cancel = {},
            FastIndexStats* stats = nullptr,
            const FastIndexProgressCallback& progress = {});
        [[nodiscard]] bool CatchUp(
            const std::wstring& path,
            const UsnCancelPredicate& cancel = {},
            FastIndexStats* stats = nullptr,
            const FastIndexProgressCallback& progress = {});

        [[nodiscard]] bool Save(const std::wstring& path, DWORD* error = nullptr) const;
        [[nodiscard]] bool Load(const std::wstring& path, const std::wstring& expected_root, DWORD* error = nullptr);
        [[nodiscard]] static bool ValidateStoreFile(const std::wstring& path, DWORD* error = nullptr);

        [[nodiscard]] std::vector<FastSearchResult> Search(
            const FastSearchOptions& options,
            FastSearchTiming* timing = nullptr) const;
        [[nodiscard]] FastIndexSnapshot Snapshot() const;
        [[nodiscard]] FastIndexStats Stats() const;
        [[nodiscard]] bool IsLoaded() const;
        void Clear();

    private:
#ifdef BLINK_INDEX_ENABLE_TEST_ACCESS
        friend class FastIndexTestAccess;
#endif

        struct Link
        {
            core::FileIdentity identity{};
            core::FileIdentity parent_identity{};
            std::wstring name;
            DWORD attributes{};
            std::int64_t last_usn{};
            std::wstring folded_name;
        };

        struct InternalExcludedRootIdentity
        {
            // Keep the complete FILE_ID_INFO-derived identity for long-term
            // FileIdentity semantics. The canonical 32-bit value is a
            // separate comparison key for USN identities only.
            core::FileIdentity full_identity{};
            DWORD canonical_volume_serial{};
        };

        struct IdentityHash
        {
            [[nodiscard]] std::size_t operator()(const core::FileIdentity& identity) const noexcept;
        };

        void AddExtensionIndexEntryUnlocked(const Link& link);
        void RemoveExtensionIndexEntryUnlocked(
            const core::FileIdentity& identity,
            const std::wstring& extension);
        void RebuildExtensionIndexUnlocked();

        [[nodiscard]] bool CatchUpUnlocked(
            const std::wstring& path,
            const UsnCancelPredicate& cancel,
            DWORD& error,
            const FastIndexProgressCallback& progress);
        [[nodiscard]] bool ApplyRecordUnlocked(const UsnRecordSummary& record);
        [[nodiscard]] bool AddOrUpdateLinkUnlocked(const Link& link);
        [[nodiscard]] bool RemoveLinkUnlocked(const Link& link);
        void UpdateLiveCountStatsUnlocked();
        void RecomputeStatsUnlocked();
        [[nodiscard]] std::wstring ResolveLinkPathUnlocked(
            const Link& link,
            std::unordered_set<core::FileIdentity, IdentityHash>& visiting,
            std::size_t depth) const;
        [[nodiscard]] std::wstring ResolveIdentityPathUnlocked(
            const core::FileIdentity& identity,
            std::unordered_set<core::FileIdentity, IdentityHash>& visiting,
            std::size_t depth) const;
        [[nodiscard]] bool IsInternalExcludedPathUnlocked(const Link& link) const;
        [[nodiscard]] bool IsInternalExcludedResolvedPathUnlocked(const std::wstring& full_path) const;

        mutable std::mutex mutex_;
        std::vector<std::wstring> internal_excluded_roots_;
        std::vector<InternalExcludedRootIdentity> internal_excluded_root_identities_;
        std::unordered_map<core::FileIdentity, std::vector<Link>, IdentityHash> links_;
        // Derived acceleration structure only. links_ remains authoritative;
        // this map is rebuilt after BLK5 Load and maintained with USN link
        // mutations. A FileIdentity bucket intentionally expands to all hard
        // links for that identity, so multiple names are never collapsed.
        std::unordered_map<
            std::wstring,
            std::unordered_set<core::FileIdentity, IdentityHash>> extension_index_;
        FastIndexSnapshot snapshot_;
        FastIndexStats stats_;
    };
}
