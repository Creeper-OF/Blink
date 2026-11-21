#include "Blink/App/FolderViewPresentation.h"

#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace blink::app
{
    namespace
    {
        std::wstring DetailsTemplate(const DetailsColumnLayout& layout)
        {
            std::wstring xaml = LR"XAML(
<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <Grid Height="32" Opacity="{Binding [CutOpacity]}" AutomationProperties.Name="{Binding [Name]}">
    <Grid.ColumnDefinitions>
)XAML";
            for (const auto width : layout.widths)
            {
                xaml += L"      <ColumnDefinition Width=\"" +
                    (width == DetailsColumnLayout::Stretch ? L"*" : std::to_wstring(width)) + L"\" />\n";
            }
            xaml += LR"XAML(
    </Grid.ColumnDefinitions>
    <Grid Grid.Column="0" VerticalAlignment="Center" Margin="8,0,4,0">
      <Grid.ColumnDefinitions><ColumnDefinition Width="28" /><ColumnDefinition Width="*" /></Grid.ColumnDefinitions>
      <FontIcon Glyph="{Binding [Icon]}" FontSize="16" />
      <TextBlock Grid.Column="1" AutomationProperties.AutomationId="DetailsCellName" Text="{Binding [Name]}" VerticalAlignment="Center" TextTrimming="CharacterEllipsis" />
    </Grid>
)XAML";
            for (std::size_t index = 1; index < layout.fields.size(); ++index)
            {
                if (layout.widths[index] != 0)
                {
                    const auto& column = DetailsColumnDefinitions[DetailsColumnIndex(layout.fields[index])];
                    xaml += L"    <TextBlock Grid.Column=\"" + std::to_wstring(index) +
                        L"\" AutomationProperties.AutomationId=\"DetailsCell" + column.property +
                        L"\" Text=\"{Binding [" + column.property + L"]}\" VerticalAlignment=\"Center\""
                        L" Margin=\"8,0,4,0\" TextTrimming=\"CharacterEllipsis\" />\n";
                }
            }
            xaml += L"  </Grid>\n</DataTemplate>";
            return xaml;
        }

        constexpr const wchar_t* kListTemplate = LR"XAML(
<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <Grid Height="28" Opacity="{Binding [CutOpacity]}" AutomationProperties.Name="{Binding [Name]}">
    <Grid.ColumnDefinitions>
      <ColumnDefinition Width="28" />
      <ColumnDefinition Width="*" />
    </Grid.ColumnDefinitions>
    <FontIcon Glyph="{Binding [Icon]}" FontSize="16" VerticalAlignment="Center" />
    <TextBlock Grid.Column="1" Text="{Binding [Name]}" VerticalAlignment="Center" TextTrimming="CharacterEllipsis" />
    <ToolTipService.ToolTip><ToolTip Content="{Binding [Type]}" /></ToolTipService.ToolTip>
  </Grid>
</DataTemplate>)XAML";

        constexpr const wchar_t* kGridTemplate = LR"XAML(
<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <Grid Width="136" Height="112" Padding="8" Opacity="{Binding [CutOpacity]}" AutomationProperties.Name="{Binding [Name]}">
    <Grid.RowDefinitions>
      <RowDefinition Height="56" />
      <RowDefinition Height="*" />
    </Grid.RowDefinitions>
    <FontIcon Glyph="{Binding [Icon]}" FontSize="36" HorizontalAlignment="Center" VerticalAlignment="Center" />
    <TextBlock Grid.Row="1" Text="{Binding [Name]}" TextAlignment="Center" TextWrapping="Wrap" MaxLines="2" TextTrimming="CharacterEllipsis" />
    <ToolTipService.ToolTip><ToolTip Content="{Binding [Name]}" /></ToolTipService.ToolTip>
  </Grid>
</DataTemplate>)XAML";

        constexpr const wchar_t* kStackPanel = LR"XAML(
<ItemsPanelTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <ItemsStackPanel />
</ItemsPanelTemplate>)XAML";

        constexpr const wchar_t* kWrapPanel = LR"XAML(
<ItemsPanelTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <ItemsWrapGrid Orientation="Horizontal" />
</ItemsPanelTemplate>)XAML";

        constexpr const wchar_t* kListContainerStyle = LR"XAML(
<Style xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="ListViewItem">
  <Setter Property="HorizontalContentAlignment" Value="Stretch" />
  <Setter Property="Padding" Value="8,0" />
  <Setter Property="MinHeight" Value="28" />
</Style>)XAML";

        constexpr const wchar_t* kDetailsContainerStyle = LR"XAML(
<Style xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="ListViewItem">
  <Setter Property="HorizontalContentAlignment" Value="Stretch" />
  <Setter Property="Padding" Value="0" />
  <Setter Property="MinHeight" Value="28" />
</Style>)XAML";

        constexpr const wchar_t* kGridContainerStyle = LR"XAML(
<Style xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="GridViewItem">
  <Setter Property="HorizontalContentAlignment" Value="Stretch" />
  <Setter Property="VerticalContentAlignment" Value="Stretch" />
  <Setter Property="Padding" Value="0" />
