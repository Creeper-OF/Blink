#pragma once

#include "Blink/Core/FileTypes.h"

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace blink::index
{
    struct DirectoryChange
    {
        DWORD action{};
        std::wstring name;
        std::wstring full_path;
        std::uint64_t file_id{};
        std::uint64_t parent_file_id{};
        std::uint64_t size{};
        DWORD attributes{};
    };

    [[nodiscard]] std::vector<DirectoryChange> WatchDirectoryOnce(
        const std::wstring& path,
        std::chrono::milliseconds timeout,
        bool watch_subtree = true,
        DWORD* error = nullptr);
}
