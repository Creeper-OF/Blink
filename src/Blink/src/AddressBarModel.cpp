#include "Blink/App/AddressBarModel.h"
#include "Blink/Core/FileTypes.h"

#include <algorithm>
#include <utility>

namespace blink::app
{
    namespace
    {
        bool HasOrdinalPrefix(const std::wstring_view value, const std::wstring_view prefix)
        {
            return prefix.empty() || (value.size() >= prefix.size() &&
                CompareStringOrdinal(value.data(), static_cast<int>(prefix.size()),
                    prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL);
        }

        bool EqualOrdinal(const std::wstring_view left, const std::wstring_view right)
        { return left.size() == right.size() && HasOrdinalPrefix(left, right); }

        bool IsDriveLetter(const wchar_t value)
        { return (value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z'); }

        std::wstring Separators(std::wstring_view value)
        {
            std::wstring result(value);
            std::replace(result.begin(), result.end(), L'/', L'\\');
            return result;
        }

        // For UNC roots the optional last separator is outside the root;
        // drive roots include their mandatory backslash.
        std::size_t CompletionRootLength(const std::wstring_view path)
        {
            if (path.size() >= 3 && IsDriveLetter(path[0]) && path[1] == L':' && path[2] == L'\\')
            { return 3; }
            if (path.size() >= 7 && path.starts_with(L"\\\\?\\") &&
                IsDriveLetter(path[4]) && path[5] == L':' && path[6] == L'\\')
            { return 7; }

            std::size_t server_start{};
            if (HasOrdinalPrefix(path, L"\\\\?\\UNC\\")) { server_start = 8; }
            else if (path.starts_with(L"\\\\") && !path.starts_with(L"\\\\?\\") && !path.starts_with(L"\\\\.\\"))
            { server_start = 2; }
            else { return 0; }
            const auto server_end = path.find(L'\\', server_start);
            if (server_end == std::wstring_view::npos || server_end == server_start) { return 0; }
            const auto share_start = server_end + 1;
            const auto separator = path.find(L'\\', share_start);
            const auto share_end = separator == std::wstring_view::npos ? path.size() : separator;
            if (share_end == share_start) { return 0; }
            return share_end;
        }

        std::wstring CompletionDirectory(const std::wstring& absolute)
        {
            // Only absolute filesystem paths reach NormalizePath here. It
            // normalizes . and .. lexically without probing the directory.
            auto directory = blink::core::NormalizePath(absolute);
            const auto root_length = CompletionRootLength(directory);
            if (root_length == 0) { return {}; }
            while (directory.size() > root_length && directory.back() == L'\\') { directory.pop_back(); }
            return directory;
        }

        AddressCompletionPlan DirectoryCompletion(const std::wstring& absolute, std::wstring prefix = {})
        {
            auto directory = CompletionDirectory(absolute);
            if (directory.empty()) { return {}; }
            return { AddressCompletionSource::Directory, std::move(directory), std::move(prefix) };
        }

        bool IsCompletionRoot(const Location& location)
        {
            const auto path = Separators(location.FolderPath());
            const auto root_length = CompletionRootLength(path);
            return root_length != 0 && (path.size() == root_length ||
                (path.size() == root_length + 1 && path.back() == L'\\'));
        }

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

    AddressCompletionPlan BuildAddressCompletionPlan(const std::wstring_view draft,
        const Location& committed, const std::wstring_view this_pc_label)
    {
        if (draft.find(L'\0') != std::wstring_view::npos) { return {}; }
        auto alias = draft;
        while (!alias.empty() && (alias.front() == L' ' || alias.front() == L'\t')) { alias.remove_prefix(1); }
        while (!alias.empty() && (alias.back() == L' ' || alias.back() == L'\t')) { alias.remove_suffix(1); }
        if (!this_pc_label.empty() && EqualOrdinal(alias, this_pc_label)) { return {}; }

        auto text = Separators(draft);
        // Environment/shell/URI syntax is not expanded by this local folder
        // completion. A literal percent elsewhere remains a folder character.
        if ((!text.empty() && text.front() == L'%' && text.find(L'%', 1) != std::wstring::npos) ||
            text.starts_with(L"\\\\.\\"))
        { return {}; }
        const bool drive_prefix = text.size() >= 2 && IsDriveLetter(text[0]) && text[1] == L':';
        const bool extended_drive = text.size() >= 6 && text.starts_with(L"\\\\?\\") &&
            IsDriveLetter(text[4]) && text[5] == L':';
        const auto colon = text.find(L':');
        if (colon != std::wstring::npos &&
            ((!drive_prefix || colon != 1) && (!extended_drive || colon != 5)))
        { return {}; }
        if (colon != std::wstring::npos && text.find(L':', colon + 1) != std::wstring::npos) { return {}; }
        if (drive_prefix && text.size() == 2)
        { return { AddressCompletionSource::ThisPC, {}, std::move(text) }; }
        if (drive_prefix && text[2] != L'\\') { return {}; }

        auto root_length = CompletionRootLength(text);
        if (text.starts_with(L"\\\\") && root_length == 0) { return {}; }
        if (root_length == 0)
        {
            if (committed.IsThisPC())
            {
                if (text == L"." || text == L".." || text.find(L'\\') != std::wstring::npos) { return {}; }
                return { AddressCompletionSource::ThisPC, {}, std::move(text) };
            }
            if (!committed.IsValid()) { return {}; }
            const auto base = Separators(committed.FolderPath());
            const auto base_root = CompletionRootLength(base);
            if (base_root == 0) { return {}; }
            text = !text.empty() && text.front() == L'\\' ?
                blink::core::JoinPath(base.substr(0, base_root), text.substr(1)) :
                blink::core::JoinPath(base, text);
            root_length = CompletionRootLength(text);
        }

        if (text.size() == root_length || text.back() == L'\\')
        { return DirectoryCompletion(text); }
        const auto separator = text.find_last_of(L'\\');
        auto prefix = text.substr(separator + 1);
        if (prefix == L"." || prefix == L"..")
        { return DirectoryCompletion(text); }
        const auto parent_length = (std::max)(root_length, separator);
        return DirectoryCompletion(text.substr(0, parent_length), std::move(prefix));
    }

    std::vector<AddressBreadcrumb> FilterAddressCompletions(const AddressCompletionPlan& plan,
        const std::vector<AddressBreadcrumb>& available, const std::size_t max)
    {
        std::vector<AddressBreadcrumb> result;
        if (plan.source == AddressCompletionSource::None || max == 0) { return result; }
        for (const auto& item : available)
        {
            if (item.label.empty() || !item.location.IsFolder() || !item.location.IsValid() ||
                (!HasOrdinalPrefix(item.label, plan.prefix) &&
                    !(plan.source == AddressCompletionSource::ThisPC && IsCompletionRoot(item.location) &&
                        HasOrdinalPrefix(item.location.FolderPath(), plan.prefix))))
            { continue; }
            if (std::any_of(result.begin(), result.end(), [&](const AddressBreadcrumb& existing) {
                return existing.location == item.location;
            }))
            { continue; }
            result.push_back(item);
            if (result.size() == max) { break; }
        }
        return result;
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
