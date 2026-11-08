#include "CopyPresentationTests.h"
#include "Blink/App/FileOperationOrigin.h"
#include "Blink/App/CopyTransferPresentation.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace blink::app;
    bool Check(bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    blink::core::FileIdentity Identity(unsigned char value)
    { blink::core::FileIdentity id; id.valid = true; id.volume_serial = 17; id.file_id[0] = value; return id; }
    void Navigate(TabRecord& tab, const wchar_t* path, unsigned char id, bool automatic = false)
    {
        const auto request = tab.state->Navigate(std::wstring(path));
        if (!request) { throw std::runtime_error("origin fixture navigation request failed"); }
        tab.OnNavigation(automatic);
        blink::core::DirectoryListing listing; listing.path = path; listing.directory_identity = Identity(id);
        if (tab.state->CommitBrowse(*request, std::move(listing)).status != CommitBrowseStatus::Committed)
        { throw std::runtime_error("origin fixture commit failed"); }
    }
}

bool RunCopyPresentationTests()
{
    using namespace blink::app;
    bool passed = true;
    TabWindowState window;
    auto* tab = window.ActiveTab();
    Navigate(*tab, L"C:\\origin-a", 1);
    const auto initial = CaptureFileOperationOrigin(*tab);
    passed &= Check(IsFileOperationOriginCurrent(initial, window.ActiveTab()), "copy_origin_initial_page_is_current");
    // A queued preparation completes only after deterministic user navigation;
    // no sleep or filesystem mutation is needed to expose a stale UI submit.
    unsigned submissions = 0;
    const auto delayed = [&] { if (IsFileOperationOriginCurrent(initial, window.ActiveTab())) { ++submissions; } };
    Navigate(*tab, L"C:\\origin-b", 2);
    passed &= Check(!IsFileOperationOriginCurrent(initial, window.ActiveTab()), "copy_origin_departed_page_rejects");
    Navigate(*tab, L"C:\\origin-a", 1);
    delayed();
    passed &= Check(submissions == 0, "copy_origin_a_b_a_never_resurrects_queued_submission");

    const auto before_switch = CaptureFileOperationOrigin(*tab);
    const auto other = window.CreateTab();
    (void)window.ActivateTab(other);
    passed &= Check(!IsFileOperationOriginCurrent(before_switch, window.ActiveTab()), "copy_origin_other_tab_rejects");
    (void)window.ActivateTab(tab->id);
    passed &= Check(!IsFileOperationOriginCurrent(before_switch, window.ActiveTab()), "copy_origin_tab_switch_back_stays_stale");

    const auto before_search = CaptureFileOperationOrigin(*tab);
    tab->EnterSearch();
    passed &= Check(!IsFileOperationOriginCurrent(before_search, tab), "copy_origin_search_surface_rejects");
    tab->LeaveSearch();
    passed &= Check(!IsFileOperationOriginCurrent(before_search, tab), "copy_origin_leave_search_stays_stale");

    const auto before_failed = CaptureFileOperationOrigin(*tab);
    const auto failed = tab->state->Navigate(L"C:\\missing-origin");
    if (!failed) { throw std::runtime_error("origin failure request missing"); }
    tab->OnNavigation(false);
    (void)tab->state->RejectBrowse(*failed, ERROR_PATH_NOT_FOUND);
    passed &= Check(!IsFileOperationOriginCurrent(before_failed, tab), "copy_origin_failed_user_navigation_stays_stale");

    const auto before_auto = CaptureFileOperationOrigin(*tab);
    Navigate(*tab, L"C:\\origin-a", 1, true);
    passed &= Check(IsFileOperationOriginCurrent(before_auto, tab), "copy_origin_same_identity_automatic_refresh_preserves_intent");
    Navigate(*tab, L"C:\\origin-a", 3, true);
    passed &= Check(!IsFileOperationOriginCurrent(before_auto, tab), "copy_origin_replacement_identity_rejects");
    const auto closing = CaptureFileOperationOrigin(*tab);
    passed &= Check(!IsFileOperationOriginCurrent(closing, tab, true) &&
        !IsFileOperationOriginCurrent(closing, nullptr), "copy_origin_closed_window_or_missing_tab_rejects");
    window.Shutdown();
    passed &= Check(!IsFileOperationOriginCurrent(closing, tab), "copy_origin_shutdown_invalidates_existing_tab");

    FileCopyRequest first; first.source_parent = L"C:\\source";
    first.source_parent_identity = Identity(10); first.source.identity = Identity(11); first.source.name = L"first.txt";
    auto second = first; second.source.name = L"hardlink.txt";
    ClipboardCopySources files = std::vector<FileCopyRequest>{ first, second, first };
    passed &= Check(SameCapturedCopySources(files, files), "copy_transfer_file_duplicates_and_hardlink_names_preserved");
    auto changed_files = files;
    std::swap(std::get<std::vector<FileCopyRequest>>(changed_files)[0], std::get<std::vector<FileCopyRequest>>(changed_files)[1]);
    passed &= Check(!SameCapturedCopySources(files, changed_files), "copy_transfer_reordered_same_id_hardlinks_rejected");
    DirectoryCopyRequest dir; dir.source_parent = L"C:\\source"; dir.source_parent_identity = Identity(10);
    dir.source.identity = Identity(12); dir.source.name = L"tree"; dir.source.is_directory = true;
    dir.source.attributes = FILE_ATTRIBUTE_DIRECTORY;
    ClipboardCopySources directory = dir;
    passed &= Check(SameCapturedCopySources(directory, directory) && !SameCapturedCopySources(files, directory),
        "copy_transfer_directory_mode_does_not_fall_back_to_files");
    auto changed_dir = directory; std::get<DirectoryCopyRequest>(changed_dir).source_parent_identity = Identity(13);
    passed &= Check(!SameCapturedCopySources(directory, changed_dir), "copy_transfer_changed_source_parent_identity_rejected");
    ClipboardCopySources empty;
    passed &= Check(!SameCapturedCopySources(empty, empty), "copy_transfer_empty_mode_never_proves_identity");

    ClipboardCopyCache cache{ directory, { L"\\\\?\\C:\\source\\tree" }, 123 };
    ClipboardFilePaths payload{ ERROR_SUCCESS, cache.paths, 123 };
    passed &= Check(cache.MatchesPayload(payload), "copy_transfer_exact_extended_payload_retains_directory_binding");
    payload.sequence = 124;
    passed &= Check(!cache.MatchesPayload(payload), "copy_transfer_external_sequence_does_not_use_internal_identity");
    payload.sequence = 123; payload.paths[0] = L"C:\\source\\tree";
    passed &= Check(!cache.MatchesPayload(payload), "copy_transfer_payload_comparison_uses_published_paths");
    payload.paths = cache.paths; payload.error = ERROR_NOT_SUPPORTED;
    passed &= Check(!cache.MatchesPayload(payload), "copy_transfer_invalid_clipboard_intent_rejected");

    ClipboardCopySources output = directory;
    const auto parent_id = Identity(20);
    const auto file_error = PrepareCopyDestination(files, L"C:\\destination", parent_id, output);
    const auto* bound_files = std::get_if<std::vector<FileCopyRequest>>(&output);
    passed &= Check(file_error == ERROR_SUCCESS && bound_files && bound_files->size() == 3 &&
        (*bound_files)[1].source.name == L"hardlink.txt" && (*bound_files)[2].source.name == L"first.txt" &&
        (*bound_files)[0].destination_parent == L"C:\\destination" && (*bound_files)[0].destination_parent_identity == parent_id &&
        (*bound_files)[0].destination_name == L"first.txt" && SameCapturedCopySources(files, output),
        "copy_transfer_binds_entire_file_batch_without_replacing_source_identities");
    const auto dir_error = PrepareCopyDestination(directory, L"C:\\destination", parent_id, output);
    const auto* bound_dir = std::get_if<DirectoryCopyRequest>(&output);
    passed &= Check(dir_error == ERROR_SUCCESS && bound_dir && bound_dir->destination_parent == L"C:\\destination" &&
        bound_dir->destination_parent_identity == parent_id && bound_dir->destination_name == L"tree" &&
        SameCapturedCopySources(directory, output), "copy_transfer_binds_directory_to_current_destination_identity");
    passed &= Check(PrepareCopyDestination(directory, L"", parent_id, output) == ERROR_INVALID_PARAMETER &&
        std::holds_alternative<std::monostate>(output), "copy_transfer_bad_destination_clears_previous_bound_output");
    passed &= Check(PrepareCopyDestination(directory, L"C:\\destination", {}, output) == ERROR_INVALID_PARAMETER &&
        std::holds_alternative<std::monostate>(output), "copy_transfer_missing_destination_identity_zero_submit");

    FileOperationSnapshot partial; partial.operation_id = 42; partial.kind = FileOperationKind::DirectoryCopy;
    partial.stage = FileOperationStage::Failed;
    partial.directory_copy_result = DirectoryCopyResult{ ERROR_CANCELLED, false, true, L"C:\\destination\\partial", 0, 1 };
    passed &= Check(!partial.result.partial_file_left && DirectoryCopyNeedsRefresh(partial),
        "directory_ui_empty_partial_root_refreshes_despite_generic_file_partial_false");
    DirectoryCopyPartialHistory history; history.Observe(partial); history.Observe(partial);
    passed &= Check(history.Count() == 1 && history.Paths().size() == 1 && history.Paths().front() == L"C:\\destination\\partial",
        "directory_ui_duplicate_publications_retain_one_truthful_partial_path");
    auto success = partial; success.operation_id = 43; success.stage = FileOperationStage::Completed;
    success.directory_copy_result = DirectoryCopyResult{ ERROR_SUCCESS, true, false, L"C:\\destination\\retry", 2, 2 };
    history.Observe(success);
    passed &= Check(DirectoryCopyNeedsRefresh(success) && history.Count() == 1 && history.Paths().size() == 1,
        "directory_ui_successful_new_name_retry_retains_prior_partial_root");
    auto stale = partial; stale.operation_id = 41; history.Observe(stale);
    passed &= Check(history.Count() == 1, "directory_ui_old_operation_cannot_recount_partial_history");
    FileOperationSnapshot no_effect; no_effect.kind = FileOperationKind::DirectoryCopy;
    no_effect.directory_copy_result = DirectoryCopyResult{ ERROR_ALREADY_EXISTS };
    passed &= Check(!DirectoryCopyNeedsRefresh(no_effect), "directory_ui_existing_root_zero_effect_never_claims_write");
    for (unsigned n = 44; n < 100; ++n) { partial.operation_id = n; history.Observe(partial); }
    passed &= Check(history.Count() == 57 && history.Paths().size() == 32,
        "directory_ui_partial_path_retention_is_bounded_without_losing_count");
    history.Reset();
    passed &= Check(history.Count() == 0 && history.Paths().empty(), "directory_ui_new_explicit_copy_starts_new_history_lineage");

    const auto root = std::filesystem::absolute(std::filesystem::path(L".build") / L"copy-presentation-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    std::filesystem::create_directories(root);
    try
    {
        const auto original = root / L"original";
        std::filesystem::create_directory(original);
        std::ofstream(original / L"preserved.txt") << "owned source";
        ClipboardCopySources before, after;
        const std::vector<std::wstring> paths{ original.wstring() };
        const auto before_error = CaptureClipboardCopySources(paths, before);
        std::filesystem::rename(original, root / L"moved-original");
        std::filesystem::create_directory(original);
        const auto after_error = CaptureClipboardCopySources(paths, after);
        passed &= Check(before_error == ERROR_SUCCESS && after_error == ERROR_SUCCESS &&
            !SameCapturedCopySources(before, after) && std::filesystem::exists(root / L"moved-original" / L"preserved.txt"),
            "copy_transfer_real_same_name_directory_replacement_cannot_reuse_cached_identity");
    }
    catch (...) { std::filesystem::remove_all(root); throw; }
    std::filesystem::remove_all(root);
    return passed;
}
