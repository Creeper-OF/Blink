#include "Blink/App/TabWindowState.h"

#include <algorithm>

namespace blink::app
{
    void TabRecord::EnterSearch() noexcept
    { ++file_operation_generation; search_visible = true; ++search_action_generation; search_action_pending = false; search_action_error = ERROR_SUCCESS; locate_after_browse.reset(); }
    void TabRecord::LeaveSearch() noexcept
    {
        ++file_operation_generation;
        search_visible = false; search_queued = false; ++search_action_generation;
        search_selection.reset(); locate_after_browse.reset();
        search_navigation_identity.reset();
        search_action_pending = false; search_action_error = ERROR_SUCCESS;
        if (search_controller) { search_controller->Cancel(); }
    }
    void TabRecord::OnNavigation(bool automatic_refresh) noexcept
    { if (!automatic_refresh) { LeaveSearch(); } }
    SearchQuery TabRecord::SearchIntent(bool reuse_committed) const
    {
        if (reuse_committed)
        {
            if (search_snapshot) { return search_snapshot->query; }
            return search_state->Snapshot().query;
        }
        SearchQuery query{ search_draft };
        query.scope = search_scope; query.mode = search_mode;
        query.selected_volumes = search_selected_volumes;
        query.include_subfolders = search_include_subfolders;
        const auto location = state->PresentationSnapshot().committed_location;
        if (query.scope == SearchScope::FollowLocation)
        { query.scope = location.IsFolder() ? SearchScope::CurrentVolume : SearchScope::AllLocalVolumes; }
        if (query.scope == SearchScope::CurrentVolume && location.IsFolder())
        { query.folder = SearchDriveRoot(location.FolderPath()); }
        else if (query.scope == SearchScope::CurrentFolder && location.IsFolder())
        { query.folder = location.FolderPath(); }
        else if (query.scope == SearchScope::SelectedFolder) { query.folder = search_selected_folder; }
        return query;
    }
    bool TabRecord::FinishLocate(std::uint64_t token)
    {
        if (!locate_after_browse || locate_navigation_token != token) { return false; }
        const auto row = std::move(*locate_after_browse); locate_after_browse.reset();
        if (!state->CommittedRow(row))
        { state->SetRowSelection({}); search_action_error = ERROR_FILE_INVALID; return false; }
        state->SetRowSelection({ row }); search_action_error = ERROR_SUCCESS; return true;
    }
    TabWindowState::TabWindowState()
    {
        active_tab_id_ = CreateTab();
    }

    std::vector<RelocatedTabNavigation> TabWindowState::RelocateFolder(const Location& source,
        const Location& destination, const blink::core::FileIdentity& source_identity, const bool preserve_suffix)
    {
        std::vector<RelocatedTabNavigation> requests;
        requests.reserve(tabs_.size());
        for (auto& tab : tabs_)
        {
            const auto current = tab->state->PresentationSnapshot().committed_location;
            const auto identity = tab->state->CommittedDirectoryIdentity();
            if (current == source && identity.valid && source_identity.valid && identity != source_identity) { continue; }
            if (auto request = tab->state->RelocateFolder(source, destination, preserve_suffix))
            { requests.push_back({ tab->id, std::move(*request) }); }
        }
        return requests;
    }

    SuspendedFolderWatches TabWindowState::SuspendFolderWatches(const std::vector<Location>& sources)
    {
        SuspendedFolderWatches stopped; stopped.tabs.reserve(tabs_.size()); stopped.completion.reserve(tabs_.size());
        std::vector<TabRecord*> selected; selected.reserve(tabs_.size());
        for (auto& tab : tabs_)
        {
            const auto page = tab->state->PresentationSnapshot();
            const auto location = !tab->folder_changes.Token().path.empty() ? Location::Folder(tab->folder_changes.Token().path) :
                page.pending_location.value_or(page.committed_location);
            if (!std::any_of(sources.begin(), sources.end(), [&](const auto& source) { return IsFolderAtOrBelow(location, source); })) { continue; }
            selected.push_back(tab.get());
        }
        for (auto* tab : selected)
        {
            stopped.tabs.push_back(tab->id); tab->folder_changes.Stop(); tab->folder_watch_error = ERROR_SUCCESS;
            if (auto completion = tab->folder_watcher->Stop()) { stopped.completion.push_back(std::move(completion)); }
        }
        return stopped;
    }

    TabWindowState::~TabWindowState() noexcept
    {
        Shutdown();
    }

