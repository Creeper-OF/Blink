#include "Blink/Index/Contracts.h"

#include <fileapi.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

#pragma comment(lib, "Kernel32.lib")

namespace blink::index::ipc
{
    namespace
    {
        constexpr std::uint32_t kWireMagic = 0x31504C42; // BLP1
        constexpr std::uint32_t kRequestKind = 1;
        constexpr std::uint32_t kResponseKind = 2;
        constexpr std::size_t kMaxFrameBytes = 1024 * 1024;
        constexpr std::uint32_t kMaxStringChars = 256 * 1024;
        constexpr std::uint32_t kMaxResults = 10'000;

        // Match the pipe DACL's client ACE.  Do not request GENERIC_WRITE:
        // its named-pipe mapping includes FILE_APPEND_DATA /
        // FILE_CREATE_PIPE_INSTANCE, which is intentionally reserved for
        // server principals.
        constexpr DWORD kPipeClientOpenRights =
            FILE_READ_DATA |
            FILE_WRITE_DATA |
            FILE_READ_ATTRIBUTES |
            FILE_WRITE_ATTRIBUTES |
            READ_CONTROL |
            SYNCHRONIZE;

        [[nodiscard]] bool InvalidHandle(HANDLE handle) noexcept
        {
            return handle == nullptr || handle == INVALID_HANDLE_VALUE;
        }

        template <typename T>
        void Append(std::vector<std::byte>& bytes, const T& value)
        {
            const auto offset = bytes.size();
            bytes.resize(offset + sizeof(T));
            std::memcpy(bytes.data() + offset, &value, sizeof(T));
        }

        void AppendBytes(std::vector<std::byte>& bytes, const void* value, std::size_t size)
        {
            const auto offset = bytes.size();
            bytes.resize(offset + size);
            if (size != 0)
            {
                std::memcpy(bytes.data() + offset, value, size);
            }
        }

        [[nodiscard]] bool AppendString(std::vector<std::byte>& bytes, const std::wstring& value)
        {
            if (value.size() > kMaxStringChars)
            {
                return false;
            }
            const auto length = static_cast<std::uint32_t>(value.size());
            Append(bytes, length);
            AppendBytes(bytes, value.data(), static_cast<std::size_t>(length) * sizeof(wchar_t));
            return true;
        }

        void AppendIdentity(std::vector<std::byte>& bytes, const core::FileIdentity& identity)
        {
            const std::uint8_t valid = identity.valid ? 1 : 0;
            Append(bytes, identity.volume_serial);
            Append(bytes, valid);
            AppendBytes(bytes, identity.file_id.data(), identity.file_id.size());
        }

        class Cursor
        {
        public:
            explicit Cursor(const std::vector<std::byte>& bytes) : bytes_(bytes) {}

            template <typename T>
            [[nodiscard]] bool Read(T& value)
            {
                if (offset_ + sizeof(T) > bytes_.size())
                {
                    return false;
                }
                std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
                offset_ += sizeof(T);
                return true;
            }

            [[nodiscard]] bool ReadBytes(void* destination, std::size_t size)
            {
                if (offset_ + size > bytes_.size())
                {
                    return false;
                }
                if (size != 0)
                {
                    std::memcpy(destination, bytes_.data() + offset_, size);
                }
                offset_ += size;
                return true;
            }

            [[nodiscard]] bool ReadString(std::wstring& value)
            {
                std::uint32_t length = 0;
                if (!Read(length) || length > kMaxStringChars)
                {
                    return false;
                }
                value.resize(length);
                return ReadBytes(value.data(), static_cast<std::size_t>(length) * sizeof(wchar_t));
            }

            [[nodiscard]] bool ReadIdentity(core::FileIdentity& identity)
            {
                std::uint8_t valid = 0;
                return Read(identity.volume_serial) &&
                    Read(valid) &&
                    ReadBytes(identity.file_id.data(), identity.file_id.size()) &&
                    (identity.valid = valid != 0, true);
            }

            [[nodiscard]] bool AtEnd() const noexcept { return offset_ == bytes_.size(); }

        private:
            const std::vector<std::byte>& bytes_;
            std::size_t offset_{};
        };

