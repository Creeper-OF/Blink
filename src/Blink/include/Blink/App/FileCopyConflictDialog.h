#pragma once

#include "Blink/App/FileCopyOperation.h"
#include "Blink/App/Localization.h"
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>
#include <functional>
#include <memory>

namespace blink::app
{
    class FileCopyConflictDialog
    {
    public:
        using SubmitCallback = std::function<void(FileCopyConflictPolicy)>;
        using FailureCallback = std::function<void(HRESULT)>;
        FileCopyConflictDialog() = default;
        ~FileCopyConflictDialog() noexcept;
        FileCopyConflictDialog(const FileCopyConflictDialog&) = delete;
        FileCopyConflictDialog& operator=(const FileCopyConflictDialog&) = delete;
        [[nodiscard]] bool Show(const winrt::Microsoft::UI::Xaml::XamlRoot& root,
            const Localization& localization, std::wstring source_path, std::wstring destination_path,
            bool enable_replace, SubmitCallback submit, FailureCallback failed, bool directory_copy = false, bool moving = false,
            bool enable_merge = false);
        [[nodiscard]] bool IsOpen() const noexcept;
        // Owner/window shutdown: hide without delivering a decision or error.
        void Close() noexcept;
    private:
        struct Ui;
        static winrt::Windows::Foundation::IAsyncAction AwaitDialog(std::shared_ptr<Ui> ui);
        std::shared_ptr<Ui> ui_;
        winrt::Windows::Foundation::IAsyncAction work_{ nullptr };
    };
}
