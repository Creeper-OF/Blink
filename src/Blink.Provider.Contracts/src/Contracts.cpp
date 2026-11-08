#include "Blink/Provider/Contracts.h"

#include "Blink/Core/FileTypes.h"

#include <sddl.h>

#include <chrono>
#include <cwctype>
#include <sstream>
#include <vector>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Kernel32.lib")

namespace blink::provider
{
    namespace
    {
        [[nodiscard]] bool InvalidHandle(HANDLE handle) noexcept
        {
            return handle == nullptr || handle == INVALID_HANDLE_VALUE;
        }

        [[nodiscard]] std::wstring Lower(std::wstring value)
        {
            for (auto& character : value)
            {
                character = static_cast<wchar_t>(std::towlower(character));
            }
            return value;
        }

        [[nodiscard]] bool WriteMessage(HANDLE pipe, const std::wstring& message)
        {
            DWORD written = 0;
            const DWORD bytes = static_cast<DWORD>(message.size() * sizeof(wchar_t));
            return WriteFile(pipe, message.data(), bytes, &written, nullptr) != FALSE && written == bytes;
        }

        [[nodiscard]] bool ReadMessage(HANDLE pipe, std::wstring& message)
        {
            std::vector<std::byte> buffer(64 * 1024);
            DWORD read = 0;
            if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
            {
                return false;
            }
            if (read % sizeof(wchar_t) != 0)
            {
                return false;
            }
            message.assign(reinterpret_cast<const wchar_t*>(buffer.data()), read / sizeof(wchar_t));
            return true;
        }

        [[nodiscard]] HANDLE OpenPipe(const std::wstring& name)
        {
            CREATEFILE3_EXTENDED_PARAMETERS parameters{};
            parameters.dwSize = sizeof(parameters);
            parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            const HANDLE pipe = CreateFile3(
                name.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                OPEN_EXISTING,
                &parameters);
            if (InvalidHandle(pipe))
            {
                return INVALID_HANDLE_VALUE;
            }
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
            {
                CloseHandle(pipe);
                return INVALID_HANDLE_VALUE;
            }
            return pipe;
        }

        [[nodiscard]] std::wstring BuildCommandLine(
            const std::wstring& host,
            const std::wstring& pipe_token,
            FakeProviderMode mode)
        {
            // The pipe is passed as an opaque token, while the quoted module
            // path makes argv[0] deterministic for the CRT.
            return L"\"" + host + L"\" --pipe-token " + pipe_token + L" --mode " + ToString(mode);
        }

        void SetError(DWORD* error, DWORD value) noexcept
        {
            if (error != nullptr)
            {
                *error = value;
            }
        }
    }

    const wchar_t* ToString(FakeProviderMode mode) noexcept
    {
        switch (mode)
        {
        case FakeProviderMode::Normal: return L"normal";
        case FakeProviderMode::Delay: return L"delay";
        case FakeProviderMode::Hang: return L"hang";
        case FakeProviderMode::Crash: return L"crash";
        case FakeProviderMode::Error: return L"error";
        case FakeProviderMode::Disconnect: return L"disconnect";
        }
        return L"normal";
    }

    FakeProviderMode ParseMode(const std::wstring& value) noexcept
    {
        const std::wstring mode = Lower(value);
        if (mode == L"delay") return FakeProviderMode::Delay;
        if (mode == L"hang") return FakeProviderMode::Hang;
        if (mode == L"crash") return FakeProviderMode::Crash;
        if (mode == L"error") return FakeProviderMode::Error;
        if (mode == L"disconnect") return FakeProviderMode::Disconnect;
        return FakeProviderMode::Normal;
    }

    ProviderClient::~ProviderClient()
    {
        Stop(true);
    }

