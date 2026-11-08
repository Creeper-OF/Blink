#include "SearchContextMenuTests.h"
#include "Blink/App/SearchContextCommands.h"
#include "Blink/App/ShellContextMenu.h"
#include "Blink/App/TabWindowState.h"
#include "Blink/App/CopyTransferPresentation.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <shellapi.h>
#include <algorithm>

#pragma comment(lib, "Shell32.lib")

namespace
{
    namespace fs = std::filesystem;
    using namespace blink::app;
    bool Check(const bool passed, const char* name)
    { std::cout << (passed ? "PASS " : "FAIL ") << name << '\n'; return passed; }
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry entry;
        if (!blink::core::ReadEntryMetadata(path.wstring(), entry)) { throw std::runtime_error("search context fixture metadata"); }
        entry.name = path.filename().wstring(); entry.full_path = path.wstring(); return entry;
    }
    SearchResult Result(const fs::path& path)
    {
        const auto entry = Entry(path), parent = Entry(path.parent_path());
        SearchResult result; result.source_volume = SearchDriveRoot(path.wstring());
        result.hit.identity = entry.identity; result.hit.parent_identity = parent.identity;
        result.hit.name = entry.name; result.hit.full_path = path.wstring();
        result.hit.is_directory = entry.is_directory; result.hit.attributes = entry.attributes; return result;
    }
    SearchContextSelection PublishResult(TabRecord& tab, const SearchResult& result)
    {
        tab.EnterSearch();
        SearchQuery query{ result.hit.name };
        const auto generation = tab.search_state->Begin(query);
        SearchSnapshot snapshot; snapshot.query = query; snapshot.stage = SearchStage::Complete;
        SearchVolumeResult volume; volume.root = result.source_volume; volume.stage = VolumeIndexStage::Healthy;
        volume.searched = true; volume.results = { result }; snapshot.volumes = { volume };
        if (!generation || !tab.search_state->Complete(generation, snapshot)) { throw std::runtime_error("search context fixture publish"); }
        tab.search_snapshot = std::make_shared<const SearchSnapshot>(tab.search_state->Snapshot());
        tab.search_selection = result;
        return { tab.id, generation, result };
    }
    std::vector<std::wstring> Arguments(const std::wstring& command_line)
    {
        int count{}; auto raw = CommandLineToArgvW(command_line.c_str(), &count);
        if (!raw) { return {}; }
        struct Release { LPWSTR* value; ~Release() { LocalFree(value); } } release{ raw };
        std::vector<std::wstring> result;
        for (int index = 0; index < count; ++index) { result.emplace_back(raw[index]); }
        return result;
    }
    std::wstring Argument(const std::vector<std::wstring>& arguments, const std::wstring& key)
    {
        auto found = std::find(arguments.begin(), arguments.end(), key);
        return found != arguments.end() && ++found != arguments.end() ? *found : std::wstring{};
    }
    bool HandleArgument(const std::vector<std::wstring>& arguments, const std::wstring& key, const std::uintptr_t expected)
    {
        try
        {
            const auto value = Argument(arguments, key); std::size_t consumed{};
            return !value.empty() && std::stoull(value, &consumed, 0) == expected && consumed == value.size();
        }
        catch (...) { return false; }
    }
    bool RunShellMenuProtocolTests()
    {
        bool passed = true;
        const std::wstring host = L"D:\\工具 目录\\Blink.ShellMenuHost.exe";
        ShellMenuRequest request;
        request.entry.identity = { 0x12345678ABCDEF01ull, { 1, 2, 3, 4 }, true };
        request.parent_identity = { 0x12345678ABCDEF01ull, { 5, 6, 7, 8 }, true };
        request.position = { -1920, 1080 }; request.locale = L"zh-CN"; request.extended = true; request.verb = L"properties";
        const auto owner = std::uintptr_t{ 0x1234567887654321ull }, cancel = std::uintptr_t{ 0x2345678998765432ull };
        request.owner = reinterpret_cast<HWND>(owner);
        bool quoted = true;
        std::vector<std::wstring> arguments;
        for (const auto& path : { L"D:\\资料 空格\\file name.txt", L"D:\\引号 \"quoted\"\\file.txt", L"D:\\目录 空格\\" })
        {
            request.entry.full_path = path;
            arguments = Arguments(BuildShellMenuCommandLine(host, request, cancel));
            quoted &= !arguments.empty() && arguments.front() == host && Argument(arguments, L"--path") == path &&
                Argument(arguments, L"--identity") == request.entry.identity.ToString() &&
                Argument(arguments, L"--parent-identity") == request.parent_identity.ToString();
        }
        passed &= Check(quoted, "shell_menu_command_line_roundtrips_unicode_spaces_quotes_and_trailing_backslash");
        passed &= Check(HandleArgument(arguments, L"--owner", owner) && HandleArgument(arguments, L"--cancel-handle", cancel) &&
            Argument(arguments, L"--x") == L"-1920" && Argument(arguments, L"--y") == L"1080" &&
            Argument(arguments, L"--locale") == L"zh-CN" && Argument(arguments, L"--verb") == L"properties" &&
            std::find(arguments.begin(), arguments.end(), L"--extended") != arguments.end(),
            "shell_menu_command_line_preserves_full_width_handles_coordinates_and_options");
        auto additional = request.entry;
        additional.full_path = L"D:\\多选 空格\\资料 一.txt";
        request.additional_entries.push_back(additional);
        additional.full_path = L"D:\\多选 空格\\引号 \"二\"\\";
        additional.identity.file_id[0] = 42;
        request.additional_entries.push_back(additional);
        arguments = Arguments(BuildShellMenuCommandLine(host, request, cancel));
        std::vector<std::wstring> extra_paths, extra_identities;
        bool pairs = true;
        for (std::size_t index = 1; index < arguments.size(); ++index)
        {
            if (arguments[index] == L"--additional-path")
            {
                pairs &= index + 3 < arguments.size() && arguments[index + 2] == L"--additional-identity";
                if (index + 1 < arguments.size()) { extra_paths.push_back(arguments[index + 1]); }
            }
            else if (arguments[index] == L"--additional-identity" && index + 1 < arguments.size())
            { extra_identities.push_back(arguments[index + 1]); }
        }
        passed &= Check(pairs && extra_paths == std::vector<std::wstring>{ request.additional_entries[0].full_path,
            request.additional_entries[1].full_path } && extra_identities == std::vector<std::wstring>{
            request.additional_entries[0].identity.ToString(), request.additional_entries[1].identity.ToString() },
            "shell_menu_multi_selection_preserves_order_paired_identities_and_quoted_paths");
        request.additional_entries.clear();
        arguments = Arguments(BuildShellMenuCommandLine(host, request));
        passed &= Check(std::find(arguments.begin(), arguments.end(), L"--additional-path") == arguments.end() &&
            std::find(arguments.begin(), arguments.end(), L"--additional-identity") == arguments.end(),
            "shell_menu_single_selection_command_line_keeps_original_cli");
        ShellMenuResult result;
        passed &= Check(ParseShellMenuReply("BLINK_SHELL_MENU_V1 cancel 0 00000000", result) &&
            result.kind == ShellMenuResultKind::Cancelled && result.command == ShellMenuCommand::None && result.error == S_OK,
            "shell_menu_reply_accepts_explicit_cancel_without_line_ending");
        passed &= Check(ParseShellMenuReply("BLINK_SHELL_MENU_V1 native 0 00000000\n", result) &&
            result.kind == ShellMenuResultKind::NativeCommand && result.command == ShellMenuCommand::None && result.error == S_OK,
            "shell_menu_reply_accepts_native_selection_with_lf_without_claiming_completion");
        passed &= Check(ParseShellMenuReply("BLINK_SHELL_MENU_V1 error 0 80070005\r\n", result) &&
            result.kind == ShellMenuResultKind::Error && result.command == ShellMenuCommand::None && result.error == E_ACCESSDENIED,
            "shell_menu_reply_accepts_failed_hresult_and_crlf");
        bool blink = true;
        for (unsigned command = 1; command <= 10; ++command)
        {
            const auto line = "BLINK_SHELL_MENU_V1 blink " + std::to_string(command) + " 00000000\r\n";
            blink &= ParseShellMenuReply(line, result) && result.kind == ShellMenuResultKind::BlinkCommand &&
                static_cast<unsigned>(result.command) == command && result.error == S_OK;
        }
        passed &= Check(blink, "shell_menu_reply_accepts_exactly_defined_blink_commands_one_through_ten");
        passed &= Check(ParseShellMenuReply("BLINK_SHELL_MENU_V1 error 0 8000ffff", result) && result.error == E_UNEXPECTED,
            "shell_menu_reply_preserves_all_eight_hresult_hex_digits_case_insensitively");
        bool malformed = true;
        for (const auto& line : { "", "BLINK_SHELL_MENU_V2 cancel 0 00000000", "BLINK_SHELL_MENU_V1 unknown 0 00000000",
            "BLINK_SHELL_MENU_V1 blink 1", "BLINK_SHELL_MENU_V1 blink 1 0000000", "BLINK_SHELL_MENU_V1 blink 1 000000000",
            "BLINK_SHELL_MENU_V1 blink 1 0x00000000", "BLINK_SHELL_MENU_V1 blink 1 G0000000", "BLINK_SHELL_MENU_V1 blink 1 00000000 extra",
            "BLINK_SHELL_MENU_V1 blink 1 00000000\nBLINK_SHELL_MENU_V1 cancel 0 00000000", "BLINK_SHELL_MENU_V1 blink 1 00000000\r" })
        { malformed &= !ParseShellMenuReply(line, result); }
        passed &= Check(malformed, "shell_menu_reply_rejects_bad_magic_fields_hex_and_extra_protocol_data");
        bool invalid_action = true;
        for (const auto& line : { "BLINK_SHELL_MENU_V1 blink 0 00000000", "BLINK_SHELL_MENU_V1 blink 11 00000000",
            "BLINK_SHELL_MENU_V1 cancel 1 00000000", "BLINK_SHELL_MENU_V1 native 2 00000000", "BLINK_SHELL_MENU_V1 error 1 80004005",
            "BLINK_SHELL_MENU_V1 error 0 00000000", "BLINK_SHELL_MENU_V1 error 0 00000001", "BLINK_SHELL_MENU_V1 cancel 0 80004005",
            "BLINK_SHELL_MENU_V1 native 0 00000001", "BLINK_SHELL_MENU_V1 blink 1 80004005" })
        { invalid_action &= !ParseShellMenuReply(line, result); }
        passed &= Check(invalid_action, "shell_menu_reply_rejects_undefined_commands_and_inconsistent_hresult_semantics");
        bool overflow = true;
        for (const auto& line : { "BLINK_SHELL_MENU_V1 blink 4294967296 00000000", "BLINK_SHELL_MENU_V1 blink 18446744073709551616 00000000",
            "BLINK_SHELL_MENU_V1 blink -1 00000000", "BLINK_SHELL_MENU_V1 blink +1 00000000", "BLINK_SHELL_MENU_V1 blink 0x1 00000000" })
        { overflow &= !ParseShellMenuReply(line, result); }
        passed &= Check(overflow, "shell_menu_reply_rejects_decimal_overflow_signs_and_non_decimal_command_ids");
        return passed;
    }
}

