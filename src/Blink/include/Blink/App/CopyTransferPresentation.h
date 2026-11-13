#pragma once
#include "Blink/App/ClipboardFileTransfer.h"
#include "Blink/App/FileOperationCoordinator.h"
#include <type_traits>

namespace blink::app
{
    [[nodiscard]] inline bool SameCapturedCopySources(const ClipboardCopySources& expected, const ClipboardCopySources& actual) noexcept
    {
        if (expected.index() != actual.index()) { return false; }
        const auto same = [](const auto& left, const auto& right, bool directory) {
            return left.source_parent_identity.valid && right.source_parent_identity.valid &&
                left.source.identity.valid && right.source.identity.valid &&
                left.source_parent_identity == right.source_parent_identity && left.source.identity == right.source.identity &&
                !left.source.name.empty() && left.source.name == right.source.name &&
                left.source.is_directory == directory && right.source.is_directory == directory &&
                ((left.source.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory &&
                ((right.source.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory &&
                !(left.source.attributes & FILE_ATTRIBUTE_REPARSE_POINT) && !(right.source.attributes & FILE_ATTRIBUTE_REPARSE_POINT);
        };
        if (const auto* files = std::get_if<std::vector<FileCopyRequest>>(&expected))
        {
            const auto* values = std::get_if<std::vector<FileCopyRequest>>(&actual);
            if (!values || files->empty() || files->size() > MaxClipboardFileCount || files->size() != values->size()) { return false; }
            for (std::size_t index = 0; index < files->size(); ++index)
            { if (!same((*files)[index], (*values)[index], false)) { return false; } }
            return true;
        }
        if (const auto* items = std::get_if<CopyItemRequests>(&expected))
        {
            const auto* values = std::get_if<CopyItemRequests>(&actual);
            if (!values || items->empty() || items->size() > MaxClipboardFileCount || items->size() != values->size()) { return false; }
            for (std::size_t index = 0; index < items->size(); ++index)
            {
                const auto& left = (*items)[index]; const auto& right = (*values)[index];
                if (left.index() != right.index()) { return false; }
                const bool matches = std::visit([&](const auto& request) {
                    using Request = std::decay_t<decltype(request)>;
                    const auto* value = std::get_if<Request>(&right);
                    return value && same(request, *value, std::is_same_v<Request, DirectoryCopyRequest>);
                }, left);
                if (!matches) { return false; }
            }
            return true;
        }
        const auto* directory = std::get_if<DirectoryCopyRequest>(&expected);
        const auto* value = std::get_if<DirectoryCopyRequest>(&actual);
        return directory && value && same(*directory, *value, true);
    }

    struct ClipboardCopyCache
    {
        ClipboardCopySources sources;
        std::vector<std::wstring> paths;
        DWORD sequence{};
        ClipboardTransferIntent intent{ ClipboardTransferIntent::Copy };
        bool move_submitted{};
        [[nodiscard]] bool MatchesPayload(const ClipboardFilePaths& payload) const noexcept
        {
            if (payload.error != ERROR_SUCCESS || intent != payload.intent || !sequence || sequence != payload.sequence || paths != payload.paths ||
                !SameCapturedCopySources(sources, sources)) { return false; }
            const auto* files = std::get_if<std::vector<FileCopyRequest>>(&sources);
            const auto* items = std::get_if<CopyItemRequests>(&sources);
            return paths.size() == (files ? files->size() : items ? items->size() : 1);
        }
    };

    [[nodiscard]] inline DWORD PrepareCopyDestination(const ClipboardCopySources& sources, const std::wstring& parent,
        const blink::core::FileIdentity& identity, ClipboardCopySources& output)
    {
        try
        {
            if (parent.empty() || !identity.valid || !SameCapturedCopySources(sources, sources))
            { output.emplace<std::monostate>(); return ERROR_INVALID_PARAMETER; }
            auto pending = sources;
            output.emplace<std::monostate>();
            const auto bind = [&parent, &identity](auto& request) {
                request.destination_parent = parent; request.destination_parent_identity = identity;
                request.destination_name = request.source.name;
            };
            if (auto* files = std::get_if<std::vector<FileCopyRequest>>(&pending))
            { for (auto& request : *files) { bind(request); } }
            else if (auto* directory = std::get_if<DirectoryCopyRequest>(&pending)) { bind(*directory); }
            else if (auto* items = std::get_if<CopyItemRequests>(&pending))
            { for (auto& item : *items) { std::visit(bind, item); } }
            output = std::move(pending);
            return ERROR_SUCCESS;
        }
        catch (const std::bad_alloc&) { output.emplace<std::monostate>(); return ERROR_NOT_ENOUGH_MEMORY; }
        catch (...) { output.emplace<std::monostate>(); return ERROR_GEN_FAILURE; }
    }

    [[nodiscard]] inline bool CopyQueueHasRootConflict(const FileOperationSnapshot& value) noexcept
    {
        if (value.kind != FileOperationKind::Copy || value.stage != FileOperationStage::Failed || !value.copy_request ||
            (value.copy_batch && !value.copy_batch->resumable)) { return false; }
        const bool collision = value.result.error == ERROR_FILE_EXISTS || value.result.error == ERROR_ALREADY_EXISTS;
        if (!value.copy_request->source.is_directory) { return collision; }
        if (!value.directory_copy_request || !value.directory_copy_result || value.directory_copy_result->partial_tree_left ||
            value.directory_copy_result->directories_created || value.directory_copy_result->files_copied) { return false; }
        const auto& request = *value.directory_copy_request;
        const bool same_source_root = request.source_parent_identity == request.destination_parent_identity &&
            CompareStringOrdinal(request.source.name.c_str(), -1, request.destination_name.c_str(), -1, TRUE) == CSTR_EQUAL;
        return collision || (value.result.error == ERROR_INVALID_PARAMETER && same_source_root);
    }

    // The single-directory route waits for this same root decision in the UI,
    // even though it has no resumable file-batch snapshot.
    [[nodiscard]] inline bool DirectoryCopyHasRootConflict(const FileOperationSnapshot& value) noexcept
    {
        if (value.kind != FileOperationKind::DirectoryCopy || value.stage != FileOperationStage::Failed ||
            !value.directory_copy_request || !value.directory_copy_result || value.directory_copy_result->partial_tree_left ||
            value.directory_copy_result->directories_created || value.directory_copy_result->files_copied) { return false; }
        const auto& request = *value.directory_copy_request;
        const bool same_root = request.source_parent_identity == request.destination_parent_identity &&
            CompareStringOrdinal(request.source.name.c_str(), -1, request.destination_name.c_str(), -1, TRUE) == CSTR_EQUAL;
        return value.result.error == ERROR_ALREADY_EXISTS || value.result.error == ERROR_FILE_EXISTS ||
            (value.result.error == ERROR_INVALID_PARAMETER && same_root);
    }

    [[nodiscard]] inline bool DirectoryCopyNeedsRefresh(const FileOperationSnapshot& value) noexcept
    { return value.kind == FileOperationKind::DirectoryCopy && value.directory_copy_result &&
        (value.directory_copy_result->copied || value.directory_copy_result->partial_tree_left); }

    // Bounded presentation facts for the current explicit copy/retry lineage.
    class DirectoryCopyPartialHistory
    {
    public:
        void Observe(const FileOperationSnapshot& value)
        {
            if (value.kind != FileOperationKind::DirectoryCopy || !value.directory_copy_result ||
                value.operation_id <= observed_operation_) { return; }
            observed_operation_ = value.operation_id;
            const auto& result = *value.directory_copy_result;
            if (!result.partial_tree_left) { return; }
            ++count_;
            if (!result.path.empty() && paths_.size() < 32 && result.path.size() <= 65536 - retained_characters_)
            { paths_.push_back(result.path); retained_characters_ += result.path.size(); }
        }
        void Reset() noexcept { paths_.clear(); count_ = 0; observed_operation_ = 0; retained_characters_ = 0; }
        [[nodiscard]] std::uint64_t Count() const noexcept { return count_; }
        [[nodiscard]] const std::vector<std::wstring>& Paths() const noexcept { return paths_; }
    private:
        std::vector<std::wstring> paths_;
        std::uint64_t count_{};
        std::uint64_t observed_operation_{};
        std::size_t retained_characters_{};
    };
}
