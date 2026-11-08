#pragma once

#include "Blink/App/BrowseController.h"
#include "Blink/App/FileActivationController.h"
#include "Blink/App/FolderChangeState.h"
#include "Blink/App/FolderWatcher.h"
#include "Blink/App/SearchCoordinator.h"
#include "Blink/App/TabState.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace blink::app
{
    using TabId = std::uint64_t;
    struct RelocatedTabNavigation { TabId tab_id{}; NavigationRequest request; };
    struct SuspendedFolderWatches
    {
        std::vector<TabId> tabs;
        std::vector<winrt::Windows::Foundation::IAsyncAction> completion;
    };

    enum class CloseTabStatus
    {
        Closed,
        LastTabWouldCloseWindow,
        NotFound,
    };

    // Window-owned tab collection. This is the authoritative app-private tab
    // store; a WinUI TabView only presents its ordered TabIds.
    struct TabRecord
    {
        TabId id{};
        std::unique_ptr<TabState> state;
        std::shared_ptr<BrowseState> browse_state;
        std::shared_ptr<BrowseController> browse_controller;
        std::shared_ptr<FileActivationController> file_activation_controller;
        std::unique_ptr<FolderWatcher> folder_watcher;
        FolderChangeState folder_changes;
        DWORD folder_watch_error{ ERROR_SUCCESS };
        std::shared_ptr<SearchState> search_state;
        std::unique_ptr<SearchCoordinator> search_controller;
        std::shared_ptr<const SearchSnapshot> search_snapshot;
        bool search_visible{};
        std::uint64_t file_operation_generation{ 1 };
        bool search_queued{};
        std::wstring search_draft;
        SearchScope search_scope{ SearchScope::FollowLocation };
        SearchMode search_mode{ SearchMode::Substring };
        std::wstring search_selected_volumes;
        std::wstring search_selected_folder;
        bool search_include_subfolders{ true };
        std::optional<SearchResult> search_selection;
        std::uint64_t search_action_generation{};
        bool search_action_pending{};
        DWORD search_action_error{ ERROR_SUCCESS };
        winrt::Windows::Foundation::IAsyncAction search_action_work{ nullptr };
        std::optional<RowIdentity> locate_after_browse;
        std::uint64_t locate_navigation_token{};
        std::optional<blink::core::FileIdentity> search_navigation_identity;

        void EnterSearch() noexcept;
        void LeaveSearch() noexcept;
        void OnNavigation(bool automatic_refresh) noexcept;
        [[nodiscard]] SearchQuery SearchIntent(bool reuse_committed) const;
        [[nodiscard]] bool FinishLocate(std::uint64_t navigation_token);
    };

    class TabWindowState
    {
    public:
        TabWindowState();
        ~TabWindowState() noexcept;

        TabWindowState(const TabWindowState&) = delete;
        TabWindowState& operator=(const TabWindowState&) = delete;

        [[nodiscard]] TabId CreateTab();
        [[nodiscard]] CloseTabStatus CloseTab(TabId id) noexcept;
        [[nodiscard]] bool ActivateTab(TabId id) noexcept;

        [[nodiscard]] TabId ActiveTabId() const noexcept;
        [[nodiscard]] TabRecord* ActiveTab() noexcept;
        [[nodiscard]] const TabRecord* ActiveTab() const noexcept;
        [[nodiscard]] TabRecord* Find(TabId id) noexcept;
        [[nodiscard]] const TabRecord* Find(TabId id) const noexcept;
        [[nodiscard]] std::vector<TabId> TabIds() const;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] std::vector<RelocatedTabNavigation> RelocateFolder(const Location& source,
            const Location& destination, const blink::core::FileIdentity& source_identity, bool preserve_suffix = true);
        [[nodiscard]] SuspendedFolderWatches SuspendFolderWatches(const std::vector<Location>& sources);

        void SetVisibilityPolicy(ItemVisibilityPolicy policy) noexcept;
        [[nodiscard]] ItemVisibilityPolicy GetVisibilityPolicy() const noexcept;

        void Shutdown() noexcept;

    private:
        [[nodiscard]] static std::unique_ptr<TabRecord> MakeTab(TabId id);

        std::vector<std::unique_ptr<TabRecord>> tabs_;
        TabId active_tab_id_{};
        TabId next_tab_id_{ 1 };
        bool shutdown_{};
        ItemVisibilityPolicy visibility_policy_;
    };
}
