#include "Blink/App/CommandBarPresentation.h"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <map>
#include <utility>

namespace blink::app
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;

    struct CommandBarPresentation::Ui
    {
        CommandBar bar{ nullptr };
        std::map<ExplorerCommandBarAction, AppBarButton> buttons;
        std::map<CommandBarMenu, MenuFlyout> menus;
        Callbacks callbacks;
        bool retired{};

        void Hide() noexcept
        {
            for (const auto& entry : menus)
            {
                try { entry.second.Hide(); } catch (...) {}
            }
            try { if (bar) { bar.IsOpen(false); } } catch (...) {}
        }

        void Retire() noexcept
        {
            retired = true;
            callbacks = {};
            Hide();
        }
    };

    CommandBarPresentation::~CommandBarPresentation() noexcept
    {
        if (ui_) { ui_->Retire(); }
    }

    CommandBar CommandBarPresentation::Build(const Localization& localization, Callbacks callbacks)
    {
        if (ui_) { ui_->Retire(); }
        const auto ui = std::make_shared<Ui>();
        ui_ = ui;
        ui->callbacks = std::move(callbacks);
        ui->bar = CommandBar();
        ui->bar.IsDynamicOverflowEnabled(true);
        ui->bar.DefaultLabelPosition(CommandBarDefaultLabelPosition::Right);
        // The standard template right-aligns primary items. Let the bar keep
        // its desired width at the left of the owner's full-width command row;
        // the available parent width still limits it and drives native overflow.
        ui->bar.HorizontalAlignment(HorizontalAlignment::Left);
        ui->bar.VerticalAlignment(VerticalAlignment::Top);
        ui->bar.Padding(Thickness{ 0, 0, 0, 0 });
        Automation::AutomationProperties::SetAutomationId(ui->bar, L"ExplorerCommandBar");
        Automation::AutomationProperties::SetName(ui->bar,
            hstring(localization.GetString(L"CommandBar.Name")));

        Style overflow(xaml_typename<CommandBarOverflowPresenter>());
        overflow.Setters().Append(Setter(Automation::AutomationProperties::AutomationIdProperty(),
            box_value(hstring(L"TopMoreMenu"))));
        ui->bar.CommandBarOverflowPresenterStyle(overflow);

        const std::weak_ptr<Ui> weak = ui;
        ui->bar.Opening([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->retired && live->callbacks.overflow_opening)
            { live->callbacks.overflow_opening(); }
        });
        ui->bar.Closed([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->retired && live->callbacks.overflow_closed)
            { live->callbacks.overflow_closed(); }
        });

        const auto add = [&](const ExplorerCommandBarAction action, const wchar_t* key,
            const wchar_t* id, const wchar_t* glyph, const wchar_t* shortcut,
            const bool secondary, const bool icon_only) {
            AppBarButton button;
            const auto label = hstring(localization.GetString(key));
            button.Label(label);
            Automation::AutomationProperties::SetAutomationId(button, id);
            Automation::AutomationProperties::SetName(button, label);
            if (glyph && *glyph)
            {
                FontIcon icon;
                icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
                icon.Glyph(glyph);
                button.Icon(icon);
            }
            if (icon_only) { button.LabelPosition(CommandBarLabelPosition::Collapsed); }
            // This is a hint for existing bindings, never another accelerator.
            if (shortcut && *shortcut) { button.KeyboardAcceleratorTextOverride(shortcut); }
            ToolTipService::SetToolTip(button, box_value(label));

            // Flyout containers invoke their MenuFlyout.Opening callback below.
            // Only concrete commands publish an action.
            if (action != ExplorerCommandBarAction::NewFolder &&
                action != ExplorerCommandBarAction::Sort && action != ExplorerCommandBarAction::View)
            {
                button.Click([weak, action, secondary](auto&& sender, auto&&) {
                    if (const auto live = weak.lock(); live && !live->retired && live->callbacks.command)
                    {
                        const auto origin = sender.template as<AppBarButton>();
                        live->callbacks.command(action, secondary || origin.IsInOverflow());
                    }
                });
            }
            ui->buttons.emplace(action, button);
            if (secondary) { ui->bar.SecondaryCommands().Append(button); }
            else { ui->bar.PrimaryCommands().Append(button); }
        };

        using Action = ExplorerCommandBarAction;
        add(Action::NewFolder, L"Command.New", L"CommandNew", L"\xE710", L"", false, false);
        ui->bar.PrimaryCommands().Append(AppBarSeparator());
        add(Action::Cut, L"Command.Cut", L"CommandCut", L"\xE8C6", L"Ctrl+X", false, true);
        add(Action::Copy, L"Command.Copy", L"CommandCopy", L"\xE8C8", L"Ctrl+C", false, true);
        add(Action::Paste, L"Command.Paste", L"CommandPaste", L"\xE77F", L"Ctrl+V", false, true);
        add(Action::Rename, L"Command.Rename", L"CommandRename", L"\xE8AC", L"F2", false, true);
        add(Action::Delete, L"Command.Delete", L"CommandDelete", L"\xE74D", L"Delete", false, true);
        ui->bar.PrimaryCommands().Append(AppBarSeparator());
        add(Action::Sort, L"Toolbar.Sort", L"CommandSort", L"\xE8CB", L"", false, false);
        add(Action::View, L"Command.View", L"FolderViewMode", L"\xE8A9", L"", false, false);

        add(Action::SelectAll, L"Command.SelectAll", L"CommandSelectAll", L"\xE8B3", L"Ctrl+A", true, false);
        add(Action::ClearSelection, L"Command.ClearSelection", L"CommandClearSelection", L"\xE8B4", L"", true, false);
        add(Action::InvertSelection, L"Command.InvertSelection", L"CommandInvertSelection", L"\xE8B5", L"", true, false);
        ui->bar.SecondaryCommands().Append(AppBarSeparator());
        add(Action::Properties, L"Command.Properties", L"CommandProperties", L"\xE946", L"Alt+Enter", true, false);
        add(Action::Refresh, L"Toolbar.Refresh", L"CommandRefresh", L"\xE72C", L"F5", true, false);
        add(Action::CopyFullPath, L"Search.MenuCopyPath", L"CommandCopyFullPath", L"\xE71B", L"", true, false);
        add(Action::CopyParentPath, L"Search.MenuCopyParentPath", L"CommandCopyParentPath", L"\xE8B7", L"", true, false);

        for (const auto kind : { CommandBarMenu::New, CommandBarMenu::Sort, CommandBarMenu::View })
        {
            MenuFlyout menu;
            const auto action = kind == CommandBarMenu::New ? Action::NewFolder :
                kind == CommandBarMenu::Sort ? Action::Sort : Action::View;
            const auto id = kind == CommandBarMenu::New ? L"TopNewMenu" :
                kind == CommandBarMenu::Sort ? L"TopSortMenu" : L"TopViewMenu";
            Automation::AutomationProperties::SetAutomationId(menu, id);
            menu.Opening([weak, kind](auto&&, auto&&) {
                if (const auto live = weak.lock(); live && !live->retired && live->callbacks.menu_opening)
                { live->callbacks.menu_opening(kind); }
            });
            ui->menus.emplace(kind, menu);
            ui->buttons.at(action).Flyout(menu);
        }
        return ui->bar;
    }

    CommandBar CommandBarPresentation::Element() const noexcept
    { return ui_ ? ui_->bar : nullptr; }

    AppBarButton CommandBarPresentation::Button(const ExplorerCommandBarAction action) const noexcept
    {
        if (!ui_) { return nullptr; }
        const auto found = ui_->buttons.find(action);
        return found == ui_->buttons.end() ? nullptr : found->second;
    }

    MenuFlyout CommandBarPresentation::Flyout(const CommandBarMenu menu) const noexcept
    {
        if (!ui_) { return nullptr; }
        const auto found = ui_->menus.find(menu);
        return found == ui_->menus.end() ? nullptr : found->second;
    }

    void CommandBarPresentation::SetEnabled(const ExplorerCommandBarAction action, const bool enabled)
    { if (const auto button = Button(action)) { button.IsEnabled(enabled); } }

    void CommandBarPresentation::SetViewHelpText(const std::wstring_view text)
    {
        if (const auto button = Button(ExplorerCommandBarAction::View))
        {
            Automation::AutomationProperties::SetHelpText(button, hstring(text));
            ToolTipService::SetToolTip(button, box_value(hstring(text)));
        }
    }

    void CommandBarPresentation::SetLabel(const ExplorerCommandBarAction action, const std::wstring_view text)
    {
        if (const auto button = Button(action))
        {
            button.Label(hstring(text));
            Automation::AutomationProperties::SetName(button, hstring(text));
            ToolTipService::SetToolTip(button, box_value(hstring(text)));
        }
    }

    void CommandBarPresentation::HideMenus() noexcept
    { if (ui_) { ui_->Hide(); } }
}
