#include "ClipboardCopySourceTests.h"
#include "Blink/App/ClipboardFileTransfer.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
#include <winioctl.h>

bool CreateOperationTestJunction(const std::filesystem::path&, const std::filesystem::path&);

namespace
{
    using namespace blink::app;
    bool Check(bool condition, const char* name)
    { std::cout << (condition ? "PASS " : "FAIL ") << name << '\n'; return condition; }
    std::filesystem::path Extended(const std::filesystem::path& path)
    { const auto value = path.wstring(); return value.starts_with(L"\\\\?\\") ? value : L"\\\\?\\" + value; }
    blink::core::FileEntry Entry(const std::filesystem::path& path)
    {
        blink::core::FileEntry entry;
        if (!blink::core::ReadEntryMetadata(Extended(path).wstring(), entry))
        { throw std::runtime_error("cannot read owned typed capture fixture metadata"); }
        entry.name = path.filename().wstring(); entry.full_path = path.wstring(); return entry;
    }
    void Write(const std::filesystem::path& path, const std::string& value)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(value.data(), static_cast<std::streamsize>(value.size()));
        if (!file) { throw std::runtime_error("cannot write owned typed capture fixture"); }
    }
    std::string Read(const std::filesystem::path& path)
    { std::ifstream file(path, std::ios::binary); return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() }; }
    bool Exists(const std::filesystem::path& path)
    { return GetFileAttributesW(Extended(path).c_str()) != INVALID_FILE_ATTRIBUTES; }
    bool Empty(const ClipboardCopySources& output) { return std::holds_alternative<std::monostate>(output); }
    bool ReparseRejected(DWORD error) { return error == ERROR_NOT_SUPPORTED || error == ERROR_REPARSE_POINT_ENCOUNTERED; }

    struct FileOracle
    {
        std::filesystem::path path; blink::core::FileIdentity identity; std::string data;
        bool Matches() const { return Exists(path) && Entry(path).identity == identity && Read(path) == data; }
    };
    FileOracle Oracle(const std::filesystem::path& path) { return { path, Entry(path).identity, Read(path) }; }
    bool FileRequestMatches(const FileCopyRequest& request, const std::filesystem::path& path)
    {
        const auto source = Entry(path), parent = Entry(path.parent_path());
        return request.source.identity == source.identity && request.source.identity.valid &&
            request.source_parent_identity == parent.identity && Entry(request.source_parent).identity == parent.identity &&
            request.source.name == path.filename().wstring() && Entry(request.source.full_path).identity == source.identity &&
            !request.source.is_directory && !(request.source.attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
            request.destination_parent.empty() && !request.destination_parent_identity.valid && request.destination_name.empty() &&
            request.conflict_policy == FileCopyConflictPolicy::Fail;
    }
    bool DirectoryRequestMatches(const DirectoryCopyRequest& request, const std::filesystem::path& path)
    {
        const auto source = Entry(path), parent = Entry(path.parent_path());
        return request.source.identity == source.identity && request.source.identity.valid &&
            request.source_parent_identity == parent.identity && Entry(request.source_parent).identity == parent.identity &&
            request.source.name == path.filename().wstring() && Entry(request.source.full_path).identity == source.identity &&
            request.source.is_directory && (request.source.attributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(request.source.attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
            request.destination_parent.empty() && !request.destination_parent_identity.valid && request.destination_name.empty() &&
            request.limits.max_entries == MaximumDirectoryCopyEntries && request.limits.max_directories == MaximumDirectoryCopyDirectories &&
            request.limits.max_depth == MaximumDirectoryCopyDepth && request.limits.max_path_bytes == MaximumDirectoryCopyPathBytes;
    }
    bool RenameRoundTrip(const std::filesystem::path& path, const std::filesystem::path& alternate)
    {
        if (!MoveFileExW(Extended(path).c_str(), Extended(alternate).c_str(), 0)) { return false; }
        if (!MoveFileExW(Extended(alternate).c_str(), Extended(path).c_str(), 0))
        { throw std::runtime_error("cannot restore owned typed capture fixture after rename"); }
        return true;
    }
    void CleanupOwnedTree(const std::filesystem::path& root) noexcept
    {
        WIN32_FIND_DATAW entry{}; const auto found = FindFirstFileW((root / L"*").c_str(), &entry);
        if (found == INVALID_HANDLE_VALUE) { return; }
        do
        {
            if (!std::wcscmp(entry.cFileName, L".") || !std::wcscmp(entry.cFileName, L"..")) { continue; }
            const auto path = root / entry.cFileName;
            const bool reparse = (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!reparse) { CleanupOwnedTree(path); (void)SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); }
                (void)RemoveDirectoryW(path.c_str());
            }
            else
            {
                if (!reparse) { (void)SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); }
                (void)DeleteFileW(path.c_str());
            }
        } while (FindNextFileW(found, &entry));
        FindClose(found);
    }
}

