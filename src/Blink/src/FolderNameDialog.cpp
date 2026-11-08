#include "Blink/App/FolderNameDialog.h"
#include "Blink/App/FileOperations.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace blink::app
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    struct FolderNameDialog::Ui
    {
        ContentDialog dialog{ nullptr };
        TextBox name{ nullptr };
        TextBlock error{ nullptr };
        std::wstring invalid_name;
        SubmitCallback submit;
        FailureCallback failed;
        bool closed{};
    };
    FolderNameDialog::~FolderNameDialog() noexcept { Close(); }
    bool FolderNameDialog::IsOpen() const noexcept { return ui_ && !ui_->closed; }
    bool FolderNameDialog::Show(const XamlRoot& root, const Localization& localization,
        std::wstring parent, std::wstring name, std::wstring error,
        SubmitCallback submit, FailureCallback failed, bool renaming, bool copying, bool directory_copy, bool moving)
    {
        if (IsOpen()) { return false; }
        auto ui = std::make_shared<Ui>();
        ui->submit = std::move(submit); ui->failed = std::move(failed);
        ui->invalid_name = localization.GetString(L"NewFolder.InvalidName");
        ui->dialog = ContentDialog(); ui->dialog.XamlRoot(root);
        ui->dialog.Title(box_value(hstring(localization.GetString(moving ? L"Move.Title" : copying ? L"Command.Copy" : renaming ? L"Command.Rename" : L"Command.NewFolder"))));
        ui->dialog.PrimaryButtonText(hstring(localization.GetString(moving ? L"Move.Title" : copying ? L"Command.Copy" : renaming ? L"Command.Rename" : L"NewFolder.Create")));
        ui->dialog.CloseButtonText(hstring(localization.GetString(L"Operation.Cancel")));
        ui->dialog.DefaultButton(ContentDialogButton::Primary);
        StackPanel content; content.Spacing(8);
        TextBlock folder; folder.Text(hstring(parent)); folder.TextWrapping(TextWrapping::Wrap);
        folder.MaxLines(3); folder.TextTrimming(TextTrimming::CharacterEllipsis);
        ToolTipService::SetToolTip(folder, box_value(hstring(parent)));
        content.Children().Append(folder);
        ui->name = TextBox(); ui->name.Text(hstring(name));
        const auto label = localization.GetString(moving ? L"Move.Name" : directory_copy ? L"DirectoryCopy.Name" : copying ? L"Copy.Name" : renaming ? L"Rename.Name" : L"NewFolder.Name");
        ui->name.PlaceholderText(hstring(label));
        Automation::AutomationProperties::SetName(ui->name, hstring(label));
        Automation::AutomationProperties::SetAutomationId(ui->name, moving ? L"MoveName" : directory_copy ? L"DirectoryCopyName" : copying ? L"CopyName" : renaming ? L"RenameName" : L"NewFolderName");
        content.Children().Append(ui->name);
        ui->error = TextBlock(); ui->error.Text(hstring(error)); ui->error.TextWrapping(TextWrapping::Wrap);
        content.Children().Append(ui->error);
        ui->dialog.Content(content);
        ui->dialog.IsPrimaryButtonEnabled(ValidateNewFolderName(name) == ERROR_SUCCESS);
        const std::weak_ptr<Ui> weak = ui;
        ui->name.TextChanged([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->closed)
            {
                const auto value = live->name.Text();
                const auto valid = ValidateNewFolderName(std::wstring_view(value.c_str(), value.size())) == ERROR_SUCCESS;
                live->dialog.IsPrimaryButtonEnabled(valid);
                live->error.Text(valid ? hstring{} : hstring(live->invalid_name));
            }
        });
        ui->dialog.Opened([weak](auto&&, auto&&) {
            if (const auto live = weak.lock(); live && !live->closed)
            { (void)live->name.Focus(FocusState::Programmatic); live->name.SelectAll(); }
        });
        ui_ = ui; work_ = AwaitDialog(std::move(ui));
        return true;
    }
    Windows::Foundation::IAsyncAction FolderNameDialog::AwaitDialog(std::shared_ptr<Ui> ui)
    {
        HRESULT error = S_OK;
        ContentDialogResult result = ContentDialogResult::None;
        try { result = co_await ui->dialog.ShowAsync(); }
        catch (...) { error = to_hresult(); }
        if (ui->closed) { co_return; }
        ui->closed = true;
        if (FAILED(error)) { if (ui->failed) { ui->failed(error); } }
        else if (result == ContentDialogResult::Primary && ui->submit)
        {
            const auto value = ui->name.Text();
            const std::wstring name(value.c_str(), value.size());
            if (ValidateNewFolderName(name) == ERROR_SUCCESS)
            {
                try { ui->submit(name); }
                catch (...) { if (ui->failed) { ui->failed(to_hresult()); } }
            }
        }
    }
    void FolderNameDialog::Close() noexcept
    {
        if (ui_)
        {
            ui_->closed = true;
            if (ui_->dialog) { try { ui_->dialog.Hide(); } catch (...) {} }
        }
        ui_.reset(); work_ = nullptr;
    }
}
