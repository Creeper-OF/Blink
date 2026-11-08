#include "Blink/App/BrowseController.h"

#include <chrono>
#include <utility>

namespace blink::app
{
    BrowseController::BrowseController(
        std::shared_ptr<BrowseState> state,
        EnumerateFunction enumerate)
        : state_(std::move(state)),
          enumerate_(enumerate ? std::move(enumerate) : EnumerateFunction(blink::core::EnumerateDirectory))
    {
    }

    BrowseController::~BrowseController() noexcept
    {
        Shutdown();
    }

    void BrowseController::Browse(std::wstring normalized_path, PublishCallback publish,
        winrt::Windows::Foundation::IAsyncAction watcher_ready)
    {
        const std::uint64_t generation = state_->Begin(normalized_path);
        if (generation == 0)
        {
            return;
        }

        const auto state = state_;
        const auto enumerate = enumerate_;

        std::scoped_lock lock(mutex_);
        if (active_work_)
        {
            // This requests cancellation before the next work item is
            // installed. A work item already inside EnumerateDirectory may
            // still finish; the generation gate remains authoritative.
            try
            {
                active_work_.Cancel();
            }
            catch (...)
            {
                // The generation gate remains authoritative even if an
                // already-finished work item rejects cancellation.
            }
        }

        active_work_ = Run(state, enumerate, generation, std::move(normalized_path),
            std::move(publish), std::move(watcher_ready));
    }

    winrt::Windows::Foundation::IAsyncAction BrowseController::Run(
        std::shared_ptr<BrowseState> state, EnumerateFunction enumerate,
        const std::uint64_t generation, std::wstring path, PublishCallback publish,
        winrt::Windows::Foundation::IAsyncAction watcher_ready)
    {
        co_await winrt::resume_background();
        if (!state->IsCurrent(generation, path)) { co_return; }
        if (watcher_ready) { co_await watcher_ready; }
        // A new navigation or shutdown can win while the watch is arming.
        // Waiting suspends this coroutine, never the UI or a pool worker.
        if (!state->IsCurrent(generation, path)) { co_return; }
        const auto background_start = std::chrono::steady_clock::now();
        const blink::core::DirectoryListing listing =
            enumerate(path);
        const auto background_completed = std::chrono::steady_clock::now();
        const double background_total_ms =
            std::chrono::duration<double, std::milli>(
                background_completed - background_start).count();

        // First gate: reject work that completed after a newer
        // Browse request or after shutdown. No UI is touched here.
        if (!state->IsCurrent(generation, path))
        {
            co_return;
        }

        if (publish)
        {
            BrowseResult result;
            result.generation = generation;
            result.requested_path = path;
            result.listing = listing;
            result.background_started_at = background_start;
            result.background_enumeration_ms = background_total_ms;
            result.background_total_ms = background_total_ms;
            result.background_completed_at = background_completed;
            publish(std::move(result));
        }
    }

    void BrowseController::Shutdown() noexcept
    {
        state_->Shutdown();

        std::scoped_lock lock(mutex_);
        if (active_work_)
        {
            try
            {
                active_work_.Cancel();
            }
            catch (...)
            {
                // Shutdown must never turn a UI close into an exception or a
                // synchronous wait. State is already invalidated above.
            }
            active_work_ = nullptr;
        }
    }

    void BrowseController::CancelPending() noexcept
    {
        state_->CancelPending();

        std::scoped_lock lock(mutex_);
        if (active_work_)
        {
            try
            {
                active_work_.Cancel();
            }
            catch (...)
            {
                // The generation gate remains authoritative.
            }
            active_work_ = nullptr;
        }
    }
}
