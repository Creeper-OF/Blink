#include "Blink/App/IndexServicePresentation.h"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <algorithm>
#include <utility>

namespace blink::app
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    struct IndexServicePresentation::Ui
    {
        Button entry{ nullptr };
        Flyout flyout{ nullptr };
        TextBlock summary{ nullptr };
        TextBlock operation{ nullptr };
        ComboBox roots{ nullptr };
        StackPanel volumes{ nullptr };
        Button retry{ nullptr };
        Button enable{ nullptr };
        Button build_index{ nullptr };
        TextBlock enable_hint{ nullptr };
        TextBlock build_hint{ nullptr };
        RefreshCallback refresh;
        RefreshCallback on_enable;
        RefreshCallback on_build;
        std::wstring selected_root;
        bool syncing{};
        bool closed{};
    };

    namespace
    {
        TextBlock Text(const std::wstring& value)
        {
            TextBlock text;
            text.Text(hstring(value));
            text.TextWrapping(TextWrapping::Wrap);
            return text;
        }
        const wchar_t* VolumeKey(const VolumeIndexStage stage)
        {
            switch (stage)
            {
            case VolumeIndexStage::Healthy: return L"Index.Healthy";
            case VolumeIndexStage::CatchingUp: return L"Index.CatchingUp";
            case VolumeIndexStage::RebuildRequired: return L"Index.RebuildRequired";
            case VolumeIndexStage::Rebuilding: return L"Index.Rebuilding";
            case VolumeIndexStage::Unavailable: return L"Index.Unavailable";
            case VolumeIndexStage::Corrupted: return L"Index.Corrupted";
            default: return L"Index.Unconfirmed";
            }
        }
    }

    bool IndexServicePresentation::CanBuildSelectedIndex(const IndexServiceSnapshot& snapshot) noexcept
    {
        if (snapshot.stage != IndexConnectionStage::Available || snapshot.operation_running || snapshot.selected_root.empty())
        { return false; }
        const auto selected = std::find_if(snapshot.volumes.begin(), snapshot.volumes.end(), [&](const auto& volume) {
            return volume.root == snapshot.selected_root;
        });
        return selected != snapshot.volumes.end() && selected->stage != VolumeIndexStage::Healthy &&
            selected->stage != VolumeIndexStage::CatchingUp && selected->stage != VolumeIndexStage::Rebuilding &&
            selected->error != ERROR_IO_PENDING;
    }

    Button IndexServicePresentation::Build(const Localization& localization, RefreshCallback refresh,
        RefreshCallback enable, RefreshCallback build_index)
    {
        Close();
        ui_ = std::make_shared<Ui>();
        const auto ui = ui_;
        ui->refresh = std::move(refresh);
        ui->on_enable = std::move(enable);
        ui->on_build = std::move(build_index);
        ui->entry = Button();
        ui->entry.Padding(Thickness{8, 2, 8, 2});
        ui->entry.VerticalAlignment(VerticalAlignment::Center);
        ui->entry.HorizontalAlignment(HorizontalAlignment::Right);
        Automation::AutomationProperties::SetAutomationId(ui->entry, L"IndexServiceStatus");
        Automation::AutomationProperties::SetName(ui->entry,
            hstring(localization.GetString(L"Index.PanelTitle")));
        ui->flyout = Flyout();
        StackPanel content;
        content.Width(360);
        content.Spacing(10);
        auto heading = Text(localization.GetString(L"Index.PanelTitle"));
        heading.FontSize(18);
        content.Children().Append(heading);
        ui->summary = Text(L"");
        content.Children().Append(ui->summary);
        ui->operation = Text(L"");
        content.Children().Append(ui->operation);
        ui->roots = ComboBox();
        ui->roots.HorizontalAlignment(HorizontalAlignment::Stretch);
        ui->roots.Header(box_value(hstring(localization.GetString(L"Index.Volume"))));
        Automation::AutomationProperties::SetAutomationId(ui->roots, L"IndexVolumeRoot");
        content.Children().Append(ui->roots);
        ui->volumes = StackPanel();
        ui->volumes.Spacing(8);
        content.Children().Append(ui->volumes);
        ui->enable_hint = Text(localization.GetString(L"Index.EnableDescription"));
        content.Children().Append(ui->enable_hint);
        ui->enable = Button();
        ui->enable.Content(box_value(hstring(localization.GetString(L"Index.Enable"))));
        Automation::AutomationProperties::SetAutomationId(ui->enable, L"IndexServiceEnable");
        content.Children().Append(ui->enable);
        ui->build_hint = Text(localization.GetString(L"Index.BuildDescription"));
        content.Children().Append(ui->build_hint);
        ui->build_index = Button();
        ui->build_index.Content(box_value(hstring(localization.GetString(L"Index.BuildSelected"))));
        Automation::AutomationProperties::SetAutomationId(ui->build_index, L"IndexVolumeBuild");
        content.Children().Append(ui->build_index);
        ui->retry = Button();
        ui->retry.Content(box_value(hstring(localization.GetString(L"Index.RefreshStatus"))));
        Automation::AutomationProperties::SetAutomationId(ui->retry, L"IndexServiceRetry");
        content.Children().Append(ui->retry);
        ScrollViewer scroll;
        scroll.MaxHeight(420);
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroll.Content(content);
        ui->flyout.Content(scroll);
        ui->entry.Flyout(ui->flyout);
        const std::weak_ptr<Ui> weak_ui = ui;
        ui->retry.Click([weak_ui](auto&&, auto&&) {
            if (auto live = weak_ui.lock(); live && !live->closed && live->refresh)
            { live->refresh(live->selected_root); }
        });
        ui->enable.Click([weak_ui](auto&&, auto&&) {
            if (auto live = weak_ui.lock(); live && !live->closed && live->enable.IsEnabled() && live->on_enable)
            { live->on_enable(live->selected_root); }
        });
        ui->build_index.Click([weak_ui](auto&&, auto&&) {
            if (auto live = weak_ui.lock(); live && !live->closed && live->build_index.IsEnabled() && live->on_build)
            { live->on_build(live->selected_root); }
        });
        ui->roots.SelectionChanged([weak_ui](auto&&, auto&&) {
            const auto live = weak_ui.lock();
            if (!live || live->closed || live->syncing || !live->refresh) { return; }
            const auto index = live->roots.SelectedIndex();
            if (index < 0) { return; }
            const auto root = unbox_value<hstring>(live->roots.Items().GetAt(index));
            if (live->selected_root != root.c_str())
            {
                live->selected_root = root.c_str();
                live->refresh(live->selected_root);
            }
        });
        Update({}, localization);
        return ui->entry;
    }

    void IndexServicePresentation::Update(const IndexServiceSnapshot& snapshot, const Localization& localization)
    {
        const auto ui = ui_;
        if (!ui || ui->closed) { return; }
        const auto key = snapshot.stage == IndexConnectionStage::Refreshing ? L"Index.Checking" :
            snapshot.stage == IndexConnectionStage::Available ? L"Index.Connected" : L"Index.Disconnected";
        ui->entry.Content(box_value(hstring(localization.GetString(key))));
        const auto state_key = snapshot.service_state == blink::index::ipc::ServiceState::Running ? L"Index.ServiceRunning" :
            snapshot.service_state == blink::index::ipc::ServiceState::Starting ? L"Index.ServiceStarting" :
            snapshot.service_state == blink::index::ipc::ServiceState::Stopping ? L"Index.ServiceStopping" :
            snapshot.service_state == blink::index::ipc::ServiceState::Faulted ? L"Index.ServiceFaulted" : L"Index.ServiceStopped";
        auto summary = snapshot.stage == IndexConnectionStage::Available ?
            localization.GetString(state_key) : localization.GetString(key);
        if (snapshot.stage == IndexConnectionStage::Unavailable)
        {
            if (snapshot.error == ERROR_CANCELLED) { summary = localization.GetString(L"Index.EnableCancelled"); }
            else { summary += L"\n" + localization.Format(L"Index.ConnectionError", std::to_wstring(snapshot.error)); }
        }
        ui->summary.Text(hstring(summary));
        ui->operation.Visibility(snapshot.operation_running ? Visibility::Visible : Visibility::Collapsed);
        ui->operation.Text(hstring(localization.Format(L"Index.OperationProgress",
            std::to_wstring(snapshot.operation_records))));
        const auto selected_root = snapshot.stage == IndexConnectionStage::Refreshing &&
            !ui->selected_root.empty() ? ui->selected_root : snapshot.selected_root;
        ui->syncing = true;
        ui->selected_root = selected_root;
        ui->roots.Items().Clear();
        ui->volumes.Children().Clear();
        int selected = -1;
        for (std::size_t index = 0; index < snapshot.volumes.size(); ++index)
        {
            const auto& volume = snapshot.volumes[index];
            ui->roots.Items().Append(box_value(hstring(volume.root)));
            if (volume.root == selected_root) { selected = static_cast<int>(index); }
            auto value = volume.root + L"  " + localization.GetString(VolumeKey(volume.stage));
            if (volume.stage == VolumeIndexStage::Healthy)
            {
                value += L"\n" + localization.Format(L"Index.VolumeCounts",
                    std::vector<std::wstring>{std::to_wstring(volume.files), std::to_wstring(volume.directories)});
            }
            else if (volume.error != ERROR_SUCCESS && volume.error != ERROR_IO_PENDING)
            { value += L"\n" + localization.Format(L"Index.VolumeError", std::to_wstring(volume.error)); }
            ui->volumes.Children().Append(Text(value));
        }
        ui->roots.SelectedIndex(selected);
        ui->syncing = false;
        const bool enabled = snapshot.stage != IndexConnectionStage::Refreshing && snapshot.stage != IndexConnectionStage::Closed;
        ui->retry.IsEnabled(enabled);
        ui->roots.IsEnabled(enabled && !snapshot.volumes.empty());
        const bool unavailable = snapshot.stage == IndexConnectionStage::Unavailable;
        const bool can_build = CanBuildSelectedIndex(snapshot);
        if (snapshot.stage != IndexConnectionStage::Refreshing)
        {
            const auto enable_visibility = unavailable && ui->on_enable ? Visibility::Visible : Visibility::Collapsed;
            ui->enable.Visibility(enable_visibility); ui->enable_hint.Visibility(enable_visibility);
            const auto build_visibility = can_build && ui->on_build ? Visibility::Visible : Visibility::Collapsed;
            ui->build_index.Visibility(build_visibility); ui->build_hint.Visibility(build_visibility);
        }
        ui->enable.IsEnabled(enabled && unavailable && static_cast<bool>(ui->on_enable));
        ui->build_index.IsEnabled(enabled && can_build && static_cast<bool>(ui->on_build));
    }

    void IndexServicePresentation::Close() noexcept
    {
        if (ui_)
        {
            ui_->closed = true;
            ui_->refresh = {};
            ui_->on_enable = {}; ui_->on_build = {};
            if (ui_->flyout) { try { ui_->flyout.Hide(); } catch (...) {} }
            ui_.reset();
        }
    }
}
