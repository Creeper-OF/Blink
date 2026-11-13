#pragma once

#include "Blink/App/CopyTransferPresentation.h"
#include <cstdint>
#include <string_view>
#include <unordered_set>

namespace blink::app
{
    enum class ClipboardTransferStage
    {
        Idle, Published, Running, Cancelling, AwaitingDecision,
        Completed, Partial, Skipped, Unchanged, Cancelled, Failed, Closed
    };

    struct ClipboardTransferOutcome
    {
        ClipboardTransferStage stage{ ClipboardTransferStage::Idle };
        ClipboardTransferIntent intent{ ClipboardTransferIntent::Copy };
        DWORD sequence{};
        std::uint64_t operation_id{};
        std::size_t requested_count{}, completed_count{}, skipped_count{}, unchanged_count{};
        std::size_t failed_count{}, cancelled_count{}, remaining_count{};
        // Supplementary facts about unfinished source items, not an assertion
        // that a historical partial destination still exists on disk.
        std::size_t partial_count{};
        // Completed items with a real postcommit/metadata warning.
        std::size_t warning_count{};
        DWORD error{ ERROR_SUCCESS };
        bool terminal{}, resumable{};
        friend bool operator==(const ClipboardTransferOutcome&, const ClipboardTransferOutcome&) = default;
    };

    // UI-owned presentation only: no filesystem, clipboard, queue or COM calls.
    // The command's ClipboardCopyCache remains responsible for MOVE replay.
    class ClipboardTransferPresentation
    {
    public:
        void Publish(const ClipboardCopyCache& published);
        [[nodiscard]] bool ClipboardChanged(DWORD sequence) noexcept;
        void Track(const ClipboardCopySources& sources, ClipboardTransferIntent intent,
            DWORD sequence, std::uint64_t operation_id);
        void Advance(std::uint64_t previous, std::uint64_t next) noexcept;
        // True when either outcome or cut presentation changed. Byte progress
        // alone does not change CutGeneration or require another row scan.
        [[nodiscard]] bool Observe(const FileOperationSnapshot& snapshot) noexcept;
        [[nodiscard]] bool IsCut(const blink::core::FileIdentity& parent,
            const blink::core::FileEntry& entry) const noexcept;
        [[nodiscard]] bool IsCut(const blink::core::FileIdentity& parent,
            const blink::core::FileIdentity& identity, std::wstring_view name,
            bool is_directory) const noexcept;
        [[nodiscard]] std::uint64_t CutGeneration() const noexcept { return cut_generation_; }
        [[nodiscard]] const ClipboardTransferOutcome& Outcome() const noexcept { return outcome_; }
        void Clear() noexcept;
        void Close() noexcept;

    private:
        struct Source
        {
            blink::core::FileIdentity parent, identity;
            std::wstring name;
            bool directory{};
            friend bool operator==(const Source&, const Source&) = default;
        };
        struct SourceView
        {
            const blink::core::FileIdentity& parent;
            const blink::core::FileIdentity& identity;
            std::wstring_view name;
            bool directory{};
        };
        struct SourceHash
        {
            using is_transparent = void;
            std::size_t operator()(const Source& source) const noexcept;
            std::size_t operator()(const SourceView& source) const noexcept;
        };
        struct SourceEqual
        {
            using is_transparent = void;
            bool operator()(const Source& left, const Source& right) const noexcept { return left == right; }
            bool operator()(const Source& left, const SourceView& right) const noexcept;
            bool operator()(const SourceView& left, const Source& right) const noexcept { return (*this)(right, left); }
        };
        struct Item
        {
            bool completed{}, skipped{}, unchanged{}, failed{}, cancelled{}, partial{}, warning{};
            DWORD error{ ERROR_SUCCESS };
        };
        using CutSources = std::unordered_set<Source, SourceHash, SourceEqual>;

        static std::vector<Source> Sources(const ClipboardCopySources& sources);
        static bool Matches(const Source& source, const FileCopyRequest& request) noexcept;
        static bool Matches(const Source& source, const DirectoryCopyRequest& request) noexcept;
        static Item CopyItem(const FileCopyResult& result, FileCopyBatchItemStage stage) noexcept;
        static Item DirectoryItem(const DirectoryCopyResult& result, FileCopyBatchItemStage stage) noexcept;
        static Item MoveItem(const MoveResult& result, MoveBatchItemStage stage) noexcept;
        [[nodiscard]] bool ClearCut() noexcept;
        void ResetTracking() noexcept;

        CutSources cut_sources_;
        std::vector<Source> published_sources_, tracked_sources_;
        std::vector<Item> items_;
        ClipboardTransferOutcome outcome_;
        DWORD published_sequence_{};
        std::uint64_t cut_generation_{};
        bool cut_bound_{}, closed_{};
    };
}
