#pragma once

#include "Blink/App/FileCopyOperation.h"
#include <optional>

namespace blink::app
{
    // The dialog owns one decision. Dismissal defaults to Cancel; disposing
    // its window suppresses the decision entirely, including a queued click.
    class CopyConflictModel
    {
    public:
        explicit CopyConflictModel(const bool enable_replace = false, const bool enable_merge = false) noexcept :
            enable_replace_(enable_replace), enable_merge_(enable_merge) {}

        [[nodiscard]] bool IsClosed() const noexcept { return closed_; }
        [[nodiscard]] FileCopyConflictPolicy SelectedPolicy() const noexcept { return selected_; }
        [[nodiscard]] bool CanChoose(const FileCopyConflictPolicy policy) const noexcept
        {
            if (closed_ || selected_explicitly_) { return false; }
            switch (policy)
            {
            case FileCopyConflictPolicy::Skip:
            case FileCopyConflictPolicy::KeepBoth:
            case FileCopyConflictPolicy::Cancel:
                return true;
            case FileCopyConflictPolicy::Replace:
                return enable_replace_;
            case FileCopyConflictPolicy::Merge:
                return enable_merge_;
            default:
                return false;
            }
        }
        [[nodiscard]] bool Choose(const FileCopyConflictPolicy policy) noexcept
        {
            if (!CanChoose(policy)) { return false; }
            selected_ = policy;
            selected_explicitly_ = true;
            return true;
        }
        [[nodiscard]] std::optional<FileCopyConflictPolicy> Complete() noexcept
        {
            if (closed_) { return {}; }
            closed_ = true;
            return selected_;
        }
        void Close() noexcept { closed_ = true; }

    private:
        bool enable_replace_{};
        bool enable_merge_{};
        bool selected_explicitly_{};
        bool closed_{};
        FileCopyConflictPolicy selected_{ FileCopyConflictPolicy::Cancel };
    };
}
