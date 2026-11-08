#include "Blink/App/FolderWatcher.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <utility>

namespace blink::app
{
    struct FolderWatcher::Session
    {
        winrt::handle wake{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        winrt::handle ready{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        std::atomic_bool stopped{};
        void Stop() noexcept
        {
            if (!stopped.exchange(true))
            {
                if (wake) { SetEvent(wake.get()); }
                if (ready) { SetEvent(ready.get()); }
            }
        }
    };

    namespace
    {
        struct SignalOnExit
        {
            HANDLE event;
            ~SignalOnExit() noexcept { SetEvent(event); }
        };
        // Never destroy or reuse an OVERLAPPED/buffer merely because
        // CancelIoEx accepted cancellation. Its completion is authoritative.
        struct PendingRead
        {
            HANDLE directory;
            OVERLAPPED& operation;
            bool active{};
            ~PendingRead() noexcept { Drain(); }
            void Drain() noexcept
            {
                if (!active) { return; }
                (void)CancelIoEx(directory, &operation);
                DWORD bytes{};
                while (!GetOverlappedResult(directory, &operation, &bytes, FALSE) &&
                    GetLastError() == ERROR_IO_INCOMPLETE)
                {
                    // Stop may have signaled the event before cancellation
                    // completed. Reset then recheck, avoiding a lost completion.
                    ResetEvent(operation.hEvent);
                    if (GetOverlappedResult(directory, &operation, &bytes, FALSE) ||
                        GetLastError() != ERROR_IO_INCOMPLETE) { break; }
                    (void)WaitForSingleObject(operation.hEvent, INFINITE);
                }
                active = false;
            }
        };
    }

    FolderWatcher::~FolderWatcher() noexcept { (void)Stop(); }

    void FolderWatcher::Start(std::wstring path, PublishCallback publish)
    {
        (void)Stop();
        auto session = std::make_shared<Session>();
        if (!session->wake || !session->ready) { winrt::throw_last_error(); }
        session_ = session;
        work_ = Run(std::move(session), std::move(path), std::move(publish));
    }

    winrt::Windows::Foundation::IAsyncAction FolderWatcher::WhenReady() const
    {
        return AwaitReady(session_);
    }

    winrt::Windows::Foundation::IAsyncAction FolderWatcher::AwaitReady(std::shared_ptr<Session> session)
    {
        if (session) { co_await winrt::resume_on_signal(session->ready.get()); }
    }

    winrt::Windows::Foundation::IAsyncAction FolderWatcher::Stop() noexcept
    {
        if (session_) { session_->Stop(); }
        session_.reset();
        return std::exchange(work_, nullptr);
    }

    winrt::Windows::Foundation::IAsyncAction FolderWatcher::Run(
        std::shared_ptr<Session> session, std::wstring path, PublishCallback publish)
    {
        const SignalOnExit release_readiness{ session->ready.get() };
        co_await winrt::resume_background();
        const auto notify = [&session, &publish](const FolderWatchEvent event) noexcept {
            if (!session->stopped.load() && publish)
            {
                try { publish(event); } catch (...) { /* Client/dispatcher failure is terminal to it. */ }
            }
        };
        if (session->stopped.load()) { co_return; }
        CREATEFILE2_EXTENDED_PARAMETERS parameters{ sizeof(parameters) };
        parameters.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED;
        const auto native = CreateFile2(path.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, &parameters);
        if (native == INVALID_HANDLE_VALUE)
        {
            notify({ FolderWatchEventKind::Unavailable, GetLastError() });
            co_return;
        }
        winrt::handle directory{ native };
        // Only invalidation hints are needed. Names/IDs in this buffer never
        // edit rows directly; a fresh DirectoryListing remains authoritative.
        alignas(DWORD) std::array<std::byte, 64 * 1024> buffer;
        bool ready{};
        bool immediate_overflow{};
        try
        {
            while (!session->stopped.load())
            {
                ResetEvent(session->wake.get());
                if (session->stopped.load()) { break; }
                OVERLAPPED operation{};
                operation.hEvent = session->wake.get();
                PendingRead pending{ directory.get(), operation };
                constexpr DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                    FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
                if (!ReadDirectoryChangesExW(directory.get(), buffer.data(),
                        static_cast<DWORD>(buffer.size()), FALSE, filter, nullptr, &operation,
                        nullptr, ReadDirectoryNotifyInformation))
                {
                    const auto error = GetLastError();
                    if (error == ERROR_NOTIFY_ENUM_DIR)
                    {
                        if (immediate_overflow)
                        {
                            notify({ FolderWatchEventKind::Unavailable, error });
                            break;
                        }
                        immediate_overflow = true;
                        notify({ FolderWatchEventKind::Overflow, error });
                        continue;
                    }
                    if (error != ERROR_IO_PENDING)
                    {
                        notify({ FolderWatchEventKind::Unavailable, error });
                        break;
                    }
                }
                pending.active = true;
                immediate_overflow = false;
                if (!ready)
                {
                    ready = true;
                    // Arm-before-enumerate closes the initial monitoring gap
                    // without invalidating a freshly opened, stable directory.
                    SetEvent(session->ready.get());
                    notify({ FolderWatchEventKind::Ready });
                }
                // Starting overlapped I/O may reset its event. Stop could have
                // signaled just before the arm, so recheck before awaiting it.
                if (session->stopped.load()) { break; }
                co_await winrt::resume_on_signal(session->wake.get());
                if (session->stopped.load()) { break; } // pending destructor drains on the worker.
                DWORD bytes{};
                const bool completed = GetOverlappedResult(directory.get(), &operation, &bytes, FALSE) != FALSE;
                const auto error = completed ? ERROR_SUCCESS : GetLastError();
                if (!completed && error == ERROR_IO_INCOMPLETE)
                {
                    notify({ FolderWatchEventKind::Unavailable, error });
                    break;
                }
                pending.active = false;
                if (completed || error == ERROR_NOTIFY_ENUM_DIR)
                {
                    notify({ bytes == 0 ? FolderWatchEventKind::Overflow : FolderWatchEventKind::Changed, error });
                }
                else
                {
                    notify({ FolderWatchEventKind::Unavailable, error });
                    break;
                }
            }
        }
        catch (...)
        {
            // PendingRead has already drained before the error is published.
            notify({ FolderWatchEventKind::Unavailable, ERROR_GEN_FAILURE });
        }
    }
}
