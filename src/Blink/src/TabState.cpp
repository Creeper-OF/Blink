#include "Blink/App/TabState.h"
#include "Blink/App/FolderViewFormatting.h"

#include <algorithm>
#include <cwctype>
#include <new>
#include <numeric>
#include <unordered_map>
#include <utility>

namespace blink::app
{
    std::optional<NavigationRequest> TabState::Navigate(const Location& target)
    {
        const auto request = navigation_.Navigate(target);
        if (!request.has_value())
        {
            latest_browse_error_ = ERROR_INVALID_NAME;
            return std::nullopt;
        }
        return Begin(std::move(request));
    }

    std::optional<NavigationRequest> TabState::Navigate(const std::wstring& target)
    {
        return Navigate(Location::Folder(target));
    }

    std::optional<NavigationRequest> TabState::Back()
    {
        return Begin(navigation_.Back());
    }

    std::optional<NavigationRequest> TabState::Forward()
    {
        return Begin(navigation_.Forward());
    }

    std::optional<NavigationRequest> TabState::Up()
    {
        return Begin(navigation_.Up());
    }

    std::optional<NavigationRequest> TabState::Refresh()
    {
        return Begin(navigation_.Refresh());
    }

    bool TabState::IsPending(const NavigationRequest& request) const noexcept
    {
        return navigation_.IsPending(request);
    }

    std::optional<NavigationRequest> TabState::RelocateFolder(const Location& source, const Location& destination, const bool preserve_suffix)
    { return Begin(navigation_.RelocateFolder(source, destination, preserve_suffix)); }

    CommitBrowseResult TabState::CommitBrowse(
        const NavigationRequest& request,
        blink::core::DirectoryListing listing)
    {
        try
        {
            if (!navigation_.IsPending(request))
            {
                return {
                    CommitBrowseStatus::StaleOrRejected,
                    CommitBrowseErrorCode(CommitBrowseStatus::StaleOrRejected) };
            }

            if (!request.target.IsFolder())
            {
                return {
                    CommitBrowseStatus::Failed,
                    CommitBrowseErrorCode(CommitBrowseStatus::Failed) };
            }

            if (listing.error != ERROR_SUCCESS)
            {
                return {
                    CommitBrowseStatus::Failed,
                    CommitBrowseErrorCode(CommitBrowseStatus::Failed, listing.error) };
            }

            std::unordered_map<RowIdentity, DWORD, RowIdentityHash> next_rows_lookup;
            std::size_t next_visible_count{};
            next_rows_lookup.reserve(listing.entries.size());
            for (const auto& entry : listing.entries)
            {
                if (entry.identity.valid)
                {
                    next_rows_lookup.emplace(MakeRowIdentity(entry), entry.attributes);
                }
                next_visible_count += visibility_policy_.Includes(entry.attributes) ? 1 : 0;
            }

            const auto navigation_before = navigation_.Snapshot();
            const bool preserve_selection =
                request.action == NavigationAction::Refresh ||
                request.action == NavigationAction::Relocate ||
                navigation_before.committed_location == request.target;

            std::vector<RowIdentity> next_selection;
            std::unordered_set<RowIdentity, RowIdentityHash> next_selection_lookup;
            if (preserve_selection)
            {
                next_selection.reserve(selection_.size());
                for (const auto& row : selection_)
                {
                    const auto found = next_rows_lookup.find(row);
                    if (row.file_identity.valid && found != next_rows_lookup.end() &&
                        visibility_policy_.Includes(found->second))
                    {
                        next_selection.push_back(row);
                        next_selection_lookup.insert(row);
                    }
                }
            }

            std::optional<RowIdentity> next_focused;
            const auto focused = focused_item_ ? next_rows_lookup.find(*focused_item_) :
                next_rows_lookup.end();
            if (focused_item_.has_value() && focused_item_->file_identity.valid &&
                focused != next_rows_lookup.end() && visibility_policy_.Includes(focused->second) &&
                preserve_selection)
            {
                next_focused = focused_item_;
            }

            listing.path = request.target.FolderPath();

            // NavigationModel::Commit has a strong pre-publication path for
            // the allocating Navigate/Up cases. The following swaps are
            // non-throwing, so no UI-visible state is split between pages.
            if (!navigation_.Commit(request))
            {
                return {
                    CommitBrowseStatus::StaleOrRejected,
                    CommitBrowseErrorCode(CommitBrowseStatus::StaleOrRejected) };
            }
            std::swap(committed_listing_, listing);
            committed_rows_lookup_.swap(next_rows_lookup);
            selection_.swap(next_selection);
            selection_lookup_.swap(next_selection_lookup);
            focused_item_ = next_focused;
            visible_item_count_ = next_visible_count;
            folder_state_ = visible_item_count_ == 0 ? FolderState::Empty : FolderState::Ready;
            latest_browse_error_ = ERROR_SUCCESS;
            return {
                CommitBrowseStatus::Committed,
                CommitBrowseErrorCode(CommitBrowseStatus::Committed) };
        }
        catch (const std::bad_alloc&)
        {
            return {
                CommitBrowseStatus::OutOfMemory,
                CommitBrowseErrorCode(CommitBrowseStatus::OutOfMemory) };
        }
        catch (...)
        {
            return {
                CommitBrowseStatus::Failed,
                CommitBrowseErrorCode(CommitBrowseStatus::Failed) };
        }
    }

