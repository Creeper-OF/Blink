#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace blink::app
{
    enum class LocationKind
    {
        ThisPC,
        Folder,
    };

    // App-private navigation value. ThisPC is a virtual Blink location and
    // never gets encoded as a filesystem path.
    class Location
    {
    public:
        Location() noexcept = default;

        [[nodiscard]] static Location ThisPC() noexcept;
        [[nodiscard]] static Location Folder(std::wstring path);

        [[nodiscard]] LocationKind Kind() const noexcept { return kind_; }
        [[nodiscard]] bool IsThisPC() const noexcept { return kind_ == LocationKind::ThisPC; }
        [[nodiscard]] bool IsFolder() const noexcept { return kind_ == LocationKind::Folder; }
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] const std::wstring& FolderPath() const noexcept { return folder_path_; }

        // Returns the parent Folder, or ThisPC for a volume root. ThisPC has
        // no parent and returns nullopt.
        [[nodiscard]] std::optional<Location> Up() const;

        friend bool operator==(const Location&, const Location&) noexcept;
        friend bool operator!=(const Location& left, const Location& right) noexcept
        {
            return !(left == right);
        }

    private:
        explicit Location(LocationKind kind, std::wstring folder_path) noexcept
            : kind_(kind), folder_path_(std::move(folder_path))
        {
        }

        LocationKind kind_{ LocationKind::ThisPC };
        std::wstring folder_path_;
    };

    // Path-only relocation for the selected folder and its descendants.
    // Unrelated locations, including sibling names sharing a prefix, return nullopt.
    [[nodiscard]] std::optional<Location> RebaseFolderLocation(const Location& location,
        const Location& source, const Location& destination);
    [[nodiscard]] bool IsFolderAtOrBelow(const Location& location, const Location& root);

    enum class ThisPcItemKind
    {
        Place,
        Volume,
    };

    struct ThisPcItem
    {
        ThisPcItemKind kind{ ThisPcItemKind::Place };
        // Places use a resource key; volumes use display_name for the current
        // root path. The target Location is the authoritative action target.
        std::wstring label_key;
        std::wstring display_name;
        Location target;
    };

    struct ThisPcContent
    {
        std::vector<ThisPcItem> items;
    };

    // Builds the Blink-native This PC model. Volume entries are ordinary
    // Folder locations for currently accessible local logical roots; they do
    // not use Shell Namespace identity.
    [[nodiscard]] ThisPcContent EnumerateThisPcContent();
}