    bool ProviderClient::Start(const std::wstring& provider_host_path, FakeProviderMode mode, DWORD* error)
    {
        Stop(true);
        const std::wstring pipe_token = L"Blink.Provider." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
        const std::wstring pipe_name = L"\\\\.\\pipe\\" + pipe_token;
        std::wstring command_line = BuildCommandLine(provider_host_path, pipe_token, mode);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process_info{};
        if (!CreateProcessW(
                provider_host_path.c_str(),
                command_line.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &startup,
                &process_info))
        {
            SetError(error, GetLastError());
            return false;
        }
        bool connected = false;
        HANDLE pipe = INVALID_HANDLE_VALUE;
        for (int attempt = 0; attempt < 150; ++attempt)
        {
            pipe = OpenPipe(pipe_name);
            if (!InvalidHandle(pipe))
            {
                connected = true;
                break;
            }
            if (WaitForSingleObject(process_info.hProcess, 0) != WAIT_TIMEOUT)
            {
                break;
            }
            Sleep(100);
        }

        if (InvalidHandle(pipe))
        {
            const DWORD connect_error = GetLastError();
            DWORD exit_code = STILL_ACTIVE;
            GetExitCodeProcess(process_info.hProcess, &exit_code);
            TerminateProcess(process_info.hProcess, ERROR_PIPE_NOT_CONNECTED);
            CloseHandle(process_info.hThread);
            CloseHandle(process_info.hProcess);
            const DWORD failure = exit_code == STILL_ACTIVE
                ? (connect_error == ERROR_SUCCESS ? ERROR_PIPE_NOT_CONNECTED : connect_error)
                : exit_code;
            SetError(error, failure);
            return false;
        }

        {
            std::scoped_lock lock(mutex_);
            process_ = process_info.hProcess;
            process_thread_ = process_info.hThread;
            pipe_ = pipe;
            pipe_name_ = pipe_name;
        }
        SetError(error, ERROR_SUCCESS);
        return true;
    }

    void ProviderClient::RequestDirectoryAsync(std::wstring path, ProviderCallback callback)
    {
        CancelRequest();
        JoinRequestThread();
        cancel_request_.store(false);
        request_thread_ = std::thread(&ProviderClient::RequestDirectory, this, std::move(path), std::move(callback));
    }

    void ProviderClient::RequestDirectory(std::wstring path, ProviderCallback callback)
    {
        HANDLE native_thread = INVALID_HANDLE_VALUE;
        DuplicateHandle(
            GetCurrentProcess(),
            GetCurrentThread(),
            GetCurrentProcess(),
            &native_thread,
            THREAD_ALL_ACCESS,
            FALSE,
            0);
        {
            std::scoped_lock lock(mutex_);
            request_thread_handle_ = native_thread;
        }

        HANDLE pipe = INVALID_HANDLE_VALUE;
        {
            std::scoped_lock lock(mutex_);
            pipe = pipe_;
        }
        ProviderResponse response;
        if (InvalidHandle(pipe))
        {
            response.error = ERROR_PIPE_NOT_CONNECTED;
            response.message = L"ProviderHost is not running";
        }
        else
        {
            const std::wstring request = L"LIST\t" + path;
            if (!WriteMessage(pipe, request))
            {
                response.error = GetLastError();
                response.message = L"Provider request write failed";
            }
            else
            {
                std::wstring wire_response;
                if (!ReadMessage(pipe, wire_response))
                {
                    response.error = cancel_request_.load() ? ERROR_CANCELLED : GetLastError();
                    response.canceled = cancel_request_.load();
                    response.message = response.canceled ? L"Provider request canceled" : L"ProviderHost disconnected";
                }
                else if (wire_response.rfind(L"OK\t", 0) == 0)
                {
                    response.success = true;
                    response.message = wire_response.substr(3);
                }
                else if (wire_response.rfind(L"ERROR\t", 0) == 0)
                {
                    response.error = ERROR_GEN_FAILURE;
                    response.message = wire_response.substr(6);
                }
                else
                {
                    response.error = ERROR_INVALID_DATA;
                    response.message = L"Invalid ProviderHost response";
                }
            }
        }

        if (native_thread != INVALID_HANDLE_VALUE)
        {
            CloseHandle(native_thread);
        }
        {
            std::scoped_lock lock(mutex_);
            request_thread_handle_ = INVALID_HANDLE_VALUE;
        }
        if (callback)
        {
            callback(std::move(response));
        }
    }

