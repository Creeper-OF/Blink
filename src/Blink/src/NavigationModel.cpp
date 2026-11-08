#include "Blink/App/NavigationModel.h"

#include <utility>

namespace blink::app
{
    NavigationModel::NavigationModel()
        : history_{ Location::ThisPC() }
    {
    }

    std::optional<NavigationRequest> NavigationModel::Navigate(const Location& target)
    {
        const auto normalized = NormalizeTarget(target);
        if (!normalized.has_value())
        {
            InvalidatePending();
            return std::nullopt;
        }
        return Begin(NavigationAction::Navigate, *normalized);
    }

    std::optional<NavigationRequest> NavigationModel::Navigate(const std::wstring& target)
    {
        return Navigate(Location::Folder(target));
    }

    std::optional<NavigationRequest> NavigationModel::Back()
    {
        const auto& history = relocated_history_ ? *relocated_history_ : history_;
        if (shutdown_ || history.empty() || cursor_ == 0)
        {
            return std::nullopt;
        }
        return Begin(NavigationAction::Back, history[cursor_ - 1]);
    }

    std::optional<NavigationRequest> NavigationModel::Forward()
    {
        const auto& history = relocated_history_ ? *relocated_history_ : history_;
        if (shutdown_ || history.empty() || cursor_ + 1 >= history.size())
        {
            return std::nullopt;
        }
        return Begin(NavigationAction::Forward, history[cursor_ + 1]);
    }

    std::optional<NavigationRequest> NavigationModel::Up()
    {
        const auto& history = relocated_history_ ? *relocated_history_ : history_;
        if (shutdown_ || history.empty())
        {
            return std::nullopt;
        }

        const auto parent = history[cursor_].Up();
        if (!parent.has_value())
        {
            return std::nullopt;
        }
        return Begin(NavigationAction::Up, *parent);
    }

    std::optional<NavigationRequest> NavigationModel::Refresh()
    {
        const auto& history = relocated_history_ ? *relocated_history_ : history_;
        if (shutdown_ || history.empty())
        {
            return std::nullopt;
        }
        return Begin(NavigationAction::Refresh, history[cursor_]);
    }

    bool NavigationModel::IsPending(const NavigationRequest& request) const noexcept
    {
        return !shutdown_ && pending_.has_value() &&
            pending_->token == request.token &&
            pending_->action == request.action &&
            pending_->target == request.target;
    }

    std::optional<NavigationRequest> NavigationModel::RelocateFolder(const Location& source, const Location& destination, const bool preserve_suffix)
    {
        if (shutdown_) { return {}; }
        auto next_history = relocated_history_ ? *relocated_history_ : history_;
        const auto rebase = [&](const Location& location) -> std::optional<Location> {
            if (preserve_suffix) { return RebaseFolderLocation(location, source, destination); }
            return destination.IsFolder() && destination.IsValid() && IsFolderAtOrBelow(location, source) ?
                std::optional<Location>{ destination } : std::nullopt;
        };
        bool changed{};
        for (auto& location : next_history)
        {
            if (auto rebased = rebase(location))
            { location = std::move(*rebased); changed = true; }
        }
        const auto pending_target = pending_ ? rebase(pending_->target) : std::nullopt;
        if (!changed && !pending_target) { return {}; }
        const bool current_changed = next_history[cursor_].Kind() != history_[cursor_].Kind() ||
            next_history[cursor_].FolderPath() != history_[cursor_].FolderPath();
        std::optional<NavigationRequest> replacement;
        if (pending_target) { replacement = NavigationRequest{ next_token_ + 1, pending_->action, *pending_target }; }
        else if (!pending_ && current_changed)
        { replacement = NavigationRequest{ next_token_ + 1, preserve_suffix ? NavigationAction::Relocate : NavigationAction::DeleteLocation, next_history[cursor_] }; }
        // Prepare the returned request and model-owned copy before changing
        // history/token state. Unrelated pending user navigation stays current.
        auto next_pending = replacement;
        if (current_changed) { relocated_history_ = std::move(next_history); }
        else { history_.swap(next_history); relocated_history_.reset(); }
        if (replacement) { pending_ = std::move(next_pending); next_token_ = replacement->token; }
        return replacement;
    }

