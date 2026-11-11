#include "Blink/App/AddressBarPresentation.h"

#include "Blink/App/ContextMenuAppearance.h"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <utility>

namespace blink::app
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;

    struct AddressBarPresentation::Ui : std::enable_shared_from_this<Ui>
    {
        ContentControl host{ nullptr };
        Grid row{ nullptr };
        Grid breadcrumb_host{ nullptr };
        BreadcrumbBar breadcrumb_bar{ nullptr };
        TextBox editor{ nullptr };
        Button history_button{ nullptr };
        MenuFlyout history_menu{ nullptr };
        AddressBarState state;
        std::vector<AddressBreadcrumb> breadcrumbs;
        std::vector<Location> history;
        std::wstring this_pc_label;
        std::wstring address_name;
        Callbacks callbacks;
        std::uint64_t breadcrumb_revision{};
        std::uint64_t history_revision{};
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
            if (editor.Text() != display_text) { editor.Text(display_text); }
            editor.Visibility(state.IsEditing() ? Visibility::Visible : Visibility::Collapsed);
            breadcrumb_host.Visibility(state.IsEditing() ? Visibility::Collapsed : Visibility::Visible);
            SyncHelpText();
        }

        void HideHistory() noexcept
        {
            // A normal Closed event must not retire a click that follows it.
            // Explicit hiding and snapshot changes do retire old menu peers.
            ++history_revision;
            try { if (history_menu) { history_menu.Hide(); } } catch (...) {}
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

            // Each rendering owns distinct boxed labels. A native overflow
            // item reports that same content object, never a mutable index.
            auto items = single_threaded_observable_vector<Windows::Foundation::IInspectable>();
            std::vector<std::pair<Windows::Foundation::IInspectable, Location>> targets;
            targets.reserve(breadcrumbs.size());
            for (const auto& breadcrumb : breadcrumbs)
            {
                auto item = box_value(hstring(breadcrumb.label));
                items.Append(item);
                targets.emplace_back(std::move(item), breadcrumb.location);
            }
            bar.ItemsSource(items);
            const auto owner = state.Owner();
            const std::weak_ptr<Ui> weak = shared_from_this();
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
            SyncVisuals();
            editor.Focus(FocusState::Programmatic);
            if (select_all) { editor.SelectAll(); }
        }

        void CancelEditing()
        {
            HideMenus();
            composing = false;
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
        ui->history_button.Click([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->retired) { live->OpenHistory(); }
        });
        ui->editor.TextChanged([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->retired && live->state.IsEditing())
            {
                const auto text = live->editor.Text();
                live->state.SetDraft(std::wstring(text.c_str(), text.size()));
                live->SyncHelpText();
            }
        });
        // These are native TextBox IME events in the pinned WinUI metadata.
        ui->editor.TextCompositionStarted([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->retired) { live->composing = true; }
        });
        ui->editor.TextCompositionEnded([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->retired) { live->composing = false; }
        });
        ui->editor.KeyDown([weak](auto&&, const Input::KeyRoutedEventArgs& args) {
            const auto live = weak.lock();
            if (!live || live->retired || live->composing) { return; }
            if (args.Key() == Windows::System::VirtualKey::Enter)
            {
                // Read Text synchronously; queued TextChanged notifications
                // must not make the last typed character disappear on Enter.
                const auto text = live->editor.Text();
                auto draft = std::wstring(text.c_str(), text.size());
                live->state.SetDraft(draft);
                live->SyncHelpText();
                const auto submit = live->callbacks.submit;
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
        ui_->HideHistory();
        ui_->state.Present(owner, ui_->TextFor(committed), preserve_draft);
        if (!ui_->state.IsEditing()) { ui_->composing = false; }
        ui_->breadcrumbs = std::move(breadcrumbs);
        ui_->history = std::move(history);
        ui_->RebuildBreadcrumbs();
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

    void AddressBarPresentation::HideMenus() noexcept
    { if (ui_ && !ui_->retired) { ui_->HideMenus(); } }

    void AddressBarPresentation::PrepareForClose()
    {
        if (!ui_ || ui_->retired) { return; }
        ui_->composing = false;
        ui_->state.Cancel();
        ui_->SyncVisuals();
        ui_->HideHistory();
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
