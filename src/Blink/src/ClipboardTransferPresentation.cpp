#include "Blink/App/ClipboardTransferPresentation.h"
#include "Blink/App/FileOperationRelocation.h"
#include <functional>

namespace blink::app
{
    namespace
    {
        FileCopyBatchItemStage CopyStage(const FileOperationStage stage) noexcept
        {
            switch (stage)
            {
            case FileOperationStage::Completed: return FileCopyBatchItemStage::Copied;
            case FileOperationStage::Skipped: return FileCopyBatchItemStage::Skipped;
            case FileOperationStage::Failed: return FileCopyBatchItemStage::Failed;
            case FileOperationStage::Cancelled: return FileCopyBatchItemStage::Cancelled;
            case FileOperationStage::Idle: case FileOperationStage::Queued: return FileCopyBatchItemStage::Pending;
            default: return FileCopyBatchItemStage::Running;
            }
        }
        MoveBatchItemStage MoveStage(const FileOperationStage stage) noexcept
        {
            switch (stage)
            {
            case FileOperationStage::Completed: return MoveBatchItemStage::Moved;
            case FileOperationStage::Skipped: return MoveBatchItemStage::Skipped;
            case FileOperationStage::Failed: return MoveBatchItemStage::Failed;
            case FileOperationStage::Cancelled: return MoveBatchItemStage::Cancelled;
            case FileOperationStage::Idle: case FileOperationStage::Queued: return MoveBatchItemStage::Pending;
            default: return MoveBatchItemStage::Running;
            }
        }
    }

    std::size_t ClipboardTransferPresentation::SourceHash::operator()(const Source& source) const noexcept
    { return (*this)(SourceView{ source.parent, source.identity, source.name, source.directory }); }

