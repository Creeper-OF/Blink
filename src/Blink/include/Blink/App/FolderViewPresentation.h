#pragma once

#include "Blink/App/TabState.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <array>

namespace blink::app
{
    // UI-only layouts. All modes consume the same tab-owned row objects;
    // neither templates nor controls own navigation, selection or enumeration.
    // Each row supplies a double CutOpacity (1.0 normally, 0.45 when cut).
    class FolderViewPresentation
    {
    public:
        void Configure(
            const winrt::Microsoft::UI::Xaml::Controls::ListViewBase& control,
            FolderViewMode mode);

    private:
        std::array<winrt::Microsoft::UI::Xaml::DataTemplate, 3> templates_{
            nullptr, nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Controls::ItemsPanelTemplate, 3> panels_{
            nullptr, nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Style, 3> container_styles_{
            nullptr, nullptr, nullptr };
    };
}
