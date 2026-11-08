#include "Blink/App/PropertiesDialog.h"
#include "Blink/App/PropertiesPresentation.h"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.System.Threading.h>
#include <chrono>
#include <mutex>

namespace blink::app
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    struct PropertiesDialog::Ui
    {
        ContentDialog dialog{ nullptr };
        StackPanel fields{ nullptr };
        TextBlock status{ nullptr };
        Localization localization;
        ItemPropertiesRequest request;
        PropertiesCancellation cancelled{ std::make_shared<std::atomic_bool>() };
        std::function<void(HRESULT)> failed;
        bool closed{};
    };
    PropertiesDialog::~PropertiesDialog() noexcept { Close(); }
    bool PropertiesDialog::IsOpen() const noexcept { return ui_ && !ui_->closed; }
    void PropertiesDialog::Render(const std::shared_ptr<Ui>& ui, const ItemPropertiesSnapshot& value)
    {
        if (ui->closed) { return; }
        ui->fields.Children().Clear();
        for (const auto& [label, value_text] : BuildPropertiesLines(ui->request, value, ui->localization))
        {
            StackPanel field; field.Spacing(2);
            TextBlock name; name.Text(hstring(label)); name.Opacity(0.7); field.Children().Append(name);
            TextBlock text; text.Text(hstring(value_text)); text.TextWrapping(TextWrapping::Wrap);
            text.IsTextSelectionEnabled(true); Automation::AutomationProperties::SetName(text, hstring(label));
            field.Children().Append(text); ui->fields.Children().Append(field);
        }
        ui->status.Text(hstring(PropertiesStatus(value, ui->localization)));
    }
    bool PropertiesDialog::Show(const XamlRoot& root, const Localization& localization, ItemPropertiesRequest request,
        std::function<void(HRESULT)> failed)
    {
        if (IsOpen() || request.entries.empty()) { return false; }
        auto ui = std::make_shared<Ui>(); ui->localization = localization; ui->request = std::move(request); ui->failed = std::move(failed);
        ui->dialog = ContentDialog(); ui->dialog.XamlRoot(root);
        ui->dialog.Title(box_value(hstring(localization.GetString(L"Command.Properties"))));
        ui->dialog.CloseButtonText(hstring(localization.GetString(L"Properties.Close"))); ui->dialog.DefaultButton(ContentDialogButton::Close);
        Automation::AutomationProperties::SetAutomationId(ui->dialog, L"ItemPropertiesDialog");
        StackPanel content; content.Spacing(12); content.MinWidth(320);
        ui->fields = StackPanel(); ui->fields.Spacing(10);
        ScrollViewer scroll; scroll.MaxHeight(440); scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto); scroll.Content(ui->fields);
        content.Children().Append(scroll);
        TextBlock note; note.Text(hstring(localization.GetString(L"Properties.FolderNote"))); note.TextWrapping(TextWrapping::Wrap); content.Children().Append(note);
        ui->status = TextBlock(); ui->status.TextWrapping(TextWrapping::Wrap); content.Children().Append(ui->status); ui->dialog.Content(content);
        ItemPropertiesSnapshot initial; initial.selected_count = ui->request.entries.size();
        for (const auto& entry : ui->request.entries) { if (entry.is_directory) { ++initial.selected_folders; } else { ++initial.selected_files; } }
        Render(ui, initial); ui_ = ui; dialog_work_ = Await(ui);
        const std::weak_ptr<Ui> weak = ui;
        const auto dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        const auto cancelled = ui->cancelled;
        struct Inbox { std::mutex mutex; std::optional<ItemPropertiesSnapshot> latest; bool queued{}; };
        const auto inbox = std::make_shared<Inbox>();
        read_work_ = Windows::System::Threading::ThreadPool::RunAsync([weak, dispatcher, cancelled, inbox, request = ui->request](auto&&) {
            auto last = std::chrono::steady_clock::time_point{};
            bool final_published{};
            const auto publish = [weak, dispatcher, inbox, &last, &final_published](const ItemPropertiesSnapshot& value) {
                const auto now = std::chrono::steady_clock::now();
                if (!value.finished && now - last < std::chrono::milliseconds(150)) { return; }
                last = now;
                {
                    std::scoped_lock lock(inbox->mutex); inbox->latest = value;
                    final_published = value.finished;
                    if (inbox->queued) { return; } inbox->queued = true;
                }
                (void)dispatcher.TryEnqueue([weak, inbox] {
                    std::optional<ItemPropertiesSnapshot> value;
                    { std::scoped_lock lock(inbox->mutex); value = std::move(inbox->latest); inbox->latest.reset(); inbox->queued = false; }
                    if (const auto live = weak.lock(); live && !live->closed && value)
                    { try { Render(live, *value); } catch (...) { live->cancelled->store(true); } }
                });
            };
            const auto result = ReadItemProperties(request, cancelled, publish);
            // Also report exceptions caught before the backend's final callback.
            try { if (!final_published) { publish(result); } } catch (...) {}
        });
        return true;
    }
    Windows::Foundation::IAsyncAction PropertiesDialog::Await(std::shared_ptr<Ui> ui)
    {
        HRESULT error = S_OK;
        try { (void)co_await ui->dialog.ShowAsync(); } catch (...) { error = to_hresult(); }
        if (ui->closed) { co_return; }
        ui->closed = true; ui->cancelled->store(true);
        if (FAILED(error) && ui->failed) { ui->failed(error); }
    }
    void PropertiesDialog::Close() noexcept
    {
        if (!ui_) { return; } ui_->closed = true; ui_->cancelled->store(true);
        try { ui_->dialog.Hide(); } catch (...) {}
        ui_.reset();
    }
}