        void SetError(DWORD* error, DWORD value) noexcept
        {
            if (error != nullptr)
            {
                *error = value;
            }
        }

        [[nodiscard]] bool SerializeRequest(const Request& request, std::vector<std::byte>& bytes)
        {
            bytes.clear();
            Append(bytes, kWireMagic);
            Append(bytes, kRequestKind);
            Append(bytes, request.protocol_version);
            Append(bytes, request.request_id);
            const auto type = static_cast<std::uint32_t>(request.type);
            Append(bytes, type);
            Append(bytes, request.operation_id);
            const std::uint8_t prefix = request.prefix ? 1 : 0;
            Append(bytes, prefix);
            Append(bytes, request.limit);
            return AppendString(bytes, request.volume_root) &&
                AppendString(bytes, request.text) &&
                AppendString(bytes, request.extension) &&
                AppendString(bytes, request.path_prefix) &&
                bytes.size() <= kMaxFrameBytes;
        }

        [[nodiscard]] bool DeserializeRequest(const std::vector<std::byte>& bytes, Request& request)
        {
            Cursor cursor(bytes);
            std::uint32_t magic = 0;
            std::uint32_t kind = 0;
            std::uint32_t type = 0;
            std::uint8_t prefix = 0;
            if (!cursor.Read(magic) || !cursor.Read(kind) || !cursor.Read(request.protocol_version) ||
                !cursor.Read(request.request_id) || !cursor.Read(type) || !cursor.Read(request.operation_id) ||
                !cursor.Read(prefix) || !cursor.Read(request.limit) ||
                !cursor.ReadString(request.volume_root) || !cursor.ReadString(request.text) ||
                !cursor.ReadString(request.extension) || !cursor.ReadString(request.path_prefix) ||
                magic != kWireMagic || kind != kRequestKind || !cursor.AtEnd())
            {
                return false;
            }
            request.type = static_cast<RequestType>(type);
            request.prefix = prefix != 0;
            return true;
        }

        [[nodiscard]] bool SerializeResponse(const Response& response, std::vector<std::byte>& bytes)
        {
            if (response.message.size() > kMaxStringChars || response.results.size() > kMaxResults)
            {
                return false;
            }
            bytes.clear();
            Append(bytes, kWireMagic);
            Append(bytes, kResponseKind);
            Append(bytes, response.protocol_version);
            Append(bytes, response.request_id);
            Append(bytes, static_cast<std::uint32_t>(response.status));
            Append(bytes, response.error);
            Append(bytes, static_cast<std::uint32_t>(response.service_state));
            Append(bytes, static_cast<std::uint32_t>(response.index_state));
            Append(bytes, response.operation_id);
            Append(bytes, response.records_seen);
            Append(bytes, response.files);
            Append(bytes, response.directories);
            Append(bytes, response.index_bytes);
            Append(bytes, response.last_usn);
            Append(bytes, response.journal_id);
            if (!AppendString(bytes, response.message))
            {
                return false;
            }
            Append(bytes, static_cast<std::uint32_t>(response.results.size()));
            for (const SearchHit& result : response.results)
            {
                AppendIdentity(bytes, result.identity);
                AppendIdentity(bytes, result.parent_identity);
                Append(bytes, result.attributes);
                Append(bytes, result.last_usn);
                const std::uint8_t directory = result.is_directory ? 1 : 0;
                Append(bytes, directory);
                if (!AppendString(bytes, result.name) || !AppendString(bytes, result.full_path))
                {
                    return false;
                }
            }
            return bytes.size() <= kMaxFrameBytes;
        }

