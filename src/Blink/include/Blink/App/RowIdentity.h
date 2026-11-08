#pragma once

#include "Blink/Core/FileTypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace blink::app
{
    // A row identity is scoped to one committed directory listing. The
    // FileIdentity remains the authoritative filesystem identity; the name is
    // the link discriminator that keeps two hard-link names as two rows.
    struct RowIdentity
    {
        blink::core::FileIdentity file_identity;
        std::wstring name;

        friend bool operator==(const RowIdentity&, const RowIdentity&) = default;
    };

    struct RowIdentityHash
    {
        [[nodiscard]] std::size_t operator()(const RowIdentity& row) const noexcept
        {
            std::size_t h = std::hash<std::uint64_t>{}(row.file_identity.volume_serial);
            for (const auto byte : row.file_identity.file_id)
            {
                h = (h * 131) + byte;
            }
            h ^= std::hash<std::wstring>{}(row.name) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct FileIdentityHash
    {
        [[nodiscard]] std::size_t operator()(const blink::core::FileIdentity& id) const noexcept
        {
            std::size_t h = std::hash<std::uint64_t>{}(id.volume_serial);
            for (const auto byte : id.file_id)
            {
                h = (h * 131) + byte;
            }
            return h;
        }
    };

    [[nodiscard]] inline RowIdentity MakeRowIdentity(
        const blink::core::FileEntry& entry)
    {
        return RowIdentity{ entry.identity, entry.name };
    }
}