    std::unique_ptr<TabRecord> TabWindowState::MakeTab(const TabId id)
    {
        auto record = std::make_unique<TabRecord>();
        record->id = id;
        record->state = std::make_unique<TabState>();
        record->browse_state = std::make_shared<BrowseState>();
        record->browse_controller = std::make_shared<BrowseController>(record->browse_state);
        record->file_activation_controller = std::make_shared<FileActivationController>();
        record->folder_watcher = std::make_unique<FolderWatcher>();
        record->search_state = std::make_shared<SearchState>();
        record->search_controller = std::make_unique<SearchCoordinator>(record->search_state);
        return record;
    }

    TabId TabWindowState::CreateTab()
    {
        const TabId id = next_tab_id_++;
        tabs_.push_back(MakeTab(id));
        tabs_.back()->state->SetVisibilityPolicy(visibility_policy_);
        if (active_tab_id_ == 0)
        {
            active_tab_id_ = id;
        }
        return id;
    }

    CloseTabStatus TabWindowState::CloseTab(const TabId id) noexcept
    {
        const auto iterator = std::find_if(
            tabs_.begin(),
            tabs_.end(),
            [id](const auto& record) { return record && record->id == id; });
        if (iterator == tabs_.end())
        {
            return CloseTabStatus::NotFound;
        }
        if (tabs_.size() == 1)
        {
            return CloseTabStatus::LastTabWouldCloseWindow;
        }

        const std::size_t index = static_cast<std::size_t>(
            std::distance(tabs_.begin(), iterator));
        const bool was_active = active_tab_id_ == id;
        if (was_active)
        {
            const std::size_t survivor_index = index + 1 < tabs_.size() ?
                index + 1 : index - 1;
            active_tab_id_ = tabs_[survivor_index]->id;
        }

        auto& record = *iterator;
        record->LeaveSearch();
        if (record->search_controller) { (void)record->search_controller->Shutdown(); }
        record->folder_changes.Stop();
        (void)record->folder_watcher->Stop();
        record->file_activation_controller->Shutdown();
        if (record->browse_controller)
        {
            record->browse_controller->Shutdown();
        }
        if (record->state)
        {
            record->state->Shutdown();
        }
        tabs_.erase(iterator);
        return CloseTabStatus::Closed;
    }

    bool TabWindowState::ActivateTab(const TabId id) noexcept
    {
        if (!Find(id))
        {
            return false;
        }
        if (id != active_tab_id_)
        { if (auto* previous = ActiveTab()) { ++previous->file_operation_generation; } }
        active_tab_id_ = id;
        return true;
    }

    TabId TabWindowState::ActiveTabId() const noexcept
    {
        return active_tab_id_;
    }

    TabRecord* TabWindowState::ActiveTab() noexcept
    {
        return Find(active_tab_id_);
    }

    const TabRecord* TabWindowState::ActiveTab() const noexcept
    {
        return Find(active_tab_id_);
    }

    TabRecord* TabWindowState::Find(const TabId id) noexcept
    {
        const auto iterator = std::find_if(
            tabs_.begin(),
            tabs_.end(),
            [id](const auto& record) { return record && record->id == id; });
        return iterator == tabs_.end() ? nullptr : iterator->get();
    }

    const TabRecord* TabWindowState::Find(const TabId id) const noexcept
    {
        const auto iterator = std::find_if(
            tabs_.begin(),
            tabs_.end(),
            [id](const auto& record) { return record && record->id == id; });
        return iterator == tabs_.end() ? nullptr : iterator->get();
    }

    std::vector<TabId> TabWindowState::TabIds() const
    {
        std::vector<TabId> ids;
        ids.reserve(tabs_.size());
        for (const auto& record : tabs_)
        {
            ids.push_back(record->id);
        }
        return ids;
    }

    std::size_t TabWindowState::Size() const noexcept
    {
        return tabs_.size();
    }

    void TabWindowState::SetVisibilityPolicy(const ItemVisibilityPolicy policy) noexcept
    {
        visibility_policy_ = policy;
        for (auto& record : tabs_)
        {
            record->LeaveSearch();
            record->state->SetVisibilityPolicy(policy);
        }
    }

    ItemVisibilityPolicy TabWindowState::GetVisibilityPolicy() const noexcept
    {
        return visibility_policy_;
    }

    void TabWindowState::Shutdown() noexcept
    {
        if (shutdown_)
        {
            return;
        }
        shutdown_ = true;
        for (auto& record : tabs_)
        {
            ++record->file_operation_generation;
            record->folder_changes.Stop();
            (void)record->folder_watcher->Stop();
            record->file_activation_controller->Shutdown();
            if (record->browse_controller)
            {
                record->browse_controller->Shutdown();
            }
            if (record->state)
            {
                record->state->Shutdown();
            }
        }
    }
}