        [[nodiscard]] bool DeserializeResponse(const std::vector<std::byte>& bytes, Response& response)
        {
            Cursor cursor(bytes);
            std::uint32_t magic = 0;
            std::uint32_t kind = 0;
            std::uint32_t status = 0;
            std::uint32_t service_state = 0;
            std::uint32_t index_state = 0;
            std::uint32_t result_count = 0;
            if (!cursor.Read(magic) || !cursor.Read(kind) || !cursor.Read(response.protocol_version) ||
                !cursor.Read(response.request_id) || !cursor.Read(status) || !cursor.Read(response.error) ||
                !cursor.Read(service_state) || !cursor.Read(index_state) ||
                !cursor.Read(response.operation_id) || !cursor.Read(response.records_seen) ||
                !cursor.Read(response.files) || !cursor.Read(response.directories) ||
                !cursor.Read(response.index_bytes) || !cursor.Read(response.last_usn) ||
                !cursor.Read(response.journal_id) || !cursor.ReadString(response.message) ||
                !cursor.Read(result_count) || result_count > kMaxResults ||
                magic != kWireMagic || kind != kResponseKind)
            {
                return false;
            }
            response.status = static_cast<ResponseStatus>(status);
            response.service_state = static_cast<ServiceState>(service_state);
            response.index_state = static_cast<IndexState>(index_state);
            response.results.clear();
            response.results.reserve(result_count);
            for (std::uint32_t i = 0; i < result_count; ++i)
            {
                SearchHit result;
                std::uint8_t directory = 0;
                if (!cursor.ReadIdentity(result.identity) ||
                    !cursor.ReadIdentity(result.parent_identity) ||
                    !cursor.Read(result.attributes) || !cursor.Read(result.last_usn) ||
                    !cursor.Read(directory) || !cursor.ReadString(result.name) ||
                    !cursor.ReadString(result.full_path))
                {
                    return false;
                }
                result.is_directory = directory != 0;
                response.results.push_back(std::move(result));
            }
            return cursor.AtEnd();
        }

