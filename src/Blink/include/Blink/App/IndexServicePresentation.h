#pragma once

#include "Blink/App/IndexServiceState.h"
#include "Blink/App/Localization.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <functional>
#include <memory>

namespace blink::app
{
    // UI composition only; Client, workers and state belong to the coordinator.
    class IndexServicePresentation
    {
    public:
        using RefreshCallback = std::function<void(std::wstring)>;
        winrt::Microsoft::UI::Xaml::Controls::Button Build(
            const Localization& localization, RefreshCallback refresh,
            RefreshCallback enable = {}, RefreshCallback build_index = {});
        [[nodiscard]] static bool CanBuildSelectedIndex(const IndexServiceSnapshot& snapshot) noexcept;
        void Update(const IndexServiceSnapshot& snapshot, const Localization& localization);
        void Close() noexcept;
    private:
        struct Ui;
        std::shared_ptr<Ui> ui_;
    };
}
