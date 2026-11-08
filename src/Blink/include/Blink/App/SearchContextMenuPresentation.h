#pragma once

#include "Blink/App/Localization.h"
#include "Blink/App/SearchContextCommands.h"
#include "Blink/App/ShellContextMenu.h"
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>
#include <functional>
#include <memory>

namespace blink::app
{
    class SearchContextMenuPresentation
    {
    public:
        struct Callbacks
        {
            std::function<void(SearchContextSelection, ShellMenuCommand)> command;
            std::function<void(SearchContextSelection)> properties;
            std::function<void(SearchContextSelection, winrt::Windows::Foundation::Point)> system_options;
            std::function<void()> closed;
        };

        SearchContextMenuPresentation() = default;
        ~SearchContextMenuPresentation() noexcept;
        SearchContextMenuPresentation(const SearchContextMenuPresentation&) = delete;
        SearchContextMenuPresentation& operator=(const SearchContextMenuPresentation&) = delete;

        // The incoming point and system_options callback use logical client
        // coordinates relative to the XamlRoot, independent of anchor offset.
        [[nodiscard]] bool Show(const winrt::Microsoft::UI::Xaml::FrameworkElement& anchor,
            winrt::Windows::Foundation::Point client_position, SearchContextSelection selection,
            const Localization& localization, bool mutation_busy, bool can_paste, Callbacks callbacks);
        // External hide never invokes Callbacks::closed.
        void Hide() noexcept;

    private:
        struct Ui;
        std::shared_ptr<Ui> ui_;
    };
}
