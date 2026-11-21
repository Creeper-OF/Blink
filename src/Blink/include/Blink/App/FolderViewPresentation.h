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
        bool DetailsColumns::* shown;

        [[nodiscard]] bool IsVisible(const DetailsColumns& columns) const noexcept
        {
            return columns.IsVisible(field);
        }
    };

    inline constexpr std::array DetailsColumnDefinitions{
        DetailsColumnDefinition{ SortField::Name, L"Name", L"Column.Name", nullptr },
        DetailsColumnDefinition{ SortField::Type, L"Type", L"Column.Type", &DetailsColumns::show_type },
        DetailsColumnDefinition{ SortField::Size, L"Size", L"Column.Size", &DetailsColumns::show_size },
        DetailsColumnDefinition{ SortField::Modified, L"Modified", L"Column.Modified", &DetailsColumns::show_modified },
    };
    inline constexpr double DetailsColumnGripWidth = 8;

    [[nodiscard]] constexpr std::uint32_t DetailsColumnIndex(const SortField field) noexcept
    {
        for (std::uint32_t index = 0; index < DetailsColumnDefinitions.size(); ++index)
        {
            if (DetailsColumnDefinitions[index].field == field) { return index; }
        }
        return static_cast<std::uint32_t>(DetailsColumnDefinitions.size());
    }

    [[nodiscard]] int MeasureDetailsColumnWidth(
        const winrt::Microsoft::UI::Xaml::Controls::ListViewBase& control,
        const winrt::Microsoft::UI::Xaml::Controls::Button& header, SortField field);

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
            const DetailsColumnLayout& layout);

    private:
        struct CachedDetailsTemplate
        {
            DetailsColumnLayout layout{};
            winrt::Microsoft::UI::Xaml::DataTemplate item_template{ nullptr };
        };
        std::array<CachedDetailsTemplate, 8> details_templates_{};
        std::array<winrt::Microsoft::UI::Xaml::DataTemplate, 2> compact_templates_{ nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Controls::ItemsPanelTemplate, 2> panels_{ nullptr, nullptr };
        std::array<winrt::Microsoft::UI::Xaml::Style, 3> container_styles_{ nullptr, nullptr, nullptr };
    };
}
