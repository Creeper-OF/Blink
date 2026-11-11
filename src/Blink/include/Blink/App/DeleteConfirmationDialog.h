#pragma once
#include "Blink/App/Localization.h"
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>
#include <functional>
#include <memory>
#include <vector>

namespace blink::app
{
    class DeleteConfirmationDialog
    {
    public:
        using DismissedCallback = std::function<void()>;
        ~DeleteConfirmationDialog() noexcept;
        void SetDismissedCallback(DismissedCallback callback);
        [[nodiscard]] bool IsOpen() const noexcept;
        bool Show(const winrt::Microsoft::UI::Xaml::XamlRoot& root, const Localization& localization,
            const std::vector<std::wstring>& names, std::function<void()> confirmed, std::function<void(HRESULT)> failed);
        void Close() noexcept;
    private:
        struct Ui;
        static winrt::Windows::Foundation::IAsyncAction Await(std::shared_ptr<Ui> ui);
        std::shared_ptr<Ui> ui_;
        DismissedCallback dismissed_;
        winrt::Windows::Foundation::IAsyncAction work_{ nullptr };
    };
}
