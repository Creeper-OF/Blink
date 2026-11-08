#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

using namespace winrt;
using namespace Microsoft::UI::Dispatching;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace
{
    std::wstring EnvironmentValue(wchar_t const* name)
    {
        DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required == 0)
        {
            return {};
        }

        std::wstring value(required, L'\0');
        DWORD written = GetEnvironmentVariableW(name, value.data(), required);
        if (written == 0 || written >= required)
        {
            return {};
        }

        value.resize(written);
        return value;
    }

    enum class SmokeMode
    {
        M0,
        M1,
        M2,
        M3,
        R0,
        R1,
        R2,
        R3,
        R4,
        R5,
    };

    SmokeMode ReadMode()
    {
        auto value = EnvironmentValue(L"BLINK_WINUI_SMOKE_MODE");
        if (_wcsicmp(value.c_str(), L"M1") == 0)
        {
            return SmokeMode::M1;
        }
        if (_wcsicmp(value.c_str(), L"M2") == 0)
        {
            return SmokeMode::M2;
        }
        if (_wcsicmp(value.c_str(), L"M3") == 0)
        {
            return SmokeMode::M3;
        }
        if (_wcsicmp(value.c_str(), L"R1") == 0)
        {
            return SmokeMode::R1;
        }
        if (_wcsicmp(value.c_str(), L"R2") == 0)
        {
            return SmokeMode::R2;
        }
        if (_wcsicmp(value.c_str(), L"R3") == 0)
        {
            return SmokeMode::R3;
        }
        if (_wcsicmp(value.c_str(), L"R4") == 0)
        {
            return SmokeMode::R4;
        }
        if (_wcsicmp(value.c_str(), L"R5") == 0)
        {
            return SmokeMode::R5;
        }
        if (_wcsicmp(value.c_str(), L"R0") == 0)
        {
            return SmokeMode::R0;
        }
        return SmokeMode::M0;
    }

    bool IsResourceProbe(SmokeMode mode)
    {
        return mode == SmokeMode::R1 || mode == SmokeMode::R2 || mode == SmokeMode::R3 ||
            mode == SmokeMode::R4 || mode == SmokeMode::R5;
    }

    class Telemetry final
    {
    public:
        Telemetry() noexcept
        {
            try
            {
                auto configuredPath = EnvironmentValue(L"BLINK_SMOKE_TELEMETRY");
                std::filesystem::path path = configuredPath.empty()
                    ? (std::filesystem::current_path() / L"smoke-telemetry.log")
                    : std::filesystem::path(configuredPath);

                if (path.has_parent_path())
                {
                    std::filesystem::create_directories(path.parent_path());
                }
                stream_.open(path, std::ios::out | std::ios::app | std::ios::binary);
            }
            catch (...)
            {
                // Telemetry must not change the smoke host's UI behavior.
            }
        }

        void Stage(std::string_view stage) noexcept
        {
            try
            {
                if (stream_.is_open())
                {
                    stream_ << stage << '\n' << std::flush;
                }
            }
            catch (...)
            {
                // Telemetry failures are intentionally non-fatal.
            }
        }

    private:
        std::ofstream stream_;
    };

    struct SmokeApp : ApplicationT<SmokeApp>
    {
        SmokeApp()
            : mode_(ReadMode())
        {
            telemetry_.Stage("app.construct.begin");
        }

        void OnLaunched(LaunchActivatedEventArgs const&)
        {
            if (mode_ == SmokeMode::M2 || mode_ == SmokeMode::M3)
            {
                telemetry_.Stage("resources.begin");
                auto resources = ResourceDictionary();
                resources.MergedDictionaries().Append(XamlControlsResources());
                Resources(resources);
                telemetry_.Stage("resources.end");
            }
            else if (IsResourceProbe(mode_))
            {
                ConfigureResourceProbe();
            }

            telemetry_.Stage("window.begin");
            window_ = Window();
            telemetry_.Stage("window.end");

            telemetry_.Stage("content.begin");
            auto root = Grid();
            if (mode_ != SmokeMode::M0)
            {
                auto text = TextBlock();
                text.Text(L"Blink WinUI smoke");
                root.Children().Append(text);
            }
            window_.Content(root);
            telemetry_.Stage("content.end");

            window_.Activate();
            telemetry_.Stage("activate");
            ArmSurvivedTelemetry();
        }

    private:
        void ConfigureResourceProbe()
        {
            if (mode_ == SmokeMode::R1)
            {
                telemetry_.Stage("rd.construct.begin");
                resources_ = ResourceDictionary();
                telemetry_.Stage("rd.construct.end");
                telemetry_.Stage("resources.set.begin");
                Resources(resources_);
                telemetry_.Stage("resources.set.end");
                return;
            }

            if (mode_ == SmokeMode::R2)
            {
                telemetry_.Stage("xcr.construct.begin");
                xaml_controls_resources_ = XamlControlsResources();
                telemetry_.Stage("xcr.construct.end");
                return;
            }

            if (mode_ == SmokeMode::R5)
            {
                telemetry_.Stage("xcr.construct.begin");
                xaml_controls_resources_ = XamlControlsResources();
                telemetry_.Stage("xcr.construct.end");
                telemetry_.Stage("resources.set.begin");
                Resources(xaml_controls_resources_);
                telemetry_.Stage("resources.set.end");
                return;
            }

            telemetry_.Stage("rd.construct.begin");
            resources_ = ResourceDictionary();
            telemetry_.Stage("rd.construct.end");
            telemetry_.Stage("xcr.construct.begin");
            xaml_controls_resources_ = XamlControlsResources();
            telemetry_.Stage("xcr.construct.end");

            if (mode_ == SmokeMode::R3 || mode_ == SmokeMode::R4)
            {
                telemetry_.Stage("merge.append.begin");
                resources_.MergedDictionaries().Append(xaml_controls_resources_);
                telemetry_.Stage("merge.append.end");
            }

            if (mode_ == SmokeMode::R4)
            {
                telemetry_.Stage("resources.set.begin");
                Resources(resources_);
                telemetry_.Stage("resources.set.end");
            }
        }

        void ArmSurvivedTelemetry()
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

        SmokeMode mode_;
        Telemetry telemetry_;
        Window window_{ nullptr };
        DispatcherQueue dispatcher_{ nullptr };
        DispatcherQueueTimer survived_timer_{ nullptr };
        ResourceDictionary resources_{ nullptr };
        XamlControlsResources xaml_controls_resources_{ nullptr };
    };
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment(apartment_type::single_threaded);
    Application::Start([](auto&&)
    {
        static auto app = make<SmokeApp>();
        (void)app;
    });
    return 0;
}
