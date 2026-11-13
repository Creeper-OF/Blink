#include "Blink/App/SearchPresentation.h"
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <algorithm>
#include <iterator>

namespace blink::app
{
    namespace
    {
        constexpr double kCutOpacity = 0.45;
        constexpr double kNormalOpacity = 1.0;
    }

    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using PropertySet = Windows::Foundation::Collections::PropertySet;
    struct SearchPresentation::Ui : std::enable_shared_from_this<Ui>
    {
        Grid root{ nullptr };
        ComboBox scope{ nullptr }, mode{ nullptr };
        ComboBoxItem current_folder{ nullptr };
        Expander advanced{ nullptr };
        TextBox selected_volumes{ nullptr }, selected_folder{ nullptr };
        StackPanel volume_options{ nullptr }, folder_options{ nullptr };
        CheckBox show_hidden{ nullptr }, include_subfolders{ nullptr };
        Button use_current_folder{ nullptr };
        Button open{ nullptr }, locate{ nullptr }, retry{ nullptr };
        TextBlock summary{ nullptr }, volumes{ nullptr }, action{ nullptr }, limit{ nullptr }, effective_scope{ nullptr };
        ListView list{ nullptr };
        Windows::Foundation::Collections::IObservableVector<Windows::Foundation::IInspectable> items{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
        SearchPresentationCallbacks callbacks;
        std::wstring folder_label, file_label;
        std::vector<SearchResult> rows;
        std::optional<std::size_t> restore_selection;
        std::size_t next{};
        std::uint64_t generation{}, tab_id{}, data_generation{};
        SearchStage data_stage{ SearchStage::Closed };
        ItemVisibilityPolicy policy;
        bool queued{}, syncing{}, closed{}, active{}, action_pending{}, selection_changed{}, query_incomplete{}, exact_incomplete{}, context_menu_open{};

        static constexpr SearchScope ScopeChoices[]{ SearchScope::FollowLocation, SearchScope::AllLocalVolumes,
            SearchScope::SelectedVolumes, SearchScope::CurrentFolder, SearchScope::SelectedFolder };
        SearchScope SelectedScope() const
        {
            const auto index = scope.SelectedIndex();
            return index >= 0 && static_cast<std::size_t>(index) < std::size(ScopeChoices) ?
                ScopeChoices[index] : SearchScope::FollowLocation;
        }
        void ShowScopeOptions()
        {
            const auto selected = SelectedScope();
            volume_options.Visibility(selected == SearchScope::SelectedVolumes ? Visibility::Visible : Visibility::Collapsed);
            folder_options.Visibility(selected == SearchScope::SelectedFolder ? Visibility::Visible : Visibility::Collapsed);
            include_subfolders.Visibility(selected == SearchScope::CurrentFolder || selected == SearchScope::SelectedFolder ?
                Visibility::Visible : Visibility::Collapsed);
        }
        void OptionsChanged()
        {
            if (closed || !active || syncing || !callbacks.options_changed || scope.SelectedIndex() < 0 || mode.SelectedIndex() < 0) { return; }
            ShowScopeOptions();
            const auto checked = include_subfolders.IsChecked();
            callbacks.options_changed({ SelectedScope(), static_cast<SearchMode>(mode.SelectedIndex()),
                std::wstring(selected_volumes.Text().c_str()), std::wstring(selected_folder.Text().c_str()), checked && checked.Value() });
        }

        std::optional<SearchResult> Selected() const
        {
            const auto index = list.SelectedIndex();
            if (index < 0 || static_cast<std::size_t>(index) >= rows.size()) { return {}; }
            return rows[index];
        }
        bool CanAct() const noexcept
        { return !closed && active && !syncing && !queued && !action_pending; }
        std::optional<SearchContextSelection> SelectionContext() const
        {
            if (!CanAct()) { return {}; }
            if (const auto selected = Selected())
            { return SearchContextSelection{ tab_id, data_generation, *selected }; }
            return {};
        }
        void DismissContextMenu() noexcept
        {
            if (!context_menu_open) { return; }
            context_menu_open = false;
            if (callbacks.dismiss_context_menu)
            {
                // Close/Suspend also use this path during window teardown.
                try { callbacks.dismiss_context_menu(tab_id, data_generation); } catch (...) {}
            }
        }
        std::optional<std::size_t> RowFromElement(const Windows::Foundation::IInspectable& source) const
        {
            auto element = source.try_as<FrameworkElement>();
            while (element && element != list)
            {
                const auto data = element.DataContext().try_as<PropertySet>();
                if (data && data.HasKey(L"SearchIndex"))
                {
                    if (!data.HasKey(L"SearchTabId") || !data.HasKey(L"SearchViewGeneration") ||
                        unbox_value<std::uint64_t>(data.Lookup(L"SearchTabId")) != tab_id ||
                        unbox_value<std::uint64_t>(data.Lookup(L"SearchViewGeneration")) != generation) { return {}; }
                    const auto index = unbox_value<std::uint32_t>(data.Lookup(L"SearchIndex"));
                    return index < rows.size() && index < next ? std::optional<std::size_t>(index) : std::nullopt;
                }
                element = Media::VisualTreeHelper::GetParent(element).try_as<FrameworkElement>();
            }
            return {};
        }
        void ShowContextMenu(const std::optional<std::size_t> row, const Windows::Foundation::Point client_position)
        {
            if (!CanAct()) { return; }
            const auto epoch = generation;
            DismissContextMenu();
            if (generation != epoch || !CanAct()) { return; }
            // A background request clears the selection instead of borrowing
            // the last hit, which may belong to a different directory.
            list.SelectedIndex(row ? static_cast<int>(*row) : -1);
            if (!row || generation != epoch || !CanAct() || !callbacks.context_menu) { return; }
            if (const auto selected = SelectionContext())
            {
                context_menu_open = true;
                callbacks.context_menu(*selected, client_position);
            }
        }
        void ShowKeyboardContextMenu()
        {
            if (!CanAct()) { return; }
            const auto index = list.SelectedIndex();
            if (index < 0 || static_cast<std::size_t>(index) >= rows.size()) { DismissContextMenu(); return; }
            auto position = list.TransformToVisual(nullptr).TransformPoint({ 16, 16 });
            if (const auto container = list.ContainerFromIndex(index).try_as<FrameworkElement>())
            {
                position = container.TransformToVisual(nullptr).TransformPoint(
                    { 8, static_cast<float>(container.ActualHeight()) });
            }
            ShowContextMenu(static_cast<std::size_t>(index), position);
        }
        void ExecuteCommand(const FolderCommand command)
        {
            if (const auto selected = SelectionContext(); selected && callbacks.command)
            {
                const auto epoch = generation;
                DismissContextMenu();
                if (generation == epoch && CanAct() && callbacks.command) { callbacks.command(*selected, command); }
            }
        }
        void Buttons()
        {
            const auto index = list.SelectedIndex();
            const auto enabled = index >= 0 && static_cast<std::size_t>(index) < rows.size() && CanAct();
            open.IsEnabled(enabled); locate.IsEnabled(enabled);
        }
        void Activate(bool locating)
        {
            if (const auto selected = Selected(); selected && CanAct() && callbacks.activate)
            {
                const auto epoch = generation;
                DismissContextMenu();
                if (generation == epoch && CanAct() && callbacks.activate) { callbacks.activate(*selected, locating); }
            }
        }
        double CutOpacityFor(const SearchResult& result) const
        {
            return callbacks.is_cut && callbacks.is_cut(result) ? kCutOpacity : kNormalOpacity;
        }
        void RefreshCutAppearance()
        {
            if (closed || !active) { return; }
            const auto count = (std::min)(next, static_cast<std::size_t>(items.Size()));
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto item = items.GetAt(static_cast<std::uint32_t>(index)).as<PropertySet>();
                const auto opacity = CutOpacityFor(rows[index]);
                if (!item.HasKey(L"CutOpacity") || unbox_value<double>(item.Lookup(L"CutOpacity")) != opacity)
                { item.Insert(L"CutOpacity", box_value(opacity)); }
            }
        }
        void Append()
        {
            if (closed || !active) { return; }
            const auto end = (std::min)(next + 128, rows.size());
            syncing = true;
            for (; next < end; ++next)
            {
                const auto& result = rows[next];
                PropertySet item;
                item.Insert(L"Name", box_value(hstring(result.hit.name)));
                item.Insert(L"FullPath", box_value(hstring(result.hit.full_path)));
                item.Insert(L"Volume", box_value(hstring(result.source_volume + L" · " +
                    (result.hit.is_directory ? folder_label : file_label))));
                item.Insert(L"SearchIndex", box_value(static_cast<std::uint32_t>(next)));
                item.Insert(L"SearchTabId", box_value(tab_id));
                item.Insert(L"SearchViewGeneration", box_value(generation));
                item.Insert(L"CutOpacity", box_value(CutOpacityFor(result)));
                items.Append(item);
                if (!selection_changed && restore_selection == next) { list.SelectedIndex(static_cast<int>(next)); }
            }
            syncing = false; Buttons();
            if (next == rows.size()) { return; }
            const auto epoch = generation;
            const std::weak_ptr<Ui> weak = shared_from_this();
            (void)dispatcher.TryEnqueue(Microsoft::UI::Dispatching::DispatcherQueuePriority::Low, [weak, epoch] {
                if (const auto ui = weak.lock(); ui && !ui->closed && ui->active && ui->generation == epoch) { ui->Append(); }
            });
        }
    };
    namespace
    {
        TextBlock Text(const std::wstring& value)
        { TextBlock text; text.Text(hstring(value)); text.TextWrapping(TextWrapping::Wrap); return text; }
        Button Action(const Localization& localization, const wchar_t* key, const wchar_t* id)
        {
            Button button; button.Content(box_value(hstring(localization.GetString(key))));
            Automation::AutomationProperties::SetAutomationId(button, id);
            return button;
        }
        const wchar_t* VolumeKey(VolumeIndexStage stage)
        {
            switch (stage)
            {
            case VolumeIndexStage::Healthy: return L"Index.Healthy";
            case VolumeIndexStage::CatchingUp: return L"Index.CatchingUp";
            case VolumeIndexStage::Rebuilding: return L"Index.Rebuilding";
            case VolumeIndexStage::RebuildRequired: return L"Index.RebuildRequired";
            case VolumeIndexStage::Corrupted: return L"Index.Corrupted";
            case VolumeIndexStage::Unavailable: return L"Index.Unavailable";
            default: return L"Index.Unconfirmed";
            }
        }
    }
    Grid SearchPresentation::Build(const Localization& localization,
        const Microsoft::UI::Dispatching::DispatcherQueue& dispatcher, SearchPresentationCallbacks callbacks)
    {
        Close(); ui_ = std::make_shared<Ui>(); const auto ui = ui_;
        ui->dispatcher = dispatcher; ui->callbacks = std::move(callbacks);
        ui->folder_label = localization.GetString(L"FileType.Folder");
        ui->file_label = localization.GetString(L"FileType.File");
        ui->root = Grid(); ui->root.RowSpacing(8);
        for (int row = 0; row < 5; ++row)
        {
            RowDefinition definition;
            definition.Height(GridLength{ row == 4 ? 1.0 : 0.0, row == 4 ? GridUnitType::Star : GridUnitType::Auto });
            ui->root.RowDefinitions().Append(definition);
        }
        ui->scope = ComboBox(); ui->scope.HorizontalAlignment(HorizontalAlignment::Stretch);
        ui->scope.Header(box_value(hstring(localization.GetString(L"Search.Scope"))));
        Automation::AutomationProperties::SetAutomationId(ui->scope, L"SearchScope");
        Automation::AutomationProperties::SetName(ui->scope, hstring(localization.GetString(L"Search.Scope")));
        std::size_t scope_index{};
        for (const auto* key : { L"Search.FollowLocation", L"Search.AllVolumes", L"Search.SelectedVolumes",
            L"Search.CurrentFolder", L"Search.SelectedFolder" })
        {
            ComboBoxItem item; item.Content(box_value(hstring(localization.GetString(key))));
            if (scope_index++ == 3) { ui->current_folder = item; }
            ui->scope.Items().Append(item);
        }
        ui->mode = ComboBox(); ui->mode.HorizontalAlignment(HorizontalAlignment::Stretch);
        ui->mode.Header(box_value(hstring(localization.GetString(L"Search.Mode"))));
        Automation::AutomationProperties::SetAutomationId(ui->mode, L"SearchMode");
        Automation::AutomationProperties::SetName(ui->mode, hstring(localization.GetString(L"Search.Mode")));
        for (const auto* key : { L"Search.Substring", L"Search.Prefix", L"Search.Extension", L"Search.Exact" })
        { ui->mode.Items().Append(box_value(hstring(localization.GetString(key)))); }
        auto submit = Action(localization, L"Search.Submit", L"SearchSubmit");
        ui->retry = Action(localization, L"Search.Retry", L"SearchRetry");
        auto leave = Action(localization, L"Search.Leave", L"SearchLeave");
        ui->open = Action(localization, L"Command.Open", L"SearchOpen");
        ui->locate = Action(localization, L"Search.Locate", L"SearchLocate");
        StackPanel actions; actions.Orientation(Orientation::Horizontal); actions.Spacing(8);
        for (auto button : { submit, ui->retry, ui->open, ui->locate, leave }) { actions.Children().Append(button); }
        ScrollViewer commands; commands.Content(actions);
        commands.HorizontalScrollBarVisibility(ScrollBarVisibility::Auto);
        commands.HorizontalScrollMode(ScrollMode::Enabled);
        commands.VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
        commands.VerticalScrollMode(ScrollMode::Disabled);
        StackPanel command_area; command_area.Spacing(6);
        ui->effective_scope = Text(L"");
        Automation::AutomationProperties::SetAutomationId(ui->effective_scope, L"SearchEffectiveScope");
        command_area.Children().Append(ui->effective_scope); command_area.Children().Append(commands);
        ui->advanced = Expander(); ui->advanced.IsExpanded(false);
        ui->advanced.Header(box_value(hstring(localization.GetString(L"Search.Advanced"))));
        ui->advanced.HorizontalAlignment(HorizontalAlignment::Stretch);
        Automation::AutomationProperties::SetAutomationId(ui->advanced, L"SearchAdvanced");
        StackPanel advanced_content; advanced_content.Spacing(10);
        Grid choices; choices.ColumnSpacing(12);
        choices.ColumnDefinitions().Append(ColumnDefinition()); choices.ColumnDefinitions().Append(ColumnDefinition());
        choices.Children().Append(ui->scope); choices.Children().Append(ui->mode); Grid::SetColumn(ui->mode, 1);
        advanced_content.Children().Append(choices);
        ui->volume_options = StackPanel(); ui->volume_options.Spacing(4);
        ui->selected_volumes = TextBox();
        ui->selected_volumes.Header(box_value(hstring(localization.GetString(L"Search.SelectedVolumes"))));
        ui->selected_volumes.PlaceholderText(hstring(localization.GetString(L"Search.SelectedVolumesHint")));
        Automation::AutomationProperties::SetAutomationId(ui->selected_volumes, L"SearchSelectedVolumes");
        ui->volume_options.Children().Append(ui->selected_volumes); advanced_content.Children().Append(ui->volume_options);
        ui->folder_options = StackPanel(); ui->folder_options.Spacing(6);
        ui->selected_folder = TextBox();
        ui->selected_folder.Header(box_value(hstring(localization.GetString(L"Search.SelectedFolder"))));
        ui->selected_folder.PlaceholderText(hstring(localization.GetString(L"Search.FolderHint")));
        Automation::AutomationProperties::SetAutomationId(ui->selected_folder, L"SearchSelectedFolder");
        ui->use_current_folder = Action(localization, L"Search.UseCurrentFolder", L"SearchUseCurrentFolder");
        ui->folder_options.Children().Append(ui->selected_folder); ui->folder_options.Children().Append(ui->use_current_folder);
        advanced_content.Children().Append(ui->folder_options);
        ui->include_subfolders = CheckBox();
        ui->include_subfolders.Content(box_value(hstring(localization.GetString(L"Search.IncludeSubfolders"))));
        Automation::AutomationProperties::SetAutomationId(ui->include_subfolders, L"SearchIncludeSubfolders");
        advanced_content.Children().Append(ui->include_subfolders);
        ui->show_hidden = CheckBox();
        ui->show_hidden.Content(box_value(hstring(localization.GetString(L"View.ShowHiddenItems"))));
        Automation::AutomationProperties::SetAutomationId(ui->show_hidden, L"SearchShowHiddenItems");
        advanced_content.Children().Append(ui->show_hidden);
        advanced_content.Children().Append(Text(localization.GetString(L"Search.AdvancedHint")));
        ui->advanced.Content(advanced_content); command_area.Children().Append(ui->advanced);
        ui->root.Children().Append(command_area);
        ui->summary = Text(L""); ui->root.Children().Append(ui->summary); Grid::SetRow(ui->summary, 1);
        StackPanel messages; messages.Spacing(4);
        ui->action = Text(L""); ui->limit = Text(L"");
        messages.Children().Append(ui->action); messages.Children().Append(ui->limit);
        ui->root.Children().Append(messages); Grid::SetRow(messages, 2);
        Expander volume_details; volume_details.Header(box_value(hstring(localization.GetString(L"Search.VolumeStates"))));
        ui->volumes = Text(L""); ScrollViewer volume_scroll; volume_scroll.MaxHeight(140); volume_scroll.Content(ui->volumes);
        volume_details.Content(volume_scroll); volume_details.HorizontalAlignment(HorizontalAlignment::Stretch);
        ui->root.Children().Append(volume_details); Grid::SetRow(volume_details, 3);
        ui->list = ListView(); ui->list.SelectionMode(ListViewSelectionMode::Single);
        ui->list.IsTabStop(true);
        ui->list.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        ui->list.IsItemClickEnabled(false);
        ui->list.ContainerContentChanging([](auto&&, const ContainerContentChangingEventArgs& args) {
            hstring name;
            if (!args.InRecycleQueue())
            {
                if (auto item = args.Item().try_as<PropertySet>(); item && item.HasKey(L"Name"))
                { name = unbox_value<hstring>(item.Lookup(L"Name")); }
            }
            Automation::AutomationProperties::SetName(args.ItemContainer(), name);
        });
        Automation::AutomationProperties::SetAutomationId(ui->list, L"SearchResults");
        ui->list.ItemTemplate(Markup::XamlReader::Load(LR"(<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"><StackPanel Spacing="3" Margin="4,5" Opacity="{Binding [CutOpacity]}"><TextBlock Text="{Binding Name}" FontSize="15" TextTrimming="CharacterEllipsis"/><TextBlock Text="{Binding FullPath}" FontSize="12" TextWrapping="Wrap" Opacity="0.8"/><TextBlock Text="{Binding Volume}" FontSize="11" Opacity="0.7"/></StackPanel></DataTemplate>)").as<DataTemplate>());
        ui->items = single_threaded_observable_vector<Windows::Foundation::IInspectable>();
        ui->list.ItemsSource(ui->items);
        ui->root.Children().Append(ui->list); Grid::SetRow(ui->list, 4);
        const std::weak_ptr<Ui> weak = ui;
        ui->show_hidden.Checked([weak](auto&&, auto&&) {
            if (auto live = weak.lock(); live && !live->closed && !live->syncing && live->callbacks.visibility_changed)
            { live->callbacks.visibility_changed(true); }
        });
        ui->show_hidden.Unchecked([weak](auto&&, auto&&) {
            if (auto live = weak.lock(); live && !live->closed && !live->syncing && live->callbacks.visibility_changed)
            { live->callbacks.visibility_changed(false); }
        });
        submit.Click([weak](auto&&, auto&&) { if (auto live = weak.lock(); live && !live->closed && live->callbacks.submit) { live->callbacks.submit(); } });
        ui->retry.Click([weak](auto&&, auto&&) { if (auto live = weak.lock(); live && !live->closed && live->callbacks.retry) { live->callbacks.retry(); } });
        leave.Click([weak](auto&&, auto&&) { if (auto live = weak.lock(); live && !live->closed && live->callbacks.leave) { live->callbacks.leave(); } });
        ui->open.Click([weak](auto&&, auto&&) { if (auto live = weak.lock(); live && !live->closed) { live->Activate(false); } });
        ui->locate.Click([weak](auto&&, auto&&) { if (auto live = weak.lock(); live && !live->closed) { live->Activate(true); } });
        const auto options_changed = [weak](auto&&, auto&&) {
            if (auto live = weak.lock(); live && !live->closed) { live->OptionsChanged(); }
        };
        ui->scope.SelectionChanged(options_changed); ui->mode.SelectionChanged(options_changed);
        ui->selected_volumes.TextChanged(options_changed); ui->selected_folder.TextChanged(options_changed);
        ui->include_subfolders.Checked(options_changed); ui->include_subfolders.Unchecked(options_changed);
        ui->use_current_folder.Click([weak](auto&&, auto&&) {
            if (auto live = weak.lock(); live && !live->closed && live->active && live->callbacks.use_current_folder)
            { live->callbacks.use_current_folder(); }
        });
        const auto submit_input = [weak](auto&&, const Input::KeyRoutedEventArgs& args) {
            if (args.Key() != Windows::System::VirtualKey::Enter) { return; }
            if (auto live = weak.lock(); live && !live->closed && live->active && live->callbacks.submit)
            { live->callbacks.submit(); args.Handled(true); }
        };
        ui->selected_volumes.KeyDown(submit_input); ui->selected_folder.KeyDown(submit_input);
        ui->list.SelectionChanged([weak](auto&&, auto&&) {
            if (auto live = weak.lock(); live && !live->closed && live->active && !live->syncing)
            {
                live->DismissContextMenu();
                live->selection_changed = true; live->Buttons();
                if (live->callbacks.selection_changed) { live->callbacks.selection_changed(live->Selected()); }
            }
        });
        ui->list.ContextRequested([weak](auto&&, const Input::ContextRequestedEventArgs& args) {
            const auto live = weak.lock();
            if (!live || live->closed || !live->active) { return; }
            args.Handled(true);
            Windows::Foundation::Point position{};
            if (!args.TryGetPosition(live->list, position)) { live->ShowKeyboardContextMenu(); return; }
            const auto row = live->RowFromElement(args.OriginalSource());
            live->ShowContextMenu(row, live->list.TransformToVisual(nullptr).TransformPoint(position));
        });
        ui->list.ContextCanceled([weak](auto&&, auto&&) {
            if (const auto live = weak.lock()) { live->DismissContextMenu(); }
        });
        ui->list.DoubleTapped([weak](auto&&, const Input::DoubleTappedRoutedEventArgs& args) {
            const auto live = weak.lock(); if (!live || !live->CanAct()) { return; }
            if (const auto index = live->RowFromElement(args.OriginalSource()); index && live->callbacks.activate)
            {
                args.Handled(true);
                const auto result = live->rows[*index];
                const auto epoch = live->generation;
                live->DismissContextMenu();
                if (live->generation == epoch && live->CanAct() && live->callbacks.activate)
                { live->callbacks.activate(result, false); }
            }
        });
        // Handle these before ListView/accelerator processing so Alt+Enter can
        // never fall through to normal activation, and Delete retains Shift.
        ui->list.PreviewKeyDown([weak](auto&&, const Input::KeyRoutedEventArgs& args) {
            const auto live = weak.lock(); if (!live || live->closed || !live->active) { return; }
            using Key = Windows::System::VirtualKey;
            const auto down = [](const Key key) {
                return (Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(key) &
                    Windows::UI::Core::CoreVirtualKeyStates::Down) != Windows::UI::Core::CoreVirtualKeyStates::None;
            };
            const auto control = down(Key::Control), shift = down(Key::Shift), alt = down(Key::Menu);
            const auto key = args.Key();
            if ((!control && !alt && !shift && key == Key::Application) ||
                (!control && !alt && shift && key == Key::F10))
            {
                args.Handled(true); live->ShowKeyboardContextMenu(); return;
            }
            if (key == Key::Enter && !shift)
            {
                if (alt && !control) { args.Handled(true); live->ExecuteCommand(FolderCommand::Properties); }
                else if (!alt) { args.Handled(true); live->Activate(control); }
                return;
            }
            std::optional<FolderCommand> command;
            if (control && !alt && !shift)
            {
                if (key == Key::C) { command = FolderCommand::Copy; }
                else if (key == Key::X) { command = FolderCommand::Cut; }
                else if (key == Key::V) { command = FolderCommand::Paste; }
            }
            else if (!control && !alt)
            {
                if (key == Key::Delete) { command = shift ? FolderCommand::PermanentDelete : FolderCommand::Delete; }
                else if (!shift && key == Key::F2) { command = FolderCommand::Rename; }
            }
            if (command) { args.Handled(true); live->ExecuteCommand(*command); }
        });
        ui->list.KeyDown([weak](auto&&, const Input::KeyRoutedEventArgs& args) {
            if (auto live = weak.lock(); live && !live->closed && live->active)
            {
                if (args.Key() == Windows::System::VirtualKey::Escape && live->callbacks.leave)
                { live->callbacks.leave(); args.Handled(true); }
            }
        });
        return ui->root;
    }

    std::optional<SearchPresentationSettings> SearchPresentation::DraftSettings(const std::uint64_t tab_id) const
    {
        const auto ui = ui_;
        if (!ui || ui->closed || !ui->active || ui->tab_id != tab_id ||
            ui->scope.SelectedIndex() < 0 || ui->mode.SelectedIndex() < 0) { return {}; }
        const auto checked = ui->include_subfolders.IsChecked();
        return SearchPresentationSettings{ ui->SelectedScope(), static_cast<SearchMode>(ui->mode.SelectedIndex()),
            std::wstring(ui->selected_volumes.Text().c_str()), std::wstring(ui->selected_folder.Text().c_str()),
            checked && checked.Value() };
    }

    void SearchPresentation::Update(const SearchSnapshot& snapshot, const SearchPresentationOptions& options, const Localization& localization)
    {
        const auto ui = ui_; if (!ui || ui->closed) { return; }
        ui->active = true;
        ui->syncing = true;
        if (ui->tab_id != options.tab_id) { ui->advanced.IsExpanded(false); }
        ui->current_folder.IsEnabled(options.current_folder_allowed);
        ui->use_current_folder.IsEnabled(options.current_folder_allowed);
        ui->show_hidden.IsChecked(box_value(options.visibility.show_hidden_items).as<Windows::Foundation::IReference<bool>>());
        int scope_index{};
        for (std::size_t index = 0; index < std::size(Ui::ScopeChoices); ++index)
        { if (Ui::ScopeChoices[index] == options.scope) { scope_index = static_cast<int>(index); break; } }
        ui->scope.SelectedIndex(scope_index); ui->mode.SelectedIndex(static_cast<int>(options.mode));
        if (ui->selected_volumes.Text() != options.selected_volumes) { ui->selected_volumes.Text(hstring(options.selected_volumes)); }
        if (ui->selected_folder.Text() != options.selected_folder) { ui->selected_folder.Text(hstring(options.selected_folder)); }
        ui->include_subfolders.IsChecked(box_value(options.include_subfolders).as<Windows::Foundation::IReference<bool>>());
        ui->ShowScopeOptions(); ui->syncing = false;
        ui->effective_scope.Text(hstring(localization.Format(options.scope_draft ? L"Search.ScopeDraftSummary" : L"Search.ScopeSummary",
            options.effective_scope.empty() ? localization.GetString(L"Search.ScopeNotSet") : options.effective_scope)));
        ui->action_pending = options.action_pending;
        const bool changed = ui->tab_id != options.tab_id || ui->data_generation != snapshot.generation ||
            ui->data_stage != snapshot.stage || ui->policy != options.visibility || ui->queued != options.queued;
        if (changed)
        {
            ui->DismissContextMenu();
            ++ui->generation; ui->tab_id = options.tab_id; ui->data_generation = snapshot.generation;
            ui->data_stage = snapshot.stage; ui->policy = options.visibility; ui->queued = options.queued;
            auto data = BuildSearchView(snapshot, options.visibility, options.selection);
            ui->syncing = true; ui->items.Clear(); ui->syncing = false;
            ui->rows = std::move(data.rows); ui->restore_selection = data.selected_index;
            ui->selection_changed = false; ui->next = 0;
            ui->query_incomplete = data.query_incomplete;
            ui->exact_incomplete = data.query_incomplete && snapshot.query.mode == SearchMode::Exact;
            ui->limit.Visibility(data.limit_reached || data.query_incomplete ? Visibility::Visible : Visibility::Collapsed);
            const auto limit_key = data.query_incomplete ?
                (ui->exact_incomplete ? L"Search.ExactLimit" : L"Search.IncompleteLimit") : L"Search.Limit";
            ui->limit.Text(hstring(localization.Format(limit_key,
                std::to_wstring(ui->exact_incomplete ? kSearchCandidateLimit : snapshot.query.limit))));
            if (options.selection && !data.selected_index && ui->callbacks.selection_changed)
            { ui->callbacks.selection_changed({}); }
            ui->Append();
        }
        const auto key = options.queued ? L"Search.Queued" : snapshot.stage == SearchStage::Searching ? L"Search.Searching" :
            ui->query_incomplete ? (ui->exact_incomplete ? L"Search.ExactPartial" : L"Search.IncompletePartial") :
            snapshot.error == ERROR_IO_PENDING ? L"Search.Preparing" :
            snapshot.stage == SearchStage::Partial ? L"Search.Partial" : snapshot.stage == SearchStage::Unavailable ?
            L"Search.Unavailable" : L"Search.Results";
        auto text = localization.Format(key, std::vector<std::wstring>{ snapshot.query.text, std::to_wstring(ui->rows.size()) });
        if (snapshot.error != ERROR_SUCCESS && snapshot.error != ERROR_IO_PENDING && snapshot.error != ERROR_MORE_DATA)
        { text += L"  " + localization.Format(L"Search.ErrorCode", std::to_wstring(snapshot.error)); }
        ui->summary.Text(hstring(text));
        const auto action_key = options.action_pending ? L"Search.Validating" :
            options.action_error == ERROR_FILE_INVALID || options.action_error == ERROR_FILE_NOT_FOUND ||
            options.action_error == ERROR_PATH_NOT_FOUND ? L"Search.ResultChanged" :
            options.action_error == ERROR_INVALID_PARAMETER || options.action_error == ERROR_INVALID_NAME ? L"Search.InvalidQuery" :
            options.action_error == ERROR_NOT_SUPPORTED ? L"Search.Unsupported" : L"Search.ActionError";
        ui->action.Visibility(options.action_pending || options.action_error != ERROR_SUCCESS ? Visibility::Visible : Visibility::Collapsed);
        ui->action.Text(hstring(localization.Format(action_key, std::to_wstring(options.action_error))));
        const auto retryable = std::any_of(snapshot.volumes.begin(), snapshot.volumes.end(),
            [](const auto& volume) { return !volume.searched || volume.query_incomplete; });
        ui->retry.IsEnabled(!options.queued && snapshot.stage != SearchStage::Searching && retryable);
        std::wstring volumes;
        for (const auto& volume : snapshot.volumes)
        {
            if (!volumes.empty()) { volumes += L"\n"; }
            volumes += volume.root + L"  " + localization.GetString(VolumeKey(volume.stage));
            if (volume.error != ERROR_SUCCESS) { volumes += L" (" + std::to_wstring(volume.error) + L")"; }
        }
        ui->volumes.Text(hstring(volumes)); ui->Buttons();
    }
    void SearchPresentation::RefreshCutAppearance()
    {
        if (const auto ui = ui_) { ui->RefreshCutAppearance(); }
    }
    void SearchPresentation::FocusResults()
    {
        if (ui_ && !ui_->closed && ui_->active) { ui_->list.Focus(FocusState::Keyboard); }
    }
    void SearchPresentation::Close() noexcept
    {
        if (ui_) { ui_->closed = true; ui_->active = false; ui_->DismissContextMenu(); ++ui_->generation; ui_->callbacks = {}; ui_.reset(); }
    }
    void SearchPresentation::Suspend() noexcept
    {
        if (ui_) { ui_->active = false; ui_->DismissContextMenu(); ++ui_->generation; ui_->data_stage = SearchStage::Closed; }
    }
}
