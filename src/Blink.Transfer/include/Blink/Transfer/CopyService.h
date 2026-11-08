#pragma once

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>

namespace blink::transfer
{
    struct CopyProgress
    {
        std::uint64_t total_bytes{};
        std::uint64_t transferred_bytes{};
    };

    struct CopyResult
    {
        HRESULT status{ E_FAIL };
        bool canceled{};
        bool paused{};
        bool partial_file_left{};
    };

    struct CopyControl
    {
        void Cancel() noexcept;
        void Pause() noexcept;
        [[nodiscard]] bool IsCanceled() const noexcept;
        [[nodiscard]] bool IsPaused() const noexcept;

        BOOL cancel_flag{};
        BOOL pause_flag{};
        std::function<void(const CopyProgress&)> progress_callback;
    };

    struct CopyTask
    {
        std::shared_ptr<CopyControl> control;
        std::future<CopyResult> completion;
    };

    [[nodiscard]] CopyTask CopyFileAsync(
        std::wstring source,
        std::wstring destination,
        std::function<void(const CopyProgress&)> progress_callback = {});
}
