#pragma once
#include "Blink/App/ClipboardPasteSession.h"
#include "Blink/App/ClipboardTransferPresentation.h"
#include <unordered_map>

namespace blink::app
{
    // Written and read only by the owning UI thread, including dispatcher
    // callbacks. The background session merely retains this opaque context.
    struct ClipboardPasteFeedbackContext { std::uint64_t operation_id{}; };
    // UI-owned receipt bindings. These are metadata lifetimes, not another file
    // operation queue. Every original object keeps its own already-tested model
    // so a new Clipboard publication cannot overwrite an older task's outcome.
    class ClipboardPasteController
    {
    public:
        ClipboardPasteController() = default;
        ~ClipboardPasteController() noexcept { Close(); }
        ClipboardPasteController(const ClipboardPasteController&) = delete;
        ClipboardPasteController& operator=(const ClipboardPasteController&) = delete;
        void Track(const ClipboardCopySources& sources, const ClipboardFilePaths& payload,
            std::uint64_t operation_id, std::shared_ptr<ClipboardPasteSession> session,
            std::shared_ptr<ClipboardPasteFeedbackContext> context = {});
        void Advance(std::uint64_t previous, std::uint64_t next);
        void Observe(const FileOperationSnapshot& snapshot);
        void CancelDecisions() noexcept;
        void CancelDecision(std::uint64_t operation_id) noexcept;
        void Close() noexcept;
        [[nodiscard]] bool Pending(std::uint64_t operation_id) const noexcept;
        [[nodiscard]] std::size_t PendingCount() const noexcept { return pending_.size(); }
    private:
        struct Receipt
        {
            ClipboardTransferPresentation model;
            std::shared_ptr<ClipboardPasteSession> session;
            std::shared_ptr<ClipboardPasteFeedbackContext> context;
            bool directory_decision{};
        };
        std::unordered_map<std::uint64_t, Receipt> pending_;
        bool closed_{};
    };
}
