#include "Blink/Core/FileTypes.h"
#include "Blink/Index/Contracts.h"
#include "Blink/Index/FastIndex.h"
#include "Blink/Index/UsnJournal.h"
#include "Blink/IndexService/PipeImpersonationGuard.h"
#include "Blink/IndexService/ProgressSnapshot.h"

#include <Windows.h>

#include <aclapi.h>
#include <fileapi.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Kernel32.lib")

namespace blink::index::service
{
    class ServiceRuntime;

    namespace
    {
        constexpr wchar_t kServiceName[] = L"Blink.IndexService";
        constexpr wchar_t kServiceSidName[] = L"NT SERVICE\\Blink.IndexService";

        // PIPE_ACCESS_DUPLEX maps the server handle to
        // FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE.  Keep the
        // client rights explicit: FILE_GENERIC_WRITE would also grant the
        // FILE_APPEND_DATA/FILE_CREATE_PIPE_INSTANCE bit for named pipes.
        constexpr DWORD kPipeClientRights =
            FILE_READ_DATA |
            FILE_WRITE_DATA |
            FILE_READ_ATTRIBUTES |
            FILE_WRITE_ATTRIBUTES |
            READ_CONTROL |
            SYNCHRONIZE;
        constexpr DWORD kPipeServerRights =
            FILE_GENERIC_READ |
            FILE_GENERIC_WRITE |
            SYNCHRONIZE;
        static_assert(kPipeClientRights == 0x00120183u);
        static_assert(kPipeServerRights == 0x0012019Fu);
        static_assert((kPipeClientRights & FILE_CREATE_PIPE_INSTANCE) == 0);

        [[nodiscard]] bool InvalidHandle(HANDLE handle) noexcept
        {
            return handle == nullptr || handle == INVALID_HANDLE_VALUE;
        }

        [[nodiscard]] std::wstring ModulePath()
        {
            std::vector<wchar_t> buffer(32 * 1024);
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            return length == 0 ? std::wstring{} : std::wstring(buffer.data(), length);
        }

        [[nodiscard]] std::wstring FindDefaultDataRoot()
        {
            wchar_t* environment = nullptr;
            std::size_t environment_length = 0;
            if (_wdupenv_s(&environment, &environment_length, L"BLINK_DATA_ROOT") == 0 &&
                environment != nullptr && environment_length != 0)
            {
                const std::wstring result(environment, environment_length - 1);
                std::free(environment);
                return result;
            }
            std::free(environment);

            std::filesystem::path current = std::filesystem::path(ModulePath()).parent_path();
            for (int depth = 0; depth < 8 && !current.empty(); ++depth)
            {
                if (std::filesystem::exists(current / L"Blink.sln"))
                {
                    return (current / L".blink-data").wstring();
                }
                current = current.parent_path();
            }
            return (std::filesystem::path(ModulePath()).parent_path() / L".blink-data").wstring();
        }

        [[nodiscard]] bool IsDriveRoot(const std::wstring& value)
        {
            return value.size() == 3 &&
                ((value[0] >= L'A' && value[0] <= L'Z') || (value[0] >= L'a' && value[0] <= L'z')) &&
                value[1] == L':' && (value[2] == L'\\' || value[2] == L'/');
        }

        [[nodiscard]] std::wstring NormalizeVolumeRoot(const std::wstring& value)
        {
            if (!IsDriveRoot(value))
            {
                return {};
            }
            wchar_t buffer[32768]{};
            if (!GetVolumePathNameW(value.c_str(), buffer, static_cast<DWORD>(std::size(buffer))))
            {
                return {};
            }
            return buffer;
        }

        [[nodiscard]] std::wstring Lower(std::wstring value)
        {
            for (wchar_t& character : value)
            {
                character = static_cast<wchar_t>(std::towlower(character));
            }
            return value;
        }

        [[nodiscard]] std::wstring SidToString(PSID sid)
        {
            if (sid == nullptr || !IsValidSid(sid))
            {
                return {};
            }
            LPWSTR text = nullptr;
            if (!ConvertSidToStringSidW(sid, &text))
            {
                return {};
            }
            const std::wstring result(text);
            LocalFree(text);
            return result;
        }

        [[nodiscard]] bool IsSidString(const std::wstring& value)
        {
            if (value.empty())
            {
                return false;
            }
            PSID sid = nullptr;
            if (!ConvertStringSidToSidW(value.c_str(), &sid))
            {
                return false;
            }
            const bool valid = IsValidSid(sid) != FALSE;
            LocalFree(sid);
            return valid;
        }

        [[nodiscard]] std::wstring CurrentProcessUserSid()
        {
            HANDLE token = INVALID_HANDLE_VALUE;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            {
                return {};
            }
            blink::core::UniqueHandle token_guard(token);

            DWORD bytes = 0;
            (void)GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
            if (bytes == 0)
            {
                return {};
            }
            std::vector<std::byte> buffer(bytes);
            if (!GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes))
            {
                return {};
            }
            const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
            return SidToString(user->User.Sid);
        }

        [[nodiscard]] std::wstring LookupAccountSidString(const std::wstring& account_name, DWORD& error)
        {
            DWORD sid_bytes = 0;
            DWORD domain_chars = 0;
            SID_NAME_USE use{};
            (void)LookupAccountNameW(
                nullptr,
                account_name.c_str(),
                nullptr,
                &sid_bytes,
                nullptr,
                &domain_chars,
                &use);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sid_bytes == 0)
            {
                error = GetLastError();
                return {};
            }