        [[nodiscard]] bool ReadMessage(HANDLE pipe, std::vector<std::byte>& bytes, DWORD* error)
        {
            bytes.clear();
            std::vector<std::byte> chunk(64 * 1024);
            for (;;)
            {
                DWORD read = 0;
                if (ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr))
                {
                    bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + read);
                    return bytes.size() <= kMaxFrameBytes;
                }
                const DWORD local_error = GetLastError();
                if (local_error != ERROR_MORE_DATA)
                {
                    SetError(error, local_error);
                    return false;
                }
                bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + read);
                if (bytes.size() > kMaxFrameBytes)
                {
                    SetError(error, ERROR_BUFFER_OVERFLOW);
                    return false;
                }
            }
        }

        [[nodiscard]] bool WriteMessage(HANDLE pipe, const std::vector<std::byte>& bytes, DWORD* error)
        {
            DWORD written = 0;
            if (!WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
                written != bytes.size())
            {
                SetError(error, GetLastError());
                return false;
            }
            return true;
        }

        [[nodiscard]] bool WaitOverlapped(HANDLE pipe, OVERLAPPED& overlapped, DWORD timeout_ms, DWORD* error)
        {
            const DWORD waited = WaitForSingleObject(overlapped.hEvent, timeout_ms);
            if (waited == WAIT_TIMEOUT)
            {
                (void)CancelIoEx(pipe, &overlapped);
                (void)WaitForSingleObject(overlapped.hEvent, 5'000);
                SetError(error, ERROR_TIMEOUT);
                return false;
            }
            if (waited != WAIT_OBJECT_0)
            {
                SetError(error, waited == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool WriteOverlapped(
            HANDLE pipe,
            const std::vector<std::byte>& bytes,
            DWORD timeout_ms,
            DWORD* error)
        {
            HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (event == nullptr)
            {
                SetError(error, GetLastError());
                return false;
            }
            OVERLAPPED overlapped{};
            overlapped.hEvent = event;
            DWORD written = 0;
            BOOL completed = WriteFile(
                pipe,
                bytes.data(),
                static_cast<DWORD>(bytes.size()),
                &written,
                &overlapped);
            DWORD local_error = completed ? ERROR_SUCCESS : GetLastError();
            if (!completed && local_error == ERROR_IO_PENDING)
            {
                if (!WaitOverlapped(pipe, overlapped, timeout_ms, &local_error) ||
                    !GetOverlappedResult(pipe, &overlapped, &written, FALSE))
                {
                    CloseHandle(event);
                    SetError(error, local_error == ERROR_SUCCESS ? GetLastError() : local_error);
                    return false;
                }
            }
            CloseHandle(event);
            if (local_error != ERROR_SUCCESS || written != bytes.size())
            {
                SetError(error, local_error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : local_error);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ReadOverlapped(
            HANDLE pipe,
            std::vector<std::byte>& bytes,
            DWORD timeout_ms,
            DWORD* error)
        {
            bytes.clear();
            HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (event == nullptr)
            {
                SetError(error, GetLastError());
                return false;
            }
            std::vector<std::byte> chunk(64 * 1024);
            bool done = false;
            while (!done)
            {
                ResetEvent(event);
                OVERLAPPED overlapped{};
                overlapped.hEvent = event;
                DWORD read = 0;
                BOOL completed = ReadFile(
                    pipe,
                    chunk.data(),
                    static_cast<DWORD>(chunk.size()),
                    &read,
                    &overlapped);
                DWORD local_error = completed ? ERROR_SUCCESS : GetLastError();
                if (!completed && local_error == ERROR_IO_PENDING)
                {
                    if (!WaitOverlapped(pipe, overlapped, timeout_ms, &local_error))
                    {
                        CloseHandle(event);
                        SetError(error, local_error);
                        return false;
                    }
                    completed = GetOverlappedResult(pipe, &overlapped, &read, FALSE);
                    if (!completed)
                    {
                        local_error = GetLastError();
                    }
                }
                bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + read);
                if (bytes.size() > kMaxFrameBytes)
                {
                    CloseHandle(event);
                    SetError(error, ERROR_BUFFER_OVERFLOW);
                    return false;
                }
                if (completed)
                {
                    done = true;
                }
                else if (local_error != ERROR_MORE_DATA)
                {
                    CloseHandle(event);
                    SetError(error, local_error);
                    return false;
                }
            }
            CloseHandle(event);
            return true;
        }
    }

    const wchar_t* RequestTypeName(RequestType type) noexcept
    {
        switch (type)
        {
        case RequestType::QueryServiceStatus: return L"QueryServiceStatus";
        case RequestType::QueryVolumeStatus: return L"QueryVolumeStatus";
        case RequestType::StartInitialIndex: return L"StartInitialIndex";
        case RequestType::QueryIndexProgress: return L"QueryIndexProgress";
        case RequestType::Search: return L"Search";
        case RequestType::StopOperation: return L"StopOperation";
        case RequestType::SearchDirectChildren: return L"SearchDirectChildren";
        case RequestType::SearchVisible: return L"SearchVisible";
        case RequestType::SearchVisibleDirectChildren: return L"SearchVisibleDirectChildren";
        case RequestType::SearchWithHidden: return L"SearchWithHidden";
        case RequestType::SearchWithHiddenDirectChildren: return L"SearchWithHiddenDirectChildren";
        default: return L"Unknown";
        }
    }

    const wchar_t* ResponseStatusName(ResponseStatus status) noexcept
    {
        switch (status)
        {
        case ResponseStatus::Ok: return L"Ok";
        case ResponseStatus::InvalidRequest: return L"InvalidRequest";
        case ResponseStatus::NotFound: return L"NotFound";
        case ResponseStatus::Busy: return L"Busy";
        case ResponseStatus::Failed: return L"Failed";
        case ResponseStatus::Unsupported: return L"Unsupported";
        default: return L"Unknown";
        }
    }

    const wchar_t* ServiceStateName(ServiceState state) noexcept
    {
        switch (state)
        {
        case ServiceState::Starting: return L"Starting";
        case ServiceState::Running: return L"Running";
        case ServiceState::Stopping: return L"Stopping";
        case ServiceState::Stopped: return L"Stopped";
        case ServiceState::Faulted: return L"Faulted";
        default: return L"Unknown";
        }
    }

    const wchar_t* IndexStateName(IndexState state) noexcept
    {
        switch (state)
        {
        case IndexState::Healthy: return L"Healthy";
        case IndexState::CatchingUp: return L"CatchingUp";
        case IndexState::RebuildRequired: return L"RebuildRequired";
        case IndexState::Rebuilding: return L"Rebuilding";
        case IndexState::Unavailable: return L"Unavailable";
        case IndexState::Corrupted: return L"Corrupted";
        default: return L"Unknown";
        }
    }

    bool ReadRequest(HANDLE pipe, Request& request, DWORD* error)
    {
        std::vector<std::byte> bytes;
        if (!ReadMessage(pipe, bytes, error) || !DeserializeRequest(bytes, request))
        {
            if (error != nullptr && *error == ERROR_SUCCESS)
            {
                *error = ERROR_INVALID_DATA;
            }
            return false;
        }
        return true;
    }

    bool WriteResponse(
        HANDLE pipe,
        const Response& response,
        DWORD* error,
        ResponseWriteTiming* timing)
    {
        std::vector<std::byte> bytes;
        if (timing != nullptr)
        {
            timing->serialization_start = std::chrono::steady_clock::now();
        }
        if (!SerializeResponse(response, bytes))
        {
            if (timing != nullptr)
            {
                timing->serialization_end = std::chrono::steady_clock::now();
            }
            SetError(error, ERROR_BUFFER_OVERFLOW);
            return false;
        }
        if (timing != nullptr)
        {
            timing->serialization_end = std::chrono::steady_clock::now();
            timing->pipe_write_start = timing->serialization_end;
        }
        const bool written = WriteMessage(pipe, bytes, error);
        if (timing != nullptr)
        {
            timing->pipe_write_end = std::chrono::steady_clock::now();
        }
        return written;
    }

    Client::~Client()
    {
        Disconnect();
    }

    bool Client::Connect(DWORD timeout_ms, DWORD* error)
    {
        SetError(error, ERROR_SUCCESS);
        Disconnect();
        if (!WaitNamedPipeW(kPipeName, timeout_ms))
        {
            SetError(error, GetLastError());
            return false;
        }

        CREATEFILE3_EXTENDED_PARAMETERS parameters{};
        parameters.dwSize = sizeof(parameters);
        parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        parameters.dwFileFlags = FILE_FLAG_OVERLAPPED;
        parameters.dwSecurityQosFlags = SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION;
        const HANDLE pipe = CreateFile3(
            kPipeName,
            kPipeClientOpenRights,
            0,
            OPEN_EXISTING,
            &parameters);
        if (InvalidHandle(pipe))
        {
            SetError(error, GetLastError());
            return false;
        }
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
        {
            const DWORD local_error = GetLastError();
            CloseHandle(pipe);
            SetError(error, local_error);
            return false;
        }
        {
            std::scoped_lock lock(mutex_);
            pipe_ = pipe;
            next_request_id_ = 1;
        }
        return true;
    }

    void Client::Disconnect()
    {
        std::scoped_lock lock(mutex_);
        if (!InvalidHandle(pipe_))
        {
            (void)CancelIoEx(pipe_, nullptr);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
    }

    bool Client::IsConnected() const
    {
        std::scoped_lock lock(mutex_);
        return !InvalidHandle(pipe_);
    }

    bool Client::Send(Request request, Response& response, DWORD timeout_ms, DWORD* error)
    {
        SetError(error, ERROR_SUCCESS);
        std::scoped_lock lock(mutex_);
        if (InvalidHandle(pipe_))
        {
            SetError(error, ERROR_PIPE_NOT_CONNECTED);
            return false;
        }
        request.protocol_version = kProtocolVersion;
        request.request_id = next_request_id_++;

        std::vector<std::byte> bytes;
        if (!SerializeRequest(request, bytes))
        {
            SetError(error, ERROR_BUFFER_OVERFLOW);
            return false;
        }
        DWORD local_error = ERROR_SUCCESS;
        if (!WriteOverlapped(pipe_, bytes, timeout_ms, &local_error))
        {
            (void)CancelIoEx(pipe_, nullptr);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
            SetError(error, local_error);
            return false;
        }
        if (!ReadOverlapped(pipe_, bytes, timeout_ms, &local_error) ||
            !DeserializeResponse(bytes, response))
        {
            if (local_error == ERROR_SUCCESS)
            {
                local_error = ERROR_INVALID_DATA;
            }
            (void)CancelIoEx(pipe_, nullptr);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
            SetError(error, local_error);
            return false;
        }
        if (response.request_id != request.request_id || response.protocol_version != kProtocolVersion)
        {
            SetError(error, ERROR_REVISION_MISMATCH);
            return false;
        }
        return true;
    }
}
