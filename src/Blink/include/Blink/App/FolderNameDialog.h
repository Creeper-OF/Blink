#pragma once

#include "Blink/App/Localization.h"
#include "Blink/App/FileNameSelection.h"
#include <Windows.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>
#include <functional>
#include <memory>

namespace blink::app
{
    class FolderNameDialog
    {
    public:
        using SubmitCallback = std::function<void(std::wstring)>;
        using FailureCallback = std::function<void(HRESULT)>;
        using DismissedCallback = std::function<void()>;
        void SetDismissedCallback(DismissedCallback callback);
        ~FolderNameDialog() noexcept;
        [[nodiscard]] bool Show(const winrt::Microsoft::UI::Xaml::XamlRoot& root,
            const Localization& localization, std::wstring parent, std::wstring name,
            std::wstring error, SubmitCallback submit, FailureCallback failed, bool renaming = false, bool copying = false,
            bool directory_copy = false, bool moving = false,
            FileNameSelectionMode selection_mode = FileNameSelectionMode::WholeName);
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close() noexcept;
    private:
        struct Ui;
        static winrt::Windows::Foundation::IAsyncAction AwaitDialog(std::shared_ptr<Ui> ui);
        DismissedCallback dismissed_;
        std::shared_ptr<Ui> ui_;
        winrt::Windows::Foundation::IAsyncAction work_{ nullptr };
    };
}