bool RunClipboardCopySourceTests()
{
    using namespace blink::app;
    bool passed = true;
    const auto root = std::filesystem::absolute(std::filesystem::path(L".build") / L"typed-copy-source-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    std::filesystem::create_directories(root);
    struct Cleanup
    { std::filesystem::path root; ~Cleanup() { const auto path = Extended(root); CleanupOwnedTree(path); (void)RemoveDirectoryW(path.c_str()); } } cleanup{ root };
    try
    {
        const auto parent_a = root / L"甲父目录", parent_b = root / L"乙父目录";
        std::filesystem::create_directory(parent_a); std::filesystem::create_directory(parent_b);
        const auto file_a = parent_a / L"文件 {0}.bin", file_b = parent_b / L"日本語\U0001f680.txt", hardlink = parent_b / L"同对象不同名称.bin";
        constexpr char Data[] = "owned-typed-source\0binary";
        Write(file_a, std::string(Data, sizeof(Data) - 1)); Write(file_b, "owned-other-parent");
        Write(file_a.wstring() + L":Zone.Identifier:$DATA", "[ZoneTransfer]\r\nZoneId=3\r\n");
        const auto oracle_a = Oracle(file_a), oracle_b = Oracle(file_b);
        const bool hardlinked = CreateHardLinkW(hardlink.c_str(), file_a.c_str(), nullptr) != FALSE;
        passed &= Check(hardlinked, "typed_capture_owned_hardlink_fixture_is_really_created");
        const auto directory = parent_a / L"目录 {1}", other_directory = parent_b / L"另一个目录";
        std::filesystem::create_directories(directory / L"后代"); std::filesystem::create_directory(other_directory);
        Write(directory / L"后代" / L"保留.txt", "owned-descendant"); const auto descendant = Oracle(directory / L"后代" / L"保留.txt");
        const auto directory_identity = Entry(directory).identity;
        const auto preserved = [&] {
            return oracle_a.Matches() && oracle_b.Matches() && descendant.Matches() &&
                Entry(directory).identity == directory_identity &&
                Read(file_a.wstring() + L":Zone.Identifier:$DATA") == "[ZoneTransfer]\r\nZoneId=3\r\n";
        };
        ClipboardCopySources output;
        if (hardlinked)
        {
            const std::vector<std::wstring> files{ file_b.wstring(), file_a.wstring(), hardlink.wstring(), file_a.wstring() };
            const auto error = CaptureClipboardCopySources(files, output); const auto* captured = std::get_if<std::vector<FileCopyRequest>>(&output);
            passed &= Check(error == ERROR_SUCCESS && captured && captured->size() == files.size() &&
                FileRequestMatches((*captured)[0], file_b) && FileRequestMatches((*captured)[1], file_a) &&
                FileRequestMatches((*captured)[2], hardlink) && FileRequestMatches((*captured)[3], file_a) &&
                (*captured)[1].source.identity == (*captured)[2].source.identity &&
                (*captured)[2].source.name == hardlink.filename().wstring() && preserved(),
                "typed_capture_files_keep_order_duplicates_hardlink_names_actual_parent_ids_and_default_destination_fields");
        }
        {
            const bool attrs = SetFileAttributesW(directory.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE) != FALSE;
            const std::vector<std::wstring> directories{ directory.wstring() };
            auto error = CaptureClipboardCopySources(directories, output); const auto* captured = std::get_if<DirectoryCopyRequest>(&output);
            passed &= Check(attrs && error == ERROR_SUCCESS && captured && DirectoryRequestMatches(*captured, directory) &&
                (captured->source.attributes & FILE_ATTRIBUTE_HIDDEN) && preserved(),
                "typed_capture_single_directory_has_real_root_parent_identity_type_attributes_and_no_paste_destination");
            error = CaptureClipboardCopySources(std::vector<std::wstring>{ file_a.wstring() }, output);
            const auto* files = std::get_if<std::vector<FileCopyRequest>>(&output);
            passed &= Check(error == ERROR_SUCCESS && files && files->size() == 1 && FileRequestMatches(files->front(), file_a),
                "typed_capture_reusing_directory_output_for_file_replaces_the_variant_instead_of_leaking_old_mode");
            error = CaptureClipboardCopySources(directories, output);
            captured = std::get_if<DirectoryCopyRequest>(&output);
            passed &= Check(error == ERROR_SUCCESS && captured && DirectoryRequestMatches(*captured, directory),
                "typed_capture_reusing_file_output_for_directory_replaces_the_variant_instead_of_appending_modes");
        }
        for (const auto& selection : std::vector<std::vector<std::wstring>>{
            { file_a.wstring(), directory.wstring() }, { directory.wstring(), file_a.wstring() },
            { directory.wstring(), other_directory.wstring() }, { directory.wstring(), directory.wstring() } })
        {
            output = DirectoryCopyRequest{}; const auto error = CaptureClipboardCopySources(selection, output);
            passed &= Check(error == ERROR_SUCCESS && output.index() == 3 && preserved(),
                "typed_capture_mixed_multiple_or_duplicate_directories_preserve_typed_selection_without_deduplication");
        }
        {
            const std::vector<std::wstring> missing{ file_a.wstring(), (parent_b / L"missing.txt").wstring() };
            output = DirectoryCopyRequest{}; auto error = CaptureClipboardCopySources(missing, output);
            passed &= Check(error == ERROR_FILE_NOT_FOUND && Empty(output) && preserved(),
                "typed_capture_missing_later_file_keeps_real_error_and_clears_prior_directory_or_partial_files");
            output = std::vector<FileCopyRequest>{ FileCopyRequest{} };
            error = CaptureClipboardCopySources(std::vector<std::wstring>{ directory.wstring(), L"relative.txt" }, output);
            passed &= Check(error == ERROR_NOT_SUPPORTED && Empty(output) && preserved(),
                "typed_capture_unsupported_later_input_never_exposes_a_directory_prefix");
        }
        {
            const auto cancelled = std::make_shared<std::atomic_bool>(true);
            const std::vector<std::wstring> too_many(MaxClipboardFileCount + 1, (parent_a / L"missing.txt").wstring());
            std::vector<std::wstring> too_large(MaxClipboardFileCount, std::wstring(4095, L'x'));
            too_large.front() = (parent_a / L"missing.txt").wstring();
            const std::vector<std::vector<std::wstring>> invalid{
                {}, too_many, too_large, { file_a.wstring(), std::wstring(L"D:\\a\0b", 7) } };
            for (const auto& paths : invalid)
            {
                output = DirectoryCopyRequest{}; const auto error = CaptureClipboardCopySources(paths, output, cancelled);
                passed &= Check(error == ERROR_INVALID_PARAMETER && Empty(output),
                    "typed_capture_validates_complete_count_payload_and_string_bounds_before_cancel_or_missing_source_io");
            }
            output = std::vector<FileCopyRequest>{ FileCopyRequest{} };
            const auto error = CaptureClipboardCopySources(std::vector<std::wstring>{ (parent_a / L"missing.txt").wstring() }, output, cancelled);
            passed &= Check(error == ERROR_CANCELLED && Empty(output) && preserved(),
                "typed_capture_valid_precancelled_selection_clears_file_mode_without_opening_missing_source");
        }
        {
            const auto target = root / L"自有联接目标";
            std::filesystem::create_directory(target); Write(target / L"保留.txt", "owned-reparse-target"); const auto sentinel = Oracle(target / L"保留.txt");
            const auto descendant_link = directory / L"后代联接";
            const bool descendant_created = CreateOperationTestJunction(descendant_link, target);
            passed &= Check(descendant_created && Exists(descendant_link) &&
                (GetFileAttributesW(descendant_link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT), "typed_capture_owned_descendant_junction_fixture_created");
            if (descendant_created)
            {
                auto error = CaptureClipboardCopySources(std::vector<std::wstring>{ directory.wstring() }, output);
                const auto* captured = std::get_if<DirectoryCopyRequest>(&output);
                passed &= Check(error == ERROR_SUCCESS && captured && DirectoryRequestMatches(*captured, directory) && sentinel.Matches() && preserved(),
                    "typed_capture_classifies_only_normal_root_without_scanning_or_following_unsupported_descendant_tree");
                error = CaptureClipboardCopySources(std::vector<std::wstring>{ descendant_link.wstring() }, output);
                passed &= Check(ReparseRejected(error) && Empty(output) && sentinel.Matches(),
                    "typed_capture_leaf_directory_junction_is_rejected_with_native_reparse_error_and_empty_output");
                error = CaptureClipboardCopySources(std::vector<std::wstring>{ (descendant_link / L"保留.txt").wstring() }, output);
                passed &= Check(ReparseRejected(error) && Empty(output) && sentinel.Matches(),
                    "typed_capture_parent_chain_junction_is_rejected_without_returning_its_target_file");
            }
            const auto stream_path = directory.wstring() + L":owned-capture-data:$DATA";
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS;
            parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            const bool attributes_reset = SetFileAttributesW(directory.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE;
            bool stream_created{};
            {
                blink::core::UniqueHandle stream(CreateFile3(stream_path.c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, CREATE_NEW, &parameters));
                constexpr char DataStream[] = "owned-directory-capture-stream"; DWORD written{};
                stream_created = attributes_reset && stream && WriteFile(stream.get(), DataStream, sizeof(DataStream) - 1, &written, nullptr) &&
                    written == sizeof(DataStream) - 1 && FlushFileBuffers(stream.get());
            }
            passed &= Check(stream_created, "typed_capture_owned_directory_ads_fixture_really_created");
            if (stream_created)
            {
                const auto error = CaptureClipboardCopySources(std::vector<std::wstring>{ directory.wstring() }, output);
                const auto* captured = std::get_if<DirectoryCopyRequest>(&output);
                passed &= Check(error == ERROR_SUCCESS && captured && DirectoryRequestMatches(*captured, directory) &&
                    Read(stream_path) == "owned-directory-capture-stream" && sentinel.Matches() && preserved(),
                    "typed_capture_directory_ads_does_not_turn_root_metadata_capture_into_tree_copy_or_stream_preflight");
            }
            const auto link = parent_a / L"源文件符号链接.txt";
            const bool linked = CreateSymbolicLinkW(link.c_str(), file_a.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
            const auto error = linked ? ERROR_SUCCESS : GetLastError();
            if (!linked && (error == ERROR_PRIVILEGE_NOT_HELD || error == ERROR_ACCESS_DENIED || error == ERROR_INVALID_PARAMETER))
            { std::cout << "SKIP typed_capture_source_symlink_fixture_unavailable error=" << error << '\n'; }
            else
            {
                passed &= Check(linked && Exists(link) && (GetFileAttributesW(link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT),
                    "typed_capture_real_file_symlink_fixture_created");
                if (linked)
                {
                    const auto captured_error = CaptureClipboardCopySources(std::vector<std::wstring>{ link.wstring() }, output);
                    passed &= Check(ReparseRejected(captured_error) && Empty(output) && oracle_a.Matches(),
                        "typed_capture_file_symlink_is_rejected_without_following_or_exposing_target_request");
                }
            }
        }
        {
            auto long_parent = root / L"长路径";
            for (unsigned index = 0; index < 6; ++index) { long_parent /= std::wstring(45, L'p') + std::to_wstring(index); }
            long_parent = Extended(long_parent); std::filesystem::create_directories(long_parent);
            const auto long_file = long_parent / L"长文件.txt", long_directory = long_parent / L"长目录";
            Write(long_file, "owned-long-capture"); std::filesystem::create_directory(long_directory); const auto oracle = Oracle(long_file);
            auto error = CaptureClipboardCopySources(std::vector<std::wstring>{ long_file.wstring() }, output);
            const auto* files = std::get_if<std::vector<FileCopyRequest>>(&output);
            passed &= Check(error == ERROR_SUCCESS && files && files->size() == 1 && FileRequestMatches(files->front(), long_file) && oracle.Matches(),
                "typed_capture_actual_extended_long_file_keeps_source_and_parent_ids_without_truncated_metadata_oracle");
            error = CaptureClipboardCopySources(std::vector<std::wstring>{ long_directory.wstring() }, output);
            const auto* captured = std::get_if<DirectoryCopyRequest>(&output);
            passed &= Check(error == ERROR_SUCCESS && captured && DirectoryRequestMatches(*captured, long_directory) && oracle.Matches(),
                "typed_capture_actual_extended_long_directory_keeps_type_and_both_ids_without_scanning_children");
            passed &= Check(RenameRoundTrip(long_file, long_parent / L"released-long-file.txt") &&
                RenameRoundTrip(long_directory, long_parent / L"released-long-directory"),
                "typed_capture_long_path_success_releases_source_metadata_handles");
        }
        {
            const auto sparse_path = parent_a / L"自有稀疏文件.bin";
            Write(sparse_path, "owned-sparse-source-data"); const auto before = Oracle(sparse_path);
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT;
            bool marked{}; DWORD sparse_error{};
            {
                blink::core::UniqueHandle file(CreateFile3(sparse_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, &parameters));
                DWORD returned{};
                marked = file && DeviceIoControl(file.get(), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
                sparse_error = marked ? ERROR_SUCCESS : GetLastError();
            }
            const bool sparse_attribute = marked && (Entry(sparse_path).attributes & FILE_ATTRIBUTE_SPARSE_FILE);
            std::cout << "INFO typed_capture_sparse_fixture_error=" << sparse_error << '\n';
            passed &= Check(marked && sparse_attribute && before.Matches(),
                "typed_capture_owned_sparse_fixture_is_really_marked_without_changing_source_identity_or_data");
            if (marked && sparse_attribute)
            {
                const std::vector<std::wstring> paths{ sparse_path.wstring() };
                output = DirectoryCopyRequest{}; const auto typed_error = CaptureClipboardCopySources(paths, output);
                FileCopyRequest legacy; const auto legacy_error = CaptureFileCopySource(sparse_path.wstring(), legacy);
                std::vector<FileCopyRequest> legacy_batch;
                const auto batch_error = CaptureFileCopySources(paths, legacy_batch);
                passed &= Check(typed_error == ERROR_NOT_SUPPORTED && Empty(output) && legacy_error == ERROR_SUCCESS &&
                    FileRequestMatches(legacy, sparse_path) && (legacy.source.attributes & FILE_ATTRIBUTE_SPARSE_FILE) &&
                    batch_error == ERROR_SUCCESS && legacy_batch.size() == 1 && FileRequestMatches(legacy_batch.front(), sparse_path) &&
                    (legacy_batch.front().source.attributes & FILE_ATTRIBUTE_SPARSE_FILE) && before.Matches() && preserved(),
                    "typed_capture_rejects_actual_sparse_storage_while_legacy_single_and_batch_metadata_capture_remain_compatible");
            }
        }
        {
            std::vector<FileCopyRequest> legacy{ FileCopyRequest{} };
            const auto error = CaptureFileCopySources(std::vector<std::wstring>{ directory.wstring() }, legacy);
            passed &= Check(error == ERROR_NOT_SUPPORTED && legacy.empty() && preserved(),
                "typed_capture_new_directory_mode_does_not_relax_legacy_files_only_capture");
            passed &= Check(RenameRoundTrip(file_a, parent_a / L"released-source.bin") &&
                RenameRoundTrip(directory, parent_a / L"released-directory") &&
                RenameRoundTrip(parent_a, root / L"released-parent-a") && RenameRoundTrip(parent_b, root / L"released-parent-b") && preserved(),
                "typed_capture_success_rejection_and_precancel_release_all_source_and_parent_leases");
        }
    }
    catch (const std::exception& error)
    {
        std::cout << "INFO typed_capture_fixture_exception " << error.what() << '\n';
        passed &= Check(false, "typed_capture_owned_fixture_completed_without_exception");
    }
    return passed;
}
