#include "Blink/App/SearchContextCommands.h"
#include "Blink/App/TabWindowState.h"
#include <algorithm>
#include <new>
#include <utility>

namespace blink::app
{
    bool IsSearchContextCurrent(const SearchContextSelection& selection, const TabRecord* tab, const bool closed) noexcept
    {
        if (closed || !tab || !selection.tab_id || tab->id != selection.tab_id || !selection.query_generation ||
            !tab->search_visible || tab->search_queued || !tab->search_state ||
            !tab->search_state->IsCurrent(selection.query_generation) || !tab->search_snapshot ||
            tab->search_snapshot->generation != selection.query_generation ||
            (tab->search_snapshot->stage != SearchStage::Complete && tab->search_snapshot->stage != SearchStage::Partial))
        { return false; }
        return std::any_of(tab->search_snapshot->volumes.begin(), tab->search_snapshot->volumes.end(), [&](const auto& volume) {
            return volume.searched && std::any_of(volume.results.begin(), volume.results.end(), [&](const auto& result) {
                return SameSearchLink(selection.result, result);
            });
        });
    }

    DWORD BuildSearchClipboardSources(const SearchActionTarget& target, ClipboardCopySources& output)
    {
        output.emplace<std::monostate>();
        if (target.error) { return target.error; }
        try
        {
            const auto& entry = target.entry;
            if (!entry.identity.valid || !target.parent_identity.valid ||
                entry.identity.volume_serial != target.parent_identity.volume_serial)
            { return ERROR_FILE_INVALID; }
            if (const auto error = ValidateNewFolderName(entry.name); error) { return error; }
            const auto slash = entry.full_path.find_last_of(L'\\');
            if (SearchDriveRoot(target.parent).empty() || slash == std::wstring::npos || entry.full_path.size() <= 3 ||
                entry.full_path.substr(slash + 1) != entry.name)
            { return ERROR_INVALID_DATA; }
            const auto parent = entry.full_path.substr(0, slash == 2 ? 3 : slash);
            if (CompareStringOrdinal(parent.c_str(), -1, target.parent.c_str(), -1, TRUE) != CSTR_EQUAL)
            { return ERROR_INVALID_DATA; }
            if ((entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                entry.is_directory != ((entry.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
            { return ERROR_NOT_SUPPORTED; }
            FileCopyRequest request{ target.parent, target.parent_identity, entry };
            if (entry.is_directory) { output.emplace<DirectoryCopyRequest>(DirectoryCopyProjection(request)); }
            else
            {
                std::vector<FileCopyRequest> files; files.push_back(std::move(request));
                output.emplace<std::vector<FileCopyRequest>>(std::move(files));
            }
            return ERROR_SUCCESS;
        }
        catch (const std::bad_alloc&) { output.emplace<std::monostate>(); return ERROR_NOT_ENOUGH_MEMORY; }
        catch (...) { output.emplace<std::monostate>(); return ERROR_GEN_FAILURE; }
    }
}