    std::size_t ClipboardTransferPresentation::SourceHash::operator()(const SourceView& source) const noexcept
    {
        std::size_t hash{};
        const auto add = [&hash](std::size_t value) { hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2); };
        for (const auto* id : { &source.parent, &source.identity })
        {
            add(std::hash<std::uint64_t>{}(id->volume_serial));
            for (const auto byte : id->file_id) { add(byte); }
        }
        add(std::hash<std::wstring_view>{}(source.name)); add(source.directory);
        return hash;
    }
    bool ClipboardTransferPresentation::SourceEqual::operator()(const Source& left, const SourceView& right) const noexcept
    { return left.parent == right.parent && left.identity == right.identity && left.name == right.name && left.directory == right.directory; }

    std::vector<ClipboardTransferPresentation::Source> ClipboardTransferPresentation::Sources(const ClipboardCopySources& sources)
    {
        std::vector<Source> result;
        if (!SameCapturedCopySources(sources, sources)) { return result; }
        const auto append = [&result](const auto& request) {
            result.push_back({ request.source_parent_identity, request.source.identity, request.source.name, request.source.is_directory });
        };
        if (const auto* files = std::get_if<std::vector<FileCopyRequest>>(&sources))
        { result.reserve(files->size()); for (const auto& file : *files) { append(file); } }
        else if (const auto* directory = std::get_if<DirectoryCopyRequest>(&sources)) { append(*directory); }
        else if (const auto* items = std::get_if<CopyItemRequests>(&sources))
        { result.reserve(items->size()); for (const auto& item : *items) { std::visit(append, item); } }
        return result;
    }
    bool ClipboardTransferPresentation::Matches(const Source& source, const FileCopyRequest& request) noexcept
    { return SourceEqual{}(source, SourceView{ request.source_parent_identity, request.source.identity, request.source.name, request.source.is_directory }); }
    bool ClipboardTransferPresentation::Matches(const Source& source, const DirectoryCopyRequest& request) noexcept
    { return SourceEqual{}(source, SourceView{ request.source_parent_identity, request.source.identity, request.source.name, request.source.is_directory }); }

    bool ClipboardTransferPresentation::ClearCut() noexcept
    {
        if (cut_sources_.empty()) { return false; }
        cut_sources_.clear(); ++cut_generation_; return true;
    }
    void ClipboardTransferPresentation::ResetTracking() noexcept
    { tracked_sources_.clear(); items_.clear(); cut_bound_ = false; outcome_ = {}; }

    void ClipboardTransferPresentation::Publish(const ClipboardCopyCache& published)
    {
        if (closed_) { return; }
        auto sources = Sources(published.sources);
        if (!published.sequence || sources.empty() || sources.size() != published.paths.size() ||
            (published.intent != ClipboardTransferIntent::Copy && published.intent != ClipboardTransferIntent::Move))
        { Clear(); return; }
        CutSources pending;
        if (published.intent == ClipboardTransferIntent::Move && !published.move_submitted)
        { pending.reserve(sources.size()); for (const auto& source : sources) { pending.insert(source); } }
        ResetTracking();
        published_sources_ = std::move(sources); published_sequence_ = published.sequence;
        if (pending != cut_sources_) { cut_sources_.swap(pending); ++cut_generation_; }
        outcome_.stage = ClipboardTransferStage::Published; outcome_.intent = published.intent;
        outcome_.sequence = published.sequence;
        outcome_.requested_count = outcome_.remaining_count = published_sources_.size();
    }
    bool ClipboardTransferPresentation::ClipboardChanged(const DWORD sequence) noexcept
    {
        if (closed_ || !published_sequence_ || sequence == published_sequence_) { return false; }
        const bool cut_changed = ClearCut();
        published_sequence_ = 0; published_sources_.clear(); cut_bound_ = false;
        if (outcome_.stage == ClipboardTransferStage::Published) { outcome_ = {}; return true; }
        return cut_changed;
    }
    void ClipboardTransferPresentation::Track(const ClipboardCopySources& sources, const ClipboardTransferIntent intent,
        const DWORD sequence, const std::uint64_t operation_id)
    {
        if (closed_ || !operation_id || (intent != ClipboardTransferIntent::Copy && intent != ClipboardTransferIntent::Move)) { return; }
        // Retrying a publication callback must not rearm a finished transfer.
        if (!tracked_sources_.empty() && operation_id == outcome_.operation_id) { return; }
        auto captured = Sources(sources); if (captured.empty()) { return; }
        std::vector<Item> pending(captured.size());
        const bool bound = intent == ClipboardTransferIntent::Move && sequence && sequence == published_sequence_ &&
            captured == published_sources_;
        tracked_sources_ = std::move(captured); items_ = std::move(pending); cut_bound_ = bound;
        outcome_ = {}; outcome_.stage = ClipboardTransferStage::Running; outcome_.intent = intent;
        outcome_.sequence = sequence; outcome_.operation_id = operation_id;
        outcome_.requested_count = outcome_.remaining_count = tracked_sources_.size();
    }
    void ClipboardTransferPresentation::Advance(const std::uint64_t previous, const std::uint64_t next) noexcept
    {
        if (closed_ || !next || previous != outcome_.operation_id || next == previous || tracked_sources_.empty()) { return; }
        outcome_.operation_id = next; outcome_.stage = ClipboardTransferStage::Running;
        outcome_.terminal = outcome_.resumable = false; outcome_.error = ERROR_SUCCESS;
        outcome_.failed_count = outcome_.cancelled_count = 0;
        outcome_.remaining_count = outcome_.requested_count - outcome_.completed_count - outcome_.skipped_count - outcome_.unchanged_count;
        for (auto& item : items_)
        { if (!item.completed && !item.skipped && !item.unchanged) { item.failed = item.cancelled = false; item.error = ERROR_SUCCESS; } }
    }

    ClipboardTransferPresentation::Item ClipboardTransferPresentation::CopyItem(const FileCopyResult& result,
        const FileCopyBatchItemStage stage) noexcept
    {
        Item item;
        item.completed = result.copied && !result.partial_file_left;
        item.partial = !item.completed && result.partial_file_left;
        item.skipped = !item.completed && result.skipped;
        item.cancelled = !item.completed && !item.skipped && stage == FileCopyBatchItemStage::Cancelled;
        item.failed = !item.completed && !item.skipped && !item.cancelled &&
            (stage == FileCopyBatchItemStage::Failed || stage == FileCopyBatchItemStage::Copied);
        item.warning = item.completed && result.error != ERROR_SUCCESS;
        item.error = item.failed || item.cancelled ? result.error : ERROR_SUCCESS;
        return item;
    }
    ClipboardTransferPresentation::Item ClipboardTransferPresentation::DirectoryItem(const DirectoryCopyResult& result,
        const FileCopyBatchItemStage stage) noexcept
    {
        Item item;
        item.completed = result.copied && !result.partial_tree_left && !result.skipped && !result.files_skipped && !result.directories_skipped;
        item.partial = !item.completed && (result.partial_tree_left || result.files_copied || result.directories_created);
        item.skipped = !item.completed && (result.skipped ||
            ((stage == FileCopyBatchItemStage::Copied || stage == FileCopyBatchItemStage::Skipped) &&
                (result.files_skipped || result.directories_skipped)));
        item.cancelled = !item.completed && !item.skipped && stage == FileCopyBatchItemStage::Cancelled;
        item.failed = !item.completed && !item.skipped && !item.cancelled &&
            (stage == FileCopyBatchItemStage::Failed || stage == FileCopyBatchItemStage::Copied);
        item.warning = item.completed && result.error != ERROR_SUCCESS;
        item.error = item.failed || item.cancelled ? result.error : ERROR_SUCCESS;
        return item;
    }
    ClipboardTransferPresentation::Item ClipboardTransferPresentation::MoveItem(const MoveResult& result,
        const MoveBatchItemStage stage) noexcept
    {
        Item item;
        item.completed = result.moved && !result.source_items_remaining && !result.files_skipped && !result.directories_skipped &&
            (!result.copy_result || (result.copy_result->copied && result.source_removed)) &&
            (!result.directory_copy_result || (result.directory_copy_result->copied && result.source_removed)) &&
            (!result.merged || result.source_removed);
        item.partial = !item.completed && (MoveHasPartialDestination(result) || MoveHasSourceChanges(result));
        item.unchanged = !item.completed && result.unchanged && !item.partial;
        item.skipped = !item.completed && !item.unchanged && result.skipped;
        item.cancelled = !item.completed && !item.unchanged && !item.skipped && stage == MoveBatchItemStage::Cancelled;
        item.failed = !item.completed && !item.unchanged && !item.skipped && !item.cancelled &&
            (stage == MoveBatchItemStage::Failed || stage == MoveBatchItemStage::Moved);
        item.warning = item.completed && result.post_commit_error != ERROR_SUCCESS;
        item.error = item.failed || item.cancelled ? result.error : ERROR_SUCCESS;
        return item;
    }

    bool ClipboardTransferPresentation::Observe(const FileOperationSnapshot& snapshot) noexcept
    {
        if (closed_ || tracked_sources_.empty() || snapshot.operation_id != outcome_.operation_id || outcome_.terminal) { return false; }
        const bool moving = outcome_.intent == ClipboardTransferIntent::Move;
        if ((moving && snapshot.kind != FileOperationKind::Move) || (!moving && snapshot.kind != FileOperationKind::Copy &&
            snapshot.kind != FileOperationKind::DirectoryCopy)) { return false; }
        const auto count = tracked_sources_.size();
        if (moving && snapshot.move_batch)
        {
            if (snapshot.move_batch->items.size() != count) { return false; }
            for (std::size_t index = 0; index < count; ++index)
            { if (!Matches(tracked_sources_[index], snapshot.move_batch->items[index].request)) { return false; } }
        }
        else if (!moving && snapshot.kind == FileOperationKind::Copy && snapshot.copy_batch)
        {
            if (snapshot.copy_batch->items.size() != count) { return false; }
            for (std::size_t index = 0; index < count; ++index)
            { if (!Matches(tracked_sources_[index], snapshot.copy_batch->items[index].request)) { return false; } }
        }
        else if (count != 1 || (moving ? !snapshot.move_request || !Matches(tracked_sources_.front(), *snapshot.move_request) :
            snapshot.kind == FileOperationKind::DirectoryCopy ? !snapshot.directory_copy_request ||
                !Matches(tracked_sources_.front(), *snapshot.directory_copy_request) :
                !snapshot.copy_request || !Matches(tracked_sources_.front(), *snapshot.copy_request))) { return false; }

        auto next = outcome_;
        next.completed_count = next.skipped_count = next.unchanged_count = next.failed_count = next.cancelled_count = 0;
        next.remaining_count = next.partial_count = next.warning_count = 0;
        bool cut_changed{};
        DWORD item_error{ ERROR_SUCCESS };
        for (std::size_t index = 0; index < count; ++index)
        {
            Item actual;
            if (moving && snapshot.move_batch)
            {
                const auto& item = snapshot.move_batch->items[index]; actual = MoveItem(item.result, item.stage);
                if (!actual.completed && item.retained_result)
                { actual.partial |= MoveHasPartialDestination(*item.retained_result) || MoveHasSourceChanges(*item.retained_result); }
            }
            else if (moving)
            {
                const MoveResult absent{ snapshot.result.error };
                actual = MoveItem(snapshot.move_result ? *snapshot.move_result : absent, MoveStage(snapshot.stage));
            }
            else if (snapshot.kind == FileOperationKind::Copy && snapshot.copy_batch)
            {
                const auto& item = snapshot.copy_batch->items[index];
                if (tracked_sources_[index].directory)
                {
                    if (item.directory_result) { actual = DirectoryItem(*item.directory_result, item.stage); }
                    else { actual = CopyItem(FileCopyResult{ item.result.error }, item.stage); }
                }
                else { actual = CopyItem(item.result, item.stage); }
                if (!actual.completed)
                {
                    actual.partial |= item.directory_partial_attempts != 0;
                    for (const auto& result : item.previous_results) { actual.partial |= result.partial_file_left; }
                }
            }
            else if (snapshot.kind == FileOperationKind::DirectoryCopy)
            {
                const DirectoryCopyResult absent{ snapshot.result.error };
                actual = DirectoryItem(snapshot.directory_copy_result ? *snapshot.directory_copy_result : absent, CopyStage(snapshot.stage));
            }
            else
            { actual = CopyItem(FileCopyResult{ snapshot.result.error, snapshot.result.committed, snapshot.result.partial_file_left,
                {}, snapshot.result.skipped }, CopyStage(snapshot.stage)); }

            // Commits are monotonic even if an earlier progress callback is
            // delivered late. The current retry may resolve an old partial.
            auto& item = items_[index];
            if (item.completed)
            { const bool warning = actual.warning; actual = item; actual.warning |= warning; }
            else if (!actual.completed) { actual.partial |= item.partial; }
            item = actual;
            if (item.completed)
            {
                ++next.completed_count;
                if (cut_bound_) { cut_changed |= cut_sources_.erase(tracked_sources_[index]) != 0; }
            }
            else if (item.skipped) { ++next.skipped_count; }
            else if (item.unchanged) { ++next.unchanged_count; }
            else if (item.failed) { ++next.failed_count; }
            else if (item.cancelled) { ++next.cancelled_count; }
            else { ++next.remaining_count; }
            if (item.partial) { ++next.partial_count; }
            if (item.warning) { ++next.warning_count; }
            if (!item_error && item.error) { item_error = item.error; }
        }

        next.resumable = !snapshot.closed && ((snapshot.copy_batch && snapshot.copy_batch->resumable) ||
            (snapshot.move_batch && snapshot.move_batch->resumable) || snapshot.directory_conflict.has_value());
        next.terminal = (FileOperationIsTerminal(snapshot.stage) || snapshot.closed) && !next.resumable;
        next.error = next.terminal || next.resumable ? snapshot.result.error : ERROR_SUCCESS;
        if ((next.terminal || next.resumable) && (!next.error || next.error == ERROR_GEN_FAILURE) && item_error)
        { next.error = item_error; }
        if (next.resumable) { next.stage = ClipboardTransferStage::AwaitingDecision; }
        else if (!next.terminal)
        { next.stage = snapshot.stage == FileOperationStage::Cancelling ? ClipboardTransferStage::Cancelling : ClipboardTransferStage::Running; }
        else if (next.completed_count == next.requested_count)
        { next.stage = ClipboardTransferStage::Completed; next.error = ERROR_SUCCESS; }
        else if (snapshot.stage == FileOperationStage::Cancelled || snapshot.closed)
        { next.stage = ClipboardTransferStage::Cancelled; if (!next.error) { next.error = ERROR_CANCELLED; } }
        else if (next.completed_count || next.partial_count)
        { next.stage = ClipboardTransferStage::Partial; if (!next.error) { next.error = ERROR_PARTIAL_COPY; } }
        else if (next.unchanged_count == next.requested_count) { next.stage = ClipboardTransferStage::Unchanged; next.error = ERROR_SUCCESS; }
        else if (next.skipped_count + next.unchanged_count == next.requested_count) { next.stage = ClipboardTransferStage::Skipped; next.error = ERROR_SUCCESS; }
        else { next.stage = ClipboardTransferStage::Failed; if (!next.error) { next.error = ERROR_GEN_FAILURE; } }

        if (next.terminal && cut_bound_ && !cut_sources_.empty()) { cut_sources_.clear(); cut_changed = true; }
        if (cut_changed) { ++cut_generation_; }
        const bool changed = cut_changed || next != outcome_; outcome_ = next; return changed;
    }

    bool ClipboardTransferPresentation::IsCut(const blink::core::FileIdentity& parent, const blink::core::FileEntry& entry) const noexcept
    { return IsCut(parent, entry.identity, entry.name, entry.is_directory); }
    bool ClipboardTransferPresentation::IsCut(const blink::core::FileIdentity& parent, const blink::core::FileIdentity& identity,
        const std::wstring_view name, const bool is_directory) const noexcept
    { return parent.valid && identity.valid && cut_sources_.find(SourceView{ parent, identity, name, is_directory }) != cut_sources_.end(); }
    void ClipboardTransferPresentation::Clear() noexcept
    {
        (void)ClearCut(); published_sources_.clear(); published_sequence_ = 0; ResetTracking();
        if (closed_) { outcome_.stage = ClipboardTransferStage::Closed; outcome_.terminal = true; }
    }
    void ClipboardTransferPresentation::Close() noexcept
    { closed_ = true; Clear(); }
}
