#pragma once
#include "Blink/App/FileCopyBatch.h"
#include "Blink/App/DirectoryCopyOperation.h"
#include <cstddef>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace blink::app
{
    inline constexpr std::size_t MaxClipboardFileCount = 1024;
    inline constexpr std::size_t MaxClipboardFilePayloadBytes = 4 * 1024 * 1024;
    enum class ClipboardTransferIntent { Copy, Move };
    struct ClipboardDropEffect
    {
        DWORD error{ ERROR_SUCCESS };
        ClipboardTransferIntent intent{ ClipboardTransferIntent::Copy };
    };
    [[nodiscard]] ClipboardDropEffect DecodeClipboardDropEffect(std::span<const std::byte> data, bool present = true);
    [[nodiscard]] DWORD ClipboardDropEffectValue(ClipboardTransferIntent intent) noexcept;

    struct ClipboardFilePaths
    {
        DWORD error{ ERROR_NOT_SUPPORTED };
        std::vector<std::wstring> paths;
        DWORD sequence{};
        ClipboardTransferIntent intent{ ClipboardTransferIntent::Copy };
    };
    struct ClipboardFilePath
    {
        DWORD error{ ERROR_NOT_SUPPORTED };
        std::wstring path;
        DWORD sequence{};
    };
    // Pure bounded Unicode CF_HDROP codec; no clipboard or filesystem access.
    // The entire list is accepted or rejected; ordering and duplicates survive.
    // Syntax validation does not classify files: capture every source before
    // submitting a copy batch. Nonzero tails after the final NUL are malformed.
    [[nodiscard]] std::vector<std::byte> EncodeClipboardFilePaths(std::span<const std::wstring> paths);
    [[nodiscard]] ClipboardFilePaths DecodeClipboardFilePaths(std::span<const std::byte> data);
    [[nodiscard]] std::vector<std::byte> EncodeClipboardFilePath(const std::wstring& path);
    [[nodiscard]] ClipboardFilePath DecodeClipboardFilePath(std::span<const std::byte> data);
    // Clipboard access may invoke another process's delayed renderer: worker-only.
    [[nodiscard]] DWORD PublishClipboardText(HWND owner, const std::wstring& text, DWORD& sequence);
    [[nodiscard]] DWORD PublishClipboardFilesCopy(HWND owner, std::span<const std::wstring> paths, DWORD& sequence);
    [[nodiscard]] ClipboardFilePaths ReadClipboardFilesCopy();
    [[nodiscard]] DWORD PublishClipboardFilesTransfer(HWND owner, std::span<const std::wstring> paths,
        ClipboardTransferIntent intent, DWORD& sequence);
    [[nodiscard]] ClipboardFilePaths ReadClipboardFilesTransfer();
    [[nodiscard]] DWORD PublishClipboardFileCopy(HWND owner, const std::wstring& path, DWORD& sequence);
    [[nodiscard]] ClipboardFilePath ReadClipboardFileCopy();
    // Worker-only metadata capture. Actual copying rechecks every captured identity.
    // Empty/oversized input is invalid. Any failure/cancellation clears requests.
    [[nodiscard]] DWORD CaptureFileCopySources(std::span<const std::wstring> paths,
        std::vector<FileCopyRequest>& requests, const FileOperationCancellation& cancelled = {});
    [[nodiscard]] DWORD CaptureFileCopySource(const std::wstring& path, FileCopyRequest& request);

    using ClipboardCopySources = std::variant<std::monostate,
        std::vector<FileCopyRequest>, DirectoryCopyRequest, CopyItemRequests>;
    // Worker-only metadata capture: ordinary files, directories, or a mixed list.
    // Files-only and single-directory representations remain stable; mixed or
    // multiple-directory selections use ordered CopyItemRequests, duplicates kept.
    // Destination fields stay empty; no tree/stream scan or copying occurs.
    // Directory ADS and descendant support are checked by the copy engine.
    // Any failure/cancellation resets output to monostate. Existing files-only
    // capture APIs continue to reject every directory source.
    [[nodiscard]] DWORD CaptureClipboardCopySources(std::span<const std::wstring> paths,
        ClipboardCopySources& output, const FileOperationCancellation& cancelled = {});
    // Worker-only single directory source capture for navigation targets.
    // Reuses the ordinary source backend; roots have no transferable parent
    // link, files/reparse points keep their real rejection, and failures or
    // cancellation clear output. No clipboard access or descendant scan.
    [[nodiscard]] DWORD CaptureDirectoryClipboardSource(const std::wstring& absolute_path,
        ClipboardCopySources& output, const FileOperationCancellation& cancelled = {});
}
