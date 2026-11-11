#pragma once

#include "Blink/App/Location.h"
#include "Blink/App/NavigationModel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace blink::app
{
    struct AddressBreadcrumb
    {
        std::wstring label;
        Location location;
    };

    enum class AddressCompletionSource
    {
        None,
        ThisPC,
        Directory,
    };

    struct AddressCompletionPlan
    {
        AddressCompletionSource source{ AddressCompletionSource::None };
        std::wstring directory;
        std::wstring prefix;
    };

    // Produces only a directory and name prefix. Relative input is resolved
    // against the committed folder, never the process or per-drive CWD.
    // An incomplete UNC server has no directory completion plan.
    [[nodiscard]] AddressCompletionPlan BuildAddressCompletionPlan(
        std::wstring_view draft, const Location& committed,
        std::wstring_view this_pc_label);

    // The owner supplies direct folders from the planned directory, or the
    // existing This PC entries. Preserve that ordering and first target value.
    [[nodiscard]] std::vector<AddressBreadcrumb> FilterAddressCompletions(
        const AddressCompletionPlan& plan,
        const std::vector<AddressBreadcrumb>& available, std::size_t max = 12);

    // Uses Location's existing parent semantics only. No folder enumeration,
    // volume metadata, or synthetic network/server locations are introduced.
    [[nodiscard]] std::vector<AddressBreadcrumb> BuildAddressBreadcrumbs(
        const Location& location, std::wstring_view this_pc_label);

    // Current first, then nearest-to-oldest back entries, then nearest-to-last
    // forward entries. Only committed Location values participate; typed text,
    // compatibility paths, and pending navigation never become history.
    [[nodiscard]] std::vector<Location> BuildAddressHistory(
        const NavigationSnapshot& snapshot, std::size_t max = 20);

    // Transient presentation state. The owning tab's Location/NavigationModel
    // remains authoritative for successful navigation and the committed text.
    class AddressBarState
    {
    public:
        void Present(std::uint64_t owner, std::wstring committed,
            bool preserve_draft = false);
        void BeginEdit();
        void SetDraft(std::wstring draft);
        void Cancel();

        [[nodiscard]] bool IsEditing() const noexcept { return editing_; }
        [[nodiscard]] std::uint64_t Owner() const noexcept { return owner_; }
        [[nodiscard]] const std::wstring& CommittedText() const noexcept { return committed_; }
        [[nodiscard]] const std::wstring& DraftText() const noexcept { return draft_; }

    private:
        std::uint64_t owner_{};
        std::wstring committed_;
        std::wstring draft_;
        bool editing_{};
    };
}
