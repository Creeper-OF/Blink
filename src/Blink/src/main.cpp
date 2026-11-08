#include "App.xaml.h"

#include "Blink/Core/FileTypes.h"
#include "Blink/App/BrowseController.h"
#include "Blink/App/FileActivationController.h"
#include "Blink/App/FileOperationCoordinator.h"
#include "Blink/App/FileOperationOrigin.h"
#include "Blink/App/CopyTransferPresentation.h"
#include "Blink/App/ClipboardFileTransfer.h"
#include "Blink/App/MoveTransferPresentation.h"
#include "Blink/App/FileOperationRelocation.h"
#include "Blink/App/DeletePresentation.h"
#include "Blink/App/DeleteConfirmationDialog.h"
#include "Blink/App/PropertiesDialog.h"
#include "Blink/App/FileDragDropController.h"
#include "Blink/App/FileCopyConflictDialog.h"
#include "Blink/App/FolderNameDialog.h"
#include "Blink/App/FolderCommandPolicy.h"
#include "Blink/App/IndexServiceCoordinator.h"
#include "Blink/App/IndexServicePresentation.h"
#include "Blink/App/SearchPresentation.h"
#include "Blink/App/SearchContextMenuPresentation.h"
#include "Blink/App/ContextMenuAppearance.h"
#include "Blink/App/SearchRequestQueue.h"
#include "Blink/App/SearchResultValidation.h"
#include "Blink/App/ShellContextMenuController.h"
#include "Blink/App/FolderViewFormatting.h"
#include "Blink/App/FolderViewPresentation.h"
#include "Blink/App/Localization.h"
#include "Blink/App/Location.h"
#include "Blink/App/NavigationModel.h"
#include "Blink/App/NavigationTree.h"
#include "Blink/App/TabState.h"
#include "Blink/App/TabWindowState.h"

#include <Windows.h>
#include <CommCtrl.h>
#include <microsoft.ui.xaml.window.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.Threading.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/base.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "windowsapp")
#pragma comment(lib, "comctl32")

using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Dispatching;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;

namespace
{
    enum class UiProbeMode
    {
        None,
        NoFolderList,
        EmptyDefaultList,
        DirectItemsString,
        ItemsSourceString,
        ItemsSourceStringExplicitPanel,
        ItemsSourceStringConstantTemplate,
        CurrentMinimalPropertySet,
    };

    [[nodiscard]] UiProbeMode ReadUiProbeMode() noexcept
    {
        wchar_t value[16]{};
        const DWORD length = GetEnvironmentVariableW(
            L"BLINK_PHASE1A_UI_PROBE",
            value,
            static_cast<DWORD>(std::size(value)));
        if (length == 0 || length >= std::size(value))
        {
            return UiProbeMode::None;
        }

        const wchar_t probe = value[0] == L'P' ? value[1] : value[0];
        switch (probe)
        {
        case L'0':
            return UiProbeMode::NoFolderList;
        case L'1':
            return UiProbeMode::EmptyDefaultList;
        case L'2':
            return UiProbeMode::DirectItemsString;
        case L'3':
            return UiProbeMode::ItemsSourceString;
        case L'4':
            return UiProbeMode::ItemsSourceStringExplicitPanel;
        case L'5':
            return UiProbeMode::ItemsSourceStringConstantTemplate;
        case L'6':
            return UiProbeMode::CurrentMinimalPropertySet;
        default:
            return UiProbeMode::None;
        }
    }

    [[nodiscard]] const char* UiProbeModeName(const UiProbeMode mode) noexcept
    {
        switch (mode)
        {
        case UiProbeMode::NoFolderList:
            return "P0-no-folder-list";
        case UiProbeMode::EmptyDefaultList:
            return "P1-empty-default-list";
        case UiProbeMode::DirectItemsString:
            return "P2-direct-items-string";
        case UiProbeMode::ItemsSourceString:
            return "P3-itemssource-string";
        case UiProbeMode::ItemsSourceStringExplicitPanel:
            return "P4-itemssource-string-explicit-panel";
        case UiProbeMode::ItemsSourceStringConstantTemplate:
            return "P5-itemssource-string-constant-template";
        case UiProbeMode::CurrentMinimalPropertySet:
            return "P6-current-minimal-propertyset";
        default:
            return "none";
        }
    }

    using BrowseClock = std::chrono::steady_clock;

    [[nodiscard]] double ElapsedMilliseconds(
        const BrowseClock::time_point start,
        const BrowseClock::time_point end) noexcept
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    struct BrowseTimingRecord
    {
        std::wstring path;
        std::uint64_t generation{};
        std::size_t entry_count{};
        double navigation_submission_ms{};
        double background_enumeration_ms{};
        double background_total_ms{};
        double result_publication_ms{};
        double commit_browse_ms{};
        double committed_entries_snapshot_ms{};
        double sorting_ms{};
        double projection_ms{};
        double property_set_ms{};
        double observable_vector_ms{};
        double selection_restore_ms{};
        double list_view_completion_ms{};
        double restore_navigation_ui_ms{};
        double navigation_buttons_ms{};
        double browse_status_ms{};
        double ui_work_total_ms{};
        double presentation_total_ms{};
        double longest_ui_block_ms{};
        double first_usable_content_ms{};
        double navigation_to_first_row_ms{};
        double navigation_to_full_presentation_ms{};
        double t1_background_start_ms{};
        double t2_enumeration_complete_ms{};
        double t3_sort_start_ms{};
        double t4_sort_complete_ms{};
        double t5_first_projection_start_ms{};
        double t6_first_projection_batch_ready_ms{};
        double t7_first_ui_publish_start_ms{};
        double t8_first_row_available_ms{};
        double t9_full_presentation_complete_ms{};
        bool presentation_batched{};
        const char* longest_ui_stage{ "none" };
        BrowseClock::time_point navigation_accepted_at{};
        BrowseClock::time_point ui_begin{};
    };

    void FinalizeBrowseTiming(BrowseTimingRecord& timing) noexcept
    {
        timing.longest_ui_stage = "none";
        double longest = 0.0;
        const auto consider = [&timing, &longest](const char* name, const double value) noexcept {
            if (value > longest)
            {
                longest = value;
                timing.longest_ui_stage = name;
            }
        };

        consider("commit_browse", timing.commit_browse_ms);
        consider("committed_entries_snapshot", timing.committed_entries_snapshot_ms);
        consider("sorting", timing.sorting_ms);
        consider("projection", timing.projection_ms);
        consider("property_set", timing.property_set_ms);
        consider("observable_vector", timing.observable_vector_ms);
        consider("selection_restore", timing.selection_restore_ms);
        consider("list_view_completion", timing.list_view_completion_ms);
        consider("restore_navigation_ui", timing.restore_navigation_ui_ms);
        consider("navigation_buttons", timing.navigation_buttons_ms);
        consider("browse_status", timing.browse_status_ms);
    }

    [[nodiscard]] std::wstring ModuleDirectory()
    {
        std::wstring buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        buffer.resize(length);
        const std::size_t separator = buffer.find_last_of(L"\\/");
        return separator == std::wstring::npos ? L"." : buffer.substr(0, separator);
    }

    class StartupTelemetry
    {
    public:
        StartupTelemetry() noexcept
            : started_(std::chrono::steady_clock::now())
        {
            try
            {
                std::wstring configured_path(32768, L'\0');
                const DWORD length = GetEnvironmentVariableW(
                    L"BLINK_STARTUP_TELEMETRY",
                    configured_path.data(),
                    static_cast<DWORD>(configured_path.size()));
                if (length > 0 && length < configured_path.size())
                {
                    configured_path.resize(length);
                    if (configured_path == L"1" || configured_path == L"true" || configured_path == L"TRUE")
                    {
                        path_ = std::filesystem::current_path() /
                            L".blink-data" / L"phase1a-startup" / L"startup.log";
                    }
                    else if (configured_path != L"0" && configured_path != L"false" && configured_path != L"FALSE")
                    {
                        path_ = std::filesystem::path(configured_path);
                    }
                }

                if (!path_.empty())
                {
                    std::error_code error;
                    std::filesystem::create_directories(path_.parent_path(), error);
                    stream_.open(path_, std::ios::out | std::ios::app | std::ios::binary);
                    if (stream_.is_open())
                    {
                        stream_ << "encoding=utf-8\n";
                        stream_.flush();
                    }
                }
            }
            catch (...)
            {
                // Startup diagnostics must never change application behavior.
            }
        }

        void Stage(const std::string& stage) noexcept
        {
            Write("stage=" + stage);
        }

        void HresultFailure(const std::string& stage, const hresult_error& error) noexcept
        {
            HresultFailure(stage, error.code(), error.message());
        }

        void HresultFailure(
            const std::string& stage,
            const winrt::hresult code,
            const hstring& message) noexcept
        {
            std::ostringstream line;
            line << "stage=" << stage
                 << " failure=hresult"
                 << " hresult=0x" << std::uppercase << std::hex
                 << static_cast<std::uint32_t>(code)
                 << std::dec
                 << " message=" << OneLine(winrt::to_string(message));
            Write(line.str());
        }

        void MessageFailure(const std::string& stage, const hstring& message) noexcept
        {
            Write("stage=" + stage + " failure=message message=" +
                OneLine(winrt::to_string(message)));
        }

        void GenericFailure(const std::string& stage) noexcept
        {
            Write("stage=" + stage + " failure=generic");
        }

        void Browse(const BrowseTimingRecord& timing) noexcept
        {
            try
            {
                std::ostringstream line;
                line << "browse_timing={\"path\":\""
                     << JsonEscape(winrt::to_string(hstring(timing.path)))
                     << "\",\"generation\":" << timing.generation
                     << ",\"entry_count\":" << timing.entry_count
                     << ",\"navigation_submission_ms\":" << std::fixed << std::setprecision(3)
                     << timing.navigation_submission_ms
                     << ",\"background_enumeration_ms\":" << timing.background_enumeration_ms
                     << ",\"background_total_ms\":" << timing.background_total_ms
                     << ",\"result_publication_ms\":" << timing.result_publication_ms
                     << ",\"commit_browse_ms\":" << timing.commit_browse_ms
                     << ",\"committed_entries_snapshot_ms\":" << timing.committed_entries_snapshot_ms
                     << ",\"sorting_ms\":" << timing.sorting_ms
                     << ",\"projection_ms\":" << timing.projection_ms
                     << ",\"property_set_ms\":" << timing.property_set_ms
                     << ",\"observable_vector_ms\":" << timing.observable_vector_ms
                     << ",\"selection_restore_ms\":" << timing.selection_restore_ms
                     << ",\"list_view_completion_ms\":" << timing.list_view_completion_ms
                     << ",\"restore_navigation_ui_ms\":" << timing.restore_navigation_ui_ms
                     << ",\"navigation_buttons_ms\":" << timing.navigation_buttons_ms
                     << ",\"browse_status_ms\":" << timing.browse_status_ms
                     << ",\"background_thread_total_ms\":" << timing.background_total_ms
                     << ",\"ui_thread_total_ms\":" << timing.ui_work_total_ms
                     << ",\"presentation_total_ms\":" << timing.presentation_total_ms
                     << ",\"longest_ui_block_ms\":" << timing.longest_ui_block_ms
                     << ",\"first_usable_content_ms\":" << timing.first_usable_content_ms
                     << ",\"navigation_to_first_row_ms\":" << timing.navigation_to_first_row_ms
                     << ",\"navigation_to_full_presentation_ms\":" << timing.navigation_to_full_presentation_ms
                     << ",\"t1_background_start_ms\":" << timing.t1_background_start_ms
                     << ",\"t2_enumeration_complete_ms\":" << timing.t2_enumeration_complete_ms
                     << ",\"t3_sort_start_ms\":" << timing.t3_sort_start_ms
                     << ",\"t4_sort_complete_ms\":" << timing.t4_sort_complete_ms
                     << ",\"t5_first_projection_start_ms\":" << timing.t5_first_projection_start_ms
                     << ",\"t6_first_projection_batch_ready_ms\":" << timing.t6_first_projection_batch_ready_ms
                     << ",\"t7_first_ui_publish_start_ms\":" << timing.t7_first_ui_publish_start_ms
                     << ",\"t8_first_row_available_ms\":" << timing.t8_first_row_available_ms
                     << ",\"t9_full_presentation_complete_ms\":" << timing.t9_full_presentation_complete_ms
                     << ",\"presentation_batched\":" << (timing.presentation_batched ? "true" : "false")
                     << ",\"longest_ui_stage\":\"" << timing.longest_ui_stage
                     << "\"}";
                Write(line.str());
            }
            catch (...)
            {
                // Browse diagnostics must never change navigation behavior.
            }
        }

        [[nodiscard]] const std::filesystem::path& Path() const noexcept
        {
            return path_;
        }

    private:
        [[nodiscard]] static std::string JsonEscape(std::string value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char character : value)
            {
                switch (character)
                {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '"':
                    escaped += "\\\"";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    escaped += character;
                    break;
                }
            }
            return escaped;
        }

        [[nodiscard]] static std::string OneLine(std::string value)
        {
            std::replace(value.begin(), value.end(), '\r', ' ');
            std::replace(value.begin(), value.end(), '\n', ' ');
            return value;
        }

        void Write(const std::string& line) noexcept
        {
            try
            {
                const std::lock_guard lock(stream_mutex_);
                if (!stream_.is_open())
                {
                    return;
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started_).count();
                stream_ << "elapsed_ms=" << elapsed << " " << line << "\n";
                stream_.flush();
            }
            catch (...)
            {
                // Diagnostics are best effort and must not mask the real error.
            }
        }

        std::chrono::steady_clock::time_point started_;
        std::filesystem::path path_;
        std::ofstream stream_;
        std::mutex stream_mutex_;
    };

    [[nodiscard]] Button MakeIconButton(
        const blink::app::Localization& localization,
        const wchar_t* glyph,
        const std::wstring_view tooltip_key)
    {
        Button button;
        auto icon = FontIcon();
        icon.Glyph(glyph);
        icon.FontSize(16);
        button.Content(icon);
        ToolTipService::SetToolTip(
            button,
            winrt::box_value(hstring(localization.GetString(tooltip_key))));
        button.Width(36);
        button.Height(32);
        button.Margin(Thickness{ 2, 0, 2, 0 });
        return button;
    }

}

namespace winrt::Blink::implementation
{
struct App : AppT<App>
{
    App()
    {
        EnsureTabUi(tab_window_state_.ActiveTabId());
        startup_.Stage("app.initialize-component.begin");
        InitializeComponent();
        startup_.Stage("app.initialize-component.end");
        ui_probe_mode_ = ReadUiProbeMode();
        if (ui_probe_mode_ != UiProbeMode::None)
        {
            startup_.Stage(std::string("ui-probe.mode=") + UiProbeModeName(ui_probe_mode_));
        }
        startup_.Stage("App.construct");
    }

    void OnLaunched(LaunchActivatedEventArgs const&)
    {
        startup_.Stage("OnLaunched.begin");
        try
        {
            InstallStartupExceptionTelemetry();
            dispatcher_ = DispatcherQueue::GetForCurrentThread();
            startup_.Stage("dispatcher.committed");
            window_ = Window();
            startup_.Stage("window.committed");
            window_.AppWindow().Title(hstring(localization_.GetString(L"App.Title")));
            // Use WinUI's title bar and the Windows 11 window material from
            // the first frame, instead of the separate default white caption.
            window_.ExtendsContentIntoTitleBar(true);
            window_.SystemBackdrop(MicaBackdrop());
            startup_.Stage("window.title-set");
            const auto weak_app = get_weak();
            window_.Closed([weak_app](auto&&, auto&&) {
                if (auto app = weak_app.get())
                {
                    app->OnWindowClosed();
                }
            });
            startup_.Stage("window.closed-handler-set");
            startup_.Stage("BuildUi.begin");
            BuildUi();
            startup_.Stage("BuildUi.end");
            BuildWindowFrame();
            InitializeFileDragDrop();
            window_.Content(window_frame_);
            window_.SetTitleBar(title_bar_);
            SyncWindowFrameTheme();
            startup_.Stage("window.content-set");
            window_.Activate();
            InstallNavigationWheelInput();
            startup_.Stage("window.activated");
            startup_.Stage("initial-this-pc.begin");
            RefreshNavigation();
            startup_.Stage("initial-this-pc.end");
            StartIndexServiceStatus();
        }
        catch (const hresult_error& error)
        {
            startup_.HresultFailure("OnLaunched", error);
            throw;
        }
        catch (...)
        {
            startup_.GenericFailure("OnLaunched");
            throw;
        }
    }

    private:
    void InstallStartupExceptionTelemetry() noexcept
    {
        startup_.Stage("startup-exception-telemetry.begin");

        try
        {
            UnhandledException([this](IInspectable const&, UnhandledExceptionEventArgs const& args) noexcept {
                try
                {
                    startup_.HresultFailure(
                        "xaml.unhandled-exception",
                        args.Exception(),
                        args.Message());
                }
                catch (...)
                {
                    startup_.GenericFailure("xaml.unhandled-exception");
                }
            });
            startup_.Stage("startup-exception-telemetry.unhandled.ok");
        }
        catch (const hresult_error& error)
        {
            startup_.HresultFailure("startup-exception-telemetry.unhandled", error);
        }
        catch (...)
        {
            startup_.GenericFailure("startup-exception-telemetry.unhandled");
        }

        try
        {
            auto debug_settings = this->DebugSettings();
            debug_settings.IsBindingTracingEnabled(true);
            debug_settings.BindingFailed(
                [this](
                    IInspectable const&,
                    Microsoft::UI::Xaml::BindingFailedEventArgs const& args) noexcept {
                    try
                    {
                        startup_.MessageFailure("xaml.binding-failed", args.Message());
                    }
                    catch (...)
                    {
                        startup_.GenericFailure("xaml.binding-failed");
                    }
                });
            startup_.Stage("startup-exception-telemetry.binding.ok");
        }
        catch (const hresult_error& error)
        {
            startup_.HresultFailure("startup-exception-telemetry.binding", error);
        }
        catch (...)
        {
            startup_.GenericFailure("startup-exception-telemetry.binding");
        }
    }

    struct ProjectedPresentationBatch
    {
        std::vector<blink::app::FolderRowViewData> rows;
        bool final_batch{};
        double sort_ms{};
        double projection_ms{};
        BrowseClock::time_point sort_started_at{};
        BrowseClock::time_point sort_completed_at{};
        BrowseClock::time_point projection_started_at{};
        BrowseClock::time_point first_batch_ready_at{};
        BrowseClock::time_point projection_completed_at{};
    };

    struct NavigationTreeRevealStepResult
    {
        blink::app::Location parent;
        blink::app::Location desired;
        std::vector<blink::app::NavigationTreeChildSpec> children;
        DWORD error{ ERROR_SUCCESS };
    };

    struct QuickNavigationItem
    {
        std::wstring label;
        blink::app::Location target;

        friend bool operator==(const QuickNavigationItem&, const QuickNavigationItem&) = default;
    };

    struct PendingNavigationTreeExpansion
    {
        std::uint64_t generation{};
        std::uint32_t expanding_event_count{};
        bool user_intent_still_expand{};
    };

    struct TabUiState
    {
        blink::app::TabId id{};
        std::vector<blink::app::FolderRowViewData> row_models;
        std::vector<blink::app::ThisPcItem> this_pc_rows;
        std::vector<winrt::Windows::Foundation::Collections::PropertySet> row_items;
        winrt::Windows::Foundation::Collections::IObservableVector<IInspectable>
            row_items_source{ nullptr };
        bool showing_committed_folder{};
        bool showing_this_pc{};
        bool presentation_in_progress{};
        std::uint64_t presentation_generation{};
        std::size_t presentation_next_index{};
        std::size_t presentation_expected_count{};
        winrt::Windows::Foundation::IAsyncAction presentation_work{ nullptr };
        winrt::Windows::Foundation::IAsyncAction this_pc_work{ nullptr };
        std::uint64_t this_pc_generation{};
        DispatcherQueueTimer folder_change_timer{ nullptr };
        std::optional<blink::core::FileIdentity> operation_refresh_identity;
    };

    [[nodiscard]] TabUiState& EnsureTabUi(const blink::app::TabId id)
    {
        auto iterator = tab_ui_states_.find(id);
        if (iterator == tab_ui_states_.end())
        {
            auto state = std::make_unique<TabUiState>();
            state->id = id;
            iterator = tab_ui_states_.emplace(id, std::move(state)).first;
        }
        return *iterator->second;
    }

    [[nodiscard]] TabUiState* FindTabUi(const blink::app::TabId id) noexcept
    {
        const auto iterator = tab_ui_states_.find(id);
        return iterator == tab_ui_states_.end() ? nullptr : iterator->second.get();
    }

    [[nodiscard]] const TabUiState* FindTabUi(const blink::app::TabId id) const noexcept
    {
        const auto iterator = tab_ui_states_.find(id);
        return iterator == tab_ui_states_.end() ? nullptr : iterator->second.get();
    }

    [[nodiscard]] blink::app::TabRecord* FindTab(const blink::app::TabId id) noexcept
    {
        return tab_window_state_.Find(id);
    }

    [[nodiscard]] blink::app::TabRecord* ActiveTab() noexcept
    {
        return tab_window_state_.ActiveTab();
    }

    [[nodiscard]] const blink::app::TabRecord* ActiveTab() const noexcept
    {
        return tab_window_state_.ActiveTab();
    }

    [[nodiscard]] blink::app::TabState* ActiveTabState() noexcept
    {
        const auto tab = ActiveTab();
        return tab && tab->state ? tab->state.get() : nullptr;
    }

    [[nodiscard]] TabUiState* ActiveTabUi() noexcept
    {
        return FindTabUi(tab_window_state_.ActiveTabId());
    }

    [[nodiscard]] const TabUiState* ActiveTabUi() const noexcept
    {
        return FindTabUi(tab_window_state_.ActiveTabId());
    }

    [[nodiscard]] blink::app::TabState& StateFor(const blink::app::TabId id) noexcept
    {
        return *FindTab(id)->state;
    }

    [[nodiscard]] blink::app::TabState& ActiveState() noexcept
    {
        return *ActiveTab()->state;
    }

    [[nodiscard]] const blink::app::TabState& ActiveState() const noexcept
    {
        return *ActiveTab()->state;
    }

    [[nodiscard]] std::shared_ptr<blink::app::BrowseState> BrowseStateFor(
        const blink::app::TabId id) const noexcept
    {
        const auto tab = tab_window_state_.Find(id);
        return tab ? tab->browse_state : nullptr;
    }

    [[nodiscard]] std::shared_ptr<blink::app::BrowseController> BrowseControllerFor(
        const blink::app::TabId id) const noexcept
    {
        const auto tab = tab_window_state_.Find(id);
        return tab ? tab->browse_controller : nullptr;
    }

    void BuildWindowFrame()
    {
        AddGridRow(window_frame_, 1, GridUnitType::Auto);
        AddGridRow(window_frame_, 1, GridUnitType::Star);

        title_bar_ = TitleBar();
        // Host the tab strip in the native caption row. TitleBar manages its
        // interactive region and reserves a drag area beside the caption buttons.
        // Let TabView request only its occupied width; stretching it would also
        // mark the unused strip after the add button as interactive, blocking drag.
        title_bar_.Height(48);
        title_bar_.Resources().Insert(box_value(L"TitleBarContentHorizontalAlignment"),
            box_value(HorizontalAlignment::Left));
        title_bar_.AutoRefreshDragRegions(true);
        title_bar_.Content(tab_view_);
        window_.AppWindow().TitleBar().PreferredHeightOption(
            Microsoft::UI::Windowing::TitleBarHeightOption::Tall);
        title_bar_.Title(hstring(localization_.GetString(L"App.Title")));
        auto icon = FontIconSource();
        icon.Glyph(L"\xE8B7");
        title_bar_.IconSource(icon);
        title_bar_.Background(SolidColorBrush(Windows::UI::Color{ 0, 0, 0, 0 }));
        Automation::AutomationProperties::SetAutomationId(title_bar_, L"BlinkTitleBar");
        window_frame_.Children().Append(title_bar_);
        Grid::SetRow(root_, 1);
        window_frame_.Children().Append(root_);

        const auto weak_app = get_weak();
        window_frame_.Loaded([weak_app](auto&&, auto&&) {
            if (auto app = weak_app.get())
            {
                app->SyncWindowFrameTheme();
            }
        });
        window_frame_.ActualThemeChanged([weak_app](auto&&, auto&&) {
            if (auto app = weak_app.get())
            {
                app->SyncWindowFrameTheme();
            }
        });
    }

    void SyncWindowFrameTheme()
    {
        if (window_style_closed_ || !window_frame_)
        {
            return;
        }
        // TitleBar reserves the native caption buttons and manages drag
        // regions/insets/DPI. Keep their palette in the same theme as XAML.
        const auto title_bar = window_.AppWindow().TitleBar();
        title_bar.PreferredTheme(window_frame_.ActualTheme() == ElementTheme::Dark ?
            Microsoft::UI::Windowing::TitleBarTheme::Dark :
            Microsoft::UI::Windowing::TitleBarTheme::Light);
        const Windows::UI::Color transparent{ 0, 0, 0, 0 };
        title_bar.ButtonBackgroundColor(transparent);
        title_bar.ButtonInactiveBackgroundColor(transparent);
    }

    void BuildUi()
    {
        startup_.Stage("BuildUi.root.begin");
        root_ = Grid();
        // Window-wide shortcuts must also work when focus is in the titlebar tabs.
        window_frame_ = Grid();
        root_.Padding(Thickness{ 12, 10, 12, 8 });
        AddGridRow(root_, 48, GridUnitType::Pixel);
        AddGridRow(root_, 1, GridUnitType::Star);
        AddGridRow(root_, 30, GridUnitType::Pixel);
        AddGridRow(root_, 1, GridUnitType::Auto);
        startup_.Stage("BuildUi.root");

        tab_view_ = TabView();
        tab_view_.Height(48);
        tab_view_.VerticalAlignment(VerticalAlignment::Center);
        Automation::AutomationProperties::SetAutomationId(tab_view_, L"WindowTabs");
        tab_view_.IsAddTabButtonVisible(true);
        tab_view_.SelectionChanged([this](auto&&, auto&&) {
            OnTabViewSelectionChanged();
        });
        tab_view_.TabCloseRequested([this](auto&&, auto&& args) {
            OnTabCloseRequested(args);
        });
        tab_view_.AddTabButtonClick([this](auto&&, auto&&) {
            CreateAndActivateTab();
        });
        auto new_tab_accelerator = KeyboardAccelerator();
        new_tab_accelerator.Key(Windows::System::VirtualKey::T);
        new_tab_accelerator.Modifiers(Windows::System::VirtualKeyModifiers::Control);
        new_tab_accelerator.Invoked([this](auto&&, auto&& args) {
            if (!IsOperationDialogOpen()) { CreateAndActivateTab(); }
            args.Handled(true);
        });
        window_frame_.KeyboardAccelerators().Append(new_tab_accelerator);

        auto close_tab_accelerator = KeyboardAccelerator();
        close_tab_accelerator.Key(Windows::System::VirtualKey::W);
        close_tab_accelerator.Modifiers(Windows::System::VirtualKeyModifiers::Control);
        close_tab_accelerator.Invoked([this](auto&&, auto&& args) {
            if (!IsOperationDialogOpen()) { CloseTab(tab_window_state_.ActiveTabId()); }
            args.Handled(true);
        });
        window_frame_.KeyboardAccelerators().Append(close_tab_accelerator);

        AddAccelerator(window_frame_, Windows::System::VirtualKey::L,
            Windows::System::VirtualKeyModifiers::Control, [this] {
                path_box_.Focus(FocusState::Keyboard);
                path_box_.SelectAll();
            });
        AddAccelerator(window_frame_, Windows::System::VirtualKey::F5,
            Windows::System::VirtualKeyModifiers::None, [this] { RefreshNavigation(); });
        for (const auto key : { Windows::System::VirtualKey::F, Windows::System::VirtualKey::E })
        {
            AddAccelerator(window_frame_, key, Windows::System::VirtualKeyModifiers::Control, [this] {
                search_box_.Focus(FocusState::Keyboard); search_box_.SelectAll();
            });
        }
        AddAccelerator(window_frame_, Windows::System::VirtualKey::Left,
            Windows::System::VirtualKeyModifiers::Menu, [this] { NavigateBack(); });
        AddAccelerator(window_frame_, Windows::System::VirtualKey::Right,
            Windows::System::VirtualKeyModifiers::Menu, [this] { NavigateForward(); });
        AddAccelerator(window_frame_, Windows::System::VirtualKey::Up,
            Windows::System::VirtualKeyModifiers::Menu, [this] { NavigateUp(); });

        auto navigation_bar = Grid();
        AddGridColumn(navigation_bar, 40, GridUnitType::Pixel);
        AddGridColumn(navigation_bar, 40, GridUnitType::Pixel);
        AddGridColumn(navigation_bar, 40, GridUnitType::Pixel);
        AddGridColumn(navigation_bar, 40, GridUnitType::Pixel);
        AddGridColumn(navigation_bar, 1, GridUnitType::Star);
        AddGridColumn(navigation_bar, 220, GridUnitType::Pixel);

        back_button_ = MakeIconButton(localization_, L"\xE72B", L"Toolbar.Back");
        back_button_.Click([this](IInspectable const&, RoutedEventArgs const&) {
            NavigateBack();
        });
        forward_button_ = MakeIconButton(localization_, L"\xE72A", L"Toolbar.Forward");
        forward_button_.Click([this](IInspectable const&, RoutedEventArgs const&) {
            NavigateForward();
        });
        up_button_ = MakeIconButton(localization_, L"\xE74A", L"Toolbar.Up");
        up_button_.Click([this](IInspectable const&, RoutedEventArgs const&) {
            NavigateUp();
        });
        refresh_button_ = MakeIconButton(localization_, L"\xE72C", L"Toolbar.Refresh");
        refresh_button_.Click([this](IInspectable const&, RoutedEventArgs const&) {
            RefreshNavigation();
        });
        navigation_bar.Children().Append(back_button_);
        navigation_bar.Children().Append(forward_button_);
        navigation_bar.Children().Append(up_button_);
        navigation_bar.Children().Append(refresh_button_);
        Grid::SetColumn(back_button_, 0);
        Grid::SetColumn(forward_button_, 1);
        Grid::SetColumn(up_button_, 2);
        Grid::SetColumn(refresh_button_, 3);

        path_box_ = TextBox();
        path_box_.Text(hstring(localization_.GetString(L"Location.ThisPC")));
        path_box_.PlaceholderText(hstring(localization_.GetString(L"Toolbar.PathPlaceholder")));
        path_box_.HorizontalAlignment(HorizontalAlignment::Stretch);
        path_box_.VerticalAlignment(VerticalAlignment::Center);
        path_box_.Margin(Thickness{ 6, 0, 8, 0 });
        path_box_.KeyDown([this](IInspectable const&, KeyRoutedEventArgs const& args) {
            if (args.Key() == Windows::System::VirtualKey::Enter)
            {
                BrowseAsync(path_box_.Text().c_str());
                args.Handled(true);
            }
        });
        navigation_bar.Children().Append(path_box_);
        Grid::SetColumn(path_box_, 4);

        search_box_ = TextBox();
        winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetAutomationId(search_box_, L"SearchQuery");
        search_box_.PlaceholderText(hstring(localization_.GetString(L"Toolbar.SearchPlaceholder")));
        search_box_.IsEnabled(true);
        const auto search_weak = get_weak();
        search_box_.TextChanged([search_weak](auto&&, auto&&) {
            if (auto app = search_weak.get(); app && !app->search_input_sync_)
            { if (auto* tab = app->ActiveTab()) { tab->search_draft = app->search_box_.Text().c_str(); } }
        });
        search_box_.KeyDown([search_weak](auto&&, const KeyRoutedEventArgs& args) {
            if (auto app = search_weak.get())
            {
                if (args.Key() == Windows::System::VirtualKey::Enter) { app->SubmitSearch(false); args.Handled(true); }
                else if (args.Key() == Windows::System::VirtualKey::Escape) { app->LeaveActiveSearch(); args.Handled(true); }
            }
        });
        search_box_.VerticalAlignment(VerticalAlignment::Center);
        navigation_bar.Children().Append(search_box_);
        Grid::SetColumn(search_box_, 5);

        Grid::SetRow(navigation_bar, 0);
        root_.Children().Append(navigation_bar);

        auto body = Grid();
        AddGridColumn(body, 220, GridUnitType::Pixel);
        AddGridColumn(body, 1, GridUnitType::Star);

        // The outer ScrollViewer is the single vertical scroll owner for the
        // complete navigation surface. The TreeView is measured as content of
        // the unbounded StackPanel below, so its internal viewport does not
        // compete with a second visible scrollbar.
        navigation_pane_ = ScrollViewer();
        Automation::AutomationProperties::SetAutomationId(navigation_pane_, L"NavigationPane");
        navigation_pane_.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        navigation_pane_.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        navigation_pane_.HorizontalAlignment(HorizontalAlignment::Stretch);
        navigation_pane_.VerticalAlignment(VerticalAlignment::Stretch);
        navigation_pane_.Loaded([this](auto&&, auto&&) {
            InstallNavigationWheelInput();
        });
        navigation_pane_.ViewChanged([this](auto&&, const ScrollViewerViewChangedEventArgs& args) {
            if (!args.IsIntermediate() && navigation_wheel_target_ &&
                std::abs(navigation_pane_.VerticalOffset() - *navigation_wheel_target_) < 0.01)
            {
                navigation_wheel_target_.reset();
            }
        });
        auto navigation_content = StackPanel();

        auto quick_panel = StackPanel();
        quick_panel.Margin(Thickness{ 4, 4, 4, 0 });
        auto quick_heading = TextBlock();
        quick_heading.Text(hstring(localization_.GetString(L"Nav.Places")));
        quick_heading.Margin(Thickness{ 8, 0, 4, 2 });
        quick_heading.FontSize(13);
        quick_panel.Children().Append(quick_heading);

        navigation_quick_list_ = ListView();
        navigation_quick_list_.SelectionMode(ListViewSelectionMode::Single);
        navigation_quick_list_.IsItemClickEnabled(true);
        // Quick locations are part of the outer navigation extent. Keep the
        // ListView for its selection visuals/routing, but do not give it a
        // bounded viewport or an independent vertical scroll surface.
        ScrollViewer::SetVerticalScrollBarVisibility(
            navigation_quick_list_,
            ScrollBarVisibility::Disabled);
        ScrollViewer::SetVerticalScrollMode(
            navigation_quick_list_,
            ScrollMode::Disabled);
        ScrollViewer::SetHorizontalScrollBarVisibility(
            navigation_quick_list_, ScrollBarVisibility::Disabled);
        ScrollViewer::SetHorizontalScrollMode(navigation_quick_list_, ScrollMode::Disabled);
        navigation_quick_list_.Loaded([this](auto&&, auto&&) {
            DisableNavigationInternalScrolling(navigation_quick_list_);
        });
        navigation_quick_list_.Margin(Thickness{ 0, 0, 0, 0 });
        ConfigureQuickNavigationItemTemplate();
        navigation_quick_list_.ItemClick([this](
            IInspectable const&,
            ItemClickEventArgs const& args) {
            OnNavigationQuickItemClick(args);
        });
        navigation_quick_items_source_ =
            winrt::single_threaded_observable_vector<IInspectable>();
        navigation_quick_list_.ItemsSource(navigation_quick_items_source_);
        quick_panel.Children().Append(navigation_quick_list_);
        navigation_content.Children().Append(quick_panel);

        auto separator = Border();
        separator.Height(1);
        separator.Background(SolidColorBrush(winrt::Windows::UI::Color{
            0xFF,
            0x70,
            0x70,
            0x70 }));
        separator.Margin(Thickness{ 4, 0, 4, 0 });
        navigation_content.Children().Append(separator);

        auto tree_heading = TextBlock();
        tree_heading.Text(hstring(localization_.GetString(L"Nav.Drives")));
        tree_heading.Margin(Thickness{ 8, 5, 4, 0 });
        tree_heading.FontSize(13);
        navigation_content.Children().Append(tree_heading);

        navigation_tree_ = TreeView();
        ConfigureNavigationTreeItemTemplate();
        navigation_tree_.SelectionMode(TreeViewSelectionMode::Single);
        navigation_tree_.ItemInvoked([this](
            TreeView const&,
            TreeViewItemInvokedEventArgs const& args) {
            OnNavigationTreeItemInvoked(args);
        });
        navigation_tree_.Expanding([this](
            TreeView const&,
            TreeViewExpandingEventArgs const& args) {
            OnNavigationTreeExpanding(args);
        });
        navigation_tree_.Collapsed([this](
            TreeView const&,
            TreeViewCollapsedEventArgs const& args) {
            OnNavigationTreeCollapsed(args);
        });
        // TreeView's template contains a TreeViewList/ScrollViewer. Disable
        // its internal scrolling after template realization so expanded lazy
        // content contributes to the outer navigation ScrollViewer extent.
        ScrollViewer::SetVerticalScrollBarVisibility(
            navigation_tree_,
            ScrollBarVisibility::Disabled);
        ScrollViewer::SetVerticalScrollMode(
            navigation_tree_,
            ScrollMode::Disabled);
        ScrollViewer::SetHorizontalScrollBarVisibility(
            navigation_tree_, ScrollBarVisibility::Disabled);
        ScrollViewer::SetHorizontalScrollMode(navigation_tree_, ScrollMode::Disabled);
        navigation_tree_.Loaded([this](auto&&, auto&&) {
            DisableNavigationInternalScrolling(navigation_tree_);
        });
        navigation_tree_.Margin(Thickness{ 4, 0, 4, 4 });
        navigation_tree_.VerticalAlignment(VerticalAlignment::Top);
        navigation_content.Children().Append(navigation_tree_);
        ResetNavigationTree({});
        navigation_pane_.Content(navigation_content);
        startup_.Stage("BuildUi.navigation");
        body.Children().Append(navigation_pane_);
        Grid::SetColumn(navigation_pane_, 0);

        content_grid_ = Grid();
        AddGridRow(content_grid_, 36, GridUnitType::Pixel);
        AddGridRow(content_grid_, 1, GridUnitType::Auto);
        AddGridRow(content_grid_, 1, GridUnitType::Star);

        BuildFolderViewCommands();
        details_header_ = Grid();
        auto details_header = details_header_;
        details_header.Height(32);
        ConfigureDetailsColumns(details_header);
        auto name_header = MakeSortHeader(L"Column.Name", blink::app::SortField::Name);
        auto type_header = MakeSortHeader(L"Column.Type", blink::app::SortField::Type);
        auto size_header = MakeSortHeader(L"Column.Size", blink::app::SortField::Size);
        auto modified_header = MakeSortHeader(L"Column.Modified", blink::app::SortField::Modified);
        details_header.Children().Append(name_header);
        details_header.Children().Append(type_header);
        details_header.Children().Append(size_header);
        details_header.Children().Append(modified_header);
        Grid::SetColumn(name_header, 0);
        Grid::SetColumn(type_header, 1);
        Grid::SetColumn(size_header, 2);
        Grid::SetColumn(modified_header, 3);
        Grid::SetRow(details_header, 1);
        content_grid_.Children().Append(details_header);

        if (ui_probe_mode_ != UiProbeMode::NoFolderList)
        {
            entries_ = CreateFolderViewControl(blink::app::FolderViewMode::Details);
            startup_.Stage("BuildUi.before-folder-template");
            ConfigureFolderItemTemplate();

            const bool uses_items_source =
                ui_probe_mode_ == UiProbeMode::None ||
                ui_probe_mode_ == UiProbeMode::ItemsSourceString ||
                ui_probe_mode_ == UiProbeMode::ItemsSourceStringExplicitPanel ||
                ui_probe_mode_ == UiProbeMode::ItemsSourceStringConstantTemplate ||
                ui_probe_mode_ == UiProbeMode::CurrentMinimalPropertySet;
            if (uses_items_source)
            {
                auto& active_tab_ui = EnsureTabUi(tab_window_state_.ActiveTabId());
                active_tab_ui.row_items_source =
                    winrt::single_threaded_observable_vector<IInspectable>();
                entries_.ItemsSource(active_tab_ui.row_items_source);
            }
            content_grid_.Children().Append(entries_);
            Grid::SetRow(entries_, 2);
        }
        body.Children().Append(content_grid_);
        Grid::SetColumn(content_grid_, 1);
        BuildSearchSurface();
        body.Children().Append(search_surface_);
        Grid::SetColumn(search_surface_, 1);

        Grid::SetRow(body, 1);
        root_.Children().Append(body);

        auto status_bar = Grid();
        AddGridColumn(status_bar, 1, GridUnitType::Star);
        AddGridColumn(status_bar, 140, GridUnitType::Pixel);
        AddGridColumn(status_bar, 180, GridUnitType::Pixel);
        status_ = TextBlock();
        status_.VerticalAlignment(VerticalAlignment::Center);
        selection_status_ = TextBlock();
        selection_status_.VerticalAlignment(VerticalAlignment::Center);
        selection_status_.HorizontalAlignment(HorizontalAlignment::Right);
        const auto weak_app = get_weak();
        index_status_ = index_service_presentation_.Build(localization_, [weak_app](std::wstring root) {
            if (auto app = weak_app.get(); app && !app->window_style_closed_)
            { app->RefreshIndexServiceStatus(std::move(root)); }
        }, [weak_app](std::wstring root) {
            if (auto app = weak_app.get(); app && !app->window_style_closed_)
            { app->ManageIndexService(std::move(root), true); }
        }, [weak_app](std::wstring root) {
            if (auto app = weak_app.get(); app && !app->window_style_closed_)
            { app->ManageIndexService(std::move(root), false); }
        });
        status_bar.Children().Append(status_);
        status_bar.Children().Append(selection_status_);
        status_bar.Children().Append(index_status_);
        Grid::SetColumn(status_, 0);
        Grid::SetColumn(selection_status_, 1);
        Grid::SetColumn(index_status_, 2);
        Grid::SetRow(status_bar, 2);
        root_.Children().Append(status_bar);
        BuildFileOperationStatus();

        SyncTabViewItems();
        PresentActiveTab();
    }

    static winrt::Windows::Foundation::IAsyncAction AwaitNativeOperationEnd(
        winrt::Windows::Foundation::IAsyncAction action, std::function<void(HRESULT)> continuation)
    {
        const auto foreground = winrt::apartment_context();
        HRESULT error = S_OK;
        try { co_await action; } catch (...) { error = winrt::to_hresult(); }
        try { co_await foreground; } catch (...) { co_return; }
        try { continuation(error); } catch (...) {}
    }

    bool IsOperationDialogOpen() const noexcept
    { return folder_name_dialog_.IsOpen() || copy_conflict_dialog_.IsOpen() || delete_confirmation_.IsOpen() || properties_dialog_.IsOpen(); }

    void CloseOperationDialogs() noexcept
    {
        if (clipboard_cancellation_) { clipboard_cancellation_->store(true); }
        const bool had_conflict = copy_conflict_wait_operation_id_ != 0 || copy_conflict_dialog_.IsOpen();
        const bool had_merge_conflict = directory_merge_dialog_operation_id_ != 0;
        copy_conflict_wait_operation_id_ = 0;
        directory_merge_dialog_operation_id_ = directory_merge_dialog_conflict_id_ = 0;
        folder_name_dialog_.Close(); copy_conflict_dialog_.Close();
        delete_confirmation_.Close(); properties_dialog_.Close();
        if ((had_conflict && file_operations_.HasPendingBatch()) || had_merge_conflict)
        {
            file_operations_.Cancel();
            try { RenderFileOperationStatus(); } catch (...) {}
        }
    }

    using FileOperationOrigin = blink::app::FileOperationOrigin;

    bool IsFileOperationOriginCurrent(const FileOperationOrigin& origin) const
    { return blink::app::IsFileOperationOriginCurrent(origin, ActiveTab(), window_style_closed_); }

    std::wstring FileOperationErrorText(const DWORD error, const bool renaming = false, const bool copying = false) const
    {
        if (copying && error == ERROR_NOT_SUPPORTED) { return localization_.GetString(L"Copy.Unsupported"); }
        if (copying && error == ERROR_FILE_INVALID) { return localization_.GetString(L"Copy.TargetChanged"); }
        if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) { return localization_.GetString(L"NewFolder.Conflict"); }
        if (error == ERROR_FILE_INVALID) { return localization_.GetString(renaming ? L"Operation.TargetChanged" : L"Operation.DirectoryChanged"); }
        if (error == ERROR_NOT_SUPPORTED) { return localization_.GetString(L"Operation.Unsupported"); }
        if (error == ERROR_INVALID_NAME || error == ERROR_FILENAME_EXCED_RANGE) { return localization_.GetString(L"NewFolder.InvalidName"); }
        return blink::core::Win32ErrorMessage(error);
    }

    void BuildFileOperationStatus()
    {
        operation_panel_ = Grid(); operation_panel_.ColumnSpacing(8);
        operation_panel_.Margin(Thickness{ 0, 6, 0, 0 });
        AddGridColumn(operation_panel_, 1, GridUnitType::Star);
        AddGridColumn(operation_panel_, 1, GridUnitType::Auto);
        AddGridColumn(operation_panel_, 1, GridUnitType::Auto);
        AddGridColumn(operation_panel_, 1, GridUnitType::Auto);
        operation_text_ = TextBlock(); operation_text_.TextWrapping(TextWrapping::Wrap);
        operation_text_.MaxLines(2); operation_text_.TextTrimming(TextTrimming::CharacterEllipsis);
        operation_text_.VerticalAlignment(VerticalAlignment::Center);
        Automation::AutomationProperties::SetAutomationId(operation_text_, L"FileOperationStatus");
        operation_cancel_ = Button(); operation_cancel_.Content(winrt::box_value(hstring(localization_.GetString(L"Operation.Cancel"))));
        operation_retry_ = Button(); operation_retry_.Content(winrt::box_value(hstring(localization_.GetString(L"NewFolder.RetryName"))));
        operation_skip_ = Button(); operation_skip_.Content(winrt::box_value(hstring(localization_.GetString(L"Delete.Skip"))));
        operation_skip_.Visibility(Visibility::Collapsed);
        Automation::AutomationProperties::SetAutomationId(operation_cancel_, L"FileOperationCancel");
        Automation::AutomationProperties::SetAutomationId(operation_retry_, L"NewFolderRetry");
        Automation::AutomationProperties::SetAutomationId(operation_skip_, L"DeleteSkip");
        const auto weak = get_weak();
        operation_cancel_.Click([weak](auto&&, auto&&) {
            if (auto app = weak.get(); app && !app->window_style_closed_)
            {
                if (app->file_drag_drop_.Preparing()) { app->file_drag_drop_.CancelPreparation(); }
                else if (app->clipboard_work_pending_ && app->clipboard_cancellation_)
                { app->clipboard_cancellation_->store(true); }
                else { app->file_operations_.Cancel(); }
                app->RenderFileOperationStatus();
            }
        });
        operation_retry_.Click([weak](auto&&, auto&&) {
            if (auto app = weak.get(); app && !app->window_style_closed_ && app->ActiveTab())
            {
                const auto last = app->file_operations_.Snapshot();
                const auto origin = app->CurrentOperationOrigin();
                if (last.kind == blink::app::FileOperationKind::Delete)
                { app->ResumeDelete(origin, last.operation_id, blink::app::DeleteRetryAction::Retry); return; }
                if (last.kind == blink::app::FileOperationKind::Move)
                {
                    if (last.move_result && (blink::app::MoveHasPartialDestination(*last.move_result) ||
                        blink::app::MoveHasSourceChanges(*last.move_result)))
                    { app->ResumeMove(origin, last.operation_id, blink::app::FileCopyConflictPolicy::Skip); return; }
                    if (last.move_request) { app->ShowMoveNameDialog(origin, *last.move_request, last.operation_id); }
                    return;
                }
                if (last.kind == blink::app::FileOperationKind::DirectoryCopy)
                {
                    if (last.directory_copy_request)
                    { app->ShowDirectoryCopyNameDialog(origin, *last.directory_copy_request, last.operation_id); }
                    return;
                }
                if ((last.stage == blink::app::FileOperationStage::Failed || last.stage == blink::app::FileOperationStage::Cancelled) &&
                    origin.identity == last.request.parent_identity && last.copy_request)
                {
                    app->ShowCopyNameDialog(origin, *last.copy_request, last.stage == blink::app::FileOperationStage::Failed ?
                        app->FileOperationErrorText(last.result.error, false, true) : std::wstring{}, last.operation_id);
                }
                else if (last.stage == blink::app::FileOperationStage::Failed && origin.identity == last.request.parent_identity)
                { app->ShowFileNameDialog(origin, last.request.name, app->FileOperationErrorText(last.result.error, last.kind == blink::app::FileOperationKind::Rename), last.rename_source); }
            }
        });
        operation_skip_.Click([weak](auto&&, auto&&) {
            if (auto app = weak.get(); app && !app->window_style_closed_ && app->ActiveTab())
            { app->ResumeDelete(blink::app::CaptureFileOperationOrigin(*app->ActiveTab()), app->file_operations_.Snapshot().operation_id,
                blink::app::DeleteRetryAction::Skip); }
        });
        operation_panel_.Children().Append(operation_text_);
        operation_panel_.Children().Append(operation_cancel_); Grid::SetColumn(operation_cancel_, 1);
        operation_panel_.Children().Append(operation_retry_); Grid::SetColumn(operation_retry_, 2);
        operation_panel_.Children().Append(operation_skip_); Grid::SetColumn(operation_skip_, 3);
        operation_panel_.Visibility(Visibility::Collapsed);
        Grid::SetRow(operation_panel_, 3); root_.Children().Append(operation_panel_);
    }

    void RenderFileOperationStatus()
    {
        if (!operation_panel_ || window_style_closed_) { return; }
        operation_skip_.Visibility(Visibility::Collapsed);
        if (file_drag_drop_.Preparing())
        {
            operation_panel_.Visibility(Visibility::Visible);
            operation_text_.Text(hstring(localization_.GetString(L"DragDrop.Preparing")));
            operation_cancel_.Visibility(Visibility::Visible); operation_cancel_.IsEnabled(true); operation_retry_.Visibility(Visibility::Collapsed); return;
        }
        if (clipboard_work_pending_)
        {
            const bool cancelling = clipboard_cancellation_ && clipboard_cancellation_->load();
            const auto text = localization_.GetString(cancelling ? L"Clipboard.Cancelling" : L"Clipboard.Preparing");
            operation_panel_.Visibility(Visibility::Visible);
            operation_text_.Text(hstring(text)); ToolTipService::SetToolTip(operation_text_, winrt::box_value(hstring(text)));
            operation_cancel_.Visibility(Visibility::Visible); operation_cancel_.IsEnabled(!cancelling);
            operation_retry_.Visibility(Visibility::Collapsed);
            return;
        }
        using Stage = blink::app::FileOperationStage;
        const auto snapshot = file_operations_.Snapshot();
        operation_panel_.Visibility(snapshot.operation_id ? Visibility::Visible : Visibility::Collapsed);
        if (!snapshot.operation_id) { return; }
        if (snapshot.kind == blink::app::FileOperationKind::Delete) { RenderDeleteStatus(snapshot); return; }
        if (snapshot.kind == blink::app::FileOperationKind::Move)
        { RenderMoveStatus(snapshot); return; }
        operation_retry_.Content(winrt::box_value(hstring(localization_.GetString(L"NewFolder.RetryName"))));
        if (snapshot.kind == blink::app::FileOperationKind::DirectoryCopy)
        { RenderDirectoryCopyStatus(snapshot); return; }
        operation_retry_.IsEnabled(true);
        const auto path = snapshot.result.path.empty() ?
            blink::core::JoinPath(snapshot.request.parent, snapshot.request.name) : snapshot.result.path;
        const bool renaming = snapshot.kind == blink::app::FileOperationKind::Rename;
        const auto key = renaming ? (snapshot.stage == Stage::Completed ?
                (snapshot.result.committed ? L"Operation.RenameCompleted" : L"Operation.RenameUnchanged") :
            snapshot.stage == Stage::Cancelled ? L"Operation.RenameCancelled" :
            snapshot.stage == Stage::Cancelling ? L"Operation.RenameCancelling" :
            snapshot.stage == Stage::Failed ? L"Operation.RenameFailed" : L"Operation.RenameRunning") :
            snapshot.stage == Stage::Completed ? L"Operation.Completed" :
            snapshot.stage == Stage::Cancelled ? L"Operation.Cancelled" :
            snapshot.stage == Stage::Cancelling ? L"Operation.Cancelling" :
            snapshot.stage == Stage::Failed ? L"Operation.Failed" : L"Operation.Running";
        auto text = snapshot.stage == Stage::Failed ? localization_.Format(key,
            std::vector<std::wstring>{ path, FileOperationErrorText(snapshot.result.error, renaming) }) : localization_.Format(key, path);
        if (snapshot.kind == blink::app::FileOperationKind::Copy)
        {
            const auto copy_key = snapshot.stage == Stage::Skipped ? L"Operation.CopySkipped" :
                snapshot.stage == Stage::Completed ? L"Operation.CopyCompleted" :
                snapshot.stage == Stage::Cancelled ? L"Operation.CopyCancelled" :
                snapshot.stage == Stage::Cancelling ? L"Operation.CopyCancelling" :
                snapshot.stage == Stage::Failed ? L"Operation.CopyFailed" : L"Operation.CopyRunning";
            text = snapshot.stage == Stage::Failed ? localization_.Format(copy_key,
                std::vector<std::wstring>{ path, FileOperationErrorText(snapshot.result.error, false, true) }) :
                localization_.Format(copy_key, path);
            if (snapshot.stage == Stage::Running && snapshot.copy_progress.total_bytes)
            { text += localization_.Format(L"Operation.CopyBytes", std::vector<std::wstring>{
                std::to_wstring(snapshot.copy_progress.transferred_bytes), std::to_wstring(snapshot.copy_progress.total_bytes) }); }
            if (snapshot.result.partial_file_left) { text += L" " + localization_.GetString(L"Operation.CopyPartial"); }
        }
        auto detail = text;
        if (snapshot.kind == blink::app::FileOperationKind::Copy && snapshot.copy_batch && snapshot.copy_batch->requested_count > 1)
        {
            const auto& batch = *snapshot.copy_batch;
            const auto key = snapshot.stage == Stage::Completed || snapshot.stage == Stage::Skipped ? L"Operation.CopyBatchCompleted" :
                snapshot.stage == Stage::Cancelled ? L"Operation.CopyBatchCancelled" :
                snapshot.stage == Stage::Cancelling ? L"Operation.CopyBatchCancelling" :
                snapshot.stage == Stage::Failed ? L"Operation.CopyBatchFailed" : L"Operation.CopyBatchRunning";
            text = localization_.GetString(key) + L" " + localization_.Format(L"Operation.CopyBatchSummary", std::vector<std::wstring>{
                std::to_wstring(batch.requested_count), std::to_wstring(batch.copied_count), std::to_wstring(batch.skipped_count),
                std::to_wstring(batch.failed_count), std::to_wstring(batch.cancelled_count), std::to_wstring(batch.remaining_count) });
            if (batch.current_index < batch.items.size())
            {
                text += L" " + localization_.Format(L"Operation.CopyBatchCurrent", std::vector<std::wstring>{
                    std::to_wstring(batch.current_index + 1), path });
                const auto bytes = snapshot.directory_copy_request ? snapshot.directory_copy_progress.current_file : snapshot.copy_progress;
                if (snapshot.directory_copy_request)
                {
                    const auto& result = snapshot.directory_copy_result;
                    text += L" " + localization_.Format(L"DirectoryCopy.Counts", std::vector<std::wstring>{
                        std::to_wstring(result ? result->files_copied : snapshot.directory_copy_progress.files_copied),
                        std::to_wstring(result ? result->directories_created : snapshot.directory_copy_progress.directories_created) });
                }
                if (snapshot.stage == Stage::Running && bytes.total_bytes)
                { text += localization_.Format(L"Operation.CopyBytes", std::vector<std::wstring>{
                    std::to_wstring(bytes.transferred_bytes), std::to_wstring(bytes.total_bytes) }); }
            }
            if (snapshot.stage == Stage::Failed) { text += L" " + FileOperationErrorText(snapshot.result.error, false, true); }
            if (batch.partial_count)
            { text += L" " + localization_.Format(L"Operation.CopyBatchPartial", std::to_wstring(batch.partial_count)); }
            detail = text;
            // Preserve the paths of earlier incomplete attempts in the tooltip,
            // even when a subsequent retry copied this queue item successfully.
            if (!snapshot.directory_copy_progress.current_destination_path.empty())
            { detail += L"\n" + snapshot.directory_copy_progress.current_destination_path; }
            std::uint64_t tree_partial_attempts{}; std::size_t retained_tree_paths{};
            for (const auto& item : batch.items)
            {
                const auto append_partial = [&detail](const blink::app::FileCopyResult& result) {
                    if (result.partial_file_left && !result.path.empty() && detail.size() + result.path.size() < 65536)
                    { detail += L"\n" + result.path; }
                };
                for (const auto& previous : item.previous_results) { append_partial(previous); }
                append_partial(item.result);
                const auto append_tree = [&](const blink::app::DirectoryCopyResult& result) {
                    if (result.partial_tree_left && !result.path.empty() && detail.size() + result.path.size() < 65536)
                    { detail += L"\n" + result.path; ++retained_tree_paths; }
                };
                tree_partial_attempts += item.directory_partial_attempts;
                for (const auto& previous : item.previous_directory_results) { append_tree(previous); }
                if (item.directory_result)
                {
                    append_tree(*item.directory_result);
                    if (!item.directory_result->failure_path.empty() && detail.size() + item.directory_result->failure_path.size() < 65536)
                    { detail += L"\n" + item.directory_result->failure_path; }
                }
            }
            if (tree_partial_attempts)
            {
                const auto note = localization_.Format(L"DirectoryCopy.PartialHistory", std::to_wstring(tree_partial_attempts));
                text += L" " + note; detail += L"\n" + note;
                if (tree_partial_attempts > retained_tree_paths) { detail += L"\n" + localization_.GetString(L"DirectoryCopy.HistoryLimited"); }
            }
        }
        const bool partial_merge = snapshot.directory_copy_request && snapshot.directory_copy_result &&
            snapshot.directory_copy_request->conflict_policy == blink::app::FileCopyConflictPolicy::Merge &&
            snapshot.directory_copy_result->partial_tree_left;
        if (snapshot.directory_copy_request && (!snapshot.copy_batch || snapshot.copy_batch->requested_count <= 1))
        {
            const auto& result = snapshot.directory_copy_result;
            const auto& progress = snapshot.directory_copy_progress;
            const auto counts = localization_.Format(L"DirectoryCopy.Counts", std::vector<std::wstring>{
                std::to_wstring(result ? result->files_copied : progress.files_copied),
                std::to_wstring(result ? result->directories_created : progress.directories_created) });
            text += L" " + counts; detail += L"\n" + counts;
        }
        if (snapshot.directory_copy_request)
        {
            const auto& result = snapshot.directory_copy_result;
            const auto files_skipped = result ? result->files_skipped : snapshot.directory_copy_progress.files_skipped;
            const auto directories_skipped = result ? result->directories_skipped : snapshot.directory_copy_progress.directories_skipped;
            if (files_skipped || directories_skipped)
            {
                const auto skipped = localization_.Format(L"DirectoryCopy.SkippedCounts", std::vector<std::wstring>{
                    std::to_wstring(files_skipped), std::to_wstring(directories_skipped) });
                text += L" " + skipped; detail += L"\n" + skipped;
            }
        }
        if (snapshot.directory_conflict)
        {
            const auto& pending = snapshot.directory_conflict->request;
            const auto waiting = localization_.Format(L"DirectoryCopy.WaitingForConflict",
                blink::core::JoinPath(pending.destination_parent, pending.destination_name));
            text = waiting + L" " + text; detail = waiting + L"\n" + detail;
        }
        operation_text_.Text(hstring(text));
        ToolTipService::SetToolTip(operation_text_, winrt::box_value(hstring(detail)));
        const bool running = snapshot.stage == Stage::Queued || snapshot.stage == Stage::Running || snapshot.stage == Stage::Cancelling;
        operation_cancel_.Visibility(running || file_operations_.HasPendingBatch() ? Visibility::Visible : Visibility::Collapsed);
        operation_cancel_.IsEnabled(snapshot.stage != Stage::Cancelling);
        const auto* tab = ActiveTab();
        const bool retryable = (snapshot.stage == Stage::Failed ||
            (snapshot.stage == Stage::Cancelled && snapshot.kind == blink::app::FileOperationKind::Copy)) &&
            (!snapshot.copy_batch || snapshot.copy_batch->requested_count <= 1 || snapshot.copy_batch->resumable);
        const bool can_retry = retryable && !partial_merge && tab && !tab->search_visible &&
            OperationTargetVisible(snapshot.request.parent_identity);
        operation_retry_.Visibility(can_retry ? Visibility::Visible : Visibility::Collapsed);
    }

    std::wstring DeleteErrorText(const HRESULT error, const blink::app::DeleteMode mode) const
    {
        if (mode == blink::app::DeleteMode::Recycle && error == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED))
        { return localization_.GetString(L"Delete.RecycleUnavailable"); }
        if (error == HRESULT_FROM_WIN32(ERROR_FILE_INVALID)) { return localization_.GetString(L"Delete.TargetChanged"); }
        return winrt::hresult_error(error).message().c_str();
    }

    void RenderDeleteStatus(const blink::app::FileOperationSnapshot& value)
    {
        using Stage = blink::app::FileOperationStage;
        auto text = localization_.GetString(blink::app::DeleteSummaryKey(value));
        if (value.delete_batch)
        {
            const auto& batch = *value.delete_batch;
            text += L" " + localization_.Format(L"Delete.Summary", std::vector<std::wstring>{ std::to_wstring(batch.requested_count),
                std::to_wstring(batch.recycled_count), std::to_wstring(batch.permanent_count), std::to_wstring(batch.skipped_count),
                std::to_wstring(batch.failed_count), std::to_wstring(batch.cancelled_count), std::to_wstring(batch.remaining_count) });
            if (value.delete_request)
            {
                text += L" " + localization_.Format(L"Delete.Current", std::vector<std::wstring>{ std::to_wstring(batch.current_index + 1),
                    blink::core::JoinPath(value.delete_request->parent, value.delete_request->source.name) });
            }
            if (batch.partial_count) { text += L" " + localization_.Format(L"Delete.Partial", std::to_wstring(batch.partial_count)); }
            if (batch.warning_count) { text += L" " + localization_.Format(L"Delete.Warning", std::to_wstring(batch.warning_count)); }
        }
        if (value.stage == Stage::Running && value.delete_progress.total_work)
        { text += L" " + localization_.Format(L"Delete.Progress", std::vector<std::wstring>{
            std::to_wstring(value.delete_progress.completed_work), std::to_wstring(value.delete_progress.total_work) }); }
        if (value.stage == Stage::Failed && value.delete_result && value.delete_request)
        { text += L" " + DeleteErrorText(value.delete_result->error, value.delete_request->mode); }
        auto detail = text;
        if (value.delete_batch)
        {
            for (const auto& item : value.delete_batch->items)
            {
                const auto& result = item.retained_result ? *item.retained_result : item.result;
                if (detail.size() + result.path.size() + result.failure_path.size() + 64 >= 65536) { break; }
                if (!result.path.empty()) { detail += L"\n" + result.path; }
                if (!result.failure_path.empty() && result.failure_path != result.path) { detail += L"\n" + result.failure_path; }
                if (FAILED(result.error)) { detail += L" (" + std::to_wstring(static_cast<std::uint32_t>(result.error)) + L")"; }
            }
        }
        operation_text_.Text(hstring(text)); ToolTipService::SetToolTip(operation_text_, winrt::box_value(hstring(detail)));
        const bool running = value.stage == Stage::Queued || value.stage == Stage::Running || value.stage == Stage::Cancelling;
        operation_cancel_.Visibility(running || file_operations_.HasPendingDeleteBatch() ? Visibility::Visible : Visibility::Collapsed);
        operation_cancel_.IsEnabled(value.stage != Stage::Cancelling);
        const auto* tab = ActiveTab();
        const bool retryable = value.stage == Stage::Failed && value.delete_batch && value.delete_batch->resumable && tab &&
            !tab->search_visible && tab->state->CommittedDirectoryIdentity() == value.request.parent_identity;
        operation_retry_.Content(winrt::box_value(hstring(localization_.GetString(L"Delete.Retry"))));
        operation_retry_.Visibility(retryable ? Visibility::Visible : Visibility::Collapsed);
        operation_skip_.Visibility(retryable ? Visibility::Visible : Visibility::Collapsed);
        operation_retry_.IsEnabled(!file_operations_.IsBusy()); operation_skip_.IsEnabled(!file_operations_.IsBusy());
    }

    void DeleteActionEnded(const std::uint64_t id)
    {
        if (file_operations_.Snapshot().operation_id != id) { return; }
        const auto action = file_operations_.CurrentAction();
        if (!action || action.Status() != winrt::Windows::Foundation::AsyncStatus::Started) { RenderFileOperationStatus(); return; }
        if (delete_wait_operation_id_ == id) { return; }
        delete_wait_operation_id_ = id; const auto weak = get_weak();
        delete_end_wait_ = AwaitNativeOperationEnd(action, [weak, id](HRESULT) {
            if (auto app = weak.get(); app && !app->window_style_closed_ && app->delete_wait_operation_id_ == id)
            {
                app->delete_wait_operation_id_ = 0;
                if (app->file_operations_.Snapshot().operation_id == id) { app->RenderFileOperationStatus(); }
            }
        });
    }

    blink::app::FileOperationCoordinator::PublishCallback DeletePublication()
    {
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        return [weak, dispatcher](const blink::app::FileOperationSnapshot& value) {
            const auto relocation = blink::app::CompletedFolderRelocation(value);
            const auto parent = blink::app::DeleteRefreshParent(value);
            const bool terminal = blink::app::FileOperationIsTerminal(value.stage); const auto id = value.operation_id;
            (void)dispatcher.TryEnqueue([weak, relocation, parent, terminal, id] {
                if (auto app = weak.get(); app && !app->window_style_closed_)
                {
                    if (terminal) { app->RestoreOperationWatches(id); }
                    if (relocation) { app->FollowFolderRelocation(*relocation); }
                    if (parent) { app->RefreshAfterFileOperation(*parent); }
                    if (app->file_operations_.Snapshot().operation_id == id)
                    { app->RenderFileOperationStatus(); if (terminal) { app->DeleteActionEnded(id); } }
                }
            });
        };
    }

    static std::vector<blink::app::Location> DeleteWatchSources(const std::vector<blink::app::DeleteRequest>& requests)
    {
        std::vector<blink::app::Location> sources;
        for (const auto& request : requests)
        { if (request.source.is_directory && !request.skip) { sources.push_back(blink::app::Location::Folder(blink::core::JoinPath(request.parent, request.source.name))); } }
        return sources;
    }
    void StartDeletes(const FileOperationOrigin& origin, std::vector<blink::app::DeleteRequest> requests)
    {
        if (!IsFileOperationOriginCurrent(origin))
        { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); return; }
        if (file_operations_.IsBusy() || file_operations_.HasPendingBatch() || clipboard_work_pending_ ||
            copy_conflict_wait_operation_id_ != 0 || IsOperationDialogOpen()) { return; }
        for (auto& request : requests)
        { if (request.parent_identity != origin.identity) { return; } request.parent = origin.location.FolderPath(); }
        auto prerequisites = PrepareOperationWatches(DeleteWatchSources(requests));
        const auto id = file_operations_.DeleteEntries(std::move(requests), DeletePublication(), std::move(prerequisites));
        operation_watch_id_ = id; if (!id) { RestoreOperationWatches(0); }
        RenderFileOperationStatus();
    }
    void ShowSelectedProperties()
    {
        const auto* tab = ActiveTab();
        if (!tab || tab->search_visible || IsOperationDialogOpen()) { return; }
        blink::app::ItemPropertiesRequest request;
        if (FAILED(blink::app::CaptureItemProperties(*tab->state, request)))
        { status_.Text(hstring(localization_.GetString(L"Properties.CaptureFailed"))); return; }
        const auto weak = get_weak();
        try
        {
            (void)properties_dialog_.Show(root_.XamlRoot(), localization_, std::move(request), [weak](HRESULT) {
                if (const auto app = weak.get(); app && !app->window_style_closed_)
                { app->status_.Text(hstring(app->localization_.GetString(L"Properties.ReadFailed"))); }
            });
        }
        catch (...)
        { properties_dialog_.Close(); status_.Text(hstring(localization_.GetString(L"Properties.ReadFailed"))); }
    }

    void DeleteSelection(const blink::app::DeleteMode mode)
    {
        const auto* tab = ActiveTab();
        if (!tab || tab->search_visible || file_operations_.IsBusy() || file_operations_.HasPendingBatch() || IsOperationDialogOpen()) { return; }
        const auto origin = blink::app::CaptureFileOperationOrigin(*tab);
        std::vector<blink::app::DeleteRequest> requests;
        if (FAILED(blink::app::CaptureDeleteRequests(*tab->state, mode, requests)))
        { status_.Text(hstring(localization_.GetString(L"Delete.CaptureFailed"))); return; }
        if (mode == blink::app::DeleteMode::Recycle) { StartDeletes(origin, std::move(requests)); return; }
        const auto weak = get_weak();
        try
        {
            std::vector<std::wstring> names; names.reserve(requests.size());
            for (const auto& request : requests) { names.push_back(request.source.name); }
            (void)delete_confirmation_.Show(root_.XamlRoot(), localization_, names, [weak, origin, requests = std::move(requests)]() mutable {
                if (auto app = weak.get(); app && !app->window_style_closed_)
                {
                    for (auto& request : requests) { request.permanent_confirmed = true; }
                    app->StartDeletes(origin, std::move(requests));
                }
            }, [weak](HRESULT) {
                if (auto app = weak.get(); app && !app->window_style_closed_) { app->status_.Text(hstring(app->localization_.GetString(L"Delete.DialogFailed"))); }
            });
        }
        catch (...)
        { delete_confirmation_.Close(); status_.Text(hstring(localization_.GetString(L"Delete.DialogFailed"))); }
    }
    void ResumeDelete(const FileOperationOrigin& origin, const std::uint64_t id, const blink::app::DeleteRetryAction action)
    {
        const auto value = file_operations_.Snapshot();
        if (!IsFileOperationOriginCurrent(origin) || file_operations_.IsBusy() || value.operation_id != id || !value.delete_request || !value.delete_batch ||
            !value.delete_batch->resumable || value.delete_request->parent_identity != origin.identity) { return; }
        std::vector<blink::app::DeleteRequest> remaining;
        for (std::size_t index = value.delete_batch->current_index; index < value.delete_batch->items.size(); ++index)
        {
            auto request = value.delete_batch->items[index].request;
            if (request.parent_identity == origin.identity) { request.parent = origin.location.FolderPath(); }
            if (index == value.delete_batch->current_index) { request.skip = action == blink::app::DeleteRetryAction::Skip; }
            remaining.push_back(std::move(request));
        }
        auto prerequisites = PrepareOperationWatches(DeleteWatchSources(remaining));
        const auto next = file_operations_.ResumeDeleteBatch(id, action, DeletePublication(), std::move(prerequisites), origin.location.FolderPath());
        operation_watch_id_ = next; if (!next) { RestoreOperationWatches(0); }
        RenderFileOperationStatus();
    }

    void ShowFileNameDialog(const FileOperationOrigin& origin, std::wstring name, std::wstring error = {},
        std::optional<blink::core::FileEntry> rename_source = {})
    {
        if (!IsFileOperationOriginCurrent(origin)) { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); return; }
        if (file_operations_.IsBusy() || file_operations_.HasPendingBatch() || clipboard_work_pending_ || copy_conflict_wait_operation_id_ != 0 || IsOperationDialogOpen())
        { status_.Text(hstring(localization_.Format(L"Status.CommandBusy", localization_.GetString(L"Command.NewFolder")))); return; }
        const auto weak = get_weak();
        try
        {
            (void)folder_name_dialog_.Show(root_.XamlRoot(), localization_, rename_source ?
                blink::core::JoinPath(origin.location.FolderPath(), rename_source->name) : origin.location.FolderPath(), std::move(name),
                std::move(error), [weak, origin, rename_source](std::wstring value) {
                    if (auto app = weak.get(); app && !app->window_style_closed_) { app->StartFileOperation(origin, std::move(value), rename_source); }
                }, [weak](HRESULT) {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    { app->status_.Text(hstring(app->localization_.GetString(L"NewFolder.DialogFailed"))); }
                }, rename_source.has_value());
        }
        catch (...) { status_.Text(hstring(localization_.GetString(L"NewFolder.DialogFailed"))); }
    }

    void StartFileOperation(const FileOperationOrigin& origin, std::wstring name,
        std::optional<blink::core::FileEntry> rename_source = {})
    {
        if (!IsFileOperationOriginCurrent(origin)) { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); return; }
        if (file_operations_.IsBusy() || file_operations_.HasPendingBatch()) { return; }
        if (rename_source && !ActiveState().SelectedCommittedRow(blink::app::MakeRowIdentity(*rename_source)))
        { status_.Text(hstring(localization_.GetString(L"Operation.TargetChanged"))); return; }
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        const auto publish = [weak, dispatcher](blink::app::FileOperationSnapshot snapshot) {
                (void)dispatcher.TryEnqueue([weak, snapshot = std::move(snapshot)] {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    {
                        if (blink::app::FileOperationIsTerminal(snapshot.stage)) { app->RestoreOperationWatches(snapshot.operation_id); }
                        if (snapshot.result.committed)
                        {
                            if (const auto relocation = blink::app::CompletedFolderRelocation(snapshot))
                            { app->FollowFolderRelocation(*relocation); }
                            app->RefreshAfterFileOperation(snapshot.request.parent_identity);
                        }
                        if (app->file_operations_.Snapshot().operation_id == snapshot.operation_id) { app->RenderFileOperationStatus(); }
                    }
                });
            };
        auto prerequisites = PrepareOperationWatches(rename_source && rename_source->is_directory ?
            std::vector<blink::app::Location>{ blink::app::Location::Folder(blink::core::JoinPath(origin.location.FolderPath(), rename_source->name)) } :
            std::vector<blink::app::Location>{});
        const auto id = rename_source ? file_operations_.Rename({ origin.location.FolderPath(), origin.identity,
                *rename_source, std::move(name) }, publish, std::move(prerequisites)) :
            file_operations_.CreateFolder({ origin.location.FolderPath(), origin.identity, std::move(name) }, publish);
        operation_watch_id_ = id;
        if (!id) { RestoreOperationWatches(0); }
        if (!id) { status_.Text(hstring(localization_.Format(L"Status.CommandBusy", localization_.GetString(
            rename_source ? L"Command.Rename" : L"Command.NewFolder")))); }
        RenderFileOperationStatus();
    }

    bool FileDropBusy() const
    { return window_style_closed_ || file_operations_.IsBusy() || file_operations_.HasPendingBatch() || clipboard_work_pending_ || IsOperationDialogOpen(); }

    std::optional<FileOperationOrigin> FileAreaDropTarget(const IInspectable& element) const
    {
        const auto* tab = ActiveTab(); const auto* ui = ActiveTabUi();
        if (!tab || !ui || tab->search_visible || ui->presentation_in_progress || tab->state->PresentationSnapshot().has_pending) { return {}; }
        const auto owner = blink::app::CaptureFileOperationOrigin(*tab);
        const auto row = ResolveRowIndexFromElement(element);
        if (ui->showing_this_pc)
        {
            if (!row || *row >= ui->this_pc_rows.size() || !ui->this_pc_rows[*row].target.IsFolder()) { return {}; }
            return blink::app::FileDropOrigin(owner, ui->this_pc_rows[*row].target);
        }
        if (!ui->showing_committed_folder || !owner.location.IsFolder() || !owner.identity.valid) { return {}; }
        if (!row) { return blink::app::FileDropOrigin(owner, owner.location, owner.identity); }
        if (*row >= ui->row_models.size()) { return {}; }
        const auto& target = ui->row_models[*row];
        if (!target.is_directory || !target.identity.valid) { return {}; }
        return blink::app::FileDropOrigin(owner, blink::app::Location::Folder(target.full_path), target.identity);
    }
    std::optional<FileOperationOrigin> NavigationDropTarget(const IInspectable& element) const
    {
        const auto* tab = ActiveTab(); if (!tab || tab->search_visible || tab->state->PresentationSnapshot().has_pending) { return {}; }
        auto current = element.try_as<DependencyObject>();
        while (current)
        {
            if (const auto item = current.try_as<TreeViewItem>())
            {
                const auto node = navigation_tree_.NodeFromContainer(item);
                const auto id = node ? NavigationTreeNodeIdFromItem(node.Content()) : std::nullopt;
                const auto* model = id ? navigation_tree_model_.Find(*id) : nullptr;
                if (model && model->target.IsFolder() && !model->reparse_point)
                { return blink::app::FileDropOrigin(blink::app::CaptureFileOperationOrigin(*tab), model->target); }
                return {};
            }
            current = VisualTreeHelper::GetParent(current);
        }
        return {};
    }
    std::optional<FileOperationOrigin> TabDropTarget(const blink::app::TabId id)
    {
        const auto* tab = FindTab(id);
        if (!tab || tab->search_visible || tab->state->PresentationSnapshot().has_pending) { return {}; }
        const auto owner = blink::app::CaptureFileOperationOrigin(*tab);
        if (!owner.location.IsFolder() || !owner.identity.valid) { return {}; }
        return blink::app::FileDropOrigin(owner, owner.location, owner.identity);
    }
    FileOperationOrigin CurrentOperationOrigin() const
    {
        const auto value = file_operations_.Snapshot();
        if (drop_retry_origin_ && (value.kind == blink::app::FileOperationKind::Copy || value.kind == blink::app::FileOperationKind::Move) &&
            value.request.parent_identity == drop_retry_origin_->identity && IsFileOperationOriginCurrent(*drop_retry_origin_)) { return *drop_retry_origin_; }
        return ActiveTab() ? blink::app::CaptureFileOperationOrigin(*ActiveTab()) : FileOperationOrigin{};
    }
    bool OperationTargetVisible(const blink::core::FileIdentity& identity) const
    { const auto origin = CurrentOperationOrigin(); return identity.valid && identity == origin.identity && IsFileOperationOriginCurrent(origin); }
    std::uint64_t SubmitFileDrop(const FileOperationOrigin& origin, const blink::app::ClipboardCopySources& sources, const DWORD effect)
    {
        if (FileDropBusy() || !IsFileOperationOriginCurrent(origin)) { return 0; }
        drop_retry_origin_ = origin;
        if (effect == DROPEFFECT_MOVE)
        {
            std::vector<blink::app::MoveRequest> moves;
            const auto error = blink::app::PrepareMoveDestination(sources, origin.location.FolderPath(), origin.identity, moves);
            if (error) { ShowClipboardCopyError(error); return 0; }
            return StartMoves(origin, std::move(moves));
        }
        blink::app::ClipboardCopySources bound;
        const auto error = blink::app::PrepareCopyDestination(sources, origin.location.FolderPath(), origin.identity, bound);
        if (error) { ShowClipboardCopyError(error); return 0; }
        blink::app::CopyItemRequests items;
        if (const auto* files = std::get_if<std::vector<blink::app::FileCopyRequest>>(&bound))
        { for (const auto& file : *files) { items.emplace_back(file); } }
        else if (const auto* directory = std::get_if<blink::app::DirectoryCopyRequest>(&bound)) { items.emplace_back(*directory); }
        else if (const auto* mixed = std::get_if<blink::app::CopyItemRequests>(&bound)) { items = *mixed; }
        return StartCopyItems(origin, std::move(items));
    }
    void CancelDragTabHover() noexcept
    { if (drag_hover_timer_) { try { drag_hover_timer_.Stop(); } catch (...) {} } drag_hover_tab_ = 0; }
    void ActivateDropTab(const blink::app::TabId id)
    {
        if (id == tab_window_state_.ActiveTabId() || !tab_window_state_.ActivateTab(id)) { return; }
        CloseOperationDialogs();
        const auto found = std::find(tab_item_ids_.begin(), tab_item_ids_.end(), id);
        if (found != tab_item_ids_.end())
        { tab_view_sync_suppressed_ = true; tab_view_.SelectedIndex(static_cast<int>(found - tab_item_ids_.begin())); tab_view_sync_suppressed_ = false; }
        PresentActiveTab();
    }
    void FileDragTabOver(const blink::app::TabId id, const DragEventArgs& args)
    {
        const auto weak = get_weak();
        file_drag_drop_.Over(args, [weak, id]() -> std::optional<FileOperationOrigin> {
            if (const auto app = weak.get()) { return app->TabDropTarget(id); } return {};
        });
        if (id == tab_window_state_.ActiveTabId()) { CancelDragTabHover(); return; }
        if (drag_hover_tab_ == id && drag_hover_timer_ && drag_hover_timer_.IsRunning()) { return; }
        CancelDragTabHover(); drag_hover_tab_ = id;
        if (!drag_hover_timer_)
        {
            drag_hover_timer_ = dispatcher_.CreateTimer(); drag_hover_timer_.Interval(std::chrono::milliseconds(500)); drag_hover_timer_.IsRepeating(false);
            drag_hover_timer_.Tick([weak](auto&&, auto&&) {
                if (const auto app = weak.get(); app && !app->window_style_closed_)
                { const auto id = app->drag_hover_tab_; app->CancelDragTabHover(); if (id) { app->ActivateDropTab(id); } }
            });
        }
        drag_hover_timer_.Start();
    }
    void ConfigureFileDragControl(const ListViewBase& control)
    {
        control.CanDragItems(true); control.CanReorderItems(false); control.AllowDrop(true);
        control.Background(SolidColorBrush(Windows::UI::Colors::Transparent()));
        const auto weak = get_weak();
        control.DragStarting([](auto&&, auto&& args) {
            args.AllowedOperations(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy |
                Windows::ApplicationModel::DataTransfer::DataPackageOperation::Move);
        });
        control.DragItemsStarting([weak](auto&& sender, auto&& args) {
            const auto app = weak.get(); if (!app || app->window_style_closed_ || sender != app->entries_) { args.Cancel(true); return; }
            const auto* tab = app->ActiveTab(); const auto* ui = app->ActiveTabUi();
            if (!tab || !ui || tab->search_visible || !ui->showing_committed_folder || ui->presentation_in_progress ||
                tab->state->PresentationSnapshot().has_pending || app->IsOperationDialogOpen() || !args.Items().Size() || args.Items().Size() > blink::app::MaxClipboardFileCount)
            { args.Cancel(true); return; }
            std::vector<blink::core::FileEntry> entries;
            for (const auto& item : args.Items())
            {
                const auto index = app->RowIndexForItem(item);
                if (!index || *index >= ui->row_models.size() || !ui->row_items_source || *index >= ui->row_items_source.Size() ||
                    ui->row_items_source.GetAt(static_cast<unsigned>(*index)) != item)
                { args.Cancel(true); return; }
                const auto& row = ui->row_models[*index]; blink::core::FileEntry entry;
                entry.name = row.name; entry.full_path = row.full_path; entry.identity = row.identity; entry.is_directory = row.is_directory;
                entries.push_back(std::move(entry));
            }
            const auto error = app->file_drag_drop_.StartSource(args.Data(), blink::app::CaptureFileOperationOrigin(*tab), entries);
            if (FAILED(error)) { args.Cancel(true); app->status_.Text(hstring(app->localization_.GetString(L"DragDrop.SourceUnavailable"))); }
        });
        control.DragItemsCompleted([weak](auto&&, auto&&) {
            if (const auto app = weak.get(); app && !app->window_style_closed_)
            { app->CancelDragTabHover(); app->file_drag_drop_.EndSource(); }
        });
        control.DragOver([weak](auto&&, const DragEventArgs& args) {
            if (const auto app = weak.get(); app && !app->window_style_closed_)
            { const auto hit = args.OriginalSource(); app->file_drag_drop_.Over(args, [weak, hit]() -> std::optional<FileOperationOrigin> {
                if (const auto live = weak.get()) { return live->FileAreaDropTarget(hit); } return {}; }); }
        });
        control.Drop([weak](auto&&, const DragEventArgs& args) {
            if (const auto app = weak.get(); app && !app->window_style_closed_)
            { const auto hit = args.OriginalSource(); app->file_drag_drop_.Drop(args, [weak, hit]() -> std::optional<FileOperationOrigin> {
                if (const auto live = weak.get()) { return live->FileAreaDropTarget(hit); } return {}; }); }
        });
    }
    void InitializeFileDragDrop()
    {
        const auto weak = get_weak(); blink::app::FileDragDropController::Callbacks callbacks;
        callbacks.busy = [weak] { const auto app = weak.get(); return !app || app->FileDropBusy(); };
        callbacks.preview_current = [weak](const FileOperationOrigin& origin) {
            const auto app = weak.get(); return app && blink::app::IsFileOperationOwnerCurrent(origin, app->FindTab(origin.tab_id), app->window_style_closed_);
        };
        callbacks.current = [weak](const FileOperationOrigin& origin) {
            const auto app = weak.get(); return app && blink::app::IsFileOperationOwnerCurrent(origin, app->ActiveTab(), app->window_style_closed_);
        };
        callbacks.submit = [weak](const FileOperationOrigin& origin, const blink::app::ClipboardCopySources& source, DWORD effect) -> std::uint64_t {
            if (const auto app = weak.get()) { return app->SubmitFileDrop(origin, source, effect); } return 0;
        };
        callbacks.snapshot = [weak] { if (const auto app = weak.get()) { return app->file_operations_.Snapshot(); } return blink::app::FileOperationSnapshot{}; };
        callbacks.error = [weak](HRESULT error) {
            if (const auto app = weak.get(); app && !app->window_style_closed_)
            { app->status_.Text(hstring(app->localization_.Format(L"DragDrop.Failed", winrt::hresult_error(error).message().c_str()))); }
        };
        callbacks.changed = [weak] { if (const auto app = weak.get(); app && !app->window_style_closed_) { app->RenderFileOperationStatus(); } };
        callbacks.refresh_source = [weak](const blink::core::FileIdentity& identity) {
            if (const auto app = weak.get(); app && !app->window_style_closed_) { app->RefreshAfterFileOperation(identity); }
        };
        file_drag_drop_.Initialize(localization_, std::move(callbacks));
        window_frame_.AllowDrop(true);
        window_frame_.DragOver([weak](auto&&, const DragEventArgs& args) {
            if (!args.Handled()) { if (const auto app = weak.get()) { app->file_drag_drop_.Over(args, [] { return std::optional<FileOperationOrigin>{}; }); } }
        });
        window_frame_.Drop([](auto&&, const DragEventArgs& args) { if (!args.Handled()) { args.Handled(true); args.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::None); } });
        window_frame_.DragLeave([weak](auto&&, const DragEventArgs& args) {
            if (const auto app = weak.get(); app && !app->window_style_closed_)
            {
                app->file_drag_drop_.Leave(args, app->window_frame_);
                const auto point = args.GetPosition(app->window_frame_);
                if (point.X < 0 || point.Y < 0 || point.X > app->window_frame_.ActualWidth() || point.Y > app->window_frame_.ActualHeight()) { app->CancelDragTabHover(); }
            }
        });
        navigation_tree_.AllowDrop(true);
        navigation_tree_.DragOver([weak](auto&&, const DragEventArgs& args) {
            if (const auto app = weak.get()) { const auto hit = args.OriginalSource(); app->file_drag_drop_.Over(args, [weak, hit]() -> std::optional<FileOperationOrigin> {
                if (const auto live = weak.get()) { return live->NavigationDropTarget(hit); } return {}; }); }
        });
        navigation_tree_.Drop([weak](auto&&, const DragEventArgs& args) {
            if (const auto app = weak.get()) { const auto hit = args.OriginalSource(); app->file_drag_drop_.Drop(args, [weak, hit]() -> std::optional<FileOperationOrigin> {
                if (const auto live = weak.get()) { return live->NavigationDropTarget(hit); } return {}; }); }
        });
    }

    void PublishSelectedFileCopy(const blink::app::ClipboardTransferIntent intent = blink::app::ClipboardTransferIntent::Copy)
    {
        if (ActiveState().PresentationSnapshot().selected_count > blink::app::MaxClipboardFileCount)
        { status_.Text(hstring(localization_.GetString(L"Clipboard.TooManyFiles"))); return; }
        const auto sources = ActiveState().SelectedCommittedRows(blink::app::MaxClipboardFileCount);
        if (!sources || sources->empty() || std::any_of(sources->begin(), sources->end(), [&sources](const auto& source) {
                return (source.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            }))
        { status_.Text(hstring(localization_.GetString(L"Clipboard.CopyLimited"))); return; }
        const auto origin = blink::app::CaptureFileOperationOrigin(*ActiveTab());
        blink::app::ClipboardCopySources expected;
        std::vector<std::wstring> paths;
        paths.reserve(sources->size());
        const auto parent = origin.location.FolderPath();
        const bool has_directory = std::any_of(sources->begin(), sources->end(), [](const auto& source) { return source.is_directory; });
        if (sources->size() == 1 && has_directory)
        {
            blink::app::DirectoryCopyRequest request;
            request.source_parent = parent; request.source_parent_identity = origin.identity; request.source = sources->front();
            expected = std::move(request);
        }
        else if (has_directory)
        {
            blink::app::CopyItemRequests items; items.reserve(sources->size());
            for (const auto& source : *sources)
            {
                blink::app::FileCopyRequest request;
                request.source_parent = parent; request.source_parent_identity = origin.identity; request.source = source;
                if (source.is_directory) { items.emplace_back(blink::app::DirectoryCopyProjection(request)); }
                else { items.emplace_back(std::move(request)); }
            }
            expected = std::move(items);
        }
        else
        {
            std::vector<blink::app::FileCopyRequest> requests; requests.reserve(sources->size());
            for (const auto& source : *sources)
            {
                blink::app::FileCopyRequest request;
                request.source_parent = parent; request.source_parent_identity = origin.identity; request.source = source;
                requests.push_back(std::move(request));
            }
            expected = std::move(requests);
        }
        for (const auto& source : *sources) { paths.push_back(blink::core::JoinPath(parent, source.name)); }
        HWND owner{};
        check_hresult(window_.as<::IWindowNative>()->get_WindowHandle(&owner));
        clipboard_work_pending_ = true;
        clipboard_cancellation_ = std::make_shared<std::atomic_bool>(false);
        const auto cancellation = clipboard_cancellation_;
        status_.Text(hstring(localization_.GetString(L"Clipboard.Preparing"))); RenderFileOperationStatus();
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        try
        {
            clipboard_work_ = Windows::System::Threading::ThreadPool::RunAsync(
                [weak, dispatcher, owner, origin, cancellation, intent, expected = std::move(expected), paths = std::move(paths)](auto&&) {
                    DWORD sequence{}, error = ERROR_GEN_FAILURE;
                    blink::app::ClipboardCopySources validated;
                    try
                    {
                        error = blink::app::CaptureClipboardCopySources(paths, validated, cancellation);
                        if (error == ERROR_SUCCESS && !blink::app::SameCapturedCopySources(expected, validated))
                        { error = ERROR_FILE_INVALID; }
                        if (error == ERROR_SUCCESS && cancellation->load()) { error = ERROR_CANCELLED; }
                        if (error == ERROR_SUCCESS) { error = blink::app::PublishClipboardFilesTransfer(owner, paths, intent, sequence); }
                    }
                    catch (const std::bad_alloc&) { error = ERROR_NOT_ENOUGH_MEMORY; }
                    catch (...) { error = ERROR_GEN_FAILURE; }
                    (void)dispatcher.TryEnqueue([weak, origin, cancellation, captured = std::move(validated),
                        published_paths = std::move(paths), sequence, error, intent]() mutable {
                        if (auto app = weak.get(); app && !app->window_style_closed_)
                        {
                            app->clipboard_work_pending_ = false; app->RenderFileOperationStatus();
                            if (error == ERROR_SUCCESS)
                            {
                                const auto count = published_paths.size();
                                // Publication already happened: preserve original IDs even if a
                                // departure/cancel raced after the clipboard call began.
                                app->clipboard_copy_cache_ = { std::move(captured), std::move(published_paths), sequence, intent };
                                if (cancellation->load() || !app->IsFileOperationOriginCurrent(origin))
                                { app->ShowClipboardCopyError(ERROR_CANCELLED); return; }
                                app->status_.Text(hstring(app->localization_.Format(intent == blink::app::ClipboardTransferIntent::Move ? L"Clipboard.CutItems" : L"Clipboard.CopiedItems", std::to_wstring(count))));
                            }
                            else { app->ShowClipboardCopyError(error); }
                        }
                    });
                });
        }
        catch (...) { clipboard_work_pending_ = false; RenderFileOperationStatus(); status_.Text(hstring(localization_.GetString(L"Clipboard.Unavailable"))); }
    }

    void PublishSelectedPaths(const bool parent_only)
    {
        const auto rows = ActiveState().SelectedCommittedRows(blink::app::MaxClipboardFileCount);
        if (!rows || rows->empty())
        {
            status_.Text(hstring(localization_.GetString(ActiveState().PresentationSnapshot().selected_count >
                blink::app::MaxClipboardFileCount ? L"Clipboard.TooManyFiles" : L"Status.CommandSelectionRequired")));
            return;
        }
        const auto parent = ActiveState().PresentationSnapshot().committed_location.FolderPath();
        std::wstring text;
        if (parent_only) { text = parent; }
        else
        {
            for (const auto& row : *rows)
            {
                if (!text.empty()) { text += L"\r\n"; }
                text += blink::core::JoinPath(parent, row.name);
            }
        }
        HWND owner{}; check_hresult(window_.as<::IWindowNative>()->get_WindowHandle(&owner));
        const auto origin = blink::app::CaptureFileOperationOrigin(*ActiveTab());
        clipboard_work_pending_ = true;
        clipboard_cancellation_ = std::make_shared<std::atomic_bool>(false);
        const auto cancellation = clipboard_cancellation_;
        RenderFileOperationStatus();
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        try
        {
            clipboard_work_ = Windows::System::Threading::ThreadPool::RunAsync(
                [weak, dispatcher, owner, origin, cancellation, parent_only, text = std::move(text)](auto&&) {
                    DWORD error = ERROR_CANCELLED, sequence{};
                    try
                    {
                        if (!cancellation->load()) { error = blink::app::PublishClipboardText(owner, text, sequence); }
                    }
                    catch (const std::bad_alloc&) { error = ERROR_NOT_ENOUGH_MEMORY; }
                    catch (...) { error = ERROR_GEN_FAILURE; }
                    (void)dispatcher.TryEnqueue([weak, origin, cancellation, parent_only, error] {
                        if (const auto app = weak.get(); app && !app->window_style_closed_)
                        {
                            app->clipboard_work_pending_ = false; app->RenderFileOperationStatus();
                            if (error) { app->ShowClipboardCopyError(error); return; }
                            app->clipboard_copy_cache_ = {};
                            if (cancellation->load() || !app->IsFileOperationOriginCurrent(origin)) { return; }
                            app->status_.Text(hstring(app->localization_.GetString(parent_only ? L"Search.ParentCopied" : L"Search.PathCopied")));
                        }
                    });
                });
        }
        catch (...) { clipboard_work_pending_ = false; RenderFileOperationStatus(); ShowClipboardCopyError(ERROR_GEN_FAILURE); }
    }

    void PasteClipboardFile(const FileOperationOrigin& origin)
    {
        if (!IsFileOperationOriginCurrent(origin)) { return; }
        clipboard_work_pending_ = true;
        clipboard_cancellation_ = std::make_shared<std::atomic_bool>(false);
        const auto cancellation = clipboard_cancellation_;
        status_.Text(hstring(localization_.GetString(L"Clipboard.Preparing"))); RenderFileOperationStatus();
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        const auto own = clipboard_copy_cache_;
        try
        {
            clipboard_work_ = Windows::System::Threading::ThreadPool::RunAsync(
                [weak, dispatcher, origin, cancellation, own](auto&&) {
                    blink::app::ClipboardCopySources requests;
                    blink::app::ClipboardFilePaths payload;
                    DWORD error = ERROR_GEN_FAILURE;
                    try
                    {
                        payload = blink::app::ReadClipboardFilesTransfer(); error = payload.error;
                        if (error == ERROR_SUCCESS)
                        {
                            // Already-submitted MOVE data is resolved through its
                            // existing queue, before touching stale source paths.
                            if (own.sequence && own.sequence == payload.sequence &&
                                own.intent == blink::app::ClipboardTransferIntent::Move && own.move_submitted)
                            { error = blink::app::PrepareClipboardPasteSources(payload, own, own.sources, requests); }
                            else
                            {
                                blink::app::ClipboardCopySources captured;
                                error = blink::app::CaptureClipboardCopySources(payload.paths, captured, cancellation);
                                if (!error) { error = blink::app::PrepareClipboardPasteSources(payload, own, captured, requests); }
                            }
                        }
                    }
                    catch (const std::bad_alloc&) { error = ERROR_NOT_ENOUGH_MEMORY; }
                    catch (...) { error = ERROR_GEN_FAILURE; requests.emplace<std::monostate>(); }
                    (void)dispatcher.TryEnqueue([weak, origin, cancellation, requests = std::move(requests), payload = std::move(payload), error]() mutable {
                        if (auto app = weak.get(); app && !app->window_style_closed_)
                        {
                            app->clipboard_work_pending_ = false; app->RenderFileOperationStatus();
                            if (cancellation->load()) { app->ShowClipboardCopyError(ERROR_CANCELLED); return; }
                            if (!app->IsFileOperationOriginCurrent(origin))
                            { app->status_.Text(hstring(app->localization_.GetString(L"Status.CommandStale"))); return; }
                            if (error != ERROR_SUCCESS) { app->ShowClipboardCopyError(error); return; }
                            if (payload.intent == blink::app::ClipboardTransferIntent::Move)
                            {
                                std::vector<blink::app::MoveRequest> moves;
                                const auto prepared = blink::app::PrepareMoveDestination(requests,
                                    origin.location.FolderPath(), origin.identity, moves);
                                if (prepared) { app->ShowClipboardCopyError(prepared); return; }
                                if (app->StartMoves(origin, std::move(moves)))
                                {
                                    // Keep the original sequence/IDs even for external
                                    // CUT data. Partial completion never replays it as
                                    // a freshly captured selection on another Paste.
                                    app->clipboard_copy_cache_ = { std::move(requests), std::move(payload.paths), payload.sequence,
                                        blink::app::ClipboardTransferIntent::Move, true };
                                }
                                return;
                            }
                            blink::app::ClipboardCopySources bound;
                            const auto prepared = blink::app::PrepareCopyDestination(requests,
                                origin.location.FolderPath(), origin.identity, bound);
                            if (prepared != ERROR_SUCCESS) { app->ShowClipboardCopyError(prepared); return; }
                            if (auto* files = std::get_if<std::vector<blink::app::FileCopyRequest>>(&bound))
                            { app->StartFileCopies(origin, std::move(*files)); }
                            else if (auto* directory = std::get_if<blink::app::DirectoryCopyRequest>(&bound))
                            { app->StartDirectoryCopy(origin, std::move(*directory)); }
                            else if (auto* items = std::get_if<blink::app::CopyItemRequests>(&bound))
                            { app->StartCopyItems(origin, std::move(*items)); }
                        }
                    });
                });
        }
        catch (...) { clipboard_work_pending_ = false; RenderFileOperationStatus(); status_.Text(hstring(localization_.GetString(L"Clipboard.Unavailable"))); }
    }

    void ShowClipboardCopyError(const DWORD error)
    {
        status_.Text(hstring(error == ERROR_INVALID_STATE ? localization_.GetString(L"Move.AlreadySubmitted") :
            error == ERROR_NOT_SAME_DEVICE ? localization_.GetString(L"Move.CrossVolume") :
            error == ERROR_NOT_SUPPORTED ? localization_.GetString(L"Clipboard.CopyLimited") :
            error == ERROR_CANCELLED ? localization_.GetString(L"Clipboard.Cancelled") :
            error == ERROR_FILE_INVALID ? localization_.GetString(L"Move.TargetChanged") :
            localization_.Format(L"Clipboard.Failed", blink::core::Win32ErrorMessage(error))));
    }

    void RenderDirectoryCopyStatus(const blink::app::FileOperationSnapshot& snapshot)
    {
        using Stage = blink::app::FileOperationStage;
        directory_copy_history_.Observe(snapshot);
        const auto* result = snapshot.directory_copy_result ? &*snapshot.directory_copy_result : nullptr;
        const auto& progress = snapshot.directory_copy_progress;
        const auto path = result && !result->path.empty() ? result->path :
            blink::core::JoinPath(snapshot.request.parent, snapshot.request.name);
        const auto key = snapshot.stage == Stage::Skipped ? L"DirectoryCopy.Skipped" :
            snapshot.stage == Stage::Completed ? L"DirectoryCopy.Completed" :
            snapshot.stage == Stage::Cancelled ? L"DirectoryCopy.Cancelled" :
            snapshot.stage == Stage::Cancelling ? L"DirectoryCopy.Cancelling" :
            snapshot.stage == Stage::Failed ? L"DirectoryCopy.Failed" :
            !progress.current_source_path.empty() ? L"DirectoryCopy.Running" : L"DirectoryCopy.Scanning";
        auto text = localization_.Format(key, path);
        const auto files = result ? result->files_copied : progress.files_copied;
        const auto directories = result ? result->directories_created : progress.directories_created;
        text += L" " + localization_.Format(L"DirectoryCopy.Counts", std::vector<std::wstring>{
            std::to_wstring(files), std::to_wstring(directories) });
        const auto files_skipped = result ? result->files_skipped : progress.files_skipped;
        const auto directories_skipped = result ? result->directories_skipped : progress.directories_skipped;
        if (files_skipped || directories_skipped)
        { text += L" " + localization_.Format(L"DirectoryCopy.SkippedCounts", std::vector<std::wstring>{
            std::to_wstring(files_skipped), std::to_wstring(directories_skipped) }); }
        if (snapshot.directory_conflict)
        {
            const auto& pending = snapshot.directory_conflict->request;
            text = localization_.Format(L"DirectoryCopy.WaitingForConflict",
                blink::core::JoinPath(pending.destination_parent, pending.destination_name)) + L" " + text;
        }
        if (snapshot.stage == Stage::Failed)
        { text += L" " + blink::core::Win32ErrorMessage(result ? result->error : snapshot.result.error); }
        if (snapshot.stage == Stage::Running && progress.current_file.total_bytes)
        { text += localization_.Format(L"Operation.CopyBytes", std::vector<std::wstring>{
            std::to_wstring(progress.current_file.transferred_bytes), std::to_wstring(progress.current_file.total_bytes) }); }
        auto detail = text;
        if (!progress.current_destination_path.empty()) { detail += L"\n" + progress.current_destination_path; }
        if (result && !result->failure_path.empty())
        { detail += L"\n" + localization_.Format(L"DirectoryCopy.FailurePath", result->failure_path); }
        if (directory_copy_history_.Count())
        {
            const auto note = localization_.Format(L"DirectoryCopy.PartialHistory", std::to_wstring(directory_copy_history_.Count()));
            text += L" " + note; detail += L"\n" + note;
            for (const auto& previous : directory_copy_history_.Paths()) { detail += L"\n" + previous; }
            if (directory_copy_history_.Count() > directory_copy_history_.Paths().size())
            { detail += L"\n" + localization_.GetString(L"DirectoryCopy.HistoryLimited"); }
        }
        operation_text_.Text(hstring(text)); ToolTipService::SetToolTip(operation_text_, winrt::box_value(hstring(detail)));
        const bool running = snapshot.stage == Stage::Queued || snapshot.stage == Stage::Running || snapshot.stage == Stage::Cancelling;
        operation_cancel_.Visibility(running ? Visibility::Visible : Visibility::Collapsed);
        operation_cancel_.IsEnabled(snapshot.stage != Stage::Cancelling);
        const auto* tab = ActiveTab();
        const bool partial_merge = snapshot.directory_copy_request && result && result->partial_tree_left &&
            snapshot.directory_copy_request->conflict_policy == blink::app::FileCopyConflictPolicy::Merge;
        const bool retryable = !partial_merge && (snapshot.stage == Stage::Failed || snapshot.stage == Stage::Cancelled) &&
            snapshot.directory_copy_request && tab && !tab->search_visible &&
            tab->state->CommittedDirectoryIdentity().valid &&
            tab->state->CommittedDirectoryIdentity() == snapshot.request.parent_identity;
        operation_retry_.Visibility(retryable ? Visibility::Visible : Visibility::Collapsed);
        operation_retry_.IsEnabled(!file_operations_.IsBusy() && !file_operations_.HasPendingBatch() &&
            copy_conflict_wait_operation_id_ == 0);
    }

    void DirectoryCopyActionEnded(const FileOperationOrigin& origin, const std::uint64_t operation_id,
        std::optional<blink::app::DirectoryCopyRequest> conflict = {})
    {
        if (window_style_closed_ || file_operations_.Snapshot().operation_id != operation_id) { return; }
        const auto action = file_operations_.CurrentAction();
        if (action && action.Status() == winrt::Windows::Foundation::AsyncStatus::Started)
        {
            if (copy_conflict_wait_operation_id_ == operation_id) { return; }
            copy_conflict_wait_operation_id_ = operation_id;
            const auto weak = get_weak();
            copy_conflict_wait_ = AwaitNativeOperationEnd(action,
                [weak, origin, operation_id, conflict = std::move(conflict)](HRESULT) mutable {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    {
                        const bool may_show_conflict = app->copy_conflict_wait_operation_id_ == operation_id;
                        if (may_show_conflict) { app->copy_conflict_wait_operation_id_ = 0; }
                        else { conflict.reset(); }
                        app->DirectoryCopyActionEnded(origin, operation_id, std::move(conflict));
                    }
                });
            return;
        }
        RenderFileOperationStatus();
        if (conflict && IsFileOperationOriginCurrent(origin))
        { ShowDirectoryCopyConflictDialog(origin, std::move(*conflict), operation_id); }
    }

    void ShowDirectoryMergeConflictDialog(const FileOperationOrigin& origin, const std::uint64_t operation_id)
    {
        const auto current = file_operations_.Snapshot();
        const bool waiting = current.operation_id == operation_id && current.directory_conflict &&
            current.stage == blink::app::FileOperationStage::Running && !current.cancel_requested;
        if (directory_merge_dialog_operation_id_ && (!waiting ||
            directory_merge_dialog_operation_id_ != operation_id ||
            directory_merge_dialog_conflict_id_ != current.directory_conflict_id))
        {
            copy_conflict_dialog_.Close();
            directory_merge_dialog_operation_id_ = directory_merge_dialog_conflict_id_ = 0;
        }
        if (!waiting || (directory_merge_dialog_operation_id_ == operation_id &&
            directory_merge_dialog_conflict_id_ == current.directory_conflict_id)) { return; }
        const auto conflict_id = current.directory_conflict_id;
        if (!IsFileOperationOriginCurrent(origin) || IsOperationDialogOpen() || clipboard_work_pending_)
        {
            (void)file_operations_.ResolveDirectoryConflict(operation_id, conflict_id, blink::app::FileCopyConflictPolicy::Cancel);
            return;
        }
        const auto& conflict = *current.directory_conflict;
        const auto& request = conflict.request;
        const auto source = blink::core::JoinPath(request.source_parent, request.source.name);
        const auto destination = blink::core::JoinPath(request.destination_parent, request.destination_name);
        const bool directory = request.source.is_directory;
        const bool can_replace = !directory && !conflict.destination_is_directory;
        const bool can_merge = directory && conflict.destination_is_directory;
        directory_merge_dialog_operation_id_ = operation_id;
        directory_merge_dialog_conflict_id_ = conflict_id;
        const auto weak = get_weak();
        try
        {
            const bool shown = copy_conflict_dialog_.Show(root_.XamlRoot(), localization_, source, destination, can_replace,
                [weak, origin, operation_id, conflict_id](blink::app::FileCopyConflictPolicy policy) {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    {
                        if (app->directory_merge_dialog_operation_id_ == operation_id &&
                            app->directory_merge_dialog_conflict_id_ == conflict_id)
                        { app->directory_merge_dialog_operation_id_ = app->directory_merge_dialog_conflict_id_ = 0; }
                        if (!app->IsFileOperationOriginCurrent(origin)) { policy = blink::app::FileCopyConflictPolicy::Cancel; }
                        (void)app->file_operations_.ResolveDirectoryConflict(operation_id, conflict_id, policy);
                        app->RenderFileOperationStatus();
                    }
                }, [weak, operation_id, conflict_id](HRESULT) {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    {
                        if (app->directory_merge_dialog_operation_id_ == operation_id &&
                            app->directory_merge_dialog_conflict_id_ == conflict_id)
                        { app->directory_merge_dialog_operation_id_ = app->directory_merge_dialog_conflict_id_ = 0; }
                        (void)app->file_operations_.ResolveDirectoryConflict(operation_id, conflict_id, blink::app::FileCopyConflictPolicy::Cancel);
                        app->status_.Text(hstring(app->localization_.GetString(L"NewFolder.DialogFailed")));
                    }
                }, directory, current.kind == blink::app::FileOperationKind::Move, can_merge);
            if (!shown)
            {
                directory_merge_dialog_operation_id_ = directory_merge_dialog_conflict_id_ = 0;
                (void)file_operations_.ResolveDirectoryConflict(operation_id, conflict_id, blink::app::FileCopyConflictPolicy::Cancel);
            }
        }
        catch (...)
        {
            copy_conflict_dialog_.Close();
            directory_merge_dialog_operation_id_ = directory_merge_dialog_conflict_id_ = 0;
            (void)file_operations_.ResolveDirectoryConflict(operation_id, conflict_id, blink::app::FileCopyConflictPolicy::Cancel);
            status_.Text(hstring(localization_.GetString(L"NewFolder.DialogFailed")));
        }
    }

    blink::app::FileOperationCoordinator::PublishCallback DirectoryCopyPublication(const FileOperationOrigin& origin)
    {
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        return [weak, dispatcher, origin](blink::app::FileOperationSnapshot snapshot) {
            const auto id = snapshot.operation_id;
            const auto parent = snapshot.request.parent_identity;
            const bool changed = blink::app::DirectoryCopyNeedsRefresh(snapshot);
            const bool terminal = snapshot.directory_copy_result.has_value();
            const bool awaiting_conflict = snapshot.directory_conflict.has_value();
            std::optional<blink::app::DirectoryCopyRequest> conflict;
            const bool same_source_root = snapshot.directory_copy_request &&
                snapshot.directory_copy_request->source_parent_identity == snapshot.directory_copy_request->destination_parent_identity &&
                CompareStringOrdinal(snapshot.directory_copy_request->source.name.c_str(), -1,
                    snapshot.directory_copy_request->destination_name.c_str(), -1, TRUE) == CSTR_EQUAL;
            if (snapshot.stage == blink::app::FileOperationStage::Failed && snapshot.directory_copy_request &&
                snapshot.directory_copy_result && !snapshot.directory_copy_result->partial_tree_left &&
                !snapshot.directory_copy_result->directories_created && !snapshot.directory_copy_result->files_copied &&
                (snapshot.result.error == ERROR_ALREADY_EXISTS || snapshot.result.error == ERROR_FILE_EXISTS ||
                    (snapshot.result.error == ERROR_INVALID_PARAMETER && same_source_root)))
            { conflict = std::move(snapshot.directory_copy_request); }
            const bool queued = dispatcher.TryEnqueue([weak, origin, id, parent, changed, terminal, conflict = std::move(conflict)]() mutable {
                if (auto app = weak.get(); app && !app->window_style_closed_)
                {
                    if (changed) { app->RefreshAfterFileOperation(parent); }
                    if (app->file_operations_.Snapshot().operation_id != id) { return; }
                    app->RenderFileOperationStatus();
                    app->ShowDirectoryMergeConflictDialog(origin, id);
                    if (terminal) { app->DirectoryCopyActionEnded(origin, id, std::move(conflict)); }
                }
            });
            if (!queued && awaiting_conflict) { throw winrt::hresult_error(E_ABORT); }
        };
    }

    void StartDirectoryCopy(const FileOperationOrigin& origin, blink::app::DirectoryCopyRequest request, bool retry = false)
    {
        if (!IsFileOperationOriginCurrent(origin))
        { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); return; }
        if (request.destination_parent_identity != origin.identity)
        { status_.Text(hstring(localization_.GetString(L"Copy.TargetChanged"))); return; }
        if (file_operations_.IsBusy() || file_operations_.HasPendingBatch() || clipboard_work_pending_ || IsOperationDialogOpen())
        { status_.Text(hstring(localization_.GetString(L"Status.CommandBusy"))); return; }
        request.destination_parent = origin.location.FolderPath();
        if (retry) { directory_copy_history_.Observe(file_operations_.Snapshot()); }
        const auto id = file_operations_.CopyDirectory(std::move(request), DirectoryCopyPublication(origin));
        if (!id) { status_.Text(hstring(localization_.GetString(L"Status.CommandBusy"))); }
        else if (!retry) { directory_copy_history_.Reset(); }
        RenderFileOperationStatus();
    }

    void ShowDirectoryCopyConflictDialog(const FileOperationOrigin& origin, blink::app::DirectoryCopyRequest request,
        const std::uint64_t operation_id)
    {
        if (!IsFileOperationOriginCurrent(origin) || file_operations_.IsBusy() || file_operations_.HasPendingBatch() ||
            clipboard_work_pending_ || copy_conflict_wait_operation_id_ != 0 || IsOperationDialogOpen()) { return; }
        const auto current = file_operations_.Snapshot();
        if (current.operation_id != operation_id || current.kind != blink::app::FileOperationKind::DirectoryCopy ||
            current.stage != blink::app::FileOperationStage::Failed || !current.directory_copy_result ||
            current.directory_copy_result->partial_tree_left || current.directory_copy_result->directories_created ||
            current.directory_copy_result->files_copied || request.destination_parent_identity != origin.identity) { return; }
        const auto source = blink::core::JoinPath(request.source_parent, request.source.name);
        const auto destination = blink::core::JoinPath(origin.location.FolderPath(), request.destination_name);
        const bool can_merge = request.source_parent_identity != request.destination_parent_identity ||
            CompareStringOrdinal(request.source.name.c_str(), -1, request.destination_name.c_str(), -1, TRUE) != CSTR_EQUAL;
        const auto weak = get_weak();
        try
        {
            (void)copy_conflict_dialog_.Show(root_.XamlRoot(), localization_, source, destination, false,
                [weak, origin, request = std::move(request), operation_id](blink::app::FileCopyConflictPolicy policy) mutable {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    {
                        if (!app->IsFileOperationOriginCurrent(origin) || app->file_operations_.Snapshot().operation_id != operation_id)
                        { app->status_.Text(hstring(app->localization_.GetString(L"Status.CommandStale"))); return; }
                        if (policy != blink::app::FileCopyConflictPolicy::Skip && policy != blink::app::FileCopyConflictPolicy::KeepBoth &&
                            policy != blink::app::FileCopyConflictPolicy::Cancel && policy != blink::app::FileCopyConflictPolicy::Merge) { return; }
                        request.conflict_policy = policy;
                        app->StartDirectoryCopy(origin, std::move(request), true);
                    }
                }, [weak](HRESULT) {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    { app->status_.Text(hstring(app->localization_.GetString(L"NewFolder.DialogFailed"))); }
                }, true, false, can_merge);
        }
        catch (...) { status_.Text(hstring(localization_.GetString(L"NewFolder.DialogFailed"))); }
    }

    void ShowDirectoryCopyNameDialog(const FileOperationOrigin& origin, blink::app::DirectoryCopyRequest request,
        const std::uint64_t operation_id)
    {
        if (!IsFileOperationOriginCurrent(origin) || file_operations_.IsBusy() || file_operations_.HasPendingBatch() ||
            clipboard_work_pending_ || copy_conflict_wait_operation_id_ != 0 || IsOperationDialogOpen()) { return; }
        const auto current = file_operations_.Snapshot();
        if (current.operation_id != operation_id || current.kind != blink::app::FileOperationKind::DirectoryCopy ||
            (current.stage != blink::app::FileOperationStage::Failed && current.stage != blink::app::FileOperationStage::Cancelled) ||
            request.destination_parent_identity != origin.identity) { return; }
        directory_copy_history_.Observe(current);
        auto message = localization_.GetString(L"DirectoryCopy.RetryNewName");
        if (current.directory_copy_result && current.directory_copy_result->partial_tree_left)
        { message += L" " + localization_.Format(L"DirectoryCopy.PartialPath", current.directory_copy_result->path); }
        const auto weak = get_weak();
        try
        {
            const auto name = request.destination_name;
            (void)folder_name_dialog_.Show(root_.XamlRoot(), localization_, origin.location.FolderPath(), name,
                std::move(message), [weak, origin, request = std::move(request), operation_id](std::wstring value) mutable {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    {
                        if (!app->IsFileOperationOriginCurrent(origin) || app->file_operations_.Snapshot().operation_id != operation_id)
                        { app->status_.Text(hstring(app->localization_.GetString(L"Status.CommandStale"))); return; }
                        request.destination_name = std::move(value);
                        request.conflict_policy = blink::app::FileCopyConflictPolicy::Fail;
                        app->StartDirectoryCopy(origin, std::move(request), true);
                    }
                }, [weak](HRESULT) {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    { app->status_.Text(hstring(app->localization_.GetString(L"NewFolder.DialogFailed"))); }
                }, false, true, true);
        }
        catch (...) { status_.Text(hstring(localization_.GetString(L"NewFolder.DialogFailed"))); }
    }

    void StartFileCopy(const FileOperationOrigin& origin, blink::app::FileCopyRequest request)
    {
        std::vector<blink::app::FileCopyRequest> requests;
        requests.push_back(std::move(request));
        StartFileCopies(origin, std::move(requests));
    }

    blink::app::FileOperationCoordinator::PublishCallback FileCopyPublication(const FileOperationOrigin& origin)
    {
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        return file_drag_drop_.Publication([weak, dispatcher, origin](blink::app::FileOperationSnapshot snapshot) {
            // The coordinator owns the full batch. Retain only this event's
            // refresh facts and, for a conflict, the current file request in
            // the dispatcher queue rather than another copy of all items.
            const auto operation_id = snapshot.operation_id;
            const auto parent_identity = snapshot.request.parent_identity;
            const bool has_batch = snapshot.copy_batch.has_value();
            const auto copied_count = has_batch ? snapshot.copy_batch->copied_count : 0;
            const auto partial_count = has_batch ? snapshot.copy_batch->partial_count : 0;
            const bool changed = snapshot.result.committed || snapshot.result.partial_file_left ||
                (snapshot.directory_copy_result && (snapshot.directory_copy_result->copied || snapshot.directory_copy_result->partial_tree_left));
            const bool awaiting_conflict = snapshot.directory_conflict.has_value();
            std::optional<blink::app::FileCopyRequest> conflict;
            if (blink::app::CopyQueueHasRootConflict(snapshot)) { conflict = std::move(snapshot.copy_request); }
            const bool queued = dispatcher.TryEnqueue([weak, origin, operation_id, parent_identity, has_batch,
                copied_count, partial_count, changed, conflict = std::move(conflict)] {
                if (auto app = weak.get(); app && !app->window_style_closed_)
                {
                    if (has_batch)
                    {
                        if (app->copy_refresh_operation_id_ != operation_id)
                        {
                            app->copy_refresh_operation_id_ = operation_id;
                            app->copy_refreshed_count_ = app->copy_partial_refreshed_count_ = 0;
                        }
                        // A second failed attempt for the same item can leave
                        // another path while its history-based partial count
                        // stays unchanged. Preserve this event's actual effect.
                        if (changed || copied_count > app->copy_refreshed_count_ || partial_count > app->copy_partial_refreshed_count_)
                        {
                            app->copy_refreshed_count_ = copied_count;
                            app->copy_partial_refreshed_count_ = partial_count;
                            app->RefreshAfterFileOperation(parent_identity);
                        }
                    }
                    else if (changed) { app->RefreshAfterFileOperation(parent_identity); }
                    const auto current = app->file_operations_.Snapshot();
                    if (current.operation_id == operation_id)
                    {
                        app->RenderFileOperationStatus();
                        app->ShowDirectoryMergeConflictDialog(origin, operation_id);
                        if (conflict && current.stage == blink::app::FileOperationStage::Failed &&
                            (!current.copy_batch || current.copy_batch->resumable))
                        { app->ShowCopyConflictDialog(origin, *conflict, operation_id); }
                    }
                }
            });
            if (!queued && awaiting_conflict) { throw winrt::hresult_error(E_ABORT); }
        });
    }

    std::wstring MoveErrorText(const DWORD error) const
    {
        if (error == ERROR_NOT_SAME_DEVICE) { return localization_.GetString(L"Move.CrossVolume"); }
        if (error == ERROR_FILE_INVALID) { return localization_.GetString(L"Move.TargetChanged"); }
        if (error == ERROR_NOT_SUPPORTED) { return localization_.GetString(L"Move.Unsupported"); }
        return FileOperationErrorText(error);
    }

    void RenderMoveStatus(const blink::app::FileOperationSnapshot& value)
    {
        using Stage = blink::app::FileOperationStage;
        const auto* batch = value.move_batch ? &*value.move_batch : nullptr;
        const auto key = blink::app::MoveSummaryKey(value);
        auto text = localization_.GetString(key);
        if (batch)
        {
            text += L" " + localization_.Format(L"Move.Summary", std::vector<std::wstring>{
                std::to_wstring(batch->requested_count), std::to_wstring(batch->moved_count), std::to_wstring(batch->unchanged_count),
                std::to_wstring(batch->skipped_count), std::to_wstring(batch->failed_count),
                std::to_wstring(batch->cancelled_count), std::to_wstring(batch->remaining_count) });
            if (batch->current_index < batch->items.size())
            {
                const auto path = value.move_result && !value.move_result->path.empty() ? value.move_result->path :
                    blink::core::JoinPath(value.request.parent, value.request.name);
                text += L" " + localization_.Format(L"Move.Current", std::vector<std::wstring>{ std::to_wstring(batch->current_index + 1), path });
            }
            if (batch->warning_count) { text += L" " + localization_.Format(L"Move.Warning", std::to_wstring(batch->warning_count)); }
            if (batch->partial_count) { text += L" " + localization_.Format(L"Move.Partial", std::to_wstring(batch->partial_count)); }
        }
        if (value.stage == Stage::Running && value.move_progress.copy_progress.total_bytes)
        { text += localization_.Format(L"Operation.CopyBytes", std::vector<std::wstring>{
            std::to_wstring(value.move_progress.copy_progress.transferred_bytes), std::to_wstring(value.move_progress.copy_progress.total_bytes) }); }
        const auto directory_counts = [this](const std::uint64_t files, const std::uint64_t directories,
            const std::uint64_t removed_files, const std::uint64_t removed_directories) {
            return localization_.Format(L"Move.DirectoryProgress", std::vector<std::wstring>{ std::to_wstring(files),
                std::to_wstring(directories), std::to_wstring(removed_files), std::to_wstring(removed_directories) });
        };
        if (value.move_result && value.move_result->directory_copy_result)
        {
            const auto& result = *value.move_result;
            text += L" " + directory_counts(result.directory_copy_result->files_copied, result.directory_copy_result->directories_created,
                result.source_files_removed, result.source_directories_removed);
            if (result.merged && (result.files_skipped || result.directories_skipped))
            { text += L" " + localization_.Format(L"Move.MergeSkipped", std::vector<std::wstring>{
                std::to_wstring(result.files_skipped), std::to_wstring(result.directories_skipped) }); }
        }
        else if (value.move_progress.directory)
        {
            const auto& p = value.move_progress;
            text += L" " + directory_counts(p.files_copied, p.directories_created, p.source_files_removed, p.source_directories_removed);
        }
        if (value.directory_conflict)
        { text += L" " + localization_.Format(L"DirectoryCopy.WaitingForConflict",
            blink::core::JoinPath(value.directory_conflict->request.destination_parent, value.directory_conflict->request.destination_name)); }
        if (value.stage == Stage::Failed) { text += L" " + MoveErrorText(value.result.error); }
        auto detail = text;
        if (batch)
        {
            for (const auto& item : batch->items)
            {
                const auto partial = item.retained_result ? &*item.retained_result :
                    blink::app::MoveHasPartialDestination(item.result) ? &item.result : nullptr;
                if (partial && detail.size() + partial->path.size() + partial->source_path.size() + 512 < 65536)
                {
                    detail += L"\n" + partial->source_path + L" → " + partial->path;
                    if (partial->directory_copy_result)
                    {
                        const auto& tree = *partial->directory_copy_result;
                        detail += L" " + directory_counts(tree.files_copied, tree.directories_created,
                            partial->source_files_removed, partial->source_directories_removed);
                        if (detail.size() + tree.failure_path.size() + 2 < 65536) { detail += L"\n" + tree.failure_path; }
                    }
                }
                if ((item.result.moved || item.result.unchanged) && detail.size() + item.result.path.size() + item.result.source_path.size() + 8 < 65536)
                {
                    detail += L"\n" + item.result.source_path + L" → " + item.result.path;
                    if (item.result.post_commit_error) { detail += L" (" + std::to_wstring(item.result.post_commit_error) + L")"; }
                }
                const auto* merge = item.retained_result && item.retained_result->merged ? &*item.retained_result :
                    item.result.merged ? &item.result : nullptr;
                if (merge && (merge->files_skipped || merge->directories_skipped) &&
                    detail.size() + merge->source_path.size() + 256 < 65536)
                { detail += L"\n" + merge->source_path + L" " + localization_.Format(L"Move.MergeSkipped",
                    std::vector<std::wstring>{ std::to_wstring(merge->files_skipped), std::to_wstring(merge->directories_skipped) }); }
            }
        }
        operation_text_.Text(hstring(text)); ToolTipService::SetToolTip(operation_text_, winrt::box_value(hstring(detail)));
        const bool running = value.stage == Stage::Queued || value.stage == Stage::Running || value.stage == Stage::Cancelling;
        operation_cancel_.Visibility(running || file_operations_.HasPendingMoveBatch() ? Visibility::Visible : Visibility::Collapsed);
        operation_cancel_.IsEnabled(value.stage != Stage::Cancelling);
        const auto* tab = ActiveTab();
        const bool can_retry = value.stage == Stage::Failed && batch && batch->resumable && tab && !tab->search_visible &&
            OperationTargetVisible(value.request.parent_identity);
        operation_retry_.Visibility(can_retry ? Visibility::Visible : Visibility::Collapsed);
        operation_retry_.Content(winrt::box_value(hstring(localization_.GetString(value.move_result &&
            (blink::app::MoveHasPartialDestination(*value.move_result) || blink::app::MoveHasSourceChanges(*value.move_result)) ?
                L"Move.SkipRemaining" : L"NewFolder.RetryName"))));
        operation_retry_.IsEnabled(!file_operations_.IsBusy() && copy_conflict_wait_operation_id_ == 0);
    }

    void MoveActionEnded(const FileOperationOrigin& origin, const std::uint64_t operation_id)
    {
        const auto current = file_operations_.Snapshot();
        if (current.operation_id != operation_id || current.kind != blink::app::FileOperationKind::Move) { return; }
        const auto action = file_operations_.CurrentAction();
        if (action && action.Status() == winrt::Windows::Foundation::AsyncStatus::Started)
        {
            if (copy_conflict_wait_operation_id_ == operation_id) { return; }
            copy_conflict_wait_operation_id_ = operation_id;
            const auto weak = get_weak();
            copy_conflict_wait_ = AwaitNativeOperationEnd(action, [weak, origin, operation_id](const HRESULT error) {
                if (auto app = weak.get(); app && !app->window_style_closed_ && app->copy_conflict_wait_operation_id_ == operation_id)
                {
                    app->copy_conflict_wait_operation_id_ = 0;
                    if (FAILED(error)) { app->RenderFileOperationStatus(); return; }
                    app->MoveActionEnded(origin, operation_id);
                }
            });
            return;
        }
        RenderFileOperationStatus();
        if (blink::app::MoveQueueHasRootConflict(current) && current.move_request && IsFileOperationOriginCurrent(origin))
        { ShowMoveConflictDialog(origin, *current.move_request, operation_id); }
    }

    blink::app::FileOperationCoordinator::PublishCallback MovePublication(const FileOperationOrigin& origin)
    {
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        return file_drag_drop_.Publication([weak, dispatcher, origin](const blink::app::FileOperationSnapshot& snapshot) {
            const auto refresh = blink::app::MoveRefreshParents(snapshot);
            const auto relocations = blink::app::CompletedFolderRelocations(snapshot);
            const auto operation_id = snapshot.operation_id;
            const bool failed = snapshot.stage == blink::app::FileOperationStage::Failed;
            const bool terminal = blink::app::FileOperationIsTerminal(snapshot.stage);
            const bool awaiting_conflict = snapshot.directory_conflict.has_value();
            const bool queued = dispatcher.TryEnqueue([weak, origin, operation_id, failed, terminal, refresh, relocations, awaiting_conflict] {
                if (auto app = weak.get(); app && !app->window_style_closed_)
                {
                    if (terminal) { app->RestoreOperationWatches(operation_id); }
                    for (const auto& relocation : relocations) { app->FollowFolderRelocation(relocation); }
                    for (const auto& parent : refresh) { app->RefreshAfterFileOperation(parent); }
                    if (app->file_operations_.Snapshot().operation_id != operation_id) { return; }
                    app->RenderFileOperationStatus();
                    if (awaiting_conflict || app->directory_merge_dialog_operation_id_ == operation_id)
                    { app->ShowDirectoryMergeConflictDialog(origin, operation_id); }
                    if (failed) { app->MoveActionEnded(origin, operation_id); }
                }
            });
            if (!queued && awaiting_conflict) { throw winrt::hresult_error(E_ABORT); }
        });
    }

    std::uint64_t StartMoves(const FileOperationOrigin& origin, std::vector<blink::app::MoveRequest> requests)
    {
        if (!IsFileOperationOriginCurrent(origin))
        { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); return 0; }
        if (file_operations_.IsBusy() || file_operations_.HasPendingBatch()) { return 0; }
        for (auto& request : requests)
        {
            if (request.destination_parent_identity != origin.identity)
            { status_.Text(hstring(localization_.GetString(L"Move.TargetChanged"))); return 0; }
            request.destination_parent = origin.location.FolderPath();
        }
        auto prerequisites = PrepareOperationWatches(MoveWatchSources(requests));
        const auto id = file_operations_.MoveEntries(std::move(requests), MovePublication(origin), std::move(prerequisites));
        operation_watch_id_ = id;
        if (!id) { RestoreOperationWatches(0); }
        if (!id) { status_.Text(hstring(localization_.Format(L"Status.CommandBusy", localization_.GetString(L"Command.Paste")))); }
        RenderFileOperationStatus(); return id;
    }

    void ResumeMove(const FileOperationOrigin& origin, const std::uint64_t operation_id,
        const blink::app::FileCopyConflictPolicy policy, std::optional<std::wstring> name = {})
    {
        const auto value = file_operations_.Snapshot();
        if (file_operations_.IsBusy() || !IsFileOperationOriginCurrent(origin) || value.operation_id != operation_id || !value.move_request ||
            value.move_request->destination_parent_identity != origin.identity)
        { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); return; }
        std::vector<blink::app::MoveRequest> remaining;
        if (value.move_batch)
        {
            for (std::size_t index = value.move_batch->current_index; index < value.move_batch->items.size(); ++index)
            {
                auto request = value.move_batch->items[index].request;
                if (index == value.move_batch->current_index) { request.conflict_policy = policy; }
                remaining.push_back(std::move(request));
            }
        }
        auto prerequisites = PrepareOperationWatches(policy == blink::app::FileCopyConflictPolicy::Cancel ?
            std::vector<blink::app::Location>{} : MoveWatchSources(remaining));
        const auto id = file_operations_.ResumeMoveBatch(operation_id, policy, std::move(name), MovePublication(origin), origin.location.FolderPath(),
            std::move(prerequisites));
        file_drag_drop_.Advance(operation_id, id, file_operations_.Snapshot());
        operation_watch_id_ = id;
        if (!id) { RestoreOperationWatches(0); }
        if (!id) { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); }
        RenderFileOperationStatus();
    }

    void ShowMoveConflictDialog(const FileOperationOrigin& origin, blink::app::MoveRequest request, const std::uint64_t operation_id)
    {
        if (!IsFileOperationOriginCurrent(origin) || IsOperationDialogOpen() || clipboard_work_pending_ || file_operations_.IsBusy() ||
            request.destination_parent_identity != origin.identity) { return; }
        const auto value = file_operations_.Snapshot();
        if (value.operation_id != operation_id || !blink::app::MoveQueueHasRootConflict(value)) { return; }
        const auto source = blink::core::JoinPath(request.source_parent, request.source.name);
        const auto target = blink::core::JoinPath(origin.location.FolderPath(), request.destination_name);
        const auto target_directory = value.move_result ? value.move_result->conflict_destination_is_directory : std::optional<bool>{};
        const bool can_replace = !request.source.is_directory && target_directory && !*target_directory;
        const bool can_merge = request.source.is_directory && target_directory.value_or(false);
        const auto weak = get_weak();
        try
        {
            (void)copy_conflict_dialog_.Show(root_.XamlRoot(), localization_, source, target, can_replace,
                [weak, origin, operation_id](const blink::app::FileCopyConflictPolicy policy) {
                    if (auto app = weak.get(); app && !app->window_style_closed_) { app->ResumeMove(origin, operation_id, policy); }
                }, [weak](HRESULT) {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    { app->status_.Text(hstring(app->localization_.GetString(L"NewFolder.DialogFailed"))); }
                }, request.source.is_directory, true, can_merge);
        }
        catch (...) { status_.Text(hstring(localization_.GetString(L"NewFolder.DialogFailed"))); }
    }

    void ShowMoveNameDialog(const FileOperationOrigin& origin, blink::app::MoveRequest request, const std::uint64_t operation_id)
    {
        if (!IsFileOperationOriginCurrent(origin) || file_operations_.IsBusy() || clipboard_work_pending_ || IsOperationDialogOpen() ||
            copy_conflict_wait_operation_id_ != 0 || request.destination_parent_identity != origin.identity) { return; }
        const auto value = file_operations_.Snapshot();
        if (value.operation_id != operation_id || !value.move_batch || !value.move_batch->resumable ||
            (value.move_result && (blink::app::MoveHasPartialDestination(*value.move_result) ||
                blink::app::MoveHasSourceChanges(*value.move_result)))) { return; }
        const auto weak = get_weak();
        try
        {
            (void)folder_name_dialog_.Show(root_.XamlRoot(), localization_, origin.location.FolderPath(), request.destination_name,
                MoveErrorText(value.result.error), [weak, origin, operation_id](std::wstring name) {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    { app->ResumeMove(origin, operation_id, blink::app::FileCopyConflictPolicy::Fail, std::move(name)); }
                }, [weak](HRESULT) {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    { app->status_.Text(hstring(app->localization_.GetString(L"NewFolder.DialogFailed"))); }
                }, false, false, request.source.is_directory, true);
        }
        catch (...) { status_.Text(hstring(localization_.GetString(L"NewFolder.DialogFailed"))); }
    }

    void StartFileCopies(const FileOperationOrigin& origin, std::vector<blink::app::FileCopyRequest> requests)
    {
        if (!IsFileOperationOriginCurrent(origin)) { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); return; }
        for (auto& request : requests)
        {
            if (request.destination_parent_identity != origin.identity)
            { status_.Text(hstring(localization_.GetString(L"Copy.TargetChanged"))); return; }
            request.destination_parent = origin.location.FolderPath();
        }
        const auto id = file_operations_.CopyBatch(std::move(requests), FileCopyPublication(origin));
        if (!id) { status_.Text(hstring(localization_.Format(L"Status.CommandBusy", localization_.GetString(L"Command.Paste")))); }
        RenderFileOperationStatus();
    }

    std::uint64_t StartCopyItems(const FileOperationOrigin& origin, blink::app::CopyItemRequests requests)
    {
        if (!IsFileOperationOriginCurrent(origin)) { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); return 0; }
        for (auto& item : requests)
        {
            const bool bound = std::visit([&](auto& request) {
                if (request.destination_parent_identity != origin.identity) { return false; }
                request.destination_parent = origin.location.FolderPath(); return true;
            }, item);
            if (!bound) { status_.Text(hstring(localization_.GetString(L"Copy.TargetChanged"))); return 0; }
        }
        const auto id = file_operations_.CopyItems(std::move(requests), FileCopyPublication(origin));
        if (!id) { status_.Text(hstring(localization_.Format(L"Status.CommandBusy", localization_.GetString(L"Command.Paste")))); }
        RenderFileOperationStatus(); return id;
    }

    void ResumeFileCopy(const FileOperationOrigin& origin, blink::app::FileCopyRequest request,
        const std::uint64_t operation_id, std::optional<std::wstring> new_name = {})
    {
        if (!IsFileOperationOriginCurrent(origin) || file_operations_.Snapshot().operation_id != operation_id)
        { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); return; }
        if (request.destination_parent_identity != origin.identity)
        { status_.Text(hstring(localization_.GetString(L"Copy.TargetChanged"))); return; }
        const auto last = file_operations_.Snapshot();
        if (last.copy_batch && last.copy_batch->resumable)
        {
            const auto policy = new_name ? blink::app::FileCopyConflictPolicy::Fail : request.conflict_policy;
            const auto id = file_operations_.ResumeCopyBatch(operation_id, policy, std::move(new_name),
                FileCopyPublication(origin), origin.location.FolderPath());
            file_drag_drop_.Advance(operation_id, id, file_operations_.Snapshot());
            if (!id) { status_.Text(hstring(localization_.Format(L"Status.CommandBusy", localization_.GetString(L"Command.Paste")))); }
            RenderFileOperationStatus();
        }
        else if (!last.copy_batch || last.copy_batch->items.size() == 1)
        {
            // A cancelled single-file attempt can still be explicitly retried by name.
            if (new_name) { request.destination_name = std::move(*new_name); request.conflict_policy = blink::app::FileCopyConflictPolicy::Fail; }
            StartFileCopy(origin, std::move(request));
        }
    }

    void ShowCopyConflictDialog(const FileOperationOrigin& origin, blink::app::FileCopyRequest request,
        const std::uint64_t operation_id)
    {
        if (!IsFileOperationOriginCurrent(origin) || IsOperationDialogOpen() || clipboard_work_pending_) { return; }
        if (request.destination_parent_identity != origin.identity) { return; }
        request.destination_parent = origin.location.FolderPath();
        const auto snapshot = file_operations_.Snapshot();
        if (snapshot.operation_id != operation_id || !blink::app::CopyQueueHasRootConflict(snapshot)) { return; }
        const auto action = file_operations_.CurrentAction();
        if (action && action.Status() == winrt::Windows::Foundation::AsyncStatus::Started)
        {
            if (copy_conflict_wait_operation_id_ == operation_id) { return; }
            copy_conflict_wait_operation_id_ = operation_id;
            const auto weak = get_weak();
            copy_conflict_wait_ = AwaitNativeOperationEnd(action,
                [weak, origin, request = std::move(request), operation_id](const HRESULT error) mutable {
                    if (auto app = weak.get(); app && !app->window_style_closed_ &&
                        app->copy_conflict_wait_operation_id_ == operation_id)
                    {
                        app->copy_conflict_wait_operation_id_ = 0;
                        if (FAILED(error))
                        { app->status_.Text(hstring(app->localization_.GetString(L"CopyConflict.PrepareFailed"))); return; }
                        app->ShowCopyConflictDialog(origin, std::move(request), operation_id);
                    }
                });
            return;
        }
        const auto weak = get_weak();
        const auto source = blink::core::JoinPath(request.source_parent, request.source.name);
        const auto destination = blink::core::JoinPath(request.destination_parent, request.destination_name);
        const bool directory = request.source.is_directory;
        const bool can_merge = directory && (request.source_parent_identity != request.destination_parent_identity ||
            CompareStringOrdinal(request.source.name.c_str(), -1, request.destination_name.c_str(), -1, TRUE) != CSTR_EQUAL);
        try
        {
            (void)copy_conflict_dialog_.Show(root_.XamlRoot(), localization_, source, destination, !directory,
                [weak, origin, request = std::move(request), operation_id](blink::app::FileCopyConflictPolicy policy) mutable {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    {
                        if (!app->IsFileOperationOriginCurrent(origin) ||
                            app->file_operations_.Snapshot().operation_id != operation_id)
                        { app->status_.Text(hstring(app->localization_.GetString(L"Status.CommandStale"))); return; }
                        request.conflict_policy = policy;
                        app->ResumeFileCopy(origin, std::move(request), operation_id);
                    }
                }, [weak](HRESULT) {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    { app->status_.Text(hstring(app->localization_.GetString(L"NewFolder.DialogFailed"))); }
                }, directory, false, can_merge);
        }
        catch (...) { status_.Text(hstring(localization_.GetString(L"NewFolder.DialogFailed"))); }
    }

    void ShowCopyNameDialog(const FileOperationOrigin& origin, blink::app::FileCopyRequest request, std::wstring error,
        const std::uint64_t operation_id)
    {
        if (!IsFileOperationOriginCurrent(origin) || file_operations_.IsBusy() || clipboard_work_pending_ || copy_conflict_wait_operation_id_ != 0 || IsOperationDialogOpen()) { return; }
        if (request.destination_parent_identity != origin.identity) { return; }
        request.destination_parent = origin.location.FolderPath();
        const auto weak = get_weak();
        try
        {
            const auto name = request.destination_name; const bool directory = request.source.is_directory;
            (void)folder_name_dialog_.Show(root_.XamlRoot(), localization_, origin.location.FolderPath(), name,
                std::move(error), [weak, origin, request = std::move(request), operation_id](std::wstring value) mutable {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    { app->ResumeFileCopy(origin, std::move(request), operation_id, std::move(value)); }
                }, [weak](HRESULT) {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    { app->status_.Text(hstring(app->localization_.GetString(L"NewFolder.DialogFailed"))); }
                }, false, true, directory);
        }
        catch (...) { status_.Text(hstring(localization_.GetString(L"NewFolder.DialogFailed"))); }
    }

    static std::vector<blink::app::Location> MoveWatchSources(const std::vector<blink::app::MoveRequest>& requests)
    {
        std::vector<blink::app::Location> sources;
        for (const auto& request : requests)
        {
            if (request.source.is_directory && request.conflict_policy != blink::app::FileCopyConflictPolicy::Skip &&
                request.conflict_policy != blink::app::FileCopyConflictPolicy::Cancel)
            { sources.push_back(blink::app::Location::Folder(blink::core::JoinPath(request.source_parent, request.source.name))); }
        }
        return sources;
    }

    blink::app::FileOperationPrerequisites PrepareOperationWatches(std::vector<blink::app::Location> sources)
    {
        RestoreOperationWatches(operation_watch_id_);
        operation_watch_sources_ = std::move(sources);
        auto stopped = tab_window_state_.SuspendFolderWatches(operation_watch_sources_);
        operation_watch_tabs_ = std::move(stopped.tabs);
        for (const auto id : operation_watch_tabs_) { StopFolderWatcher(id); }
        operation_watch_id_ = 0;
        return std::move(stopped.completion);
    }

    void RestoreOperationWatches(const std::uint64_t operation_id)
    {
        if (operation_id != operation_watch_id_) { return; }
        auto ids = std::move(operation_watch_tabs_); operation_watch_sources_.clear(); operation_watch_id_ = 0;
        for (const auto id : ids)
        {
            if (const auto* tab = FindTab(id))
            {
                const auto page = tab->state->PresentationSnapshot();
                // Monitoring was suspended, including any pending browse.
                // Catch up once after rearming if contents changed in that gap.
                EnsureFolderWatcher(id, page.pending_location.value_or(page.committed_location), true, true);
            }
        }
    }

    void FollowFolderRelocation(const blink::app::FolderRelocation& relocation)
    {
        if (!folder_relocations_.Accept(relocation)) { return; }
        const auto requests = tab_window_state_.RelocateFolder(relocation.source, relocation.destination, relocation.source_identity, relocation.preserve_suffix);
        for (const auto& item : requests) { StartNavigation(item.tab_id, item.request); }
    }

    void RefreshAfterFileOperation(const blink::core::FileIdentity& identity)
    {
        for (const auto id : tab_window_state_.TabIds())
        {
            const auto* tab = FindTab(id); auto* ui = FindTabUi(id);
            if (!tab || !ui || !identity.valid || tab->state->CommittedDirectoryIdentity() != identity) { continue; }
            ui->operation_refresh_identity = identity;
            if (ui->folder_change_timer) { ui->folder_change_timer.Start(); }
            ReconcileFolderChanges(id);
        }
    }

    void BuildFolderViewCommands()
    {
        auto commands = StackPanel();
        commands.Orientation(Orientation::Horizontal);
        commands.Spacing(8);
        commands.VerticalAlignment(VerticalAlignment::Center);

        view_mode_picker_ = ComboBox();
        view_mode_picker_.Width(150);
        for (const auto key : { L"View.Details", L"View.List", L"View.Grid" })
        {
            view_mode_picker_.Items().Append(winrt::box_value(hstring(localization_.GetString(key))));
        }
        Automation::AutomationProperties::SetAutomationId(view_mode_picker_, L"FolderViewMode");
        Automation::AutomationProperties::SetName(
            view_mode_picker_, hstring(localization_.GetString(L"Toolbar.View")));
        view_mode_picker_.SelectionChanged([this](auto&&, auto&&) {
            if (view_commands_sync_suppressed_ || ui_probe_mode_ != UiProbeMode::None)
            {
                return;
            }
            const auto index = view_mode_picker_.SelectedIndex();
            if (index >= 0 && index <= 2)
            {
                SetFolderViewMode(index == 2 ? blink::app::FolderViewMode::Grid :
                    index == 1 ? blink::app::FolderViewMode::List : blink::app::FolderViewMode::Details);
            }
        });
        commands.Children().Append(view_mode_picker_);

        sort_field_picker_ = ComboBox();
        sort_field_picker_.Width(150);
        for (const auto key : { L"Column.Name", L"Column.Type", L"Column.Size", L"Column.Modified" })
        {
            sort_field_picker_.Items().Append(winrt::box_value(hstring(localization_.Format(
                L"Toolbar.SortBy", localization_.GetString(key)))));
        }
        Automation::AutomationProperties::SetAutomationId(sort_field_picker_, L"FolderSortField");
        Automation::AutomationProperties::SetName(
            sort_field_picker_, hstring(localization_.GetString(L"Toolbar.Sort")));
        sort_field_picker_.SelectionChanged([this](auto&&, auto&&) {
            if (view_commands_sync_suppressed_)
            {
                return;
            }
            constexpr blink::app::SortField fields[]{ blink::app::SortField::Name,
                blink::app::SortField::Type, blink::app::SortField::Size, blink::app::SortField::Modified };
            const auto index = sort_field_picker_.SelectedIndex();
            if (index >= 0 && index < 4)
            {
                auto descriptor = ActiveState().GetSortDescriptor();
                descriptor.field = fields[index];
                SetFolderSort(descriptor);
            }
        });
        commands.Children().Append(sort_field_picker_);

        sort_direction_button_ = Button();
        sort_direction_button_.Width(36);
        Automation::AutomationProperties::SetAutomationId(sort_direction_button_, L"FolderSortDirection");
        sort_direction_button_.Click([this](auto&&, auto&&) {
            auto descriptor = ActiveState().GetSortDescriptor();
            descriptor.direction = descriptor.direction == blink::app::SortDirection::Ascending ?
                blink::app::SortDirection::Descending : blink::app::SortDirection::Ascending;
            SetFolderSort(descriptor);
        });
        commands.Children().Append(sort_direction_button_);

        show_hidden_items_ = CheckBox();
        show_hidden_items_.Content(winrt::box_value(hstring(
            localization_.GetString(L"View.ShowHiddenItems"))));
        show_hidden_items_.VerticalAlignment(VerticalAlignment::Center);
        Automation::AutomationProperties::SetAutomationId(show_hidden_items_, L"ShowHiddenItems");
        ToolTipService::SetToolTip(show_hidden_items_, winrt::box_value(hstring(
            localization_.GetString(L"View.ShowHiddenItemsHint"))));
        show_hidden_items_.Checked([this](auto&&, auto&&) { SetShowHiddenItems(true); });
        show_hidden_items_.Unchecked([this](auto&&, auto&&) { SetShowHiddenItems(false); });
        commands.Children().Append(show_hidden_items_);
        content_grid_.Children().Append(commands);
    }

    struct FolderMenuContext
    {
        blink::app::TabId tab_id{};
        std::uint64_t presentation_generation{};
        std::uint64_t view_generation{};
        blink::app::Location location;
        std::optional<std::size_t> row_index;
        std::uint64_t operation_generation{};
        std::uint64_t selection_generation{};
        std::size_t selected_count{};
        std::vector<blink::app::RowIdentity> selected_rows;
    };

    FolderMenuContext CaptureFolderMenuContext(
        const std::optional<std::size_t> row_index) const
    {
        const auto* tab_ui = ActiveTabUi();
        FolderMenuContext context{ tab_window_state_.ActiveTabId(),
            tab_ui ? tab_ui->presentation_generation : 0,
            folder_view_generation_, ActiveState().PresentationSnapshot().committed_location,
            row_index };
        if (const auto* tab = ActiveTab()) { context.operation_generation = tab->file_operation_generation; }
        context.selection_generation = folder_selection_generation_;
        context.selected_count = ActiveState().PresentationSnapshot().selected_count;
        if (row_index && context.location.IsFolder())
        {
            if (const auto rows = ActiveState().SelectedCommittedRows(blink::app::MaxClipboardFileCount))
            {
                for (const auto& row : *rows) { context.selected_rows.push_back(blink::app::MakeRowIdentity(row)); }
            }
        }
        return context;
    }

    bool IsFolderMenuContextCurrent(const FolderMenuContext& context) const
    {
        const auto* tab_ui = ActiveTabUi();
        return !window_style_closed_ && ActiveTab() && !ActiveTab()->search_visible && context.tab_id == tab_window_state_.ActiveTabId() &&
            context.operation_generation == ActiveTab()->file_operation_generation &&
            tab_ui && context.presentation_generation == tab_ui->presentation_generation &&
            context.view_generation == folder_view_generation_ &&
            context.location == ActiveState().PresentationSnapshot().committed_location &&
            (!context.row_index || !context.location.IsFolder() ||
                context.selection_generation == folder_selection_generation_);
    }

    blink::app::FolderCommandCapabilities FolderCommandCapabilities(
        const FolderMenuContext& context) const
    {
        const auto snapshot = ActiveState().PresentationSnapshot();
        const auto* tab_ui = ActiveTabUi();
        bool target = context.row_index && tab_ui &&
            (tab_ui->showing_this_pc ? *context.row_index < tab_ui->this_pc_rows.size() :
                tab_ui->showing_committed_folder && *context.row_index < tab_ui->row_models.size());
        if (target && tab_ui->showing_committed_folder)
        {
            target = ActiveState().IsRowSelected(tab_ui->row_models[*context.row_index].row_identity);
        }
        else if (target)
        {
            std::uint32_t index{};
            target = entries_.SelectedItems().IndexOf(
                tab_ui->row_items_source.GetAt(static_cast<std::uint32_t>(*context.row_index)), index);
        }
        return { snapshot.committed_location.IsFolder(),
            snapshot.has_pending || !tab_ui || tab_ui->presentation_in_progress,
            tab_ui && tab_ui->showing_this_pc && entries_ ?
                entries_.SelectedItems().Size() : snapshot.selected_count, target,
            ActiveState().CommittedDirectoryIdentity().valid, file_operations_.IsBusy() || file_operations_.HasPendingBatch() ||
                clipboard_work_pending_ || copy_conflict_wait_operation_id_ != 0 || IsOperationDialogOpen() || file_drag_drop_.Pending() };
    }

    void ExecuteFolderCommand(const blink::app::FolderCommand command,
        const FolderMenuContext& context)
    {
        using blink::app::FolderCommand;
        using blink::app::FolderCommandStatus;
        if (!IsFolderMenuContextCurrent(context))
        {
            if (!window_style_closed_)
            {
                status_.Text(hstring(localization_.GetString(L"Status.CommandStale")));
            }
            return;
        }
        const auto availability = blink::app::EvaluateFolderCommand(
            command, FolderCommandCapabilities(context));
        const auto label = localization_.GetString(blink::app::FolderCommandLabelKey(command));
        if (availability != FolderCommandStatus::Ready)
        {
            const auto key = availability == FolderCommandStatus::Busy ? L"Status.CommandBusy" :
                availability == FolderCommandStatus::FolderRequired ? L"Status.CommandFolderRequired" :
                availability == FolderCommandStatus::SelectionRequired ? L"Status.CommandSelectionRequired" :
                availability == FolderCommandStatus::SingleSelectionRequired ? L"Status.CommandSingleRequired" :
                availability == FolderCommandStatus::TargetUnavailable ?
                    (command == FolderCommand::NewFolder ? L"NewFolder.Unavailable" : L"Status.OpenChanged") :
                L"Status.CommandNotImplemented";
            status_.Text(hstring(localization_.Format(key, label)));
            startup_.Stage("folder-command.unavailable");
            return;
        }
        switch (command)
        {
        case FolderCommand::Open:
            ActivatePresentedRow(*context.row_index);
            break;
        case FolderCommand::Refresh:
            RefreshNavigation();
            break;
        case FolderCommand::NewFolder:
            ShowFileNameDialog(blink::app::CaptureFileOperationOrigin(*ActiveTab()),
                localization_.GetString(L"NewFolder.DefaultName"));
            break;
        case FolderCommand::Cut:
            PublishSelectedFileCopy(blink::app::ClipboardTransferIntent::Move);
            break;
        case FolderCommand::Copy:
            PublishSelectedFileCopy();
            break;
        case FolderCommand::CopyFullPath:
        case FolderCommand::CopyParentPath:
            PublishSelectedPaths(command == FolderCommand::CopyParentPath);
            break;
        case FolderCommand::Paste:
            PasteClipboardFile(blink::app::CaptureFileOperationOrigin(*ActiveTab()));
            break;
        case FolderCommand::Rename:
            if (const auto source = SelectedEntryFromCommittedState())
            { ShowFileNameDialog(blink::app::CaptureFileOperationOrigin(*ActiveTab()), source->name, {}, source); }
            break;
        case FolderCommand::Properties:
            ShowSelectedProperties();
            break;
        case FolderCommand::Delete:
        case FolderCommand::PermanentDelete:
            DeleteSelection(command == FolderCommand::PermanentDelete ? blink::app::DeleteMode::Permanent : blink::app::DeleteMode::Recycle);
            break;
        case FolderCommand::ViewDetails:
        case FolderCommand::ViewList:
        case FolderCommand::ViewGrid:
            SetFolderViewMode(command == FolderCommand::ViewDetails ?
                blink::app::FolderViewMode::Details : command == FolderCommand::ViewList ?
                    blink::app::FolderViewMode::List : blink::app::FolderViewMode::Grid);
            break;
        default:
            auto sort = ActiveState().GetSortDescriptor();
            switch (command)
            {
            case FolderCommand::SortName: sort.field = blink::app::SortField::Name; break;
            case FolderCommand::SortType: sort.field = blink::app::SortField::Type; break;
            case FolderCommand::SortSize: sort.field = blink::app::SortField::Size; break;
            case FolderCommand::SortModified: sort.field = blink::app::SortField::Modified; break;
            case FolderCommand::SortAscending: sort.direction = blink::app::SortDirection::Ascending; break;
            case FolderCommand::SortDescending: sort.direction = blink::app::SortDirection::Descending; break;
            default: return;
            }
            SetFolderSort(sort);
            break;
        }
        startup_.Stage("folder-command.executed");
    }

    void CancelFolderSystemMenu() noexcept
    {
        if (folder_system_menu_context_)
        {
            folder_system_menu_context_.reset();
            shell_context_menu_.CancelPending();
        }
    }

    void ExecuteFolderShellCommand(const blink::app::ShellMenuCommand command, const FolderMenuContext& context)
    {
        using blink::app::ShellMenuCommand;
        using blink::app::FolderCommand;
        if (!IsFolderMenuContextCurrent(context)) { return; }
        if (command == ShellMenuCommand::Locate)
        {
            // The containing folder is already displayed; reveal the selected
            // row without navigating or changing the preserved multi-selection.
            if (context.row_index)
            {
                if (const auto* ui = ActiveTabUi(); ui && ui->row_items_source &&
                    *context.row_index < ui->row_items_source.Size())
                { entries_.ScrollIntoView(ui->row_items_source.GetAt(static_cast<std::uint32_t>(*context.row_index))); }
            }
            entries_.Focus(FocusState::Programmatic);
            return;
        }
        const auto mapped = command == ShellMenuCommand::Open ? FolderCommand::Open :
            command == ShellMenuCommand::Cut ? FolderCommand::Cut :
            command == ShellMenuCommand::Copy ? FolderCommand::Copy :
            command == ShellMenuCommand::PasteParent ? FolderCommand::Paste :
            command == ShellMenuCommand::CopyFullPath ? FolderCommand::CopyFullPath :
            command == ShellMenuCommand::CopyParentPath ? FolderCommand::CopyParentPath :
            command == ShellMenuCommand::Rename ? FolderCommand::Rename :
            command == ShellMenuCommand::PermanentDelete ||
                (command == ShellMenuCommand::Delete && (GetKeyState(VK_SHIFT) & 0x8000)) ? FolderCommand::PermanentDelete :
            FolderCommand::Delete;
        if (command != ShellMenuCommand::None) { ExecuteFolderCommand(mapped, context); }
    }

    void ShowFolderSystemContextMenu(const FolderMenuContext& context, const Windows::Foundation::Point position)
    {
        if (!IsFolderMenuContextCurrent(context)) { return; }
        const auto rows = ActiveState().SelectedCommittedRows(blink::app::MaxClipboardFileCount);
        if (!rows || rows->empty() || !context.location.IsFolder()) { return; }
        blink::app::ShellMenuRequest request;
        request.entry = rows->front();
        request.parent_identity = ActiveState().CommittedDirectoryIdentity();
        request.additional_entries.assign(rows->begin() + 1, rows->end());
        request.locale = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? L"zh-CN" : L"en-US";
        request.extended = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        check_hresult(window_.as<::IWindowNative>()->get_WindowHandle(&request.owner));
        const auto client = entries_.TransformToVisual(nullptr).TransformPoint(position);
        const auto scale = entries_.XamlRoot().RasterizationScale();
        request.position = { static_cast<LONG>(std::lround(client.X * scale)),
            static_cast<LONG>(std::lround(client.Y * scale)) };
        if (!ClientToScreen(request.owner, &request.position))
        { ShowClipboardCopyError(GetLastError()); return; }
        DismissSearchContextMenu();
        if (folder_menu_) { folder_menu_.Hide(); }
        folder_system_menu_context_ = context;
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        try
        {
            (void)shell_context_menu_.Show(std::move(request),
                [weak, dispatcher, context](std::uint64_t generation, blink::app::ShellMenuResult result) {
                    (void)dispatcher.TryEnqueue([weak, context, generation, result] {
                        if (const auto app = weak.get(); app && app->shell_context_menu_.IsCurrent(generation) &&
                            app->folder_system_menu_context_ && app->IsFolderMenuContextCurrent(context))
                        {
                            app->folder_system_menu_context_.reset();
                            using blink::app::ShellMenuResultKind;
                            if (result.kind == ShellMenuResultKind::BlinkCommand)
                            { app->ExecuteFolderShellCommand(result.command, context); }
                            else if (result.kind == ShellMenuResultKind::Error)
                            {
                                wchar_t code[16]{}; swprintf_s(code, L"0x%08X", static_cast<unsigned>(result.error));
                                app->status_.Text(hstring(app->localization_.Format(L"Search.MenuError", code)));
                            }
                            else if (result.kind == ShellMenuResultKind::NativeCommand)
                            { app->status_.Text(hstring(app->localization_.GetString(L"Search.NativeDispatched"))); }
                        }
                    });
                });
        }
        catch (...)
        {
            CancelFolderSystemMenu();
            status_.Text(hstring(localization_.Format(L"Search.MenuError", L"0x80004005")));
        }
    }

    MenuFlyoutItemBase MakeFolderMenuItem(const blink::app::FolderCommand command,
        const FolderMenuContext& context, const wchar_t* automation_id,
        const std::optional<bool> checked = std::nullopt)
    {
        const auto availability = blink::app::EvaluateFolderCommand(
            command, FolderCommandCapabilities(context));
        const auto label = localization_.GetString(blink::app::FolderCommandLabelKey(command)) +
            (availability == blink::app::FolderCommandStatus::NotImplemented ?
                localization_.GetString(L"Command.UnavailableSuffix") : std::wstring{});
        const auto weak_app = get_weak();
        const auto invoke = [weak_app, command, context](auto&&, auto&&) {
            if (auto app = weak_app.get())
            {
                app->ExecuteFolderCommand(command, context);
            }
        };
        MenuFlyoutItemBase result{ nullptr };
        if (checked)
        {
            auto item = ToggleMenuFlyoutItem();
            item.Text(hstring(label));
            item.IsChecked(*checked);
            item.Click(invoke);
            result = item;
        }
        else
        {
            auto item = MenuFlyoutItem();
            item.Text(hstring(label));
            item.Click(invoke);
            result = item;
        }
        result.IsEnabled(availability == blink::app::FolderCommandStatus::Ready ||
            availability == blink::app::FolderCommandStatus::NotImplemented);
        using blink::app::FolderCommand;
        const wchar_t* glyph = L""; const wchar_t* shortcut = L"";
        switch (command)
        {
        case FolderCommand::Open: glyph = L"\xE8E5"; shortcut = L"Enter"; break;
        case FolderCommand::Cut: glyph = L"\xE8C6"; shortcut = L"Ctrl+X"; break;
        case FolderCommand::Copy: glyph = L"\xE8C8"; shortcut = L"Ctrl+C"; break;
        case FolderCommand::Paste: glyph = L"\xE77F"; shortcut = L"Ctrl+V"; break;
        case FolderCommand::CopyFullPath: glyph = L"\xE71B"; break;
        case FolderCommand::CopyParentPath: glyph = L"\xE8B7"; break;
        case FolderCommand::Rename: glyph = L"\xE8AC"; shortcut = L"F2"; break;
        case FolderCommand::Delete: glyph = L"\xE74D"; shortcut = L"Delete"; break;
        case FolderCommand::Properties: glyph = L"\xE946"; shortcut = L"Alt+Enter"; break;
        case FolderCommand::NewFolder: glyph = L"\xE8F4"; shortcut = L"Ctrl+Shift+N"; break;
        case FolderCommand::Refresh: glyph = L"\xE72C"; shortcut = L"F5"; break;
        case FolderCommand::ViewDetails: glyph = L"\xE9D5"; break;
        case FolderCommand::ViewList: glyph = L"\xEA37"; break;
        case FolderCommand::ViewGrid: glyph = L"\xE80A"; break;
        case FolderCommand::SortName: glyph = L"\xE8D2"; break;
        case FolderCommand::SortType: glyph = L"\xE8A5"; break;
        case FolderCommand::SortSize: glyph = L"\xE8F1"; break;
        case FolderCommand::SortModified: glyph = L"\xE787"; break;
        case FolderCommand::SortAscending: glyph = L"\xE74A"; break;
        case FolderCommand::SortDescending: glyph = L"\xE74B"; break;
        default: break;
        }
        blink::app::StyleContextMenuItem(result, glyph, shortcut);
        if (command == FolderCommand::Paste)
        { result.IsEnabled(result.IsEnabled() && IsClipboardFormatAvailable(CF_HDROP)); }
        Automation::AutomationProperties::SetAutomationId(result, automation_id);
        return result;
    }

    void ShowFolderContextMenu(const std::optional<std::size_t> row_index,
        const Windows::Foundation::Point position)
    {
        using blink::app::FolderCommand;
        DismissSearchContextMenu();
        if (folder_menu_)
        {
            folder_menu_.Hide();
        }
        // Right-clicking an unselected row replaces selection. A selected row
        // keeps the existing multi-selection, including distinct hard-link rows.
        const auto* tab_ui = ActiveTabUi();
        if (row_index && tab_ui && !tab_ui->presentation_in_progress &&
            !ActiveState().PresentationSnapshot().has_pending && tab_ui->row_items_source &&
            *row_index < tab_ui->row_items_source.Size())
        {
            const auto item = tab_ui->row_items_source.GetAt(static_cast<std::uint32_t>(*row_index));
            std::uint32_t selected_index{};
            if (!entries_.SelectedItems().IndexOf(item, selected_index))
            {
                entries_.SelectedItems().Clear();
                entries_.SelectedItems().Append(item);
            }
        }
        const auto context = CaptureFolderMenuContext(row_index);
        folder_menu_ = MenuFlyout();
        blink::app::ApplyContextMenuAppearance(folder_menu_, entries_, L"FolderContextMenu");
        const auto add = [this, &context](const FolderCommand command, const wchar_t* id) {
            folder_menu_.Items().Append(MakeFolderMenuItem(command, context, id));
        };
        if (row_index)
        {
            add(FolderCommand::Open, L"ContextOpen");
            folder_menu_.Items().Append(MenuFlyoutSeparator());
            add(FolderCommand::Cut, L"ContextCut");
            add(FolderCommand::Copy, L"ContextCopy");
            add(FolderCommand::Paste, L"ContextPaste");
            folder_menu_.Items().Append(MenuFlyoutSeparator());
            add(FolderCommand::CopyFullPath, L"ContextCopyPath");
            add(FolderCommand::CopyParentPath, L"ContextCopyParentPath");
            folder_menu_.Items().Append(MenuFlyoutSeparator());
            add(FolderCommand::Rename, L"ContextRename");
            add(FolderCommand::Delete, L"ContextDelete");
            add(FolderCommand::Properties, L"ContextProperties");
            folder_menu_.Items().Append(MenuFlyoutSeparator());
            if (context.location.IsFolder())
            {
                MenuFlyoutItem system;
                system.Text(hstring(localization_.GetString(L"Search.MenuSystemOptions")));
                blink::app::StyleContextMenuItem(system, L"\xE712");
                system.IsEnabled(blink::app::EvaluateFolderCommand(FolderCommand::Properties,
                    FolderCommandCapabilities(context)) == blink::app::FolderCommandStatus::Ready &&
                    !context.selected_rows.empty() && context.selected_rows.size() == context.selected_count);
                Automation::AutomationProperties::SetAutomationId(system, L"ContextSystemOptions");
                const auto weak = get_weak();
                system.Click([weak, context, position](auto&&, auto&&) {
                    if (const auto app = weak.get()) { app->ShowFolderSystemContextMenu(context, position); }
                });
                folder_menu_.Items().Append(system);
                folder_menu_.Items().Append(MenuFlyoutSeparator());
            }
        }
        else
        {
            add(FolderCommand::Paste, L"ContextPaste");
            add(FolderCommand::NewFolder, L"ContextNewFolder");
            folder_menu_.Items().Append(MenuFlyoutSeparator());
        }
        add(FolderCommand::Refresh, L"ContextRefresh");

        auto views = MenuFlyoutSubItem();
        blink::app::StyleContextMenuSubItem(views, L"\xE8A9");
        views.Text(hstring(localization_.GetString(L"Command.View")));
        Automation::AutomationProperties::SetAutomationId(views, L"ContextView");
        const auto mode = ActiveState().GetViewMode();
        views.IsEnabled(blink::app::EvaluateFolderCommand(FolderCommand::ViewDetails,
            FolderCommandCapabilities(context)) == blink::app::FolderCommandStatus::Ready);
        views.Items().Append(MakeFolderMenuItem(FolderCommand::ViewDetails, context,
            L"ContextViewDetails", mode == blink::app::FolderViewMode::Details));
        views.Items().Append(MakeFolderMenuItem(FolderCommand::ViewList, context,
            L"ContextViewList", mode == blink::app::FolderViewMode::List));
        views.Items().Append(MakeFolderMenuItem(FolderCommand::ViewGrid, context,
            L"ContextViewGrid", mode == blink::app::FolderViewMode::Grid));
        folder_menu_.Items().Append(views);

        auto sorting = MenuFlyoutSubItem();
        blink::app::StyleContextMenuSubItem(sorting, L"\xE8CB");
        sorting.Text(hstring(localization_.GetString(L"Toolbar.Sort")));
        Automation::AutomationProperties::SetAutomationId(sorting, L"ContextSort");
        const auto sort = ActiveState().GetSortDescriptor();
        sorting.Items().Append(MakeFolderMenuItem(FolderCommand::SortName, context,
            L"ContextSortName", sort.field == blink::app::SortField::Name));
        sorting.Items().Append(MakeFolderMenuItem(FolderCommand::SortType, context,
            L"ContextSortType", sort.field == blink::app::SortField::Type));
        sorting.Items().Append(MakeFolderMenuItem(FolderCommand::SortSize, context,
            L"ContextSortSize", sort.field == blink::app::SortField::Size));
        sorting.Items().Append(MakeFolderMenuItem(FolderCommand::SortModified, context,
            L"ContextSortModified", sort.field == blink::app::SortField::Modified));
        sorting.Items().Append(MenuFlyoutSeparator());
        sorting.Items().Append(MakeFolderMenuItem(FolderCommand::SortAscending, context,
            L"ContextSortAscending", sort.direction == blink::app::SortDirection::Ascending));
        sorting.Items().Append(MakeFolderMenuItem(FolderCommand::SortDescending, context,
            L"ContextSortDescending", sort.direction == blink::app::SortDirection::Descending));
        sorting.IsEnabled(blink::app::EvaluateFolderCommand(FolderCommand::SortName,
            FolderCommandCapabilities(context)) == blink::app::FolderCommandStatus::Ready);
        folder_menu_.Items().Append(sorting);

        auto options = Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowOptions();
        options.Position(position);
        folder_menu_.ShowAt(entries_, options);
        startup_.Stage(row_index ? "folder-menu.item" : "folder-menu.background");
    }

    void ShowKeyboardFolderContextMenu()
    {
        auto row = ResolveRowIndexFromElement(FocusManager::GetFocusedElement(entries_.XamlRoot()));
        if (!row && entries_.SelectedItems().Size())
        {
            row = RowIndexForItem(entries_.SelectedItems().GetAt(0));
        }
        Windows::Foundation::Point position{ 16, 16 };
        if (row)
        {
            if (const auto item = entries_.ContainerFromIndex(static_cast<int32_t>(*row))
                    .try_as<FrameworkElement>())
            {
                position = item.TransformToVisual(entries_).TransformPoint(
                    { 8, static_cast<float>(item.ActualHeight()) });
            }
        }
        ShowFolderContextMenu(row, position);
    }

    ListViewBase CreateFolderViewControl(const blink::app::FolderViewMode mode)
    {
        ListViewBase control = mode == blink::app::FolderViewMode::Grid ?
            GridView().as<ListViewBase>() : ListView().as<ListViewBase>();
        control.SelectionMode(ListViewSelectionMode::Extended);
        ConfigureFileDragControl(control);
        // Keep an empty folder's command surface reachable from the keyboard.
        control.IsTabStop(true);
        Automation::AutomationProperties::SetAutomationId(control, L"FolderEntries");
        control.ContainerContentChanging([](auto&&, const ContainerContentChangingEventArgs& args) {
            hstring name;
            if (!args.InRecycleQueue())
            {
                if (const auto row = args.Item().try_as<Windows::Foundation::Collections::PropertySet>();
                    row && row.HasKey(L"Name"))
                {
                    name = winrt::unbox_value<hstring>(row.Lookup(L"Name"));
                }
            }
            // Assign on realization and recycling. Binding inside a Style
            // Setter is not a reliable way to name a reused item container.
            Automation::AutomationProperties::SetName(args.ItemContainer(), name);
        });
        control.SelectionChanged([this](IInspectable const& sender, auto&&) {
            if (sender == entries_)
            {
                OnListSelectionChanged();
            }
        });
        AddAccelerator(control, Windows::System::VirtualKey::A,
            Windows::System::VirtualKeyModifiers::Control,
            [this] { SelectAllFolderRows(); }, true);
        AddAccelerator(control, Windows::System::VirtualKey::Enter,
            Windows::System::VirtualKeyModifiers::None,
            [this] { ActivateFocusedRow(); }, true);
        const auto command_key = [this, &control](const Windows::System::VirtualKey key,
            const Windows::System::VirtualKeyModifiers modifiers,
            const blink::app::FolderCommand command) {
            const auto weak_app = get_weak();
            AddAccelerator(control, key, modifiers, [weak_app, command] {
                if (auto app = weak_app.get())
                {
                    auto row = app->ResolveRowIndexFromElement(
                        FocusManager::GetFocusedElement(app->entries_.XamlRoot()));
                    if (!row && app->entries_.SelectedItems().Size())
                    {
                        row = app->RowIndexForItem(app->entries_.SelectedItems().GetAt(0));
                    }
                    app->ExecuteFolderCommand(command, app->CaptureFolderMenuContext(row));
                }
            }, true);
        };
        command_key(Windows::System::VirtualKey::Enter, Windows::System::VirtualKeyModifiers::Menu,
            blink::app::FolderCommand::Properties);
        command_key(Windows::System::VirtualKey::F2, Windows::System::VirtualKeyModifiers::None,
            blink::app::FolderCommand::Rename);
        control.PreviewKeyDown([this](auto&& sender, const KeyRoutedEventArgs& args) {
            if (sender != entries_ || args.Key() != Windows::System::VirtualKey::Delete)
            {
                return;
            }
            // Route both Delete variants from this input message before
            // control/accelerator handling, using the same scoped command path.
            const auto down = [](const Windows::System::VirtualKey key) {
                return (Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(key) &
                    Windows::UI::Core::CoreVirtualKeyStates::Down) !=
                    Windows::UI::Core::CoreVirtualKeyStates::None;
            };
            if (down(Windows::System::VirtualKey::Control) || down(Windows::System::VirtualKey::Menu))
            {
                return;
            }
            const bool permanent_delete = down(Windows::System::VirtualKey::Shift);
            auto row = ResolveRowIndexFromElement(FocusManager::GetFocusedElement(entries_.XamlRoot()));
            if (!row && entries_.SelectedItems().Size())
            {
                row = RowIndexForItem(entries_.SelectedItems().GetAt(0));
            }
            args.Handled(true);
            ExecuteFolderCommand(permanent_delete ?
                blink::app::FolderCommand::PermanentDelete : blink::app::FolderCommand::Delete,
                CaptureFolderMenuContext(row));
        });
        command_key(Windows::System::VirtualKey::C, Windows::System::VirtualKeyModifiers::Control,
            blink::app::FolderCommand::Copy);
        command_key(Windows::System::VirtualKey::X, Windows::System::VirtualKeyModifiers::Control,
            blink::app::FolderCommand::Cut);
        command_key(Windows::System::VirtualKey::V, Windows::System::VirtualKeyModifiers::Control,
            blink::app::FolderCommand::Paste);
        command_key(Windows::System::VirtualKey::N,
            Windows::System::VirtualKeyModifiers::Control | Windows::System::VirtualKeyModifiers::Shift,
            blink::app::FolderCommand::NewFolder);
        const auto weak_app = get_weak();
        const auto show_keyboard_menu = [weak_app] {
            if (auto app = weak_app.get())
            {
                app->ShowKeyboardFolderContextMenu();
            }
        };
        AddAccelerator(control, Windows::System::VirtualKey::F10,
            Windows::System::VirtualKeyModifiers::Shift, show_keyboard_menu, true);
        AddAccelerator(control, Windows::System::VirtualKey::Application,
            Windows::System::VirtualKeyModifiers::None, show_keyboard_menu, true);
        control.ContextRequested([this](auto&& sender, const ContextRequestedEventArgs& args) {
            if (sender != entries_)
            {
                return;
            }
            Windows::Foundation::Point position{ 16, 16 };
            auto row = ResolveRowIndexFromElement(args.OriginalSource());
            if (!args.TryGetPosition(entries_, position))
            {
                ShowKeyboardFolderContextMenu();
                args.Handled(true);
                return;
            }
            ShowFolderContextMenu(row, position);
            args.Handled(true);
        });
        control.ContextCanceled([this](auto&& sender, auto&&) {
            if (sender == entries_ && folder_menu_)
            {
                folder_menu_.Hide();
            }
        });
        const auto double_tapped_handler = DoubleTappedEventHandler([this](
            IInspectable const& sender, DoubleTappedRoutedEventArgs const& args) {
            if (sender == entries_)
            {
                OnFolderDoubleTapped(args);
            }
        });
        control.AddHandler(UIElement::DoubleTappedEvent(), winrt::box_value(double_tapped_handler), true);
        const auto tapped_handler = TappedEventHandler([this](
            IInspectable const& sender, TappedRoutedEventArgs const& args) {
            if (sender == entries_)
            {
                OnThisPcTapped(args);
            }
        });
        control.AddHandler(UIElement::TappedEvent(), winrt::box_value(tapped_handler), true);
        return control;
    }

    void ApplyFolderViewMode(const blink::app::FolderViewMode mode)
    {
        if (!entries_ || ui_probe_mode_ != UiProbeMode::None || rendered_view_mode_ == mode)
        {
            return;
        }
        // Prepare the new layout before touching the current control. A
        // template failure must not discard the user's existing presentation.
        auto replacement = CreateFolderViewControl(mode);
        folder_view_presentation_.Configure(replacement, mode);
        selection_sync_suppressed_ = true;
        const auto previous = entries_;
        previous.SelectedItems().Clear();
        previous.ItemsSource(nullptr);
        std::uint32_t position{};
        if (content_grid_.Children().IndexOf(previous, position))
        {
            content_grid_.Children().RemoveAt(position);
        }
        entries_ = replacement;
        Grid::SetRow(entries_, 2);
        content_grid_.Children().Append(entries_);
        rendered_view_mode_ = mode;
        ++folder_view_generation_;
        startup_.Stage(std::string("folder-view.mode=") +
            (mode == blink::app::FolderViewMode::Grid ? "grid" :
                mode == blink::app::FolderViewMode::List ? "list" : "details"));
    }

    void SetFolderViewMode(const blink::app::FolderViewMode mode)
    {
        if (ActiveState().GetViewMode() == mode)
        {
            return;
        }
        const auto tab_id = tab_window_state_.ActiveTabId();
        const auto focused = entries_ && entries_.XamlRoot() ?
            ResolveRowIndexFromElement(FocusManager::GetFocusedElement(entries_.XamlRoot())) :
            std::optional<std::size_t>{};
        IInspectable focused_item{ nullptr };
        if (focused.has_value())
        {
            const auto* tab_ui = ActiveTabUi();
            if (tab_ui && tab_ui->row_items_source && *focused < tab_ui->row_items_source.Size())
            {
                focused_item = tab_ui->row_items_source.GetAt(static_cast<std::uint32_t>(*focused));
            }
        }
        ApplyFolderViewMode(mode);
        ActiveState().SetViewMode(mode);
        BindFolderViewToActiveTab();
        SyncFolderViewCommands();
        UpdateSelectionStatus();
        // Preserve actual list focus only if the command originated there.
        // A toolbar click must not steal focus from an editor or another tab.
        if (focused_item)
        {
            entries_.ScrollIntoView(focused_item);
            const auto weak_app = get_weak();
            const auto generation = folder_view_generation_;
            auto completed = std::make_shared<bool>(false);
            const auto restore_focus = [weak_app, tab_id, generation, focused_item, completed](
                auto&&, auto&&) {
                if (std::exchange(*completed, true))
                {
                    return;
                }
                if (auto app = weak_app.get(); app &&
                    app->tab_window_state_.ActiveTabId() == tab_id &&
                    app->folder_view_generation_ == generation && app->entries_)
                {
                    if (const auto container = app->entries_.ContainerFromItem(focused_item).try_as<Control>())
                    {
                        container.Focus(FocusState::Programmatic);
                    }
                }
            };
            if (entries_.IsLoaded())
            {
                restore_focus(entries_, RoutedEventArgs(nullptr));
            }
            else
            {
                entries_.Loaded(restore_focus);
            }
        }
    }

    void SyncFolderViewCommands()
    {
        const auto mode = ActiveState().GetViewMode();
        const auto sort = ActiveState().GetSortDescriptor();
        view_commands_sync_suppressed_ = true;
        view_mode_picker_.SelectedIndex(mode == blink::app::FolderViewMode::Grid ? 2 :
            mode == blink::app::FolderViewMode::List ? 1 : 0);
        sort_field_picker_.SelectedIndex(sort.field == blink::app::SortField::Type ? 1 :
            sort.field == blink::app::SortField::Size ? 2 :
            sort.field == blink::app::SortField::Modified ? 3 : 0);
        const bool descending = sort.direction == blink::app::SortDirection::Descending;
        sort_direction_button_.Content(winrt::box_value(hstring(descending ? L"\x2193" : L"\x2191")));
        const auto direction_label = localization_.GetString(
            descending ? L"Sort.Descending" : L"Sort.Ascending");
        Automation::AutomationProperties::SetName(sort_direction_button_, hstring(direction_label));
        ToolTipService::SetToolTip(sort_direction_button_, winrt::box_value(hstring(direction_label)));
        details_header_.Visibility(mode == blink::app::FolderViewMode::Details ?
            Visibility::Visible : Visibility::Collapsed);
        view_commands_sync_suppressed_ = false;
    }

    void SetFolderSort(const blink::app::SortDescriptor descriptor)
    {
        const auto snapshot = ActiveState().PresentationSnapshot();
        const auto* tab_ui = ActiveTabUi();
        if (!snapshot.committed_location.IsFolder() || snapshot.has_pending ||
            (tab_ui && tab_ui->presentation_in_progress))
        {
            SyncFolderViewCommands();
            return;
        }
        ActiveState().SetSortDescriptor(descriptor);
        SyncFolderViewCommands();
        RenderCommittedFolder();
        UpdateNavigationButtons();
    }

    void SetShowHiddenItems(const bool show)
    {
        const blink::app::ItemVisibilityPolicy policy{ show };
        if (policy == tab_window_state_.GetVisibilityPolicy())
        {
            return;
        }
        // Prepare the root values before replacing the model or UI nodes.
        std::vector<blink::app::NavigationTreeNodeSpec> roots;
        const auto* root = navigation_tree_model_.Find(navigation_tree_model_.RootId());
        if (root)
        {
            roots.reserve(root->child_ids.size());
            for (const auto id : root->child_ids)
            {
                if (const auto* child = navigation_tree_model_.Find(id))
                {
                    roots.push_back({ child->kind, child->label, child->target,
                        child->can_expand, child->reparse_point });
                }
            }
        }

        tab_window_state_.SetVisibilityPolicy(policy);
        show_hidden_items_.IsChecked(winrt::box_value(show).as<Windows::Foundation::IReference<bool>>());
        ++folder_view_generation_;
        for (const auto id : tab_window_state_.TabIds())
        {
            const auto* tab = FindTab(id);
            if (tab && tab->state->PresentationSnapshot().committed_location.IsFolder())
            {
                // Cancels any projection of the previous policy and reuses
                // the same committed page/history and current view/sort.
                (void)RenderCommittedFolderWithTiming(id);
            }
        }
        // All tree workers captured the old policy. The lifetime gate drops
        // their late results; reveal the active location with the new policy.
        ResetNavigationTree(roots);
        SyncNavigationPaneToCommittedLocation();
        UpdateNavigationButtons();
        UpdateBrowseStatus();
        startup_.Stage(show ? "visibility.show-hidden=true" : "visibility.show-hidden=false");
    }

    void AddAccelerator(
        const UIElement& owner,
        const Windows::System::VirtualKey key,
        const Windows::System::VirtualKeyModifiers modifiers,
        std::function<void()> invoke,
        const bool focus_scoped = false)
    {
        auto accelerator = KeyboardAccelerator();
        accelerator.Key(key);
        accelerator.Modifiers(modifiers);
        if (focus_scoped)
        {
            // Ctrl+A/Enter belong to the file list, never the address editor.
            accelerator.ScopeOwner(owner);
        }
        const auto weak = get_weak();
        accelerator.Invoked([weak, invoke = std::move(invoke)](auto&&, auto&& args) {
            args.Handled(true);
            if (auto app = weak.get(); app && !app->window_style_closed_ && !app->IsOperationDialogOpen()) { invoke(); }
        });
        owner.KeyboardAccelerators().Append(accelerator);
    }

    static void AddGridRow(
        const Grid& grid,
        const double value,
        const GridUnitType unit_type)
    {
        auto row = RowDefinition();
        row.Height(GridLength{ value, unit_type });
        grid.RowDefinitions().Append(row);
    }

    static void AddGridColumn(
        const Grid& grid,
        const double value,
        const GridUnitType unit_type)
    {
        auto column = ColumnDefinition();
        column.Width(GridLength{ value, unit_type });
        grid.ColumnDefinitions().Append(column);
    }

    static void ConfigureDetailsColumns(const Grid& grid)
    {
        AddGridColumn(grid, 1, GridUnitType::Star);
        AddGridColumn(grid, 140, GridUnitType::Pixel);
        AddGridColumn(grid, 110, GridUnitType::Pixel);
        AddGridColumn(grid, 180, GridUnitType::Pixel);
    }

    Button MakeSortHeader(
        const std::wstring_view label_key,
        const blink::app::SortField field)
    {
        const auto label = localization_.GetString(label_key);
        auto button = Button();
        button.Content(winrt::box_value(hstring(label)));
        ToolTipService::SetToolTip(
            button,
            winrt::box_value(hstring(localization_.Format(L"Toolbar.SortBy", label))));
        button.HorizontalAlignment(HorizontalAlignment::Stretch);
        button.VerticalAlignment(VerticalAlignment::Stretch);
        button.Margin(Thickness{ 0, 0, 4, 0 });
        button.Click([this, field](IInspectable const&, RoutedEventArgs const&) {
            auto descriptor = ActiveState().GetSortDescriptor();
            if (descriptor.field == field)
            {
                descriptor.direction = descriptor.direction == blink::app::SortDirection::Ascending ?
                    blink::app::SortDirection::Descending : blink::app::SortDirection::Ascending;
            }
            else
            {
                descriptor.field = field;
                descriptor.direction = blink::app::SortDirection::Ascending;
            }
            SetFolderSort(descriptor);
        });
        return button;
    }

    [[nodiscard]] std::wstring NavigationTreeDisplayLabel(
        const blink::app::NavigationTreeNode& node) const
    {
        if (node.load_state != blink::app::NavigationTreeLoadState::Failed)
        {
            return node.label;
        }
        return node.label + L" [Win32 " + std::to_wstring(node.error) + L"]";
    }

    [[nodiscard]] winrt::Windows::Foundation::Collections::PropertySet
        MakeNavigationTreeContent(
        const blink::app::NavigationTreeNode& node) const
    {
        auto content = winrt::Windows::Foundation::Collections::PropertySet();
        content.Insert(
            L"Label",
            winrt::box_value(hstring(NavigationTreeDisplayLabel(node))));
        content.Insert(L"NodeId", winrt::box_value(node.id));
        return content;
    }

    void ConfigureQuickNavigationItemTemplate()
    {
        constexpr const wchar_t* kQuickNavigationItemTemplate = LR"XAML(
<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <TextBlock Text="{Binding [Label]}" Padding="8,3,8,3" />
</DataTemplate>)XAML";
        auto loaded = Microsoft::UI::Xaml::Markup::XamlReader::Load(
            hstring(kQuickNavigationItemTemplate));
        navigation_quick_list_.ItemTemplate(loaded.as<DataTemplate>());
    }

    // Handle wheel messages at the desktop input boundary: nested WinUI
    // template scroll viewers may consume them before XAML routed handlers.
    // All callbacks run on this window's UI thread and are removed on close.
    static LRESULT CALLBACK NavigationWheelSubclass(HWND window, UINT message,
        WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR context) noexcept
    {
        auto* app = reinterpret_cast<App*>(context);
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(window, NavigationWheelSubclass, id);
        }
        else if (message == WM_LBUTTONDOWN || message == WM_POINTERDOWN || message == WM_KEYDOWN)
        {
            app->navigation_wheel_target_.reset();
        }
        else if (message == WM_MOUSEWHEEL || message == WM_POINTERWHEEL)
        {
            try
            {
                POINT point{ static_cast<short>(LOWORD(lparam)),
                    static_cast<short>(HIWORD(lparam)) };
                const bool control = message == WM_MOUSEWHEEL ?
                    (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) != 0 :
                    (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (app->IsPointOverNavigationPane(point) && !control)
                {
                    app->ScrollNavigationPane(GET_WHEEL_DELTA_WPARAM(wparam));
                    return 0;
                }
            }
            catch (...)
            {
                app->startup_.GenericFailure("navigation-wheel.native");
            }
        }
        return DefSubclassProc(window, message, wparam, lparam);
    }

    void AddNavigationWheelWindow(HWND window)
    {
        if (GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId())
        {
            return;
        }
        const auto id = reinterpret_cast<UINT_PTR>(this);
        DWORD_PTR context{};
        if (!GetWindowSubclass(window, NavigationWheelSubclass, id, &context) &&
            SetWindowSubclass(window, NavigationWheelSubclass, id,
                reinterpret_cast<DWORD_PTR>(this)))
        {
            navigation_wheel_windows_.push_back(window);
        }
    }

    void InstallNavigationWheelInput()
    {
        winrt::check_hresult(window_.as<::IWindowNative>()->get_WindowHandle(
            &navigation_window_handle_));
        AddNavigationWheelWindow(navigation_window_handle_);
        EnumChildWindows(navigation_window_handle_, [](HWND child, LPARAM context) -> BOOL {
            auto* app = reinterpret_cast<App*>(context);
            try { app->AddNavigationWheelWindow(child); }
            catch (...) { return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(this));
    }

    void RemoveNavigationWheelInput() noexcept
    {
        for (const auto window : navigation_wheel_windows_)
        {
            RemoveWindowSubclass(window, NavigationWheelSubclass,
                reinterpret_cast<UINT_PTR>(this));
        }
        navigation_wheel_windows_.clear();
        navigation_window_handle_ = nullptr;
    }

    bool IsPointOverNavigationPane(POINT screen_point) const
    {
        if (!navigation_pane_ || !root_.XamlRoot() ||
            !ScreenToClient(navigation_window_handle_, &screen_point))
        {
            return false;
        }
        const auto scale = root_.XamlRoot().RasterizationScale();
        const auto bounds = navigation_pane_.TransformToVisual(root_).TransformBounds(
            Windows::Foundation::Rect{ 0, 0,
                static_cast<float>(navigation_pane_.ActualWidth()),
                static_cast<float>(navigation_pane_.ActualHeight()) });
        const auto x = screen_point.x / scale;
        const auto y = screen_point.y / scale;
        return x >= bounds.X && x < bounds.X + bounds.Width &&
            y >= bounds.Y && y < bounds.Y + bounds.Height;
    }

    void ScrollNavigationPane(const int wheel_delta)
    {
        UINT lines = 3;
        if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
        {
            lines = 3;
        }
        if (lines == 0 || navigation_pane_.ScrollableHeight() <= 0)
        {
            return;
        }
        constexpr double kNavigationLineHeight = 32.0;
        const double step = lines == WHEEL_PAGESCROLL ? navigation_pane_.ViewportHeight() :
            kNavigationLineHeight * static_cast<double>(lines);
        // Accumulate against the most recent request while a frame is pending.
        const double target = std::clamp(navigation_wheel_target_.value_or(
            navigation_pane_.VerticalOffset()) -
            static_cast<double>(wheel_delta) / WHEEL_DELTA * step,
            0.0, navigation_pane_.ScrollableHeight());
        navigation_wheel_target_ = target;
        if (!navigation_pane_.ChangeView(nullptr,
            winrt::box_value(target).as<Windows::Foundation::IReference<double>>(), nullptr, true))
        {
            navigation_wheel_target_.reset();
        }
        if (!startup_.Path().empty())
        {
            startup_.Stage("navigation-wheel.forwarded target=" + std::to_string(target));
        }
    }

    static void DisableNavigationInternalScrolling(
        const DependencyObject& root)
    {
        if (!root)
        {
            return;
        }

        const auto child_count = VisualTreeHelper::GetChildrenCount(root);
        for (std::int32_t index = 0; index < child_count; ++index)
        {
            const auto child = VisualTreeHelper::GetChild(root, index);
            if (const auto scroll_viewer = child.try_as<ScrollViewer>())
            {
                scroll_viewer.VerticalScrollBarVisibility(
                    ScrollBarVisibility::Disabled);
                scroll_viewer.VerticalScrollMode(ScrollMode::Disabled);
                scroll_viewer.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
                scroll_viewer.HorizontalScrollMode(ScrollMode::Disabled);
                scroll_viewer.ZoomMode(ZoomMode::Disabled);
            }
            DisableNavigationInternalScrolling(child);
        }
    }

    void ConfigureNavigationTreeItemTemplate()
    {
        // For RootNodes-managed trees WinUI supplies a TreeViewNode to the
        // ItemTemplate. The app data lives in TreeViewNode.Content, so bind
        // through Content to the presentation-only Label field. NodeId stays
        // separate and is never used as display text.
        constexpr const wchar_t* kNavigationTreeItemTemplate = LR"XAML(
<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <TextBlock Text="{Binding Content[Label]}" Padding="8,5,8,5" />
</DataTemplate>)XAML";
        auto loaded = Microsoft::UI::Xaml::Markup::XamlReader::Load(
            hstring(kNavigationTreeItemTemplate));
        navigation_tree_.ItemTemplate(loaded.as<DataTemplate>());
    }

    [[nodiscard]] TreeViewNode MaterializeNavigationTreeNode(
        const blink::app::NavigationTreeNodeId node_id)
    {
        const auto* node = navigation_tree_model_.Find(node_id);
        if (!node)
        {
            return TreeViewNode{ };
        }

        auto tree_node = TreeViewNode();
        tree_node.Content(MakeNavigationTreeContent(*node));
        const bool has_unrealized_children = node->can_expand &&
            !node->reparse_point &&
            node->load_state != blink::app::NavigationTreeLoadState::Loaded;
        tree_node.HasUnrealizedChildren(has_unrealized_children);
        navigation_tree_ui_nodes_[node_id] = tree_node;

        if (node->load_state == blink::app::NavigationTreeLoadState::Loaded)
        {
            for (const auto child_id : node->child_ids)
            {
                tree_node.Children().Append(MaterializeNavigationTreeNode(child_id));
            }
        }
        return tree_node;
    }

    void SetNavigationTreeExpanded(
        const TreeViewNode& node,
        const bool expanded)
    {
        const auto node_id = NavigationTreeNodeIdFromItem(node.Content());
        startup_.Stage(
            std::string("navigation-tree.is-expanded.set.node=") +
            (node_id.has_value() ? std::to_string(*node_id) : "unknown") +
            ".value=" + (expanded ? "true" : "false"));
        navigation_tree_expansion_sync_suppressed_ = true;
        node.IsExpanded(expanded);
        navigation_tree_expansion_sync_suppressed_ = false;
    }

    void UpdateNavigationTreeNodeVisual(
        const blink::app::NavigationTreeNodeId node_id)
    {
        const auto* node = navigation_tree_model_.Find(node_id);
        const auto iterator = navigation_tree_ui_nodes_.find(node_id);
        if (!node || iterator == navigation_tree_ui_nodes_.end())
        {
            return;
        }

        iterator->second.Content(MakeNavigationTreeContent(*node));
        const bool has_unrealized_children = node->can_expand &&
            !node->reparse_point &&
            node->load_state != blink::app::NavigationTreeLoadState::Loaded;
        iterator->second.HasUnrealizedChildren(has_unrealized_children);
    }

    void MaterializeNavigationTreeChildren(
        const blink::app::NavigationTreeNodeId parent_id)
    {
        const auto* parent = navigation_tree_model_.Find(parent_id);
        const auto iterator = navigation_tree_ui_nodes_.find(parent_id);
        if (!parent || iterator == navigation_tree_ui_nodes_.end())
        {
            return;
        }

        const auto child_count = parent->child_ids.size();
        startup_.Stage(
            std::string("navigation-tree.children-materialization.begin.parent=") +
            std::to_string(parent_id) + ".count=" + std::to_string(child_count));
        // Copy the WinRT value before recursive materialization. Each child
        // insertion may rehash navigation_tree_ui_nodes_, invalidating the
        // iterator into that unordered_map.
        auto parent_tree_node = iterator->second;
        parent_tree_node.Children().Clear();
        for (const auto child_id : parent->child_ids)
        {
            parent_tree_node.Children().Append(MaterializeNavigationTreeNode(child_id));
        }
        parent_tree_node.HasUnrealizedChildren(false);
        startup_.Stage(
            std::string("navigation-tree.children-materialization.end.parent=") +
            std::to_string(parent_id) + ".count=" + std::to_string(child_count));
    }

    void ResetNavigationTreeUi()
    {
        if (!navigation_tree_)
        {
            navigation_tree_ui_nodes_.clear();
            return;
        }

        const bool previous_selection_suppressed =
            navigation_tree_selection_sync_suppressed_;
        navigation_tree_selection_sync_suppressed_ = true;
        try
        {
            // SelectedNodes must release its TreeViewNode references before
            // RootNodes or the presentation-only node cache is destroyed.
            navigation_tree_.SelectedNodes().Clear();
            navigation_tree_.RootNodes().Clear();
            navigation_tree_ui_nodes_.clear();
        }
        catch (...)
        {
            navigation_tree_selection_sync_suppressed_ =
                previous_selection_suppressed;
            throw;
        }
        navigation_tree_selection_sync_suppressed_ = previous_selection_suppressed;
    }

    void ResetNavigationTree(
        const std::vector<blink::app::NavigationTreeNodeSpec>& children)
    {
        CancelTreeRevealWork();
        CancelTreeExpansionWork();
        ++navigation_tree_lifetime_generation_;
        const auto root_id = navigation_tree_model_.ResetTopLevel(
            localization_.GetString(L"Location.ThisPC"),
            children);
        ResetNavigationTreeUi();
        if (!navigation_tree_ || root_id == 0)
        {
            return;
        }

        auto root_node = MaterializeNavigationTreeNode(root_id);
        SetNavigationTreeExpanded(root_node, true);
        navigation_tree_.RootNodes().Append(root_node);
    }

    void ApplyNavigationTreeContent(const blink::app::ThisPcContent& content)
    {
        std::vector<blink::app::NavigationTreeNodeSpec> children;
        children.reserve(content.items.size());
        std::vector<QuickNavigationItem> new_quick_items;
        for (const auto& item : content.items)
        {
            if (item.kind == blink::app::ThisPcItemKind::Place)
            {
                const auto label = item.label_key.empty() ? item.display_name :
                    localization_.GetString(item.label_key);
                if (!label.empty() && item.target.IsValid())
                {
                    new_quick_items.push_back(QuickNavigationItem{
                        label,
                        item.target });
                }
                continue;
            }

            const auto label = item.display_name.empty() ?
                item.target.FolderPath() : item.display_name;
            if (!label.empty() && item.target.IsValid())
            {
                children.push_back(blink::app::NavigationTreeNodeSpec{
                    blink::app::NavigationTreeNodeKind::Volume,
                    label,
                    item.target,
                    true,
                    false });
            }
        }

        if (quick_navigation_items_ != new_quick_items)
        {
            quick_navigation_items_ = std::move(new_quick_items);
            UpdateQuickNavigationItems();
        }

        const bool model_rebuilt = navigation_tree_model_.SynchronizeThisPcContent(
            localization_.GetString(L"Location.ThisPC"),
            children);

        if (model_rebuilt)
        {
            CancelTreeRevealWork();
            CancelTreeExpansionWork();
            ++navigation_tree_lifetime_generation_;
            const auto root_id = navigation_tree_model_.RootId();
            ResetNavigationTreeUi();
            if (navigation_tree_ && root_id != 0)
            {
                auto root_node = MaterializeNavigationTreeNode(root_id);
                SetNavigationTreeExpanded(root_node, true);
                navigation_tree_.RootNodes().Append(root_node);
            }
        }

        SyncNavigationPaneToCommittedLocation();
    }

    void UpdateQuickNavigationItems()
    {
        if (!navigation_quick_list_ || !navigation_quick_items_source_)
        {
            return;
        }

        navigation_quick_selection_sync_suppressed_ = true;
        navigation_quick_items_source_.Clear();
        for (std::size_t index = 0; index < quick_navigation_items_.size(); ++index)
        {
            auto item = winrt::Windows::Foundation::Collections::PropertySet();
            item.Insert(L"QuickId", winrt::box_value(static_cast<std::uint64_t>(index)));
            item.Insert(
                L"Label",
                winrt::box_value(hstring(quick_navigation_items_[index].label)));
            navigation_quick_items_source_.Append(item);
        }
        navigation_quick_list_.SelectedIndex(-1);
        navigation_quick_selection_sync_suppressed_ = false;
    }

    [[nodiscard]] std::optional<std::size_t> FindQuickNavigationItem(
        const blink::app::Location& target) const
    {
        for (std::size_t index = 0; index < quick_navigation_items_.size(); ++index)
        {
            if (quick_navigation_items_[index].target == target)
            {
                return index;
            }
        }
        return std::nullopt;
    }

    void ClearQuickNavigationSelection()
    {
        if (!navigation_quick_list_)
        {
            return;
        }
        navigation_quick_selection_sync_suppressed_ = true;
        navigation_quick_list_.SelectedIndex(-1);
        navigation_quick_selection_sync_suppressed_ = false;
    }

    void SelectQuickNavigationItem(const std::size_t index)
    {
        if (!navigation_quick_list_ || index >= quick_navigation_items_.size())
        {
            ClearQuickNavigationSelection();
            return;
        }
        navigation_quick_selection_sync_suppressed_ = true;
        navigation_quick_list_.SelectedIndex(static_cast<int32_t>(index));
        navigation_quick_selection_sync_suppressed_ = false;
    }

    void OnNavigationQuickItemClick(const ItemClickEventArgs& args)
    {
        if (navigation_quick_selection_sync_suppressed_)
        {
            return;
        }
        const auto item = args.ClickedItem().try_as<
            winrt::Windows::Foundation::Collections::PropertySet>();
        if (!item || !item.HasKey(L"QuickId"))
        {
            return;
        }
        try
        {
            const auto index = winrt::unbox_value<std::uint64_t>(
                item.Lookup(L"QuickId"));
            if (index < quick_navigation_items_.size())
            {
                NavigateToLocation(quick_navigation_items_[index].target);
            }
        }
        catch (...)
        {
            // A malformed presentation item is not a navigation request.
        }
    }

    [[nodiscard]] std::optional<blink::app::NavigationTreeNodeId>
        NavigationTreeNodeIdFromItem(const IInspectable& item) const
    {
        auto element = item.try_as<FrameworkElement>();
        if (element && element.Tag())
        {
            try
            {
                return winrt::unbox_value<blink::app::NavigationTreeNodeId>(element.Tag());
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        const auto properties = item.try_as<
            winrt::Windows::Foundation::Collections::PropertySet>();
        if (properties && properties.HasKey(L"NodeId"))
        {
            try
            {
                return winrt::unbox_value<blink::app::NavigationTreeNodeId>(
                    properties.Lookup(L"NodeId"));
            }
            catch (...)
            {
                return std::nullopt;
            }
        }

        const auto tree_node = item.try_as<TreeViewNode>();
        if (tree_node)
        {
            return NavigationTreeNodeIdFromItem(tree_node.Content());
        }
        return std::nullopt;
    }

    void ClearNavigationTreeSelection()
    {
        if (!navigation_tree_)
        {
            return;
        }
        const bool previous_selection_suppressed =
            navigation_tree_selection_sync_suppressed_;
        navigation_tree_selection_sync_suppressed_ = true;
        try
        {
            navigation_tree_.SelectedNodes().Clear();
        }
        catch (...)
        {
            navigation_tree_selection_sync_suppressed_ =
                previous_selection_suppressed;
            throw;
        }
        navigation_tree_selection_sync_suppressed_ = previous_selection_suppressed;
    }

    void SelectNavigationTreeNode(
        const blink::app::NavigationTreeNodeId node_id)
    {
        const auto iterator = navigation_tree_ui_nodes_.find(node_id);
        if (!navigation_tree_ || iterator == navigation_tree_ui_nodes_.end())
        {
            ClearNavigationTreeSelection();
            return;
        }
        ClearNavigationTreeSelection();
        const bool previous_selection_suppressed =
            navigation_tree_selection_sync_suppressed_;
        navigation_tree_selection_sync_suppressed_ = true;
        try
        {
            navigation_tree_.SelectedNodes().Append(iterator->second);
        }
        catch (...)
        {
            navigation_tree_selection_sync_suppressed_ =
                previous_selection_suppressed;
            throw;
        }
        navigation_tree_selection_sync_suppressed_ = previous_selection_suppressed;
    }

    void OnNavigationTreeItemInvoked(
        const TreeViewItemInvokedEventArgs& args)
    {
        const auto node_id = NavigationTreeNodeIdFromItem(args.InvokedItem());
        const auto* node = node_id.has_value() ?
            navigation_tree_model_.Find(*node_id) : nullptr;
        if (!node || !node->target.IsValid())
        {
            return;
        }

        SelectNavigationTreeNode(node->id);
        NavigateToLocation(node->target);
        args.Handled(true);
    }

    void OnNavigationTreeExpanding(
        const TreeViewExpandingEventArgs& args)
    {
        if (navigation_tree_expansion_sync_suppressed_)
        {
            startup_.Stage("navigation-tree.expanding.suppressed");
            return;
        }
        const auto node = args.Node();
        startup_.Stage("navigation-tree.expanding.received");
        const auto node_id = NavigationTreeNodeIdFromItem(node.Content());
        if (!node_id.has_value())
        {
            startup_.Stage("navigation-tree.expanding.node-unresolved");
            return;
        }
        const auto* model_node = navigation_tree_model_.Find(*node_id);
        startup_.Stage(
            std::string("navigation-tree.expanding.node-resolved.node=") +
            std::to_string(*node_id) +
            ".is_expanded=" + (node.IsExpanded() ? "true" : "false") +
            ".has_unrealized=" + (node.HasUnrealizedChildren() ? "true" : "false") +
            ".state=" + (model_node ?
                std::to_string(static_cast<int>(model_node->load_state)) : "unknown") +
            ".generation=" + (model_node ?
                std::to_string(model_node->expansion_generation) : "unknown"));
        if (node_id.has_value())
        {
            if (const auto iterator = navigation_tree_pending_expansions_.find(*node_id);
                iterator != navigation_tree_pending_expansions_.end() &&
                model_node &&
                model_node->load_state == blink::app::NavigationTreeLoadState::Loading &&
                iterator->second.generation == model_node->expansion_generation)
            {
                iterator->second.user_intent_still_expand = true;
                ++iterator->second.expanding_event_count;
                startup_.Stage(
                    std::string("navigation-tree.expansion.intent-reaffirmed.node=") +
                    std::to_string(*node_id) + ".events=" +
                    std::to_string(iterator->second.expanding_event_count));
            }
            StartNavigationTreeExpansion(*node_id);
        }
    }

    void OnNavigationTreeCollapsed(
        const TreeViewCollapsedEventArgs& args)
    {
        if (navigation_tree_expansion_sync_suppressed_)
        {
            startup_.Stage("navigation-tree.collapsed.suppressed");
            return;
        }

        const auto node = args.Node();
        const auto node_id = NavigationTreeNodeIdFromItem(node.Content());
        if (!node_id.has_value())
        {
            startup_.Stage("navigation-tree.collapsed.node-unresolved");
            return;
        }

        const auto* model_node = navigation_tree_model_.Find(*node_id);
        const auto iterator = navigation_tree_pending_expansions_.find(*node_id);
        startup_.Stage(
            std::string("navigation-tree.collapsed.node=") +
            std::to_string(*node_id) + ".is_expanded=" +
            (node.IsExpanded() ? "true" : "false") +
            ".has_unrealized=" +
            (node.HasUnrealizedChildren() ? "true" : "false") +
            ".state=" + (model_node ?
                std::to_string(static_cast<int>(model_node->load_state)) : "unknown") +
            ".pending=" + (iterator != navigation_tree_pending_expansions_.end() ?
                "true" : "false"));
        if (iterator != navigation_tree_pending_expansions_.end() &&
            model_node &&
            model_node->load_state == blink::app::NavigationTreeLoadState::Loading &&
            iterator->second.generation == model_node->expansion_generation)
        {
            iterator->second.user_intent_still_expand = false;
            startup_.Stage(
                std::string("navigation-tree.expansion.intent-cancelled.node=") +
                std::to_string(*node_id) + ".generation=" +
                std::to_string(iterator->second.generation));
        }
    }

    void SelectAllFolderRows()
    {
        const auto* tab_ui = ActiveTabUi();
        if (!tab_ui || !entries_ || !tab_ui->showing_committed_folder ||
            tab_ui->presentation_in_progress || ActiveState().PresentationSnapshot().has_pending)
        {
            return;
        }
        if (!ActiveState().SelectAllRows())
        {
            status_.Text(hstring(localization_.GetString(L"Status.SelectAllFailed")));
            return;
        }
        ++folder_selection_generation_;
        CancelFolderSystemMenu();
        selection_sync_suppressed_ = true;
        try
        {
            entries_.SelectAll();
            const auto selected = entries_.SelectedItems();
            for (std::size_t index = 0; index < tab_ui->row_models.size(); ++index)
            {
                if (!tab_ui->row_models[index].identity.valid &&
                    tab_ui->row_items_source && index < tab_ui->row_items_source.Size())
                {
                    std::uint32_t selected_index{};
                    if (selected.IndexOf(tab_ui->row_items_source.GetAt(
                            static_cast<std::uint32_t>(index)), selected_index))
                    {
                        selected.RemoveAt(selected_index);
                    }
                }
            }
        }
        catch (...)
        {
            selection_sync_suppressed_ = false;
            startup_.GenericFailure("selection.select-all");
            status_.Text(hstring(localization_.GetString(L"Status.SelectAllFailed")));
            return;
        }
        selection_sync_suppressed_ = false;
        UpdateSelectionStatus();
    }

    void ActivateFocusedRow()
    {
        if (!entries_)
        {
            return;
        }
        auto index = ResolveRowIndexFromElement(
            FocusManager::GetFocusedElement(entries_.XamlRoot()));
        const auto* tab_ui = ActiveTabUi();
        if (index.has_value() && tab_ui && tab_ui->showing_committed_folder &&
            (*index >= tab_ui->row_models.size() ||
                !ActiveState().IsRowSelected(tab_ui->row_models[*index].row_identity)))
        {
            index.reset();
        }
        if (!index.has_value() && entries_.SelectedItems().Size() != 0)
        {
            index = RowIndexForItem(entries_.SelectedItems().GetAt(0));
        }
        if (index.has_value())
        {
            ActivatePresentedRow(*index);
        }
    }

    void OnFolderDoubleTapped(const DoubleTappedRoutedEventArgs& args)
    {
        startup_.Stage("folder-double-tap.received");
        const auto clicked = ResolveClickedRowIndex(args);
        if (clicked.has_value())
        {
            ActivatePresentedRow(*clicked);
            args.Handled(true);
        }
    }

    void ActivatePresentedRow(const std::size_t index)
    {
        const auto* tab_ui = ActiveTabUi();
        if (!tab_ui || tab_ui->presentation_in_progress ||
            ActiveState().PresentationSnapshot().has_pending)
        {
            return;
        }
        if (tab_ui->showing_this_pc)
        {
            if (index < tab_ui->this_pc_rows.size())
            {
                NavigateToLocation(tab_ui->this_pc_rows[index].target);
            }
            return;
        }
        if (!tab_ui->showing_committed_folder || index >= tab_ui->row_models.size())
        {
            return;
        }
        const auto& row = tab_ui->row_models[index];
        if (row.is_directory)
        {
            BrowseAsync(row.full_path);
            return;
        }
        const auto entry = ActiveState().CommittedRow(row.row_identity);
        auto* tab = ActiveTab();
        if (!entry || !tab || !tab->file_activation_controller)
        {
            status_.Text(hstring(localization_.GetString(L"Status.OpenChanged")));
            return;
        }
        const auto tab_id = tab->id;
        const auto dispatcher = dispatcher_;
        const auto weak_app = get_weak();
        (void)tab->file_activation_controller->Open(*entry,
            [weak_app, dispatcher, tab_id](const std::uint64_t generation) {
                (void)dispatcher.TryEnqueue([weak_app, tab_id, generation] {
                    if (auto app = weak_app.get())
                    {
                        const auto* tab = app->FindTab(tab_id);
                        if (!tab || !tab->file_activation_controller ||
                            !tab->file_activation_controller->IsCurrent(generation))
                        {
                            return;
                        }
                        if (tab_id == app->tab_window_state_.ActiveTabId())
                        {
                            app->UpdateBrowseStatus();
                        }
                    }
                });
            });
        startup_.Stage("file-activation.submitted");
        UpdateBrowseStatus();
    }

    void OnThisPcTapped(const TappedRoutedEventArgs& args)
    {
        const auto* tab_ui = ActiveTabUi();
        if (!tab_ui || !tab_ui->showing_this_pc || ActiveState().PresentationSnapshot().has_pending)
        {
            return;
        }

        const auto clicked = ResolveClickedRowIndex(args);
        if (!clicked.has_value() || *clicked >= tab_ui->this_pc_rows.size())
        {
            return;
        }

        NavigateToLocation(tab_ui->this_pc_rows[*clicked].target);
        args.Handled(true);
    }

    void NavigateToLocation(const blink::app::Location& location)
    {
        if (const auto request = ActiveState().Navigate(location); request.has_value())
        {
            StartNavigation(*request);
        }
    }

    void UpdateSelectionStatus()
    {
        if (folder_system_menu_context_ && !IsFolderMenuContextCurrent(*folder_system_menu_context_))
        { CancelFolderSystemMenu(); }
        if (const auto* tab = ActiveTab(); tab && tab->search_visible)
        {
            selection_status_.Text(hstring(localization_.Format(L"Status.Selected", tab->search_selection ? 1u : 0u)));
            return;
        }
        startup_.Stage("selection-status.begin");
        const auto snapshot = ActiveState().PresentationSnapshot();
        selection_status_.Text(hstring(
            localization_.Format(L"Status.Selected", snapshot.selected_count)));
        startup_.Stage("selection-status.end");
    }

    void UpdateTabLabel(const blink::app::TabId tab_id = 0)
    {
        startup_.Stage("tab-label.begin");
        const auto active = tab_id ? tab_id : tab_window_state_.ActiveTabId();
        const auto iterator = std::find(tab_item_ids_.begin(), tab_item_ids_.end(), active);
        if (iterator == tab_item_ids_.end() || !tab_view_)
        {
            startup_.Stage("tab-label.unavailable");
            return;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(tab_item_ids_.begin(), iterator));
        if (index < tab_view_.TabItems().Size())
        {
            if (const auto item = tab_view_.TabItems().GetAt(
                    static_cast<std::uint32_t>(index)).try_as<TabViewItem>())
            {
                item.Header(winrt::box_value(hstring(TabTitle(active))));
            }
        }
        startup_.Stage("tab-label.updated");
    }

    void UpdateBrowseStatus()
    {
        startup_.Stage("browse-status.begin");
        const auto tab_snapshot = ActiveState().PresentationSnapshot();
        const auto active_tab = ActiveTab();
        const auto browse_snapshot = active_tab && active_tab->browse_state ?
            active_tab->browse_state->Snapshot() : blink::app::BrowseSnapshot{};
        auto text = blink::app::FormatBrowseActivity(browse_snapshot, tab_snapshot);
        if (!tab_snapshot.has_pending && active_tab && active_tab->file_activation_controller)
        {
            const auto activation = active_tab->file_activation_controller->Snapshot();
            switch (activation.stage)
            {
            case blink::app::FileActivationStage::Opening:
                text = localization_.Format(L"Status.OpeningFile", activation.path);
                break;
            case blink::app::FileActivationStage::Dispatched:
                text = localization_.Format(L"Status.OpenDispatched", activation.path);
                break;
            case blink::app::FileActivationStage::Failed:
                if (activation.error == HRESULT_FROM_WIN32(ERROR_FILE_INVALID))
                {
                    text = localization_.GetString(L"Status.OpenChanged");
                }
                else if (activation.error == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION))
                {
                    text = localization_.Format(L"Status.OpenNoAssociation", activation.path);
                }
                else if (activation.error == HRESULT_FROM_WIN32(ERROR_TIMEOUT))
                {
                    text = localization_.GetString(L"Status.OpenTimedOut");
                }
                else
                {
                    std::wstringstream error;
                    error << L"0x" << std::hex << std::setw(8) << std::setfill(L'0')
                        << static_cast<std::uint32_t>(activation.error);
                    text = localization_.Format(L"Status.OpenFailed",
                        std::vector<std::wstring>{ activation.path, error.str() });
                }
                break;
            default:
                break;
            }
        }
        if (active_tab && active_tab->folder_watch_error != ERROR_SUCCESS &&
            tab_snapshot.committed_location.IsFolder() && !tab_snapshot.has_pending)
        {
            text += L"  " + localization_.Format(L"Status.WatchUnavailable",
                std::to_wstring(active_tab->folder_watch_error));
        }
        if (active_tab && !active_tab->search_visible && active_tab->search_action_error != ERROR_SUCCESS)
        { text = localization_.GetString(L"Search.ResultChanged"); }
        status_.Text(hstring(text));
        startup_.Stage("browse-status.text");
        UpdateTabLabel();
        startup_.Stage("browse-status.tab");
        UpdateSelectionStatus();
        startup_.Stage("browse-status.end");
        PresentSearchSurface();
        RenderFileOperationStatus();
    }

    void BrowseAsync(std::wstring path)
    {
        const auto location = localization_.IsThisPcDisplayText(path) ?
            blink::app::Location::ThisPC() :
            blink::app::Location::Folder(std::move(path));
        const auto request = ActiveState().Navigate(location);
        if (!request.has_value())
        {
            RestoreNavigationUi();
            status_.Text(hstring(localization_.GetString(L"Status.InvalidPath")));
            return;
        }
        StartNavigation(*request);
    }

    void NavigateBack()
    {
        if (const auto* tab = ActiveTab(); tab && tab->search_visible) { LeaveActiveSearch(); return; }
        if (const auto request = ActiveState().Back(); request.has_value())
        {
            StartNavigation(*request);
        }
    }

    void NavigateForward()
    {
        if (const auto request = ActiveState().Forward(); request.has_value())
        {
            StartNavigation(*request);
        }
    }

    void NavigateUp()
    {
        if (const auto request = ActiveState().Up(); request.has_value())
        {
            StartNavigation(*request);
        }
    }

    void RefreshNavigation()
    {
        if (const auto* tab = ActiveTab(); tab && tab->search_visible) { SubmitSearch(false, true); return; }
        if (const auto request = ActiveState().Refresh(); request.has_value())
        {
            StartNavigation(*request);
        }
    }

    void BuildSearchSurface()
    {
        const auto weak = get_weak();
        blink::app::SearchPresentationCallbacks callbacks;
        callbacks.submit = [weak] { if (auto app = weak.get()) { app->SubmitSearch(false); } };
        callbacks.retry = [weak] { if (auto app = weak.get()) { app->SubmitSearch(true); } };
        callbacks.leave = [weak] { if (auto app = weak.get()) { app->LeaveActiveSearch(); } };
        callbacks.options_changed = [weak](blink::app::SearchPresentationSettings settings) {
            if (auto app = weak.get())
            {
                if (auto* tab = app->ActiveTab())
                {
                    tab->search_scope = settings.scope; tab->search_mode = settings.mode;
                    tab->search_selected_volumes = std::move(settings.selected_volumes);
                    tab->search_selected_folder = std::move(settings.selected_folder);
                    tab->search_include_subfolders = settings.include_subfolders;
                    if (tab->search_visible) { app->PresentSearchSurface(); }
                }
            }
        };
        callbacks.use_current_folder = [weak] {
            if (auto app = weak.get())
            {
                if (auto* tab = app->ActiveTab())
                {
                    const auto location = tab->state->PresentationSnapshot().committed_location;
                    if (!location.IsFolder()) { return; }
                    tab->search_scope = blink::app::SearchScope::SelectedFolder;
                    tab->search_selected_folder = location.FolderPath();
                    if (tab->search_visible) { app->PresentSearchSurface(); }
                }
            }
        };
        callbacks.visibility_changed = [weak](bool show) { if (auto app = weak.get()) { app->SetShowHiddenItems(show); } };
        callbacks.selection_changed = [weak](std::optional<blink::app::SearchResult> result) {
            if (auto app = weak.get())
            {
                if (auto* tab = app->ActiveTab(); tab && tab->search_visible)
                { tab->search_selection = std::move(result); app->UpdateSelectionStatus(); }
            }
        };
        callbacks.activate = [weak](blink::app::SearchResult result, bool locate) {
            if (auto app = weak.get()) { app->StartSearchResultAction(std::move(result), locate); }
        };
        callbacks.context_menu = [weak](blink::app::SearchContextSelection selection, Windows::Foundation::Point point) {
            if (auto app = weak.get()) { app->ShowSearchContextMenu(std::move(selection), point); }
        };
        callbacks.command = [weak](blink::app::SearchContextSelection selection, blink::app::FolderCommand command) {
            if (auto app = weak.get()) { app->ExecuteSearchKeyboardCommand(std::move(selection), command); }
        };
        callbacks.dismiss_context_menu = [weak](std::uint64_t tab_id, std::uint64_t generation) {
            if (auto app = weak.get(); app && app->search_menu_selection_ &&
                app->search_menu_selection_->tab_id == tab_id && app->search_menu_selection_->query_generation == generation)
            { app->DismissSearchContextMenu(); }
        };
        search_surface_ = search_presentation_.Build(localization_, dispatcher_, std::move(callbacks));
        search_surface_.Visibility(Visibility::Collapsed);
    }

    void PresentSearchSurface()
    {
        auto* tab = ActiveTab(); if (!tab || !search_surface_ || window_style_closed_) { return; }
        if (search_menu_selection_ && (!blink::app::IsSearchContextCurrent(*search_menu_selection_, tab) ||
            tab->file_operation_generation != search_menu_owner_generation_)) { DismissSearchContextMenu(); }
        const auto visible = tab->search_visible;
        content_grid_.Visibility(visible ? Visibility::Collapsed : Visibility::Visible);
        search_surface_.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
        search_input_sync_ = true;
        if (search_box_.Text() != tab->search_draft) { search_box_.Text(hstring(tab->search_draft)); }
        search_input_sync_ = false;
        if (!visible) { search_presentation_.Suspend(); return; }
        blink::app::SearchPresentationOptions options;
        options.tab_id = tab->id; options.queued = tab->search_queued;
        const auto location = tab->state->PresentationSnapshot().committed_location;
        options.current_folder_allowed = location.IsFolder();
        options.scope = tab->search_scope; options.mode = tab->search_mode;
        options.selected_volumes = tab->search_selected_volumes;
        options.selected_folder = tab->search_selected_folder;
        options.include_subfolders = tab->search_include_subfolders;
        using Scope = blink::app::SearchScope;
        switch (options.scope)
        {
        case Scope::AllLocalVolumes:
            options.effective_scope = localization_.GetString(L"Search.AllVolumes"); break;
        case Scope::FollowLocation:
        case Scope::CurrentVolume:
            options.effective_scope = location.IsFolder() ? blink::app::SearchDriveRoot(location.FolderPath()) :
                localization_.GetString(L"Search.AllVolumes");
            break;
        case Scope::SelectedVolumes:
            options.effective_scope = options.selected_volumes; break;
        case Scope::CurrentFolder:
            if (location.IsFolder()) { options.effective_scope = location.FolderPath(); }
            else { options.effective_scope = localization_.GetString(L"Search.CurrentFolderUnavailable"); }
            break;
        case Scope::SelectedFolder:
            options.effective_scope = options.selected_folder; break;
        }
        if (options.effective_scope.find_first_not_of(L" \t\r\n") == std::wstring::npos)
        { options.effective_scope = localization_.GetString(L"Search.ScopeNotSet"); }
        else if ((options.scope == Scope::CurrentFolder && location.IsFolder()) || options.scope == Scope::SelectedFolder)
        { options.effective_scope += L" · " + localization_.GetString(options.include_subfolders ?
            L"Search.IncludeSubfolders" : L"Search.FolderOnly"); }
        if (tab->search_snapshot)
        {
            const auto draft = tab->SearchIntent(false);
            const auto& committed = tab->search_snapshot->query;
            options.scope_draft = draft.scope != committed.scope || draft.mode != committed.mode || draft.folder != committed.folder ||
                draft.selected_volumes != committed.selected_volumes || draft.include_subfolders != committed.include_subfolders;
        }
        options.visibility = tab_window_state_.GetVisibilityPolicy(); options.selection = tab->search_selection;
        options.action_error = tab->search_action_error;
        options.action_pending = tab->search_action_pending ||
            (tab->search_action_work && tab->search_action_work.Status() == Windows::Foundation::AsyncStatus::Started);
        const blink::app::SearchSnapshot empty;
        search_presentation_.Update(tab->search_snapshot ? *tab->search_snapshot : empty, options, localization_);
    }

    void LeaveActiveSearch()
    {
        if (auto* tab = ActiveTab(); tab && tab->search_visible)
        {
            CloseOperationDialogs();
            search_requests_.RemoveTab(tab->id); tab->LeaveSearch();
            ++folder_view_generation_; PresentActiveTab();
            if (entries_) { (void)entries_.Focus(FocusState::Keyboard); }
        }
    }

    void SubmitSearch(const bool retry, const bool refresh_current = false)
    {
        auto* tab = ActiveTab();
        if (!tab || window_style_closed_ || (tab->state->PresentationSnapshot().has_pending && !tab->search_visible)) { return; }
        if (!retry && !refresh_current)
        {
            // TextChanged can be queued behind the key/click that submits.
            tab->search_draft = search_box_.Text().c_str();
            if (auto settings = search_presentation_.DraftSettings(tab->id))
            {
                tab->search_scope = settings->scope; tab->search_mode = settings->mode;
                tab->search_selected_volumes = std::move(settings->selected_volumes);
                tab->search_selected_folder = std::move(settings->selected_folder);
                tab->search_include_subfolders = settings->include_subfolders;
            }
        }
        auto query = tab->SearchIntent(retry || refresh_current);
        if (query.text.empty()) { LeaveActiveSearch(); return; }
        CloseOperationDialogs();
        tab->EnterSearch();
        const auto error = blink::app::ValidateSearchQuery(query);
        if (error != ERROR_SUCCESS) { tab->search_action_error = error; PresentSearchSurface(); return; }
        if (!retry)
        {
            tab->search_controller->Cancel(); tab->search_selection.reset();
            auto pending = std::make_shared<blink::app::SearchSnapshot>(); pending->query = query;
            tab->search_snapshot = std::move(pending);
        }
        tab->file_activation_controller->CancelPending();
        tab->search_queued = true;
        search_requests_.Enqueue({ tab->id, std::move(query), retry });
        ++folder_view_generation_;
        if (folder_menu_) { try { folder_menu_.Hide(); } catch (...) {} folder_menu_ = nullptr; }
        PresentSearchSurface(); UpdateNavigationButtons(); UpdateSelectionStatus();
        TryStartPendingSearch();
    }

    void SearchRequestFinished(const std::uint64_t serial, const blink::app::TabId id,
        std::shared_ptr<const blink::app::SearchSnapshot> snapshot)
    {
        if (window_style_closed_ || !search_requests_.Finish(serial)) { return; }
        search_work_ = nullptr;
        if (auto* tab = FindTab(id); tab && snapshot && tab->search_state->IsCurrent(snapshot->generation))
        { tab->search_snapshot = std::move(snapshot); }
        if (id == tab_window_state_.ActiveTabId()) { PresentSearchSurface(); UpdateSelectionStatus(); }
        TryStartPendingSearch();
    }

    void TryStartPendingSearch()
    {
        if (window_style_closed_ || search_requests_.Busy()) { return; }
        if (index_service_state_ && index_service_state_->Snapshot().stage == blink::app::IndexConnectionStage::Refreshing) { return; }
        const auto ticket = search_requests_.TakeNext();
        if (!ticket)
        {
            if (index_refresh_deferred_) { index_refresh_deferred_ = false; RefreshIndexServiceStatus(index_service_selected_root_); }
            return;
        }
        auto* tab = FindTab(ticket->request.tab_id);
        if (!tab || !tab->search_visible)
        { (void)search_requests_.Finish(ticket->serial); TryStartPendingSearch(); return; }
        tab->search_queued = false;
        const auto generation = tab->search_controller->Search(ticket->request.query, {}, ticket->request.retry);
        tab->search_snapshot = std::make_shared<blink::app::SearchSnapshot>(tab->search_state->Snapshot());
        search_work_ = tab->search_controller->CurrentAction();
        if (tab->id == tab_window_state_.ActiveTabId()) { PresentSearchSurface(); }
        if (generation == 0 || !search_work_)
        { SearchRequestFinished(ticket->serial, tab->id, tab->search_snapshot); return; }
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        const auto state = tab->search_state; const auto id = tab->id; const auto serial = ticket->serial;
        search_work_.Completed([weak, dispatcher, state, id, serial](auto&&, auto) {
            std::shared_ptr<const blink::app::SearchSnapshot> snapshot;
            try { snapshot = std::make_shared<blink::app::SearchSnapshot>(state->Snapshot()); } catch (...) {}
            (void)dispatcher.TryEnqueue([weak, id, serial, snapshot = std::move(snapshot)] {
                if (auto app = weak.get()) { app->SearchRequestFinished(serial, id, snapshot); }
            });
        });
    }

    void ApplySearchTarget(const blink::app::TabId id, const blink::app::SearchActionTarget& target, const bool locate)
    {
        auto* tab = FindTab(id); if (!tab) { return; }
        if (locate || target.entry.is_directory)
        {
            const auto path = locate ? target.parent : target.entry.full_path;
            const auto request = tab->state->Navigate(blink::app::Location::Folder(path));
            if (!request) { tab->search_action_error = ERROR_INVALID_NAME; return; }
            StartNavigation(id, *request, false, locate ? target.parent_identity : target.entry.identity,
                locate ? std::optional{ blink::app::MakeRowIdentity(target.entry) } : std::nullopt);
            return;
        }
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        (void)tab->file_activation_controller->Open(target.entry, [weak, dispatcher, id](std::uint64_t generation) {
            (void)dispatcher.TryEnqueue([weak, id, generation] {
                if (auto app = weak.get())
                {
                    const auto* tab = app->FindTab(id);
                    if (tab && tab->file_activation_controller->IsCurrent(generation) && id == app->tab_window_state_.ActiveTabId())
                    { app->UpdateBrowseStatus(); }
                }
            });
        });
        if (id == tab_window_state_.ActiveTabId()) { UpdateBrowseStatus(); }
    }

    void DismissSearchContextMenu() noexcept
    {
        folder_system_menu_context_.reset();
        ++search_menu_generation_;
        search_menu_selection_.reset();
        search_context_menu_presentation_.Hide();
        shell_context_menu_.CancelPending();
    }

    bool SearchContextCurrent(const blink::app::SearchContextSelection& selection, const std::uint64_t owner_generation) const
    {
        const auto* tab = ActiveTab();
        return blink::app::IsSearchContextCurrent(selection, tab, window_style_closed_) &&
            tab->file_operation_generation == owner_generation;
    }

    void ShowSearchContextMenu(blink::app::SearchContextSelection selection, Windows::Foundation::Point point)
    {
        auto* tab = ActiveTab();
        if (!blink::app::IsSearchContextCurrent(selection, tab, window_style_closed_) || tab->search_action_pending) { return; }
        DismissSearchContextMenu();
        search_menu_selection_ = selection;
        const auto generation = search_menu_generation_;
        const auto owner_generation = search_menu_owner_generation_ = tab->file_operation_generation;
        const auto weak = get_weak();
        blink::app::SearchContextMenuPresentation::Callbacks callbacks;
        callbacks.command = [weak, generation, owner_generation](blink::app::SearchContextSelection selected,
            blink::app::ShellMenuCommand command) {
            if (auto app = weak.get(); app && app->search_menu_generation_ == generation &&
                app->SearchContextCurrent(selected, owner_generation))
            {
                if (command == blink::app::ShellMenuCommand::Delete && (GetKeyState(VK_SHIFT) & 0x8000))
                { command = blink::app::ShellMenuCommand::PermanentDelete; }
                app->DismissSearchContextMenu();
                app->ExecuteSearchContextCommand(std::move(selected), command);
            }
        };
        callbacks.properties = [weak, generation, owner_generation](blink::app::SearchContextSelection selected) {
            if (auto app = weak.get(); app && app->search_menu_generation_ == generation &&
                app->SearchContextCurrent(selected, owner_generation))
            { app->ShowSearchSystemContextMenu(std::move(selected), {}, true); }
        };
        callbacks.system_options = [weak, generation, owner_generation](blink::app::SearchContextSelection selected,
            Windows::Foundation::Point position) {
            if (auto app = weak.get(); app && app->search_menu_generation_ == generation &&
                app->SearchContextCurrent(selected, owner_generation))
            { app->ShowSearchSystemContextMenu(std::move(selected), position); }
        };
        callbacks.closed = [weak, generation] {
            if (auto app = weak.get(); app && app->search_menu_generation_ == generation) { app->DismissSearchContextMenu(); }
        };
        try
        {
            if (!search_context_menu_presentation_.Show(window_frame_, point, std::move(selection), localization_,
                FileDropBusy(), IsClipboardFormatAvailable(CF_HDROP) != FALSE, std::move(callbacks)))
            { DismissSearchContextMenu(); }
        }
        catch (...)
        {
            DismissSearchContextMenu();
            status_.Text(hstring(localization_.Format(L"Search.ActionError", std::to_wstring(static_cast<unsigned>(winrt::to_hresult())))));
        }
    }

    void ShowSearchSystemContextMenu(blink::app::SearchContextSelection selection,
        Windows::Foundation::Point point = {}, const bool properties = false)
    {
        auto* tab = ActiveTab();
        if (!blink::app::IsSearchContextCurrent(selection, tab, window_style_closed_) || tab->search_action_pending) { return; }
        DismissSearchContextMenu();
        HWND owner{}; check_hresult(window_.as<::IWindowNative>()->get_WindowHandle(&owner));
        const auto scale = root_.XamlRoot().RasterizationScale();
        POINT screen{ static_cast<LONG>(std::lround(point.X * scale)), static_cast<LONG>(std::lround(point.Y * scale)) };
        if (!ClientToScreen(owner, &screen)) { return; }
        search_menu_selection_ = selection;
        const auto owner_generation = search_menu_owner_generation_ = tab->file_operation_generation;
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        try
        {
            (void)shell_context_menu_.Show(std::move(selection), screen, owner,
                PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? L"zh-CN" : L"en-US",
                (GetKeyState(VK_SHIFT) & 0x8000) != 0, properties,
                [weak, dispatcher, owner_generation](std::uint64_t generation, blink::app::SearchContextSelection selected,
                    blink::app::SearchActionTarget, blink::app::ShellMenuResult result) {
                    (void)dispatcher.TryEnqueue([weak, generation, owner_generation, selected = std::move(selected), result]() mutable {
                        if (auto app = weak.get(); app && app->shell_context_menu_.IsCurrent(generation) &&
                            app->SearchContextCurrent(selected, owner_generation))
                        {
                            app->search_menu_selection_.reset();
                            using blink::app::ShellMenuResultKind;
                            if (result.kind == ShellMenuResultKind::BlinkCommand)
                            { app->ExecuteSearchContextCommand(std::move(selected), result.command); }
                            else if (result.kind == ShellMenuResultKind::Error)
                            {
                                wchar_t code[16]{}; swprintf_s(code, L"0x%08X", static_cast<unsigned>(result.error));
                                app->status_.Text(hstring(app->localization_.Format(L"Search.MenuError", code)));
                            }
                            else if (result.kind == ShellMenuResultKind::NativeCommand)
                            { app->status_.Text(hstring(app->localization_.GetString(L"Search.NativeDispatched"))); }
                        }
                    });
                });
        }
        catch (...)
        {
            DismissSearchContextMenu();
            status_.Text(hstring(localization_.Format(L"Search.MenuError", L"0x80004005")));
        }
    }

    void ExecuteSearchKeyboardCommand(blink::app::SearchContextSelection selection, const blink::app::FolderCommand command)
    {
        using blink::app::FolderCommand; using blink::app::ShellMenuCommand;
        if (command == FolderCommand::Properties) { ShowSearchSystemContextMenu(std::move(selection), {}, true); return; }
        const auto mapped = command == FolderCommand::Open ? ShellMenuCommand::Open :
            command == FolderCommand::Copy ? ShellMenuCommand::Copy : command == FolderCommand::Cut ? ShellMenuCommand::Cut :
            command == FolderCommand::Paste ? ShellMenuCommand::PasteParent : command == FolderCommand::Rename ? ShellMenuCommand::Rename :
            command == FolderCommand::Delete ? ShellMenuCommand::Delete : command == FolderCommand::PermanentDelete ? ShellMenuCommand::PermanentDelete : ShellMenuCommand::None;
        if (mapped != ShellMenuCommand::None) { ExecuteSearchContextCommand(std::move(selection), mapped); }
    }

    void PublishSearchClipboard(const blink::app::SearchContextSelection& selection,
        const blink::app::SearchActionTarget& target, const blink::app::ShellMenuCommand command)
    {
        using blink::app::ShellMenuCommand;
        if (FileDropBusy()) { status_.Text(hstring(localization_.GetString(L"Clipboard.Unavailable"))); return; }
        const auto owner_generation = ActiveTab()->file_operation_generation;
        const bool text = command == ShellMenuCommand::CopyFullPath || command == ShellMenuCommand::CopyParentPath;
        const auto intent = command == ShellMenuCommand::Cut ? blink::app::ClipboardTransferIntent::Move : blink::app::ClipboardTransferIntent::Copy;
        blink::app::ClipboardCopySources expected;
        if (!text)
        {
            const auto error = blink::app::BuildSearchClipboardSources(target, expected);
            if (error) { ShowClipboardCopyError(error); return; }
        }
        HWND owner{}; check_hresult(window_.as<::IWindowNative>()->get_WindowHandle(&owner));
        clipboard_work_pending_ = true;
        clipboard_cancellation_ = std::make_shared<std::atomic_bool>(false);
        const auto cancellation = clipboard_cancellation_;
        RenderFileOperationStatus();
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        try
        {
            clipboard_work_ = Windows::System::Threading::ThreadPool::RunAsync(
                [weak, dispatcher, owner, selection, owner_generation, expected = std::move(expected), target,
                    text, command, intent, cancellation](auto&&) {
                    DWORD error = ERROR_GEN_FAILURE, sequence{};
                    blink::app::ClipboardCopySources validated;
                    std::vector<std::wstring> paths{ target.entry.full_path };
                    try
                    {
                        if (text) { error = blink::app::ValidateSearchResult(selection.result).error; }
                        else
                        {
                            error = blink::app::CaptureClipboardCopySources(paths, validated, cancellation);
                            if (!error && !blink::app::SameCapturedCopySources(expected, validated)) { error = ERROR_FILE_INVALID; }
                        }
                        if (!error && cancellation->load()) { error = ERROR_CANCELLED; }
                        if (!error)
                        {
                            error = text ? blink::app::PublishClipboardText(owner,
                                command == ShellMenuCommand::CopyParentPath ? target.parent : target.entry.full_path, sequence) :
                                blink::app::PublishClipboardFilesTransfer(owner, paths, intent, sequence);
                        }
                    }
                    catch (const std::bad_alloc&) { error = ERROR_NOT_ENOUGH_MEMORY; }
                    catch (...) { error = ERROR_GEN_FAILURE; }
                    (void)dispatcher.TryEnqueue([weak, selection, owner_generation, cancellation, captured = std::move(validated),
                        paths = std::move(paths), sequence, error, intent, text, command]() mutable {
                        if (auto app = weak.get(); app && !app->window_style_closed_)
                        {
                            app->clipboard_work_pending_ = false; app->RenderFileOperationStatus();
                            if (!error)
                            {
                                if (text) { app->clipboard_copy_cache_ = {}; }
                                else { app->clipboard_copy_cache_ = { std::move(captured), std::move(paths), sequence, intent }; }
                                if (cancellation->load() || !app->SearchContextCurrent(selection, owner_generation))
                                { app->ShowClipboardCopyError(ERROR_CANCELLED); return; }
                                app->status_.Text(hstring(text ? app->localization_.GetString(command == ShellMenuCommand::CopyParentPath ?
                                    L"Search.ParentCopied" : L"Search.PathCopied") : app->localization_.Format(intent == blink::app::ClipboardTransferIntent::Move ?
                                        L"Clipboard.CutItems" : L"Clipboard.CopiedItems", L"1")));
                            }
                            else { app->ShowClipboardCopyError(error); }
                        }
                    });
                });
        }
        catch (...) { clipboard_work_pending_ = false; RenderFileOperationStatus(); ShowClipboardCopyError(ERROR_GEN_FAILURE); }
    }

    struct PendingSearchFolderCommand
    {
        blink::app::TabId tab_id{};
        std::uint64_t token{}, owner_generation{};
        blink::app::FolderCommand command{};
        blink::app::RowIdentity row;
        std::wstring parent;
        blink::core::FileIdentity parent_identity;
        bool located{};
    };

    void NavigateSearchForCommand(const blink::app::TabId id, const blink::app::SearchActionTarget& target,
        const blink::app::FolderCommand command)
    {
        auto* tab = FindTab(id); if (!tab) { return; }
        const auto request = tab->state->Navigate(blink::app::Location::Folder(target.parent));
        if (!request) { tab->search_action_error = ERROR_INVALID_NAME; return; }
        const auto row = blink::app::MakeRowIdentity(target.entry);
        StartNavigation(id, *request, false, target.parent_identity, row);
        pending_search_folder_command_ = PendingSearchFolderCommand{ id, request->token, tab->file_operation_generation,
            command, row, target.parent, target.parent_identity };
    }

    void TryCompleteSearchFolderCommand()
    {
        if (!pending_search_folder_command_) { return; }
        const auto pending = *pending_search_folder_command_;
        const auto* tab = ActiveTab(); const auto* ui = ActiveTabUi();
        if (!tab || window_style_closed_ || tab->id != pending.tab_id || tab->search_visible ||
            tab->file_operation_generation != pending.owner_generation || tab->locate_navigation_token != pending.token)
        { pending_search_folder_command_.reset(); return; }
        if (!pending.located || !ui || ui->presentation_in_progress || tab->state->PresentationSnapshot().has_pending) { return; }
        pending_search_folder_command_.reset();
        const auto location = tab->state->PresentationSnapshot().committed_location;
        if (!location.IsFolder() || location.FolderPath() != pending.parent ||
            tab->state->CommittedDirectoryIdentity() != pending.parent_identity || !tab->state->IsRowSelected(pending.row))
        { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); return; }
        const auto found = std::find_if(ui->row_models.begin(), ui->row_models.end(), [&](const auto& row) { return row.row_identity == pending.row; });
        if (found == ui->row_models.end()) { status_.Text(hstring(localization_.GetString(L"Status.CommandStale"))); return; }
        ExecuteFolderCommand(pending.command, CaptureFolderMenuContext(static_cast<std::size_t>(found - ui->row_models.begin())));
    }

    void ExecuteSearchContextCommand(blink::app::SearchContextSelection selection, const blink::app::ShellMenuCommand command)
    {
        auto* tab = ActiveTab();
        if (!blink::app::IsSearchContextCurrent(selection, tab, window_style_closed_) || tab->search_action_pending) { return; }
        const auto owner_generation = tab->file_operation_generation;
        const auto action_generation = ++tab->search_action_generation;
        tab->search_action_pending = true; tab->search_action_error = ERROR_SUCCESS;
        const auto weak = get_weak(); const auto dispatcher = dispatcher_;
        const auto target = std::make_shared<blink::app::SearchActionTarget>(); target->error = ERROR_GEN_FAILURE;
        try
        {
            tab->search_action_work = Windows::System::Threading::ThreadPool::RunAsync(
                [selection, target](auto&&) {
                    try { *target = blink::app::ValidateSearchResult(selection.result); }
                    catch (...) { target->error = ERROR_GEN_FAILURE; }
                });
            tab->search_action_work.Completed([weak, dispatcher, target, selection, owner_generation, action_generation, command](auto&&, auto status) {
                (void)dispatcher.TryEnqueue([weak, target, selection, owner_generation, action_generation, command, status] {
                    if (auto app = weak.get(); app && !app->window_style_closed_)
                    {
                        auto* tab = app->FindTab(selection.tab_id); if (!tab || tab->search_action_generation != action_generation) { return; }
                        tab->search_action_pending = false;
                        if (!app->SearchContextCurrent(selection, owner_generation))
                        {
                            if (selection.tab_id == app->tab_window_state_.ActiveTabId()) { app->PresentSearchSurface(); }
                            return;
                        }
                        tab->search_action_error = status == Windows::Foundation::AsyncStatus::Completed ? target->error : ERROR_GEN_FAILURE;
                        if (!tab->search_action_error)
                        {
                            using blink::app::ShellMenuCommand; using blink::app::FolderCommand;
                            switch (command)
                            {
                            case ShellMenuCommand::Open: app->ApplySearchTarget(selection.tab_id, *target, false); break;
                            case ShellMenuCommand::Locate: app->ApplySearchTarget(selection.tab_id, *target, true); break;
                            case ShellMenuCommand::Copy: case ShellMenuCommand::Cut:
                            case ShellMenuCommand::CopyFullPath: case ShellMenuCommand::CopyParentPath:
                                app->PublishSearchClipboard(selection, *target, command); break;
                            case ShellMenuCommand::PasteParent: app->NavigateSearchForCommand(selection.tab_id, *target, FolderCommand::Paste); break;
                            case ShellMenuCommand::Rename: app->NavigateSearchForCommand(selection.tab_id, *target, FolderCommand::Rename); break;
                            case ShellMenuCommand::Delete: app->NavigateSearchForCommand(selection.tab_id, *target, FolderCommand::Delete); break;
                            case ShellMenuCommand::PermanentDelete: app->NavigateSearchForCommand(selection.tab_id, *target, FolderCommand::PermanentDelete); break;
                            default: break;
                            }
                        }
                        app->PresentSearchSurface();
                    }
                });
            });
        }
        catch (...) { tab->search_action_pending = false; tab->search_action_error = ERROR_GEN_FAILURE; }
        PresentSearchSurface();
    }

    void StartSearchResultAction(blink::app::SearchResult result, const bool locate)
    {
        const auto* tab = ActiveTab(); if (!tab || !tab->search_snapshot) { return; }
        ExecuteSearchContextCommand({ tab->id, tab->search_snapshot->generation, std::move(result) },
            locate ? blink::app::ShellMenuCommand::Locate : blink::app::ShellMenuCommand::Open);
    }

    void StartIndexServiceStatus()
    {
        index_service_state_ = std::make_shared<blink::app::IndexServiceState>();
        index_service_coordinator_ = std::make_unique<blink::app::IndexServiceCoordinator>(index_service_state_);
        const auto weak_app = get_weak();
        index_service_timer_ = dispatcher_.CreateTimer();
        index_service_timer_.Interval(std::chrono::seconds(15));
        index_service_timer_.IsRepeating(true);
        index_service_timer_.Tick([weak_app](auto&&, auto&&) {
            if (auto app = weak_app.get(); app && !app->window_style_closed_)
            { app->RefreshIndexServiceStatus(app->index_service_selected_root_); }
        });
        index_service_timer_.Start();
        RefreshIndexServiceStatus({});
    }

    blink::app::IndexServiceCoordinator::PublishCallback IndexServicePublication()
    {
        const auto weak_app = get_weak();
        const auto dispatcher = dispatcher_;
        return [weak_app, dispatcher](const std::uint64_t generation) {
            (void)dispatcher.TryEnqueue([weak_app, generation] {
                if (auto app = weak_app.get(); app && !app->window_style_closed_ &&
                    app->index_service_state_->IsCurrent(generation))
                {
                    const auto snapshot = app->index_service_state_->Snapshot();
                    app->index_service_selected_root_ = snapshot.selected_root;
                    app->index_service_presentation_.Update(snapshot, app->localization_);
                    app->TryStartPendingSearch();
                }
            });
        };
    }

    void RefreshIndexServiceStatus(std::wstring root)
    {
        if (window_style_closed_ || !index_service_coordinator_) { return; }
        index_service_selected_root_ = std::move(root);
        if (search_requests_.Busy() || search_requests_.HasPending()) { index_refresh_deferred_ = true; return; }
        (void)index_service_coordinator_->Refresh(index_service_selected_root_, IndexServicePublication());
        index_service_presentation_.Update(index_service_state_->Snapshot(), localization_);
    }

    void ManageIndexService(std::wstring root, const bool enable)
    {
        if (window_style_closed_ || !index_service_coordinator_) { return; }
        const auto command = localization_.GetString(enable ? L"Index.Enable" : L"Index.BuildSelected");
        if (search_requests_.Busy() || search_requests_.HasPending() ||
            index_service_state_->Snapshot().stage == blink::app::IndexConnectionStage::Refreshing)
        { status_.Text(hstring(localization_.Format(L"Status.CommandBusy", command))); return; }
        const auto snapshot = index_service_state_->Snapshot();
        if (root.empty()) { root = snapshot.selected_root; }
        if (enable ? snapshot.stage != blink::app::IndexConnectionStage::Unavailable :
            root != snapshot.selected_root || !blink::app::IndexServicePresentation::CanBuildSelectedIndex(snapshot))
        { index_service_presentation_.Update(snapshot, localization_); return; }
        index_service_selected_root_ = std::move(root);
        const auto generation = enable ? index_service_coordinator_->EnableService(index_service_selected_root_, IndexServicePublication()) :
            index_service_coordinator_->BuildIndex(index_service_selected_root_, IndexServicePublication());
        if (!generation) { status_.Text(hstring(localization_.Format(L"Status.CommandBusy", command))); }
        index_service_presentation_.Update(index_service_state_->Snapshot(), localization_);
    }

    void StopFolderWatcher(const blink::app::TabId id) noexcept
    {
        if (auto* tab = FindTab(id))
        {
            tab->folder_changes.Stop();
            (void)tab->folder_watcher->Stop();
            tab->folder_watch_error = ERROR_SUCCESS;
        }
        if (auto* tab_ui = FindTabUi(id); tab_ui && tab_ui->folder_change_timer)
        {
            try { tab_ui->folder_change_timer.Stop(); }
            catch (...) { /* The invalidated token still gates dispatcher shutdown. */ }
        }
    }

    void ReconcileFolderChanges(const blink::app::TabId id)
    {
        auto* tab = FindTab(id);
        auto* tab_ui = FindTabUi(id);
        if (window_style_closed_ || !tab || !tab_ui) { return; }
        const auto snapshot = tab->state->PresentationSnapshot();
        if (tab_ui->operation_refresh_identity)
        {
            if (!snapshot.committed_location.IsFolder() ||
                tab->state->CommittedDirectoryIdentity() != *tab_ui->operation_refresh_identity)
            { tab_ui->operation_refresh_identity.reset(); }
            else if (!snapshot.has_pending && !tab_ui->presentation_in_progress)
            {
                tab_ui->operation_refresh_identity.reset();
                if (const auto request = tab->state->Refresh()) { StartNavigation(id, *request, true); return; }
            }
        }
        const auto token = tab->folder_changes.Token();
        const bool busy = snapshot.has_pending || tab_ui->presentation_in_progress ||
            !snapshot.committed_location.IsFolder() ||
            snapshot.committed_location.FolderPath() != token.path;
        if (tab->folder_watch_error == ERROR_SUCCESS && tab->folder_changes.TakeRefresh(
                token, blink::app::FolderChangeState::Clock::now(), busy))
        {
            if (const auto request = tab->state->Refresh())
            {
                startup_.Stage("folder-watch.reconcile");
                StartNavigation(id, *request, true);
            }
        }
        if ((!tab->folder_changes.HasChanges() || tab->folder_watch_error != ERROR_SUCCESS) && !tab_ui->operation_refresh_identity)
        {
            tab_ui->folder_change_timer.Stop();
        }
    }

    void EnsureFolderWatcher(const blink::app::TabId id, const blink::app::Location& location,
        const bool reuse_subscription = false, const bool restoring_committed_page = false)
    {
        auto* tab = FindTab(id);
        auto* tab_ui = FindTabUi(id);
        if (!tab || !tab_ui || window_style_closed_) { return; }
        if (!location.IsFolder()) { StopFolderWatcher(id); return; }
        if (std::any_of(operation_watch_sources_.begin(), operation_watch_sources_.end(),
            [&](const auto& source) { return blink::app::IsFolderAtOrBelow(location, source); }))
        {
            if (std::find(operation_watch_tabs_.begin(), operation_watch_tabs_.end(), id) == operation_watch_tabs_.end())
            { operation_watch_tabs_.push_back(id); }
            StopFolderWatcher(id); return;
        }
        const auto& path = location.FolderPath();
        if (reuse_subscription && tab->folder_changes.Token().path == path &&
            tab->folder_watch_error == ERROR_SUCCESS)
        {
            return;
        }
        StopFolderWatcher(id);
        const auto token = tab->folder_changes.Begin(path);
        const auto weak_app = get_weak();
        if (!tab_ui->folder_change_timer)
        {
            tab_ui->folder_change_timer = dispatcher_.CreateTimer();
            tab_ui->folder_change_timer.Interval(std::chrono::milliseconds(100));
            tab_ui->folder_change_timer.IsRepeating(true);
            tab_ui->folder_change_timer.Tick([weak_app, id](auto&&, auto&&) {
                if (auto app = weak_app.get()) { app->ReconcileFolderChanges(id); }
            });
        }
        // Bound each subscription to one queued UI hint. A burst must not fill
        // the dispatcher with per-file work; preserve errors across coalescing.
        struct HintInbox
        {
            std::atomic_bool queued{};
            std::atomic<DWORD> error{ ERROR_SUCCESS };
        };
        const auto inbox = std::make_shared<HintInbox>();
        const auto dispatcher = dispatcher_;
        try
        {
            tab->folder_watcher->Start(path, [weak_app, dispatcher, inbox, id, token, restoring_committed_page](const auto event) {
                if (event.kind != blink::app::FolderWatchEventKind::Unavailable &&
                    !event.RequiresRefresh(restoring_committed_page)) { return; }
                if (event.kind == blink::app::FolderWatchEventKind::Unavailable)
                {
                    inbox->error.store(event.error);
                }
                if (inbox->queued.exchange(true)) { return; }
                try
                {
                    (void)dispatcher.TryEnqueue([weak_app, inbox, id, token] {
                        inbox->queued.store(false);
                        const auto error = inbox->error.exchange(ERROR_SUCCESS);
                        auto app = weak_app.get();
                        if (!app || app->window_style_closed_) { return; }
                        auto* tab = app->FindTab(id);
                        auto* tab_ui = app->FindTabUi(id);
                        if (!tab || !tab_ui || !tab->folder_changes.IsCurrent(token)) { return; }
                        if (error != ERROR_SUCCESS)
                        {
                            tab->folder_watch_error = error;
                            if (!tab_ui->operation_refresh_identity) { tab_ui->folder_change_timer.Stop(); }
                            app->startup_.Stage("folder-watch.unavailable");
                            if (id == app->tab_window_state_.ActiveTabId()) { app->UpdateBrowseStatus(); }
                            return;
                        }
                        if (tab->folder_changes.Notify(token, blink::app::FolderChangeState::Clock::now()))
                        {
                            if (!tab_ui->folder_change_timer.IsRunning())
                            {
                                tab_ui->folder_change_timer.Start();
                            }
                        }
                    });
                }
                catch (...) { /* Dispatcher shutdown is terminal; do not retry. */ }
            });
            startup_.Stage("folder-watch.started");
        }
        catch (...)
        {
            tab->folder_watch_error = ERROR_GEN_FAILURE;
            if (id == tab_window_state_.ActiveTabId()) { UpdateBrowseStatus(); }
        }
    }

    void CancelTreeRevealWork() noexcept
    {
        ++navigation_tree_reveal_generation_;
        if (!navigation_tree_reveal_work_)
        {
            return;
        }
        try
        {
            navigation_tree_reveal_work_.Cancel();
        }
        catch (...)
        {
            // The reveal generation gate remains authoritative when a
            // cancellation races with a completed enumeration.
        }
        navigation_tree_reveal_work_ = nullptr;
    }

    void CancelTreeExpansionWork() noexcept
    {
        for (auto& [node_id, work] : navigation_tree_expansion_work_)
        {
            (void)node_id;
            if (!work)
            {
                continue;
            }
            try
            {
                work.Cancel();
            }
            catch (...)
            {
                // The generation gate remains authoritative.
            }
        }
        navigation_tree_expansion_work_.clear();
        navigation_tree_pending_expansions_.clear();
    }

    void CancelNavigationTreeWork() noexcept
    {
        ++navigation_tree_lifetime_generation_;
        CancelTreeRevealWork();
        CancelTreeExpansionWork();
        navigation_tree_model_.Shutdown();
        navigation_tree_ui_nodes_.clear();
    }

    void StartNavigationTreeExpansion(
        const blink::app::NavigationTreeNodeId node_id)
    {
        const auto* node_before = navigation_tree_model_.Find(node_id);
        startup_.Stage(
            std::string("navigation-tree.expansion.begin.node=") +
            std::to_string(node_id) + ".state=" + (node_before ?
                std::to_string(static_cast<int>(node_before->load_state)) : "unknown") +
            ".generation=" + (node_before ?
                std::to_string(node_before->expansion_generation) : "unknown"));
        const auto token = navigation_tree_model_.BeginExpansion(node_id);
        if (!token.has_value())
        {
            const auto* rejected = navigation_tree_model_.Find(node_id);
            startup_.Stage(
                std::string("navigation-tree.expansion.rejected.node=") +
                std::to_string(node_id) + ".state=" + (rejected ?
                    std::to_string(static_cast<int>(rejected->load_state)) : "unknown"));
            return;
        }
        startup_.Stage(
            std::string("navigation-tree.expansion.accepted.node=") +
            std::to_string(node_id) + ".generation=" +
            std::to_string(token->generation));
        navigation_tree_pending_expansions_[node_id] =
            PendingNavigationTreeExpansion{ token->generation, 1, true };
        startup_.Stage(
            std::string("navigation-tree.expansion.intent-recorded.node=") +
            std::to_string(node_id) + ".generation=" +
            std::to_string(token->generation) + ".events=1");

        const auto lifetime_generation = navigation_tree_lifetime_generation_;
        const auto visibility_policy = tab_window_state_.GetVisibilityPolicy();
        const auto weak_app = get_weak();
        const auto dispatcher = dispatcher_;
        try
        {
            const auto work = winrt::Windows::System::Threading::ThreadPool::RunAsync(
                 [weak_app, dispatcher, lifetime_generation, visibility_policy, token = *token](
                     winrt::Windows::Foundation::IAsyncAction const& action) mutable {
                     if (action.Status() == winrt::Windows::Foundation::AsyncStatus::Canceled)
                     {
                         return;
                     }

                     const auto enumeration_start = BrowseClock::now();
                     if (auto app = weak_app.get())
                     {
                         app->startup_.Stage(
                             std::string("navigation-tree.enumeration.begin.node=") +
                             std::to_string(token.node_id) + ".thread=" +
                             std::to_string(GetCurrentThreadId()));
                     }
                     blink::core::DirectoryListing listing;
                    try
                    {
                        listing = blink::core::EnumerateDirectory(token.target.FolderPath());
                    }
                    catch (...)
                    {
                        listing.error = ERROR_GEN_FAILURE;
                    }
                    DWORD error = listing.error;
                    std::vector<blink::app::NavigationTreeChildSpec> children;
                    if (error == ERROR_SUCCESS)
                    {
                        try
                        {
                            children = blink::app::FilterNavigationTreeChildren(listing, visibility_policy);
                        }
                        catch (...)
                        {
                            error = ERROR_GEN_FAILURE;
                         }
                     }
                     const auto enumeration_ms = ElapsedMilliseconds(
                         enumeration_start,
                         BrowseClock::now());
                     if (auto app = weak_app.get())
                     {
                         app->startup_.Stage(
                             std::string("navigation-tree.enumeration.end.node=") +
                             std::to_string(token.node_id) + ".elapsed_ms=" +
                             std::to_string(enumeration_ms) + ".children=" +
                             std::to_string(children.size()) + ".thread=" +
                             std::to_string(GetCurrentThreadId()));
                     }
                     try
                    {
                        (void)dispatcher.TryEnqueue(
                            [weak_app,
                                lifetime_generation,
                                token,
                                error,
                                children = std::move(children)]() mutable {
                                 if (auto app = weak_app.get())
                                 {
                                    if (app->navigation_tree_lifetime_generation_ !=
                                            lifetime_generation ||
                                        !app->navigation_tree_model_.IsExpansionCurrent(token))
                                    {
                                        return;
                                     }
                                     const auto child_count = children.size();
                                     app->startup_.Stage(
                                         error == ERROR_SUCCESS ?
                                             std::string("navigation-tree.expansion.enumeration-completed.children=") +
                                                 std::to_string(child_count) :
                                             "navigation-tree.expansion.enumeration-failed");
                                     app->navigation_tree_expansion_work_.erase(token.node_id);
                                    bool completed = false;
                                    if (error != ERROR_SUCCESS)
                                    {
                                        (void)app->navigation_tree_model_.FailExpansion(
                                            token,
                                            error);
                                    }
                                    else
                                    {
                                        completed = app->navigation_tree_model_.CompleteExpansion(
                                            token,
                                            children);
                                    }
                                    app->UpdateNavigationTreeNodeVisual(token.node_id);
                                    if (error == ERROR_SUCCESS && completed)
                                    {
                                        app->MaterializeNavigationTreeChildren(token.node_id);
                                        app->startup_.Stage(
                                            std::string("navigation-tree.expansion.children-published.node=") +
                                            std::to_string(token.node_id) + ".count=" +
                                            std::to_string(child_count));
                                    }

                                    std::uint32_t expanding_event_count = 0;
                                    bool apply_pending_visual_expansion = false;
                                    if (const auto pending =
                                            app->navigation_tree_pending_expansions_.find(token.node_id);
                                        pending != app->navigation_tree_pending_expansions_.end())
                                    {
                                        expanding_event_count =
                                            pending->second.expanding_event_count;
                                        const auto* completed_node =
                                            app->navigation_tree_model_.Find(token.node_id);
                                        apply_pending_visual_expansion =
                                            error == ERROR_SUCCESS &&
                                            completed &&
                                            completed_node &&
                                            completed_node->load_state ==
                                                blink::app::NavigationTreeLoadState::Loaded &&
                                            pending->second.generation == token.generation &&
                                            pending->second.user_intent_still_expand;
                                        app->navigation_tree_pending_expansions_.erase(pending);
                                    }

                                    const auto* completed_node =
                                        app->navigation_tree_model_.Find(token.node_id);
                                    const auto ui_iterator =
                                        app->navigation_tree_ui_nodes_.find(token.node_id);
                                    app->startup_.Stage(
                                        std::string("navigation-tree.expansion.after-async.node=") +
                                        std::to_string(token.node_id) + ".completed=" +
                                        (completed ? "true" : "false") + ".state=" +
                                        (completed_node ?
                                            std::to_string(static_cast<int>(completed_node->load_state)) :
                                            "unknown") + ".has_unrealized=" +
                                        (ui_iterator != app->navigation_tree_ui_nodes_.end() &&
                                                ui_iterator->second.HasUnrealizedChildren() ?
                                            "true" : "false") + ".is_expanded=" +
                                        (ui_iterator != app->navigation_tree_ui_nodes_.end() &&
                                                ui_iterator->second.IsExpanded() ?
                                            "true" : "false") + ".events=" +
                                        std::to_string(expanding_event_count) +
                                        ".intent_expand=" +
                                        (apply_pending_visual_expansion ? "true" : "false"));
                                    if (apply_pending_visual_expansion &&
                                        ui_iterator != app->navigation_tree_ui_nodes_.end())
                                    {
                                        app->startup_.Stage(
                                            std::string("navigation-tree.expansion.visual-expand.requested.node=") +
                                            std::to_string(token.node_id));
                                        app->SetNavigationTreeExpanded(ui_iterator->second, true);
                                        app->startup_.Stage(
                                            std::string("navigation-tree.expansion.visual-expand.completed.node=") +
                                            std::to_string(token.node_id) + ".is_expanded=" +
                                            (ui_iterator->second.IsExpanded() ? "true" : "false"));
                                    }
                                    else if (error != ERROR_SUCCESS || !completed)
                                    {
                                        if (ui_iterator != app->navigation_tree_ui_nodes_.end())
                                        {
                                            app->SetNavigationTreeExpanded(ui_iterator->second, false);
                                        }
                                    }
                                }
                            });
                    }
                    catch (...)
                    {
                        // Dispatcher shutdown is terminal.
                    }
                });
            navigation_tree_expansion_work_[node_id] = work;
        }
        catch (...)
        {
            navigation_tree_pending_expansions_.erase(node_id);
            (void)navigation_tree_model_.FailExpansion(*token, ERROR_GEN_FAILURE);
            UpdateNavigationTreeNodeVisual(node_id);
            if (const auto iterator = navigation_tree_ui_nodes_.find(node_id);
                iterator != navigation_tree_ui_nodes_.end())
            {
                SetNavigationTreeExpanded(iterator->second, false);
            }
        }
    }

    void ApplyNavigationTreeReveal(
        const std::uint64_t reveal_generation,
        const blink::app::TabId tab_id,
        const blink::app::Location& target,
        const blink::app::NavigationTreeNodeId base_id,
        std::vector<NavigationTreeRevealStepResult> results)
    {
        if (reveal_generation != navigation_tree_reveal_generation_ ||
            tab_id != tab_window_state_.ActiveTabId())
        {
            return;
        }
        const auto* tab = FindTab(tab_id);
        if (!tab || !tab->state ||
            tab->state->PresentationSnapshot().committed_location != target)
        {
            return;
        }
        navigation_tree_reveal_work_ = nullptr;

        auto current_id = base_id;
        for (auto& result : results)
        {
            auto* parent = navigation_tree_model_.Find(current_id);
            if (!parent || parent->target != result.parent ||
                parent->load_state == blink::app::NavigationTreeLoadState::Loading)
            {
                ClearNavigationTreeSelection();
                return;
            }

            auto child_id = navigation_tree_model_.FindChild(current_id, result.desired);
            if (child_id == 0)
            {
                if (parent->load_state == blink::app::NavigationTreeLoadState::Loaded)
                {
                    // A concurrent expansion/refresh has authoritative
                    // knowledge that this path is not currently represented.
                    ClearNavigationTreeSelection();
                    return;
                }

                const auto token = navigation_tree_model_.BeginExpansion(current_id);
                if (!token.has_value())
                {
                    ClearNavigationTreeSelection();
                    return;
                }
                if (result.error != ERROR_SUCCESS)
                {
                    (void)navigation_tree_model_.FailExpansion(*token, result.error);
                    UpdateNavigationTreeNodeVisual(current_id);
                    ClearNavigationTreeSelection();
                    return;
                }
                if (!navigation_tree_model_.CompleteExpansion(*token, result.children))
                {
                    ClearNavigationTreeSelection();
                    return;
                }
                MaterializeNavigationTreeChildren(current_id);
                child_id = navigation_tree_model_.FindChild(current_id, result.desired);
            }

            if (child_id == 0)
            {
                ClearNavigationTreeSelection();
                return;
            }
            if (const auto iterator = navigation_tree_ui_nodes_.find(current_id);
                iterator != navigation_tree_ui_nodes_.end())
            {
                SetNavigationTreeExpanded(iterator->second, true);
            }
            current_id = child_id;
        }

        if (current_id == navigation_tree_model_.FindExact(target))
        {
            SelectNavigationTreeNode(current_id);
        }
        else
        {
            ClearNavigationTreeSelection();
        }
    }

    void StartNavigationTreeReveal(
        const blink::app::TabId tab_id,
        const blink::app::Location& target)
    {
        CancelTreeRevealWork();
        const auto exact_id = navigation_tree_model_.FindExact(target);
        if (exact_id != 0)
        {
            SelectNavigationTreeNode(exact_id);
            return;
        }

        if (target.IsThisPC())
        {
            const auto root_id = navigation_tree_model_.RootId();
            if (root_id != 0)
            {
                SelectNavigationTreeNode(root_id);
            }
            return;
        }

        const auto base_id = navigation_tree_model_.FindBestAncestor(target);
        const auto* base = navigation_tree_model_.Find(base_id);
        if (!base || !target.IsFolder() || !base->target.IsFolder() || base->reparse_point)
        {
            ClearNavigationTreeSelection();
            return;
        }

        std::vector<blink::app::Location> targets;
        auto current = target;
        while (current != base->target)
        {
            targets.push_back(current);
            const auto parent = current.Up();
            if (!parent.has_value() || parent->IsThisPC())
            {
                ClearNavigationTreeSelection();
                return;
            }
            current = *parent;
        }
        std::reverse(targets.begin(), targets.end());
        if (targets.empty())
        {
            SelectNavigationTreeNode(base_id);
            return;
        }

        const auto reveal_generation = ++navigation_tree_reveal_generation_;
        const auto lifetime_generation = navigation_tree_lifetime_generation_;
        const auto base_target = base->target;
        const auto visibility_policy = tab_window_state_.GetVisibilityPolicy();
        const auto weak_app = get_weak();
        const auto dispatcher = dispatcher_;
        try
        {
            navigation_tree_reveal_work_ =
                winrt::Windows::System::Threading::ThreadPool::RunAsync(
                    [weak_app,
                        dispatcher,
                        reveal_generation,
                        lifetime_generation,
                        tab_id,
                        target,
                        base_id,
                        base_target,
                        visibility_policy,
                        targets = std::move(targets)](
                        winrt::Windows::Foundation::IAsyncAction const& action) mutable {
                        if (action.Status() == winrt::Windows::Foundation::AsyncStatus::Canceled)
                        {
                            return;
                        }
                        std::vector<NavigationTreeRevealStepResult> results;
                        blink::app::Location parent = base_target;
                        for (std::size_t index = 0; index < targets.size(); ++index)
                        {
                            if (action.Status() == winrt::Windows::Foundation::AsyncStatus::Canceled)
                            {
                                return;
                            }
                            NavigationTreeRevealStepResult result;
                            result.parent = parent;
                            result.desired = targets[index];
                            try
                            {
                                const auto listing = blink::core::EnumerateDirectory(
                                    parent.FolderPath());
                                result.error = listing.error;
                                if (result.error == ERROR_SUCCESS)
                                {
                                    result.children =
                                        blink::app::FilterNavigationTreeChildren(listing, visibility_policy);
                                    const auto desired = std::find_if(
                                        result.children.begin(),
                                        result.children.end(),
                                        [&result](const auto& child) {
                                            return child.target == result.desired;
                                        });
                                    if (desired == result.children.end())
                                    {
                                        result.error = ERROR_PATH_NOT_FOUND;
                                    }
                                    else if (desired->reparse_point &&
                                             index + 1 < targets.size())
                                    {
                                        result.error = ERROR_NOT_SUPPORTED;
                                    }
                                }
                            }
                            catch (...)
                            {
                                result.error = ERROR_GEN_FAILURE;
                            }
                            results.push_back(std::move(result));
                            if (results.back().error != ERROR_SUCCESS)
                            {
                                break;
                            }
                            parent = targets[index];
                        }

                        try
                        {
                            (void)dispatcher.TryEnqueue(
                                [weak_app,
                                    reveal_generation,
                                    lifetime_generation,
                                    tab_id,
                                    target,
                                    base_id,
                                    results = std::move(results)]() mutable {
                                    if (auto app = weak_app.get())
                                    {
                                        if (app->navigation_tree_lifetime_generation_ !=
                                                lifetime_generation ||
                                            reveal_generation !=
                                                app->navigation_tree_reveal_generation_)
                                        {
                                            return;
                                        }
                                        app->ApplyNavigationTreeReveal(
                                            reveal_generation,
                                            tab_id,
                                            target,
                                            base_id,
                                            std::move(results));
                                    }
                                });
                        }
                        catch (...)
                        {
                            // Dispatcher shutdown is terminal.
                        }
                    });
        }
        catch (...)
        {
            ClearNavigationTreeSelection();
        }
    }

    void SyncNavigationPaneToCommittedLocation()
    {
        CancelTreeRevealWork();
        const auto* tab = ActiveTab();
        if (!tab || !tab->state)
        {
            ClearQuickNavigationSelection();
            ClearNavigationTreeSelection();
            return;
        }

        const auto target = tab->state->PresentationSnapshot().committed_location;
        if (target.IsThisPC())
        {
            ClearQuickNavigationSelection();
            const auto root_id = navigation_tree_model_.RootId();
            if (root_id != 0)
            {
                SelectNavigationTreeNode(root_id);
            }
            else
            {
                ClearNavigationTreeSelection();
            }
            return;
        }

        if (const auto quick_index = FindQuickNavigationItem(target);
            quick_index.has_value())
        {
            SelectQuickNavigationItem(*quick_index);
            ClearNavigationTreeSelection();
            return;
        }

        ClearQuickNavigationSelection();
        StartNavigationTreeReveal(
            tab_window_state_.ActiveTabId(),
            target);
    }

    void CancelPresentationWork(const blink::app::TabId id) noexcept
    {
        auto* tab_ui = FindTabUi(id);
        if (!tab_ui)
        {
            return;
        }
        if (tab_ui->presentation_work)
        {
            try
            {
                tab_ui->presentation_work.Cancel();
            }
            catch (...)
            {
                // The generation gate remains authoritative if cancellation
                // races with a completed projection task.
            }
            tab_ui->presentation_work = nullptr;
            tab_ui->presentation_expected_count = 0;
        }
    }

    void CancelThisPcWork(const blink::app::TabId id) noexcept
    {
        auto* tab_ui = FindTabUi(id);
        if (!tab_ui)
        {
            return;
        }
        ++tab_ui->this_pc_generation;
        if (tab_ui->this_pc_work)
        {
            try
            {
                tab_ui->this_pc_work.Cancel();
            }
            catch (...)
            {
                // The generation gate remains authoritative.
            }
            tab_ui->this_pc_work = nullptr;
        }
    }

    void StartNavigation(const blink::app::NavigationRequest& request)
    {
        StartNavigation(tab_window_state_.ActiveTabId(), request);
    }

    void StartNavigation(
        const blink::app::TabId tab_id,
        const blink::app::NavigationRequest& request,
        const bool automatic_refresh = false,
        std::optional<blink::core::FileIdentity> expected_root = {},
        std::optional<blink::app::RowIdentity> locate = {})
    {
        if (pending_search_folder_command_ && pending_search_folder_command_->tab_id == tab_id &&
            pending_search_folder_command_->token != request.token) { pending_search_folder_command_.reset(); }
        if (auto* tab = FindTab(tab_id))
        {
            tab->OnNavigation(automatic_refresh);
            if (!automatic_refresh)
            {
                if (const auto ui = FindTabUi(tab_id)) { ui->operation_refresh_identity.reset(); }
                if (tab_id == tab_window_state_.ActiveTabId()) { CloseOperationDialogs(); }
                search_requests_.RemoveTab(tab_id);
                tab->search_navigation_identity = expected_root;
                tab->locate_after_browse = std::move(locate);
                tab->locate_navigation_token = request.token;
                if (tab_id == tab_window_state_.ActiveTabId()) { PresentSearchSurface(); }
            }
        }
        // Explicit navigation/Refresh reopens the path even if it now points
        // to a replacement directory. Automatic reconcile retains the armed
        // subscription. Fresh enumeration awaits its initial arm below.
        EnsureFolderWatcher(tab_id, request.target, automatic_refresh);
        if (const auto tab = FindTab(tab_id); tab && tab->file_activation_controller)
        {
            tab->file_activation_controller->CancelPending();
        }
        if (tab_id == tab_window_state_.ActiveTabId())
        {
            // A newer committed-navigation request invalidates any in-flight
            // ancestor reveal, but does not discard shared expansion state.
            CancelTreeRevealWork();
        }
        if (request.target.IsThisPC())
        {
            StartThisPCNavigation(tab_id, request);
            return;
        }
        StartFolderNavigation(tab_id, request, automatic_refresh);
    }

    void StartThisPCNavigation(
        const blink::app::TabId tab_id,
        blink::app::NavigationRequest request)
    {
        auto* tab = FindTab(tab_id);
        auto* tab_ui = FindTabUi(tab_id);
        if (!tab || !tab_ui || !tab->state || !tab->browse_controller)
        {
            return;
        }
        CancelPresentationWork(tab_id);
        CancelThisPcWork(tab_id);
        tab->browse_controller->CancelPending();
        ++tab_ui->presentation_generation;
        tab_ui->presentation_next_index = 0;
        tab_ui->presentation_expected_count = 0;
        tab_ui->presentation_in_progress = false;
        if (tab_id == tab_window_state_.ActiveTabId())
        {
            selection_sync_suppressed_ = false;
        }

        if (tab_id == tab_window_state_.ActiveTabId())
        {
            path_box_.Text(hstring(localization_.GetString(L"Location.ThisPC")));
            UpdateNavigationButtons();
            UpdateBrowseStatus();
        }

        const auto generation = tab_ui->this_pc_generation;
        const auto dispatcher = dispatcher_;
        const auto weak_app = get_weak();
        tab_ui->this_pc_work = winrt::Windows::System::Threading::ThreadPool::RunAsync(
            [weak_app, dispatcher, tab_id, generation, request = std::move(request)](
                winrt::Windows::Foundation::IAsyncAction const&) mutable {
                try
                {
                    auto content = blink::app::EnumerateThisPcContent();
                    (void)dispatcher.TryEnqueue(
                        [weak_app,
                            tab_id,
                            generation,
                            request,
                            content = std::move(content)]() mutable {
                            if (auto app = weak_app.get())
                            {
                                auto* tab = app->FindTab(tab_id);
                                auto* tab_ui = app->FindTabUi(tab_id);
                                if (!tab || !tab_ui || generation != tab_ui->this_pc_generation ||
                                    !tab->state || !tab->state->IsPending(request))
                                {
                                    return;
                                }

                                const auto commit = tab->state->CommitThisPC(
                                    request,
                                    std::move(content));
                                if (commit.status == blink::app::CommitBrowseStatus::StaleOrRejected)
                                {
                                    return;
                                }
                                if (commit.status != blink::app::CommitBrowseStatus::Committed)
                                {
                                    const DWORD error = commit.error == ERROR_SUCCESS ?
                                        ERROR_GEN_FAILURE : commit.error;
                                    if (tab->state->RejectBrowse(request, error))
                                    {
                                        if (tab_id == app->tab_window_state_.ActiveTabId())
                                        {
                                            app->RestoreNavigationUi();
                                            app->UpdateBrowseStatus();
                                        }
                                    }
                                    return;
                                }

                                tab_ui->this_pc_work = nullptr;
                                // RenderThisPC owns the single active-tab
                                // presentation. Inactive completion remains
                                // tab-local and is presented on activation.
                                app->RenderThisPC(tab_id);
                            }
                        });
                }
                catch (...)
                {
                    try
                    {
                        (void)dispatcher.TryEnqueue(
                            [weak_app, tab_id, generation, request]() mutable {
                                if (auto app = weak_app.get())
                                {
                                    auto* tab = app->FindTab(tab_id);
                                    auto* tab_ui = app->FindTabUi(tab_id);
                                    if (!tab || !tab_ui || generation != tab_ui->this_pc_generation ||
                                        !tab->state || !tab->state->IsPending(request))
                                    {
                                        return;
                                    }
                                    if (tab->state->RejectBrowse(request, ERROR_GEN_FAILURE))
                                    {
                                        if (tab_id == app->tab_window_state_.ActiveTabId())
                                        {
                                            app->RestoreNavigationUi();
                                            app->UpdateBrowseStatus();
                                        }
                                    }
                                }
                            });
                    }
                    catch (...)
                    {
                        // Dispatcher shutdown is terminal.
                    }
                }
            });
    }

    void StartFolderNavigation(
        const blink::app::TabId tab_id,
        blink::app::NavigationRequest request,
        const bool automatic_refresh)
    {
        auto* tab = FindTab(tab_id);
        auto* tab_ui = FindTabUi(tab_id);
        if (!tab || !tab_ui || !tab->state || !tab->browse_state || !tab->browse_controller)
        {
            return;
        }
        CancelPresentationWork(tab_id);
        CancelThisPcWork(tab_id);
        tab->browse_controller->CancelPending();
        ++tab_ui->presentation_generation;
        tab_ui->presentation_next_index = 0;
        tab_ui->presentation_expected_count = 0;
        tab_ui->presentation_in_progress = false;
        if (tab_id == tab_window_state_.ActiveTabId())
        {
            selection_sync_suppressed_ = false;
        }
        const std::wstring requested_path = request.target.FolderPath();
        const auto timing = std::make_shared<BrowseTimingRecord>();
        timing->path = requested_path;
        const auto navigation_start = BrowseClock::now();
        timing->navigation_accepted_at = navigation_start;
        if (tab_id == tab_window_state_.ActiveTabId())
        {
            if (!automatic_refresh && request.action != blink::app::NavigationAction::Relocate && request.action != blink::app::NavigationAction::DeleteLocation)
            { path_box_.Text(hstring(requested_path)); }
            UpdateNavigationButtons();
            UpdateBrowseStatus();
        }
        timing->navigation_submission_ms =
            ElapsedMilliseconds(navigation_start, BrowseClock::now());
        const auto dispatcher = dispatcher_;
        const auto state = tab->browse_state;
        const auto weak_app = get_weak();
        const auto expected_root = tab->locate_navigation_token == request.token ? tab->search_navigation_identity :
            std::optional<blink::core::FileIdentity>{};
        tab->browse_controller->Browse(
            requested_path,
            [dispatcher, state, weak_app, tab_id, timing, automatic_refresh, expected_root, request = std::move(request)](
                blink::app::BrowseResult result) mutable {
                // Second worker-side check before queueing a UI callback.
                if (!state->IsCurrent(result.generation, result.requested_path))
                {
                    return;
                }
                if (result.listing.error == ERROR_SUCCESS && expected_root)
                {
                    const auto actual = blink::app::ResolveSearchFolderScope(request.target.FolderPath());
                    if (actual.error != ERROR_SUCCESS || actual.identity != *expected_root)
                    { result.listing.error = actual.error == ERROR_SUCCESS ? ERROR_FILE_INVALID : actual.error; }
                }

                bool queued = false;
                try
                {
                    queued = dispatcher.TryEnqueue(
                        [state, weak_app, tab_id, timing, automatic_refresh, request, result = std::move(result)]() mutable {
                            const auto ui_begin = BrowseClock::now();
                            timing->ui_begin = ui_begin;
                            timing->generation = result.generation;
                            timing->entry_count = result.listing.entries.size();
                            timing->background_enumeration_ms =
                                result.background_enumeration_ms;
                            timing->background_total_ms = result.background_total_ms;
                            if (result.background_started_at != BrowseClock::time_point{})
                            {
                                timing->t1_background_start_ms = ElapsedMilliseconds(
                                    timing->navigation_accepted_at,
                                    result.background_started_at);
                            }
                            if (result.background_completed_at != BrowseClock::time_point{})
                            {
                                timing->t2_enumeration_complete_ms = ElapsedMilliseconds(
                                    timing->navigation_accepted_at,
                                    result.background_completed_at);
                                timing->result_publication_ms = ElapsedMilliseconds(
                                    result.background_completed_at,
                                    ui_begin);
                            }

                            auto app = weak_app.get();
                            if (!app)
                            {
                                return;
                            }
                            app->startup_.Stage("browse.callback.begin");

                            // NavigationModel is the committed-page gate. A
                            // newer navigation or an invalid address input
                            // invalidates this request even if BrowseState is
                            // still current for the old worker result.
                            auto* tab = app->FindTab(tab_id);
                            auto* tab_ui = app->FindTabUi(tab_id);
                            if (!tab || !tab_ui || !tab->state || !tab->state->IsPending(request))
                            {
                                return;
                            }

                            // BrowseState is the execution-generation gate;
                            // TabState is the committed-page gate. Both must
                            // still describe this same request on the UI turn.
                            if (!state->IsCurrent(result.generation, result.requested_path))
                            {
                                return;
                            }

                            if (result.listing.error != ERROR_SUCCESS)
                            {
                                const DWORD error = result.listing.error;
                                if (!state->Complete(
                                        result.generation,
                                        result.requested_path,
                                        error))
                                {
                                    return;
                                }
                                if (app->pending_search_folder_command_ && app->pending_search_folder_command_->tab_id == tab_id &&
                                    app->pending_search_folder_command_->token == request.token) { app->pending_search_folder_command_.reset(); }
                                if (!tab->state->RejectBrowse(request, error))
                                {
                                    return;
                                }
                                if (request.action != blink::app::NavigationAction::Refresh)
                                {
                                    app->EnsureFolderWatcher(tab_id,
                                        tab->state->PresentationSnapshot().committed_location, false, true);
                                }
                                if (tab_id == app->tab_window_state_.ActiveTabId())
                                {
                                    app->RestoreNavigationUi(automatic_refresh);
                                    app->UpdateBrowseStatus();
                                }
                                return;
                            }

                            app->startup_.Stage("commit.begin");
                            const auto commit_start = BrowseClock::now();
                            const auto commit = tab->state->CommitBrowse(
                                request,
                                std::move(result.listing));
                            timing->commit_browse_ms =
                                ElapsedMilliseconds(commit_start, BrowseClock::now());
                            app->startup_.Stage("commit.end");
                            if (commit.status == blink::app::CommitBrowseStatus::StaleOrRejected)
                            {
                                return;
                            }
                            if (commit.status != blink::app::CommitBrowseStatus::Committed)
                            {
                                const DWORD error = commit.error == ERROR_SUCCESS ?
                                    ERROR_GEN_FAILURE : commit.error;
                                if (app->pending_search_folder_command_ && app->pending_search_folder_command_->tab_id == tab_id &&
                                    app->pending_search_folder_command_->token == request.token) { app->pending_search_folder_command_.reset(); }
                                if (!tab->state->RejectBrowse(request, error))
                                {
                                    return;
                                }
                                (void)state->Complete(
                                    result.generation,
                                    result.requested_path,
                                    error);
                                if (request.action != blink::app::NavigationAction::Refresh)
                                {
                                    app->EnsureFolderWatcher(tab_id,
                                        tab->state->PresentationSnapshot().committed_location, false, true);
                                }
                                if (tab_id == app->tab_window_state_.ActiveTabId())
                                {
                                    app->RestoreNavigationUi(automatic_refresh);
                                    app->UpdateBrowseStatus();
                                }
                                return;
                            }

                            // Request completion is separate from the committed
                            // page. Both are finalized on this UI-thread turn.
                            if (!state->Complete(
                                    result.generation,
                                    result.requested_path,
                                    ERROR_SUCCESS))
                            {
                                return;
                            }

                            app->startup_.Stage("browse.finalize.restore.begin");
                            if (tab_id != app->tab_window_state_.ActiveTabId()) { app->UpdateTabLabel(tab_id); }
                            if (tab->locate_after_browse && tab->locate_navigation_token == request.token)
                            {
                                const auto located = tab->FinishLocate(request.token);
                                if (app->pending_search_folder_command_ && app->pending_search_folder_command_->tab_id == tab_id &&
                                    app->pending_search_folder_command_->token == request.token)
                                {
                                    if (located) { app->pending_search_folder_command_->located = true; }
                                    else { app->pending_search_folder_command_.reset(); }
                                }
                            }
                            const auto restore_start = BrowseClock::now();
                            if (tab_id == app->tab_window_state_.ActiveTabId())
                            {
                                app->RestoreNavigationUi(automatic_refresh);
                            }
                            timing->restore_navigation_ui_ms =
                                ElapsedMilliseconds(restore_start, BrowseClock::now());
                            app->startup_.Stage("browse.finalize.restore.end");
                            app->startup_.Stage("browse.finalize.render.begin");
                            const bool presentation_deferred =
                                app->RenderCommittedFolderWithTiming(tab_id, timing);
                            app->startup_.Stage("browse.finalize.render.end");
                            const auto initial_ui_end = BrowseClock::now();
                            timing->ui_work_total_ms +=
                                ElapsedMilliseconds(ui_begin, initial_ui_end);
                            timing->longest_ui_block_ms = std::max(
                                timing->longest_ui_block_ms,
                                ElapsedMilliseconds(ui_begin, initial_ui_end));
                            if (!presentation_deferred &&
                                tab_id == app->tab_window_state_.ActiveTabId())
                            {
                                app->FinishBrowsePresentation(timing);
                            }
                        });
                }
                catch (...)
                {
                    // Dispatcher shutdown is a terminal path. Do not retry
                    // or assume that the UI received the result.
                    queued = false;
                }

                // Queue failure is a terminal path, especially during
                // shutdown. There is intentionally no retry loop.
                if (!queued)
                {
                    return;
                }
            }, tab->folder_watcher->WhenReady());
        if (tab_id == tab_window_state_.ActiveTabId())
        {
            UpdateBrowseStatus();
        }
    }

    [[nodiscard]] std::wstring TabTitle(const blink::app::TabId id) const
    {
        const auto* tab = tab_window_state_.Find(id);
        if (!tab || !tab->state)
        {
            return localization_.GetString(L"Location.ThisPC");
        }
        const auto location = tab->state->PresentationSnapshot().committed_location;
        if (location.IsThisPC())
        {
            return localization_.GetString(L"Location.ThisPC");
        }
        const auto& path = location.FolderPath();
        if (path.size() >= 3 && path[1] == L':' &&
            (path[2] == L'\\' || path[2] == L'/'))
        {
            return path.substr(0, 2);
        }
        const auto separator = path.find_last_of(L"\\/");
        return separator == std::wstring::npos ? path :
            (separator + 1 < path.size() ? path.substr(separator + 1) : path);
    }

    [[nodiscard]] std::optional<blink::app::TabId> TabIdFromItem(
        const IInspectable& value) const
    {
        const auto item = value.try_as<TabViewItem>();
        if (!item || !item.Tag())
        {
            return std::nullopt;
        }
        try
        {
            return winrt::unbox_value<blink::app::TabId>(item.Tag());
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    void SyncTabViewItems()
    {
        if (!tab_view_)
        {
            return;
        }
        tab_view_sync_suppressed_ = true;
        tab_item_ids_.clear();
        tab_view_.TabItems().Clear();
        const auto ids = tab_window_state_.TabIds();
        for (const auto id : ids)
        {
            auto item = TabViewItem();
            item.Tag(winrt::box_value(id));
            item.Header(winrt::box_value(hstring(TabTitle(id))));
            item.IsClosable(true);
            item.AllowDrop(true);
            const auto weak = get_weak();
            item.DragOver([weak, id](auto&&, const DragEventArgs& args) {
                if (const auto app = weak.get(); app && !app->window_style_closed_) { app->FileDragTabOver(id, args); }
            });
            item.DragLeave([weak, id](auto&& sender, const DragEventArgs& args) {
                if (const auto app = weak.get(); app && app->drag_hover_tab_ == id)
                {
                    const auto element = sender.as<FrameworkElement>(); const auto point = args.GetPosition(element);
                    if (point.X < 0 || point.Y < 0 || point.X > element.ActualWidth() || point.Y > element.ActualHeight()) { app->CancelDragTabHover(); }
                }
            });
            item.Drop([weak, id](auto&&, const DragEventArgs& args) {
                if (const auto app = weak.get(); app && !app->window_style_closed_)
                {
                    app->CancelDragTabHover(); app->ActivateDropTab(id);
                    app->file_drag_drop_.Drop(args, [weak, id]() -> std::optional<FileOperationOrigin> {
                        if (const auto live = weak.get()) { return live->TabDropTarget(id); } return {};
                    });
                }
            });
            tab_item_ids_.push_back(id);
            tab_view_.TabItems().Append(item);
        }
        const auto active = tab_window_state_.ActiveTabId();
        const auto active_index = std::find(
            tab_item_ids_.begin(), tab_item_ids_.end(), active);
        if (active_index != tab_item_ids_.end())
        {
            tab_view_.SelectedIndex(static_cast<int32_t>(
                std::distance(tab_item_ids_.begin(), active_index)));
        }
        tab_view_sync_suppressed_ = false;
    }

    void OnTabViewSelectionChanged()
    {
        if (tab_view_sync_suppressed_ || !tab_view_)
        {
            return;
        }
        const auto id = TabIdFromItem(tab_view_.SelectedItem());
        if (!id.has_value() || !tab_window_state_.ActivateTab(*id))
        {
            return;
        }
        CloseOperationDialogs();
        PresentActiveTab();
    }

    void CreateAndActivateTab()
    {
        CloseOperationDialogs();
        const auto id = tab_window_state_.CreateTab();
        EnsureTabUi(id);
        tab_window_state_.ActivateTab(id);
        SyncTabViewItems();
        PresentActiveTab();
        RefreshNavigation();
    }

    void CloseTab(const blink::app::TabId id)
    {
        if (id == tab_window_state_.ActiveTabId()) { CloseOperationDialogs(); }
        if (tab_window_state_.Size() == 1)
        {
            if (window_)
            {
                window_.Close();
            }
            return;
        }

        CancelPresentationWork(id);
        CancelThisPcWork(id);
        StopFolderWatcher(id);
        search_requests_.RemoveTab(id);
        const auto status = tab_window_state_.CloseTab(id);
        if (status != blink::app::CloseTabStatus::Closed)
        {
            return;
        }
        tab_ui_states_.erase(id);
        SyncTabViewItems();
        PresentActiveTab();
    }

    void OnTabCloseRequested(const TabViewTabCloseRequestedEventArgs& args)
    {
        const auto id = TabIdFromItem(args.Item());
        if (id.has_value())
        {
            CloseTab(*id);
        }
    }

    void BindFolderViewToActiveTab()
    {
        // Any tab/source presentation invalidates a delayed focus restore,
        // including A -> B -> A switches that reuse the same view control.
        ++folder_view_generation_;
        CancelFolderSystemMenu();
        const auto* tab = ActiveTab();
        auto* tab_ui = ActiveTabUi();
        if (!tab || !tab_ui || !tab->state)
        {
            return;
        }
        if (entries_)
        {
            if (!tab_ui->row_items_source)
            {
                tab_ui->row_items_source =
                    winrt::single_threaded_observable_vector<IInspectable>();
            }
            selection_sync_suppressed_ = true;
            entries_.ItemsSource(tab_ui->row_items_source);
            entries_.SelectedItems().Clear();
            if (tab_ui->showing_committed_folder)
            {
                for (std::size_t index = 0; index < tab_ui->row_models.size(); ++index)
                {
                    if (index >= tab_ui->row_items_source.Size())
                    {
                        break;
                    }
                    const auto& row = tab_ui->row_models[index];
                    if (row.row_identity.file_identity.valid &&
                        tab->state->IsRowSelected(row.row_identity))
                    {
                        entries_.SelectedItems().Append(
                            tab_ui->row_items_source.GetAt(static_cast<std::uint32_t>(index)));
                    }
                }
            }
            selection_sync_suppressed_ = tab_ui->presentation_in_progress;
        }
    }

    void PresentActiveTab()
    {
        const auto* tab = ActiveTab();
        if (!tab || !ActiveTabUi() || !tab->state)
        {
            return;
        }
        ApplyFolderViewMode(tab->state->GetViewMode());
        SyncFolderViewCommands();
        if (tab->state->PresentationSnapshot().committed_location.IsThisPC())
        {
            ApplyNavigationTreeContent(tab->state->CommittedThisPCSnapshot());
        }
        BindFolderViewToActiveTab();
        RestoreNavigationUi();
        UpdateBrowseStatus();
        PresentSearchSurface();
    }

    void RestoreNavigationUi(const bool preserve_address_draft = false)
    {
        startup_.Stage("restore.path.begin");
        const auto snapshot = ActiveState().PresentationSnapshot();
        if (!preserve_address_draft)
        {
            path_box_.Text(hstring(snapshot.committed_location.IsThisPC() ?
                localization_.GetString(L"Location.ThisPC") :
                snapshot.committed_location.FolderPath()));
        }
        startup_.Stage("restore.path.end");
        startup_.Stage("restore.navigation.begin");
        UpdateNavigationButtons();
        startup_.Stage("restore.navigation.end");
        startup_.Stage("restore.tab.begin");
        UpdateTabLabel();
        startup_.Stage("restore.tab.end");
        SyncNavigationPaneToCommittedLocation();
    }

    void UpdateNavigationButtons()
    {
        if (folder_system_menu_context_ && !IsFolderMenuContextCurrent(*folder_system_menu_context_))
        { CancelFolderSystemMenu(); }
        startup_.Stage("navigation-buttons.begin");
        const auto snapshot = ActiveState().PresentationSnapshot();
        const auto* tab_ui = ActiveTabUi();
        const bool enabled = !snapshot.has_pending &&
            !(tab_ui && tab_ui->presentation_in_progress);
        const auto* active = ActiveTab();
        back_button_.IsEnabled((snapshot.can_go_back && enabled) || (active && active->search_visible));
        startup_.Stage("navigation-buttons.back");
        forward_button_.IsEnabled(snapshot.can_go_forward && enabled && !(active && active->search_visible));
        startup_.Stage("navigation-buttons.forward");
        up_button_.IsEnabled(snapshot.committed_location.IsFolder() && (enabled || (active && active->search_visible)));
        startup_.Stage("navigation-buttons.up");
        refresh_button_.IsEnabled(enabled || (active && active->search_visible));
        search_box_.IsEnabled(!snapshot.has_pending || (active && active->search_visible));
        const bool can_sort = enabled && snapshot.committed_location.IsFolder();
        sort_field_picker_.IsEnabled(can_sort);
        sort_direction_button_.IsEnabled(can_sort);
        for (const auto& child : details_header_.Children())
        {
            if (const auto control = child.try_as<Control>())
            {
                control.IsEnabled(can_sort);
            }
        }
        startup_.Stage("navigation-buttons.refresh");
        startup_.Stage("navigation-buttons.end");
    }

    void OnWindowClosed() noexcept
    {
        window_style_closed_ = true;
        DismissSearchContextMenu();
        shell_context_menu_.Shutdown(); search_menu_selection_.reset(); pending_search_folder_command_.reset();
        CloseOperationDialogs();
        file_operations_.Shutdown();
        CancelDragTabHover(); file_drag_drop_.Close();
        if (clipboard_cancellation_) { clipboard_cancellation_->store(true); }
        search_requests_.Close();
        search_presentation_.Close();
        if (index_service_coordinator_) { (void)index_service_coordinator_->Shutdown(); }
        if (index_service_timer_) { try { index_service_timer_.Stop(); } catch (...) {} }
        index_service_presentation_.Close();
        if (folder_menu_)
        {
            try { folder_menu_.Hide(); } catch (...) {}
            folder_menu_ = nullptr;
        }
        RemoveNavigationWheelInput();
        ++folder_view_generation_;
        CancelNavigationTreeWork();
        for (const auto id : tab_window_state_.TabIds())
        {
            StopFolderWatcher(id);
            CancelPresentationWork(id);
            CancelThisPcWork(id);
        }
        selection_sync_suppressed_ = false;
        tab_window_state_.Shutdown();
    }

    void RenderCommittedFolder()
    {
        (void)RenderCommittedFolderWithTiming(
            tab_window_state_.ActiveTabId(),
            nullptr);
    }

    void RenderThisPC(const blink::app::TabId tab_id)
    {
        auto* tab = FindTab(tab_id);
        auto* tab_ui = FindTabUi(tab_id);
        if (!tab || !tab_ui || !tab->state)
        {
            return;
        }
        CancelPresentationWork(tab_id);
        ++tab_ui->presentation_generation;
        tab_ui->presentation_next_index = 0;
        tab_ui->presentation_expected_count = 0;
        tab_ui->presentation_in_progress = false;
        tab_ui->showing_this_pc = true;
        tab_ui->showing_committed_folder = false;
        tab_ui->this_pc_rows = tab->state->CommittedThisPCSnapshot().items;
        if (!tab_ui->row_items_source)
        {
            tab_ui->row_items_source =
                winrt::single_threaded_observable_vector<IInspectable>();
        }
        tab_ui->row_models.clear();
        tab_ui->row_items.clear();
        tab_ui->row_items_source.Clear();

        for (const auto& row : tab_ui->this_pc_rows)
        {
            const std::wstring label = row.kind == blink::app::ThisPcItemKind::Place ?
                localization_.GetString(row.label_key) : row.display_name;
            const std::wstring type = row.kind == blink::app::ThisPcItemKind::Place ?
                localization_.GetString(L"ThisPC.Places") :
                localization_.GetString(L"ThisPC.Drive");
            auto item = winrt::Windows::Foundation::Collections::PropertySet();
            item.Insert(L"Name", winrt::box_value(hstring(label)));
            item.Insert(L"Type", winrt::box_value(hstring(type)));
            item.Insert(L"Size", winrt::box_value(hstring(
                localization_.GetString(L"Common.NotAvailable"))));
            item.Insert(L"Modified", winrt::box_value(hstring(
                localization_.GetString(L"Common.NotAvailable"))));
            item.Insert(L"Icon", winrt::box_value(hstring(
                row.kind == blink::app::ThisPcItemKind::Place ? L"\xE8B7" : L"\xE7F8")));
            tab_ui->row_items.push_back(item);
            tab_ui->row_items_source.Append(item);
        }

        if (tab_id == tab_window_state_.ActiveTabId())
        {
            PresentActiveTab();
        }
    }

    bool RenderCommittedFolderWithTiming(
        const blink::app::TabId tab_id,
        const std::shared_ptr<BrowseTimingRecord>& timing = nullptr)
    {
        auto* tab = FindTab(tab_id);
        auto* tab_ui = FindTabUi(tab_id);
        if (!tab || !tab_ui || !tab->state)
        {
            return false;
        }
        tab_ui->showing_this_pc = false;
        tab_ui->showing_committed_folder = true;
        if (!timing)
        {
            CancelPresentationWork(tab_id);
            ++tab_ui->presentation_generation;
            tab_ui->presentation_next_index = 0;
            tab_ui->presentation_in_progress = false;
            if (tab_id == tab_window_state_.ActiveTabId())
            {
                selection_sync_suppressed_ = false;
            }
        }
        startup_.Stage("render.begin");
        if (ui_probe_mode_ == UiProbeMode::NoFolderList)
        {
            tab_ui->showing_committed_folder = false;
            startup_.Stage("folder-render-skipped");
            return false;
        }
        if (ui_probe_mode_ != UiProbeMode::None)
        {
            RenderUiProbeItems(tab_id);
            startup_.Stage("render.probe.end");
            return false;
        }

        const auto snapshot_start = BrowseClock::now();
        auto entries = tab->state->VisibleEntriesSnapshot();
        if (timing)
        {
            timing->committed_entries_snapshot_ms =
                ElapsedMilliseconds(snapshot_start, BrowseClock::now());
        }
        const auto sort_descriptor = tab->state->GetSortDescriptor();
        if (entries.size() > kPresentationBatchSize)
        {
            BeginBatchedRowPresentation(
                tab_id,
                std::move(entries),
                sort_descriptor,
                timing);
            startup_.Stage("render.end");
            return true;
        }

        const auto sorting_start = BrowseClock::now();
        if (timing)
        {
            timing->t3_sort_start_ms = ElapsedMilliseconds(
                timing->navigation_accepted_at,
                sorting_start);
        }
        auto sorted_entries = blink::app::TabState::SortEntries(
            std::move(entries),
            sort_descriptor);
        const auto sorting_end = BrowseClock::now();
        if (timing)
        {
            timing->sorting_ms = ElapsedMilliseconds(sorting_start, sorting_end);
            timing->t4_sort_complete_ms = ElapsedMilliseconds(
                timing->navigation_accepted_at,
                sorting_end);
        }
        RenderEntries(tab_id, sorted_entries, timing.get());
        startup_.Stage("render.end");
        return false;
    }

    void FinishBrowsePresentation(const std::shared_ptr<BrowseTimingRecord>& timing)
    {
        startup_.Stage("browse.finalize.navigation.begin");
        const auto finalize_start = BrowseClock::now();
        const auto navigation_buttons_start = BrowseClock::now();
        UpdateNavigationButtons();
        timing->navigation_buttons_ms =
            ElapsedMilliseconds(navigation_buttons_start, BrowseClock::now());
        startup_.Stage("browse.finalize.navigation.end");
        startup_.Stage("browse.finalize.status.begin");
        const auto browse_status_start = BrowseClock::now();
        UpdateBrowseStatus();
        timing->browse_status_ms =
            ElapsedMilliseconds(browse_status_start, BrowseClock::now());
        startup_.Stage("browse.finalize.status.end");
        const auto finalize_end = BrowseClock::now();
        const double finalize_work_ms =
            ElapsedMilliseconds(finalize_start, finalize_end);
        timing->ui_work_total_ms += finalize_work_ms;
        timing->longest_ui_block_ms = std::max(
            timing->longest_ui_block_ms,
            finalize_work_ms);
        timing->presentation_total_ms =
            ElapsedMilliseconds(timing->ui_begin, finalize_end);
        timing->t9_full_presentation_complete_ms =
            ElapsedMilliseconds(timing->navigation_accepted_at, finalize_end);
        timing->navigation_to_full_presentation_ms =
            timing->t9_full_presentation_complete_ms;
        FinalizeBrowseTiming(*timing);
        startup_.Browse(*timing);
        TryCompleteSearchFolderCommand();
    }

    void FinishPresentationWithoutTiming()
    {
        UpdateNavigationButtons();
        UpdateBrowseStatus();
        TryCompleteSearchFolderCommand();
    }

    void ConfigureFolderItemTemplate()
    {
        if (ui_probe_mode_ == UiProbeMode::None)
        {
            folder_view_presentation_.Configure(entries_, ActiveState().GetViewMode());
            return;
        }
        constexpr const wchar_t* kFolderItemsPanel = LR"XAML(
<ItemsPanelTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <ItemsStackPanel />
</ItemsPanelTemplate>)XAML";
        const bool use_explicit_items_panel =
            ui_probe_mode_ == UiProbeMode::None ||
            ui_probe_mode_ == UiProbeMode::ItemsSourceStringExplicitPanel ||
            ui_probe_mode_ == UiProbeMode::CurrentMinimalPropertySet;
        if (use_explicit_items_panel)
        {
            startup_.Stage("ConfigureFolderItemTemplate.items-panel.begin");
            try
            {
                auto loaded = Microsoft::UI::Xaml::Markup::XamlReader::Load(
                    hstring(kFolderItemsPanel));
                auto panel = loaded.as<ItemsPanelTemplate>();
                entries_.ItemsPanel(panel);
                startup_.Stage("ConfigureFolderItemTemplate.items-panel.ok");
            }
            catch (const hresult_error& error)
            {
                startup_.HresultFailure("ConfigureFolderItemTemplate.items-panel", error);
                throw;
            }
            catch (...)
            {
                startup_.GenericFailure("ConfigureFolderItemTemplate.items-panel");
                throw;
            }
        }
        else
        {
            startup_.Stage("ui-probe.default-items-panel");
        }

        if (ui_probe_mode_ == UiProbeMode::EmptyDefaultList ||
            ui_probe_mode_ == UiProbeMode::DirectItemsString ||
            ui_probe_mode_ == UiProbeMode::ItemsSourceString ||
            ui_probe_mode_ == UiProbeMode::ItemsSourceStringExplicitPanel)
        {
            startup_.Stage("ui-probe.no-item-template");
            return;
        }

        constexpr const wchar_t* kConstantProbeTemplate = LR"XAML(
<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <Grid Height="32">
    <TextBlock Text="Blink probe" />
  </Grid>
</DataTemplate>)XAML";

        constexpr const wchar_t* kPropertySetIndexerProbeTemplate = LR"XAML(
<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <TextBlock Text="{Binding [Name]}" />
</DataTemplate>)XAML";

        const wchar_t* template_text = kConstantProbeTemplate;
        std::string template_stage = "ConfigureFolderItemTemplate.data-template";
        switch (ui_probe_mode_)
        {
        case UiProbeMode::ItemsSourceStringConstantTemplate:
            template_text = kConstantProbeTemplate;
            template_stage = "ui-probe.P5.constant-template";
            break;
        case UiProbeMode::CurrentMinimalPropertySet:
            template_text = kPropertySetIndexerProbeTemplate;
            template_stage = "ui-probe.P6.propertyset-indexer-binding";
            break;
        default:
            break;
        }

        startup_.Stage(template_stage + ".begin");
        try
        {
            auto loaded = Microsoft::UI::Xaml::Markup::XamlReader::Load(
                hstring(template_text));
            auto data_template = loaded.as<DataTemplate>();
            entries_.ItemTemplate(data_template);
            startup_.Stage(template_stage + ".ok");
        }
        catch (const hresult_error& error)
        {
            startup_.HresultFailure(template_stage, error);
            throw;
        }
        catch (...)
        {
            startup_.GenericFailure(template_stage);
            throw;
        }
    }

    static constexpr std::size_t kPresentationBatchSize = 250;

    void PrepareRowPresentationSurface(
        TabUiState& tab_ui,
        BrowseTimingRecord* timing = nullptr)
    {
        tab_ui.row_items.clear();
        startup_.Stage("row-source.clear.begin");
        const auto vector_clear_start = BrowseClock::now();
        if (!tab_ui.row_items_source)
        {
            tab_ui.row_items_source =
                winrt::single_threaded_observable_vector<IInspectable>();
        }
        tab_ui.row_items_source.Clear();
        const double vector_clear_ms =
            ElapsedMilliseconds(vector_clear_start, BrowseClock::now());
        startup_.Stage("row-source.clear.ok");
        if (timing)
        {
            timing->observable_vector_ms = vector_clear_ms;
        }
    }

    void PrepareRowProjection(
        TabUiState& tab_ui,
        const std::vector<blink::core::FileEntry>& entries,
        BrowseTimingRecord* timing = nullptr)
    {
        startup_.Stage("projection.begin");
        const auto projection_start = BrowseClock::now();
        if (timing)
        {
            timing->t5_first_projection_start_ms = ElapsedMilliseconds(
                timing->navigation_accepted_at,
                projection_start);
        }
        tab_ui.row_models = blink::app::BuildFolderRowProjection(entries);
        const auto projection_end = BrowseClock::now();
        if (timing)
        {
            timing->projection_ms =
                ElapsedMilliseconds(projection_start, projection_end);
            timing->t6_first_projection_batch_ready_ms =
                ElapsedMilliseconds(timing->navigation_accepted_at, projection_end);
        }
        startup_.Stage("projection.end");
        PrepareRowPresentationSurface(tab_ui, timing);
        tab_ui.row_items.reserve(tab_ui.row_models.size());
    }

    void PublishRowItems(
        TabUiState& tab_ui,
        const std::size_t begin,
        const std::size_t end,
        BrowseTimingRecord* timing = nullptr)
    {
        double property_set_ms = 0.0;
        double observable_vector_append_ms = 0.0;
        const bool first_item = tab_ui.row_items.empty() && begin < end;
        if (first_item)
        {
            startup_.Stage("row-item.first.begin");
            if (timing)
            {
                timing->t7_first_ui_publish_start_ms = ElapsedMilliseconds(
                    timing->navigation_accepted_at,
                    BrowseClock::now());
            }
        }
        for (std::size_t index = begin; index < end; ++index)
        {
            const auto& model = tab_ui.row_models[index];
            const auto property_set_start = BrowseClock::now();
            auto item = winrt::Windows::Foundation::Collections::PropertySet();
            item.Insert(L"RowIndex", winrt::box_value(static_cast<std::uint64_t>(index)));
            item.Insert(L"Name", winrt::box_value(hstring(model.name)));
            item.Insert(L"Type", winrt::box_value(hstring(model.type)));
            item.Insert(L"Size", winrt::box_value(hstring(model.size)));
            item.Insert(L"Modified", winrt::box_value(hstring(model.modified)));
            item.Insert(L"Icon", winrt::box_value(hstring(model.icon)));
            item.Insert(L"Directory", winrt::box_value(model.is_directory));
            tab_ui.row_items.push_back(item);
            const auto append_start = BrowseClock::now();
            property_set_ms += ElapsedMilliseconds(property_set_start, append_start);
            if (first_item && index == begin)
            {
                startup_.Stage("row-source.first-append.begin");
            }
            tab_ui.row_items_source.Append(item);
            observable_vector_append_ms +=
                ElapsedMilliseconds(append_start, BrowseClock::now());
            if (first_item && index == begin)
            {
                startup_.Stage("row-source.first-append.ok");
                startup_.Stage("row-item.first.ok");
                if (timing && timing->ui_begin != BrowseClock::time_point{})
                {
                    timing->first_usable_content_ms =
                        ElapsedMilliseconds(timing->ui_begin, BrowseClock::now());
                    timing->navigation_to_first_row_ms =
                        ElapsedMilliseconds(
                            timing->navigation_accepted_at,
                            BrowseClock::now());
                    timing->t8_first_row_available_ms =
                        timing->navigation_to_first_row_ms;
                }
            }
        }
        if (timing)
        {
            timing->property_set_ms += property_set_ms;
            timing->observable_vector_ms += observable_vector_append_ms;
        }
    }

    void RestoreRenderedSelection(
        const blink::app::TabId tab_id,
        TabUiState& tab_ui,
        BrowseTimingRecord* timing = nullptr,
        const BrowseClock::time_point list_view_start = BrowseClock::time_point{})
    {
        const auto selection_restore_start = BrowseClock::now();
        startup_.Stage("render.selection-restore.begin");
        const auto* tab = FindTab(tab_id);
        for (std::size_t index = 0; tab && index < tab_ui.row_models.size(); ++index)
        {
            const auto& row_identity = tab_ui.row_models[index].row_identity;
            if (row_identity.file_identity.valid && tab->state->IsRowSelected(row_identity) &&
                tab_ui.row_items_source && index < tab_ui.row_items_source.Size() &&
                tab_id == tab_window_state_.ActiveTabId())
            {
                entries_.SelectedItems().Append(
                    tab_ui.row_items_source.GetAt(static_cast<std::uint32_t>(index)));
            }
        }
        startup_.Stage("render.selection-restore.end");
        if (timing)
        {
            timing->selection_restore_ms =
                ElapsedMilliseconds(selection_restore_start, BrowseClock::now());
        }
        if (tab_id == tab_window_state_.ActiveTabId())
        {
            selection_sync_suppressed_ = false;
            UpdateSelectionStatus();
        }
        tab_ui.showing_committed_folder = true;
        if (timing && list_view_start != BrowseClock::time_point{})
        {
            timing->list_view_completion_ms =
                ElapsedMilliseconds(list_view_start, BrowseClock::now());
        }
    }

    void BeginBatchedRowPresentation(
        const blink::app::TabId tab_id,
        std::vector<blink::core::FileEntry> entries,
        const blink::app::SortDescriptor sort_descriptor,
        const std::shared_ptr<BrowseTimingRecord>& timing)
    {
        auto* tab_ui = FindTabUi(tab_id);
        if (!tab_ui)
        {
            return;
        }
        const bool active = tab_id == tab_window_state_.ActiveTabId();
        if (active)
        {
            selection_sync_suppressed_ = true;
        }
        tab_ui->showing_committed_folder = false;
        tab_ui->presentation_in_progress = true;
        startup_.Stage("render.selection-clear.begin");
        if (active && entries_)
        {
            entries_.SelectedItems().Clear();
        }
        startup_.Stage("render.selection-clear.ok");
        tab_ui->row_models.clear();
        PrepareRowPresentationSurface(*tab_ui, timing.get());
        tab_ui->presentation_expected_count = entries.size();
        tab_ui->row_models.reserve(tab_ui->presentation_expected_count);
        tab_ui->row_items.reserve(tab_ui->presentation_expected_count);
        if (timing)
        {
            timing->presentation_batched = true;
        }
        tab_ui->presentation_next_index = 0;
        if (active)
        {
            UpdateNavigationButtons();
        }
        const auto presentation_generation = ++tab_ui->presentation_generation;
        startup_.Stage("projection.background.begin");
        const auto weak_app = get_weak();
        const auto dispatcher = dispatcher_;
        try
        {
            tab_ui->presentation_work =
                winrt::Windows::System::Threading::ThreadPool::RunAsync(
                    [weak_app,
                        tab_id,
                        dispatcher,
                        presentation_generation,
                        timing,
                        sort_descriptor,
                        entries = std::move(entries)](
                        winrt::Windows::Foundation::IAsyncAction const& action) mutable {
                        try
                        {
                            const auto sort_start = BrowseClock::now();
                            auto sorted_entries = blink::app::TabState::SortEntries(
                                std::move(entries),
                                sort_descriptor);
                            const auto sort_end = BrowseClock::now();
                            const auto projection_start = sort_end;
                            const std::size_t total = sorted_entries.size();
                            BrowseClock::time_point first_batch_ready{};
                            blink::app::FolderRowProjectionBuilder projection_builder;

                            for (std::size_t begin = 0; begin < total; begin += kPresentationBatchSize)
                            {
                                if (action.Status() ==
                                    winrt::Windows::Foundation::AsyncStatus::Canceled)
                                {
                                    return;
                                }

                                const std::size_t end = std::min(
                                    begin + kPresentationBatchSize,
                                    total);
                                auto models = projection_builder.Build(
                                    sorted_entries,
                                    begin,
                                    end);
                                const auto batch_ready = BrowseClock::now();
                                if (first_batch_ready == BrowseClock::time_point{})
                                {
                                    first_batch_ready = batch_ready;
                                }

                                ProjectedPresentationBatch batch;
                                batch.rows = std::move(models);
                                batch.sort_ms = ElapsedMilliseconds(sort_start, sort_end);
                                batch.projection_ms = ElapsedMilliseconds(
                                    projection_start,
                                    batch_ready);
                                batch.sort_started_at = sort_start;
                                batch.sort_completed_at = sort_end;
                                batch.projection_started_at = projection_start;
                                batch.first_batch_ready_at = first_batch_ready;
                                if (!dispatcher.TryEnqueue(
                                    [weak_app,
                                        tab_id,
                                        presentation_generation,
                                        timing,
                                        batch = std::move(batch)]() mutable {
                                        if (auto app = weak_app.get())
                                        {
                                            app->PublishProjectedPresentationBatch(
                                                tab_id,
                                                presentation_generation,
                                                timing,
                                                std::move(batch));
                                        }
                                    }))
                                {
                                    return;
                                }
                            }

                            if (action.Status() ==
                                winrt::Windows::Foundation::AsyncStatus::Canceled)
                            {
                                return;
                            }

                            const auto projection_end = BrowseClock::now();
                            ProjectedPresentationBatch final_batch;
                            final_batch.final_batch = true;
                            final_batch.sort_ms = ElapsedMilliseconds(sort_start, sort_end);
                            final_batch.projection_ms = ElapsedMilliseconds(
                                projection_start,
                                projection_end);
                            final_batch.sort_started_at = sort_start;
                            final_batch.sort_completed_at = sort_end;
                            final_batch.projection_started_at = projection_start;
                            final_batch.first_batch_ready_at = first_batch_ready;
                            final_batch.projection_completed_at = projection_end;
                            (void)dispatcher.TryEnqueue(
                                [weak_app,
                                    tab_id,
                                    presentation_generation,
                                    timing,
                                    final_batch = std::move(final_batch)]() mutable {
                                    if (auto app = weak_app.get())
                                    {
                                        app->PublishProjectedPresentationBatch(
                                            tab_id,
                                            presentation_generation,
                                            timing,
                                            std::move(final_batch));
                                    }
                                });
                        }
                        catch (...)
                        {
                            const auto failure = std::current_exception();
                            try
                            {
                                (void)dispatcher.TryEnqueue(
                                    [weak_app,
                                        tab_id,
                                        presentation_generation,
                                        failure]() mutable {
                                        if (auto app = weak_app.get())
                                        {
                                            auto* tab_ui = app->FindTabUi(tab_id);
                                            if (tab_ui && presentation_generation ==
                                                tab_ui->presentation_generation)
                                            {
                                                tab_ui->presentation_work = nullptr;
                                                tab_ui->presentation_in_progress = false;
                                                if (tab_id == app->tab_window_state_.ActiveTabId())
                                                {
                                                    app->selection_sync_suppressed_ = false;
                                                }
                                                app->startup_.GenericFailure(
                                                    "projection.background");
                                                std::rethrow_exception(failure);
                                            }
                                        }
                                    });
                            }
                            catch (...)
                            {
                                // A dispatcher shutdown is terminal; the
                                // weak-app/generation gates prevent stale UI.
                            }
                        }
                    });
        }
        catch (...)
        {
            tab_ui->presentation_in_progress = false;
            if (active)
            {
                selection_sync_suppressed_ = false;
            }
            startup_.GenericFailure("projection.background");
        }
    }

    void PublishProjectedPresentationBatch(
        const blink::app::TabId tab_id,
        const std::uint64_t presentation_generation,
        const std::shared_ptr<BrowseTimingRecord>& timing,
        ProjectedPresentationBatch batch)
    {
        auto* tab_ui = FindTabUi(tab_id);
        if (!tab_ui || presentation_generation != tab_ui->presentation_generation)
        {
            return;
        }

        const bool active = tab_id == tab_window_state_.ActiveTabId();
        if (batch.final_batch)
        {
            tab_ui->presentation_work = nullptr;
            if (tab_ui->row_models.size() != tab_ui->presentation_expected_count)
            {
                tab_ui->presentation_in_progress = false;
                if (active)
                {
                    selection_sync_suppressed_ = false;
                }
                startup_.GenericFailure("projection.background.incomplete");
                return;
            }

            if (timing)
            {
                timing->sorting_ms = batch.sort_ms;
                timing->projection_ms = batch.projection_ms;
                timing->t3_sort_start_ms = ElapsedMilliseconds(
                    timing->navigation_accepted_at,
                    batch.sort_started_at);
                timing->t4_sort_complete_ms = ElapsedMilliseconds(
                    timing->navigation_accepted_at,
                    batch.sort_completed_at);
                timing->t5_first_projection_start_ms = ElapsedMilliseconds(
                    timing->navigation_accepted_at,
                    batch.projection_started_at);
                timing->t6_first_projection_batch_ready_ms =
                    batch.first_batch_ready_at == BrowseClock::time_point{} ?
                    0.0 :
                    ElapsedMilliseconds(
                        timing->navigation_accepted_at,
                        batch.first_batch_ready_at);
            }
            startup_.Stage("projection.background.end");
            const auto list_view_start = BrowseClock::now();
            RestoreRenderedSelection(tab_id, *tab_ui, timing.get(), list_view_start);
            tab_ui->presentation_in_progress = false;
            startup_.Stage("render.presentation.complete");
            if (timing && active)
            {
                FinishBrowsePresentation(timing);
            }
            else if (active)
            {
                FinishPresentationWithoutTiming();
            }
            return;
        }

        const auto batch_start = BrowseClock::now();
        startup_.Stage("presentation.batch.begin");
        const bool first_batch = tab_ui->row_models.empty() && !batch.rows.empty();
        if (first_batch && timing)
        {
            timing->t6_first_projection_batch_ready_ms =
                ElapsedMilliseconds(
                    timing->navigation_accepted_at,
                    batch.first_batch_ready_at);
        }
        const std::size_t begin = tab_ui->row_models.size();
        tab_ui->row_models.insert(
            tab_ui->row_models.end(),
            std::make_move_iterator(batch.rows.begin()),
            std::make_move_iterator(batch.rows.end()));
        const std::size_t end = tab_ui->row_models.size();
        PublishRowItems(*tab_ui, begin, end, timing.get());
        tab_ui->presentation_next_index = end;
        startup_.Stage("presentation.batch.end");
        const auto batch_end = BrowseClock::now();
        const double batch_ms = ElapsedMilliseconds(batch_start, batch_end);
        if (timing)
        {
            timing->ui_work_total_ms += batch_ms;
            timing->longest_ui_block_ms = std::max(
                timing->longest_ui_block_ms,
                batch_ms);
        }
    }

    void RebuildRowProjection(
        const blink::app::TabId tab_id,
        const std::vector<blink::core::FileEntry>& entries,
        BrowseTimingRecord* timing = nullptr)
    {
        auto* tab_ui = FindTabUi(tab_id);
        if (!tab_ui)
        {
            return;
        }
        PrepareRowProjection(*tab_ui, entries, timing);
        PublishRowItems(*tab_ui, 0, tab_ui->row_models.size(), timing);
        startup_.Stage("row-source.complete");
    }

    void RenderEntries(
        const blink::app::TabId tab_id,
        const std::vector<blink::core::FileEntry>& entries,
        BrowseTimingRecord* timing = nullptr)
    {
        auto* tab_ui = FindTabUi(tab_id);
        if (!tab_ui)
        {
            return;
        }
        const bool active = tab_id == tab_window_state_.ActiveTabId();
        if (active)
        {
            selection_sync_suppressed_ = true;
        }
        startup_.Stage("render.selection-clear.begin");
        if (active && entries_)
        {
            entries_.SelectedItems().Clear();
        }
        startup_.Stage("render.selection-clear.ok");
        RebuildRowProjection(tab_id, entries, timing);
        const auto list_view_start = BrowseClock::now();
        RestoreRenderedSelection(tab_id, *tab_ui, timing, list_view_start);
    }

    [[nodiscard]] std::optional<std::size_t> RowIndexForItem(
        const IInspectable& item) const
    {
        const auto* tab_ui = ActiveTabUi();
        if (!tab_ui)
        {
            return std::nullopt;
        }
        if (const auto prop_set = item.try_as<winrt::Windows::Foundation::Collections::PropertySet>())
        {
            if (prop_set.HasKey(L"RowIndex"))
            {
                try
                {
                    const auto index = static_cast<std::size_t>(
                        winrt::unbox_value<std::uint64_t>(prop_set.Lookup(L"RowIndex")));
                    if (index < tab_ui->row_items.size() && item == tab_ui->row_items[index])
                    {
                        return index;
                    }
                }
                catch (...)
                {
                }
            }
        }
        for (std::size_t index = 0; index < tab_ui->row_items.size(); ++index)
        {
            // Compare the canonical IUnknown identity, not the ABI pointer of
            // whichever projected interface DataContext currently exposes.
            // XAML may return another interface view for the same PropertySet.
            if (item == tab_ui->row_items[index])
            {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> ResolveClickedRowIndex(
        const RoutedEventArgs& args) const
    {
        return ResolveRowIndexFromElement(args.OriginalSource());
    }

    [[nodiscard]] std::optional<std::size_t> ResolveRowIndexFromElement(
        const IInspectable& source) const
    {
        auto element = source.try_as<FrameworkElement>();
        while (element)
        {
            const auto data_context = element.DataContext();
            if (data_context)
            {
                if (const auto index = RowIndexForItem(data_context); index.has_value())
                {
                    return index;
                }
            }
            element = VisualTreeHelper::GetParent(element).try_as<FrameworkElement>();
        }
        return std::nullopt;
    }

    void RenderUiProbeItems(const blink::app::TabId tab_id)
    {
        auto* tab_ui = FindTabUi(tab_id);
        if (!tab_ui)
        {
            return;
        }
        startup_.Stage("ui-probe.items.begin");
        if (ui_probe_mode_ == UiProbeMode::NoFolderList)
        {
            startup_.Stage("folder-render-skipped");
            startup_.Stage("ui-probe.items.end");
            return;
        }

        const bool active = tab_id == tab_window_state_.ActiveTabId();
        if (active)
        {
            selection_sync_suppressed_ = true;
            entries_.SelectedItems().Clear();
        }
        tab_ui->row_models.clear();
        tab_ui->row_items.clear();
        const bool uses_items_source =
            ui_probe_mode_ == UiProbeMode::ItemsSourceString ||
            ui_probe_mode_ == UiProbeMode::ItemsSourceStringExplicitPanel ||
            ui_probe_mode_ == UiProbeMode::ItemsSourceStringConstantTemplate ||
            ui_probe_mode_ == UiProbeMode::CurrentMinimalPropertySet;
        if (uses_items_source)
        {
            if (!tab_ui->row_items_source)
            {
                tab_ui->row_items_source =
                    winrt::single_threaded_observable_vector<IInspectable>();
            }
            tab_ui->row_items_source.Clear();
        }

        for (int index = 1; index <= 3; ++index)
        {
            const std::wstring name = L"Blink probe item " + std::to_wstring(index);
            const auto string_item = winrt::box_value(hstring(name));
            if (ui_probe_mode_ == UiProbeMode::DirectItemsString)
            {
                if (active)
                {
                    entries_.Items().Append(string_item);
                }
                continue;
            }
            if (ui_probe_mode_ == UiProbeMode::ItemsSourceString ||
                ui_probe_mode_ == UiProbeMode::ItemsSourceStringExplicitPanel ||
                ui_probe_mode_ == UiProbeMode::ItemsSourceStringConstantTemplate)
            {
                tab_ui->row_items_source.Append(string_item);
                continue;
            }

            if (ui_probe_mode_ == UiProbeMode::CurrentMinimalPropertySet)
            {
                auto item = winrt::Windows::Foundation::Collections::PropertySet();
                item.Insert(L"Name", string_item);
                tab_ui->row_items.push_back(item);
                tab_ui->row_items_source.Append(item);
            }
        }

        if (active)
        {
            selection_sync_suppressed_ = false;
            selection_status_.Text(hstring(localization_.GetString(L"Status.ZeroSelected")));
        }
        tab_ui->showing_committed_folder = false;
        startup_.Stage("ui-probe.items.end");
    }

    void OnListSelectionChanged()
    {
        const auto* tab_ui = ActiveTabUi();
        if (!tab_ui || selection_sync_suppressed_ || !tab_ui->showing_committed_folder)
        {
            return;
        }

        // Bind menu commands to this selection, including selections larger
        // than the clipboard/native-menu capture limit.
        ++folder_selection_generation_;

        std::vector<blink::app::RowIdentity> rows;
        std::vector<IInspectable> valid_items;
        std::vector<IInspectable> invalid_items;
        const auto selected_items = entries_.SelectedItems();
        for (std::uint32_t index = 0; index < selected_items.Size(); ++index)
        {
            const auto item = selected_items.GetAt(index);
            const auto rendered_index = RowIndexForItem(item);
            if (!rendered_index.has_value())
            {
                continue;
            }
            if (*rendered_index >= tab_ui->row_models.size())
            {
                continue;
            }
            const auto& row = tab_ui->row_models[*rendered_index];
            if (!row.identity.valid)
            {
                invalid_items.push_back(item);
                continue;
            }
            rows.push_back(row.row_identity);
            valid_items.push_back(item);
        }

        if (!invalid_items.empty())
        {
            selection_sync_suppressed_ = true;
            entries_.SelectedItems().Clear();
            for (const auto& item : valid_items)
            {
                entries_.SelectedItems().Append(item);
            }
            selection_sync_suppressed_ = false;
        }
        ActiveState().SetRowSelection(std::move(rows));
        UpdateSelectionStatus();
    }

    [[nodiscard]] std::optional<blink::core::FileEntry> SelectedEntryFromCommittedState() const
    {
        const auto* tab_ui = ActiveTabUi();
        if (!tab_ui || !tab_ui->showing_committed_folder)
        {
            return std::nullopt;
        }
        if (entries_.SelectedItems().Size() == 0)
        {
            return std::nullopt;
        }
        const auto index = RowIndexForItem(entries_.SelectedItems().GetAt(0));
        if (!index.has_value() || *index >= tab_ui->row_models.size())
        {
            return std::nullopt;
        }
        const auto& row = tab_ui->row_models[*index];
        if (!row.identity.valid)
        {
            return std::nullopt;
        }
        return ActiveState().SelectedCommittedRow(row.row_identity);
    }

    StartupTelemetry startup_;
    blink::app::Localization localization_;
    UiProbeMode ui_probe_mode_{ UiProbeMode::None };
    Window window_{ nullptr };
    DispatcherQueue dispatcher_{ nullptr };
    Grid root_{ nullptr };
    Grid window_frame_{ nullptr };
    TitleBar title_bar_{ nullptr };
    MenuFlyout folder_menu_{ nullptr };
    std::optional<FolderMenuContext> folder_system_menu_context_;
    bool window_style_closed_{};
    blink::app::FileOperationCoordinator file_operations_;
    blink::app::FolderRelocationTracker folder_relocations_;
    std::uint64_t operation_watch_id_{};
    std::vector<blink::app::Location> operation_watch_sources_;
    std::vector<blink::app::TabId> operation_watch_tabs_;
    blink::app::FolderNameDialog folder_name_dialog_;
    blink::app::FileCopyConflictDialog copy_conflict_dialog_;
    std::uint64_t directory_merge_dialog_operation_id_{}, directory_merge_dialog_conflict_id_{};
    blink::app::DeleteConfirmationDialog delete_confirmation_;
    blink::app::PropertiesDialog properties_dialog_;
    blink::app::FileDragDropController file_drag_drop_;
    std::optional<FileOperationOrigin> drop_retry_origin_;
    DispatcherQueueTimer drag_hover_timer_{ nullptr };
    blink::app::TabId drag_hover_tab_{};
    std::uint64_t delete_wait_operation_id_{};
    winrt::Windows::Foundation::IAsyncAction delete_end_wait_{ nullptr };
    std::uint64_t copy_conflict_wait_operation_id_{};
    winrt::Windows::Foundation::IAsyncAction copy_conflict_wait_{ nullptr };
    bool clipboard_work_pending_{};
    blink::app::FileOperationCancellation clipboard_cancellation_;
    blink::app::ClipboardCopyCache clipboard_copy_cache_;
    blink::app::ShellContextMenuController shell_context_menu_;
    blink::app::SearchContextMenuPresentation search_context_menu_presentation_;
    std::optional<blink::app::SearchContextSelection> search_menu_selection_;
    std::uint64_t search_menu_owner_generation_{}, search_menu_generation_{};
    std::optional<PendingSearchFolderCommand> pending_search_folder_command_;
    blink::app::DirectoryCopyPartialHistory directory_copy_history_;
    std::uint64_t copy_refresh_operation_id_{};
    std::size_t copy_refreshed_count_{}, copy_partial_refreshed_count_{};
    winrt::Windows::Foundation::IAsyncAction clipboard_work_{ nullptr };
    Grid operation_panel_{ nullptr };
    TextBlock operation_text_{ nullptr };
    Button operation_cancel_{ nullptr }, operation_retry_{ nullptr }, operation_skip_{ nullptr };
    TabView tab_view_{ nullptr };
    ScrollViewer navigation_pane_{ nullptr };
    std::optional<double> navigation_wheel_target_;
    HWND navigation_window_handle_{};
    std::vector<HWND> navigation_wheel_windows_;
    ListView navigation_quick_list_{ nullptr };
    winrt::Windows::Foundation::Collections::IObservableVector<IInspectable>
        navigation_quick_items_source_{ nullptr };
    std::vector<QuickNavigationItem> quick_navigation_items_;
    TreeView navigation_tree_{ nullptr };
    Grid content_grid_{ nullptr };
    Grid details_header_{ nullptr };
    ComboBox view_mode_picker_{ nullptr };
    ComboBox sort_field_picker_{ nullptr };
    Button sort_direction_button_{ nullptr };
    CheckBox show_hidden_items_{ nullptr };
    blink::app::FolderViewPresentation folder_view_presentation_;
    blink::app::FolderViewMode rendered_view_mode_{ blink::app::FolderViewMode::Details };
    std::uint64_t folder_view_generation_{};
    bool view_commands_sync_suppressed_{};
    TextBox path_box_{ nullptr };
    Button back_button_{ nullptr };
    Button forward_button_{ nullptr };
    Button up_button_{ nullptr };
    Button refresh_button_{ nullptr };
    TextBox search_box_{ nullptr };
    Grid search_surface_{ nullptr };
    blink::app::SearchPresentation search_presentation_;
    blink::app::SearchRequestQueue search_requests_;
    Windows::Foundation::IAsyncAction search_work_{ nullptr };
    bool search_input_sync_{};
    bool index_refresh_deferred_{};
    TextBlock status_{ nullptr };
    TextBlock selection_status_{ nullptr };
    Button index_status_{ nullptr };
    blink::app::IndexServicePresentation index_service_presentation_;
    std::shared_ptr<blink::app::IndexServiceState> index_service_state_;
    std::unique_ptr<blink::app::IndexServiceCoordinator> index_service_coordinator_;
    DispatcherQueueTimer index_service_timer_{ nullptr };
    std::wstring index_service_selected_root_;
    ListViewBase entries_{ nullptr };
    std::uint64_t folder_selection_generation_{};
    bool selection_sync_suppressed_{};
    bool navigation_quick_selection_sync_suppressed_{};
    bool navigation_tree_selection_sync_suppressed_{};
    bool navigation_tree_expansion_sync_suppressed_{};
    bool tab_view_sync_suppressed_{};
    std::vector<blink::app::TabId> tab_item_ids_;
    blink::app::TabWindowState tab_window_state_;
    std::unordered_map<blink::app::TabId, std::unique_ptr<TabUiState>> tab_ui_states_;
    blink::app::NavigationTreeModel navigation_tree_model_;
    std::unordered_map<blink::app::NavigationTreeNodeId, TreeViewNode>
        navigation_tree_ui_nodes_;
    std::unordered_map<blink::app::NavigationTreeNodeId, winrt::Windows::Foundation::IAsyncAction>
        navigation_tree_expansion_work_;
    std::unordered_map<blink::app::NavigationTreeNodeId, PendingNavigationTreeExpansion>
        navigation_tree_pending_expansions_;
    winrt::Windows::Foundation::IAsyncAction navigation_tree_reveal_work_{ nullptr };
    std::uint64_t navigation_tree_lifetime_generation_{};
    std::uint64_t navigation_tree_reveal_generation_{};
};
}

// The generated metadata includes App.xaml.g.hpp and its startup factory.
// App is defined in this translation unit, so both must be compiled here.
#include "XamlTypeInfo.g.cpp"
#include "XamlTypeInfo.Impl.g.cpp"
