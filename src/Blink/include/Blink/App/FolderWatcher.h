#pragma once

#include <Windows.h>
#include <winrt/Windows.Foundation.h>

#include <functional>
#include <memory>
#include <string>

namespace blink::app
{
    enum class FolderWatchEventKind { Ready, Changed, Overflow, Unavailable };
    struct FolderWatchEvent
    {
        FolderWatchEventKind kind{ FolderWatchEventKind::Changed };
        DWORD error{ ERROR_SUCCESS };
        [[nodiscard]] bool RequiresRefresh(const bool restoring_committed_page = false) const noexcept
        {
            // Fresh navigation enumerates after the watch is armed. Only an
            // old committed page restored after failure needs a Ready catch-up.
            return kind == FolderWatchEventKind::Changed || kind == FolderWatchEventKind::Overflow ||
                (kind == FolderWatchEventKind::Ready && restoring_committed_page);
        }
    };

    // Owns one non-recursive directory subscription. Idle I/O uses an event
    // await, not a blocked worker. Stop signals cancellation without waiting
    // on the UI; the returned action lets ordinary tests await real cleanup.
    class FolderWatcher
    {
    public:
        using PublishCallback = std::function<void(FolderWatchEvent)>;
        FolderWatcher() = default;
        ~FolderWatcher() noexcept;
        FolderWatcher(const FolderWatcher&) = delete;
        FolderWatcher& operator=(const FolderWatcher&) = delete;

        void Start(std::wstring path, PublishCallback publish);
        // Each call owns an independent awaiter. Failure/Stop also release it
        // so an unavailable subscription cannot strand a pending navigation.
        winrt::Windows::Foundation::IAsyncAction WhenReady() const;
        winrt::Windows::Foundation::IAsyncAction Stop() noexcept;

    private:
        struct Session;
        static winrt::Windows::Foundation::IAsyncAction AwaitReady(std::shared_ptr<Session> session);
        static winrt::Windows::Foundation::IAsyncAction Run(
            std::shared_ptr<Session> session, std::wstring path, PublishCallback publish);
        std::shared_ptr<Session> session_;
        winrt::Windows::Foundation::IAsyncAction work_{ nullptr };
    };
}
