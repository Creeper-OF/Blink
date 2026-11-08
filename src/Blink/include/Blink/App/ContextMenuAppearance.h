#pragma once

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

namespace blink::app
{
    // Keep WinUI's template, theme brushes, shadow and focus behavior. The
    // visible presenter receives the ID because MenuFlyout has no visible peer.
    inline void ApplyContextMenuAppearance(
        const winrt::Microsoft::UI::Xaml::Controls::MenuFlyout& menu,
        const winrt::Microsoft::UI::Xaml::FrameworkElement& anchor,
        const wchar_t* automation_id)
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        Style presenter(xaml_typename<MenuFlyoutPresenter>());
        presenter.Setters().Append(Setter(FrameworkElement::RequestedThemeProperty(), box_value(anchor.ActualTheme())));
        presenter.Setters().Append(Setter(Control::CornerRadiusProperty(), box_value(CornerRadius{ 8, 8, 8, 8 })));
        presenter.Setters().Append(Setter(Control::PaddingProperty(), box_value(Thickness{ 4, 4, 4, 4 })));
        presenter.Setters().Append(Setter(FrameworkElement::MinWidthProperty(), box_value(260.0)));
        presenter.Setters().Append(Setter(FrameworkElement::MaxWidthProperty(), box_value(420.0)));
        if (automation_id && *automation_id)
        {
            presenter.Setters().Append(Setter(Automation::AutomationProperties::AutomationIdProperty(), box_value(hstring(automation_id))));
            Automation::AutomationProperties::SetAutomationId(menu, automation_id);
        }
        menu.MenuFlyoutPresenterStyle(presenter);
    }

    // MenuFlyoutItem also covers ToggleMenuFlyoutItem's inherited item API.
    // Shortcut labels do not register accelerators or change input routing.
    inline void StyleContextMenuItem(
        const winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItemBase& item,
        const wchar_t* glyph = L"", const wchar_t* shortcut = L"")
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;
        if (item.try_as<MenuFlyoutSeparator>()) { return; }
        item.MinHeight(32);
        item.Padding(Thickness{ 10, 6, 10, 6 });

        const auto leaf = item.try_as<MenuFlyoutItem>();
        const auto submenu = item.try_as<MenuFlyoutSubItem>();
        if (glyph && *glyph)
        {
            FontIcon icon;
            icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
            icon.FontSize(16);
            icon.Glyph(glyph);
            if (leaf) { leaf.Icon(icon); }
            else if (submenu) { submenu.Icon(icon); }
        }
        if (leaf && shortcut && *shortcut) { leaf.KeyboardAcceleratorTextOverride(shortcut); }
    }

    inline void StyleContextMenuSubItem(
        const winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutSubItem& item,
        const wchar_t* glyph = L"")
    {
        StyleContextMenuItem(item, glyph);
    }
}
