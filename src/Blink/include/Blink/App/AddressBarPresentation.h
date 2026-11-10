#pragma once

#include "Blink/App/AddressBarModel.h"
#include "Blink/App/Localization.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace blink::app
{
    // Owns only address-row visuals. Locations and history are supplied by the
    // active navigation model; navigation itself remains with the owner.
    class AddressBarPresentation
    {
    public:
        struct Callbacks
        {
            std::function<void(std::wstring)> submit;
            std::function<void(Location)> navigate;
            std::function<void()> cancel;
            std::function<void()> history_opening;
        };

        AddressBarPresentation() = default;
        ~AddressBarPresentation() noexcept;
        AddressBarPresentation(const AddressBarPresentation&) = delete;
        AddressBarPresentation& operator=(const AddressBarPresentation&) = delete;

        [[nodiscard]] winrt::Microsoft::UI::Xaml::FrameworkElement Build(
            const Localization& localization, Callbacks callbacks);
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::TextBox Editor() const noexcept;

        void Present(std::uint64_t owner, const Location& committed,
            std::vector<AddressBreadcrumb> breadcrumbs, std::vector<Location> history,
            bool preserve_draft = false);
        void BeginEdit(bool select_all = true);
        void CancelEdit();
        // history_opening runs before the menu is created, so the owner may
        // synchronously Present a fresh history snapshot from that callback.
        void OpenHistory();
        void HideMenus() noexcept;
        // Retire callbacks when the window closes; never rebuild its visual tree.
        // Finish the native text edit while its window and focus host are live.
        void PrepareForClose();
        void Close() noexcept;
        void SetEnabled(bool enabled);

    private:
        struct Ui;
        std::shared_ptr<Ui> ui_;
    };
}