</Style>)XAML";
    }

    int MeasureDetailsColumnWidth(const winrt::Microsoft::UI::Xaml::Controls::ListViewBase& control,
        const winrt::Microsoft::UI::Xaml::Controls::Button& header, const SortField field)
    {
        using namespace winrt;
        using namespace Microsoft::UI::Xaml::Controls;
        using Windows::Foundation::Collections::PropertySet;
        const hstring property{ DetailsColumnDefinitions[DetailsColumnIndex(field)].property };
        const auto infinity = std::numeric_limits<float>::infinity();
        TextBlock measure;
        const auto measure_text = [&](const hstring& text) {
            measure.Text(text);
            measure.Measure({ infinity, infinity });
            return static_cast<double>(measure.DesiredSize().Width);
        };

        double width = DetailsColumns::MinimumWidth(field);
        // Measure only the current viewport so fitting stays responsive in large folders.
        if (const auto panel = control.ItemsPanelRoot().try_as<ItemsStackPanel>())
        {
            const auto items = control.Items();
            const auto last = std::min(panel.LastVisibleIndex(), static_cast<std::int32_t>(items.Size()) - 1);
            const auto padding = field == SortField::Name ? 40.0 : 12.0;
            for (auto index = std::max(0, panel.FirstVisibleIndex()); index <= last; ++index)
            {
                if (const auto row = items.GetAt(index).try_as<PropertySet>(); row && row.HasKey(property))
                {
                    width = std::max(width, measure_text(unbox_value<hstring>(row.Lookup(property))) + padding);
                }
            }
        }
        measure.FontFamily(header.FontFamily());
        measure.FontSize(header.FontSize());
        measure.FontWeight(header.FontWeight());
        const auto padding = header.Padding();
        width = std::max(width, measure_text(unbox_value<hstring>(header.Content())) +
            padding.Left + padding.Right + DetailsColumnGripWidth);
        return static_cast<int>(std::min(std::ceil(width), static_cast<double>(DetailsColumns::MaximumWidth)));
    }

    void FolderViewPresentation::Configure(
        const winrt::Microsoft::UI::Xaml::Controls::ListViewBase& control,
        const FolderViewMode mode, const DetailsColumns columns)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        using winrt::Microsoft::UI::Xaml::Markup::XamlReader;
        const std::size_t index = mode == FolderViewMode::Grid ? 1 : 0;
        const std::size_t style_index = mode == FolderViewMode::Details ? 2 : index;
        if (!panels_[index])
        {
            panels_[index] = XamlReader::Load(
                index == 1 ? kWrapPanel : kStackPanel).as<ItemsPanelTemplate>();
        }
        if (!container_styles_[style_index])
        {
            constexpr std::array styles{ kListContainerStyle, kGridContainerStyle, kDetailsContainerStyle };
            container_styles_[style_index] = XamlReader::Load(styles[style_index]).as<Style>();
        }
        control.ItemsPanel(panels_[index]);
        control.ItemContainerStyle(container_styles_[style_index]);
        if (mode == FolderViewMode::Details)
        {
            SetDetailsColumns(control, columns.Layout(0));
        }
        else
        {
            auto& item_template = compact_templates_[index];
            if (!item_template)
            {
                item_template = XamlReader::Load(index == 1 ? kGridTemplate : kListTemplate).as<DataTemplate>();
            }
            control.ItemTemplate(item_template);
        }
        ScrollViewer::SetHorizontalScrollMode(control, ScrollMode::Disabled);
        ScrollViewer::SetHorizontalScrollBarVisibility(control, ScrollBarVisibility::Disabled);
    }

    void FolderViewPresentation::SetDetailsColumns(
        const winrt::Microsoft::UI::Xaml::Controls::ListViewBase& control,
        const DetailsColumnLayout& layout)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace Controls;
        const auto index = (layout.widths[1] ? 1u : 0u) |
            (layout.widths[2] ? 2u : 0u) | (layout.widths[3] ? 4u : 0u);
        auto& cached = details_templates_[index];
        if (!cached.item_template || cached.layout != layout)
        {
            cached.item_template = Markup::XamlReader::Load(winrt::hstring(DetailsTemplate(layout))).as<DataTemplate>();
            cached.layout = layout;
        }
        if (control.ItemTemplate() != cached.item_template)
        {
            control.ItemTemplate(cached.item_template);
        }
        const auto horizontal_mode = layout.scroll_extent ? ScrollMode::Enabled : ScrollMode::Disabled;
        const auto scrollbar = layout.scroll_extent ? ScrollBarVisibility::Auto : ScrollBarVisibility::Disabled;
        if (ScrollViewer::GetHorizontalScrollMode(control) != horizontal_mode)
        {
            ScrollViewer::SetHorizontalScrollMode(control, horizontal_mode);
            ScrollViewer::SetHorizontalScrollBarVisibility(control, scrollbar);
        }
        if (const auto panel = control.ItemsPanelRoot(); panel && panel.MinWidth() != layout.scroll_extent)
        {
            panel.MinWidth(layout.scroll_extent);
        }
    }
}
