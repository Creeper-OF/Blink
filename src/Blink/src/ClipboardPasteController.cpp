#include "Blink/App/ClipboardPasteController.h"

namespace blink::app
{
    void ClipboardPasteController::Track(const ClipboardCopySources& sources, const ClipboardFilePaths& payload,
        const std::uint64_t operation_id, std::shared_ptr<ClipboardPasteSession> session,
        std::shared_ptr<ClipboardPasteFeedbackContext> context)
    {
        if (closed_ || !operation_id || !session || pending_.contains(operation_id)) { return; }
        Receipt receipt;
        receipt.model.Track(sources, payload.intent, payload.sequence, operation_id);
        if (receipt.model.Outcome().operation_id != operation_id) { return; }
        receipt.session = std::move(session);
        receipt.context = std::move(context);
        if (receipt.context) { receipt.context->operation_id = operation_id; }
        pending_.emplace(operation_id, std::move(receipt));
    }
    void ClipboardPasteController::Advance(const std::uint64_t previous, const std::uint64_t next)
    {
        if (closed_ || !next || previous == next || pending_.contains(next)) { return; }
        auto entry = pending_.extract(previous);
        if (entry.empty()) { return; }
        entry.mapped().model.Advance(previous, next);
        entry.mapped().directory_decision = false;
        if (entry.mapped().context) { entry.mapped().context->operation_id = next; }
        entry.key() = next;
        pending_.insert(std::move(entry));
    }
    void ClipboardPasteController::Observe(const FileOperationSnapshot& snapshot)
    {
        const auto found = pending_.find(snapshot.operation_id);
        if (found == pending_.end()) { return; }
        auto& receipt = found->second;
        (void)receipt.model.Observe(snapshot);
        if (DirectoryCopyHasRootConflict(snapshot)) { receipt.directory_decision = true; return; }
        const auto& outcome = receipt.model.Outcome();
        if (!outcome.terminal || outcome.resumable) { return; }
        const bool complete = outcome.stage == ClipboardTransferStage::Completed && outcome.requested_count &&
            outcome.completed_count == outcome.requested_count;
        const HRESULT result = complete ? S_OK : outcome.error ? HRESULT_FROM_WIN32(outcome.error) : S_FALSE;
        // Complete only posts to the original STA; it never invokes the source
        // application's COM code on this UI thread.
        receipt.session->Complete(outcome.intent, complete, result);
        pending_.erase(found);
    }
    void ClipboardPasteController::CancelDecisions() noexcept
    {
        for (auto it = pending_.begin(); it != pending_.end();)
        {
            if (!it->second.directory_decision) { ++it; continue; }
            it->second.session->Cancel();
            it = pending_.erase(it);
        }
    }
    void ClipboardPasteController::Close() noexcept
    {
        closed_ = true;
        for (auto& [id, receipt] : pending_) { receipt.session->Cancel(); }
        pending_.clear();
    }
    void ClipboardPasteController::CancelDecision(const std::uint64_t operation_id) noexcept
    {
        const auto found = pending_.find(operation_id);
        if (found == pending_.end() || !found->second.directory_decision) { return; }
        found->second.session->Cancel();
        pending_.erase(found);
    }
    bool ClipboardPasteController::Pending(const std::uint64_t operation_id) const noexcept
    { return pending_.contains(operation_id); }
}
