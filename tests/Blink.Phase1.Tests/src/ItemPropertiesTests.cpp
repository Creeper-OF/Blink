#include "ItemPropertiesTests.h"
#include "Blink/App/PropertiesPresentation.h"
#include "Blink/App/FolderCommandPolicy.h"
#include <winioctl.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <future>

bool CreateOperationTestJunction(const std::filesystem::path&, const std::filesystem::path&);
namespace
{
    namespace fs = std::filesystem;
    using namespace blink::app;
    bool Check(bool value, const char* name) { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    fs::path Extended(const fs::path& path) { return path.wstring().starts_with(L"\\\\?\\") ? path : fs::path(L"\\\\?\\" + path.wstring()); }
    void Write(const fs::path& path, const std::string& data)
    { std::ofstream file(Extended(path), std::ios::binary); file << data; if (!file) { throw std::runtime_error("properties fixture write"); } }
    std::string Read(const fs::path& path) { std::ifstream file(Extended(path), std::ios::binary); return { std::istreambuf_iterator<char>(file), {} }; }
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry value; if (!blink::core::ReadEntryMetadata(Extended(path).wstring(), value)) { throw std::runtime_error("properties fixture metadata"); }
        value.name = path.filename().wstring(); value.full_path = path.wstring(); value.extension = blink::core::FileExtension(value.name); return value;
    }
    ItemPropertiesRequest Request(const fs::path& parent, const std::vector<fs::path>& paths)
    { ItemPropertiesRequest value{ parent.wstring(), Entry(parent).identity }; for (const auto& path : paths) { value.entries.push_back(Entry(path)); } return value; }
    void Cleanup(const fs::path& root) noexcept
    {
        WIN32_FIND_DATAW entry{}; const auto pattern = Extended(root) / L"*"; const auto find = FindFirstFileW(pattern.c_str(), &entry);
        if (find == INVALID_HANDLE_VALUE) { return; }
        do
        {
            if (!std::wcscmp(entry.cFileName, L".") || !std::wcscmp(entry.cFileName, L"..")) { continue; }
            const auto path = Extended(root) / entry.cFileName;
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            { if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { Cleanup(path); } SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); RemoveDirectoryW(path.c_str()); }
            else { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(path.c_str()); }
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
}
bool RunItemPropertiesTests()
{
    bool passed = true;
    const auto root = fs::absolute(fs::path(L".build") / L"item-properties-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    fs::create_directories(root);
    struct Final { fs::path root; ~Final() { Cleanup(root); RemoveDirectoryW(Extended(root).c_str()); } } final{ root };
    try
    {
        const auto parent = root / L"基础属性"; fs::create_directory(parent); const auto file = parent / L"文字.txt";
        Write(file, "payload"); Write(file.wstring() + L":Zone.Identifier", "zone");
        auto request = Request(parent, { file }); request.entries[0].size = 999;
        const auto original = Entry(file); const auto result = ReadItemProperties(request);
        passed &= Check(PropertiesTotalsComplete(result) && result.files == 1 && result.bytes == 7 && result.items[0].entry.identity == original.identity &&
            result.items[0].entry.full_path == file.wstring(), "properties_real_file_reads_current_handle_metadata_instead_of_cached_row_size");
        passed &= Check(CompareFileTime(&result.items[0].entry.creation_time, &original.creation_time) == 0 &&
            CompareFileTime(&result.items[0].entry.last_write_time, &original.last_write_time) == 0 &&
            CompareFileTime(&result.items[0].entry.last_access_time, &original.last_access_time) == 0 &&
            result.allocated_bytes == original.allocated_size, "properties_real_file_reports_creation_write_access_times_and_allocation");
        passed &= Check(Read(file) == "payload" && Read(file.wstring() + L":Zone.Identifier") == "zone",
            "properties_metadata_read_preserves_main_content_and_ads");
        {
            const auto directory = parent / L"tree"; fs::create_directories(directory / L"sub" / L"empty");
            Write(directory / L"root.txt", "abc"); Write(directory / L"sub" / L"child.txt", "12345");
            const auto source = Request(parent, { directory }); const auto value = ReadItemProperties(source);
            passed &= Check(PropertiesTotalsComplete(value) && value.files == 2 && value.folders == 2 && value.bytes == 8 &&
                value.selected_folders == 1 && value.selected_files == 0, "properties_recursive_directory_includes_nested_files_and_empty_folders");
            const auto mixed = ReadItemProperties(Request(parent, { file, directory }));
            passed &= Check(PropertiesTotalsComplete(mixed) && mixed.selected_count == 2 && mixed.selected_files == 1 && mixed.selected_folders == 1 &&
                mixed.files == 3 && mixed.bytes == 15 && mixed.folders == 2, "properties_multi_selection_aggregates_files_and_directory_contents");
            auto cancel = std::make_shared<std::atomic_bool>();
            const auto stopped = ReadItemProperties(source, cancel, [cancel](const ItemPropertiesSnapshot& value) {
                if (value.files) { cancel->store(true); }
            });
            passed &= Check(stopped.finished && stopped.cancelled && !PropertiesTotalsComplete(stopped) && stopped.files == 1 && stopped.bytes == 3 &&
                Read(directory / L"sub" / L"child.txt") == "12345", "properties_cancel_between_directory_reads_preserves_counted_prefix_and_source");
            cancel = std::make_shared<std::atomic_bool>();
            const auto cancelled_root = ReadItemProperties(source, cancel, [cancel](const ItemPropertiesSnapshot& value) {
                if (value.read_count) { cancel->store(true); }
            });
            passed &= Check(cancelled_root.cancelled && cancelled_root.read_count == 1 && !cancelled_root.files && !cancelled_root.bytes,
                "properties_close_after_root_metadata_cancels_before_descendant_enumeration");
            const auto busy = directory / L"sub";
            blink::core::UniqueHandle reader(CreateFileW(Extended(busy).c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            const bool opened = static_cast<bool>(reader);
            const auto partial = ReadItemProperties(source); reader.reset();
            std::cout << "INFO properties_busy_descendant opened=" << opened << " error=" << partial.error << " bytes=" << partial.bytes
                << " files=" << partial.files << " unavailable=" << partial.unavailable_count << '\n';
            passed &= Check(opened && partial.finished && !PropertiesTotalsComplete(partial) && partial.error == ERROR_SHARING_VIOLATION && partial.bytes == 3 &&
                partial.files == 1 && partial.unavailable_count && !partial.failure_path.empty(), "properties_unreadable_descendant_keeps_other_totals_and_reports_partial");
            const auto empty = parent / L"empty"; fs::create_directory(empty); const auto zero = ReadItemProperties(Request(parent, { empty }));
            passed &= Check(PropertiesTotalsComplete(zero) && !zero.files && !zero.folders && !zero.bytes, "properties_empty_directory_has_complete_zero_content_size");
        }
        {
            const auto alias = parent / L"alias.txt"; fs::create_hard_link(file, alias);
            const auto value = ReadItemProperties(Request(parent, { file, alias }));
            passed &= Check(PropertiesTotalsComplete(value) && value.files == 2 && value.bytes == 14 && value.items[0].entry.identity == value.items[1].entry.identity,
                "properties_hardlinks_count_selected_names_without_identity_deduplication");
            TabState state; auto navigation = state.Navigate(Location::Folder(parent.wstring())); auto listing = blink::core::EnumerateDirectory(parent.wstring());
            listing.entries = { Entry(file), Entry(alias) }; if (navigation) { (void)state.CommitBrowse(*navigation, std::move(listing)); } (void)state.SelectAllRows();
            ItemPropertiesRequest selected; const auto captured = CaptureItemProperties(state, selected);
            passed &= Check(SUCCEEDED(captured) && selected.entries.size() == 2 && selected.entries[0].name != selected.entries[1].name &&
                selected.entries[0].identity == selected.entries[1].identity && selected.parent_identity == Entry(parent).identity,
                "properties_capture_uses_committed_link_rows_and_original_parent_identity");
            navigation = state.Navigate(Location::Folder(root.wstring())); const auto pending = CaptureItemProperties(state, selected);
            passed &= Check(FAILED(pending) && selected.entries.empty() && selected.parent.empty(), "properties_pending_navigation_clears_capture_output");
        }
        {
            const auto target = parent / L"link-target"; fs::create_directory(target); Write(target / L"sentinel", "do-not-count-target");
            const auto link = parent / L"directory-link"; const bool created = CreateOperationTestJunction(link, target);
            const auto value = ReadItemProperties(Request(parent, { link }));
            passed &= Check(created && PropertiesTotalsComplete(value) && value.links_not_followed == 1 && !value.files && !value.bytes &&
                (value.items[0].entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) && Read(target / L"sentinel") == "do-not-count-target",
                "properties_selected_junction_reads_link_object_without_counting_target");
            const auto tree = parent / L"tree-with-link"; fs::create_directory(tree); Write(tree / L"own.txt", "own");
            const bool nested = CreateOperationTestJunction(tree / L"target", target); const auto totals = ReadItemProperties(Request(parent, { tree }));
            passed &= Check(nested && PropertiesTotalsComplete(totals) && totals.bytes == 3 && totals.files == 1 && totals.folders == 1 && totals.links_not_followed == 1,
                "properties_nested_junction_is_counted_as_folder_but_target_is_not_traversed");
        }
        {
            const auto path = parent / L"sparse.bin";
            blink::core::UniqueHandle handle(CreateFileW(Extended(path).c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW, 0, nullptr));
            DWORD returned{}; const bool sparse = handle && DeviceIoControl(handle.get(), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
            LARGE_INTEGER size{}; size.QuadPart = 16 * 1024 * 1024;
            const bool sized = sparse && SetFilePointerEx(handle.get(), size, nullptr, FILE_BEGIN) && SetEndOfFile(handle.get()); handle.reset();
            DWORD high{}; const auto low = GetCompressedFileSizeW(Extended(path).c_str(), &high); const auto actual = (std::uint64_t(high) << 32) | low;
            const auto value = ReadItemProperties(Request(parent, { path }));
            passed &= Check(sized && PropertiesTotalsComplete(value) && value.bytes == static_cast<std::uint64_t>(size.QuadPart) &&
                value.allocated_bytes == actual && value.allocated_bytes < value.bytes, "properties_sparse_file_uses_physical_compressed_size_not_logical_allocation");
            const auto folder = parent / L"sparse-folder"; fs::create_directory(folder); fs::rename(path, folder / path.filename());
            const auto totals = ReadItemProperties(Request(parent, { folder }));
            passed &= Check(PropertiesTotalsComplete(totals) && totals.bytes == static_cast<std::uint64_t>(size.QuadPart) && totals.allocated_bytes == actual,
                "properties_recursive_sparse_file_allocation_matches_selected_file_read");
        }
        {
            const auto path = parent / L"readonly-hidden.txt"; Write(path, "attributes");
            const bool set = SetFileAttributesW(Extended(path).c_str(), FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN) != FALSE;
            const auto value = ReadItemProperties(Request(parent, { path }));
            passed &= Check(set && PropertiesTotalsComplete(value) && (value.items[0].entry.attributes & FILE_ATTRIBUTE_READONLY) &&
                (value.items[0].entry.attributes & FILE_ATTRIBUTE_HIDDEN) && Read(path) == "attributes", "properties_readonly_hidden_metadata_is_visible_without_changing_flags");
        }
        {
            const auto path = parent / L"replaced.txt"; Write(path, "original"); auto source = Request(parent, { path });
            fs::rename(path, parent / L"saved.txt"); Write(path, "replacement"); const auto value = ReadItemProperties(source);
            passed &= Check(value.finished && value.error == ERROR_FILE_INVALID && !value.read_count && !value.bytes && Read(path) == "replacement",
                "properties_same_path_replacement_is_reported_instead_of_disclosing_new_file_as_old_selection");
            const auto normal = ReadItemProperties(Request(parent, { path, file }));
            passed &= Check(PropertiesTotalsComplete(normal) && normal.read_count == 2 && normal.bytes == 18,
                "properties_new_explicit_selection_can_read_current_replacement");
            source = Request(parent, { path, file }); fs::remove(path); const auto partial = ReadItemProperties(source);
            passed &= Check(partial.finished && !PropertiesTotalsComplete(partial) && partial.unavailable_count == 1 && partial.read_count == 1 && partial.bytes == 7,
                "properties_missing_selected_item_does_not_hide_other_readable_items");
        }
        {
            const auto path = root / L"old-parent"; fs::create_directory(path); Write(path / L"file", "old"); const auto source = Request(path, { path / L"file" });
            fs::rename(path, root / L"parent-saved"); fs::create_directory(path); Write(path / L"file", "new"); const auto value = ReadItemProperties(source);
            passed &= Check(value.finished && value.error == ERROR_FILE_INVALID && !value.read_count && Read(path / L"file") == "new",
                "properties_replaced_parent_does_not_rebind_captured_selection");
        }
        {
            auto path = root / L"long"; for (unsigned index = 0; index < 9; ++index) { path /= L"long-properties-folder-segment-012345"; }
            fs::create_directories(Extended(path)); const auto source = path / L"中文.txt"; Write(source, "long");
            const auto value = ReadItemProperties(Request(path, { source }));
            passed &= Check(source.wstring().size() > MAX_PATH && PropertiesTotalsComplete(value) && value.bytes == 4,
                "properties_long_unicode_file_uses_extended_metadata_path");
        }
        {
            auto cancelled = std::make_shared<std::atomic_bool>(true); const auto before = ReadItemProperties(request, cancelled);
            passed &= Check(before.finished && before.cancelled && before.error == ERROR_CANCELLED && !before.read_count,
                "properties_pre_cancel_finishes_without_reading_selected_metadata");
            cancelled = std::make_shared<std::atomic_bool>(); const auto initial = ReadItemProperties(request, cancelled, [cancelled](const auto&) { cancelled->store(true); });
            passed &= Check(initial.cancelled && !initial.read_count, "properties_initial_progress_cancel_prevents_parent_and_item_reads");
            const auto exception = ReadItemProperties(request, {}, [](const auto&) { throw 1; });
            passed &= Check(exception.finished && exception.error == ERROR_UNHANDLED_EXCEPTION && !exception.read_count,
                "properties_observer_exception_is_contained_and_reported");
            std::vector<bool> publications; const auto normal = ReadItemProperties(request, {}, [&](const auto& value) { publications.push_back(value.finished); });
            passed &= Check(PropertiesTotalsComplete(normal) && publications.size() >= 3 && !publications.front() && publications.back() &&
                std::count(publications.begin(), publications.end(), true) == 1, "properties_progress_has_one_final_snapshot_after_initial_and_metadata");
        }
        {
            auto invalid = request; invalid.entries[0].identity.valid = false; const auto value = ReadItemProperties(invalid);
            passed &= Check(value.finished && value.error == ERROR_FILE_INVALID && !value.read_count, "properties_invalid_original_identity_is_not_silently_recaptured");
            auto mixed = request; mixed.entries.push_back(mixed.entries.front()); mixed.entries.back().name = L"unbound"; mixed.entries.back().identity.valid = false;
            const auto partial = ReadItemProperties(mixed);
            passed &= Check(partial.finished && partial.error == ERROR_FILE_INVALID && partial.read_count == 1 && partial.unavailable_count == 1 && partial.bytes == 7 &&
                !PropertiesTotalsComplete(partial), "properties_unbound_selected_item_keeps_other_valid_metadata_and_totals");
            const auto caller = GetCurrentThreadId(); DWORD worker{};
            auto future = std::async(std::launch::async, [&] { worker = GetCurrentThreadId(); return ReadItemProperties(request); });
            const auto background = future.get();
            passed &= Check(PropertiesTotalsComplete(background) && worker != caller && background.bytes == 7,
                "properties_native_reader_can_finish_on_background_thread_without_ui_or_com");
        }
        {
            const Localization localization; const auto lines = BuildPropertiesLines(request, result, localization);
            const auto get = [&](const wchar_t* key) { const auto label = localization.GetString(key); for (const auto& line : lines) { if (line.first == label) { return line.second; } } return std::wstring{}; };
            passed &= Check(get(L"Properties.Name") == file.filename().wstring() && get(L"Properties.Location") == parent.wstring() &&
                get(L"Properties.Size").find(L"999") == std::wstring::npos && get(L"Properties.Size").find(L"7") != std::wstring::npos &&
                get(L"Properties.Type") != localization.GetString(L"Properties.Unavailable"), "properties_presentation_shows_original_name_and_actual_size_type_location");
            passed &= Check(PropertiesAttributes(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN, localization).find(localization.GetString(L"Properties.ReadOnly")) != std::wstring::npos &&
                PropertiesAttributes(FILE_ATTRIBUTE_NORMAL, localization) == localization.GetString(L"Properties.None"),
                "properties_attribute_text_is_localized_and_normal_is_not_an_error");
        }
        {
            const auto path = parent / L"timestamp.txt"; Write(path, "time"); const auto other = parent / L"different.bin"; Write(other, "different");
            SYSTEMTIME utc{}; utc.wYear = 2026; utc.wMonth = 11; utc.wDay = 5; utc.wHour = 12; utc.wMinute = 34; utc.wSecond = 56;
            FILETIME time{}; const bool converted = SystemTimeToFileTime(&utc, &time) != FALSE;
            blink::core::UniqueHandle handle(CreateFileW(Extended(path).c_str(), FILE_WRITE_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
            const bool changed = converted && handle && SetFileTime(handle.get(), nullptr, nullptr, &time); handle.reset();
            const Localization localization; const auto source = Request(parent, { path }); const auto value = ReadItemProperties(source);
            const auto get = [&](const std::vector<PropertiesLine>& lines, const wchar_t* key) {
                const auto label = localization.GetString(key); for (const auto& line : lines) { if (line.first == label) { return line.second; } } return std::wstring{};
            };
            const auto fields = BuildPropertiesLines(source, value, localization);
            passed &= Check(changed && PropertiesTotalsComplete(value) && CompareFileTime(&value.items[0].entry.last_write_time, &time) == 0 &&
                get(fields, L"Properties.Modified").ends_with(L":56") && FormatPropertiesTime({}, localization) == localization.GetString(L"Common.NotAvailable"),
                "properties_actual_timestamp_keeps_seconds_and_unknown_time_does_not_show_1601");
            const auto selection = Request(parent, { path, other }); const auto mixed = ReadItemProperties(selection);
            const auto multi = BuildPropertiesLines(selection, mixed, localization);
            passed &= Check(PropertiesTotalsComplete(mixed) && get(multi, L"Properties.Type") == localization.GetString(L"Properties.MultipleTypes") &&
                get(multi, L"Properties.Modified") == localization.GetString(L"Properties.MultipleValues"),
                "properties_multi_selection_distinct_types_and_times_do_not_collapse_to_first_item");
        }
    }
    catch (const std::exception& error)
    { std::cout << "INFO properties_fixture_exception " << error.what() << '\n'; passed &= Check(false, "properties_owned_fixture_unexpected_failure"); }
    {
        TabState state; ItemPropertiesRequest request; request.parent = L"old"; request.entries.resize(1);
        passed &= Check(FAILED(CaptureItemProperties(state, request)) && request.parent.empty() && request.entries.empty(),
            "properties_this_pc_or_empty_selection_clears_output");
        FolderCommandCapabilities capabilities{ true, false, 2, true, true, true };
        passed &= Check(EvaluateFolderCommand(FolderCommand::Properties, capabilities) == FolderCommandStatus::Ready &&
            EvaluateFolderCommand(FolderCommand::Delete, capabilities) == FolderCommandStatus::Busy,
            "properties_multi_selection_read_is_available_while_mutation_queue_is_busy");
        capabilities.busy = true;
        passed &= Check(EvaluateFolderCommand(FolderCommand::Properties, capabilities) == FolderCommandStatus::Busy,
            "properties_command_respects_pending_browse_busy_state");
    }
    return passed;
}