    CommitBrowseResult TabState::CommitThisPC(
        const NavigationRequest& request,
        ThisPcContent content)
    {
        try
        {
            if (!navigation_.IsPending(request) || !request.target.IsThisPC())
            {
                return {
                    CommitBrowseStatus::StaleOrRejected,
                    CommitBrowseErrorCode(CommitBrowseStatus::StaleOrRejected) };
            }

            if (!navigation_.Commit(request))
            {
                return {
                    CommitBrowseStatus::StaleOrRejected,
                    CommitBrowseErrorCode(CommitBrowseStatus::StaleOrRejected) };
            }

            committed_listing_ = {};
            committed_rows_lookup_.clear();
            visible_item_count_ = 0;
            committed_this_pc_ = std::move(content);
            selection_.clear();
            selection_lookup_.clear();
            focused_item_.reset();
            folder_state_ = FolderState::Idle;
            latest_browse_error_ = ERROR_SUCCESS;
            return {
                CommitBrowseStatus::Committed,
                CommitBrowseErrorCode(CommitBrowseStatus::Committed) };
        }
        catch (const std::bad_alloc&)
        {
            return {
                CommitBrowseStatus::OutOfMemory,
                CommitBrowseErrorCode(CommitBrowseStatus::OutOfMemory) };
        }
        catch (...)
        {
            return {
                CommitBrowseStatus::Failed,
                CommitBrowseErrorCode(CommitBrowseStatus::Failed) };
        }
    }

    bool TabState::RejectBrowse(
        const NavigationRequest& request,
        const DWORD error) noexcept
    {
        if (!navigation_.Reject(request))
        {
            return false;
        }

        latest_browse_error_ = error;
        return true;
    }

    void TabState::Shutdown() noexcept
    {
        navigation_.Shutdown();
    }

    TabSnapshot TabState::Snapshot() const
    {
        TabSnapshot snapshot;
        snapshot.navigation = navigation_.Snapshot();
        snapshot.committed_listing = committed_listing_;
        snapshot.committed_this_pc = committed_this_pc_;
        snapshot.folder_state = folder_state_;
        snapshot.latest_browse_error = latest_browse_error_;
        snapshot.selection = selection_;
        snapshot.focused_item = focused_item_;
        snapshot.sort = sort_;
        snapshot.view_mode = view_mode_;
        snapshot.visibility_policy = visibility_policy_;
        return snapshot;
    }

    TabPresentationSnapshot TabState::PresentationSnapshot() const
    {
        const auto navigation = navigation_.Snapshot();
        TabPresentationSnapshot snapshot;
        snapshot.committed_location = navigation.committed_location;
        snapshot.committed_current_path = navigation.committed_current_path;
        snapshot.folder_state = folder_state_;
        snapshot.latest_browse_error = latest_browse_error_;
        snapshot.committed_item_count = navigation.committed_location.IsThisPC() ?
            committed_this_pc_.items.size() : visible_item_count_;
        snapshot.this_pc_item_count = committed_this_pc_.items.size();
        snapshot.selected_count = selection_.size();
        snapshot.can_go_back = navigation.can_go_back;
        snapshot.can_go_forward = navigation.can_go_forward;
        snapshot.has_pending = navigation.has_pending;
        snapshot.pending_location = navigation.pending_location;
        snapshot.sort = sort_;
        snapshot.view_mode = view_mode_;
        return snapshot;
    }

