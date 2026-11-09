#pragma once

#include "Blink/App/ExplorerCommandBarModel.h"
#include "Blink/App/Localization.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <functional>
#include <memory>
#include <string_view>

namespace blink::app
{
    enum class CommandBarMenu { New, Sort, View };

    // This is a projection of the owning tab/search command state. It never
    // captures filesystem targets, performs I/O or decides command availability.
    class CommandBarPresentation
    {
    public:
        struct Callbacks
        {
            std::function<void(ExplorerCommandBarAction, bool from_overflow)> command;
            std::function<void(CommandBarMenu)> menu_opening;
            std::function<void()> overflow_opening;
            std::function<void()> overflow_closed;
        };

        CommandBarPresentation() = default;
        ~CommandBarPresentation() noexcept;
        CommandBarPresentation(const CommandBarPresentation&) = delete;
        CommandBarPresentation& operator=(const CommandBarPresentation&) = delete;

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::CommandBar Build(
            const Localization& localization, Callbacks callbacks);
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::CommandBar Element() const noexcept;
        // More is the actual CommandBar template button, not a second AppBarButton.
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::AppBarButton Button(
            ExplorerCommandBarAction action) const noexcept;
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::MenuFlyout Flyout(
            CommandBarMenu menu) const noexcept;

        void SetEnabled(ExplorerCommandBarAction action, bool enabled);
        void SetViewHelpText(std::wstring_view text);
        void SetLabel(ExplorerCommandBarAction action, std::wstring_view text);
        // Close visible menus without destroying the owner's command callbacks.
        void HideMenus() noexcept;

    private:
        struct Ui;
        std::shared_ptr<Ui> ui_;
    };
}
