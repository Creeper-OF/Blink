#include "FileCopyOperationTests.h"
#include "Blink/App/FileCopyOperation.h"
#include "Blink/App/CopyConflictModel.h"
#include <winioctl.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

bool CreateOperationTestJunction(const std::filesystem::path&, const std::filesystem::path&);

namespace
{
    bool Check(bool condition, const char* name)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        return condition;
    }

    void CopyDiagnostic(const blink::app::FileCopyResult& result, const char* name)
    {
        // ASCII only: preserve the actual native result in UTF-16 test logs
        // without mixing wide console output or locale-dependent path text.
        std::cout << "INFO file_copy_native_result case=" << name << " error=" << result.error <<
            " copied=" << result.copied << " skipped=" << result.skipped <<
            " partial=" << result.partial_file_left << " path_chars=" << result.path.size() << '\n';
    }

    blink::core::FileEntry Entry(const std::filesystem::path& path)
    {
        blink::core::FileEntry entry;
        (void)blink::core::ReadEntryMetadata(path.wstring(), entry);
        entry.name = path.filename().wstring();
        entry.full_path = path.wstring();
        return entry;
    }

    std::wstring Extended(const std::filesystem::path& path)
    {
        const auto value = path.wstring();
        return value.starts_with(L"\\\\?\\") ? value : L"\\\\?\\" + value;
    }

    void Write(const std::filesystem::path& path, const std::string& contents)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!stream) { throw std::runtime_error("cannot write owned file-copy fixture"); }
    }

    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }

    bool Exists(const std::filesystem::path& path)
    {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool Preserved(const std::filesystem::path& path, const blink::core::FileIdentity& identity,
        const std::string& contents)
    {
        return Exists(path) && Entry(path).identity == identity && Read(path) == contents;
    }

    blink::app::FileCopyRequest Request(const std::filesystem::path& source_parent,
        const std::wstring& source_name, const std::filesystem::path& destination_parent,
        const std::wstring& destination_name)
    {
        return { source_parent.wstring(), Entry(source_parent).identity, Entry(source_parent / source_name),
            destination_parent.wstring(), Entry(destination_parent).identity, destination_name };
    }

    blink::core::UniqueHandle Open(const std::filesystem::path& path, DWORD access,
        DWORD sharing, DWORD disposition = OPEN_EXISTING, bool directory = false)
    {
        CREATEFILE3_EXTENDED_PARAMETERS parameters{};
        parameters.dwSize = sizeof(parameters);
        parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT |
            (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0);
        return blink::core::UniqueHandle(CreateFile3(path.c_str(), access, sharing, disposition, &parameters));
    }

    bool SharingError(DWORD error)
    {
        return error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED;
    }

    bool RenameBlocked(const std::filesystem::path& source, const std::filesystem::path& destination)
    {
        const bool moved = MoveFileExW(source.c_str(), destination.c_str(), 0) != FALSE;
        const auto error = GetLastError();
        if (moved && !MoveFileExW(destination.c_str(), source.c_str(), 0))
        { throw std::runtime_error("cannot restore unexpectedly movable owned fixture"); }
        return !moved && SharingError(error);
    }

    bool RenameRoundTrip(const std::filesystem::path& source, const std::filesystem::path& destination)
    {
        if (!MoveFileExW(source.c_str(), destination.c_str(), 0)) { return false; }
        if (!MoveFileExW(destination.c_str(), source.c_str(), 0))
        { throw std::runtime_error("cannot restore released owned fixture"); }
        return true;
    }

    struct Cleanup
    {
        std::filesystem::path root;
        std::vector<std::filesystem::path> directory_links;
        std::vector<std::filesystem::path> file_links;
        std::vector<std::filesystem::path> reset_attributes;
        ~Cleanup()
        {
            for (const auto& path : reset_attributes) { (void)SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); }
            for (const auto& link : directory_links) { (void)RemoveDirectoryW(link.c_str()); }
            for (const auto& link : file_links) { (void)DeleteFileW(link.c_str()); }
            std::error_code error;
            std::filesystem::remove_all(Extended(root), error);
        }
    };
}