    bool NavigationModel::Commit(const NavigationRequest& request)
    {
        if (!IsPending(request))
        {
            return false;
        }

        const auto& history = relocated_history_ ? *relocated_history_ : history_;
        const bool has_current = !history.empty();
        auto next_cursor = cursor_;
        std::optional<std::vector<Location>> next_history;
        switch (request.action)
        {
        case NavigationAction::Navigate:
        case NavigationAction::Up:
            if (!has_current)
            {
                next_history = std::vector<Location>{ request.target };
                next_cursor = 0;
            }
            else if (history[cursor_] != request.target)
            {
                next_history = history;
                next_history->resize(cursor_ + 1);
                next_history->push_back(request.target);
                next_cursor = next_history->size() - 1;
            }
            break;

        case NavigationAction::Back:
            if (cursor_ == 0 || history[cursor_ - 1] != request.target)
            {
                return false;
            }
            --next_cursor;
            break;

        case NavigationAction::Forward:
            if (cursor_ + 1 >= history.size() || history[cursor_ + 1] != request.target)
            {
                return false;
            }
            ++next_cursor;
            break;

        case NavigationAction::Refresh:
        case NavigationAction::Relocate:
        case NavigationAction::DeleteLocation:
            if (!has_current || history[cursor_] != request.target)
            {
                return false;
            }
            break;
        default: return false;
        }

        if (next_history) { history_.swap(*next_history); }
        else if (relocated_history_) { history_.swap(*relocated_history_); }
        relocated_history_.reset(); cursor_ = next_cursor;
        pending_.reset();
        return true;
    }

    bool NavigationModel::Reject(const NavigationRequest& request) noexcept
    {
        if (!IsPending(request))
        {
            return false;
        }
        pending_.reset();
        return true;
    }

    void NavigationModel::Shutdown() noexcept
    {
        shutdown_ = true;
        pending_.reset();
        relocated_history_.reset();
        ++next_token_;
    }

    NavigationSnapshot NavigationModel::Snapshot() const
    {
        NavigationSnapshot snapshot;
        if (!history_.empty())
        {
            snapshot.committed_location = history_[cursor_];
            if (snapshot.committed_location.IsFolder())
            {
                snapshot.committed_current_path = snapshot.committed_location.FolderPath();
            }
            snapshot.back_history.assign(history_.begin(), history_.begin() + cursor_);
            snapshot.forward_history.assign(history_.begin() + cursor_ + 1, history_.end());
            snapshot.can_go_back = cursor_ != 0;
            snapshot.can_go_forward = cursor_ + 1 < history_.size();
        }
        snapshot.has_pending = pending_.has_value();
        snapshot.pending_token = pending_.has_value() ? pending_->token : 0;
        if (pending_.has_value())
        {
            snapshot.pending_location = pending_->target;
        }
        return snapshot;
    }

    std::optional<NavigationRequest> NavigationModel::Begin(
        const NavigationAction action,
        Location target)
    {
        if (shutdown_ || !target.IsValid())
        {
            return std::nullopt;
        }

        NavigationRequest request;
        request.token = ++next_token_;
        request.action = action;
        request.target = std::move(target);
        pending_ = request;
        return request;
    }

    void NavigationModel::InvalidatePending() noexcept
    {
        pending_.reset();
        ++next_token_;
    }

    std::optional<Location> NavigationModel::NormalizeTarget(const Location& target)
    {
        if (!target.IsValid())
        {
            return std::nullopt;
        }
        if (target.IsThisPC())
        {
            return Location::ThisPC();
        }
        return Location::Folder(target.FolderPath());
    }
}
