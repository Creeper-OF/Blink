#include "Blink/App/NavigationPane.h"

#include <Windows.h>

namespace blink::app
{
    namespace
    {
        [[nodiscard]] int ExactLocationIndex(
            const Location& committed_location,
            const std::vector<Location>& targets)
        {
            for (std::size_t index = 0; index < targets.size(); ++index)
            {
                if (committed_location == targets[index])
                {
                    return static_cast<int>(index);
                }
            }
            return -1;
        }
    }

    NavigationPaneSelection ResolveNavigationPaneSelection(
        const Location& committed_location,
        const std::vector<Location>& place_locations,
        const std::vector<Location>& drive_locations)
    {
        if (!committed_location.IsFolder())
        {
            return {};
        }

        const int place_index = ExactLocationIndex(committed_location, place_locations);
        if (place_index >= 0)
        {
            return NavigationPaneSelection{ place_index, -1 };
        }

        const int drive_index = ExactLocationIndex(committed_location, drive_locations);
        return NavigationPaneSelection{ -1, drive_index };
    }

    NavigationPaneSelection ResolveNavigationPaneSelection(
        const std::wstring& committed_path,
        const std::vector<std::wstring>& place_paths,
        const std::vector<std::wstring>& drive_paths)
    {
        std::vector<Location> place_locations;
        place_locations.reserve(place_paths.size());
        for (const auto& path : place_paths)
        {
            place_locations.push_back(Location::Folder(path));
        }

        std::vector<Location> drive_locations;
        drive_locations.reserve(drive_paths.size());
        for (const auto& path : drive_paths)
        {
            drive_locations.push_back(Location::Folder(path));
        }

        return ResolveNavigationPaneSelection(
            Location::Folder(committed_path),
            place_locations,
            drive_locations);
    }
}
