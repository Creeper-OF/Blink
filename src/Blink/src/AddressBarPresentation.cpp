#include "Blink/App/AddressBarPresentation.h"

#include "Blink/App/ContextMenuAppearance.h"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <algorithm>
#include <chrono>
#include <utility>

namespace blink::app
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;

    namespace
    {
        // BreadcrumbBar accepts IElementFactory for both inline and overflow
        // items. Prepare fresh native containers before their template wires
        // Enter/Space, so child actions work in either presentation.
        struct BreadcrumbItemFactory : implements<BreadcrumbItemFactory, IElementFactory>
        {
            DataTemplate content_template;
            std::function<void(const BreadcrumbBarItem&, const Windows::Foundation::IInspectable&)> prepare;

            BreadcrumbItemFactory(DataTemplate value, decltype(prepare) callback)
                : content_template(std::move(value)), prepare(std::move(callback)) {}

            UIElement GetElement(const ElementFactoryGetArgs& args)
            {
                BreadcrumbBarItem item;
                item.Content(args.Data());
                item.ContentTemplate(content_template);
                prepare(item, args.Data());
                return item;
            }

            void RecycleElement(const ElementFactoryRecycleArgs&) {}
        };
    }

    struct AddressBarPresentation::Ui : std::enable_shared_from_this<Ui>
    {
        ContentControl host{ nullptr };
        Grid row{ nullptr };
        Grid breadcrumb_host{ nullptr };
        BreadcrumbBar breadcrumb_bar{ nullptr };
        TextBox editor{ nullptr };
        Button history_button{ nullptr };
        MenuFlyout history_menu{ nullptr };
        MenuFlyout children_menu{ nullptr };
        Primitives::Popup suggestion_popup{ nullptr };
        FlyoutPresenter suggestion_presenter{ nullptr };
        ListView suggestion_list{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer suggestion_timer{ nullptr };
        std::vector<AddressBreadcrumb> suggestions;
        Location committed_location;
        std::wstring observed_text;
        std::wstring pending_suggestion_draft;
        std::uint64_t suggestion_revision{}, pending_suggestion_request{}, pending_suggestion_owner{};
        bool suggestion_pending{}, suggestion_inflight{}, suggestions_armed{}, active{ true };
        int pending_suggestion_direction{};
        Location children_parent;
        Localization localization;
        AddressBarState state;
        std::vector<AddressBreadcrumb> breadcrumbs;
        std::vector<Location> history;
        std::wstring this_pc_label;
        std::wstring address_name;
        Callbacks callbacks;
        std::uint64_t breadcrumb_revision{};
        std::uint64_t history_revision{};
        std::uint64_t children_revision{};
        bool children_open{};
        bool children_loading{};
        bool composing{};
        bool enabled{ true };
        bool retired{};

        [[nodiscard]] std::wstring TextFor(const Location& location) const
        {
            return location.IsThisPC() ? this_pc_label : location.FolderPath();
        }

        void SyncHelpText()
        {
            const auto& text = state.IsEditing() ? state.DraftText() : state.CommittedText();
            Automation::AutomationProperties::SetHelpText(host, hstring(text));
            if (breadcrumb_bar)
            { Automation::AutomationProperties::SetHelpText(breadcrumb_bar, hstring(text)); }
        }

        void SyncVisuals()
        {
            const auto& text = state.IsEditing() ? state.DraftText() : state.CommittedText();
            const hstring display_text(text);
            if (editor.Text() != display_text)
            {
                observed_text = text;
                editor.Text(display_text);
            }
            editor.Visibility(state.IsEditing() ? Visibility::Visible : Visibility::Collapsed);
            breadcrumb_host.Visibility(state.IsEditing() ? Visibility::Collapsed : Visibility::Visible);
            SyncHelpText();
        }

        [[nodiscard]] bool HasEditorFocus() const
        {
            if (!host.XamlRoot()) { return false; }
            auto focused = Input::FocusManager::GetFocusedElement(host.XamlRoot()).try_as<DependencyObject>();
            for (; focused; focused = Media::VisualTreeHelper::GetParent(focused))
            { if (focused == editor) { return true; } }
            return false;
        }

        void HideSuggestions() noexcept
        {
            const auto request = suggestion_revision++;
            suggestion_pending = false;
            suggestion_inflight = false;
            pending_suggestion_direction = 0;
            try { if (suggestion_timer) { suggestion_timer.Stop(); } } catch (...) {}
            try { if (suggestion_popup) { suggestion_popup.IsOpen(false); } } catch (...) {}
            suggestions.clear();
            try { if (suggestion_list) { suggestion_list.Items().Clear(); } } catch (...) {}
            try { if (callbacks.suggestions_cancelled) { callbacks.suggestions_cancelled(request); } } catch (...) {}
        }

        void SizeSuggestions()
        {
            suggestion_presenter.Width(std::clamp(editor.ActualWidth(), 240.0, 640.0));
        }

        void RequestSuggestions()
        {
            suggestion_timer.Stop();
            if (retired || !enabled || !active || composing || !suggestions_armed || !state.IsEditing() ||
                !suggestion_pending || !HasEditorFocus() || pending_suggestion_request != suggestion_revision ||
                pending_suggestion_owner != state.Owner() || editor.Text() != hstring(pending_suggestion_draft))
            { return; }
            suggestion_pending = false;
            suggestion_inflight = true;
            const auto requested = callbacks.suggestions_requested;
            if (requested) { requested(state.Owner(), suggestion_revision, pending_suggestion_draft, committed_location); }
        }

        void QueueSuggestions(const bool immediate = false, const int direction = 0)
        {
            HideSuggestions();
            if (retired || !enabled || !active || composing || !suggestions_armed || !state.IsEditing() || !HasEditorFocus())
            { return; }
            pending_suggestion_request = ++suggestion_revision;
            pending_suggestion_owner = state.Owner();
            pending_suggestion_draft = std::wstring(editor.Text());
            pending_suggestion_direction = direction;
            suggestion_pending = true;
            if (immediate) { RequestSuggestions(); }
            else { suggestion_timer.Start(); }
        }

        void AcceptSuggestion(const std::size_t index, const bool navigate_now)
        {
            if (retired || !enabled || !active || composing || !state.IsEditing() ||
                !suggestion_popup.IsOpen() || index >= suggestions.size()) { return; }
            if (editor.Text() != hstring(pending_suggestion_draft)) { HideSuggestions(); return; }
            const auto target = suggestions[index].location;
            if (!target.IsFolder()) { return; }
            auto text = target.FolderPath();
            HideSuggestions();
            if (!navigate_now && !text.empty() && text.back() != L'\\') { text += L'\\'; }
            observed_text = text;
            state.SetDraft(text);
            editor.Text(hstring(text));
            editor.SelectionStart(static_cast<int32_t>(text.size()));
            editor.SelectionLength(0);
            SyncHelpText();
            if (navigate_now)
            {
                suggestions_armed = false;
                const auto navigate = callbacks.navigate;
                if (navigate) { navigate(target); }
            }
            else { QueueSuggestions(); }
        }

        void CompleteSuggestions(const std::uint64_t owner, const std::uint64_t request,
            const std::wstring& draft, std::vector<AddressBreadcrumb> values)
        {
            if (retired || !enabled || !active || composing || !suggestions_armed || !state.IsEditing() ||
                suggestion_revision != request || state.Owner() != owner || editor.Text() != hstring(draft) || !HasEditorFocus())
            { return; }
            suggestion_inflight = false;
            suggestion_list.Items().Clear();
            suggestions = std::move(values);
            if (suggestions.empty()) { suggestion_popup.IsOpen(false); return; }
            for (std::size_t index = 0; index < suggestions.size(); ++index)
            {
                const auto& value = suggestions[index];
                ListViewItem item;
                item.IsTabStop(false);
                item.AllowFocusOnInteraction(false);
                item.MinHeight(32);
                item.Padding(Thickness{ 10, 6, 10, 6 });
                item.Tag(box_value(static_cast<std::uint64_t>(index)));
                StackPanel content;
                content.Orientation(Orientation::Horizontal);
                content.Spacing(8);
                FontIcon icon;
                icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
                icon.FontSize(16);
                icon.Glyph(L"\xE8B7");
                TextBlock label;
                label.Text(hstring(value.label));
                label.VerticalAlignment(VerticalAlignment::Center);
                content.Children().Append(icon);
                content.Children().Append(label);
                item.Content(content);
                Automation::AutomationProperties::SetAutomationId(item, hstring(L"AddressSuggestion" + std::to_wstring(index)));
                Automation::AutomationProperties::SetName(item, hstring(value.label));
                Automation::AutomationProperties::SetHelpText(item, hstring(TextFor(value.location)));
                ToolTipService::SetToolTip(item, box_value(hstring(TextFor(value.location))));
                suggestion_list.Items().Append(item);
            }
            suggestion_list.SelectedIndex(pending_suggestion_direction > 0 ? 0 :
                pending_suggestion_direction < 0 ? static_cast<int32_t>(suggestions.size() - 1) : -1);
            pending_suggestion_direction = 0;
            SizeSuggestions();
            suggestion_presenter.RequestedTheme(host.ActualTheme());
            suggestion_popup.IsOpen(true);
        }

        void HideHistory() noexcept
        {
            // A normal Closed event must not retire a click that follows it.
            // Explicit hiding and snapshot changes do retire old menu peers.
            ++history_revision;
            try { if (history_menu) { history_menu.Hide(); } } catch (...) {}
        }

        void HideChildren() noexcept
        {
            const auto request = children_revision++;
            children_open = false;
            children_loading = false;
            try { if (children_menu) { children_menu.Hide(); } } catch (...) {}
            try { if (callbacks.children_cancelled) { callbacks.children_cancelled(request); } } catch (...) {}
        }

        void AddChildrenStatus(const wchar_t* id, const std::wstring& text)
        {
            MenuFlyoutItem item;
            item.Text(hstring(text));
            item.IsEnabled(false);
            StyleContextMenuItem(item);
            Automation::AutomationProperties::SetAutomationId(item, id);
            children_menu.Items().Append(item);
        }

        void OpenChildren(FrameworkElement anchor, const Location& parent)
        {
            if (retired || !enabled || state.IsEditing() || !anchor) { return; }
            HideHistory();
            HideChildren();
            // An overflow item's template belongs to a native popup. Dismiss
            // it before opening our menu, and use the stable address host.
            DependencyObject ancestor = anchor;
            for (; ancestor && ancestor != breadcrumb_host; ancestor = Media::VisualTreeHelper::GetParent(ancestor)) {}
            if (!ancestor)
            {
                RebuildBreadcrumbs();
                anchor = host;
            }
            const auto request = ++children_revision;
            const auto owner = state.Owner();
            children_parent = parent;
            children_open = true;
            children_loading = true;
            children_menu = MenuFlyout();
            ApplyContextMenuAppearance(children_menu, host, L"AddressChildrenMenu");
            AddChildrenStatus(L"AddressChildrenLoading", localization.GetString(L"AddressBar.ChildrenLoading"));
            const std::weak_ptr<Ui> weak = shared_from_this();
            children_menu.Closed([weak, request](auto&&, auto&&) {
                const auto live = weak.lock();
                if (!live || live->children_revision != request) { return; }
                live->children_open = false;
                // Closing while loading retires the result. A loaded menu's
                // Closed may precede its Click, so retain its action snapshot.
                if (live->children_loading) { ++live->children_revision; }
                live->children_loading = false;
                if (live->callbacks.children_cancelled) { live->callbacks.children_cancelled(request); }
            });
            children_menu.ShowAt(anchor);
            const auto requested = callbacks.children_requested;
            if (requested) { requested(owner, request, parent); }
        }

        void CompleteChildren(const std::uint64_t owner, const std::uint64_t request,
            const Location& parent, std::vector<AddressBreadcrumb> children, const std::wstring& error)
        {
            if (retired || !enabled || !children_open || !children_loading ||
                children_revision != request || state.Owner() != owner || children_parent != parent)
            { return; }
            children_loading = false;
            children_menu.Items().Clear();
            if (!error.empty())
            {
                AddChildrenStatus(L"AddressChildrenFailed", localization.Format(L"AddressBar.ChildrenFailed", error));
                return;
            }
            if (children.empty())
            {
                AddChildrenStatus(L"AddressChildrenEmpty", localization.GetString(L"AddressBar.ChildrenEmpty"));
                return;
            }
            const std::weak_ptr<Ui> weak = shared_from_this();
            for (std::size_t index = 0; index < children.size(); ++index)
            {
                const auto target = children[index].location;
                MenuFlyoutItem item;
                item.Text(hstring(children[index].label));
                StyleContextMenuItem(item, L"\xE8B7");
                Automation::AutomationProperties::SetAutomationId(item, hstring(L"AddressChild" + std::to_wstring(index)));
                Automation::AutomationProperties::SetHelpText(item, hstring(TextFor(target)));
                item.Click([weak, owner, request, target](auto&&, auto&&) {
                    if (const auto live = weak.lock(); live && !live->retired && live->enabled &&
                        live->children_revision == request && live->state.Owner() == owner)
                    {
                        const auto navigate = live->callbacks.navigate;
                        if (navigate) { navigate(target); }
                    }
                });
                children_menu.Items().Append(item);
            }
            // Loading has no focusable row. Give keyboard navigation a real
            // starting item while this menu still owns the interaction.
            children_menu.Items().GetAt(0).Focus(FocusState::Programmatic);
        }

        void RebuildBreadcrumbs()
        {
            const auto revision = ++breadcrumb_revision;
            if (breadcrumb_bar)
            {
                breadcrumb_bar.Visibility(Visibility::Collapsed);
                breadcrumb_bar.ItemsSource(nullptr);
            }
            breadcrumb_host.Children().Clear();

            BreadcrumbBar bar;
            bar.HorizontalAlignment(HorizontalAlignment::Stretch);
            bar.VerticalAlignment(VerticalAlignment::Center);
            bar.MinWidth(0);
            bar.Height(32);
            bar.Margin(Thickness{ 6, 0, 0, 0 });
            Automation::AutomationProperties::SetAutomationId(bar, L"AddressBreadcrumbs");
            Automation::AutomationProperties::SetName(bar, hstring(address_name));

            const auto content_template = Markup::XamlReader::Load(LR"(<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
                <StackPanel Orientation="Horizontal" Spacing="2" AutomationProperties.Name="{Binding [Label]}">
                    <TextBlock Text="{Binding [Label]}" VerticalAlignment="Center" />
                    <Button Command="{Binding [ChildrenCommand]}" CommandParameter="{Binding RelativeSource={RelativeSource Self}}"
                        AutomationProperties.AutomationId="{Binding [ChildrenId]}" AutomationProperties.Name="{Binding [ChildrenName]}"
                        AutomationProperties.HelpText="{Binding [Path]}" ToolTipService.ToolTip="{Binding [ChildrenName]}"
                        MinWidth="20" MinHeight="24" Padding="2,0" BorderThickness="0" Background="Transparent">
                        <FontIcon FontFamily="Segoe Fluent Icons" Glyph="&#xE70D;" FontSize="10" />
                    </Button>
                </StackPanel>
            </DataTemplate>)").as<DataTemplate>();
            // Native inline and overflow containers get separate visuals from
            // the template, while retaining the same immutable data target.
            auto items = single_threaded_observable_vector<Windows::Foundation::IInspectable>();
            std::vector<std::pair<Windows::Foundation::IInspectable, Location>> targets;
            targets.reserve(breadcrumbs.size());
            const auto owner = state.Owner();
            const std::weak_ptr<Ui> weak = shared_from_this();
            for (std::size_t index = 0; index < breadcrumbs.size(); ++index)
            {
                const auto& breadcrumb = breadcrumbs[index];
                const auto target = breadcrumb.location;
                Input::XamlUICommand command;
                // WinUI copies command descriptions into button HelpText.
                // Keep the immutable location available to assistive tools.
                command.Description(hstring(TextFor(target)));
                command.ExecuteRequested([weak, revision, owner, target](auto&&, const Input::ExecuteRequestedEventArgs& args) {
                    if (const auto live = weak.lock(); live && !live->retired && live->enabled &&
                        live->breadcrumb_revision == revision && live->state.Owner() == owner)
                    { live->OpenChildren(args.Parameter().try_as<FrameworkElement>(), target); }
                });
                Windows::Foundation::Collections::PropertySet item;
                item.Insert(L"Label", box_value(hstring(breadcrumb.label)));
                item.Insert(L"Path", box_value(hstring(TextFor(target))));
                item.Insert(L"ChildrenName", box_value(hstring(localization.Format(L"AddressBar.Children", breadcrumb.label))));
                item.Insert(L"ChildrenId", box_value(hstring(L"AddressChildren" + std::to_wstring(index))));
                item.Insert(L"ChildrenCommand", command);
                items.Append(item);
                targets.emplace_back(std::move(item), breadcrumb.location);
            }
            bar.ItemsSource(items);
            bar.ItemTemplate(make<BreadcrumbItemFactory>(content_template,
                [weak, revision, owner](const BreadcrumbBarItem& crumb, const Windows::Foundation::IInspectable& data) {
                    const auto values = data.try_as<Windows::Foundation::Collections::IMap<hstring, Windows::Foundation::IInspectable>>();
                    if (!values) { return; }
                    Automation::AutomationProperties::SetName(crumb, unbox_value<hstring>(values.Lookup(L"Label")));
                    Automation::AutomationProperties::SetHelpText(crumb, unbox_value<hstring>(values.Lookup(L"Path")));
                    const auto command = values.Lookup(L"ChildrenCommand").as<Input::XamlUICommand>();
                    const auto child_id = unbox_value<hstring>(values.Lookup(L"ChildrenId"));
                    crumb.PreviewKeyDown([weak, revision, owner, command, child_id](auto&& sender, const Input::KeyRoutedEventArgs& args) {
                        const auto live = weak.lock();
                        if (!live || live->retired || !live->enabled || live->breadcrumb_revision != revision || live->state.Owner() != owner)
                        { return; }
                        if (args.Key() == Windows::System::VirtualKey::Down)
                        {
                            args.Handled(true);
                            command.Execute(sender);
                            return;
                        }
                        if (args.Key() != Windows::System::VirtualKey::Enter && args.Key() != Windows::System::VirtualKey::Space)
                        { return; }
                        auto source = args.OriginalSource().try_as<DependencyObject>();
                        for (; source && source != sender; source = Media::VisualTreeHelper::GetParent(source))
                        {
                            if (const auto button = source.try_as<Button>(); button &&
                                Automation::AutomationProperties::GetAutomationId(button) == child_id)
                            {
                                args.Handled(true);
                                command.Execute(button);
                                return;
                            }
                        }
                    });
                }));
            bar.ItemClicked([weak, revision, owner, targets = std::move(targets)](
                auto&&, const BreadcrumbBarItemClickedEventArgs& args) {
                const auto live = weak.lock();
                if (!live || live->retired || !live->enabled ||
                    live->breadcrumb_revision != revision || live->state.Owner() != owner)
                { return; }
                const auto item = args.Item();
                for (const auto& target : targets)
                {
                    if (item == target.first)
                    {
                        const auto navigate = live->callbacks.navigate;
                        if (navigate) { navigate(target.second); }
                        return;
                    }
                }
            });
            breadcrumb_bar = bar;
            breadcrumb_host.Children().Append(bar);
            SyncHelpText();
        }

        void HideMenus() noexcept
        {
            HideHistory();
            HideChildren();
            HideSuggestions();
            // Replacing the native control also dismisses its overflow popup.
            // Its old event closure keeps its own values and is now retired.
            try { RebuildBreadcrumbs(); } catch (...) {}
        }

        void BeginEditing(const bool select_all)
        {
            if (retired || !enabled) { return; }
            if (state.IsEditing())
            {
                const auto text = editor.Text();
                state.SetDraft(std::wstring(text.c_str(), text.size()));
            }
            HideMenus();
            state.BeginEdit();
            suggestions_armed = true;
            SyncVisuals();
            editor.Focus(FocusState::Programmatic);
            if (select_all) { editor.SelectAll(); }
        }

        void CancelEditing()
        {
            HideMenus();
            composing = false;
            suggestions_armed = false;
            state.Cancel();
            SyncVisuals();
        }

        void OpenHistory()
        {
            if (retired || !enabled) { return; }
            HideMenus();
            const auto opening = callbacks.history_opening;
            if (opening) { opening(); }
            if (retired || !enabled) { return; }
            if (history.empty())
            {
                BeginEditing(true);
                return;
            }

            composing = false;
            state.Cancel();
            SyncVisuals();
            const auto revision = ++history_revision;
            const auto owner = state.Owner();
            const std::weak_ptr<Ui> weak = shared_from_this();
            MenuFlyout menu;
            ApplyContextMenuAppearance(menu, host, L"AddressHistoryMenu");
            for (std::size_t index = 0; index < history.size(); ++index)
            {
                const auto target = history[index];
                const auto text = TextFor(target);
                MenuFlyoutItem item;
                item.Text(hstring(text));
                StyleContextMenuItem(item);
                Automation::AutomationProperties::SetAutomationId(item,
                    hstring(L"AddressHistory" + std::to_wstring(index)));
                Automation::AutomationProperties::SetHelpText(item, hstring(text));
                item.Click([weak, revision, owner, target](auto&&, auto&&) {
                    if (const auto live = weak.lock(); live && !live->retired && live->enabled &&
                        live->history_revision == revision && live->state.Owner() == owner)
                    {
                        const auto navigate = live->callbacks.navigate;
                        if (navigate) { navigate(target); }
                    }
                });
                menu.Items().Append(item);
            }
            history_menu = menu;
            menu.ShowAt(history_button);
        }

        void Retire() noexcept
        {
            if (retired) { return; }
            retired = true;
            composing = false;
            callbacks = {};
            HideHistory();
            HideChildren();
            HideSuggestions();
            ++breadcrumb_revision;
            // Window.Closed may be unloading XAML. The native tree owns its
            // teardown; only retire late events and dismiss an existing menu.
        }

    };

    AddressBarPresentation::~AddressBarPresentation() noexcept
    {
        if (ui_) { ui_->Retire(); }
    }

    FrameworkElement AddressBarPresentation::Build(const Localization& localization, Callbacks callbacks)
    {
        if (ui_) { ui_->Retire(); }
        const auto ui = std::make_shared<Ui>();
        ui_ = ui;
        ui->callbacks = std::move(callbacks);
        ui->localization = localization;
        ui->this_pc_label = localization.GetString(L"Location.ThisPC");
        ui->address_name = localization.GetString(L"AddressBar.Name");
        ui->state.Present(0, ui->this_pc_label);
        ui->breadcrumbs = { { ui->this_pc_label, Location::ThisPC() } };

        ui->host = ContentControl();
        ui->host.HorizontalAlignment(HorizontalAlignment::Stretch);
        ui->host.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        ui->host.VerticalAlignment(VerticalAlignment::Center);
        ui->host.VerticalContentAlignment(VerticalAlignment::Stretch);
        ui->host.Margin(Thickness{ 6, 0, 8, 0 });
        ui->host.Height(32);
        ui->host.MinWidth(0);
        ui->host.IsTabStop(false);
        Automation::AutomationProperties::SetAutomationId(ui->host, L"AddressBar");
        Automation::AutomationProperties::SetName(ui->host, hstring(ui->address_name));
        Automation::AutomationProperties::SetAccessibilityView(ui->host, Automation::Peers::AccessibilityView::Content);

        ui->row = Grid();
        ui->row.Background(Media::SolidColorBrush(Windows::UI::Colors::Transparent()));
        ColumnDefinition address_column;
        address_column.Width(GridLength{ 1, GridUnitType::Star });
        address_column.MinWidth(0);
        ui->row.ColumnDefinitions().Append(address_column);
        ColumnDefinition history_column;
        history_column.Width(GridLength{ 32, GridUnitType::Pixel });
        ui->row.ColumnDefinitions().Append(history_column);
        ui->host.Content(ui->row);

        ui->breadcrumb_host = Grid();
        ui->row.Children().Append(ui->breadcrumb_host);
        ui->editor = TextBox();
        ui->editor.PlaceholderText(hstring(localization.GetString(L"Toolbar.PathPlaceholder")));
        ui->editor.HorizontalAlignment(HorizontalAlignment::Stretch);
        ui->editor.VerticalAlignment(VerticalAlignment::Center);
        ui->editor.Height(32);
        ui->editor.MinWidth(0);
        ui->editor.Visibility(Visibility::Collapsed);
        Automation::AutomationProperties::SetAutomationId(ui->editor, L"AddressEditor");
        Automation::AutomationProperties::SetName(ui->editor,
            hstring(localization.GetString(L"AddressBar.Edit")));
        ui->row.Children().Append(ui->editor);

        ui->suggestion_list = ListView();
        ui->suggestion_list.IsTabStop(false);
        ui->suggestion_list.AllowFocusOnInteraction(false);
        ui->suggestion_list.IsItemClickEnabled(true);
        ui->suggestion_list.SelectionMode(ListViewSelectionMode::Single);
        ui->suggestion_list.MaxHeight(288);
        Automation::AutomationProperties::SetAutomationId(ui->suggestion_list, L"AddressSuggestions");
        Automation::AutomationProperties::SetName(ui->suggestion_list, hstring(localization.GetString(L"AddressBar.Suggestions")));
        ui->suggestion_presenter = FlyoutPresenter();
        ui->suggestion_presenter.IsTabStop(false);
        ui->suggestion_presenter.AllowFocusOnInteraction(false);
        ui->suggestion_presenter.Padding(Thickness{ 4, 4, 4, 4 });
        ui->suggestion_presenter.CornerRadius(CornerRadius{ 8, 8, 8, 8 });
        ui->suggestion_presenter.Content(ui->suggestion_list);
        ui->suggestion_popup = Primitives::Popup();
        ui->suggestion_popup.IsLightDismissEnabled(false);
        ui->suggestion_popup.PlacementTarget(ui->editor);
        ui->suggestion_popup.DesiredPlacement(Primitives::PopupPlacementMode::BottomEdgeAlignedLeft);
        ui->suggestion_popup.Child(ui->suggestion_presenter);
        ui->row.Children().Append(ui->suggestion_popup);
        ui->suggestion_timer = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        ui->suggestion_timer.IsRepeating(false);
        ui->suggestion_timer.Interval(std::chrono::milliseconds(180));

        ui->history_button = Button();
        ui->history_button.Width(32);
        ui->history_button.Height(32);
        ui->history_button.Padding(Thickness{ 0, 0, 0, 0 });
        ui->history_button.VerticalAlignment(VerticalAlignment::Center);
        FontIcon history_icon;
        history_icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
        history_icon.FontSize(12);
        history_icon.Glyph(L"\xE70D");
        ui->history_button.Content(history_icon);
        const auto history_label = localization.GetString(L"AddressBar.History");
        Automation::AutomationProperties::SetAutomationId(ui->history_button, L"AddressHistoryButton");
        Automation::AutomationProperties::SetName(ui->history_button, hstring(history_label));
        ToolTipService::SetToolTip(ui->history_button, box_value(hstring(history_label + L" (F4)")));
        Grid::SetColumn(ui->history_button, 1);
        ui->row.Children().Append(ui->history_button);

        const std::weak_ptr<Ui> weak = ui;
        ui->suggestion_timer.Tick([weak](auto&&, auto&&) {
            if (const auto live = weak.lock()) { live->RequestSuggestions(); }
        });
        ui->suggestion_list.ItemClick([weak](auto&&, const ItemClickEventArgs& args) {
            const auto live = weak.lock();
            if (!live) { return; }
            const auto clicked = args.ClickedItem();
            // Own-container ListViewItems can report their Content as the
            // clicked data item. Resolve either identity in the current list;
            // an unloaded peer from an older draft matches neither value.
            for (std::uint32_t index = 0; index < live->suggestion_list.Items().Size(); ++index)
            {
                const auto item = live->suggestion_list.Items().GetAt(index).as<ListViewItem>();
                if (clicked == item || clicked == item.Content())
                { live->AcceptSuggestion(index, true); return; }
            }
        });
        ui->editor.LostFocus([weak](auto&&, auto&&) {
            const auto live = weak.lock();
            if (!live || live->retired) { return; }
            (void)live->host.DispatcherQueue().TryEnqueue([weak] {
                if (const auto live = weak.lock(); live && !live->retired && !live->HasEditorFocus())
                { live->HideSuggestions(); }
            });
        });
        ui->host.SizeChanged([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && live->suggestion_popup.IsOpen()) { live->SizeSuggestions(); }
        });
        ui->history_button.Click([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->retired) { live->OpenHistory(); }
        });
        ui->editor.TextChanged([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->retired && live->state.IsEditing())
            {
                const auto text = live->editor.Text();
                const std::wstring draft(text);
                const bool changed = draft != live->observed_text;
                live->observed_text = draft;
                live->state.SetDraft(draft);
                live->SyncHelpText();
                if (changed) { live->QueueSuggestions(); }
            }
        });
        // These are native TextBox IME events in the pinned WinUI metadata.
        ui->editor.TextCompositionStarted([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->retired)
            { live->composing = true; live->HideSuggestions(); }
        });
        ui->editor.TextCompositionEnded([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->retired)
            { live->composing = false; live->QueueSuggestions(); }
        });
        // Tab and arrow navigation must be intercepted before TextBox's
        // own caret/focus handling, while composing text keeps native input.
        ui->editor.PreviewKeyDown([weak](auto&&, const Input::KeyRoutedEventArgs& args) {
            const auto live = weak.lock();
            if (!live || live->retired || live->composing) { return; }
            const auto is_down = [](const Windows::System::VirtualKey key) {
                return (Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(key) &
                    Windows::UI::Core::CoreVirtualKeyStates::Down) == Windows::UI::Core::CoreVirtualKeyStates::Down;
            };
            const bool plain = !is_down(Windows::System::VirtualKey::Control) &&
                !is_down(Windows::System::VirtualKey::Shift) && !is_down(Windows::System::VirtualKey::Menu);
            if (plain && (args.Key() == Windows::System::VirtualKey::Down || args.Key() == Windows::System::VirtualKey::Up))
            {
                const int direction = args.Key() == Windows::System::VirtualKey::Down ? 1 : -1;
                if (!live->suggestion_popup.IsOpen())
                {
                    if ((live->suggestion_pending || live->suggestion_inflight) &&
                        live->pending_suggestion_owner == live->state.Owner() &&
                        live->pending_suggestion_request == live->suggestion_revision &&
                        live->editor.Text() == hstring(live->pending_suggestion_draft))
                    {
                        live->pending_suggestion_direction = direction;
                        if (live->suggestion_pending) { live->RequestSuggestions(); }
                    }
                    else { live->QueueSuggestions(true, direction); }
                }
                else
                {
                    const auto count = static_cast<int32_t>(live->suggestions.size());
                    const auto selected = live->suggestion_list.SelectedIndex();
                    const auto next = selected < 0 ? (direction > 0 ? 0 : count - 1) :
                        (selected + direction + count) % count;
                    live->suggestion_list.SelectedIndex(next);
                    live->suggestion_list.ScrollIntoView(live->suggestion_list.Items().GetAt(next));
                }
                args.Handled(true);
                return;
            }
            if (plain && args.Key() == Windows::System::VirtualKey::Tab && live->suggestion_popup.IsOpen() && !live->suggestions.empty())
            {
                if (live->editor.Text() != hstring(live->pending_suggestion_draft))
                { live->HideSuggestions(); return; }
                const auto selected = live->suggestion_list.SelectedIndex();
                args.Handled(true);
                live->AcceptSuggestion(selected < 0 ? 0 : static_cast<std::size_t>(selected), false);
                return;
            }
        });
        // Keep Enter/Escape after the native TextBox/IME input handling, as
        // before completion was added. Read the latest Text on this UI turn.
        ui->editor.KeyDown([weak](auto&&, const Input::KeyRoutedEventArgs& args) {
            const auto live = weak.lock();
            if (!live || live->retired || live->composing) { return; }
            if (args.Key() == Windows::System::VirtualKey::Enter)
            {
                const auto selected = live->suggestion_list.SelectedIndex();
                if (live->suggestion_popup.IsOpen() && selected >= 0 &&
                    live->editor.Text() == hstring(live->pending_suggestion_draft))
                {
                    args.Handled(true);
                    live->AcceptSuggestion(static_cast<std::size_t>(selected), true);
                    return;
                }
                // Read Text synchronously; queued TextChanged notifications
                // must not make the last typed character disappear on Enter.
                const auto text = live->editor.Text();
                auto draft = std::wstring(text.c_str(), text.size());
                live->state.SetDraft(draft);
                live->SyncHelpText();
                const auto submit = live->callbacks.submit;
                live->suggestions_armed = false;
                live->HideSuggestions();
                args.Handled(true);
                if (submit) { submit(std::move(draft)); }
            }
            else if (args.Key() == Windows::System::VirtualKey::Escape)
            {
                live->CancelEditing();
                const auto cancel = live->callbacks.cancel;
                args.Handled(true);
                if (cancel) { cancel(); }
            }
        });
        ui->host.Tapped([weak](auto&&, const Input::TappedRoutedEventArgs& args) {
            const auto live = weak.lock();
            if (!live || live->retired || !live->enabled || live->state.IsEditing()) { return; }
            auto source = args.OriginalSource().try_as<DependencyObject>();
            while (source && source != live->host)
            {
                if (source.try_as<BreadcrumbBarItem>() || source.try_as<Button>() || source.try_as<TextBox>())
                { return; }
                source = Media::VisualTreeHelper::GetParent(source);
            }
            live->BeginEditing(true);
            args.Handled(true);
        });

        ui->RebuildBreadcrumbs();
        ui->SyncVisuals();
        return ui->host;
    }

    TextBox AddressBarPresentation::Editor() const noexcept
    { return ui_ ? ui_->editor : nullptr; }

    void AddressBarPresentation::Present(const std::uint64_t owner, const Location& committed,
        std::vector<AddressBreadcrumb> breadcrumbs, std::vector<Location> history, const bool preserve_draft)
    {
        if (!ui_ || ui_->retired) { return; }
        // TextChanged may still be queued when a directory refresh arrives.
        // Only this same editor owner may supply the draft being preserved.
        if (preserve_draft && ui_->state.Owner() == owner && ui_->state.IsEditing())
        {
            const auto text = ui_->editor.Text();
            ui_->state.SetDraft(std::wstring(text.c_str(), text.size()));
        }
        const bool keep_children = preserve_draft && ui_->children_open && ui_->state.Owner() == owner &&
            ui_->state.CommittedText() == ui_->TextFor(committed);
        const bool keep_suggestions = preserve_draft && ui_->state.IsEditing() && ui_->state.Owner() == owner &&
            ui_->committed_location == committed;
        if (!keep_suggestions) { ui_->HideSuggestions(); }
        ui_->HideHistory();
        if (!keep_children) { ui_->HideChildren(); }
        ui_->state.Present(owner, ui_->TextFor(committed), preserve_draft);
        ui_->committed_location = committed;
        if (!ui_->state.IsEditing()) { ui_->composing = false; ui_->suggestions_armed = false; }
        ui_->breadcrumbs = std::move(breadcrumbs);
        ui_->history = std::move(history);
        if (!keep_children) { ui_->RebuildBreadcrumbs(); }
        ui_->SyncVisuals();
    }

    void AddressBarPresentation::BeginEdit(const bool select_all)
    { if (ui_) { ui_->BeginEditing(select_all); } }

    bool AddressBarPresentation::IsComposing() const noexcept
    { return ui_ && !ui_->retired && ui_->composing; }

    void AddressBarPresentation::CancelEdit()
    { if (ui_ && !ui_->retired) { ui_->CancelEditing(); } }

    void AddressBarPresentation::OpenHistory()
    { if (ui_) { ui_->OpenHistory(); } }

    void AddressBarPresentation::CompleteChildren(const std::uint64_t owner, const std::uint64_t request,
        const Location& parent, std::vector<AddressBreadcrumb> children, std::wstring error)
    { if (ui_) { ui_->CompleteChildren(owner, request, parent, std::move(children), error); } }

    void AddressBarPresentation::CompleteSuggestions(const std::uint64_t owner, const std::uint64_t request,
        const std::wstring& draft, std::vector<AddressBreadcrumb> suggestions)
    { if (ui_) { ui_->CompleteSuggestions(owner, request, draft, std::move(suggestions)); } }

    void AddressBarPresentation::HideSuggestions() noexcept
    { if (ui_) { ui_->HideSuggestions(); } }

    void AddressBarPresentation::SetActive(const bool active)
    {
        if (!ui_ || ui_->retired) { return; }
        ui_->active = active;
        if (!active) { ui_->HideSuggestions(); }
    }

    void AddressBarPresentation::HideMenus() noexcept
    { if (ui_ && !ui_->retired) { ui_->HideMenus(); } }

    void AddressBarPresentation::PrepareForClose()
    {
        if (!ui_ || ui_->retired) { return; }
        ui_->composing = false;
        ui_->state.Cancel();
        ui_->SyncVisuals();
        ui_->HideHistory();
        ui_->HideChildren();
        ui_->suggestions_armed = false;
        ui_->HideSuggestions();
    }

    void AddressBarPresentation::Close() noexcept
    { if (ui_) { ui_->Retire(); } }

    void AddressBarPresentation::SetEnabled(const bool enabled)
    {
        if (!ui_ || ui_->retired) { return; }
        if (!enabled) { ui_->HideMenus(); }
        ui_->enabled = enabled;
        ui_->host.IsEnabled(enabled);
    }
}
