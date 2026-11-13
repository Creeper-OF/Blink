#pragma once
#include "Blink/App/FolderCommandPolicy.h"
#include "Blink/App/SearchContextCommands.h"
#include "Blink/App/SearchViewModel.h"
#include "Blink/App/Localization.h"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <functional>
#include <memory>

namespace blink::app
{
    struct SearchPresentationSettings
    {
        SearchScope scope{ SearchScope::FollowLocation };
        SearchMode mode{ SearchMode::Substring };
        std::wstring selected_volumes, selected_folder;
        bool include_subfolders{ true };
    };
    struct SearchPresentationOptions
    {
        std::uint64_t tab_id{};
        bool queued{};
        bool current_folder_allowed{};
        SearchScope scope{ SearchScope::FollowLocation };
        SearchMode mode{ SearchMode::Substring };
        ItemVisibilityPolicy visibility;
        std::optional<SearchResult> selection;
        DWORD action_error{ ERROR_SUCCESS };
        bool action_pending{};
        std::wstring selected_volumes, selected_folder, effective_scope;
        bool include_subfolders{ true }, scope_draft{};
    };
    struct SearchPresentationCallbacks
    {
        std::function<void()> submit, retry, leave;
        std::function<void(SearchPresentationSettings)> options_changed;
        std::function<void()> use_current_folder;
        std::function<void(bool)> visibility_changed;
        std::function<void(std::optional<SearchResult>)> selection_changed;
        std::function<void(SearchResult, bool)> activate;
        // Position is relative to the XamlRoot in logical client pixels. The
        // window owner applies rasterization scale and converts to screen space.
        std::function<void(SearchContextSelection, winrt::Windows::Foundation::Point)> context_menu;
        std::function<void(SearchContextSelection, FolderCommand)> command;
        std::function<void(std::uint64_t, std::uint64_t)> dismiss_context_menu;
        // Fixed for the Build lifetime; a fast UI-thread-only lookup without I/O.
        // It is not invoked while suspended or after Close.
        std::function<bool(const SearchResult&)> is_cut;
    };
    class SearchPresentation
    {
    public:
        winrt::Microsoft::UI::Xaml::Controls::Grid Build(const Localization& localization,
            const winrt::Microsoft::UI::Dispatching::DispatcherQueue& dispatcher,
            SearchPresentationCallbacks callbacks);
        void Update(const SearchSnapshot& snapshot, const SearchPresentationOptions& options, const Localization& localization);
        // Refresh only the already appended row properties after cut state changes.
        // Does not replace items, selection or the pending Append generation.
        void RefreshCutAppearance();
        [[nodiscard]] std::optional<SearchPresentationSettings> DraftSettings(std::uint64_t tab_id) const;
        void FocusResults();
        void Close() noexcept;
        void Suspend() noexcept;
    private:
        struct Ui;
        std::shared_ptr<Ui> ui_;
    };
}
