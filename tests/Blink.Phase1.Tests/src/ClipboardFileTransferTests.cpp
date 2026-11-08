#include "ClipboardFileTransferTests.h"
#include "Blink/App/ClipboardFileTransfer.h"
#include <shellapi.h>
#include <shlobj_core.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace
{
    bool Check(bool condition, const char* label)
    { std::cout << (condition ? "PASS " : "FAIL ") << label << '\n'; return condition; }

    std::vector<std::byte> RawDropList(const std::vector<std::wstring>& paths)
    {
        std::wstring names;
        for (const auto& path : paths) { names.append(path); names.push_back(L'\0'); }
        names.push_back(L'\0');
        if (paths.empty()) { names.push_back(L'\0'); }
        const DROPFILES header{ sizeof(DROPFILES), {}, FALSE, TRUE };
        std::vector<std::byte> data(sizeof(header) + names.size() * sizeof(wchar_t));
        std::memcpy(data.data(), &header, sizeof(header));
        std::memcpy(data.data() + sizeof(header), names.data(), names.size() * sizeof(wchar_t));
        return data;
    }

    bool RejectedEntirely(const blink::app::ClipboardFilePaths& result, DWORD expected)
    { return result.error == expected && result.paths.empty(); }

    struct FixtureCleanup
    {
        std::filesystem::path root;
        ~FixtureCleanup()
        {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    };

    std::string ReadOwnedFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    }

    bool RunBatchCodecTests()
    {
        using namespace blink::app;
        bool passed = true;
        const std::vector<std::wstring> paths{ L"D:\\资料\\甲 {0}.txt", L"D:\\日本語\U0001f680\\乙.bin",
            L"D:\\资料\\甲 {0}.txt", L"C:\\third.txt" };
        const auto payload = EncodeClipboardFilePaths(paths);
        auto decoded = DecodeClipboardFilePaths(payload);
        passed &= Check(decoded.error == ERROR_SUCCESS && decoded.paths == paths && decoded.sequence == 0,
            "clipboard_batch_unicode_order_and_duplicate_paths_roundtrip_without_normalization");
        passed &= Check(payload == RawDropList(paths),
            "clipboard_batch_payload_is_exact_unicode_dropfiles_double_null_list");
        passed &= Check(DecodeClipboardFilePath(payload).error == ERROR_NOT_SUPPORTED &&
            DecodeClipboardFilePath(payload).path.empty(), "clipboard_single_wrapper_rejects_valid_complete_multi_file_list");
        const std::wstring one = paths.front();
        passed &= Check(EncodeClipboardFilePaths({ &one, 1 }) == EncodeClipboardFilePath(one) &&
            DecodeClipboardFilePaths(EncodeClipboardFilePath(one)).paths == std::vector<std::wstring>{ one },
            "clipboard_single_wrappers_preserve_original_payload_and_path");

        passed &= Check(EncodeClipboardFilePaths({}).empty() &&
            EncodeClipboardFilePaths(std::vector<std::wstring>{ L"D:\\valid", L"" }).empty() &&
            EncodeClipboardFilePaths(std::vector<std::wstring>{ L"D:\\valid", std::wstring(L"D:\\a\0b", 7) }).empty(),
            "clipboard_batch_encoder_rejects_empty_list_empty_item_or_embedded_null_without_partial_payload");
        const std::wstring maximum_path(32766, L'x');
        decoded = DecodeClipboardFilePaths(EncodeClipboardFilePaths({ &maximum_path, 1 }));
        passed &= Check(decoded.error == ERROR_SUCCESS && decoded.paths == std::vector<std::wstring>{ maximum_path } &&
            EncodeClipboardFilePaths(std::vector<std::wstring>{ one, std::wstring(32767, L'x') }).empty(),
            "clipboard_batch_each_path_obeys_exact_32767_utf16_unit_exclusive_limit");
        passed &= Check(RejectedEntirely(DecodeClipboardFilePaths(RawDropList({ one, std::wstring(32767, L'x') })), ERROR_INVALID_DATA),
            "clipboard_batch_decoder_rejects_oversized_later_path_without_delivering_earlier_path");

        auto truncated = payload;
        truncated.resize(truncated.size() - sizeof(wchar_t));
        passed &= Check(RejectedEntirely(DecodeClipboardFilePaths(truncated), ERROR_INVALID_DATA),
            "clipboard_batch_decoder_requires_complete_final_double_null_after_all_items");
        truncated.resize(truncated.size() - sizeof(wchar_t));
        passed &= Check(RejectedEntirely(DecodeClipboardFilePaths(truncated), ERROR_INVALID_DATA),
            "clipboard_batch_decoder_rejects_unterminated_last_path_without_partial_success");
        passed &= Check(RejectedEntirely(DecodeClipboardFilePaths(RawDropList({})), ERROR_INVALID_DATA) &&
            RejectedEntirely(DecodeClipboardFilePaths(RawDropList({ L"", one })), ERROR_INVALID_DATA) &&
            RejectedEntirely(DecodeClipboardFilePaths(RawDropList({ one, L"", paths.back() })), ERROR_INVALID_DATA),
            "clipboard_batch_decoder_rejects_empty_items_or_hidden_paths_after_early_terminator");
        auto padded = payload;
        padded.resize(padded.size() + 6 * sizeof(wchar_t));
        passed &= Check(DecodeClipboardFilePaths(padded).error == ERROR_SUCCESS && DecodeClipboardFilePaths(padded).paths == paths,
            "clipboard_batch_decoder_accepts_only_zero_allocator_padding_after_list");
        const wchar_t hidden = L'X';
        std::memcpy(padded.data() + padded.size() - sizeof(wchar_t), &hidden, sizeof(hidden));
        passed &= Check(RejectedEntirely(DecodeClipboardFilePaths(padded), ERROR_INVALID_DATA),
            "clipboard_batch_decoder_rejects_nonzero_bytes_after_complete_list");

        auto offset = payload;
        offset.insert(offset.begin() + sizeof(DROPFILES), 4, std::byte{ 0x7f });
        DROPFILES header{}; std::memcpy(&header, offset.data(), sizeof(header));
        header.pFiles += 4; std::memcpy(offset.data(), &header, sizeof(header));
        passed &= Check(DecodeClipboardFilePaths(offset).error == ERROR_SUCCESS && DecodeClipboardFilePaths(offset).paths == paths,
            "clipboard_batch_decoder_uses_bounded_header_offset_instead_of_assuming_fixed_body");
        ++header.pFiles; std::memcpy(offset.data(), &header, sizeof(header));
        passed &= Check(RejectedEntirely(DecodeClipboardFilePaths(offset), ERROR_INVALID_DATA),
            "clipboard_batch_decoder_rejects_unaligned_offset_without_partial_paths");
        header.pFiles = MAXDWORD; std::memcpy(offset.data(), &header, sizeof(header));
        passed &= Check(RejectedEntirely(DecodeClipboardFilePaths(offset), ERROR_INVALID_DATA),
            "clipboard_batch_decoder_rejects_maximum_offset_without_overflow");
        std::vector<std::byte> unaligned(1, std::byte{});
        unaligned.insert(unaligned.end(), payload.begin(), payload.end());
        decoded = DecodeClipboardFilePaths({ unaligned.data() + 1, payload.size() });
        passed &= Check(decoded.error == ERROR_SUCCESS && decoded.paths == paths,
            "clipboard_batch_decoder_handles_unaligned_external_payload_with_safe_reads");

        std::vector<std::wstring> count_limit(MaxClipboardFileCount, one);
        decoded = DecodeClipboardFilePaths(EncodeClipboardFilePaths(count_limit));
        passed &= Check(decoded.error == ERROR_SUCCESS && decoded.paths == count_limit,
            "clipboard_batch_codec_accepts_exact_1024_file_boundary_without_deduplication");
        count_limit.push_back(one);
        passed &= Check(EncodeClipboardFilePaths(count_limit).empty() &&
            RejectedEntirely(DecodeClipboardFilePaths(RawDropList(count_limit)), ERROR_NOT_SUPPORTED),
            "clipboard_batch_codec_rejects_1025_files_without_returning_first_1024");
        auto over_count_truncated = RawDropList(count_limit);
        over_count_truncated.resize(over_count_truncated.size() - sizeof(wchar_t));
        passed &= Check(RejectedEntirely(DecodeClipboardFilePaths(over_count_truncated), ERROR_INVALID_DATA),
            "clipboard_batch_decoder_checks_terminator_even_after_count_limit_was_exceeded");

        const auto list_units = (MaxClipboardFilePayloadBytes - sizeof(DROPFILES)) / sizeof(wchar_t);
        const auto per_item_units = (list_units - 1) / MaxClipboardFileCount;
        std::vector<std::wstring> byte_limit(MaxClipboardFileCount, std::wstring(per_item_units - 1, L'p'));
        byte_limit.back().append(list_units - 1 - per_item_units * MaxClipboardFileCount, L'q');
        const auto exact_payload = EncodeClipboardFilePaths(byte_limit);
        decoded = DecodeClipboardFilePaths(exact_payload);
        passed &= Check(exact_payload.size() == MaxClipboardFilePayloadBytes && decoded.error == ERROR_SUCCESS && decoded.paths == byte_limit,
            "clipboard_batch_codec_accepts_exact_four_mib_payload_boundary");
        byte_limit.back().push_back(L'q');
        auto too_many_bytes = exact_payload;
        too_many_bytes.resize(too_many_bytes.size() + sizeof(wchar_t));
        passed &= Check(EncodeClipboardFilePaths(byte_limit).empty() &&
            RejectedEntirely(DecodeClipboardFilePaths(too_many_bytes), ERROR_NOT_SUPPORTED),
            "clipboard_batch_codec_rejects_payload_over_four_mib_without_partial_paths");
        return passed;
    }
}
bool RunClipboardFileTransferTests()
{
    using namespace blink::app;
    bool passed = true;
    const std::wstring path = L"D:\\资料\\日本語😀\\文档 {1}.txt";
    const auto encoded = EncodeClipboardFilePath(path);
    const auto decoded = DecodeClipboardFilePath(encoded);
    passed &= Check(decoded.error == ERROR_SUCCESS && decoded.path == path, "clipboard_unicode_file_drop_roundtrip");
    passed &= Check(EncodeClipboardFilePath(L"").empty() && EncodeClipboardFilePath(std::wstring(32767, L'x')).empty(), "clipboard_codec_rejects_empty_or_oversized");
    passed &= Check(EncodeClipboardFilePath(std::wstring(L"D:\\a\0b", 7)).empty(), "clipboard_codec_rejects_embedded_null");
    passed &= Check(DecodeClipboardFilePath({}).error == ERROR_INVALID_DATA, "clipboard_codec_rejects_truncated_header");
    auto malformed = encoded;
    DROPFILES header{}; std::memcpy(&header, malformed.data(), sizeof(header));
    header.pFiles = 0; std::memcpy(malformed.data(), &header, sizeof(header));
    passed &= Check(DecodeClipboardFilePath(malformed).error == ERROR_INVALID_DATA, "clipboard_codec_rejects_header_offset");
    header.pFiles = static_cast<DWORD>(malformed.size() + 2); std::memcpy(malformed.data(), &header, sizeof(header));
    passed &= Check(DecodeClipboardFilePath(malformed).error == ERROR_INVALID_DATA, "clipboard_codec_rejects_out_of_bounds");
    header.pFiles = sizeof(DROPFILES) + 1; std::memcpy(malformed.data(), &header, sizeof(header));
    passed &= Check(DecodeClipboardFilePath(malformed).error == ERROR_INVALID_DATA, "clipboard_codec_rejects_unaligned_unicode");
    malformed = encoded; malformed.pop_back();
    passed &= Check(DecodeClipboardFilePath(malformed).error == ERROR_INVALID_DATA, "clipboard_codec_rejects_odd_unicode_bytes");
    malformed = encoded; malformed.resize(malformed.size() - sizeof(wchar_t));
    passed &= Check(DecodeClipboardFilePath(malformed).error == ERROR_INVALID_DATA, "clipboard_codec_requires_double_terminator");
    malformed = RawDropList({ path, L"D:\\next.txt" });
    passed &= Check(DecodeClipboardFilePath(malformed).error == ERROR_NOT_SUPPORTED, "clipboard_codec_rejects_multiple_files");
    malformed = encoded; std::memcpy(&header, malformed.data(), sizeof(header)); header.fWide = FALSE;
    std::memcpy(malformed.data(), &header, sizeof(header));
    passed &= Check(DecodeClipboardFilePath(malformed).error == ERROR_NOT_SUPPORTED, "clipboard_codec_explicitly_rejects_ansi");
    passed &= RunBatchCodecTests();

    const auto fixture = std::filesystem::current_path() / L".build" /
        (L"clipboard-source-fixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::error_code filesystem_error;
    const auto created = std::filesystem::create_directories(fixture, filesystem_error);
    if (!Check(created && !filesystem_error, "clipboard_source_fixture_created")) { return false; }
    const FixtureCleanup cleanup{ fixture };
    const auto file_path = fixture / L"复制源.txt";
    { std::ofstream file(file_path, std::ios::binary); file << "clipboard source identity oracle"; }
    FileCopyRequest captured;
    const auto error = CaptureFileCopySource(file_path.wstring(), captured);
    const auto listing = blink::core::EnumerateDirectory(fixture.wstring());
    passed &= Check(error == ERROR_SUCCESS && listing.error == ERROR_SUCCESS && listing.entries.size() == 1 &&
        captured.source.identity == listing.entries.front().identity && captured.source_parent_identity == listing.directory_identity,
        "clipboard_worker_captures_file_and_parent_identity");
    FileCopyRequest extended;
    passed &= Check(CaptureFileCopySource(L"\\\\?\\" + file_path.wstring(), extended) == ERROR_SUCCESS &&
        extended.source.identity == captured.source.identity, "clipboard_worker_extended_path_identity");
    FileCopyRequest invalid;
    passed &= Check(CaptureFileCopySource(L"relative.txt", invalid) == ERROR_NOT_SUPPORTED &&
        CaptureFileCopySource(L"\\\\server\\share\\file.txt", invalid) == ERROR_NOT_SUPPORTED,
        "clipboard_worker_rejects_relative_and_unc");
    passed &= Check(CaptureFileCopySource(file_path.wstring() + L":stream", invalid) == ERROR_NOT_SUPPORTED,
        "clipboard_worker_rejects_ads_path");
    passed &= Check(CaptureFileCopySource(fixture.wstring(), invalid) == ERROR_NOT_SUPPORTED,
        "clipboard_worker_rejects_directory_source");
    passed &= Check(CaptureFileCopySource((fixture / L"missing.txt").wstring(), invalid) == ERROR_FILE_NOT_FOUND,
        "clipboard_worker_missing_source_is_failure");

    const auto nested = fixture / L"另一个目录";
    std::filesystem::create_directory(nested, filesystem_error);
    const auto other_file = nested / L"日本語\U0001f680 {2}.bin";
    const std::string other_contents = "owned second clipboard source";
    { std::ofstream file(other_file, std::ios::binary); file << other_contents; }
    const auto hardlink = fixture / L"same-object-another-name.txt";
    const bool hardlinked = CreateHardLinkW(hardlink.c_str(), file_path.c_str(), nullptr) != FALSE;
    passed &= Check(!filesystem_error && hardlinked && ReadOwnedFile(other_file) == other_contents,
        "clipboard_batch_owned_unicode_second_parent_and_hardlink_fixtures_created");
    const std::vector<std::wstring> source_paths{ other_file.wstring(), file_path.wstring(), hardlink.wstring(), file_path.wstring() };
    std::vector<FileCopyRequest> sources;
    auto batch_error = CaptureFileCopySources(source_paths, sources);
    blink::core::FileEntry other_metadata, nested_metadata;
    const bool metadata_read = blink::core::ReadEntryMetadata(other_file.wstring(), other_metadata) &&
        blink::core::ReadEntryMetadata(nested.wstring(), nested_metadata);
    passed &= Check(batch_error == ERROR_SUCCESS && sources.size() == source_paths.size() && metadata_read &&
        sources[0].source.full_path == source_paths[0] && sources[0].source.identity == other_metadata.identity &&
        sources[0].source_parent_identity == nested_metadata.identity &&
        sources[1].source.full_path == source_paths[1] && sources[1].source.identity == captured.source.identity &&
        sources[2].source.full_path == source_paths[2] && sources[2].source.name == hardlink.filename().wstring() &&
        sources[2].source.identity == captured.source.identity &&
        sources[3].source.full_path == source_paths[3] && sources[3].source.identity == captured.source.identity &&
        ReadOwnedFile(file_path) == "clipboard source identity oracle" && ReadOwnedFile(other_file) == other_contents,
        "clipboard_batch_capture_preserves_order_duplicate_names_hardlink_identity_and_each_parent");

    const std::vector<std::wstring> extended_paths{ L"\\\\?\\" + file_path.wstring(), L"\\\\?\\" + other_file.wstring() };
    batch_error = CaptureFileCopySources(extended_paths, sources);
    passed &= Check(batch_error == ERROR_SUCCESS && sources.size() == 2 && sources[0].source.identity == captured.source.identity &&
        sources[1].source.identity == other_metadata.identity && sources[0].source_parent_identity == captured.source_parent_identity &&
        sources[1].source_parent_identity == nested_metadata.identity,
        "clipboard_batch_capture_rechecks_extended_paths_without_changing_identity");

    sources = { captured };
    const std::vector<std::wstring> mixed_folder{ file_path.wstring(), nested.wstring(), other_file.wstring() };
    batch_error = CaptureFileCopySources(mixed_folder, sources);
    passed &= Check(batch_error == ERROR_NOT_SUPPORTED && sources.empty() &&
        ReadOwnedFile(file_path) == "clipboard source identity oracle" && ReadOwnedFile(other_file) == other_contents,
        "clipboard_batch_mixed_folder_source_rejects_entire_selection_and_clears_partial_metadata");
    sources = { captured };
    const std::vector<std::wstring> missing_source{ file_path.wstring(), (fixture / L"missing-second.txt").wstring(), other_file.wstring() };
    batch_error = CaptureFileCopySources(missing_source, sources);
    passed &= Check(batch_error == ERROR_FILE_NOT_FOUND && sources.empty() &&
        ReadOwnedFile(file_path) == "clipboard source identity oracle" && ReadOwnedFile(other_file) == other_contents,
        "clipboard_batch_missing_later_source_never_returns_earlier_partial_capture");
    sources = { captured };
    const std::vector<std::wstring> mixed_unc{ file_path.wstring(), L"\\\\server\\share\\remote.txt" };
    batch_error = CaptureFileCopySources(mixed_unc, sources);
    passed &= Check(batch_error == ERROR_NOT_SUPPORTED && sources.empty(),
        "clipboard_batch_unsupported_later_source_clears_all_captured_requests");
    sources = { captured };
    const std::vector<std::wstring> malformed_paths{ file_path.wstring(), std::wstring(L"D:\\a\0b", 7) };
    batch_error = CaptureFileCopySources(malformed_paths, sources);
    passed &= Check(batch_error == ERROR_INVALID_PARAMETER && sources.empty(),
        "clipboard_batch_validates_every_input_string_before_metadata_capture");
    sources = { captured };
    batch_error = CaptureFileCopySources({}, sources);
    passed &= Check(batch_error == ERROR_INVALID_PARAMETER && sources.empty(),
        "clipboard_batch_empty_selection_is_invalid_and_clears_previous_requests");
    sources = { captured };
    const std::vector<std::wstring> too_many_sources(MaxClipboardFileCount + 1, file_path.wstring());
    batch_error = CaptureFileCopySources(too_many_sources, sources);
    passed &= Check(batch_error == ERROR_INVALID_PARAMETER && sources.empty(),
        "clipboard_batch_capture_rejects_over_1024_sources_before_any_partial_output");
    sources = { captured };
    const std::vector<std::wstring> oversized_selection(MaxClipboardFileCount, std::wstring(2047, L'x'));
    batch_error = CaptureFileCopySources(oversized_selection, sources);
    passed &= Check(batch_error == ERROR_INVALID_PARAMETER && sources.empty(),
        "clipboard_batch_capture_applies_aggregate_payload_bound_before_opening_sources");
    sources = { captured };
    const auto cancelled = std::make_shared<std::atomic_bool>(true);
    batch_error = CaptureFileCopySources(missing_source, sources, cancelled);
    passed &= Check(batch_error == ERROR_CANCELLED && sources.empty(),
        "clipboard_batch_precancel_opens_no_missing_source_and_returns_no_requests");

    DWORD sequence = 123;
    passed &= Check(PublishClipboardFilesCopy(nullptr, source_paths, sequence) == ERROR_INVALID_PARAMETER && sequence == 0,
        "clipboard_batch_publisher_rejects_null_owner_without_touching_actual_clipboard");
    sequence = 123;
    passed &= Check(PublishClipboardFileCopy(nullptr, file_path.wstring(), sequence) == ERROR_INVALID_PARAMETER && sequence == 0,
        "clipboard_single_publisher_compatibility_wrapper_preserves_validation_without_clipboard_access");

    const auto renamed_nested = fixture / L"metadata-handles-released";
    const bool parent_renamed = MoveFileExW(nested.c_str(), renamed_nested.c_str(), 0) != FALSE;
    const bool parent_restored = parent_renamed && MoveFileExW(renamed_nested.c_str(), nested.c_str(), 0) != FALSE;
    passed &= Check(parent_renamed && parent_restored,
        "clipboard_batch_success_failure_and_cancel_release_every_parent_lease");
    std::filesystem::remove_all(fixture, filesystem_error);
    passed &= Check(!filesystem_error && !std::filesystem::exists(fixture), "clipboard_worker_releases_metadata_handles");
    return passed;
}
