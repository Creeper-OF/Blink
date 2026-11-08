#include "Blink/App/FileActivationController.h"

#include <winrt/Windows.System.Threading.h>

#include <filesystem>
#include <utility>

#pragma comment(lib, "windowsapp")

namespace blink::app
{
    struct FileActivationController::State
    {
        mutable std::mutex mutex;
        FileActivationSnapshot snapshot;
        bool shutdown{};
        // Borrowed from the worker's UniqueHandle while holding mutex. Cancel
        // may terminate this helper only; target applications are never job-owned.
        HANDLE host_process{};

        [[nodiscard]] bool IsCurrent(const std::uint64_t generation) const noexcept
        {
            std::scoped_lock lock(mutex);
            return !shutdown && generation == snapshot.generation;
        }

        bool Complete(const std::uint64_t generation, const HRESULT error) noexcept
        {
            std::scoped_lock lock(mutex);
            if (shutdown || generation != snapshot.generation)
            {
                return false;
            }
            snapshot.error = error;
            snapshot.stage = SUCCEEDED(error) ?
                FileActivationStage::Dispatched : FileActivationStage::Failed;
            return true;
        }
    };

    FileActivationController::FileActivationController(
        ValidateFunction validate, LaunchFunction launch)
        : state_(std::make_shared<State>()),
          validate_(validate ? std::move(validate) : ValidateFunction(ValidateFileActivation)),
          launch_(std::move(launch))
    {
    }

    FileActivationController::~FileActivationController() noexcept
    {
        Shutdown();
    }

    HRESULT FileActivationController::DispatchHost(
        const std::shared_ptr<State>& state,
        const std::uint64_t generation,
        const blink::core::FileEntry& entry)
    {
        std::wstring module(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, module.data(),
            static_cast<DWORD>(module.size()));
        if (length == 0 || length >= module.size())
        {
            return HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
        }
        module.resize(length);
        const auto host = (std::filesystem::path(module).parent_path() /
            L"Blink.FileActivationHost.exe").wstring();
        auto command = BuildFileActivationCommandLine(host, entry);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &child))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        blink::core::UniqueHandle process(child.hProcess);
        blink::core::UniqueHandle thread(child.hThread);
        {
            std::scoped_lock lock(state->mutex);
            if (state->shutdown || generation != state->snapshot.generation)
            {
                (void)TerminateProcess(process.get(), ERROR_CANCELLED);
                return HRESULT_FROM_WIN32(ERROR_CANCELLED);
            }
            state->host_process = process.get();
        }
        struct HostLease
        {
            std::shared_ptr<State> state;
            HANDLE process;
            ~HostLease()
            {
                std::scoped_lock lock(state->mutex);
                if (state->host_process == process) { state->host_process = nullptr; }
            }
        } lease{ state, process.get() };
        if (ResumeThread(thread.get()) == static_cast<DWORD>(-1))
        {
            const DWORD error = GetLastError();
            (void)TerminateProcess(process.get(), error);
            return HRESULT_FROM_WIN32(error);
        }
        // Only the isolated dispatch helper is awaited. No target-process idle
        // or lifetime wait occurs, and UI close can terminate the owned helper.
        constexpr DWORD kHostDispatchDeadlineMs = 30000;
        const DWORD waited = WaitForSingleObject(process.get(), kHostDispatchDeadlineMs);
        if (waited != WAIT_OBJECT_0)
        {
            const DWORD error = waited == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
            (void)TerminateProcess(process.get(), error);
            return HRESULT_FROM_WIN32(error);
        }
        DWORD exit_code{};
        if (!GetExitCodeProcess(process.get(), &exit_code))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return FileActivationHostResult(exit_code);
    }

    std::uint64_t FileActivationController::Open(
        blink::core::FileEntry entry, PublishCallback publish)
    {
        CancelPending();
        std::scoped_lock work_lock(work_mutex_);
        std::uint64_t generation{};
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->shutdown)
            {
                return 0;
            }
            generation = ++state_->snapshot.generation;
            state_->snapshot.path = entry.full_path;
            state_->snapshot.stage = FileActivationStage::Opening;
            state_->snapshot.error = S_OK;
        }
        const auto state = state_;
        try
        {
            work_ = winrt::Windows::System::Threading::ThreadPool::RunAsync(
                [state, generation, entry = std::move(entry),
                    validate = validate_, launch = launch_, publish](
                    winrt::Windows::Foundation::IAsyncAction const& action) {
                    if (!state->IsCurrent(generation))
                    {
                        return;
                    }
                    HRESULT result = S_OK;
                    try
                    {
                        const DWORD error = validate(entry);
                        // A close/new navigation during metadata I/O must prevent
                        // any not-yet-dispatched external application launch.
                        if (!state->IsCurrent(generation) ||
                            action.Status() == winrt::Windows::Foundation::AsyncStatus::Canceled)
                        {
                            return;
                        }
                        result = error == ERROR_SUCCESS ?
                            (launch ? launch(entry) : DispatchHost(state, generation, entry)) :
                            HRESULT_FROM_WIN32(error);
                    }
                    catch (...)
                    {
                        result = winrt::to_hresult();
                    }
                    if (state->Complete(generation, result) && publish)
                    {
                        publish(generation);
                    }
                });
        }
        catch (...)
        {
            if (state->Complete(generation, winrt::to_hresult()) && publish)
            {
                publish(generation);
            }
        }
        return generation;
    }

    FileActivationSnapshot FileActivationController::Snapshot() const
    {
        std::scoped_lock lock(state_->mutex);
        return state_->snapshot;
    }

    bool FileActivationController::IsCurrent(const std::uint64_t generation) const noexcept
    {
        return state_->IsCurrent(generation);
    }

    void FileActivationController::Cancel(const bool shutdown) noexcept
    {
        std::scoped_lock work_lock(work_mutex_);
        {
            std::scoped_lock lock(state_->mutex);
            state_->shutdown = state_->shutdown || shutdown;
            ++state_->snapshot.generation;
            state_->snapshot.stage = FileActivationStage::Idle;
            state_->snapshot.path.clear();
            state_->snapshot.error = S_OK;
            if (state_->host_process)
            {
                // Only our dispatch helper is stopped; already launched user
                // applications are deliberately outside this ownership scope.
                (void)TerminateProcess(state_->host_process, ERROR_CANCELLED);
            }
        }
        if (work_)
        {
            try { work_.Cancel(); }
            catch (...) { /* The generation gate remains authoritative. */ }
            work_ = nullptr;
        }
    }

    void FileActivationController::CancelPending() noexcept { Cancel(false); }
    void FileActivationController::Shutdown() noexcept { Cancel(true); }
}
