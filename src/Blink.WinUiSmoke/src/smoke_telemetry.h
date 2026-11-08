#pragma once

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace blink_winui_smoke
{
    inline std::wstring EnvironmentValue(wchar_t const* name)
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
}