    std::vector<blink::core::FileEntry> TabState::CommittedEntriesSnapshot() const
    {
        return committed_listing_.entries;
    }

    std::vector<blink::core::FileEntry> TabState::VisibleEntriesSnapshot() const
    {
        std::vector<blink::core::FileEntry> entries;
        entries.reserve(visible_item_count_);
        for (const auto& entry : committed_listing_.entries)
        {
            if (visibility_policy_.Includes(entry.attributes))
            {
                entries.push_back(entry);
            }
        }
        return entries;
    }

    ThisPcContent TabState::CommittedThisPCSnapshot() const
    {
        return committed_this_pc_;
    }

    std::vector<blink::core::FileEntry> TabState::SortedEntries() const
    {
        return SortEntries(VisibleEntriesSnapshot(), sort_);
    }

    std::vector<blink::core::FileEntry> TabState::SortEntries(
        std::vector<blink::core::FileEntry> entries,
        const SortDescriptor descriptor)
    {
        if (descriptor.field == SortField::Type)
        {
            // Use exactly the displayed type labels, including shared labels
            // for .txt/.log, image formats and C++ source extensions. Cache
            // per normalized extension for this sort only, so comparison has
            // no resource lookup and does not retain language-sensitive data.
            std::unordered_map<std::wstring, std::wstring> type_labels;
            std::vector<const std::wstring*> type_keys(entries.size(), nullptr);
            std::vector<std::size_t> order(entries.size());
            std::iota(order.begin(), order.end(), std::size_t{});
            for (std::size_t index = 0; index < entries.size(); ++index)
            {
                const auto& entry = entries[index];
                if (entry.is_directory)
                {
                    continue;
                }
                auto extension = entry.extension;
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](const wchar_t character) {
                        return static_cast<wchar_t>(std::towlower(character));
                    });
                const auto [label, inserted] = type_labels.try_emplace(std::move(extension));
                if (inserted)
                {
                    label->second = FileTypeLabel(entry);
                }
                // unordered_map rehash preserves pointers to element values.
                type_keys[index] = &label->second;
            }

            std::stable_sort(order.begin(), order.end(), [&](
                const std::size_t left_index, const std::size_t right_index) {
                    const auto& left = entries[left_index];
                    const auto& right = entries[right_index];
                    if (left.is_directory != right.is_directory)
                    {
                        return left.is_directory;
                    }
                    int comparison = left.is_directory ? 0 :
                        type_keys[left_index]->compare(*type_keys[right_index]);
                    if (comparison == 0)
                    {
                        comparison = left.name < right.name ? -1 :
                            (left.name == right.name ? 0 : 1);
                    }
                    return descriptor.direction == SortDirection::Descending ?
                        comparison > 0 : comparison < 0;
                });