bool RunSearchContextMenuTests()
{
    bool passed = true;
    const auto root = fs::absolute(fs::path(L".build") / L"search-context-menu-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{ root };
    try
    {
        passed &= RunShellMenuProtocolTests();
        const auto sources = root / L"搜索来源", behind = root / L"背后浏览";
        fs::create_directories(sources / L"目录结果"); fs::create_directories(behind);
        const auto file = sources / L"资料 文件.txt", hidden_selection = behind / L"背后选中.txt";
        { std::ofstream stream(file); stream << "search target"; }
        { std::ofstream stream(hidden_selection); stream << "unrelated browsing selection"; }
        const auto result = Result(file); const auto target = ValidateSearchResult(result);
        ShellMenuRequest folder_menu;
        folder_menu.entry = Entry(file); folder_menu.parent_identity = Entry(sources).identity;
        passed &= Check(ValidateShellMenuRequest(folder_menu) == ERROR_SUCCESS,
            "folder_shell_menu_accepts_real_selection_without_search_result");
        folder_menu.additional_entries = { Entry(sources / L"目录结果") };
        passed &= Check(ValidateShellMenuRequest(folder_menu) == ERROR_SUCCESS,
            "folder_shell_menu_accepts_file_and_directory_siblings_together");
        auto invalid_menu = folder_menu;
        invalid_menu.additional_entries = { invalid_menu.entry };
        invalid_menu.additional_entries.front().full_path = (sources / L"." / file.filename()).wstring();
        passed &= Check(ValidateShellMenuRequest(invalid_menu) == ERROR_INVALID_PARAMETER,
            "folder_shell_menu_rejects_duplicate_normalized_selected_paths");
        invalid_menu = folder_menu; invalid_menu.additional_entries = { Entry(hidden_selection) };
        passed &= Check(ValidateShellMenuRequest(invalid_menu) == ERROR_INVALID_PARAMETER,
            "folder_shell_menu_rejects_selection_from_another_parent");
        invalid_menu = folder_menu; invalid_menu.additional_entries.front().identity.file_id[0] ^= 1;
        passed &= Check(ValidateShellMenuRequest(invalid_menu) == ERROR_FILE_INVALID,
            "folder_shell_menu_revalidates_every_selected_sibling_identity");
        invalid_menu = folder_menu; invalid_menu.parent_identity.file_id[0] ^= 1;
        passed &= Check(ValidateShellMenuRequest(invalid_menu) == ERROR_FILE_INVALID,
            "folder_shell_menu_revalidates_captured_parent_identity");
        invalid_menu = folder_menu;
        invalid_menu.additional_entries.front().full_path = (sources / L"已不存在.txt").wstring();
        passed &= Check(ValidateShellMenuRequest(invalid_menu) == ERROR_FILE_NOT_FOUND,
            "folder_shell_menu_returns_real_missing_sibling_error");
        TabWindowState window; auto* tab = window.ActiveTab();
        const auto navigation = tab->state->Navigate(behind.wstring());
        if (!navigation || tab->state->CommitBrowse(*navigation, blink::core::EnumerateDirectory(behind.wstring())).status != CommitBrowseStatus::Committed)
        { throw std::runtime_error("search context fixture browse"); }
        tab->state->SetRowSelection({ MakeRowIdentity(Entry(hidden_selection)) });
        auto selection = PublishResult(*tab, result);
        passed &= Check(!target.error && IsSearchContextCurrent(selection, window.ActiveTab()),
            "search_context_current_published_single_result_is_actionable");

        ClipboardCopySources clipboard;
        const auto error = BuildSearchClipboardSources(target, clipboard);
        const auto* files = std::get_if<std::vector<FileCopyRequest>>(&clipboard);
        const auto browsing = tab->state->SelectedCommittedRows(1);
        passed &= Check(!error && files && files->size() == 1 && browsing && browsing->size() == 1 &&
            browsing->front().name == hidden_selection.filename().wstring() && files->front().source.name == file.filename().wstring() &&
            files->front().source.full_path == file.wstring() && files->front().source.identity == target.entry.identity &&
            files->front().source_parent == sources.wstring() && files->front().source_parent_identity == target.parent_identity,
            "search_context_clipboard_uses_validated_result_instead_of_hidden_browse_selection");
        passed &= Check(files && files->front().destination_parent.empty() && files->front().destination_name.empty() &&
            !files->front().destination_parent_identity.valid,
            "search_context_copy_and_cut_sources_have_no_preselected_destination");
        const auto payload = EncodeClipboardFilePaths(std::vector<std::wstring>{ target.entry.full_path });
        const auto decoded = DecodeClipboardFilePaths(payload);
        ClipboardCopySources captured;
        passed &= Check(!decoded.error && decoded.paths == std::vector<std::wstring>{ file.wstring() } &&
            !CaptureClipboardCopySources(decoded.paths, captured) && SameCapturedCopySources(clipboard, captured),
            "search_context_unicode_clipboard_sources_match_existing_native_capture");

        const auto folder_result = Result(sources / L"目录结果");
        const auto folder_target = ValidateSearchResult(folder_result);
        ClipboardCopySources folder_sources;
        const auto folder_error = BuildSearchClipboardSources(folder_target, folder_sources);
        const auto* directory = std::get_if<DirectoryCopyRequest>(&folder_sources);
        passed &= Check(!folder_error && directory && directory->source.is_directory &&
            directory->source.identity == folder_target.entry.identity && directory->source_parent == sources.wstring() &&
            directory->source_parent_identity == folder_target.parent_identity && directory->destination_parent.empty(),
            "search_context_directory_result_uses_existing_directory_clipboard_variant");
        passed &= Check(folder_target.parent == sources.wstring() && folder_target.parent != folder_target.entry.full_path &&
            folder_target.parent_identity == result.hit.parent_identity,
            "search_context_directory_parent_destination_is_its_containing_directory");

        const auto alias = sources / L"硬链接 副本.txt";
        const bool linked = CreateHardLinkW(alias.c_str(), file.c_str(), nullptr) != FALSE;
        const auto alias_result = Result(alias); const auto alias_target = ValidateSearchResult(alias_result);
        folder_menu.additional_entries = { alias_target.entry };
        passed &= Check(linked && ValidateShellMenuRequest(folder_menu) == ERROR_SUCCESS,
            "folder_shell_menu_keeps_distinct_hardlink_names_in_multi_selection");
        ClipboardCopySources alias_sources;
        const auto alias_error = BuildSearchClipboardSources(alias_target, alias_sources);
        const auto* aliases = std::get_if<std::vector<FileCopyRequest>>(&alias_sources);
        passed &= Check(linked && !alias_error && aliases && aliases->size() == 1 &&
            aliases->front().source.identity == target.entry.identity && aliases->front().source.name == alias.filename().wstring() &&
            aliases->front().source.full_path == alias.wstring() && !SameCapturedCopySources(clipboard, alias_sources),
            "search_context_hardlink_result_preserves_clicked_alias_name_and_path");

        auto invalid = target; invalid.error = ERROR_FILE_NOT_FOUND;
        passed &= Check(BuildSearchClipboardSources(invalid, clipboard) == ERROR_FILE_NOT_FOUND &&
            std::holds_alternative<std::monostate>(clipboard),
            "search_context_failed_validation_clears_prior_clipboard_sources");
        invalid = target; invalid.parent = behind.wstring();
        passed &= Check(BuildSearchClipboardSources(invalid, clipboard) != ERROR_SUCCESS &&
            std::holds_alternative<std::monostate>(clipboard),
            "search_context_target_parent_must_match_result_full_path");
        invalid = target; invalid.parent_identity.valid = false;
        passed &= Check(BuildSearchClipboardSources(invalid, clipboard) == ERROR_FILE_INVALID &&
            std::holds_alternative<std::monostate>(clipboard),
            "search_context_target_requires_validated_parent_identity");

        tab->search_draft = L"new draft only"; tab->search_mode = SearchMode::Exact;
        tab->search_scope = SearchScope::SelectedVolumes; tab->search_selected_volumes = L"C:";
        passed &= Check(IsSearchContextCurrent(selection, window.ActiveTab()),
            "search_context_unsubmitted_draft_and_options_do_not_change_menu_target");
        const auto published = tab->search_snapshot;
        auto removed = std::make_shared<SearchSnapshot>(*published); removed->volumes.front().results.clear(); tab->search_snapshot = removed;
        passed &= Check(!IsSearchContextCurrent(selection, window.ActiveTab()),
            "search_context_removed_result_invalidates_old_menu");
        removed = std::make_shared<SearchSnapshot>(*published); removed->volumes.front().searched = false; tab->search_snapshot = removed;
        passed &= Check(!IsSearchContextCurrent(selection, window.ActiveTab()),
            "search_context_failed_volume_cannot_reuse_stored_rows");
        removed = std::make_shared<SearchSnapshot>(*published); removed->volumes.front().results = { alias_result }; tab->search_snapshot = removed;
        passed &= Check(!IsSearchContextCurrent(selection, window.ActiveTab()),
            "search_context_same_identity_under_another_hardlink_name_is_not_original_row");
        tab->search_snapshot = published; tab->search_queued = true;
        passed &= Check(!IsSearchContextCurrent(selection, window.ActiveTab()),
            "search_context_queued_replacement_search_invalidates_old_menu_before_rpc");
        tab->search_queued = false;
        const auto old_tab = tab->id; const auto other_tab = window.CreateTab(); (void)window.ActivateTab(other_tab);
        passed &= Check(!IsSearchContextCurrent(selection, window.ActiveTab()),
            "search_context_switching_active_tab_does_not_retarget_old_menu");
        (void)window.ActivateTab(old_tab);
        passed &= Check(!IsSearchContextCurrent(selection, window.ActiveTab(), true),
            "search_context_closed_window_rejects_old_menu");
        (void)tab->search_state->Begin({ L"replacement query" });
        passed &= Check(!IsSearchContextCurrent(selection, window.ActiveTab()),
            "search_context_new_query_generation_invalidates_old_menu");
        tab->search_state->Cancel(); selection = PublishResult(*tab, result); tab->LeaveSearch();
        passed &= Check(!IsSearchContextCurrent(selection, window.ActiveTab()),
            "search_context_leaving_search_invalidates_old_menu");
    }
    catch (const std::exception& error)
    { std::cout << "INFO search_context_fixture_error " << error.what() << '\n'; passed &= Check(false, "search_context_fixture_completed"); }
    return passed;
}
