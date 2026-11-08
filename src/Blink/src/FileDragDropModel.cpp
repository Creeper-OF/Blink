#include "Blink/App/FileDragDropModel.h"
#include "Blink/App/FileOperationRelocation.h"
#include <algorithm>

namespace blink::app
{
    DWORD CaptureFileDragSelection(const FileOperationOrigin& source, std::span<const blink::core::FileEntry> entries, FileDragSelection& output)
    {
        output = {};
        try
        {
            if (!source.location.IsFolder() || !source.identity.valid || entries.empty() || entries.size() > MaxClipboardFileCount)
            { return ERROR_INVALID_PARAMETER; }
            FileDragSelection pending; pending.parent_identity = source.identity;
            bool directories{}; for (const auto& entry : entries) { directories |= entry.is_directory; }
            CopyItemRequests mixed; std::vector<FileCopyRequest> files;
            for (const auto& entry : entries)
            {
                if (!entry.identity.valid || entry.identity.volume_serial != source.identity.volume_serial || entry.name.empty() ||
                    entry.name == L"." || entry.name == L".." || entry.name.find_first_of(L"\\/:\0", 0, 4) != std::wstring::npos)
                { return ERROR_FILE_INVALID; }
                FileCopyRequest request; request.source_parent = source.location.FolderPath(); request.source_parent_identity = source.identity;
                request.source = entry; request.source.attributes = entry.is_directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
                pending.paths.push_back(blink::core::JoinPath(request.source_parent, entry.name));
                if (!directories) { files.push_back(std::move(request)); }
                else if (entry.is_directory) { mixed.emplace_back(DirectoryCopyProjection(request)); }
                else { mixed.emplace_back(std::move(request)); }
            }
            if (EncodeClipboardFilePaths(pending.paths).empty()) { return ERROR_NOT_SUPPORTED; }
            if (!directories) { pending.expected = std::move(files); }
            else if (mixed.size() == 1) { pending.expected = std::get<DirectoryCopyRequest>(std::move(mixed.front())); }
            else { pending.expected = std::move(mixed); }
            GUID guid{}; const auto created = CoCreateGuid(&guid); if (FAILED(created)) { return ERROR_GEN_FAILURE; }
            wchar_t token[40]{}; if (!StringFromGUID2(guid, token, 40)) { return ERROR_GEN_FAILURE; } pending.token = token;
            output = std::move(pending); return ERROR_SUCCESS;
        }
        catch (const std::bad_alloc&) { output = {}; return ERROR_NOT_ENOUGH_MEMORY; }
        catch (...) { output = {}; return ERROR_GEN_FAILURE; }
    }
    PreparedFileDrag PrepareFileDrag(const FileDragDataReference& reference, const std::optional<FileDragSelection>& own,
        const FileOperationCancellation& cancelled) noexcept
    {
        PreparedFileDrag result;
        try
        {
            if (cancelled && cancelled->load()) { result.error = HRESULT_FROM_WIN32(ERROR_CANCELLED); return result; }
            winrt::com_ptr<IDataObject> object; result.error = reference.Resolve(object); if (FAILED(result.error)) { return result; }
            result.payload = ReadFileDragData(object.get()); result.error = result.payload.error; if (FAILED(result.error)) { return result; }
            result.error = HRESULT_FROM_WIN32(CaptureClipboardCopySources(result.payload.paths, result.sources, cancelled));
            if (FAILED(result.error)) { return result; }
            if (own && own->token == result.payload.token &&
                (own->paths != result.payload.paths || !SameCapturedCopySources(own->expected, result.sources)))
            { result.sources.emplace<std::monostate>(); result.error = HRESULT_FROM_WIN32(ERROR_FILE_INVALID); }
        }
        catch (const std::bad_alloc&) { result.sources.emplace<std::monostate>(); result.error = E_OUTOFMEMORY; }
        catch (...) { result.sources.emplace<std::monostate>(); result.error = E_FAIL; }
        return result;
    }
    DWORD ChooseFileDropEffect(const ClipboardCopySources& sources, const std::wstring& destination,
        const blink::core::FileIdentity& identity, const DWORD allowed, const FileDropModifiers modifiers)
    {
        if (!identity.valid || destination.empty() || modifiers.alt || (modifiers.control && modifiers.shift) ||
            !SameCapturedCopySources(sources, sources)) { return DROPEFFECT_NONE; }
        bool same_volume = true, same_parent = true, recursive{};
        const auto target = Location::Folder(destination);
        const auto inspect = [&](const auto& request) {
            same_volume &= request.source.identity.volume_serial == identity.volume_serial;
            same_parent &= request.source_parent_identity == identity;
            if (request.source.is_directory && IsFolderAtOrBelow(target,
                Location::Folder(blink::core::JoinPath(request.source_parent, request.source.name)))) { recursive = true; }
        };
        if (const auto* files = std::get_if<std::vector<FileCopyRequest>>(&sources)) { for (const auto& file : *files) { inspect(file); } }
        else if (const auto* directory = std::get_if<DirectoryCopyRequest>(&sources)) { inspect(*directory); }
        else if (const auto* items = std::get_if<CopyItemRequests>(&sources)) { for (const auto& item : *items) { std::visit(inspect, item); } }
        if (recursive) { return DROPEFFECT_NONE; }
        auto effect = modifiers.control ? DROPEFFECT_COPY : modifiers.shift ? DROPEFFECT_MOVE : same_volume ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        if (!(allowed & effect))
        {
            if (modifiers.control || modifiers.shift) { return DROPEFFECT_NONE; }
            effect = allowed & DROPEFFECT_COPY ? DROPEFFECT_COPY : allowed & DROPEFFECT_MOVE ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
        }
        return effect == DROPEFFECT_MOVE && same_parent ? DROPEFFECT_NONE : effect;
    }
    HRESULT ReadFileDropTarget(const std::wstring& path, blink::core::FileIdentity& identity) noexcept
    {
        identity = {};
        try
        {
            auto native = blink::core::NormalizePath(path);
            if (native.empty()) { return E_INVALIDARG; }
            if (!native.starts_with(L"\\\\?\\")) { native = native.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + native.substr(2) : L"\\\\?\\" + native; }
            blink::core::FileEntry entry;
            if (!blink::core::ReadEntryMetadata(native, entry)) { const auto error = GetLastError(); return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE); }
            if (!entry.is_directory || (entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) || !entry.identity.valid) { return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); }
            identity = entry.identity; return S_OK;
        }
        catch (...) { return E_OUTOFMEMORY; }
    }
    FileOperationOrigin FileDropOrigin(const FileOperationOrigin& owner, const Location& target, const blink::core::FileIdentity& identity)
    {
        auto result = owner; result.owner_location = owner.location; result.owner_identity = owner.identity;
        result.location = target; result.identity = identity; return result;
    }
    std::optional<FileDropOutcome> CompletedFileDrop(const FileOperationSnapshot& value) noexcept
    {
        using Stage = FileOperationStage;
        if ((value.kind != FileOperationKind::Copy && value.kind != FileOperationKind::Move) || !FileOperationIsTerminal(value.stage) ||
            (value.copy_batch && value.copy_batch->resumable) || (value.move_batch && value.move_batch->resumable)) { return {}; }
        FileDropOutcome result;
        result.error = value.stage == Stage::Completed || value.stage == Stage::Skipped ? S_OK : value.stage == Stage::Cancelled ? E_ABORT :
            value.result.error ? HRESULT_FROM_WIN32(value.result.error) : E_FAIL;
        if (SUCCEEDED(result.error))
        {
            if (value.kind == FileOperationKind::Move && value.move_batch && value.move_batch->moved_count) { result.logical_effect = DROPEFFECT_MOVE; }
            else if (value.kind == FileOperationKind::Copy && value.copy_batch && value.copy_batch->copied_count) { result.logical_effect = DROPEFFECT_COPY; }
        }
        return result;
    }
}
