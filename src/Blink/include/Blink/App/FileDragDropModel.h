#pragma once
#include "Blink/App/FileDragData.h"
#include "Blink/App/FileOperationOrigin.h"
#include "Blink/App/MoveTransferPresentation.h"

namespace blink::app
{
    struct FileDragSelection
    {
        std::wstring token;
        std::vector<std::wstring> paths;
        ClipboardCopySources expected;
        blink::core::FileIdentity parent_identity;
    };
    struct PreparedFileDrag
    {
        HRESULT error{ E_PENDING };
        FileDragPayload payload;
        ClipboardCopySources sources;
    };
    struct FileDropModifiers { bool control{}, shift{}, alt{}; };
    [[nodiscard]] DWORD CaptureFileDragSelection(const FileOperationOrigin& source,
        std::span<const blink::core::FileEntry> entries, FileDragSelection& output);
    [[nodiscard]] PreparedFileDrag PrepareFileDrag(const FileDragDataReference& reference,
        const std::optional<FileDragSelection>& own, const FileOperationCancellation& cancelled = {}) noexcept;
    [[nodiscard]] DWORD ChooseFileDropEffect(const ClipboardCopySources& sources, const std::wstring& destination,
        const blink::core::FileIdentity& identity, DWORD allowed, FileDropModifiers modifiers);
    [[nodiscard]] HRESULT ReadFileDropTarget(const std::wstring& path, blink::core::FileIdentity& identity) noexcept;
    [[nodiscard]] FileOperationOrigin FileDropOrigin(const FileOperationOrigin& owner, const Location& target,
        const blink::core::FileIdentity& identity = {});
    struct FileDropOutcome { HRESULT error{ E_FAIL }; DWORD logical_effect{}; };
    [[nodiscard]] std::optional<FileDropOutcome> CompletedFileDrop(const FileOperationSnapshot& snapshot) noexcept;
}
