#pragma once

#include "Blink/App/Location.h"

#include <string>
#include <vector>

namespace blink::app
{
    struct NavigationPaneSelection
    {
        int place_index{ -1 };
        int drive_index{ -1 };
    };

    // Presentation-only mapping from the committed location to the exact
    // Place/Drive row. Descendants intentionally do not select their parent
    // Place; NavigationModel/TabState remain authoritative for location.
    [[nodiscard]] NavigationPaneSelection ResolveNavigationPaneSelection(
        const Location& committed_location,
        const std::vector<Location>& place_locations,
        const std::vector<Location>& drive_locations);

    // Compatibility adapter for the Phase 1A presentation tests. It converts
    // paths at the boundary and still performs Location equality internally.
    [[nodiscard]] NavigationPaneSelection ResolveNavigationPaneSelection(
        const std::wstring& committed_path,
        const std::vector<std::wstring>& place_paths,
        const std::vector<std::wstring>& drive_paths);
}
