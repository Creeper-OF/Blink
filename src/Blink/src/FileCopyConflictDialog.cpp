#include "Blink/App/FileCopyConflictDialog.h"
#include "Blink/App/CopyConflictModel.h"
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <utility>

namespace blink::app
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;

    struct FileCopyConflictDialog::Ui
    {
        explicit Ui(const bool enable_replace, const bool enable_merge) : model(enable_replace, enable_merge) {}
        ContentDialog dialog{ nullptr };
        CopyConflictModel model;
        SubmitCallback submit;
        FailureCallback failed;
        void ReportFailure(const HRESULT error) const noexcept
        {
            if (failed) { try { failed(error); } catch (...) {} }
        }
    };

    FileCopyConflictDialog::~FileCopyConflictDialog() noexcept { Close(); }
    bool FileCopyConflictDialog::IsOpen() const noexcept { return ui_ && !ui_->model.IsClosed(); }
    bool FileCopyConflictDialog::Show(const XamlRoot& root, const Localization& localization,
        std::wstring source_path, std::wstring destination_path, const bool enable_replace,
        SubmitCallback submit, FailureCallback failed, const bool directory_copy, const bool moving, const bool enable_merge)
    {
        if (IsOpen()) { return false; }
        const bool allow_replace = enable_replace && !directory_copy;
        const bool allow_merge = enable_merge && directory_copy && !moving;
        auto ui = std::make_shared<Ui>(allow_replace, allow_merge);
        ui->submit = std::move(submit); ui->failed = std::move(failed);
        ui->dialog = ContentDialog(); ui->dialog.XamlRoot(root);
        ui->dialog.Title(box_value(hstring(localization.GetString(moving ? L"MoveConflict.Title" : directory_copy ? L"DirectoryCopyConflict.Title" : L"CopyConflict.Title"))));
        ui->dialog.CloseButtonText(hstring(localization.GetString(L"CopyConflict.Cancel")));
        // Pressing Enter or Escape must never implicitly overwrite a file.
        ui->dialog.DefaultButton(ContentDialogButton::Close);
        Automation::AutomationProperties::SetAutomationId(ui->dialog, moving ? L"MoveConflictDialog" : directory_copy ? L"DirectoryCopyConflictDialog" : L"CopyConflictDialog");
        StackPanel content; content.Spacing(8);
        const auto add_text = [&content](const std::wstring& text, const wchar_t* id) {
            TextBlock block; block.Text(hstring(text)); block.TextWrapping(TextWrapping::Wrap);
            block.MaxLines(3); block.TextTrimming(TextTrimming::CharacterEllipsis);
            ToolTipService::SetToolTip(block, box_value(hstring(text)));
            Automation::AutomationProperties::SetAutomationId(block, id);
            content.Children().Append(block);
        };
        add_text(localization.GetString(moving ? L"MoveConflict.Description" : directory_copy ? L"DirectoryCopyConflict.Description" : L"CopyConflict.Description"), L"CopyConflictDescription");
        add_text(localization.Format(L"CopyConflict.Source", source_path), L"CopyConflictSource");
        add_text(localization.Format(L"CopyConflict.Destination", destination_path), L"CopyConflictDestination");
        const std::weak_ptr<Ui> weak = ui;
        const auto add_choice = [&content, &localization, weak](const FileCopyConflictPolicy policy,
            const wchar_t* key, const wchar_t* id, const bool enabled = true) {
            Button button; button.Content(box_value(hstring(localization.GetString(key))));
            button.HorizontalAlignment(HorizontalAlignment::Stretch); button.IsEnabled(enabled);
            Automation::AutomationProperties::SetAutomationId(button, id);
            button.Click([weak, policy](auto&&, auto&&) {
                if (const auto live = weak.lock(); live && live->model.Choose(policy))
                {
                    try { live->dialog.Hide(); }
                    catch (...)
                    {
                        live->model.Close();
                        live->ReportFailure(to_hresult());
                    }
                }
            });
            content.Children().Append(button);
        };
        const bool directory_merge = directory_copy && !moving;
        add_choice(directory_merge ? FileCopyConflictPolicy::Merge : FileCopyConflictPolicy::Replace,
            directory_merge ? L"DirectoryCopyConflict.Merge" : L"CopyConflict.Replace",
            directory_merge ? L"CopyConflictMerge" : L"CopyConflictReplace", directory_merge ? allow_merge : allow_replace);
        if (!allow_replace && !allow_merge)
        { add_text(localization.GetString(directory_copy && !moving ? L"DirectoryCopyConflict.ReplaceUnavailable" : L"CopyConflict.ReplaceUnavailable"), L"CopyConflictReplaceUnavailable"); }
        add_choice(FileCopyConflictPolicy::Skip, L"CopyConflict.Skip", L"CopyConflictSkip");
        add_choice(FileCopyConflictPolicy::KeepBoth, L"CopyConflict.KeepBoth", L"CopyConflictKeepBoth");
        add_text(localization.GetString(moving ? L"MoveConflict.KeepBothDescription" : directory_copy ? L"DirectoryCopyConflict.KeepBothDescription" : L"CopyConflict.KeepBothDescription"), L"CopyConflictKeepBothDescription");
        ui->dialog.Content(content);
        ui->dialog.Closing([weak](auto&&, auto&&) {
            if (const auto live = weak.lock()) { (void)live->model.Choose(FileCopyConflictPolicy::Cancel); }
        });
        ui_ = ui;
        work_ = AwaitDialog(std::move(ui));
        return true;
    }

    Windows::Foundation::IAsyncAction FileCopyConflictDialog::AwaitDialog(std::shared_ptr<Ui> ui)
    {
        HRESULT error = S_OK;
        try { (void)co_await ui->dialog.ShowAsync(); }
        catch (...) { error = to_hresult(); }
        if (ui->model.IsClosed()) { co_return; }
        if (FAILED(error))
        {
            ui->model.Close();
            ui->ReportFailure(error);
            co_return;
        }
        const auto policy = ui->model.Complete();
        if (policy && ui->submit)
        {
            try { ui->submit(*policy); }
            catch (...) { ui->ReportFailure(to_hresult()); }
        }
    }
    void FileCopyConflictDialog::Close() noexcept
    {
        if (ui_)
        {
            ui_->model.Close();
            if (ui_->dialog) { try { ui_->dialog.Hide(); } catch (...) {} }
        }
        ui_.reset(); work_ = nullptr;
    }
}
