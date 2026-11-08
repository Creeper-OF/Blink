#pragma once
#include "Blink/App/ItemProperties.h"
#include "Blink/App/Localization.h"
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

namespace blink::app
{
    class PropertiesDialog
    {
    public:
        ~PropertiesDialog() noexcept;
        [[nodiscard]] bool Show(const winrt::Microsoft::UI::Xaml::XamlRoot& root,
            const Localization& localization, ItemPropertiesRequest request, std::function<void(HRESULT)> failed);
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close() noexcept;
    private:
        struct Ui;
        static void Render(const std::shared_ptr<Ui>& ui, const ItemPropertiesSnapshot& value);
        static winrt::Windows::Foundation::IAsyncAction Await(std::shared_ptr<Ui> ui);
        std::shared_ptr<Ui> ui_;
        winrt::Windows::Foundation::IAsyncAction dialog_work_{ nullptr }, read_work_{ nullptr };
    };
}
