#include "FileNameSelectionTests.h"
#include "Blink/App/FileNameSelection.h"

#include <iostream>
#include <string>
#include <string_view>

namespace
{
    using blink::app::BuildFileNameSelection;
    using blink::app::FileNameSelectionMode;

    bool Check(const bool value, const char* name)
    {
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
        return value;
    }

    bool SelectionIs(const std::wstring_view name, const FileNameSelectionMode mode,
        const std::wstring_view selected, const std::wstring_view suffix = {})
    {
        const auto range = BuildFileNameSelection(name, mode);
        if (range.start > name.size() || range.length > name.size() - range.start)
        {
            return false;
        }
        return range.start == 0 &&
            name.substr(range.start, range.length) == selected &&
            name.substr(range.start + range.length) == suffix;
    }
}

bool RunFileNameSelectionTests()
{
    bool passed = true;
    constexpr auto stem = FileNameSelectionMode::FileStem;
    constexpr auto whole = FileNameSelectionMode::WholeName;

    passed &= Check(SelectionIs(L"report.txt", stem, L"report", L".txt"),
        "filename_selection_file_stem_keeps_extension_unselected");
    passed &= Check(SelectionIs(L"archive.tar.gz", stem, L"archive.tar", L".gz"),
        "filename_selection_multisuffix_only_preserves_last_extension");
    passed &= Check(SelectionIs(L"README", stem, L"README"),
        "filename_selection_extensionless_name_selects_everything");
    passed &= Check(SelectionIs(L".gitignore", stem, L".gitignore"),
        "filename_selection_leading_only_dotfile_selects_everything");
    passed &= Check(SelectionIs(L".config.json", stem, L".config", L".json"),
        "filename_selection_dotfile_with_extension_keeps_its_stem");
    passed &= Check(SelectionIs(L"report.", stem, L"report."),
        "filename_selection_trailing_dot_is_not_an_empty_extension");
    passed &= Check(SelectionIs(L"", stem, L""),
        "filename_selection_empty_input_has_empty_range");
    passed &= Check(SelectionIs(L".", stem, L".") && SelectionIs(L"..", stem, L".."),
        "filename_selection_dot_only_input_is_not_reinterpreted_as_a_suffix");
    passed &= Check(SelectionIs(L"资料.v2", whole, L"资料.v2") &&
        SelectionIs(L"folder.tar.gz", whole, L"folder.tar.gz"),
        "filename_selection_directory_mode_keeps_dotted_names_whole");
    passed &= Check(SelectionIs(L"draft.v2.txt", whole, L"draft.v2.txt"),
        "filename_selection_general_name_dialog_mode_keeps_full_preset");
    passed &= Check(SelectionIs(L"项目 报告.Final.TXT", stem, L"项目 报告.Final", L".TXT"),
        "filename_selection_unicode_spaces_and_extension_case_are_preserved");

    // Explicit UTF-16 pair makes the TextBox offset contract visible: the
    // rocket is two code units and selection must include both of them.
    constexpr std::wstring_view unicode_stem = L"报告-\xD83D\xDE80";
    constexpr std::wstring_view unicode_name = L"报告-\xD83D\xDE80.data";
    const auto unicode_range = BuildFileNameSelection(unicode_name, stem);
    passed &= Check(unicode_range.start == 0 && unicode_range.length == 5 &&
        SelectionIs(unicode_name, stem, unicode_stem, L".data"),
        "filename_selection_utf16_pair_is_not_split_before_extension");

    const std::wstring backing = L"prefix:report.txt.not-part-of-name";
    const std::wstring_view bounded(backing.data() + 7, 10);
    passed &= Check(SelectionIs(bounded, stem, L"report", L".txt") &&
        backing == L"prefix:report.txt.not-part-of-name",
        "filename_selection_respects_view_boundary_and_leaves_original_text_unchanged");
    return passed;
}