            std::vector<blink::core::FileEntry> sorted;
            sorted.reserve(entries.size());
            for (const auto index : order)
            {
                sorted.push_back(std::move(entries[index]));
            }
            return sorted;
        }

        std::stable_sort(entries.begin(), entries.end(), [descriptor](
            const blink::core::FileEntry& left,
            const blink::core::FileEntry& right) {
                if (left.is_directory != right.is_directory)
                {
                    return left.is_directory;
                }

                int comparison = 0;
                switch (descriptor.field)
                {
                case SortField::Name:
                    comparison = left.name < right.name ? -1 : (left.name == right.name ? 0 : 1);
                    break;
                case SortField::Type:
                    // Type sorting uses prepared labels in the branch above.
                    break;
                case SortField::Size:
                    comparison = left.size < right.size ? -1 : (left.size == right.size ? 0 : 1);
                    break;
                case SortField::Modified:
                {
                    const ULARGE_INTEGER left_time{
                        left.last_write_time.dwLowDateTime,
                        left.last_write_time.dwHighDateTime };
                    const ULARGE_INTEGER right_time{
                        right.last_write_time.dwLowDateTime,
                        right.last_write_time.dwHighDateTime };
                    comparison = left_time.QuadPart < right_time.QuadPart ?
                        -1 : (left_time.QuadPart == right_time.QuadPart ? 0 : 1);
                    break;
                }
                }

                if (comparison == 0)
                {
                    comparison = left.name < right.name ? -1 : (left.name == right.name ? 0 : 1);
                }
                if (descriptor.direction == SortDirection::Descending)
                {
                    return comparison > 0;
                }
                return comparison < 0;
            });
        return entries;
    }

    bool TabState::SelectEntry(const blink::core::FileEntry& entry)
    {
        const auto row = MakeRowIdentity(entry);
        if (!row.file_identity.valid ||
            !IsVisibleRow(row))
        {
            ClearSelection();
            return false;
        }

        SetRowSelection({ row });
        return true;
    }

    void TabState::SetRowSelection(std::vector<RowIdentity> rows) noexcept
    {
        selection_.clear();
        selection_lookup_.clear();
        selection_.reserve(rows.size());

        for (auto& row : rows)
        {
            if (!row.file_identity.valid)
            {
                continue;
            }
            if (!IsVisibleRow(row))
            {
                continue;
            }
            if (selection_lookup_.insert(row).second)
            {
                selection_.push_back(std::move(row));
            }
        }

        focused_item_ = selection_.empty() ?
            std::nullopt : std::optional<RowIdentity>(selection_.back());
    }

    bool TabState::SelectAllRows() noexcept
    {
        try
        {
            std::vector<RowIdentity> rows;
            std::unordered_set<RowIdentity, RowIdentityHash> lookup;
            rows.reserve(committed_rows_lookup_.size());
            lookup.reserve(committed_rows_lookup_.size());
            for (const auto& entry : committed_listing_.entries)
            {
                if (!visibility_policy_.Includes(entry.attributes))
                {
                    continue;
                }
                auto row = MakeRowIdentity(entry);
                if (row.file_identity.valid && lookup.insert(row).second)
                {
                    rows.push_back(std::move(row));
                }
            }
            auto focus = rows.empty() ?
                std::nullopt : std::optional<RowIdentity>(rows.front());
            selection_.swap(rows);
            selection_lookup_.swap(lookup);
            focused_item_.swap(focus);
            return true;
        }
        catch (...)
        {
            // Failure is reported by the command surface; keep prior selection.
            return false;
        }
    }

    void TabState::SetSelection(std::vector<blink::core::FileIdentity> identities) noexcept
    {
        std::vector<RowIdentity> rows;
        rows.reserve(identities.size());
        for (const auto& identity : identities)
        {
            if (!identity.valid)
            {
                continue;
            }
            const auto entry = std::find_if(
                committed_listing_.entries.begin(),
                committed_listing_.entries.end(),
                [this, &identity](const auto& candidate) {
                    return candidate.identity.valid && candidate.identity == identity &&
                        visibility_policy_.Includes(candidate.attributes);
                });
            if (entry != committed_listing_.entries.end())
            {
                rows.push_back(MakeRowIdentity(*entry));
            }
        }
        SetRowSelection(std::move(rows));
    }

    void TabState::ClearSelection() noexcept
    {
        selection_.clear();
        selection_lookup_.clear();
        focused_item_.reset();
    }

    bool TabState::IsSelected(const blink::core::FileIdentity& identity) const noexcept
    {
        if (!identity.valid)
        {
            return false;
        }
        return std::any_of(selection_.begin(), selection_.end(), [&identity](const auto& row) {
            return row.file_identity == identity;
        });
    }

    bool TabState::IsRowSelected(const RowIdentity& row) const noexcept
    {
        if (!row.file_identity.valid)
        {
            return false;
        }
        return selection_lookup_.contains(row);
    }

    std::optional<blink::core::FileEntry> TabState::SelectedCommittedRow(
        const RowIdentity& row) const
    {
        if (!IsRowSelected(row))
        {
            return std::nullopt;
        }
        return CommittedRow(row);
    }

    std::optional<std::vector<blink::core::FileEntry>> TabState::SelectedCommittedRows(
        const std::size_t maximum) const
    {
        if (selection_.empty() || selection_.size() > maximum) { return {}; }
        std::vector<blink::core::FileEntry> entries;
        entries.reserve(selection_.size());
        for (const auto& entry : committed_listing_.entries)
        {
            if (visibility_policy_.Includes(entry.attributes) && IsRowSelected(MakeRowIdentity(entry)))
            { entries.push_back(entry); }
        }
        if (entries.size() != selection_.size()) { return {}; }
        return entries;
    }

    std::optional<blink::core::FileEntry> TabState::CommittedRow(
        const RowIdentity& row) const
    {
        if (!IsVisibleRow(row))
        {
            return std::nullopt;
        }
        const auto entry = std::find_if(
            committed_listing_.entries.begin(),
            committed_listing_.entries.end(),
            [&row](const blink::core::FileEntry& candidate) {
                return MakeRowIdentity(candidate) == row;
            });
        if (entry == committed_listing_.entries.end())
        {
            return std::nullopt;
        }
        return *entry;
    }

    std::optional<blink::core::FileEntry> TabState::SelectedCommittedEntry(
        const blink::core::FileIdentity& identity) const
    {
        if (!IsSelected(identity))
        {
            return std::nullopt;
        }
        const auto entry = std::find_if(
            committed_listing_.entries.begin(),
            committed_listing_.entries.end(),
            [this, &identity](const blink::core::FileEntry& candidate) {
                return candidate.identity.valid && candidate.identity == identity &&
                    visibility_policy_.Includes(candidate.attributes);
            });
        if (entry == committed_listing_.entries.end())
        {
            return std::nullopt;
        }
        return *entry;
    }

    void TabState::SetSortDescriptor(const SortDescriptor descriptor) noexcept
    {
        sort_ = descriptor;
    }

    SortDescriptor TabState::GetSortDescriptor() const noexcept
    {
        return sort_;
    }

    void TabState::SetViewMode(const FolderViewMode mode) noexcept
    {
        view_mode_ = mode;
    }

    FolderViewMode TabState::GetViewMode() const noexcept
    {
        return view_mode_;
    }

    std::optional<NavigationRequest> TabState::Begin(
        std::optional<NavigationRequest> request)
    {
        if (!request.has_value())
        {
            return std::nullopt;
        }
        return std::move(request);
    }

    bool TabState::ContainsIdentity(
        const std::vector<blink::core::FileEntry>& entries,
        const blink::core::FileIdentity& identity) noexcept
    {
        if (!identity.valid)
        {
            return false;
        }
        return std::any_of(entries.begin(), entries.end(), [&identity](const auto& entry) {
            return entry.identity.valid && entry.identity == identity;
        });
    }

    bool TabState::ContainsRowIdentity(
        const std::vector<blink::core::FileEntry>& entries,
        const RowIdentity& row) noexcept
    {
        if (!row.file_identity.valid)
        {
            return false;
        }
        return std::any_of(entries.begin(), entries.end(), [&row](const auto& entry) {
            return MakeRowIdentity(entry) == row;
        });
    }

    void TabState::SetVisibilityPolicy(const ItemVisibilityPolicy policy) noexcept
    {
        if (visibility_policy_ == policy)
        {
            return;
        }
        visibility_policy_ = policy;
        visible_item_count_ = 0;
        for (const auto& entry : committed_listing_.entries)
        {
            visible_item_count_ += policy.Includes(entry.attributes) ? 1 : 0;
        }
        std::erase_if(selection_, [this](const RowIdentity& row) {
            if (IsVisibleRow(row))
            {
                return false;
            }
            selection_lookup_.erase(row);
            return true;
        });
        if (focused_item_ && !IsVisibleRow(*focused_item_))
        {
            focused_item_.reset();
        }
        if (folder_state_ != FolderState::Idle)
        {
            folder_state_ = visible_item_count_ == 0 ? FolderState::Empty : FolderState::Ready;
        }
    }

    ItemVisibilityPolicy TabState::GetVisibilityPolicy() const noexcept
    {
        return visibility_policy_;
    }

    bool TabState::IsVisibleRow(const RowIdentity& row) const noexcept
    {
        const auto found = committed_rows_lookup_.find(row);
        return row.file_identity.valid && found != committed_rows_lookup_.end() &&
            visibility_policy_.Includes(found->second);
    }
}
