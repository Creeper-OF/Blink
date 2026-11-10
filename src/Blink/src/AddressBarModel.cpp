#include "Blink/App/AddressBarModel.h"

#include <algorithm>
#include <utility>

namespace blink::app
{
    namespace
    {
        std::wstring_view WithoutTrailingSeparators(std::wstring_view path)
        {
            while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
            {
                path.remove_suffix(1);
            }
            return path;
        }

        std::wstring FolderLabel(const Location& location, const bool root)
        {
            if (root) { return location.FolderPath(); }
            const auto path = WithoutTrailingSeparators(location.FolderPath());
            const auto separator = path.find_last_of(L"\\/");
            return std::wstring(separator == std::wstring_view::npos ?
                path : path.substr(separator + 1));
        }
    }

    std::vector<AddressBreadcrumb> BuildAddressBreadcrumbs(
        const Location& location, const std::wstring_view this_pc_label)
    {
        if (!location.IsValid()) { return {}; }

        std::vector<Location> ancestors{ location };
        auto current = location;
        while (const auto parent = current.Up())
        {
            if (!parent->IsValid() || *parent == current) { break; }
            // Removing a trailing separator is not another directory level.
            // Keep the original leaf target, including its submitted spelling.
            const bool same_folder = parent->IsFolder() && current.IsFolder() &&
                WithoutTrailingSeparators(parent->FolderPath()) ==
                    WithoutTrailingSeparators(current.FolderPath());
            if (!same_folder) { ancestors.push_back(*parent); }
            current = *parent;
        }
        std::reverse(ancestors.begin(), ancestors.end());

        std::vector<AddressBreadcrumb> breadcrumbs;
        breadcrumbs.reserve(ancestors.size());
        for (std::size_t index = 0; index < ancestors.size(); ++index)
        {
            const auto& target = ancestors[index];
            auto label = target.IsThisPC() ? std::wstring(this_pc_label) :
                FolderLabel(target, index == 0 || ancestors[index - 1].IsThisPC());
            breadcrumbs.push_back({ std::move(label), target });
        }
        return breadcrumbs;
    }

    std::vector<Location> BuildAddressHistory(
        const NavigationSnapshot& snapshot, const std::size_t max)
    {
        std::vector<Location> history;
        const auto append = [&](const Location& location) {
            if (history.size() < max && location.IsValid() &&
                std::find(history.begin(), history.end(), location) == history.end())
            {
                history.push_back(location);
            }
        };
        append(snapshot.committed_location);
        for (auto it = snapshot.back_history.rbegin();
            it != snapshot.back_history.rend() && history.size() < max; ++it)
        {
            append(*it);
        }
        for (const auto& location : snapshot.forward_history)
        {
            if (history.size() == max) { break; }
            append(location);
        }
        return history;
    }

    void AddressBarState::Present(const std::uint64_t owner, std::wstring committed,
        const bool preserve_draft)
    {
        const bool keep_draft = owner_ == owner && editing_ && preserve_draft;
        owner_ = owner;
        committed_ = std::move(committed);
        if (!keep_draft)
        {
            editing_ = false;
            draft_ = committed_;
        }
    }

    void AddressBarState::BeginEdit()
    {
        if (editing_) { return; }
        draft_ = committed_;
        editing_ = true;
    }

    void AddressBarState::SetDraft(std::wstring draft)
    {
        if (editing_) { draft_ = std::move(draft); }
    }

    void AddressBarState::Cancel()
    {
        editing_ = false;
        draft_ = committed_;
    }
}
