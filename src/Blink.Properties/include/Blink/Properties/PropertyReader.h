#pragma once

#include "Blink/Core/FileTypes.h"

#include <Windows.h>

#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace blink::properties
{
    struct PropertyValue
    {
        std::wstring key;
        std::wstring value;
    };

    struct StreamInfo
    {
        std::wstring name;
        std::uint64_t size{};
        std::uint64_t allocated_size{};
    };

    struct AclEntry
    {
        std::wstring sid;
        BYTE type{};
        BYTE flags{};
        ACCESS_MASK mask{};
        bool inherited{};
    };

    struct SecuritySnapshot
    {
        bool available{};
        bool dacl_present{};
        bool dacl_protected{};
        std::wstring owner_sid;
        std::vector<AclEntry> entries;
        DWORD error{};
    };

    struct FileProperties
    {
        bool available{};
        DWORD error{};
        blink::core::FileEntry entry{};
        std::uint32_t hard_link_count{};
        bool compressed{};
        WORD compression_format{};
        std::vector<StreamInfo> streams;
        std::vector<PropertyValue> metadata;
        SecuritySnapshot security;
    };

    [[nodiscard]] FileProperties ReadFileProperties(
        const std::wstring& path,
        bool read_metadata = true,
        bool read_security = true);

    [[nodiscard]] std::future<FileProperties> ReadFilePropertiesAsync(
        std::wstring path,
        bool read_metadata = true,
        bool read_security = true);
}

