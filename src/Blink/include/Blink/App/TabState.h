#pragma once

#include "Blink/App/NavigationModel.h"
#include "Blink/App/ItemVisibilityPolicy.h"
#include "Blink/App/RowIdentity.h"
#include "Blink/Core/FileTypes.h"

#include <Windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace blink::app
{
    enum class FolderState
    {
        Idle,
        Ready,
        Empty,
    };

    enum class CommitBrowseStatus
    {
        Committed,
        StaleOrRejected,
        OutOfMemory,
        Failed,
    };

    struct CommitBrowseResult
    {
        CommitBrowseStatus status{ CommitBrowseStatus::Failed };
        DWORD error{ ERROR_GEN_FAILURE };
    };

    [[nodiscard]] constexpr DWORD CommitBrowseErrorCode(
        const CommitBrowseStatus status,
        const DWORD detail = ERROR_GEN_FAILURE) noexcept
    {
        switch (status)
        {
        case CommitBrowseStatus::Committed:
        case CommitBrowseStatus::StaleOrRejected:
            return ERROR_SUCCESS;
        case CommitBrowseStatus::OutOfMemory:
            return ERROR_NOT_ENOUGH_MEMORY;
        case CommitBrowseStatus::Failed:
            return detail == ERROR_SUCCESS ? ERROR_GEN_FAILURE : detail;
        }
        return ERROR_GEN_FAILURE;
    }

    enum class SortField
    {
        Name,
        Size,
        Modified,
        Type,
    };

    enum class SortDirection
    {
        Ascending,
        Descending,
    };

    struct SortDescriptor
    {
        SortField field{ SortField::Name };
        SortDirection direction{ SortDirection::Ascending };

        friend bool operator==(const SortDescriptor&, const SortDescriptor&) = default;
    };

    enum class FolderViewMode
    {
        Details,
        List,
        Grid,
    };

    struct TabSnapshot
    {
        NavigationSnapshot navigation;
        blink::core::DirectoryListing committed_listing;
        ThisPcContent committed_this_pc;
        FolderState folder_state{ FolderState::Idle };
        DWORD latest_browse_error{ ERROR_SUCCESS };
        std::vector<RowIdentity> selection;
        std::optional<RowIdentity> focused_item;
        SortDescriptor sort;
        FolderViewMode view_mode{ FolderViewMode::Details };
        ItemVisibilityPolicy visibility_policy;
    };

    struct TabPresentationSnapshot
    {
        Location committed_location;
        // Derived compatibility value; callers should use committed_location.
        std::wstring committed_current_path;
        FolderState folder_state{ FolderState::Idle };
        DWORD latest_browse_error{ ERROR_SUCCESS };
        std::size_t committed_item_count{};
        std::size_t this_pc_item_count{};
        std::size_t selected_count{};
        bool can_go_back{};
        bool can_go_forward{};
        bool has_pending{};
        std::optional<Location> pending_location;
        SortDescriptor sort;
        FolderViewMode view_mode{ FolderViewMode::Details };
    };

    // Single-tab application state. NavigationModel owns path/history
    // semantics; this class owns the committed folder page, browse error and
    // identity-based selection for that one tab. It has no UI or I/O.
    class TabState
    {
    public:
        [[nodiscard]] std::optional<NavigationRequest> Navigate(const Location& target);
        [[nodiscard]] std::optional<NavigationRequest> Navigate(const std::wstring& target);
        [[nodiscard]] std::optional<NavigationRequest> Back();
        [[nodiscard]] std::optional<NavigationRequest> Forward();
        [[nodiscard]] std::optional<NavigationRequest> Up();
        [[nodiscard]] std::optional<NavigationRequest> Refresh();
        [[nodiscard]] std::optional<NavigationRequest> RelocateFolder(const Location& source, const Location& destination, bool preserve_suffix = true);

        [[nodiscard]] bool IsPending(const NavigationRequest& request) const noexcept;

        // CommitBrowse accepts only a successful listing. It prepares all
        // allocating work before NavigationModel::Commit, then swaps the
        // committed listing/selection without an observable intermediate page.
        [[nodiscard]] CommitBrowseResult CommitBrowse(
            const NavigationRequest& request,
            blink::core::DirectoryListing listing);

        // ThisPC has no DirectoryListing. Its content is committed through a
        // separate value model while sharing the same navigation/history gate.
        [[nodiscard]] CommitBrowseResult CommitThisPC(
            const NavigationRequest& request,
            ThisPcContent content);

        // RejectBrowse never changes the committed path/listing/history.
        [[nodiscard]] bool RejectBrowse(
            const NavigationRequest& request,
            DWORD error) noexcept;

        void Shutdown() noexcept;

        [[nodiscard]] TabSnapshot Snapshot() const;
        // Address history does not need to copy a potentially large directory listing.
        [[nodiscard]] NavigationSnapshot NavigationState() const { return navigation_.Snapshot(); }
        [[nodiscard]] blink::core::FileIdentity CommittedDirectoryIdentity() const noexcept
        { return committed_listing_.directory_identity; }
        [[nodiscard]] TabPresentationSnapshot PresentationSnapshot() const;
        [[nodiscard]] std::vector<blink::core::FileEntry> CommittedEntriesSnapshot() const;
        [[nodiscard]] std::vector<blink::core::FileEntry> VisibleEntriesSnapshot() const;
        [[nodiscard]] ThisPcContent CommittedThisPCSnapshot() const;
        [[nodiscard]] std::vector<blink::core::FileEntry> SortedEntries() const;
        [[nodiscard]] static std::vector<blink::core::FileEntry> SortEntries(
            std::vector<blink::core::FileEntry> entries,
            SortDescriptor descriptor);

        [[nodiscard]] bool SelectEntry(const blink::core::FileEntry& entry);
        void SetRowSelection(std::vector<RowIdentity> rows) noexcept;
        [[nodiscard]] bool SelectAllRows() noexcept;
        // Complement within the current visible committed rows. Allocation
        // failure leaves the existing selection and logical focus unchanged.
        [[nodiscard]] bool InvertRowSelection() noexcept;
        [[nodiscard]] std::optional<blink::core::FileEntry> CommittedRow(
            const RowIdentity& row) const;
        [[nodiscard]] bool IsRowSelected(const RowIdentity& row) const noexcept;
        [[nodiscard]] std::optional<blink::core::FileEntry> SelectedCommittedRow(
            const RowIdentity& row) const;
        // Bounded selection capture in committed-listing order, with one scan.
        // Empty, oversized or stale selections fail as a whole.
        [[nodiscard]] std::optional<std::vector<blink::core::FileEntry>> SelectedCommittedRows(
            std::size_t maximum) const;

        // Compatibility helpers for callers that only have an object
        // identity. UI code must use the row-identity APIs above.
        void SetSelection(std::vector<blink::core::FileIdentity> identities) noexcept;
        void ClearSelection() noexcept;
        [[nodiscard]] bool IsSelected(const blink::core::FileIdentity& identity) const noexcept;
        [[nodiscard]] std::optional<blink::core::FileEntry> SelectedCommittedEntry(
            const blink::core::FileIdentity& identity) const;

        void SetSortDescriptor(SortDescriptor descriptor) noexcept;
        [[nodiscard]] SortDescriptor GetSortDescriptor() const noexcept;

        // Presentation preference only; changing it does not navigate,
        // replace the committed page or change identity-based selection.
        void SetViewMode(FolderViewMode mode) noexcept;
        [[nodiscard]] FolderViewMode GetViewMode() const noexcept;

        // Refilters the committed snapshot without I/O/history changes.
        // Selection and logical focus cannot refer to excluded rows.
        void SetVisibilityPolicy(ItemVisibilityPolicy policy) noexcept;
        [[nodiscard]] ItemVisibilityPolicy GetVisibilityPolicy() const noexcept;

    private:
        [[nodiscard]] std::optional<NavigationRequest> Begin(
            std::optional<NavigationRequest> request);
        [[nodiscard]] static bool ContainsIdentity(
            const std::vector<blink::core::FileEntry>& entries,
            const blink::core::FileIdentity& identity) noexcept;
        [[nodiscard]] static bool ContainsRowIdentity(
            const std::vector<blink::core::FileEntry>& entries,
            const RowIdentity& row) noexcept;
        [[nodiscard]] bool IsVisibleRow(const RowIdentity& row) const noexcept;

        NavigationModel navigation_;
        blink::core::DirectoryListing committed_listing_;
        ThisPcContent committed_this_pc_;
        FolderState folder_state_{ FolderState::Idle };
        DWORD latest_browse_error_{ ERROR_SUCCESS };
        std::vector<RowIdentity> selection_;
        std::unordered_set<RowIdentity, RowIdentityHash> selection_lookup_;
        std::unordered_map<RowIdentity, DWORD, RowIdentityHash> committed_rows_lookup_;
        std::optional<RowIdentity> focused_item_;
        SortDescriptor sort_;
        FolderViewMode view_mode_{ FolderViewMode::Details };
        ItemVisibilityPolicy visibility_policy_;
        std::size_t visible_item_count_{};
    };
}