            std::vector<std::byte> sid(sid_bytes);
            std::vector<wchar_t> domain(std::max<DWORD>(domain_chars, 1));
            if (!LookupAccountNameW(
                    nullptr,
                    account_name.c_str(),
                    sid.data(),
                    &sid_bytes,
                    domain.data(),
                    &domain_chars,
                    &use))
            {
                error = GetLastError();
                return {};
            }
            const std::wstring result = SidToString(reinterpret_cast<PSID>(sid.data()));
            error = result.empty() ? ERROR_INVALID_SID : ERROR_SUCCESS;
            return result;
        }

        [[nodiscard]] std::string NarrowAscii(const std::wstring& value)
        {
            std::string result;
            result.reserve(value.size());
            for (const wchar_t character : value)
            {
                result.push_back(character >= 0 && character <= 0x7F
                    ? static_cast<char>(character)
                    : '?');
            }
            return result;
        }

        [[nodiscard]] std::string JsonEscape(const std::string& value)
        {
            std::string result;
            result.reserve(value.size() + 8);
            for (const char character : value)
            {
                switch (character)
                {
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result.push_back(character); break;
                }
            }
            return result;
        }

        [[nodiscard]] std::wstring SecurityDescriptorToSddl(PSECURITY_DESCRIPTOR descriptor, DWORD& error)
        {
            LPWSTR text = nullptr;
            if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(
                    descriptor,
                    SDDL_REVISION_1,
                    DACL_SECURITY_INFORMATION,
                    &text,
                    nullptr))
            {
                error = GetLastError();
                return {};
            }
            const std::wstring result(text);
            LocalFree(text);
            error = ERROR_SUCCESS;
            return result;
        }

        [[nodiscard]] double ElapsedMilliseconds(
            const std::chrono::steady_clock::time_point origin,
            const std::chrono::steady_clock::time_point mark) noexcept
        {
            if (mark == std::chrono::steady_clock::time_point{})
            {
                return -1.0;
            }
            return std::chrono::duration<double, std::milli>(mark - origin).count();
        }

        [[nodiscard]] ipc::IndexState ToIndexState(index::FastIndexHealth health) noexcept
        {
            switch (health)
            {
            case FastIndexHealth::Healthy: return ipc::IndexState::Healthy;
            case FastIndexHealth::CatchingUp: return ipc::IndexState::CatchingUp;
            case FastIndexHealth::RebuildRequired: return ipc::IndexState::RebuildRequired;
            case FastIndexHealth::Rebuilding: return ipc::IndexState::Rebuilding;
            case FastIndexHealth::Unavailable: return ipc::IndexState::Unavailable;
            case FastIndexHealth::Corrupted: return ipc::IndexState::Corrupted;
            default: return ipc::IndexState::Unknown;
            }
        }

        [[nodiscard]] bool QueryTokenIdentity(HANDLE token_handle, DWORD& error)
        {
            DWORD bytes = 0;
            (void)GetTokenInformation(token_handle, TokenUser, nullptr, 0, &bytes);
            if (bytes == 0)
            {
                error = GetLastError();
                return false;
            }
            std::vector<std::byte> buffer(bytes);
            if (!GetTokenInformation(token_handle, TokenUser, buffer.data(), bytes, &bytes))
            {
                error = GetLastError();
                return false;
            }
            error = ERROR_SUCCESS;
            return true;
        }

        [[nodiscard]] bool GetClientIdentity(
            HANDLE pipe,
            ULONG& process_id,
            DWORD& error,
            std::string& failure_api,
            bool& fatal_security_failure,
            PipeImpersonationTelemetry telemetry)
        {
            process_id = 0;
            error = ERROR_SUCCESS;
            failure_api.clear();
            fatal_security_failure = false;
            if (!GetNamedPipeClientProcessId(pipe, &process_id) || process_id == 0)
            {
                error = GetLastError();
                failure_api = "GetNamedPipeClientProcessId";
                return false;
            }

            PipeImpersonationState impersonation_state;
            {
                ScopedPipeImpersonation impersonation(pipe, impersonation_state, telemetry);
                if (!impersonation.Succeeded())
                {
                    error = impersonation.ImpersonateError();
                    failure_api = "ImpersonateNamedPipeClient";
                    return false;
                }

                bool token_identity_valid = false;
                DWORD token_error = ERROR_SUCCESS;
                const char* token_failure_api = "TokenIdentityQuery";
                try
                {
                    HANDLE token_handle = INVALID_HANDLE_VALUE;
                    const bool token_opened = OpenThreadToken(
                        GetCurrentThread(), TOKEN_QUERY, TRUE, &token_handle) != FALSE;
                    if (!token_opened)
                    {
                        token_error = GetLastError();
                        token_failure_api = "OpenThreadToken";
                    }
                    else
                    {
                        blink::core::UniqueHandle token(token_handle);
                        token_identity_valid = QueryTokenIdentity(token.get(), token_error);
                        if (!token_identity_valid)
                        {
                            token_failure_api = "GetTokenInformation";
                        }
                    }
                }
                catch (...)
                {
                    token_error = ERROR_EXCEPTION_IN_SERVICE;
                    token_failure_api = "TokenIdentityQueryException";
                }

                if (!impersonation.RevertNow())
                {
                    fatal_security_failure = true;
                    error = impersonation_state.revert_error;
                    failure_api = "RevertToSelf";
                    return false;
                }
                if (!token_identity_valid)
                {
                    error = token_error;
                    failure_api = token_failure_api;
                    return false;
                }
                return true;
            }
        }

        [[nodiscard]] bool CreateDataRoot(const std::wstring& root, DWORD& error)
        {
            std::error_code filesystem_error;
            std::filesystem::create_directories(std::filesystem::path(root) / L"index", filesystem_error);
            if (filesystem_error)
            {
                error = static_cast<DWORD>(filesystem_error.value());
                return false;
            }
            error = ERROR_SUCCESS;
            return true;
        }

        [[nodiscard]] std::wstring StorePathFor(const std::wstring& data_root, const std::wstring& volume_root)
        {
            const std::wstring drive = Lower(volume_root.substr(0, 1));
            return (std::filesystem::path(data_root) / L"index" / (drive + L".blink5")).wstring();
        }

        [[nodiscard]] std::uint64_t FileSize(const std::wstring& path)
        {
            CREATEFILE3_EXTENDED_PARAMETERS parameters{};
            parameters.dwSize = sizeof(parameters);
            parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            blink::core::UniqueHandle file(CreateFile3(
                path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                OPEN_EXISTING,
                &parameters));
            if (!file)
            {
                return 0;
            }
            LARGE_INTEGER size{};
            return GetFileSizeEx(file.get(), &size) && size.QuadPart >= 0
                ? static_cast<std::uint64_t>(size.QuadPart)
                : 0;
        }

        ServiceRuntime* g_runtime = nullptr;
        SERVICE_STATUS_HANDLE g_service_status_handle = nullptr;

        void SetServiceState(DWORD state, DWORD win32_error = ERROR_SUCCESS);
        VOID WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
        DWORD WINAPI ServiceHandler(DWORD control, DWORD event_type, LPVOID event_data, LPVOID context);
        BOOL WINAPI ConsoleHandler(DWORD control);
    }

    class ServiceRuntime
    {
    public:
        explicit ServiceRuntime(
            std::wstring data_root,
            std::wstring telemetry_root = {},
            std::wstring authorized_client_sid = {},
            std::wstring service_sid = {},
            bool hardened_security = false)
            : data_root_(std::filesystem::absolute(data_root).wstring()),
              telemetry_root_(telemetry_root.empty() ? L"" : std::filesystem::absolute(telemetry_root).wstring()),
              authorized_client_sid_(std::move(authorized_client_sid)),
              service_sid_(std::move(service_sid)),
              hardened_security_(hardened_security) {}
        ~ServiceRuntime()
        {
            Stop();
        }

        [[nodiscard]] int RunConsole()
        {
            g_runtime = this;
            if (!Start(false))
            {
                return static_cast<int>(last_error_);
            }
            SetConsoleCtrlHandler(ConsoleHandler, TRUE);
            WaitForSingleObject(stop_event_, INFINITE);
            SetConsoleCtrlHandler(ConsoleHandler, FALSE);
            Stop();
            return static_cast<int>(last_error_);
        }

        [[nodiscard]] int RunAsService()
        {
            g_runtime = this;
            SERVICE_TABLE_ENTRYW table[] = {
                { const_cast<LPWSTR>(kServiceName), ServiceMain },
                { nullptr, nullptr },
            };
            if (!StartServiceCtrlDispatcherW(table))
            {
                return static_cast<int>(GetLastError());
            }
            return static_cast<int>(last_error_);
        }

        void OnServiceMain()
        {
            g_service_status_handle = RegisterServiceCtrlHandlerExW(kServiceName, ServiceHandler, this);
            if (g_service_status_handle == nullptr)
            {
                last_error_ = GetLastError();
                return;
            }
            SetServiceState(SERVICE_START_PENDING);
            if (!Start(true))
            {
                SetServiceState(SERVICE_STOPPED, last_error_);
                return;
            }
            SetServiceState(SERVICE_RUNNING);
            WaitForSingleObject(stop_event_, INFINITE);
            SetServiceState(SERVICE_STOP_PENDING);
            Stop();
            SetServiceState(SERVICE_STOPPED, last_error_);
        }

        void RequestStop()
        {
            if (stop_event_ != nullptr)
            {
                SetEvent(stop_event_);
            }
            std::shared_ptr<std::atomic_bool> cancel;
            {
                std::scoped_lock lock(state_mutex_);
                cancel = operation_.cancel;
            }
            if (cancel)
            {
                cancel->store(true);
            }
            HANDLE native_thread = INVALID_HANDLE_VALUE;
            {
                std::scoped_lock lock(state_mutex_);
                native_thread = pipe_thread_native_;
            }
            if (!InvalidHandle(native_thread))
            {
                (void)CancelSynchronousIo(native_thread);
            }
        }

        void StopForFatalSecurityCondition(DWORD error) noexcept
        {
            last_error_ = error == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : error;
            scheduler_running_.store(false);
            if (stop_event_ != nullptr)
            {
                SetEvent(stop_event_);
            }
        }

    private:
        struct OperationInfo
        {
            std::uint64_t id{};
            bool running{};
            bool initial{};
            bool loading{};
            std::wstring volume_root;
            std::shared_ptr<std::atomic_bool> cancel;
            std::shared_ptr<ProgressState> progress;
            FastIndexStats stats;
            DWORD error{};
        };

        struct ClientSession
        {
            std::uint64_t id{};
            HANDLE pipe{ nullptr };
            std::thread thread;
            std::atomic<HANDLE> cancel_thread{ nullptr };
            std::atomic_bool cancel_requested{ false };
            std::atomic_bool finished{ false };
        };

        void JoinClientSession(const std::shared_ptr<ClientSession>& session)
        {
            if (session == nullptr)
            {
                return;
            }
            if (session->thread.joinable())
            {
                session->thread.join();
            }
            const HANDLE cancel_thread = session->cancel_thread.exchange(nullptr);
            if (!InvalidHandle(cancel_thread))
            {
                CloseHandle(cancel_thread);
            }
        }

        void ReapCompletedClientSessions()
        {
            std::vector<std::shared_ptr<ClientSession>> completed;
            {
                std::scoped_lock lock(client_sessions_mutex_);
                for (auto iterator = client_sessions_.begin();
                     iterator != client_sessions_.end();)
                {
                    if (!iterator->second->finished.load(std::memory_order_acquire))
                    {
                        ++iterator;
                        continue;
                    }
                    completed.push_back(std::move(iterator->second));
                    iterator = client_sessions_.erase(iterator);
                }
            }
            for (const auto& session : completed)
            {
                JoinClientSession(session);
            }
        }

        void CancelClientSessions()
        {
            std::vector<std::shared_ptr<ClientSession>> sessions;
            {
                std::scoped_lock lock(client_sessions_mutex_);
                for (const auto& [session_id, session] : client_sessions_)
                {
                    (void)session_id;
                    sessions.push_back(session);
                }
            }

            for (const auto& session : sessions)
            {
                session->cancel_requested.store(true, std::memory_order_release);
                const HANDLE cancel_thread = session->cancel_thread.load(std::memory_order_acquire);
                if (InvalidHandle(cancel_thread))
                {
                    continue;
                }
                const BOOL canceled = CancelSynchronousIo(cancel_thread);
                const DWORD error = canceled ? ERROR_SUCCESS : GetLastError();
                WriteIpcConnectionEvent(
                    "client_session_read_cancel_return",
                    canceled != FALSE,
                    error,
                    0,
                    0,
                    static_cast<std::uint32_t>(session->id));
            }
        }

        void JoinAllClientSessions()
        {
            std::map<std::uint64_t, std::shared_ptr<ClientSession>> sessions;
            {
                std::scoped_lock lock(client_sessions_mutex_);
                sessions.swap(client_sessions_);
            }
            for (const auto& [session_id, session] : sessions)
            {
                (void)session_id;
                JoinClientSession(session);
            }
        }

        void StartClientSession(HANDLE pipe)
        {
            const auto session = std::make_shared<ClientSession>();
            session->id = ++next_client_session_id_;
            session->pipe = pipe;
            try
            {
                session->thread = std::thread(
                    [this, session]()
                    {
                        HANDLE cancel_thread = nullptr;
                        if (!DuplicateHandle(
                                GetCurrentProcess(),
                                GetCurrentThread(),
                                GetCurrentProcess(),
                                &cancel_thread,
                                THREAD_ALL_ACCESS,
                                FALSE,
                                0))
                        {
                            const DWORD error = GetLastError();
                            WriteIpcConnectionEvent(
                                "client_session_cancel_handle_failed",
                                false,
                                error,
                                0,
                                0,
                                static_cast<std::uint32_t>(session->id));
                        }
                        else
                        {
                            session->cancel_thread.store(
                                cancel_thread,
                                std::memory_order_release);
                        }

                        if (!InvalidHandle(cancel_thread) &&
                            !session->cancel_requested.load(std::memory_order_acquire))
                        {
                            try
                            {
                                HandleClient(session->pipe);
                            }
                            catch (...)
                            {
                                WriteIpcConnectionEvent(
                                    "client_handler_exception",
                                    false,
                                    ERROR_GEN_FAILURE,
                                    0,
                                    0,
                                    static_cast<std::uint32_t>(session->id));
                            }
                        }
                        (void)DisconnectNamedPipe(session->pipe);
                        CloseHandle(session->pipe);
                        session->pipe = nullptr;
                        session->finished.store(true, std::memory_order_release);
                    });
            }
            catch (...)
            {
                CloseHandle(pipe);
                last_error_ = ERROR_NOT_ENOUGH_MEMORY;
                return;
            }

            try
            {
                std::scoped_lock lock(client_sessions_mutex_);
                client_sessions_.emplace(session->id, session);
            }
            catch (...)
            {
                session->cancel_requested.store(true, std::memory_order_release);
                const HANDLE cancel_thread = session->cancel_thread.load(std::memory_order_acquire);
                if (!InvalidHandle(cancel_thread))
                {
                    (void)CancelSynchronousIo(cancel_thread);
                }
                JoinClientSession(session);
                last_error_ = ERROR_NOT_ENOUGH_MEMORY;
                return;
            }
            WriteIpcConnectionEvent(
                "client_session_owned",
                true,
                ERROR_SUCCESS,
                0,
                0,
                static_cast<std::uint32_t>(session->id));
        }

        [[nodiscard]] bool Start(bool service_mode)
        {
            service_mode_ = service_mode;
            DWORD error = ERROR_SUCCESS;
            if (!CreateDataRoot(data_root_, error))
            {
                last_error_ = error;
                return false;
            }
            index_->SetInternalExcludedRoots({ data_root_ });
            if (!telemetry_root_.empty())
            {
                std::error_code telemetry_error;
                std::filesystem::create_directories(telemetry_root_, telemetry_error);
                if (telemetry_error)
                {
                    last_error_ = static_cast<DWORD>(telemetry_error.value());
                    return false;
                }
            }
            if (!InitializePipeSecurity(error))
            {
                last_error_ = error;
                return false;
            }
            stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (stop_event_ == nullptr)
            {
                last_error_ = GetLastError();
                return false;
            }
            {
                std::scoped_lock lock(state_mutex_);
                service_state_ = ipc::ServiceState::Running;
            }
            pipe_thread_ = std::thread(&ServiceRuntime::PipeLoop, this);
            scheduler_running_.store(true);
            scheduler_thread_ = std::thread(&ServiceRuntime::SchedulerLoop, this);
            return true;
        }

        void Stop()
        {
            RequestStop();
            scheduler_running_.store(false);
            if (scheduler_thread_.joinable())
            {
                scheduler_thread_.join();
            }
            if (pipe_thread_.joinable())
            {
                pipe_thread_.join();
            }
            CancelClientSessions();
            JoinAllClientSessions();
            if (operation_thread_.joinable())
            {
                operation_thread_.join();
            }
            if (stop_event_ != nullptr)
            {
                CloseHandle(stop_event_);
                stop_event_ = nullptr;
            }
            g_runtime = nullptr;
        }

        void SchedulerLoop()
        {
            while (scheduler_running_.load())
            {
                if (stop_event_ != nullptr && WaitForSingleObject(stop_event_, 1000) != WAIT_TIMEOUT)
                {
                    break;
                }

                std::wstring volume;
                std::shared_ptr<FastIndex> current;
                {
                    std::scoped_lock lock(state_mutex_);
                    if (!scheduler_running_.load() || operation_.running || resident_indexes_.empty())
                    {
                        continue;
                    }
                    auto next = resident_indexes_.upper_bound(scheduler_volume_);
                    if (next == resident_indexes_.end()) { next = resident_indexes_.begin(); }
                    volume = next->first;
                    current = next->second;
                    scheduler_volume_ = volume;
                }

                const FastIndexSnapshot snapshot = current->Snapshot();
                if (!snapshot.root_identity.valid) continue;

                const JournalState journal = QueryUsnJournal(volume);
                if (!journal.available) continue;

                if (journal.journal_id == snapshot.journal_id &&
                    snapshot.last_usn >= journal.first_usn &&
                    snapshot.last_usn < journal.next_usn)
                {
                    ipc::Response dummy;
                    StartOperation(volume, false, dummy, current);
                }
            }
        }

        void WriteOperationTelemetry(
            std::uint64_t operation_id,
            bool initial,
            const FastIndexStats& stats,
            DWORD error,
            double operation_seconds,
            double save_seconds) const
        {
            if (telemetry_root_.empty())
            {
                return;
            }

            const std::filesystem::path path = std::filesystem::path(telemetry_root_) /
                (L"operation-" + std::to_wstring(operation_id) + L".json");
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                return;
            }
            std::string failure_stage;
            for (const wchar_t character : stats.failure_stage)
            {
                failure_stage.push_back(character >= 0 && character <= 0x7F ? static_cast<char>(character) : '?');
            }
            output << "{\n"
                   << "  \"operation_id\": " << operation_id << ",\n"
                   << "  \"initial\": " << (initial ? "true" : "false") << ",\n"
                   << "  \"error\": " << error << ",\n"
                   << "  \"records_seen\": " << stats.records_seen << ",\n"
                   << "  \"mft_records\": " << stats.mft_records << ",\n"
                   << "  \"usn_records\": " << stats.usn_records << ",\n"
                   << "  \"user_mutations_applied\": " << stats.user_mutations_applied << ",\n"
                   << "  \"files\": " << stats.files << ",\n"
                   << "  \"directories\": " << stats.directories << ",\n"
                   << "  \"links\": " << stats.links << ",\n"
                   << "  \"unique_files\": " << stats.unique_files << ",\n"
                   << "  \"index_bytes\": " << stats.index_bytes << ",\n"
                   << "  \"resident_bytes\": " << stats.resident_bytes << ",\n"
                   << "  \"build_seconds\": " << stats.build_seconds << ",\n"
                   << "  \"mft_enumeration_seconds\": " << stats.mft_enumeration_seconds << ",\n"
                   << "  \"parent_tree_build_seconds\": " << stats.parent_tree_build_seconds << ",\n"
                   << "  \"usn_read_seconds\": " << stats.usn_read_seconds << ",\n"
                   << "  \"save_seconds\": " << save_seconds << ",\n"
                   << "  \"catchup_start_usn\": " << stats.catchup_start_usn << ",\n"
                   << "  \"catchup_target_usn\": " << stats.catchup_target_usn << ",\n"
                   << "  \"catchup_completed_usn\": " << stats.catchup_completed_usn << ",\n"
                   << "  \"catchup_batch_count\": " << stats.catchup_batch_count << ",\n"
                   << "  \"operation_seconds\": " << operation_seconds << ",\n"
                   << "  \"failure_stage\": \"" << failure_stage << "\"\n"
                   << "}\n";
        }

        void WriteIpcRejectionTelemetry(
            const std::string& failure_api,
            DWORD error,
            ULONG process_id) const
        {
            if (telemetry_root_.empty())
            {
                return;
            }
            const std::filesystem::path path = std::filesystem::path(telemetry_root_) / L"ipc-rejections.log";
            std::ofstream output(path, std::ios::app);
            if (!output)
            {
                return;
            }
            output << "api=" << failure_api
                   << " win32_error=" << error
                   << " client_pid=" << process_id
                   << "\n";
        }

        void WriteIpcConnectionEvent(
            const char* stage,
            bool success,
            DWORD error = ERROR_SUCCESS,
            ULONG client_process_id = 0,
            std::uint64_t request_id = 0,
            std::uint32_t request_type = 0,
            const char* detail = "") const
        {
            if (telemetry_root_.empty())
            {
                return;
            }
            const std::filesystem::path path = std::filesystem::path(telemetry_root_) /
                L"ipc-connection.jsonl";
            std::scoped_lock lock(telemetry_mutex_);
            std::ofstream output(path, std::ios::binary | std::ios::app);
            if (!output)
            {
                return;
            }
            const std::uint64_t sequence = ipc_trace_sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
            output << "{\"sequence\":" << sequence
                   << ",\"tick_count\":" << GetTickCount64()
                   << ",\"process_id\":" << GetCurrentProcessId()
                   << ",\"thread_id\":" << GetCurrentThreadId()
                   << ",\"stage\":\"" << JsonEscape(stage == nullptr ? "" : stage) << "\""
                   << ",\"success\":" << (success ? "true" : "false")
                   << ",\"win32_error\":" << error
                   << ",\"client_process_id\":" << client_process_id
                   << ",\"request_id\":" << request_id
                   << ",\"request_type\":" << request_type
                   << ",\"detail\":\"" << JsonEscape(detail == nullptr ? "" : detail) << "\"}\n";
            output.flush();
        }

        void WriteSearchTimingEvent(
            std::uint64_t request_id,
            const char* stage,
            double elapsed_ms,
            std::uint64_t result_count) const
        {
            if (telemetry_root_.empty() || elapsed_ms < 0.0)
            {
                return;
            }
            const std::filesystem::path path = std::filesystem::path(telemetry_root_) / L"search-timing.jsonl";
            std::scoped_lock lock(telemetry_mutex_);
            std::ofstream output(path, std::ios::binary | std::ios::app);
            if (!output)
            {
                return;
            }
            output << std::fixed << std::setprecision(3)
                   << "{\"request_id\":" << request_id
                   << ",\"stage\":\"" << stage
                   << "\",\"elapsed_ms\":" << elapsed_ms
                   << ",\"result_count\":" << result_count
                   << "}\n";
        }

        [[nodiscard]] bool InitializePipeSecurity(DWORD& error)
        {
            if (authorized_client_sid_.empty())
            {
                if (service_mode_)
                {
                    error = ERROR_INVALID_SID;
                    return false;
                }
                authorized_client_sid_ = CurrentProcessUserSid();
            }
            if (!IsSidString(authorized_client_sid_))
            {
                error = ERROR_INVALID_SID;
                return false;
            }

            if (!service_sid_.empty() && !IsSidString(service_sid_))
            {
                error = ERROR_INVALID_SID;
                return false;
            }
            if (service_sid_.empty())
            {
                DWORD lookup_error = ERROR_SUCCESS;
                service_sid_ = LookupAccountSidString(kServiceSidName, lookup_error);
                if (service_sid_.empty() && hardened_security_)
                {
                    error = lookup_error == ERROR_SUCCESS ? ERROR_NONE_MAPPED : lookup_error;
                    return false;
                }
            }

            pipe_sddl_ = L"D:P(A;;0x";
            wchar_t rights[16]{};
            (void)swprintf_s(rights, 16, L"%08X", kPipeServerRights);
            pipe_sddl_ += rights;
            pipe_sddl_ += L";;;SY)";
            if (!service_sid_.empty())
            {
                pipe_sddl_ += L"(A;;0x";
                (void)swprintf_s(rights, 16, L"%08X", kPipeServerRights);
                pipe_sddl_ += rights;
                pipe_sddl_ += L";;;" + service_sid_ + L")";
            }
            pipe_sddl_ += L"(A;;0x";
            (void)swprintf_s(rights, 16, L"%08X", kPipeClientRights);
            pipe_sddl_ += rights;
            pipe_sddl_ += L";;;" + authorized_client_sid_ + L")";
            error = ERROR_SUCCESS;
            return true;
        }

        void WritePipeSecurityTelemetry(HANDLE pipe)
        {
            std::scoped_lock lock(telemetry_mutex_);
            if (pipe_security_telemetry_written_)
            {
                return;
            }
            pipe_security_telemetry_written_ = true;

            PSECURITY_DESCRIPTOR descriptor = nullptr;
            const DWORD query_result = GetSecurityInfo(
                pipe,
                SE_KERNEL_OBJECT,
                DACL_SECURITY_INFORMATION,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                &descriptor);
            DWORD sddl_error = query_result;
            std::wstring actual_sddl;
            if (query_result == ERROR_SUCCESS)
            {
                actual_sddl = SecurityDescriptorToSddl(descriptor, sddl_error);
            }
            if (descriptor != nullptr)
            {
                LocalFree(descriptor);
            }

            std::error_code directory_error;
            const std::filesystem::path security_directory = std::filesystem::path(data_root_) / L"security";
            std::filesystem::create_directories(security_directory, directory_error);
            if (directory_error)
            {
                return;
            }

            const auto write = [&](const std::filesystem::path& path)
            {
                std::ofstream output(path, std::ios::binary | std::ios::trunc);
                if (!output)
                {
                    return;
                }
                output << "{\n"
                       << "  \"service_name\": \"Blink.IndexService\",\n"
                       << "  \"service_sid_name\": \"NT SERVICE\\\\Blink.IndexService\",\n"
                       << "  \"service_sid\": \"" << JsonEscape(NarrowAscii(service_sid_)) << "\",\n"
                       << "  \"authorized_client_sid\": \"" << JsonEscape(NarrowAscii(authorized_client_sid_)) << "\",\n"
                       << "  \"security_profile\": \""
                       << (hardened_security_ ? "hardened-local-system-candidate" : "single-user-pipe-acl")
                       << "\",\n"
                       << "  \"pipe_reject_remote_clients\": true,\n"
                       << "  \"pipe_client_rights_hex\": \"0x";
                wchar_t rights[16]{};
                (void)swprintf_s(rights, 16, L"%08X", kPipeClientRights);
                output << NarrowAscii(rights) << "\",\n"
                       << "  \"pipe_server_rights_hex\": \"0x";
                (void)swprintf_s(rights, 16, L"%08X", kPipeServerRights);
                output << NarrowAscii(rights) << "\",\n"
                       << "  \"expected_pipe_sddl\": \"" << JsonEscape(NarrowAscii(pipe_sddl_)) << "\",\n"
                       << "  \"descriptor_query\": \"" << (query_result == ERROR_SUCCESS ? "PASS" : "FAIL") << "\",\n"
                       << "  \"descriptor_query_error\": " << query_result << ",\n"
                       << "  \"descriptor_sddl_error\": " << sddl_error << ",\n"
                       << "  \"actual_pipe_sddl\": \"" << JsonEscape(NarrowAscii(actual_sddl)) << "\"\n"
                       << "}\n";
            };
            write(security_directory / L"pipe-security.json");
            if (!telemetry_root_.empty())
            {
                write(std::filesystem::path(telemetry_root_) / L"pipe-security.json");
            }
        }

        void PipeLoop()
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
                std::scoped_lock lock(state_mutex_);
                pipe_thread_native_ = native_thread;
            }

            while (WaitForSingleObject(stop_event_, 0) == WAIT_TIMEOUT)
            {
                ReapCompletedClientSessions();
                PSECURITY_DESCRIPTOR descriptor = nullptr;
                SECURITY_ATTRIBUTES security{};
                security.nLength = sizeof(security);
                if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                        pipe_sddl_.c_str(),
                        SDDL_REVISION_1,
                        &descriptor,
                        nullptr))
                {
                    last_error_ = GetLastError();
                    break;
                }
                security.lpSecurityDescriptor = descriptor;
                const HANDLE pipe = CreateNamedPipeW(
                    ipc::kPipeName,
                    PIPE_ACCESS_DUPLEX,
                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                    16,
                    512 * 1024,
                    512 * 1024,
                    0,
                    &security);
                LocalFree(descriptor);
                const bool pipe_created = !InvalidHandle(pipe);
                const DWORD create_error = pipe_created ? ERROR_SUCCESS : GetLastError();
                WriteIpcConnectionEvent(
                    "create_named_pipe_return",
                    pipe_created,
                    create_error,
                    0,
                    0,
                    0,
                    "PIPE_ACCESS_DUPLEX;PIPE_TYPE_MESSAGE;PIPE_READMODE_MESSAGE;PIPE_WAIT;PIPE_REJECT_REMOTE_CLIENTS;overlapped=false");
                if (!pipe_created)
                {
                    last_error_ = create_error;
                    break;
                }

                WritePipeSecurityTelemetry(pipe);

                WriteIpcConnectionEvent(
                    "connect_named_pipe_enter",
                    true,
                    ERROR_SUCCESS,
                    0,
                    0,
                    0,
                    "blocking=true;overlapped=false");
                const BOOL connect_call = ConnectNamedPipe(pipe, nullptr);
                const DWORD connect_error = GetLastError();
                const bool connected = connect_call || connect_error == ERROR_PIPE_CONNECTED;
                WriteIpcConnectionEvent(
                    "connect_named_pipe_return",
                    connected,
                    connect_call ? ERROR_SUCCESS : connect_error,
                    0,
                    0,
                    0,
                    connect_call ? "SUCCESS" : (connect_error == ERROR_PIPE_CONNECTED ? "ERROR_PIPE_CONNECTED" : "FAILURE"));
                if (!connected)
                {
                    CloseHandle(pipe);
                    if (WaitForSingleObject(stop_event_, 0) != WAIT_TIMEOUT)
                    {
                        break;
                    }
                    last_error_ = connect_error;
                    continue;
                }

                ULONG client_process_id = 0;
                DWORD identity_error = ERROR_SUCCESS;
                std::string identity_failure_api;
                bool fatal_security_failure = false;
                const PipeImpersonationTelemetry identity_telemetry{
                    [](void* context, const char* stage, bool success, DWORD error) noexcept
                    {
                        try
                        {
                            static_cast<ServiceRuntime*>(context)->WriteIpcConnectionEvent(
                                stage,
                                success,
                                error);
                        }
                        catch (...)
                        {
                            // Security telemetry must not throw through the guard.
                        }
                    },
                    this
                };
                WriteIpcConnectionEvent("client_connected", true);
                WriteIpcConnectionEvent("client_identity_enter", true);
                if (!GetClientIdentity(
                        pipe,
                        client_process_id,
                        identity_error,
                        identity_failure_api,
                        fatal_security_failure,
                        identity_telemetry))
                {
                    WriteIpcConnectionEvent(
                        "client_identity_return",
                        false,
                        identity_error,
                        client_process_id,
                        0,
                        0,
                        identity_failure_api.c_str());
                    if (fatal_security_failure)
                    {
                        WriteIpcConnectionEvent(
                            "fatal_security_condition",
                            false,
                            identity_error,
                            client_process_id,
                            0,
                            0,
                            identity_failure_api.c_str());
                        StopForFatalSecurityCondition(identity_error);
                        CloseHandle(pipe);
                        break;
                    }
                    WriteIpcRejectionTelemetry(identity_failure_api, identity_error, client_process_id);
                    (void)DisconnectNamedPipe(pipe);
                    CloseHandle(pipe);
                    continue;
                }
                WriteIpcConnectionEvent("client_identity_return", true, ERROR_SUCCESS, client_process_id);
                StartClientSession(pipe);
            }
            ReapCompletedClientSessions();
            {
                std::scoped_lock lock(state_mutex_);
                pipe_thread_native_ = INVALID_HANDLE_VALUE;
            }
            if (!InvalidHandle(native_thread))
            {
                CloseHandle(native_thread);
            }
        }

        void HandleClient(HANDLE pipe)
        {
            while (WaitForSingleObject(stop_event_, 0) == WAIT_TIMEOUT)
            {
                ipc::Request request;
                DWORD error = ERROR_SUCCESS;
                WriteIpcConnectionEvent("read_request_enter", true);
                if (!ipc::ReadRequest(pipe, request, &error))
                {
                    WriteIpcConnectionEvent("read_request_return", false, error, 0, 0, 0, "invalid_or_closed_message");
                    break;
                }
                WriteIpcConnectionEvent(
                    "request_header_received",
                    true,
                    ERROR_SUCCESS,
                    0,
                    request.request_id,
                    static_cast<std::uint32_t>(request.type));

                const bool is_search = request.type == ipc::RequestType::Search ||
                    request.type == ipc::RequestType::SearchDirectChildren ||
                    request.type == ipc::RequestType::SearchVisible ||
                    request.type == ipc::RequestType::SearchVisibleDirectChildren ||
                    request.type == ipc::RequestType::SearchWithHidden ||
                    request.type == ipc::RequestType::SearchWithHiddenDirectChildren;
                std::shared_ptr<FastSearchTiming> search_timing;
                std::shared_ptr<std::atomic_bool> stop_search_observer;
                std::shared_ptr<std::atomic_uint32_t> emitted_search_stages;
                std::thread search_observer;
                std::function<void()> observe_search;
                const auto request_received = std::chrono::steady_clock::now();
                if (is_search)
                {
                    search_timing = std::make_shared<FastSearchTiming>(request_received);
                    stop_search_observer = std::make_shared<std::atomic_bool>(false);
                    emitted_search_stages = std::make_shared<std::atomic_uint32_t>(0);

                    WriteSearchTimingEvent(request.request_id, "request_received", 0.0, 0);

                    const auto emit_search_stage = [this, request_id = request.request_id, emitted_search_stages](
                                                        const char* stage,
                                                        std::uint32_t bit,
                                                        std::int64_t elapsed_us,
                                                        std::uint64_t result_count)
                    {
                        if (elapsed_us < 0)
                        {
                            return;
                        }
                        const std::uint32_t previous = emitted_search_stages->fetch_or(bit);
                        if ((previous & bit) == 0)
                        {
                            WriteSearchTimingEvent(
                                request_id,
                                stage,
                                static_cast<double>(elapsed_us) / 1000.0,
                                result_count);
                        }
                    };

                    observe_search = [search_timing, emit_search_stage]()
                    {
                        emit_search_stage(
                            "lock_wait_start",
                            1u << 0,
                            search_timing->lock_wait_start_us.load(std::memory_order_acquire),
                            0);
                        emit_search_stage(
                            "lock_acquired",
                            1u << 1,
                            search_timing->lock_acquired_us.load(std::memory_order_acquire),
                            0);
                        emit_search_stage(
                            "search_start",
                            1u << 2,
                            search_timing->search_start_us.load(std::memory_order_acquire),
                            0);
                        const std::int64_t search_end =
                            search_timing->search_end_us.load(std::memory_order_acquire);
                        emit_search_stage(
                            "search_end",
                            1u << 3,
                            search_end,
                            search_timing->result_count.load(std::memory_order_acquire));
                        emit_search_stage(
                            "result_count",
                            1u << 4,
                            search_end,
                            search_timing->result_count.load(std::memory_order_acquire));
                    };

                    search_observer = std::thread(
                        [stop_search_observer, observe_search]()
                        {
                            while (!stop_search_observer->load(std::memory_order_acquire))
                            {
                                observe_search();
                                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                            }
                            observe_search();
                        });
                }

                ipc::Response response = HandleRequest(request, search_timing.get());
                response.request_id = request.request_id;
                WriteIpcConnectionEvent(
                    "dispatcher_return",
                    true,
                    response.error,
                    0,
                    request.request_id,
                    static_cast<std::uint32_t>(request.type),
                    "request_dispatched");
                if (is_search)
                {
                    stop_search_observer->store(true, std::memory_order_release);
                    if (search_observer.joinable())
                    {
                        search_observer.join();
                    }
                    observe_search();
                }

                ipc::ResponseWriteTiming write_timing;
                if (is_search)
                {
                    write_timing.request_received = request_received;
                }
                WriteIpcConnectionEvent(
                    "response_write_enter",
                    true,
                    ERROR_SUCCESS,
                    0,
                    request.request_id,
                    static_cast<std::uint32_t>(request.type));
                const bool response_written = ipc::WriteResponse(
                        pipe,
                        response,
                        &error,
                        is_search ? &write_timing : nullptr);
                WriteIpcConnectionEvent(
                    "response_write_return",
                    response_written,
                    response_written ? ERROR_SUCCESS : error,
                    0,
                    request.request_id,
                    static_cast<std::uint32_t>(request.type));
                if (!response_written)
                {
                    if (is_search)
                    {
                        WriteSearchTimingEvent(
                            request.request_id,
                            "serialization_start",
                            ElapsedMilliseconds(request_received, write_timing.serialization_start),
                            response.results.size());
                        WriteSearchTimingEvent(
                            request.request_id,
                            "serialization_end",
                            ElapsedMilliseconds(request_received, write_timing.serialization_end),
                            response.results.size());
                        WriteSearchTimingEvent(
                            request.request_id,
                            "pipe_write_start",
                            ElapsedMilliseconds(request_received, write_timing.pipe_write_start),
                            response.results.size());
                        WriteSearchTimingEvent(
                            request.request_id,
                            "pipe_write_end",
                            ElapsedMilliseconds(request_received, write_timing.pipe_write_end),
                            response.results.size());
                    }
                    break;
                }
                if (is_search)
                {
                    WriteSearchTimingEvent(
                        request.request_id,
                        "serialization_start",
                        ElapsedMilliseconds(request_received, write_timing.serialization_start),
                        response.results.size());
                    WriteSearchTimingEvent(
                        request.request_id,
                        "serialization_end",
                        ElapsedMilliseconds(request_received, write_timing.serialization_end),
                        response.results.size());
                    WriteSearchTimingEvent(
                        request.request_id,
                        "pipe_write_start",
                        ElapsedMilliseconds(request_received, write_timing.pipe_write_start),
                        response.results.size());
                    WriteSearchTimingEvent(
                        request.request_id,
                        "pipe_write_end",
                        ElapsedMilliseconds(request_received, write_timing.pipe_write_end),
                        response.results.size());
                }
            }
        }

        [[nodiscard]] ipc::Response BaseResponse(const ipc::Request& request) const
        {
            ipc::Response response;
            response.request_id = request.request_id;
            response.protocol_version = ipc::kProtocolVersion;
            {
                std::scoped_lock lock(state_mutex_);
                response.service_state = service_state_;
                response.operation_id = operation_.id;
            }
            return response;
        }

        [[nodiscard]] bool ValidateRequestVersion(const ipc::Request& request, ipc::Response& response) const
        {
            if (request.protocol_version != ipc::kProtocolVersion)
            {
                response.status = ipc::ResponseStatus::Unsupported;
                response.error = ERROR_REVISION_MISMATCH;
                response.message = L"Unsupported IndexService protocol version";
                return false;
            }
            return true;
        }

        void RememberProgressLocked(
            std::uint64_t operation_id,
            const std::shared_ptr<ProgressState>& progress)
        {
            progress_history_[operation_id] = progress;
            while (progress_history_.size() > 16)
            {
                progress_history_.erase(progress_history_.begin());
            }
        }

        void PublishOperationProgress(
            const std::shared_ptr<ProgressState>& progress,
            std::uint64_t operation_id,
            bool initial,
            FastIndexProgressStage stage,
            const FastIndexStats& stats,
            std::chrono::steady_clock::time_point& next_publication,
            bool force = false) const
        {
            // The callback can run while FastIndex::mutex_ is held.  This
            // path takes only the short ProgressState copy lock and never
            // calls back into FastIndex or state_mutex_.
            if (progress == nullptr)
            {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (!force && now < next_publication)
            {
                return;
            }

            ProgressSnapshot snapshot;
            snapshot.operation_id = operation_id;
            snapshot.initial = initial;
            snapshot.stage = stage == FastIndexProgressStage::EnumeratingMft
                ? ProgressStage::EnumeratingMft
                : ProgressStage::CatchingUpUsn;
            snapshot.index_state = stage == FastIndexProgressStage::EnumeratingMft
                ? ipc::IndexState::Rebuilding
                : ipc::IndexState::CatchingUp;
            snapshot.records_seen = stats.records_seen;
            snapshot.files = stats.files;
            snapshot.directories = stats.directories;
            snapshot.index_bytes = stats.index_bytes;
            if (stage == FastIndexProgressStage::CatchingUpUsn)
            {
                // These values are populated from real journal USNs by
                // FastIndex; MFT enumeration deliberately leaves them zero.
                snapshot.last_usn = stats.catchup_completed_usn;
                snapshot.target_usn = stats.catchup_target_usn;
            }
            snapshot.error = stats.error;
            snapshot.completed = false;
            progress->Publish(snapshot);
            next_publication = now + std::chrono::milliseconds(100);
        }

        void PublishSavingProgress(
            const std::shared_ptr<ProgressState>& progress,
            std::uint64_t operation_id,
            bool initial,
            const FastIndexStats& stats) const
        {
            if (progress == nullptr)
            {
                return;
            }
            ProgressSnapshot snapshot;
            snapshot.operation_id = operation_id;
            snapshot.initial = initial;
            snapshot.stage = ProgressStage::Saving;
            snapshot.index_state = initial ? ipc::IndexState::Rebuilding : ipc::IndexState::CatchingUp;
            snapshot.records_seen = stats.records_seen;
            snapshot.files = stats.files;
            snapshot.directories = stats.directories;
            snapshot.index_bytes = stats.index_bytes;
            snapshot.last_usn = stats.catchup_completed_usn;
            snapshot.target_usn = stats.catchup_target_usn;
            snapshot.error = stats.error;
            snapshot.completed = false;
            progress->Publish(snapshot);
        }

        void PublishFinalProgress(
            const std::shared_ptr<ProgressState>& progress,
            std::uint64_t operation_id,
            bool initial,
            const FastIndexStats& stats,
            const FastIndexSnapshot& index_snapshot,
            bool success,
            DWORD error) const
        {
            if (progress == nullptr)
            {
                return;
            }
            ProgressSnapshot snapshot;
            snapshot.operation_id = operation_id;
            snapshot.initial = initial;
            snapshot.stage = success ? ProgressStage::Completed : ProgressStage::Failed;
            snapshot.index_state = success ? ipc::IndexState::Healthy : ToIndexState(stats.health);
            snapshot.records_seen = stats.records_seen;
            snapshot.files = stats.files;
            snapshot.directories = stats.directories;
            snapshot.index_bytes = stats.index_bytes;
            snapshot.last_usn = index_snapshot.last_usn;
            snapshot.target_usn = stats.catchup_target_usn;
            snapshot.error = error;
            snapshot.completed = true;
            progress->Publish(snapshot);
        }

        [[nodiscard]] bool FillRunningOperationResponse(ipc::Response& response,
            const std::wstring& requested_volume) const
        {
            std::shared_ptr<ProgressState> progress;
            {
                std::scoped_lock lock(state_mutex_);
                if (!operation_.running)
                {
                    return false;
                }
                if (Lower(operation_.volume_root) != Lower(requested_volume) &&
                    resident_indexes_.contains(Lower(requested_volume)))
                {
                    return false;
                }
                progress = operation_.progress;
            }
            if (progress == nullptr)
            {
                return false;
            }

            const ProgressSnapshot snapshot = progress->Read();
            response.status = ipc::ResponseStatus::Busy;
            response.error = ERROR_IO_PENDING;
            response.operation_id = snapshot.operation_id;
            response.index_state = snapshot.index_state;
            response.records_seen = snapshot.records_seen;
            response.files = snapshot.files;
            response.directories = snapshot.directories;
            response.index_bytes = snapshot.index_bytes;
            response.last_usn = snapshot.last_usn;
            response.message = L"Index operation is running";
            return true;
        }

        void StartLoadOperation(const std::wstring& normalized)
        {
            const auto loaded = std::make_shared<FastIndex>();
            loaded->SetInternalExcludedRoots({ data_root_ });
            std::thread completed;
            std::uint64_t operation_id = 0;
            std::shared_ptr<ProgressState> progress;
            std::unique_lock lock(state_mutex_);
            if (operation_.running || resident_indexes_.contains(Lower(normalized))) { return; }
            if (operation_thread_.joinable()) { completed = std::move(operation_thread_); }
            operation_id = ++next_operation_id_;
            operation_.id = operation_id;
            operation_.running = true;
            operation_.initial = false;
            operation_.loading = true;
            operation_.volume_root = normalized;
            const auto cancel = std::make_shared<std::atomic_bool>(false);
            operation_.cancel = cancel;
            operation_.error = ERROR_IO_PENDING;
            ProgressSnapshot initial_snapshot;
            initial_snapshot.operation_id = operation_id;
            initial_snapshot.stage = ProgressStage::LoadingStore;
            initial_snapshot.index_state = ipc::IndexState::Rebuilding;
            initial_snapshot.error = ERROR_IO_PENDING;
            progress = std::make_shared<ProgressState>(initial_snapshot);
            operation_.progress = progress;
            RememberProgressLocked(operation_id, progress);
            operation_thread_ = std::thread(
                [this, loaded, cancel, normalized, operation_id, progress]()
                {
                    DWORD load_error = ERROR_SUCCESS;
                    bool ok = loaded->Load(StorePathFor(data_root_, normalized), normalized, &load_error);
                    FastIndexStats stats = loaded->Stats();
                    const FastIndexSnapshot snapshot = loaded->Snapshot();
                    {
                        std::scoped_lock lock(state_mutex_);
                        if (operation_.id == operation_id)
                        {
                            if (cancel->load())
                            {
                                ok = false; load_error = ERROR_CANCELLED;
                                stats.health = FastIndexHealth::RebuildRequired; stats.error = load_error;
                            }
                            if (ok)
                            {
                                resident_indexes_.insert_or_assign(Lower(normalized), loaded);
                                index_ = loaded;
                            }
                            operation_.running = false;
                            operation_.stats = stats;
                            operation_.error = ok ? ERROR_SUCCESS : load_error;
                            operation_.cancel.reset();
                        }
                    }
                    PublishFinalProgress(progress, operation_id, false, stats, snapshot, ok,
                        ok ? ERROR_SUCCESS : load_error);
                });
            lock.unlock();
            if (completed.joinable()) { completed.detach(); }
        }

        [[nodiscard]] bool EnsureLoaded(const std::wstring& volume_root, ipc::Response& response,
            std::shared_ptr<FastIndex>& loaded)
        {
            const std::wstring normalized = NormalizeVolumeRoot(volume_root);
            if (normalized.empty())
            {
                response.status = ipc::ResponseStatus::InvalidRequest;
                response.error = ERROR_INVALID_NAME;
                response.message = L"Only a local drive volume root is accepted";
                response.index_state = ipc::IndexState::Unavailable;
                return false;
            }
            if (FillRunningOperationResponse(response, normalized)) { return false; }
            {
                std::scoped_lock lock(state_mutex_);
                const auto resident = resident_indexes_.find(Lower(normalized));
                if (resident != resident_indexes_.end() &&
                    (!operation_.running || Lower(operation_.volume_root) != Lower(normalized)))
                {
                    loaded = resident->second;
                    return true;
                }
                if (operation_.running)
                {
                    response.status = ipc::ResponseStatus::Busy;
                    response.error = ERROR_IO_PENDING;
                    response.operation_id = operation_.id;
                    response.message = L"Index operation is running";
                    response.index_state = operation_.initial || operation_.loading
                        ? ipc::IndexState::Rebuilding : ipc::IndexState::CatchingUp;
                    return false;
                }
                if (operation_.loading && Lower(operation_.volume_root) == Lower(normalized) &&
                    (operation_.error == ERROR_CRC || operation_.error == ERROR_INVALID_DATA || operation_.error == ERROR_VOLUME_NOT_SUPPORTED))
                {
                    response.status = ipc::ResponseStatus::Failed;
                    response.error = operation_.error;
                    response.index_state = ToIndexState(operation_.stats.health);
                    response.message = L"The requested volume store failed to load; initial indexing is required";
                    return false;
                }
            }
            const std::wstring store_path = StorePathFor(data_root_, normalized);
            DWORD validate_error = ERROR_SUCCESS;
            if (!FastIndex::ValidateStoreFile(store_path, &validate_error))
            {
                const bool missing = validate_error == ERROR_FILE_NOT_FOUND || validate_error == ERROR_PATH_NOT_FOUND;
                response.status = missing ? ipc::ResponseStatus::NotFound : ipc::ResponseStatus::Failed;
                response.error = validate_error;
                response.index_state = validate_error == ERROR_INVALID_DATA || validate_error == ERROR_CRC
                    ? ipc::IndexState::Corrupted : ipc::IndexState::Unavailable;
                response.message = L"The requested volume store is not available or corrupt; initial indexing is required";
                return false;
            }
            StartLoadOperation(normalized);
            response.status = ipc::ResponseStatus::Busy;
            response.error = ERROR_IO_PENDING;
            response.index_state = ipc::IndexState::Rebuilding;
            response.message = L"Fast index store is loading in background";
            return false;
        }

        [[nodiscard]] ipc::IndexState RefreshIndexState(const std::wstring& volume_root, ipc::Response& response,
            const std::shared_ptr<FastIndex>& loaded)
        {
            {
                std::scoped_lock lock(state_mutex_);
                if (operation_.running && Lower(operation_.volume_root) == Lower(NormalizeVolumeRoot(volume_root)))
                {
                    response.error = ERROR_IO_PENDING;
                    response.operation_id = operation_.id;
                    return operation_.initial || operation_.loading ? ipc::IndexState::Rebuilding : ipc::IndexState::CatchingUp;
                }
            }
            const FastIndexSnapshot snapshot = loaded->Snapshot();
            if (!snapshot.root_identity.valid)
            {
                response.error = ERROR_INVALID_STATE;
                return ipc::IndexState::RebuildRequired;
            }
            const JournalState journal = QueryUsnJournal(volume_root);
            if (!journal.available)
            {
                response.error = journal.error;
                return ipc::IndexState::Unavailable;
            }
            response.journal_id = journal.journal_id;
            response.last_usn = snapshot.last_usn;
            if (journal.journal_id != snapshot.journal_id ||
                snapshot.last_usn < journal.first_usn ||
                (journal.lowest_valid_usn > 0 && snapshot.last_usn < journal.lowest_valid_usn))
            {
                response.error = ERROR_JOURNAL_ENTRY_DELETED;
                return ipc::IndexState::RebuildRequired;
            }
            response.error = snapshot.error;
            return ToIndexState(snapshot.health);
        }

        void CopyStatsToResponse(ipc::Response& response, std::shared_ptr<FastIndex> loaded = {}) const
        {
            if (!loaded) { std::scoped_lock lock(state_mutex_); loaded = index_; }
            const ipc::IndexState requested_state = response.index_state;
            const FastIndexStats stats = loaded->Stats();
            response.records_seen = stats.records_seen;
            response.files = stats.files;
            response.directories = stats.directories;
            response.index_bytes = stats.index_bytes;
            response.last_usn = loaded->Snapshot().last_usn;
            if (requested_state == ipc::IndexState::Unknown || requested_state == ipc::IndexState::Healthy)
            {
                response.index_state = ToIndexState(stats.health);
            }
        }

        [[nodiscard]] ipc::Response HandleRequest(
            const ipc::Request& request,
            FastSearchTiming* search_timing = nullptr)
        {
            ipc::Response response = BaseResponse(request);
            const bool protocol_valid = ValidateRequestVersion(request, response);
            WriteIpcConnectionEvent(
                "protocol_validation",
                protocol_valid,
                protocol_valid ? ERROR_SUCCESS : response.error,
                0,
                request.request_id,
                static_cast<std::uint32_t>(request.type),
                protocol_valid ? "accepted" : "rejected");
            if (!protocol_valid)
            {
                return response;
            }



            switch (request.type)
            {
            case ipc::RequestType::QueryServiceStatus:
                response.status = ipc::ResponseStatus::Ok;
                response.error = ERROR_SUCCESS;
                response.message = data_root_;
                CopyStatsToResponse(response);
                return response;

            case ipc::RequestType::QueryVolumeStatus:
            {
                std::shared_ptr<FastIndex> loaded;
                if (!EnsureLoaded(request.volume_root, response, loaded))
                {
                    return response;
                }
                response.index_state = RefreshIndexState(request.volume_root, response, loaded);
                if (response.error == ERROR_IO_PENDING)
                {
                    response.status = ipc::ResponseStatus::Busy;
                    response.message = L"An index operation is running for the requested volume";
                    return response;
                }
                response.status = response.index_state == ipc::IndexState::Healthy
                    ? ipc::ResponseStatus::Ok
                    : ipc::ResponseStatus::Failed;
                response.message = ipc::IndexStateName(response.index_state);
                CopyStatsToResponse(response, loaded);
                return response;
            }

            case ipc::RequestType::StartInitialIndex:
                if (NormalizeVolumeRoot(request.volume_root).empty())
                {
                    response.status = ipc::ResponseStatus::InvalidRequest;
                    response.error = ERROR_INVALID_NAME;
                    response.message = L"StartInitialIndex requires a drive volume root";
                    return response;
                }
                StartOperation(request.volume_root, true, response);
                return response;

            case ipc::RequestType::QueryIndexProgress:
                return ProgressResponse(request, response);

            case ipc::RequestType::StopOperation:
                return StopOperation(request, response);

            case ipc::RequestType::Search:
            case ipc::RequestType::SearchDirectChildren:
            case ipc::RequestType::SearchVisible:
            case ipc::RequestType::SearchVisibleDirectChildren:
            case ipc::RequestType::SearchWithHidden:
            case ipc::RequestType::SearchWithHiddenDirectChildren:
                return SearchRequest(request, response, search_timing);

            default:
                response.status = ipc::ResponseStatus::InvalidRequest;
                response.error = ERROR_INVALID_PARAMETER;
                response.message = L"Unknown IndexService command";
                return response;
            }
        }

        void StartOperation(const std::wstring& volume_root, bool initial, ipc::Response& response,
            const std::shared_ptr<FastIndex>& expected = {})
        {
            const std::wstring normalized = NormalizeVolumeRoot(volume_root);
            if (normalized.empty())
            {
                response.status = ipc::ResponseStatus::InvalidRequest;
                response.error = ERROR_INVALID_NAME;
                response.message = L"Invalid local volume root";
                return;
            }

            auto current = initial ? std::make_shared<FastIndex>() : std::shared_ptr<FastIndex>{};
            if (initial) { current->SetInternalExcludedRoots({ data_root_ }); }
            std::thread completed;
            std::shared_ptr<std::atomic_bool> cancel;
            std::shared_ptr<ProgressState> progress;
            std::uint64_t operation_id = 0;
            std::unique_lock state_lock(state_mutex_);
            if (!initial)
            {
                const auto resident = resident_indexes_.find(Lower(normalized));
                if (resident == resident_indexes_.end() || (expected && resident->second != expected)) { return; }
                current = resident->second;
            }
            if (operation_.running)
            {
                response.status = ipc::ResponseStatus::Busy;
                response.error = ERROR_BUSY;
                response.operation_id = operation_.id;
                response.message = L"An index operation is already running";
                response.index_state = ipc::IndexState::CatchingUp;
                return;
            }
            if (operation_thread_.joinable())
            {
                completed = std::move(operation_thread_);
            }
            operation_id = ++next_operation_id_;
            cancel = std::make_shared<std::atomic_bool>(false);
            operation_.id = operation_id;
            operation_.running = true;
            operation_.initial = initial;
            operation_.loading = false;
            operation_.volume_root = normalized;
            operation_.cancel = cancel;
            operation_.stats = {};
            operation_.stats.health = initial ? FastIndexHealth::Rebuilding : FastIndexHealth::CatchingUp;
            operation_.error = ERROR_IO_PENDING;
            ProgressSnapshot initial_snapshot;
            initial_snapshot.operation_id = operation_id;
            initial_snapshot.initial = initial;
            initial_snapshot.stage = initial
                ? ProgressStage::EnumeratingMft
                : ProgressStage::CatchingUpUsn;
            initial_snapshot.index_state = initial
                ? ipc::IndexState::Rebuilding
                : ipc::IndexState::CatchingUp;
            initial_snapshot.error = ERROR_IO_PENDING;
            progress = std::make_shared<ProgressState>(initial_snapshot);
            operation_.progress = progress;
            RememberProgressLocked(operation_id, progress);
            operation_thread_ = std::thread(
                [this, current, normalized, initial, cancel, operation_id, progress]()
                {
                    const auto operation_started = std::chrono::steady_clock::now();
                    auto next_progress_publication = std::chrono::steady_clock::now();
                    FastIndexStats stats;
                    bool success = false;
                    DWORD operation_error = ERROR_SUCCESS;
                    const FastIndexProgressCallback progress_callback =
                        [this, progress, operation_id, initial, &next_progress_publication](
                            FastIndexProgressStage stage,
                            const FastIndexStats& current_stats)
                        {
                            PublishOperationProgress(
                                progress,
                                operation_id,
                                initial,
                                stage,
                                current_stats,
                                next_progress_publication);
                        };
                    if (initial)
                    {
                        success = current->BuildInitial(
                            normalized,
                            [cancel]() { return cancel->load(); },
                            &stats,
                            progress_callback);
                    }
                    else
                    {
                        success = current->CatchUp(
                            normalized,
                            [cancel]() { return cancel->load(); },
                            &stats,
                            progress_callback);
                    }
                    if (cancel->load() && !success)
                    {
                        operation_error = ERROR_CANCELLED;
                    }
                    else if (!success)
                    {
                        operation_error = stats.error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : stats.error;
                    }
                    else
                    {
                        DWORD save_error = ERROR_SUCCESS;
                        const auto save_started = std::chrono::steady_clock::now();
                        PublishSavingProgress(progress, operation_id, initial, stats);
                        if (initial || stats.user_mutations_applied > 0)
                        {
                            if (!current->Save(StorePathFor(data_root_, normalized), &save_error))
                            {
                                success = false;
                                operation_error = save_error;
                                stats.error = save_error;
                                stats.health = FastIndexHealth::Corrupted;
                            }
                        }
                        stats.save_seconds = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - save_started).count();
                        if (success)
                        {
                            stats.index_bytes = FileSize(StorePathFor(data_root_, normalized));
                        }
                    }

                    const FastIndexSnapshot final_snapshot = current->Snapshot();

                    // A pure internal catch-up must still consume its USN
                    // records and advance the in-memory checkpoint, but it
                    // must not write another telemetry file on the indexed
                    // volume. That write would itself create a USN record
                    // and make the scheduler start an endless self-catch-up
                    // loop. Failed operations remain observable.
                    const bool publish_operation_telemetry =
                        initial || operation_error != ERROR_SUCCESS || stats.user_mutations_applied > 0;
                    if (publish_operation_telemetry)
                    {
                        WriteOperationTelemetry(
                            operation_id,
                            initial,
                            stats,
                            operation_error,
                            std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - operation_started).count(),
                            stats.save_seconds);
                    }

                    PublishFinalProgress(
                        progress,
                        operation_id,
                        initial,
                        stats,
                        final_snapshot,
                        success,
                        operation_error);

                    std::scoped_lock lock(state_mutex_);
                    if (operation_.id == operation_id)
                    {
                        operation_.running = false;
                        operation_.stats = stats;
                        operation_.error = operation_error;
                        operation_.cancel.reset();
                        if (success)
                        {
                            resident_indexes_.insert_or_assign(Lower(normalized), current);
                            index_ = current;
                            operation_.stats.health = FastIndexHealth::Healthy;
                        }
                    }
                });
            state_lock.unlock();
            if (completed.joinable()) { completed.detach(); }
            response.status = ipc::ResponseStatus::Ok;
            response.error = ERROR_SUCCESS;
            response.operation_id = operation_id;
            response.index_state = initial ? ipc::IndexState::Rebuilding : ipc::IndexState::CatchingUp;
            response.message = initial ? L"Initial MFT/USN indexing started" : L"USN catch-up started";
        }

        [[nodiscard]] ipc::Response ProgressResponse(const ipc::Request& request, ipc::Response response)
        {
            std::shared_ptr<ProgressState> progress;
            {
                std::scoped_lock lock(state_mutex_);
                if (request.operation_id == 0 || request.operation_id == operation_.id)
                {
                    progress = operation_.progress;
                }
                else if (request.operation_id < operation_.id)
                {
                    const auto it = progress_history_.find(request.operation_id);
                    if (it != progress_history_.end())
                    {
                        progress = it->second;
                    }
                }
            }

            if (progress == nullptr)
            {
                response.status = ipc::ResponseStatus::NotFound;
                response.error = ERROR_NOT_FOUND;
                response.message = L"Operation ID not found";
                return response;
            }

            // The IPC path copies only the independent snapshot.  In
            // particular, it never calls FastIndex::Snapshot/Stats.
            const ProgressSnapshot snapshot = progress->Read();
            response.status = ipc::ResponseStatus::Ok;
            response.error = snapshot.completed ? snapshot.error : ERROR_IO_PENDING;
            response.operation_id = snapshot.operation_id;
            response.index_state = snapshot.index_state;
            response.records_seen = snapshot.records_seen;
            response.files = snapshot.files;
            response.directories = snapshot.directories;
            response.index_bytes = snapshot.index_bytes;
            response.last_usn = snapshot.last_usn;
            response.message = snapshot.completed ? L"Operation finished" : L"Operation running";
            return response;
        }

        [[nodiscard]] ipc::Response StopOperation(const ipc::Request& request, ipc::Response response)
        {
            std::scoped_lock lock(state_mutex_);
            if (!operation_.running || (request.operation_id != 0 && request.operation_id != operation_.id))
            {
                response.status = ipc::ResponseStatus::NotFound;
                response.error = ERROR_NOT_FOUND;
                response.message = L"No matching running operation";
                return response;
            }
            operation_.cancel->store(true);
            response.status = ipc::ResponseStatus::Ok;
            response.error = ERROR_SUCCESS;
            response.operation_id = operation_.id;
            response.index_state = ipc::IndexState::RebuildRequired;
            response.message = L"Cancellation requested";
            return response;
        }

        [[nodiscard]] ipc::Response SearchRequest(
            const ipc::Request& request,
            ipc::Response response,
            FastSearchTiming* search_timing = nullptr)
        {
            const bool direct_children = request.type == ipc::RequestType::SearchDirectChildren ||
                request.type == ipc::RequestType::SearchVisibleDirectChildren ||
                request.type == ipc::RequestType::SearchWithHiddenDirectChildren;
            if (direct_children && request.path_prefix.empty())
            {
                response.status = ipc::ResponseStatus::InvalidRequest;
                response.error = ERROR_INVALID_PARAMETER;
                response.message = L"Direct-child search requires a folder path";
                return response;
            }
            std::shared_ptr<FastIndex> loaded;
            if (!EnsureLoaded(request.volume_root, response, loaded))
            {
                return response;
            }
            response.index_state = RefreshIndexState(request.volume_root, response, loaded);
            if (response.error == ERROR_IO_PENDING)
            {
                response.status = ipc::ResponseStatus::Busy;
                response.message = L"An index operation is running for the requested volume";
                return response;
            }
            if (response.index_state != ipc::IndexState::Healthy && response.index_state != ipc::IndexState::CatchingUp)
            {
                response.status = ipc::ResponseStatus::Failed;
                response.message = ipc::IndexStateName(response.index_state);
                CopyStatsToResponse(response, loaded);
                return response;
            }

            FastSearchOptions options;
            options.text = request.text;
            options.prefix = request.prefix;
            options.extension = request.extension;
            options.path_prefix = request.path_prefix;
            options.direct_children = direct_children;
            if (request.type == ipc::RequestType::SearchVisible ||
                request.type == ipc::RequestType::SearchVisibleDirectChildren)
            { options.excluded_attributes = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM; }
            else if (request.type == ipc::RequestType::SearchWithHidden ||
                request.type == ipc::RequestType::SearchWithHiddenDirectChildren)
            { options.excluded_attributes = FILE_ATTRIBUTE_SYSTEM; }
            options.limit = std::min<std::size_t>(request.limit == 0 ? 1000 : request.limit, 1000);
            const auto hits = loaded->Search(options, search_timing);
            response.results.reserve(hits.size());
            for (const FastSearchResult& hit : hits)
            {
                ipc::SearchHit result;
                result.identity = hit.identity;
                result.parent_identity = hit.parent_identity;
                result.name = hit.name;
                result.full_path = hit.full_path;
                result.attributes = hit.attributes;
                result.is_directory = hit.is_directory;
                result.last_usn = hit.last_usn;
                response.results.push_back(std::move(result));
            }
            response.status = ipc::ResponseStatus::Ok;
            response.error = ERROR_SUCCESS;
            response.message = L"Fast index search completed";
            CopyStatsToResponse(response, loaded);
            const auto observed = loaded->Snapshot();
            response.index_state = ToIndexState(observed.health);
            if (response.index_state != ipc::IndexState::Healthy && response.index_state != ipc::IndexState::CatchingUp)
            {
                response.status = ipc::ResponseStatus::Failed;
                response.error = observed.error ? observed.error : ERROR_NOT_READY;
                response.message = ipc::IndexStateName(response.index_state);
                response.results.clear();
            }
            return response;
        }

        std::wstring data_root_;
        std::wstring telemetry_root_;
        std::wstring authorized_client_sid_;
        std::wstring service_sid_;
        std::wstring pipe_sddl_;
        bool hardened_security_{};
        HANDLE stop_event_{ nullptr };
        HANDLE pipe_thread_native_{ INVALID_HANDLE_VALUE };
        std::thread pipe_thread_;
        std::thread operation_thread_;
        std::thread scheduler_thread_;
        std::atomic<bool> scheduler_running_{ false };
        mutable std::mutex client_sessions_mutex_;
        std::map<std::uint64_t, std::shared_ptr<ClientSession>> client_sessions_;
        std::uint64_t next_client_session_id_{};
        std::shared_ptr<FastIndex> index_{ std::make_shared<FastIndex>() };
        std::map<std::wstring, std::shared_ptr<FastIndex>> resident_indexes_;
        std::wstring scheduler_volume_;
        mutable std::mutex state_mutex_;
        ipc::ServiceState service_state_{ ipc::ServiceState::Starting };
        OperationInfo operation_;
        std::map<std::uint64_t, std::shared_ptr<ProgressState>> progress_history_;
        mutable std::mutex telemetry_mutex_;
        mutable std::atomic_uint64_t ipc_trace_sequence_{};
        bool pipe_security_telemetry_written_{};
        std::uint64_t next_operation_id_{};
        bool service_mode_{};
        DWORD last_error_{ ERROR_SUCCESS };
    };

    namespace
    {
        void SetServiceState(DWORD state, DWORD win32_error)
        {
            if (g_service_status_handle == nullptr)
            {
                return;
            }
            SERVICE_STATUS status{};
            status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
            status.dwCurrentState = state;
            status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
            status.dwWin32ExitCode = win32_error;
            status.dwWaitHint = state == SERVICE_START_PENDING ? 10'000 : 5'000;
            (void)::SetServiceStatus(g_service_status_handle, &status);
        }

        VOID WINAPI ServiceMain(DWORD, LPWSTR*)
        {
            if (g_runtime != nullptr)
            {
                g_runtime->OnServiceMain();
            }
        }

        DWORD WINAPI ServiceHandler(DWORD control, DWORD, LPVOID, LPVOID context)
        {
            auto* runtime = static_cast<ServiceRuntime*>(context);
            if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN)
            {
                runtime->RequestStop();
            }
            return NO_ERROR;
        }

        BOOL WINAPI ConsoleHandler(DWORD control)
        {
            if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT || control == CTRL_CLOSE_EVENT ||
                control == CTRL_SHUTDOWN_EVENT)
            {
                if (g_runtime != nullptr)
                {
                    g_runtime->RequestStop();
                }
                return TRUE;
            }
            return FALSE;
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    bool console = false;
    std::wstring data_root;
    std::wstring telemetry_root;
    std::wstring authorized_client_sid;
    std::wstring service_sid;
    bool hardened_security = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring argument = argv[i];
        if (argument == L"--console")
        {
            console = true;
        }
        else if (argument == L"--data-root" && i + 1 < argc)
        {
            data_root = argv[++i];
        }
        else if (argument == L"--acceptance-telemetry-root" && i + 1 < argc)
        {
            telemetry_root = argv[++i];
        }
        else if (argument == L"--authorized-client-sid" && i + 1 < argc)
        {
            authorized_client_sid = argv[++i];
        }
        else if (argument == L"--service-sid" && i + 1 < argc)
        {
            service_sid = argv[++i];
        }
        else if (argument == L"--hardened-security")
        {
            hardened_security = true;
        }
    }
    if (data_root.empty())
    {
        data_root = blink::index::service::FindDefaultDataRoot();
    }
    blink::index::service::ServiceRuntime runtime(
        std::move(data_root),
        std::move(telemetry_root),
        std::move(authorized_client_sid),
        std::move(service_sid),
        hardened_security);
    return console ? runtime.RunConsole() : runtime.RunAsService();
}
