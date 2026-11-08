#include "App.xaml.h"

#include <chrono>

#include <winrt/Microsoft.UI.Xaml.Controls.h>

using namespace winrt;
using namespace Microsoft::UI::Dispatching;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::Blink::WinUiSmoke::implementation
{
    App::App()
    {
        telemetry_.Stage("app.construct.begin");
        telemetry_.Stage("resources.begin");
        InitializeComponent();
        telemetry_.Stage("resources.end");
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        telemetry_.Stage("window.begin");
        window_ = Window();
        telemetry_.Stage("window.end");

        telemetry_.Stage("content.begin");
        auto root = Grid();
        auto text = TextBlock();
        text.Text(L"generated-main smoke");
        root.Children().Append(text);
        window_.Content(root);
        telemetry_.Stage("content.end");

        window_.Activate();
        telemetry_.Stage("activate");
        ArmSurvivedTelemetry();
    }

    void App::ArmSurvivedTelemetry()
    {
        dispatcher_ = DispatcherQueue::GetForCurrentThread();
        if (!dispatcher_)
        {
            return;
        }

        survived_timer_ = dispatcher_.CreateTimer();
        survived_timer_.Interval(std::chrono::seconds(1));
        survived_timer_.IsRepeating(false);
        survived_timer_.Tick([this](auto&&, auto&&)
        {
            telemetry_.Stage("survived");
        });
        survived_timer_.Start();
    }
}
