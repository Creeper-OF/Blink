#include "Blink/App/FolderViewPresentation.h"

#include <winrt/Microsoft.UI.Xaml.Markup.h>

namespace blink::app
{
    namespace
    {
        constexpr const wchar_t* kDetailsTemplate = LR"XAML(
<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
  <Grid Height="32" Opacity="{Binding [CutOpacity]}" AutomationProperties.Name="{Binding [Name]}">
    <Grid.ColumnDefinitions>
      <ColumnDefinition Width="*" />
      <ColumnDefinition Width="140" />
      <ColumnDefinition Width="110" />
      <ColumnDefinition Width="180" />
    </Grid.ColumnDefinitions>
    <Grid Grid.Column="0" VerticalAlignment="Center">
      <Grid.ColumnDefinitions><ColumnDefinition Width="28" /><ColumnDefinition Width="*" /></Grid.ColumnDefinitions>
      <FontIcon Glyph="{Binding [Icon]}" FontSize="16" />
      <TextBlock Grid.Column="1" Text="{Binding [Name]}" VerticalAlignment="Center" TextTrimming="CharacterEllipsis" />
    </Grid>
    <TextBlock Grid.Column="1" Text="{Binding [Type]}" VerticalAlignment="Center" Margin="8,0,4,0" TextTrimming="CharacterEllipsis" />
    <TextBlock Grid.Column="2" Text="{Binding [Size]}" VerticalAlignment="Center" Margin="8,0,4,0" />
    <TextBlock Grid.Column="3" Text="{Binding [Modified]}" VerticalAlignment="Center" Margin="8,0,4,0" />
  </Grid>
</DataTemplate>)XAML";

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
        const FolderViewMode mode)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        using winrt::Microsoft::UI::Xaml::Markup::XamlReader;
        const std::size_t index = mode == FolderViewMode::Grid ? 2 :
            mode == FolderViewMode::List ? 1 : 0;
        if (!templates_[index])
        {
            const auto* text = index == 2 ? kGridTemplate :
                index == 1 ? kListTemplate : kDetailsTemplate;
            auto item_template = XamlReader::Load(text).as<DataTemplate>();
            auto items_panel = XamlReader::Load(
                index == 2 ? kWrapPanel : kStackPanel).as<ItemsPanelTemplate>();
            auto container_style = XamlReader::Load(
                index == 2 ? kGridContainerStyle : kListContainerStyle).as<Style>();
            templates_[index] = std::move(item_template);
            panels_[index] = std::move(items_panel);
            container_styles_[index] = std::move(container_style);
        }
        control.ItemsPanel(panels_[index]);
        control.ItemTemplate(templates_[index]);
        control.ItemContainerStyle(container_styles_[index]);
        ScrollViewer::SetHorizontalScrollMode(control, ScrollMode::Disabled);
        ScrollViewer::SetHorizontalScrollBarVisibility(control, ScrollBarVisibility::Disabled);
    }
}
