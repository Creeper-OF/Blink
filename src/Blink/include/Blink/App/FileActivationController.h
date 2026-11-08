#pragma once

#include "Blink/App/FileActivation.h"

#include <winrt/Windows.Foundation.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace blink::app
{
    enum class FileActivationStage
    {
        Idle,
        Opening,
        Dispatched,
        Failed,
    };

    struct FileActivationSnapshot
    {
        std::uint64_t generation{};
        FileActivationStage stage{ FileActivationStage::Idle };
        std::wstring path;
        HRESULT error{ S_OK };
    };

    // Tab-owned, ordinary-user file-association dispatch. Neither validation nor
    // Shell execution runs on the UI thread. Cancellation invalidates queued
    // work/results; it cannot recall a request already dispatched to Windows.
    class FileActivationController
    {
    public:
        using ValidateFunction = std::function<DWORD(const blink::core::FileEntry&)>;
        using LaunchFunction = std::function<HRESULT(const blink::core::FileEntry&)>;
        using PublishCallback = std::function<void(std::uint64_t)>;

        explicit FileActivationController(
            ValidateFunction validate = ValidateFileActivation,
            LaunchFunction launch = LaunchFunction{});
        ~FileActivationController() noexcept;

        FileActivationController(const FileActivationController&) = delete;
        FileActivationController& operator=(const FileActivationController&) = delete;

        [[nodiscard]] std::uint64_t Open(
            blink::core::FileEntry entry,
            PublishCallback publish);
        [[nodiscard]] FileActivationSnapshot Snapshot() const;
        [[nodiscard]] bool IsCurrent(std::uint64_t generation) const noexcept;
        void CancelPending() noexcept;
        void Shutdown() noexcept;

    private:
        struct State;
        [[nodiscard]] static HRESULT DispatchHost(
            const std::shared_ptr<State>& state,
            std::uint64_t generation,
            const blink::core::FileEntry& entry);
        void Cancel(bool shutdown) noexcept;

        std::shared_ptr<State> state_;
        ValidateFunction validate_;
        LaunchFunction launch_;
        std::mutex work_mutex_;
        winrt::Windows::Foundation::IAsyncAction work_{ nullptr };
    };
}
