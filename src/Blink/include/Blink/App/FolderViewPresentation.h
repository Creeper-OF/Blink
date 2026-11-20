#pragma once

#include "Blink/App/TabState.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <array>

namespace blink::app
{
    struct DetailsColumnDefinition
    {
        SortField field;
        const wchar_t* property;
        const wchar_t* label;
        int width;
        bool DetailsColumns::* shown;

        [[nodiscard]] constexpr bool IsVisible(const DetailsColumns columns) const noexcept
        {
            return !shown || columns.*shown;
        }
    };

    inline constexpr std::array DetailsColumnDefinitions{
        DetailsColumnDefinition{ SortField::Name, L"Name", L"Column.Name", 0, nullptr },
        DetailsColumnDefinition{ SortField::Type, L"Type", L"Column.Type", 140, &DetailsColumns::show_type },
        DetailsColumnDefinition{ SortField::Size, L"Size", L"Column.Size", 110, &DetailsColumns::show_size },
        DetailsColumnDefinition{ SortField::Modified, L"Modified", L"Column.Modified", 180, &DetailsColumns::show_modified },
    };

    // UI-only layouts. All modes consume the same tab-owned row objects;
    // neither templates nor controls own navigation, selection or enumeration.
    // Each row supplies a double CutOpacity (1.0 normally, 0.45 when cut).
    class FolderViewPresentation
    {
    public:
        void Configure(
            const winrt::Microsoft::UI::Xaml::Controls::ListViewBase& control,
            FolderViewMode mode, DetailsColumns columns);
        void SetDetailsColumns(const winrt::Microsoft::UI::Xaml::Controls::ListViewBase& control,
            DetailsColumns columns);

    private:
        std::array<winrt::Microsoft::UI::Xaml::DataTemplate, 8> details_templates_{
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::DataTemplate, 2> compact_templates_{ nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Controls::ItemsPanelTemplate, 2> panels_{ nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Style, 2> container_styles_{ nullptr, nullptr };
    };
}