    void ProviderClient::CancelRequest()
    {
        cancel_request_.store(true);
        HANDLE request_thread = INVALID_HANDLE_VALUE;
        {
            std::scoped_lock lock(mutex_);
            request_thread = request_thread_handle_;
        }
        if (!InvalidHandle(request_thread))
        {
            CancelSynchronousIo(request_thread);
        }
    }

    void ProviderClient::JoinRequestThread()
    {
        if (request_thread_.joinable())
        {
            request_thread_.join();
        }
    }

    void ProviderClient::Stop(bool terminate_host)
    {
        CancelRequest();
        JoinRequestThread();

        HANDLE pipe = INVALID_HANDLE_VALUE;
        HANDLE process = INVALID_HANDLE_VALUE;
        HANDLE process_thread = INVALID_HANDLE_VALUE;
        {
            std::scoped_lock lock(mutex_);
            pipe = pipe_;
            process = process_;
            process_thread = process_thread_;
            pipe_ = INVALID_HANDLE_VALUE;
            process_ = INVALID_HANDLE_VALUE;
            process_thread_ = INVALID_HANDLE_VALUE;
            pipe_name_.clear();
        }

        if (!InvalidHandle(pipe))
        {
            CloseHandle(pipe);
        }
        if (!InvalidHandle(process))
        {
            if (terminate_host && WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
            {
                TerminateProcess(process, ERROR_PROCESS_ABORTED);
            }
            WaitForSingleObject(process, 2000);
            CloseHandle(process);
        }
        if (!InvalidHandle(process_thread))
        {
            CloseHandle(process_thread);
        }
    }

    bool ProviderClient::IsRunning() const
    {
        std::scoped_lock lock(mutex_);
        return !InvalidHandle(process_) && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }

    std::wstring ProviderClient::PipeName() const
    {
        std::scoped_lock lock(mutex_);
        return pipe_name_;
    }

    int ProviderServer::Run(const std::wstring& pipe_name, FakeProviderMode mode)
    {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)",
                SDDL_REVISION_1,
                &descriptor,
                nullptr))
        {
            return static_cast<int>(GetLastError());
        }
        security.lpSecurityDescriptor = descriptor;

        const HANDLE pipe = CreateNamedPipeW(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            64 * 1024,
            64 * 1024,
            0,
            &security);
        LocalFree(descriptor);
        if (InvalidHandle(pipe))
        {
            return static_cast<int>(GetLastError());
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!connected)
        {
            CloseHandle(pipe);
            return static_cast<int>(GetLastError());
        }

        while (true)
        {
            std::wstring request;
            if (!ReadMessage(pipe, request))
            {
                break;
            }
            if (request.rfind(L"LIST\t", 0) != 0)
            {
                (void)WriteMessage(pipe, L"ERROR\tUnknown command");
                continue;
            }

            if (mode == FakeProviderMode::Delay)
            {
                Sleep(30'000);
            }
            else if (mode == FakeProviderMode::Hang)
            {
                Sleep(INFINITE);
            }
            else if (mode == FakeProviderMode::Crash)
            {
                TerminateProcess(GetCurrentProcess(), 0xC0000005);
            }
            else if (mode == FakeProviderMode::Error)
            {
                (void)WriteMessage(pipe, L"ERROR\tFakeProvider returned a deliberate error");
                continue;
            }
            else if (mode == FakeProviderMode::Disconnect)
            {
                (void)DisconnectNamedPipe(pipe);
                break;
            }

            const std::wstring path = request.substr(5);
            const std::wstring message = L"OK\tFakeProvider normal response for " + path + L"\nentry-a\nentry-b";
            if (!WriteMessage(pipe, message))
            {
                break;
            }
        }

        (void)DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        return 0;
    }
}
