#pragma once

#include "Blink/App/Location.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace blink::app
{
    enum class NavigationAction
    {
        Navigate,
        Back,
        Forward,
        Up,
        Refresh,
        Relocate,
        DeleteLocation,
    };

    struct NavigationRequest
    {
        std::uint64_t token{};
        NavigationAction action{ NavigationAction::Navigate };
        Location target;
    };

    struct NavigationSnapshot
    {
        Location committed_location;
        // Derived compatibility value for existing folder presentation/status
        // callers. Location is the authoritative navigation truth.
        std::wstring committed_current_path;
        std::vector<Location> back_history;
        std::vector<Location> forward_history;
        bool can_go_back{};
        bool can_go_forward{};
        bool has_pending{};
        std::uint64_t pending_token{};
        std::optional<Location> pending_location;
    };

    // Pure committed navigation state. It performs path normalization and
    // parent-path parsing only; existence, ACL and directory enumeration are
    // owned by the caller's browse layer.
    class NavigationModel
    {
    public:
        NavigationModel();

        [[nodiscard]] std::optional<NavigationRequest> Navigate(const Location& target);
        // Compatibility boundary for existing address-bar callers. The
        // resulting request still carries a Location, never a path-only target.
        [[nodiscard]] std::optional<NavigationRequest> Navigate(const std::wstring& target);
        [[nodiscard]] std::optional<NavigationRequest> Back();
        [[nodiscard]] std::optional<NavigationRequest> Forward();
        [[nodiscard]] std::optional<NavigationRequest> Up();
        [[nodiscard]] std::optional<NavigationRequest> Refresh();
        [[nodiscard]] std::optional<NavigationRequest> RelocateFolder(const Location& source, const Location& destination, bool preserve_suffix = true);

        [[nodiscard]] bool IsPending(const NavigationRequest& request) const noexcept;
        [[nodiscard]] bool Commit(const NavigationRequest& request);
        [[nodiscard]] bool Reject(const NavigationRequest& request) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] NavigationSnapshot Snapshot() const;

    private:
        [[nodiscard]] std::optional<NavigationRequest> Begin(
            NavigationAction action,
            Location target);
        void InvalidatePending() noexcept;

        [[nodiscard]] static std::optional<Location> NormalizeTarget(
            const Location& target);

        std::vector<Location> history_;
        // Keep the displayed page unchanged until its new listing commits.
        // A user navigation elsewhere can commit these history updates too.
        std::optional<std::vector<Location>> relocated_history_;
        std::size_t cursor_{};
        std::optional<NavigationRequest> pending_;
        std::uint64_t next_token_{};
        bool shutdown_{};
    };
}
