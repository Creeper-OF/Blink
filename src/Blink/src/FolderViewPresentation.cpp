#include "Blink/App/FolderViewPresentation.h"

#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <string>

namespace blink::app
{
    namespace
    {
        std::wstring DetailsTemplate(const DetailsColumns columns)
        {
            std::wstring xaml = LR"XAML(
<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <Grid Height="32" Opacity="{Binding [CutOpacity]}" AutomationProperties.Name="{Binding [Name]}">
    <Grid.ColumnDefinitions>
      <ColumnDefinition Width="*" />
)XAML";
            for (std::size_t index = 1; index < DetailsColumnDefinitions.size(); ++index)
            {
                const auto& column = DetailsColumnDefinitions[index];
                xaml += L"      <ColumnDefinition Width=\"" +
                    std::to_wstring(column.IsVisible(columns) ? column.width : 0) + L"\" />\n";
            }
            xaml += LR"XAML(
    </Grid.ColumnDefinitions>
    <Grid Grid.Column="0" VerticalAlignment="Center">
      <Grid.ColumnDefinitions><ColumnDefinition Width="28" /><ColumnDefinition Width="*" /></Grid.ColumnDefinitions>
      <FontIcon Glyph="{Binding [Icon]}" FontSize="16" />
      <TextBlock Grid.Column="1" AutomationProperties.AutomationId="DetailsCellName" Text="{Binding [Name]}" VerticalAlignment="Center" TextTrimming="CharacterEllipsis" />
    </Grid>
)XAML";
            for (std::size_t index = 1; index < DetailsColumnDefinitions.size(); ++index)
            {
                const auto& column = DetailsColumnDefinitions[index];
                if (column.IsVisible(columns))
                {
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

        constexpr const wchar_t* kGridContainerStyle = LR"XAML(
<Style xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" TargetType="GridViewItem">
  <Setter Property="HorizontalContentAlignment" Value="Stretch" />
  <Setter Property="VerticalContentAlignment" Value="Stretch" />
  <Setter Property="Padding" Value="0" />
</Style>)XAML";
    }

    void FolderViewPresentation::Configure(
        const winrt::Microsoft::UI::Xaml::Controls::ListViewBase& control,
        const FolderViewMode mode, const DetailsColumns columns)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        using winrt::Microsoft::UI::Xaml::Markup::XamlReader;
        const std::size_t index = mode == FolderViewMode::Grid ? 1 : 0;
        if (!panels_[index])
        {
            auto items_panel = XamlReader::Load(
                index == 1 ? kWrapPanel : kStackPanel).as<ItemsPanelTemplate>();
            auto container_style = XamlReader::Load(
                index == 1 ? kGridContainerStyle : kListContainerStyle).as<Style>();
            panels_[index] = std::move(items_panel);
            container_styles_[index] = std::move(container_style);
        }
        control.ItemsPanel(panels_[index]);
        control.ItemContainerStyle(container_styles_[index]);
        if (mode == FolderViewMode::Details)
        {
            SetDetailsColumns(control, columns);
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
        const DetailsColumns columns)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        const auto index = (columns.show_type ? 1u : 0u) |
            (columns.show_size ? 2u : 0u) | (columns.show_modified ? 4u : 0u);
        auto& item_template = details_templates_[index];
        if (!item_template)
        {
            item_template = Markup::XamlReader::Load(winrt::hstring(DetailsTemplate(columns))).as<DataTemplate>();
        }
        if (control.ItemTemplate() != item_template)
        {
            control.ItemTemplate(item_template);
        }
    }
}
