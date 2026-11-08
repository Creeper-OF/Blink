#pragma once

#include <Windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace blink::provider
{
    enum class FakeProviderMode
    {
        Normal,
        Delay,
        Hang,
        Crash,
        Error,
        Disconnect,
    };

    [[nodiscard]] const wchar_t* ToString(FakeProviderMode mode) noexcept;
    [[nodiscard]] FakeProviderMode ParseMode(const std::wstring& value) noexcept;

    struct ProviderResponse
    {
        bool success{};
        bool canceled{};
        DWORD error{};
        std::wstring message;
    };

    using ProviderCallback = std::function<void(ProviderResponse)>;

    class ProviderClient
    {
    public:
        ProviderClient() = default;
        ~ProviderClient();

        ProviderClient(const ProviderClient&) = delete;
        ProviderClient& operator=(const ProviderClient&) = delete;

        [[nodiscard]] bool Start(
            const std::wstring& provider_host_path,
            FakeProviderMode mode,
            DWORD* error = nullptr);
        void RequestDirectoryAsync(std::wstring path, ProviderCallback callback);
        void CancelRequest();
        void Stop(bool terminate_host = true);
        [[nodiscard]] bool IsRunning() const;
        [[nodiscard]] std::wstring PipeName() const;

    private:
        void JoinRequestThread();
        void RequestDirectory(std::wstring path, ProviderCallback callback);

        mutable std::mutex mutex_;
        HANDLE process_{ INVALID_HANDLE_VALUE };
        HANDLE process_thread_{ INVALID_HANDLE_VALUE };
        HANDLE pipe_{ INVALID_HANDLE_VALUE };
        HANDLE request_thread_handle_{ INVALID_HANDLE_VALUE };
        std::thread request_thread_;
        std::atomic_bool cancel_request_{ false };
        std::wstring pipe_name_;
    };

    class ProviderServer
    {
    public:
        [[nodiscard]] static int Run(const std::wstring& pipe_name, FakeProviderMode mode);
    };
}