bool RunFileCopyOperationTests()
{
    using namespace blink::app;
    bool passed = true;
    const auto root = std::filesystem::absolute(std::filesystem::path(L".build") / L"file-copy-operation-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    Cleanup cleanup{ root, {}, {}, {} };
    try
    {
        const auto source_parent = root / L"源目录";
        const auto destination_parent = root / L"目标目录";
        std::filesystem::create_directories(source_parent);
        std::filesystem::create_directories(destination_parent);
        constexpr char binary_contents[] = "owned source\r\n\0binary\xff";
        const std::string small_contents(binary_contents, sizeof(binary_contents) - 1);
        const auto source_path = source_parent / L"源文件 {0}.bin";
        Write(source_path, small_contents);
        auto request = Request(source_parent, source_path.filename().wstring(), destination_parent, L"复制 {1}.bin");
        const auto source_id = request.source.identity;
        bool progress_valid = true;
        std::uint64_t previous{};
        std::uint64_t last_total{}, last_transferred{};
        auto result = CopyFileSafely(request, {}, [&](const FileCopyProgress& progress) {
            progress_valid &= progress.total_bytes == small_contents.size() &&
                progress.transferred_bytes >= previous && progress.transferred_bytes <= progress.total_bytes;
            previous = progress.transferred_bytes;
            last_total = progress.total_bytes;
            last_transferred = progress.transferred_bytes;
        });
        passed &= Check(result.copied && result.error == ERROR_SUCCESS && !result.partial_file_left &&
            Read(destination_parent / request.destination_name) == small_contents &&
            Entry(destination_parent / request.destination_name).identity.valid &&
            Entry(result.path).identity == Entry(destination_parent / request.destination_name).identity &&
            Entry(destination_parent / request.destination_name).identity != source_id &&
            Preserved(source_path, source_id, small_contents),
            "file_copy_unicode_binary_contents_match_and_source_identity_preserved");
        passed &= Check(progress_valid && last_total == small_contents.size() && last_transferred == small_contents.size(),
            "file_copy_progress_is_monotonic_bounded_and_finishes_at_actual_file_size");

        const auto metadata_source = source_parent / L"metadata-source.bin";
        const auto metadata_destination = destination_parent / L"metadata-copy.bin";
        Write(metadata_source, small_contents);
        cleanup.reset_attributes.push_back(metadata_source);
        cleanup.reset_attributes.push_back(metadata_destination);
        constexpr ULONGLONG CreationTicks = 132224078450000000ULL;
        constexpr ULONGLONG WriteTicks = CreationTicks + 1234567890ULL;
        const FILETIME desired_creation{ static_cast<DWORD>(CreationTicks), static_cast<DWORD>(CreationTicks >> 32) };
        const FILETIME desired_write{ static_cast<DWORD>(WriteTicks), static_cast<DWORD>(WriteTicks >> 32) };
        bool metadata_fixture{};
        {
            auto metadata_handle = Open(metadata_source, FILE_WRITE_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
            metadata_fixture = metadata_handle && SetFileTime(metadata_handle.get(), &desired_creation, nullptr, &desired_write);
        }
        constexpr DWORD DesiredAttributes = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE;
        metadata_fixture = metadata_fixture && SetFileAttributesW(metadata_source.c_str(), DesiredAttributes);
        request = Request(source_parent, metadata_source.filename().wstring(), destination_parent, metadata_destination.filename().wstring());
        result = CopyFileSafely(request);
        const auto copied_metadata = Entry(metadata_destination);
        const auto original_metadata = Entry(metadata_source);
        passed &= Check(metadata_fixture && result.copied && result.error == ERROR_SUCCESS && !result.partial_file_left &&
            (copied_metadata.attributes & DesiredAttributes) == DesiredAttributes &&
            CompareFileTime(&copied_metadata.creation_time, &desired_creation) == 0 &&
            CompareFileTime(&copied_metadata.last_write_time, &desired_write) == 0 &&
            (original_metadata.attributes & DesiredAttributes) == DesiredAttributes &&
            CompareFileTime(&original_metadata.creation_time, &desired_creation) == 0 &&
            CompareFileTime(&original_metadata.last_write_time, &desired_write) == 0 &&
            Read(metadata_destination) == small_contents && Preserved(metadata_source, request.source.identity, small_contents),
            "file_copy_preserves_supported_readonly_hidden_archive_attributes_and_creation_write_times");
        (void)SetFileAttributesW(metadata_source.c_str(), FILE_ATTRIBUTE_NORMAL);
        (void)SetFileAttributesW(metadata_destination.c_str(), FILE_ATTRIBUTE_NORMAL);

        Write(source_parent / L"zero.dat", {});
        request = Request(source_parent, L"zero.dat", destination_parent, L"zero-copy.dat");
        result = CopyFileSafely(request);
        passed &= Check(result.copied && result.error == ERROR_SUCCESS && !result.partial_file_left &&
            Exists(destination_parent / request.destination_name) && Read(destination_parent / request.destination_name).empty() &&
            Entry(destination_parent / request.destination_name).size == 0 &&
            Entry(destination_parent / request.destination_name).identity != request.source.identity &&
            Preserved(source_parent / L"zero.dat", request.source.identity, {}),
            "file_copy_zero_byte_file_creates_distinct_empty_destination");

        request = Request(source_parent, source_path.filename().wstring(), destination_parent, L"pre-cancelled.bin");
        result = CopyFileSafely(request, std::make_shared<std::atomic_bool>(true));
        passed &= Check(!result.copied && result.error == ERROR_CANCELLED && !result.partial_file_left &&
            !Exists(destination_parent / request.destination_name) && Preserved(source_path, source_id, small_contents),
            "file_copy_precancel_creates_no_target_and_preserves_source");

        const auto invalid_copy = [&](FileCopyRequest invalid, DWORD expected_error, const char* name) {
            const auto attempted_path = std::filesystem::path(invalid.destination_parent) / invalid.destination_name;
            const auto failed = CopyFileSafely(invalid);
            passed &= Check(!failed.copied && failed.error == expected_error && !failed.partial_file_left &&
                !Exists(attempted_path) && Preserved(source_path, source_id, small_contents), name);
        };
        request.destination_name = L"wrong-source-id.bin";
        auto invalid = request;
        invalid.source.identity.file_id[0] ^= 1;
        invalid_copy(invalid, ERROR_FILE_INVALID, "file_copy_wrong_source_identity_creates_no_target");
        invalid = request; invalid.destination_name = L"wrong-source-parent.bin";
        invalid.source_parent_identity.file_id[0] ^= 1;
        invalid_copy(invalid, ERROR_FILE_INVALID, "file_copy_wrong_source_parent_identity_creates_no_target");
        invalid = request; invalid.destination_name = L"wrong-destination-parent.bin";
        invalid.destination_parent_identity.file_id[0] ^= 1;
        invalid_copy(invalid, ERROR_FILE_INVALID, "file_copy_wrong_destination_parent_identity_creates_no_target");
        invalid = request; invalid.destination_name = L"source-type-changed.bin"; invalid.source.is_directory = true;
        invalid_copy(invalid, ERROR_NOT_SUPPORTED, "file_copy_directory_source_metadata_is_explicitly_unsupported");
        invalid = request; invalid.destination_name = L"..\\escape.bin";
        invalid_copy(invalid, ERROR_INVALID_NAME, "file_copy_invalid_destination_leaf_cannot_escape_parent");
        invalid = request; invalid.destination_name = L"alternate:stream";
        invalid_copy(invalid, ERROR_INVALID_NAME, "file_copy_destination_cannot_create_alternate_stream");
        invalid = request; invalid.source.name = L"..\\escape.bin"; invalid.destination_name = L"invalid-source-name.bin";
        invalid_copy(invalid, ERROR_INVALID_NAME, "file_copy_source_leaf_cannot_escape_committed_parent");
        invalid = request; invalid.source_parent = L"relative"; invalid.destination_name = L"relative-source-parent.bin";
        invalid_copy(invalid, ERROR_NOT_SUPPORTED, "file_copy_relative_source_parent_is_explicitly_unsupported");
        invalid = request; invalid.destination_parent = L"\\\\server\\share";
        result = CopyFileSafely(invalid);
        passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_file_left &&
            Preserved(source_path, source_id, small_contents), "file_copy_unc_destination_parent_is_explicitly_unsupported");

        const auto existing_path = destination_parent / L"existing.bin";
        const std::string existing_contents = "different destination content";
        Write(existing_path, existing_contents);
        const auto existing_id = Entry(existing_path).identity;
        request = Request(source_parent, source_path.filename().wstring(), destination_parent, existing_path.filename().wstring());
        result = CopyFileSafely(request);
        passed &= Check(!result.copied && result.error != ERROR_SUCCESS && !result.partial_file_left &&
            Preserved(existing_path, existing_id, existing_contents) && Preserved(source_path, source_id, small_contents),
            "file_copy_existing_regular_target_is_never_overwritten_or_deleted");
        request.conflict_policy = FileCopyConflictPolicy::Skip;
        result = CopyFileSafely(request);
        passed &= Check(result.error == ERROR_SUCCESS && !result.copied && result.skipped && !result.partial_file_left &&
            Preserved(existing_path, existing_id, existing_contents) && Preserved(source_path, source_id, small_contents),
            "file_copy_explicit_skip_preserves_existing_target_and_source_without_false_copy_success");
        request.destination_name = L"skip-without-conflict.bin";
        bool skip_observer{};
        result = CopyFileSafely(request, {}, [&](const FileCopyProgress&) { skip_observer = true; });
        passed &= Check(result.error == ERROR_SUCCESS && !result.copied && result.skipped && !result.partial_file_left &&
            !skip_observer && !Exists(destination_parent / request.destination_name) &&
            Preserved(source_path, source_id, small_contents),
            "file_copy_explicit_skip_never_copies_even_when_no_target_exists");
        request.destination_name = L"skip-vanished-target.bin";
        Write(destination_parent / request.destination_name, existing_contents);
        std::filesystem::remove(destination_parent / request.destination_name);
        auto skipped_invalid = request;
        skipped_invalid.source_parent = L"invalid-source-parent";
        skipped_invalid.source.identity = {};
        skipped_invalid.destination_parent_identity = {};
        result = CopyFileSafely(skipped_invalid, {}, [&](const FileCopyProgress&) { skip_observer = true; });
        passed &= Check(result.error == ERROR_SUCCESS && result.skipped && !result.copied && !result.partial_file_left &&
            !skip_observer && result.path == (destination_parent / request.destination_name).wstring() &&
            !Exists(destination_parent / request.destination_name),
            "file_copy_skip_after_dialog_target_vanishes_ignores_invalid_source_and_creates_nothing");
        CopyConflictModel fake_ui(false);
        const bool chose_skip = fake_ui.Choose(FileCopyConflictPolicy::Skip);
        const auto ui_decision = fake_ui.Complete();
        skipped_invalid.destination_name = L"fake-ui-skip.bin";
        skipped_invalid.conflict_policy = ui_decision.value_or(FileCopyConflictPolicy::Fail);
        result = CopyFileSafely(skipped_invalid, {}, [&](const FileCopyProgress&) { skip_observer = true; });
        passed &= Check(chose_skip && ui_decision == FileCopyConflictPolicy::Skip && result.error == ERROR_SUCCESS &&
            result.skipped && !result.copied && !result.partial_file_left && !skip_observer &&
            !Exists(destination_parent / skipped_invalid.destination_name),
            "file_copy_fake_ui_skip_choice_maps_to_native_no_io_even_without_a_remaining_conflict");
        request.destination_name = existing_path.filename().wstring();
        request.conflict_policy = FileCopyConflictPolicy::Cancel;
        bool cancel_observer{};
        result = CopyFileSafely(request, {}, [&](const FileCopyProgress&) { cancel_observer = true; });
        passed &= Check(result.error == ERROR_CANCELLED && !result.copied && !result.skipped && !result.partial_file_left &&
            !cancel_observer && Preserved(existing_path, existing_id, existing_contents) && Preserved(source_path, source_id, small_contents),
            "file_copy_explicit_cancel_performs_no_copy_or_observer_work");
        request.conflict_policy = FileCopyConflictPolicy::Replace;
        auto directory_replace = request; directory_replace.source.is_directory = true;
        result = CopyFileSafely(directory_replace);
        passed &= Check(result.error == ERROR_NOT_SUPPORTED && !result.copied && !result.skipped && !result.partial_file_left &&
            Preserved(existing_path, existing_id, existing_contents) && Preserved(source_path, source_id, small_contents),
            "file_copy_directory_replace_is_unsupported_and_preserves_existing_target");
        request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
        Write(destination_parent / L"existing (2).bin", existing_contents);
        const auto second_id = Entry(destination_parent / L"existing (2).bin").identity;
        result = CopyFileSafely(request);
        passed &= Check(result.error == ERROR_SUCCESS && result.copied && !result.skipped && !result.partial_file_left &&
            result.path == (destination_parent / L"existing (3).bin").wstring() &&
            Read(result.path) == small_contents && Preserved(existing_path, existing_id, existing_contents) &&
            Preserved(destination_parent / L"existing (2).bin", second_id, existing_contents) &&
            Preserved(source_path, source_id, small_contents),
            "file_copy_keep_both_uses_first_free_deterministic_suffix_and_preserves_all_conflicts");
        for (const auto& name : { std::wstring{ L"README" }, std::wstring{ L".gitignore" }, std::wstring{ L"压缩包.tar.gz" } })
        {
            Write(destination_parent / name, existing_contents);
            request.destination_name = name;
            result = CopyFileSafely(request);
            const auto expected = name == L"README" ? L"README (2)" :
                name == L".gitignore" ? L".gitignore (2)" : L"压缩包.tar (2).gz";
            passed &= Check(result.error == ERROR_SUCCESS && result.copied && !result.skipped &&
                result.path == (destination_parent / expected).wstring() && Read(result.path) == small_contents &&
                Read(destination_parent / name) == existing_contents,
                "file_copy_keep_both_preserves_last_extension_dotfile_or_extensionless_name");
        }
        const auto long_leaf = std::wstring(246, L'u') + L"\U0001f680abc.bin";
        Write(Extended(destination_parent / long_leaf), existing_contents);
        request.destination_name = long_leaf;
        result = CopyFileSafely(request);
        const auto shortened_leaf = std::wstring(246, L'u') + L" (2).bin";
        passed &= Check(long_leaf.size() == 255 && result.error == ERROR_SUCCESS && result.copied && !result.skipped &&
            result.path == (destination_parent / shortened_leaf).wstring() && Read(Extended(result.path)) == small_contents &&
            Read(Extended(destination_parent / long_leaf)) == existing_contents,
            "file_copy_keep_both_long_leaf_preserves_extension_and_never_splits_utf16_surrogate_pair");
        request = Request(source_parent, source_path.filename().wstring(), destination_parent, L"racing-keep-both.bin");
        request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
        bool keep_both_rival{};
        result = CopyFileSafely(request, {}, [&](const FileCopyProgress& progress) {
            if (progress.transferred_bytes != 0 || keep_both_rival) { return; }
            keep_both_rival = true;
            Write(destination_parent / request.destination_name, existing_contents);
            Write(destination_parent / L"racing-keep-both (2).bin", existing_contents);
        });
        passed &= Check(keep_both_rival && result.error == ERROR_SUCCESS && result.copied && !result.skipped &&
            result.path == (destination_parent / L"racing-keep-both (3).bin").wstring() && Read(result.path) == small_contents &&
            Read(destination_parent / request.destination_name) == existing_contents &&
            Read(destination_parent / L"racing-keep-both (2).bin") == existing_contents,
            "file_copy_keep_both_retries_atomic_creation_after_concurrent_target_appearance");
        request = Request(source_parent, source_path.filename().wstring(), destination_parent, L"racing-target.bin");
        bool rival_created{};
        bool rival_written{};
        blink::core::FileIdentity rival_identity;
        result = CopyFileSafely(request, {}, [&](const FileCopyProgress& progress) {
            if (progress.transferred_bytes != 0 || rival_created) { return; }
            auto rival = Open(destination_parent / request.destination_name, GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, CREATE_NEW);
            rival_created = static_cast<bool>(rival);
            DWORD written{};
            rival_written = rival && WriteFile(rival.get(), existing_contents.data(),
                static_cast<DWORD>(existing_contents.size()), &written, nullptr) && written == existing_contents.size() &&
                blink::core::ReadFileIdentity(rival.get(), rival_identity);
        });
        passed &= Check(rival_created && rival_written && !result.copied && result.error != ERROR_SUCCESS &&
            !result.partial_file_left && Preserved(destination_parent / request.destination_name, rival_identity, existing_contents) &&
            Preserved(source_path, source_id, small_contents),
            "file_copy_target_created_after_source_validation_wins_without_overwrite_or_cleanup");
        request = Request(source_parent, source_path.filename().wstring(), source_parent, source_path.filename().wstring());
        result = CopyFileSafely(request);
        passed &= Check(!result.copied && result.error != ERROR_SUCCESS && !result.partial_file_left &&
            Preserved(source_path, source_id, small_contents), "file_copy_same_source_path_is_not_overwritten");
        const auto hardlink_path = destination_parent / L"hardlink.bin";
        const bool hardlinked = CreateHardLinkW(hardlink_path.c_str(), source_path.c_str(), nullptr) != FALSE;
        request = Request(source_parent, source_path.filename().wstring(), destination_parent, hardlink_path.filename().wstring());
        result = CopyFileSafely(request);
        passed &= Check(hardlinked && !result.copied && result.error != ERROR_SUCCESS && !result.partial_file_left &&
            Preserved(hardlink_path, source_id, small_contents) && Preserved(source_path, source_id, small_contents),
            "file_copy_existing_hardlink_target_cannot_truncate_the_source_object");
        const auto occupied_directory = destination_parent / L"existing-directory";
        std::filesystem::create_directory(occupied_directory);
        Write(occupied_directory / L"child.bin", existing_contents);
        const auto occupied_directory_id = Entry(occupied_directory).identity;
        request.destination_name = occupied_directory.filename().wstring();
        result = CopyFileSafely(request);
        CopyDiagnostic(result, "existing_directory_conflict");
        passed &= Check(!result.copied && result.error == ERROR_FILE_EXISTS && !result.partial_file_left &&
            Entry(occupied_directory).identity == occupied_directory_id &&
            Read(occupied_directory / L"child.bin") == existing_contents && Preserved(source_path, source_id, small_contents),
            "file_copy_existing_directory_conflict_is_normalized_for_dialog_and_children_are_preserved");
        request.conflict_policy = FileCopyConflictPolicy::Skip;
        result = CopyFileSafely(request);
        passed &= Check(result.error == ERROR_SUCCESS && result.skipped && !result.copied && !result.partial_file_left &&
            Entry(occupied_directory).identity == occupied_directory_id && Read(occupied_directory / L"child.bin") == existing_contents,
            "file_copy_skip_policy_treats_existing_directory_as_an_untouched_conflict");
        request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
        result = CopyFileSafely(request);
        CopyDiagnostic(result, "keep_both_directory_conflict");
        passed &= Check(result.error == ERROR_SUCCESS && result.copied && !result.skipped && !result.partial_file_left &&
            result.path == (destination_parent / L"existing-directory (2)").wstring() && Read(result.path) == small_contents &&
            Entry(occupied_directory).identity == occupied_directory_id && Read(occupied_directory / L"child.bin") == existing_contents,
            "file_copy_keep_both_handles_directory_collision_without_treating_it_as_permission_failure");

        std::filesystem::create_directory(source_parent / L"directory-source");
        Write(source_parent / L"directory-source" / L"child.bin", small_contents);
        request = Request(source_parent, L"directory-source", destination_parent, L"must-not-copy-directory");
        result = CopyFileSafely(request);
        passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_file_left &&
            !Exists(destination_parent / request.destination_name) &&
            Read(source_parent / L"directory-source" / L"child.bin") == small_contents,
            "file_copy_directory_source_is_explicitly_unsupported_without_traversal");

        const auto stream_source = source_parent / L"named-stream-source.bin";
        Write(stream_source, small_contents);
        Write(stream_source.wstring() + L":owned-stream", existing_contents);
        request = Request(source_parent, stream_source.filename().wstring(), destination_parent, L"preserved-stream.bin");
        result = CopyFileSafely(request);
        CopyDiagnostic(result, "named_data_stream");
        passed &= Check(result.copied && result.error == ERROR_SUCCESS && !result.partial_file_left &&
            Read(destination_parent / request.destination_name) == small_contents &&
            Read((destination_parent / request.destination_name).wstring() + L":owned-stream") == existing_contents &&
            Preserved(stream_source, request.source.identity, small_contents) &&
            Read(stream_source.wstring() + L":owned-stream") == existing_contents,
            "file_copy_named_data_stream_is_preserved_with_default_contents_and_source_identity");

        const auto zone_source = source_parent / L"downloaded-file.bin";
        const auto zone_destination = destination_parent / L"downloaded-copy.bin";
        const std::string zone_contents = "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=https://example.test/owned\r\n";
        Write(zone_source, small_contents);
        Write(zone_source.wstring() + L":Zone.Identifier", zone_contents);
        Write(zone_source.wstring() + L":中文流\U0001f680", small_contents);
        Write(zone_source.wstring() + L":empty-stream", {});
        Write(zone_source.wstring() + L":$DATA", existing_contents);
        cleanup.reset_attributes.push_back(zone_source);
        cleanup.reset_attributes.push_back(zone_destination);
        bool zone_metadata_fixture{};
        {
            auto metadata_handle = Open(zone_source, FILE_WRITE_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
            zone_metadata_fixture = metadata_handle && SetFileTime(metadata_handle.get(), &desired_creation, nullptr, &desired_write);
        }
        zone_metadata_fixture = zone_metadata_fixture && SetFileAttributesW(zone_source.c_str(), DesiredAttributes);
        request = Request(source_parent, zone_source.filename().wstring(), destination_parent, zone_destination.filename().wstring());
        bool stream_progress_valid = true;
        std::uint64_t stream_progress_previous{}, stream_progress_final{};
        const auto expected_stream_bytes = small_contents.size() * 2 + zone_contents.size() + existing_contents.size();
        result = CopyFileSafely(request, {}, [&](const FileCopyProgress& value) {
            stream_progress_valid &= value.total_bytes == expected_stream_bytes &&
                value.transferred_bytes >= stream_progress_previous && value.transferred_bytes <= value.total_bytes;
            stream_progress_previous = value.transferred_bytes;
            stream_progress_final = value.transferred_bytes;
        });
        CopyDiagnostic(result, "zone_unicode_empty_streams_metadata_progress");
        auto empty_stream = Open(zone_destination.wstring() + L":empty-stream", GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
        FILE_STANDARD_INFO empty_standard{};
        const bool empty_stream_preserved = empty_stream &&
            GetFileInformationByHandleEx(empty_stream.get(), FileStandardInfo, &empty_standard, sizeof(empty_standard)) &&
            empty_standard.EndOfFile.QuadPart == 0;
        empty_stream.reset();
        const auto copied_zone_metadata = Entry(zone_destination);
        passed &= Check(zone_metadata_fixture && result.copied && result.error == ERROR_SUCCESS && !result.partial_file_left &&
            Read(zone_destination) == small_contents && Read(zone_destination.wstring() + L":Zone.Identifier") == zone_contents &&
            Read(zone_destination.wstring() + L":中文流\U0001f680") == small_contents && empty_stream_preserved &&
            Read(zone_destination.wstring() + L":$DATA") == existing_contents &&
            Preserved(zone_source, request.source.identity, small_contents) &&
            Read(zone_source.wstring() + L":Zone.Identifier") == zone_contents &&
            Read(zone_source.wstring() + L":中文流\U0001f680") == small_contents &&
            (copied_zone_metadata.attributes & DesiredAttributes) == DesiredAttributes &&
            CompareFileTime(&copied_zone_metadata.creation_time, &desired_creation) == 0 &&
            CompareFileTime(&copied_zone_metadata.last_write_time, &desired_write) == 0,
            "file_copy_preserves_zone_identifier_unicode_empty_named_streams_and_basic_metadata");
        passed &= Check(stream_progress_valid && stream_progress_final == expected_stream_bytes,
            "file_copy_progress_total_includes_default_and_all_named_stream_data");
        (void)SetFileAttributesW(zone_source.c_str(), FILE_ATTRIBUTE_NORMAL);
        (void)SetFileAttributesW(zone_destination.c_str(), FILE_ATTRIBUTE_NORMAL);
        request.destination_name = L"stream-writer-must-not-copy.bin";
        {
            auto stream_writer = Open(zone_source.wstring() + L":Zone.Identifier", GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
            result = CopyFileSafely(request);
            passed &= Check(stream_writer && !result.copied && SharingError(result.error) && !result.partial_file_left &&
                !Exists(destination_parent / request.destination_name) &&
                Preserved(zone_source, request.source.identity, small_contents) &&
                Read(zone_source.wstring() + L":Zone.Identifier") == zone_contents,
                "file_copy_existing_named_stream_writer_prevents_unstable_stream_copy");
        }

        Write(source_parent / L"zero-with-stream.bin", {});
        Write((source_parent / L"zero-with-stream.bin").wstring() + L":Zone.Identifier", zone_contents);
        request = Request(source_parent, L"zero-with-stream.bin", destination_parent, L"zero-with-stream-copy.bin");
        result = CopyFileSafely(request);
        CopyDiagnostic(result, "zero_default_zone_identifier");
        passed &= Check(result.copied && result.error == ERROR_SUCCESS && !result.partial_file_left &&
            Read(destination_parent / request.destination_name).empty() &&
            Read((destination_parent / request.destination_name).wstring() + L":Zone.Identifier") == zone_contents &&
            Read((source_parent / request.source.name).wstring() + L":Zone.Identifier") == zone_contents,
            "file_copy_empty_default_stream_still_preserves_nonempty_zone_identifier");

        const auto sparse_stream_source = source_parent / L"special-stream-source.bin";
        Write(sparse_stream_source, small_contents);
        Write(sparse_stream_source.wstring() + L":sparse-stream", existing_contents);
        bool sparse_fixture{};
        {
            auto sparse = Open(sparse_stream_source.wstring() + L":sparse-stream", GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
            DWORD returned{};
            sparse_fixture = sparse && DeviceIoControl(sparse.get(), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
        }
        request = Request(source_parent, sparse_stream_source.filename().wstring(), destination_parent, L"must-not-flatten-sparse-stream.bin");
        result = CopyFileSafely(request);
        passed &= Check(sparse_fixture && result.error == ERROR_NOT_SUPPORTED && !result.copied && !result.skipped &&
            !result.partial_file_left && !Exists(destination_parent / request.destination_name) &&
            Preserved(sparse_stream_source, request.source.identity, small_contents) &&
            Read(sparse_stream_source.wstring() + L":sparse-stream") == existing_contents,
            "file_copy_special_sparse_named_stream_is_explicitly_unsupported_without_flattening");

        Write(source_parent / L"replaced.bin", small_contents);
        request = Request(source_parent, L"replaced.bin", destination_parent, L"stale-copy.bin");
        std::filesystem::rename(source_parent / L"replaced.bin", source_parent / L"original-replaced.bin");
        Write(source_parent / L"replaced.bin", existing_contents);
        const auto replacement_id = Entry(source_parent / L"replaced.bin").identity;
        result = CopyFileSafely(request);
        passed &= Check(!result.copied && result.error == ERROR_FILE_INVALID && !result.partial_file_left &&
            !Exists(destination_parent / request.destination_name) &&
            Preserved(source_parent / L"replaced.bin", replacement_id, existing_contents) &&
            Preserved(source_parent / L"original-replaced.bin", request.source.identity, small_contents),
            "file_copy_replaced_source_is_not_copied_and_both_objects_are_preserved");
        std::filesystem::remove(source_parent / L"replaced.bin");
        result = CopyFileSafely(request);
        passed &= Check(!result.copied && result.error != ERROR_SUCCESS && !result.partial_file_left &&
            !Exists(destination_parent / request.destination_name), "file_copy_deleted_source_creates_no_destination");

        const auto stale_parent = root / L"stale-destination-parent";
        std::filesystem::create_directory(stale_parent);
        request = Request(source_parent, source_path.filename().wstring(), stale_parent, L"must-not-write.bin");
        std::filesystem::rename(stale_parent, root / L"original-destination-parent");
        std::filesystem::create_directory(stale_parent);
        result = CopyFileSafely(request);
        passed &= Check(!result.copied && result.error == ERROR_FILE_INVALID && !result.partial_file_left &&
            !Exists(stale_parent / request.destination_name) && !Exists(root / L"original-destination-parent" / request.destination_name) &&
            Preserved(source_path, source_id, small_contents),
            "file_copy_replaced_destination_parent_is_not_written");
        const auto stale_source_parent = root / L"stale-source-parent";
        std::filesystem::create_directory(stale_source_parent);
        Write(stale_source_parent / L"source.bin", small_contents);
        request = Request(stale_source_parent, L"source.bin", destination_parent, L"stale-parent-copy.bin");
        std::filesystem::rename(stale_source_parent, root / L"original-source-parent");
        std::filesystem::create_directory(stale_source_parent);
        Write(stale_source_parent / L"source.bin", existing_contents);
        result = CopyFileSafely(request);
        passed &= Check(!result.copied && result.error == ERROR_FILE_INVALID && !result.partial_file_left &&
            !Exists(destination_parent / request.destination_name) &&
            Read(stale_source_parent / L"source.bin") == existing_contents &&
            Read(root / L"original-source-parent" / L"source.bin") == small_contents,
            "file_copy_replaced_source_parent_does_not_copy_the_replacement");

        request = Request(source_parent, source_path.filename().wstring(), destination_parent, L"busy-source-copy.bin");
        {
            auto writer = Open(source_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
            result = CopyFileSafely(request);
            passed &= Check(writer && !result.copied && SharingError(result.error) && !result.partial_file_left &&
                !Exists(destination_parent / request.destination_name) && Preserved(source_path, source_id, small_contents),
                "file_copy_existing_source_writer_prevents_unstable_copy");
        }
        for (const auto& parent : { source_parent, destination_parent })
        {
            request.destination_name = parent == source_parent ? L"source-parent-locked.bin" : L"destination-parent-locked.bin";
            auto deleting = Open(parent, DELETE | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, true);
            result = CopyFileSafely(request);
            passed &= Check(deleting && !result.copied && SharingError(result.error) && !result.partial_file_left &&
                !Exists(destination_parent / request.destination_name) && Preserved(source_path, source_id, small_contents),
                parent == source_parent ? "file_copy_source_parent_delete_handle_prevents_copy" :
                "file_copy_destination_parent_delete_handle_prevents_copy");
        }

        std::string large_contents(3 * 1024 * 1024 + 17, '\0');
        for (std::size_t index = 0; index < large_contents.size(); ++index)
        { large_contents[index] = static_cast<char>((index * 31 + index / 251) & 0xff); }
        const auto large_source = source_parent / L"large.bin";
        Write(large_source, large_contents);
        const auto large_stream_source = source_parent / L"large-stream-source.bin";
        Write(large_stream_source, small_contents);
        Write(large_stream_source.wstring() + L":Zone.Identifier", zone_contents);
        Write(large_stream_source.wstring() + L":payload", large_contents);
        request = Request(source_parent, large_stream_source.filename().wstring(), destination_parent, L"cancelled-stream-copy.bin");
        auto stream_cancel = std::make_shared<std::atomic_bool>(false);
        bool cancelled_named_data{};
        const auto all_stream_bytes = small_contents.size() + zone_contents.size() + large_contents.size();
        result = CopyFileSafely(request, stream_cancel, [&](const FileCopyProgress& value) {
            if (value.transferred_bytes > small_contents.size() + zone_contents.size() && value.transferred_bytes < value.total_bytes)
            { cancelled_named_data = true; stream_cancel->store(true); }
        });
        CopyDiagnostic(result, "cancel_named_stream");
        passed &= Check(cancelled_named_data && !result.copied && result.error == ERROR_CANCELLED && !result.partial_file_left &&
            !Exists(destination_parent / request.destination_name) &&
            Preserved(large_stream_source, request.source.identity, small_contents) &&
            Read(large_stream_source.wstring() + L":Zone.Identifier") == zone_contents &&
            Read(large_stream_source.wstring() + L":payload") == large_contents,
            "file_copy_cancel_during_named_stream_removes_all_owned_target_streams_and_preserves_source");
        request.destination_name = L"guarded-stream-copy.bin";
        stream_cancel = std::make_shared<std::atomic_bool>(false);
        bool named_guards_observed{}, named_write_blocked{}, named_source_delete_blocked{}, named_target_write_blocked{}, stream_late_cancel{};
        DWORD named_writer_error{}, named_source_delete_error{}, named_target_writer_error{};
        result = CopyFileSafely(request, stream_cancel, [&](const FileCopyProgress& value) {
            if (value.total_bytes == all_stream_bytes && value.transferred_bytes == value.total_bytes)
            { stream_late_cancel = true; stream_cancel->store(true); return; }
            if (named_guards_observed || value.transferred_bytes <= small_contents.size() + zone_contents.size()) { return; }
            named_guards_observed = true;
            auto writer = Open(large_stream_source.wstring() + L":payload", GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
            named_writer_error = writer ? ERROR_SUCCESS : GetLastError();
            named_write_blocked = !writer && SharingError(named_writer_error);
            const bool source_deleted = DeleteFileW((large_stream_source.wstring() + L":payload").c_str()) != FALSE;
            named_source_delete_error = source_deleted ? ERROR_SUCCESS : GetLastError();
            named_source_delete_blocked = !source_deleted && SharingError(named_source_delete_error);
            auto target_writer = Open((destination_parent / request.destination_name).wstring() + L":payload", GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
            named_target_writer_error = target_writer ? ERROR_SUCCESS : GetLastError();
            named_target_write_blocked = !target_writer && SharingError(named_target_writer_error);
        });
        CopyDiagnostic(result, "named_stream_guards_late_cancel");
        std::cout << "INFO file_copy_named_guards observed=" << named_guards_observed <<
            " writer_error=" << named_writer_error << " source_delete_error=" << named_source_delete_error <<
            " target_writer_error=" << named_target_writer_error << " late_cancel=" << stream_late_cancel <<
            " target_payload_bytes=" << Read((destination_parent / request.destination_name).wstring() + L":payload").size() << '\n';
        passed &= Check(named_guards_observed && named_write_blocked && named_source_delete_blocked && named_target_write_blocked &&
            stream_late_cancel && stream_cancel->load() && result.copied && result.error == ERROR_SUCCESS && !result.partial_file_left &&
            Read(destination_parent / request.destination_name) == small_contents &&
            Read((destination_parent / request.destination_name).wstring() + L":payload") == large_contents &&
            Read((destination_parent / request.destination_name).wstring() + L":Zone.Identifier") == zone_contents &&
            Preserved(large_stream_source, request.source.identity, small_contents),
            "file_copy_named_stream_source_and_write_guards_survive_io_and_late_cancel_keeps_complete_copy");

        request.destination_name = L"delete-pending-stream-copy.bin";
        bool stream_delete_attempted{}, target_stream_deleted{}, deletion_reported_complete{};
        DWORD target_stream_delete_error{};
        result = CopyFileSafely(request, {}, [&](const FileCopyProgress& value) {
            if (value.transferred_bytes == value.total_bytes) { deletion_reported_complete = true; return; }
            if (stream_delete_attempted || value.transferred_bytes <= small_contents.size() + zone_contents.size()) { return; }
            stream_delete_attempted = true;
            target_stream_deleted = DeleteFileW(((destination_parent / request.destination_name).wstring() + L":payload").c_str()) != FALSE;
            target_stream_delete_error = target_stream_deleted ? ERROR_SUCCESS : GetLastError();
        });
        CopyDiagnostic(result, "target_named_stream_delete_pending");
        std::cout << "INFO file_copy_target_stream_delete attempted=" << stream_delete_attempted <<
            " deleted=" << target_stream_deleted << " delete_error=" << target_stream_delete_error <<
            " complete_reported=" << deletion_reported_complete << '\n';
        const bool deleted_safely = target_stream_deleted ?
            (!result.copied && result.error == ERROR_FILE_INVALID && !result.partial_file_left &&
                !deletion_reported_complete && !Exists(destination_parent / request.destination_name)) :
            (SharingError(target_stream_delete_error) && result.copied && result.error == ERROR_SUCCESS &&
                !result.partial_file_left && Read((destination_parent / request.destination_name).wstring() + L":payload") == large_contents);
        passed &= Check(stream_delete_attempted && deleted_safely &&
            Preserved(large_stream_source, request.source.identity, small_contents) &&
            Read(large_stream_source.wstring() + L":Zone.Identifier") == zone_contents &&
            Read(large_stream_source.wstring() + L":payload") == large_contents,
            "file_copy_target_named_stream_deletion_is_blocked_or_detected_before_success_with_owned_cleanup");
        request = Request(source_parent, L"large.bin", destination_parent, L"cancelled-partial.bin");
        const auto large_id = request.source.identity;
        auto cancelled = std::make_shared<std::atomic_bool>(false);
        bool cancelled_during_progress{};
        result = CopyFileSafely(request, cancelled, [&](const FileCopyProgress& progress) {
            if (progress.transferred_bytes > 0 && progress.transferred_bytes < progress.total_bytes)
            { cancelled_during_progress = true; cancelled->store(true); }
        });
        passed &= Check(cancelled_during_progress && !result.copied && result.error == ERROR_CANCELLED &&
            !result.partial_file_left && !Exists(destination_parent / request.destination_name) &&
            Preserved(large_source, large_id, large_contents),
            "file_copy_cancel_after_real_progress_removes_owned_partial_and_preserves_source");
        request.destination_name = L"cancelled-before-create.bin";
        cancelled = std::make_shared<std::atomic_bool>(false);
        bool initial_cancel{};
        result = CopyFileSafely(request, cancelled, [&](const FileCopyProgress& progress) {
            if (progress.transferred_bytes == 0)
            { initial_cancel = true; cancelled->store(true); }
        });
        passed &= Check(initial_cancel && !result.copied && result.error == ERROR_CANCELLED &&
            !result.partial_file_left && !Exists(destination_parent / request.destination_name) &&
            Preserved(large_source, large_id, large_contents),
            "file_copy_cancel_from_initial_progress_creates_no_target");
        request.destination_name = L"exception-partial.bin";
        bool callback_threw{};
        result = CopyFileSafely(request, {}, [&](const FileCopyProgress& progress) {
            if (progress.transferred_bytes > 0 && progress.transferred_bytes < progress.total_bytes)
            { callback_threw = true; throw std::runtime_error("owned progress callback failure"); }
        });
        passed &= Check(callback_threw && !result.copied && result.error == ERROR_UNHANDLED_EXCEPTION &&
            !result.partial_file_left && !Exists(destination_parent / request.destination_name) &&
            Preserved(large_source, large_id, large_contents),
            "file_copy_progress_callback_exception_is_contained_and_owned_partial_is_cleaned");
        request.destination_name = L"late-cancel-success.bin";
        cancelled = std::make_shared<std::atomic_bool>(false);
        bool final_progress{};
        result = CopyFileSafely(request, cancelled, [&](const FileCopyProgress& progress) {
            if (progress.total_bytes > 0 && progress.transferred_bytes == progress.total_bytes)
            { final_progress = true; cancelled->store(true); }
        });
        passed &= Check(final_progress && cancelled->load() && result.copied && result.error == ERROR_SUCCESS &&
            !result.partial_file_left && Read(destination_parent / request.destination_name) == large_contents &&
            Preserved(large_source, large_id, large_contents),
            "file_copy_cancel_at_postcommit_progress_keeps_actual_success_and_complete_file");
        request.destination_name = L"late-exception-success.bin";
        bool final_threw{};
        result = CopyFileSafely(request, {}, [&](const FileCopyProgress& progress) {
            if (progress.total_bytes > 0 && progress.transferred_bytes == progress.total_bytes)
            { final_threw = true; throw std::runtime_error("postcommit callback failure"); }
        });
        passed &= Check(final_threw && result.copied && result.error == ERROR_SUCCESS && !result.partial_file_left &&
            Read(destination_parent / request.destination_name) == large_contents && Preserved(large_source, large_id, large_contents),
            "file_copy_postcommit_callback_exception_does_not_reverse_completed_copy");

        request.destination_name = L"guarded-copy.bin";
        bool guards_observed{}, source_rename_blocked{}, source_write_blocked{}, source_parent_blocked{},
            destination_parent_blocked{}, ancestor_blocked{}, destination_delete_blocked{};
        result = CopyFileSafely(request, {}, [&](const FileCopyProgress& progress) {
            if (guards_observed || progress.transferred_bytes == 0 || progress.transferred_bytes >= progress.total_bytes) { return; }
            guards_observed = true;
            source_rename_blocked = RenameBlocked(large_source, source_parent / L"unexpected-source.bin");
            auto writer = Open(large_source, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
            const auto writer_error = GetLastError();
            source_write_blocked = !writer && SharingError(writer_error);
            source_parent_blocked = RenameBlocked(source_parent, root / L"unexpected-source-parent");
            destination_parent_blocked = RenameBlocked(destination_parent, root / L"unexpected-destination-parent");
            ancestor_blocked = RenameBlocked(root, root.wstring() + L"-unexpected");
            const bool deleted = DeleteFileW((destination_parent / request.destination_name).c_str()) != FALSE;
            destination_delete_blocked = !deleted && SharingError(GetLastError());
        });
        passed &= Check(guards_observed && source_rename_blocked && source_write_blocked && source_parent_blocked &&
            destination_parent_blocked && ancestor_blocked && destination_delete_blocked && result.copied &&
            result.error == ERROR_SUCCESS && Read(destination_parent / request.destination_name) == large_contents &&
            Preserved(large_source, large_id, large_contents),
            "file_copy_source_destination_and_entire_parent_chains_remain_guarded_through_io");
        const bool source_parent_released = RenameRoundTrip(source_parent, root / L"released-source-parent");
        const bool destination_parent_released = RenameRoundTrip(destination_parent, root / L"released-destination-parent");
        passed &= Check(source_parent_released && destination_parent_released,
            "file_copy_parent_guards_are_released_after_return");

        const auto mutable_stream_source = source_parent / L"stream-added-during-copy.bin";
        Write(mutable_stream_source, large_contents);
        request = Request(source_parent, mutable_stream_source.filename().wstring(), destination_parent, L"must-not-omit-new-stream.bin");
        bool stream_attempted{}, stream_added{}, stream_written{};
        DWORD stream_error{};
        result = CopyFileSafely(request, {}, [&](const FileCopyProgress& progress) {
            if (stream_attempted || progress.transferred_bytes == 0 || progress.transferred_bytes >= progress.total_bytes) { return; }
            stream_attempted = true;
            auto stream = Open(mutable_stream_source.wstring() + L":concurrent-stream", GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, CREATE_NEW);
            stream_error = GetLastError();
            stream_added = static_cast<bool>(stream);
            DWORD written{};
            stream_written = stream && WriteFile(stream.get(), existing_contents.data(),
                static_cast<DWORD>(existing_contents.size()), &written, nullptr) && written == existing_contents.size();
        });
        if (stream_added)
        {
            passed &= Check(stream_attempted && stream_written && !result.copied && result.error == ERROR_NOT_SUPPORTED &&
                !result.partial_file_left && !Exists(destination_parent / request.destination_name) &&
                Preserved(mutable_stream_source, request.source.identity, large_contents) &&
                Read(mutable_stream_source.wstring() + L":concurrent-stream") == existing_contents,
                "file_copy_named_stream_added_during_io_is_detected_before_commit_and_source_is_preserved");
        }
        else if (stream_attempted && SharingError(stream_error))
        {
            std::cout << "SKIP file_copy_concurrent_named_stream_mutation_blocked_by_filesystem error=" << stream_error << '\n';
            passed &= Check(result.copied && result.error == ERROR_SUCCESS && !result.partial_file_left &&
                Read(destination_parent / request.destination_name) == large_contents &&
                Preserved(mutable_stream_source, request.source.identity, large_contents),
                "file_copy_blocked_named_stream_mutation_leaves_exact_default_stream_copy");
        }
        else
        {
            std::cout << "INFO file_copy_concurrent_named_stream_fixture_error=" << stream_error << '\n';
            passed &= Check(false, "file_copy_concurrent_named_stream_fixture_exercised_or_explicitly_blocked");
        }

        const auto junction_target = root / L"junction-target";
        std::filesystem::create_directory(junction_target);
        std::filesystem::create_directory(junction_target / L"nested");
        Write(junction_target / L"owned.bin", small_contents);
        Write(junction_target / L"nested" / L"owned.bin", small_contents);
        const auto junction = root / L"junction";
        const bool linked = CreateOperationTestJunction(junction, junction_target);
        if (linked) { cleanup.directory_links.push_back(junction); }
        passed &= Check(linked && Exists(junction) && (GetFileAttributesW(junction.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0,
            "file_copy_real_owned_junction_fixture_created_without_elevation");
        if (linked)
        {
            request = Request(root, L"junction", destination_parent, L"must-not-copy-junction");
            result = CopyFileSafely(request);
            passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_file_left &&
                !Exists(destination_parent / request.destination_name) && Read(junction_target / L"owned.bin") == small_contents,
                "file_copy_source_reparse_object_is_not_followed");
            request = Request(junction, L"owned.bin", destination_parent, L"must-not-follow-source-parent.bin");
            result = CopyFileSafely(request);
            passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_file_left &&
                !Exists(destination_parent / request.destination_name), "file_copy_source_reparse_parent_is_not_followed");
            request = Request(junction / L"nested", L"owned.bin", destination_parent, L"must-not-follow-source-ancestor.bin");
            result = CopyFileSafely(request);
            passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_file_left &&
                !Exists(destination_parent / request.destination_name), "file_copy_source_reparse_ancestor_is_not_followed");
            request = Request(source_parent, source_path.filename().wstring(), junction, L"must-not-follow-destination.bin");
            result = CopyFileSafely(request);
            passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_file_left &&
                !Exists(junction_target / request.destination_name), "file_copy_destination_reparse_parent_is_not_followed");
            request = Request(source_parent, source_path.filename().wstring(), junction / L"nested", L"must-not-follow-ancestor.bin");
            result = CopyFileSafely(request);
            passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_file_left &&
                !Exists(junction_target / L"nested" / request.destination_name),
                "file_copy_destination_reparse_ancestor_is_not_followed");
            request = Request(source_parent, source_path.filename().wstring(), root, L"junction");
            result = CopyFileSafely(request);
            passed &= Check(!result.copied && result.error != ERROR_SUCCESS && !result.partial_file_left &&
                (GetFileAttributesW(junction.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                Read(junction_target / L"owned.bin") == small_contents && Preserved(source_path, source_id, small_contents),
                "file_copy_existing_destination_junction_is_neither_followed_nor_deleted");
        }

        for (const bool dangling : { false, true })
        {
            const auto link = destination_parent / (dangling ? L"dangling-symlink.bin" : L"existing-symlink.bin");
            const auto target = destination_parent / (dangling ? L"missing-symlink-target.bin" : L"owned-symlink-target.bin");
            if (!dangling) { Write(target, existing_contents); }
            const bool created = CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
            const auto link_error = GetLastError();
            if (!created && (link_error == ERROR_PRIVILEGE_NOT_HELD || link_error == ERROR_ACCESS_DENIED || link_error == ERROR_INVALID_PARAMETER))
            {
                std::cout << "SKIP file_copy_destination_" << (dangling ? "dangling" : "existing") <<
                    "_symlink_fixture_unavailable error=" << link_error << '\n';
                continue;
            }
            passed &= Check(created && Exists(link) && (GetFileAttributesW(link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0,
                dangling ? "file_copy_real_dangling_symlink_fixture_created" : "file_copy_real_existing_symlink_fixture_created");
            if (!created) { continue; }
            cleanup.file_links.push_back(link);
            request = Request(source_parent, source_path.filename().wstring(), destination_parent, link.filename().wstring());
            result = CopyFileSafely(request);
            passed &= Check(!result.copied && result.error != ERROR_SUCCESS && !result.partial_file_left &&
                Exists(link) && (GetFileAttributesW(link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                (dangling ? !Exists(target) : Read(target) == existing_contents) && Preserved(source_path, source_id, small_contents),
                dangling ? "file_copy_dangling_destination_symlink_target_is_not_created" :
                "file_copy_existing_destination_symlink_target_is_not_changed");
            request = Request(destination_parent, link.filename().wstring(), source_parent,
                dangling ? L"must-not-follow-dangling-source.bin" : L"must-not-follow-source-symlink.bin");
            result = CopyFileSafely(request);
            passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_file_left &&
                !Exists(source_parent / request.destination_name) && Exists(link) &&
                (dangling ? !Exists(target) : Read(target) == existing_contents),
                dangling ? "file_copy_dangling_source_symlink_is_not_followed" : "file_copy_source_symlink_is_not_followed");
        }

        auto long_source = source_parent;
        auto long_destination = destination_parent;
        for (int index = 0; index < 6; ++index)
        {
            long_source /= std::wstring(45, L's') + std::to_wstring(index);
            long_destination /= std::wstring(45, L'd') + std::to_wstring(index);
        }
        long_source = Extended(long_source);
        long_destination = Extended(long_destination);
        std::filesystem::create_directories(long_source);
        std::filesystem::create_directories(long_destination);
        Write(long_source / L"长路径源文件.bin", small_contents);
        request = Request(long_source, L"长路径源文件.bin", long_destination, L"长路径复制 {0}.bin");
        result = CopyFileSafely(request);
        passed &= Check(result.copied && result.error == ERROR_SUCCESS && !result.partial_file_left &&
            Read(long_destination / request.destination_name) == small_contents &&
            Preserved(long_source / request.source.name, request.source.identity, small_contents),
            "file_copy_extended_long_source_and_destination_paths_preserve_exact_contents");
        Write((long_source / request.source.name).wstring() + L":Zone.Identifier", zone_contents);
        request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
        result = CopyFileSafely(request);
        CopyDiagnostic(result, "longpath_keep_both_ads");
        const auto long_both_path = long_destination / L"长路径复制 {0} (2).bin";
        // Operation results use a display DOS path, as rename/new-folder do.
        // This unmanifested test process must use extended paths for native
        // metadata calls beyond MAX_PATH; both actual and expected IDs must exist.
        const auto long_actual_entry = Entry(Extended(result.path));
        const auto long_expected_entry = Entry(long_both_path);
        passed &= Check(result.copied && result.error == ERROR_SUCCESS && !result.partial_file_left && !result.skipped &&
            long_actual_entry.identity.valid && long_expected_entry.identity.valid &&
            long_actual_entry.identity == long_expected_entry.identity &&
            Read(long_both_path) == small_contents && Read(long_both_path.wstring() + L":Zone.Identifier") == zone_contents &&
            Read(long_destination / request.destination_name) == small_contents,
            "file_copy_keep_both_preserves_unicode_extension_and_zone_identifier_on_extended_long_paths");
    }
    catch (const std::exception& exception)
    {
        std::cout << "INFO file_copy_fixture_exception " << exception.what() << '\n';
        passed &= Check(false, "file_copy_owned_native_fixture_completed_without_exception");
    }
    return passed;
}
