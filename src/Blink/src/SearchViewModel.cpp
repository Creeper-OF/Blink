#include "Blink/App/SearchViewModel.h"
#include <algorithm>
namespace blink::app
{
    SearchViewData BuildSearchView(const SearchSnapshot& snapshot, ItemVisibilityPolicy policy, const std::optional<SearchResult>& selection)
    {
        SearchViewData data;
        for (const auto& volume : snapshot.volumes)
        {
            if (!volume.searched) { continue; }
            data.limit_reached = data.limit_reached || volume.limit_reached;
            data.query_incomplete = data.query_incomplete || volume.query_incomplete;
            for (const auto& result : volume.results)
            {
                if (!policy.Includes(result.hit.attributes)) { continue; }
                if (selection && SameSearchLink(*selection, result)) { data.selected_index = data.rows.size(); }
                data.rows.push_back(result);
            }
        }
        return data;
    }

    bool CanAppendSearchRows(const std::vector<SearchResult>& current, const std::vector<SearchResult>& incoming) noexcept
    {
        return current.size() <= incoming.size() &&
            std::equal(current.begin(), current.end(), incoming.begin(), [](const auto& left, const auto& right) {
                return left.source_volume == right.source_volume && left.hit.full_path == right.hit.full_path &&
                    left.hit.name == right.hit.name && left.hit.identity == right.hit.identity &&
                    left.hit.parent_identity == right.hit.parent_identity && left.hit.attributes == right.hit.attributes &&
                    left.hit.is_directory == right.hit.is_directory && left.hit.last_usn == right.hit.last_usn;
            });
    }
}
