#pragma once

#include "Blink/App/BrowseState.h"

#include <winrt/Windows.Foundation.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace blink::app
{
    // Owns the current asynchronous browse work. Cancellation here is
    // logical: the synchronous EnumerateDirectory call cannot be forcibly
    // interrupted once it has started.
    class BrowseController
    {
    public:
        using EnumerateFunction = std::function<blink::core::DirectoryListing(const std::wstring&)>;
        using PublishCallback = std::function<void(BrowseResult)>;

        explicit BrowseController(
            std::shared_ptr<BrowseState> state,
            EnumerateFunction enumerate = blink::core::EnumerateDirectory);
        ~BrowseController() noexcept;

        BrowseController(const BrowseController&) = delete;
        BrowseController& operator=(const BrowseController&) = delete;

        void Browse(std::wstring normalized_path, PublishCallback publish,
            winrt::Windows::Foundation::IAsyncAction watcher_ready = nullptr);
        void CancelPending() noexcept;
        void Shutdown() noexcept;

    private:
        static winrt::Windows::Foundation::IAsyncAction Run(
            std::shared_ptr<BrowseState> state, EnumerateFunction enumerate,
            std::uint64_t generation, std::wstring path, PublishCallback publish,
            winrt::Windows::Foundation::IAsyncAction watcher_ready);
        std::shared_ptr<BrowseState> state_;
        EnumerateFunction enumerate_;
        std::mutex mutex_;
        winrt::Windows::Foundation::IAsyncAction active_work_{ nullptr };
    };
}
