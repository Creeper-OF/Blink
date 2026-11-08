#include "Blink/App/SearchViewModel.h"
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
}
