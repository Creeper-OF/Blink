#pragma once

#include "Blink/Core/FileTypes.h"

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace blink::app
{
    enum class BrowseStateKind
    {
        Idle,
        Loading,
        Completed,
        Failed,
    };

    struct BrowseResult
    {
        std::uint64_t generation{};
        std::wstring requested_path;
        blink::core::DirectoryListing listing;
        std::chrono::steady_clock::time_point background_started_at{};
        double background_enumeration_ms{};
        double background_total_ms{};
        std::chrono::steady_clock::time_point background_completed_at{};
    };

    struct BrowseSnapshot
    {
        std::wstring requested_path;
        std::uint64_t generation{};
        BrowseStateKind state{ BrowseStateKind::Idle };
        DWORD error{ ERROR_SUCCESS };
        bool shutting_down{};
    };

    // Request/execution state only. The committed directory listing and page
    // path belong to TabState, not to this request tracker.
    // The mutex protects only these small in-memory fields; it never surrounds
    // EnumerateDirectory or any other filesystem operation.
    class BrowseState
    {
    public:
        [[nodiscard]] std::uint64_t Begin(std::wstring normalized_path);

        [[nodiscard]] bool IsCurrent(
            std::uint64_t generation,
            const std::wstring& requested_path) const;

        [[nodiscard]] bool Complete(
            std::uint64_t generation,
            const std::wstring& requested_path,
            DWORD error) noexcept;

        void Shutdown() noexcept;
        void CancelPending() noexcept;

        [[nodiscard]] BrowseSnapshot Snapshot() const;

    private:
        mutable std::mutex mutex_;
        std::wstring requested_path_;
        std::uint64_t generation_{};
        BrowseStateKind state_{ BrowseStateKind::Idle };
        DWORD error_{ ERROR_SUCCESS };
        bool shutting_down_{};
    };
}
