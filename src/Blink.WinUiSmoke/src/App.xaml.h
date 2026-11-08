#pragma once

#include <unknwn.h>

#include "App.xaml.g.h"
#include "smoke_telemetry.h"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace winrt::Blink::WinUiSmoke::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        void ArmSurvivedTelemetry();

        blink_winui_smoke::Telemetry telemetry_;
        Microsoft::UI::Xaml::Window window_{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer survived_timer_{ nullptr };
    };
}
