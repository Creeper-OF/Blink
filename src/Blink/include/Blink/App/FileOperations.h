#pragma once

#include "Blink/Core/FileTypes.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string_view>

namespace blink::app
{
    struct NewFolderRequest
    {
        std::wstring parent;
        blink::core::FileIdentity parent_identity;
        std::wstring name;
    };

    struct FileOperationResult
    {
        DWORD error{ ERROR_GEN_FAILURE };
        bool committed{};
        std::wstring path;
        bool partial_file_left{};
        bool skipped{};
    };

    using NewFolderResult = FileOperationResult;

    using FileOperationCancellation = std::shared_ptr<std::atomic_bool>;
    using CreateDirectoryFunction = std::function<bool(const std::wstring&, DWORD*)>;

    [[nodiscard]] DWORD ValidateNewFolderName(std::wstring_view name) noexcept;
    // Worker-only. The optional create function runs with the validated
    // directory chain held; it is also the native failure/race test seam.
    [[nodiscard]] NewFolderResult CreateFolderSafely(const NewFolderRequest& request,
        const FileOperationCancellation& cancelled = {}, const CreateDirectoryFunction& create = {});
}
