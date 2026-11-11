#include "Blink/App/DeleteConfirmationDialog.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace blink::app
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    struct DeleteConfirmationDialog::Ui
    {
        ContentDialog dialog{ nullptr };
        std::function<void()> confirmed;
        std::function<void(HRESULT)> failed;
        DismissedCallback dismissed;
        bool closed{};
    };
    DeleteConfirmationDialog::~DeleteConfirmationDialog() noexcept { Close(); }
    void DeleteConfirmationDialog::SetDismissedCallback(DismissedCallback callback)
    { dismissed_ = std::move(callback); }
    bool DeleteConfirmationDialog::IsOpen() const noexcept { return ui_ && !ui_->closed; }
    bool DeleteConfirmationDialog::Show(const XamlRoot& root, const Localization& localization, const std::vector<std::wstring>& names,
        std::function<void()> confirmed, std::function<void(HRESULT)> failed)
    {
        if (IsOpen() || names.empty()) { return false; }
        auto ui = std::make_shared<Ui>(); ui->confirmed = std::move(confirmed); ui->failed = std::move(failed); ui->dismissed = dismissed_;
        ui->dialog = ContentDialog(); ui->dialog.XamlRoot(root);
        ui->dialog.Title(box_value(hstring(localization.GetString(L"Delete.ConfirmTitle"))));
        ui->dialog.PrimaryButtonText(hstring(localization.GetString(L"Delete.ConfirmPermanent")));
        ui->dialog.CloseButtonText(hstring(localization.GetString(L"Operation.Cancel")));
        ui->dialog.DefaultButton(ContentDialogButton::Close);
        StackPanel content; content.Spacing(8);
        TextBlock description; description.Text(hstring(localization.Format(L"Delete.ConfirmDescription", std::to_wstring(names.size()))));
        description.TextWrapping(TextWrapping::Wrap); content.Children().Append(description);
        std::wstring listing;
        for (std::size_t index = 0; index < names.size() && index < 8; ++index)
        { if (index) { listing += L"\n"; } listing += names[index]; }
        TextBlock items; items.Text(hstring(listing)); items.TextWrapping(TextWrapping::Wrap); items.MaxLines(10);
        items.TextTrimming(TextTrimming::CharacterEllipsis); content.Children().Append(items);
        ui->dialog.Content(content); ui_ = ui; work_ = Await(std::move(ui)); return true;
    }
    Windows::Foundation::IAsyncAction DeleteConfirmationDialog::Await(std::shared_ptr<Ui> ui)
    {
        HRESULT error = S_OK; ContentDialogResult result = ContentDialogResult::None;
        try { result = co_await ui->dialog.ShowAsync(); } catch (...) { error = to_hresult(); }
        if (ui->closed) { co_return; }
        ui->closed = true;
        if (FAILED(error)) { if (ui->failed) { ui->failed(error); } }
        else if (result == ContentDialogResult::Primary && ui->confirmed)
        { try { ui->confirmed(); } catch (...) { if (ui->failed) { ui->failed(to_hresult()); } } }
        if (ui->dismissed) { ui->dismissed(); }
    }
    void DeleteConfirmationDialog::Close() noexcept
    {
        if (!ui_) { return; } ui_->closed = true;
        try { ui_->dialog.Hide(); } catch (...) {}
        ui_.reset();
    }
}
