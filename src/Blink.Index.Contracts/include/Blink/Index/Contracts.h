#pragma once

#include "Blink/Core/FileTypes.h"

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace blink::index::ipc
{
    constexpr std::uint32_t kProtocolVersion = 1;
    constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\Blink.IndexService.v1";

    enum class RequestType : std::uint32_t
    {
        QueryServiceStatus = 1,
        QueryVolumeStatus = 2,
        StartInitialIndex = 3,
        QueryIndexProgress = 4,
        Search = 5,
        StopOperation = 6,
        // Existing Search payload, restricted to direct children of path_prefix.
        SearchDirectChildren = 7,
    };

    enum class ResponseStatus : std::uint32_t
    {
        Ok = 0,
        InvalidRequest = 1,
        NotFound = 2,
        Busy = 3,
        Failed = 4,
        Unsupported = 5,
    };

    enum class ServiceState : std::uint32_t
    {
        Starting = 0,
        Running = 1,
        Stopping = 2,
        Stopped = 3,
        Faulted = 4,
    };

    enum class IndexState : std::uint32_t
    {
        Unknown = 0,
        Healthy = 1,
        CatchingUp = 2,
        RebuildRequired = 3,
        Rebuilding = 4,
        Unavailable = 5,
        Corrupted = 6,
    };

    [[nodiscard]] const wchar_t* RequestTypeName(RequestType type) noexcept;
    [[nodiscard]] const wchar_t* ResponseStatusName(ResponseStatus status) noexcept;
    [[nodiscard]] const wchar_t* ServiceStateName(ServiceState state) noexcept;
    [[nodiscard]] const wchar_t* IndexStateName(IndexState state) noexcept;

    struct Request
    {
        std::uint32_t protocol_version{ kProtocolVersion };
        std::uint64_t request_id{};
        RequestType type{ RequestType::QueryServiceStatus };
        std::uint64_t operation_id{};
        std::wstring volume_root;
        std::wstring text;
        std::wstring extension;
        std::wstring path_prefix;
        bool prefix{};
        std::uint32_t limit{ 200 };
    };

    struct SearchHit
    {
        core::FileIdentity identity{};
        core::FileIdentity parent_identity{};
        std::wstring name;
        std::wstring full_path;
        DWORD attributes{};
        bool is_directory{};
        std::int64_t last_usn{};
    };

    struct Response
    {
        std::uint32_t protocol_version{ kProtocolVersion };
        std::uint64_t request_id{};
        ResponseStatus status{ ResponseStatus::Failed };
        DWORD error{ ERROR_GEN_FAILURE };
        ServiceState service_state{ ServiceState::Stopped };
        IndexState index_state{ IndexState::Unknown };
        std::uint64_t operation_id{};
        std::uint64_t records_seen{};
        std::uint64_t files{};
        std::uint64_t directories{};
        std::uint64_t index_bytes{};
        std::int64_t last_usn{};
        std::uint64_t journal_id{};
        std::wstring message;
        std::vector<SearchHit> results;
    };

    struct ResponseWriteTiming
    {
        std::chrono::steady_clock::time_point request_received{};
        std::chrono::steady_clock::time_point serialization_start{};
        std::chrono::steady_clock::time_point serialization_end{};
        std::chrono::steady_clock::time_point pipe_write_start{};
        std::chrono::steady_clock::time_point pipe_write_end{};
    };

    [[nodiscard]] bool ReadRequest(HANDLE pipe, Request& request, DWORD* error = nullptr);
    [[nodiscard]] bool WriteResponse(
        HANDLE pipe,
        const Response& response,
        DWORD* error = nullptr,
        ResponseWriteTiming* timing = nullptr);

    class Client
    {
    public:
        Client() = default;
        ~Client();

        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        [[nodiscard]] bool Connect(DWORD timeout_ms = 1000, DWORD* error = nullptr);
        void Disconnect();
        [[nodiscard]] bool IsConnected() const;
        [[nodiscard]] bool Send(Request request, Response& response, DWORD timeout_ms = 5000, DWORD* error = nullptr);

    private:
        mutable std::mutex mutex_;
        HANDLE pipe_{ INVALID_HANDLE_VALUE };
        std::uint64_t next_request_id_{ 1 };
    };
}
