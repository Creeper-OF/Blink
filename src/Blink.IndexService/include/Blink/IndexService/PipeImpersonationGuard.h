#pragma once

#include <Windows.h>

namespace blink::index::service
{
    struct PipeImpersonationTelemetry
    {
        using Callback = void (*)(
            void* context,
            const char* stage,
            bool success,
            DWORD error) noexcept;

        Callback callback{};
        void* context{};

        void Emit(const char* stage, bool success, DWORD error) const noexcept
        {
            if (callback == nullptr)
            {
                return;
            }
            try
            {
                callback(context, stage, success, error);
            }
            catch (...)
            {
                // Telemetry must never weaken the impersonation cleanup path.
            }
        }
    };

    struct PipeImpersonationState
    {
        bool impersonated{};
        bool revert_attempted{};
        bool revert_succeeded{};
        bool revert_failed{};
        DWORD impersonate_error{ ERROR_SUCCESS };
        DWORD revert_error{ ERROR_SUCCESS };
    };

    class ScopedPipeImpersonation final
    {
    public:
        ScopedPipeImpersonation(
            HANDLE pipe,
            PipeImpersonationState& state,
            PipeImpersonationTelemetry telemetry = {}) noexcept
            : state_(state), telemetry_(telemetry)
        {
            state_ = {};
            if (ImpersonateNamedPipeClient(pipe))
            {
                active_ = true;
                state_.impersonated = true;
                telemetry_.Emit("impersonate_success", true, ERROR_SUCCESS);
            }
            else
            {
                state_.impersonate_error = GetLastError();
                telemetry_.Emit("impersonate_success", false, state_.impersonate_error);
            }
        }

        ~ScopedPipeImpersonation() noexcept
        {
            if (active_)
            {
                (void)RevertNow();
            }
        }

        ScopedPipeImpersonation(const ScopedPipeImpersonation&) = delete;
        ScopedPipeImpersonation& operator=(const ScopedPipeImpersonation&) = delete;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return state_.impersonated;
        }

        [[nodiscard]] DWORD ImpersonateError() const noexcept
        {
            return state_.impersonate_error;
        }

        [[nodiscard]] bool RevertNow() noexcept
        {
            if (!active_)
            {
                return !state_.revert_failed;
            }

            state_.revert_attempted = true;
            telemetry_.Emit("revert_attempt", true, ERROR_SUCCESS);
            if (RevertToSelf())
            {
                active_ = false;
                state_.revert_succeeded = true;
                telemetry_.Emit("revert_success", true, ERROR_SUCCESS);
                return true;
            }

            state_.revert_failed = true;
            state_.revert_error = GetLastError();
            telemetry_.Emit("revert_error", false, state_.revert_error);
            return false;
        }

    private:
        PipeImpersonationState& state_;
        PipeImpersonationTelemetry telemetry_;
        bool active_{};
    };
}
