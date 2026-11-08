#include "DirectoryCopyOperationTests.h"
#include "Blink/App/DirectoryCopyOperation.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

bool CreateOperationTestJunction(const std::filesystem::path&, const std::filesystem::path&);

namespace
{
    bool Check(bool condition, const char* name)
    { std::cout << (condition ? "PASS " : "FAIL ") << name << '\n'; return condition; }

    std::filesystem::path Extended(const std::filesystem::path& path)
    {
        const auto value = path.wstring();
        return value.starts_with(L"\\\\?\\") ? value : L"\\\\?\\" + value;
    }

    blink::core::FileEntry Entry(const std::filesystem::path& path)
    {
        blink::core::FileEntry result;
        if (!blink::core::ReadEntryMetadata(path.wstring(), result))
        { throw std::runtime_error("cannot read owned directory-copy fixture metadata"); }
        result.name = path.filename().wstring(); result.full_path = path.wstring();
        return result;
    }

    void Write(const std::filesystem::path& path, const std::string& value)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(value.data(), static_cast<std::streamsize>(value.size()));
        if (!file) { throw std::runtime_error("cannot write owned directory-copy fixture"); }
    }

    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    }

    bool Exists(const std::filesystem::path& path)
    { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

    std::pair<std::uint64_t, std::uint64_t> CopiedObjects(const std::filesystem::path& root)
    {
        if (!Exists(root)) { return {}; }
        std::uint64_t files{}, directories{ 1 };
        for (const auto& child : std::filesystem::recursive_directory_iterator(root))
        {
            const auto attributes = GetFileAttributesW(child.path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            { throw std::runtime_error("unexpected reparse or unreadable entry in owned copy result"); }
            if (attributes & FILE_ATTRIBUTE_DIRECTORY) { ++directories; } else { ++files; }
        }
        return { files, directories };
    }

    struct FileOracle
    {
        std::filesystem::path path;
        blink::core::FileIdentity identity;
        std::string contents;
        bool Matches() const
        { return Exists(path) && Entry(path).identity == identity && Read(path) == contents; }
    };

    FileOracle Oracle(const std::filesystem::path& path)
    { return { path, Entry(path).identity, Read(path) }; }

    blink::core::UniqueHandle Open(const std::filesystem::path& path, DWORD access,
        DWORD share, bool directory = false)
    {
        CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
        parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0);
        return blink::core::UniqueHandle(CreateFile3(path.c_str(), access, share, OPEN_EXISTING, &parameters));
    }

    bool EmptyStreamExists(const std::filesystem::path& path)
    {
        auto stream = Open(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
        LARGE_INTEGER size{};
        return stream && GetFileSizeEx(stream.get(), &size) && size.QuadPart == 0;
    }

    bool RenameBlocked(const std::filesystem::path& original, const std::filesystem::path& alternate)
    {
        const bool moved = MoveFileExW(original.c_str(), alternate.c_str(), 0) != FALSE;
        const auto error = GetLastError();
        if (moved && !MoveFileExW(alternate.c_str(), original.c_str(), 0))
        { throw std::runtime_error("cannot restore unexpectedly movable owned directory fixture"); }
        return !moved && (error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED);
    }

    bool RenameRoundTrip(const std::filesystem::path& original, const std::filesystem::path& alternate)
    {
        if (!MoveFileExW(original.c_str(), alternate.c_str(), 0)) { return false; }
        if (!MoveFileExW(alternate.c_str(), original.c_str(), 0))
        { throw std::runtime_error("cannot restore released owned directory fixture"); }
        return true;
    }

    bool SetTimes(const std::filesystem::path& path, ULONGLONG creation, ULONGLONG written)
    {
        auto handle = Open(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, true);
        const FILETIME creation_time{ static_cast<DWORD>(creation), static_cast<DWORD>(creation >> 32) };
        const FILETIME write_time{ static_cast<DWORD>(written), static_cast<DWORD>(written >> 32) };
        return handle && SetFileTime(handle.get(), &creation_time, nullptr, &write_time);
    }

    void CleanupOwnedTree(const std::filesystem::path& root) noexcept
    {
        WIN32_FIND_DATAW entry{};
        const auto found = FindFirstFileW((root / L"*").c_str(), &entry);
        if (found == INVALID_HANDLE_VALUE) { return; }
        do
        {
            if (!std::wcscmp(entry.cFileName, L".") || !std::wcscmp(entry.cFileName, L"..")) { continue; }
            const auto path = root / entry.cFileName;
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { CleanupOwnedTree(path); }
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                { (void)SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); }
                (void)RemoveDirectoryW(path.c_str());
            }
            else
            {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                { (void)SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); }
                (void)DeleteFileW(path.c_str());
            }
        } while (FindNextFileW(found, &entry));
        FindClose(found);
    }

    struct Fixture
    {
        std::filesystem::path root, source_parent, source, destination_parent, destination;
        std::vector<FileOracle> source_files;
        explicit Fixture(const std::filesystem::path& suite, const wchar_t* label, bool populate = true)
            : root(suite / label), source_parent(root / L"源父目录"), source(source_parent / L"源目录"),
            destination_parent(root / L"目标父目录"), destination(destination_parent / L"复制目录")
        {
            std::filesystem::create_directories(source);
            std::filesystem::create_directories(destination_parent);
            if (!populate) { return; }
            std::filesystem::create_directories(source / L"下一级" / L"空目录");
            std::filesystem::create_directory(source / L"独立空目录");
            constexpr char binary[] = "owned-root\0binary\xff";
            Write(source / L"根文件 {0}.bin", std::string(binary, sizeof(binary) - 1));
            Write(source / L"下一级" / L"资料.txt", "owned-nested-contents");
            Write((source / L"根文件 {0}.bin").wstring() + L":Zone.Identifier:$DATA", "[ZoneTransfer]\r\nZoneId=3\r\n");
            Write((source / L"根文件 {0}.bin").wstring() + L":资料 {1}:$DATA", "owned-unicode-stream");
            Write((source / L"根文件 {0}.bin").wstring() + L":empty:$DATA", {});
            source_files.push_back(Oracle(source / L"根文件 {0}.bin"));
            source_files.push_back(Oracle(source / L"下一级" / L"资料.txt"));
        }
        bool SourcesPreserved() const
        {
            return std::all_of(source_files.begin(), source_files.end(), [](const auto& oracle) { return oracle.Matches(); }) &&
                (source_files.empty() ||
                    (Read((source / L"根文件 {0}.bin").wstring() + L":Zone.Identifier:$DATA") == "[ZoneTransfer]\r\nZoneId=3\r\n" &&
                     Read((source / L"根文件 {0}.bin").wstring() + L":资料 {1}:$DATA") == "owned-unicode-stream" &&
                     EmptyStreamExists((source / L"根文件 {0}.bin").wstring() + L":empty:$DATA")));
        }
        bool CompleteDestination() const
        {
            return std::filesystem::is_directory(destination / L"下一级" / L"空目录") &&
                std::filesystem::is_directory(destination / L"独立空目录") &&
                Read(destination / L"根文件 {0}.bin") == source_files[0].contents &&
                Read(destination / L"下一级" / L"资料.txt") == source_files[1].contents &&
                Read((destination / L"根文件 {0}.bin").wstring() + L":Zone.Identifier:$DATA") == "[ZoneTransfer]\r\nZoneId=3\r\n" &&
                Read((destination / L"根文件 {0}.bin").wstring() + L":资料 {1}:$DATA") == "owned-unicode-stream" &&
                EmptyStreamExists((destination / L"根文件 {0}.bin").wstring() + L":empty:$DATA");
        }
    };

    blink::app::DirectoryCopyRequest Request(const Fixture& fixture)
    {
        return { fixture.source_parent.wstring(), Entry(fixture.source_parent).identity, Entry(fixture.source),
            fixture.destination_parent.wstring(), Entry(fixture.destination_parent).identity, fixture.destination.filename().wstring() };
    }

    void Diagnostic(const blink::app::DirectoryCopyResult& result, const char* name)
    {
        std::cout << "INFO directory_copy_result case=" << name << " error=" << result.error <<
            " copied=" << result.copied << " partial=" << result.partial_tree_left <<
            " files=" << result.files_copied << " directories=" << result.directories_created <<
            " path_chars=" << result.path.size() << " failure_path_chars=" << result.failure_path.size() << '\n';
    }
}

bool RunDirectoryCopyOperationTests()
{
    using namespace blink::app;
    bool passed = true;
    const auto root = std::filesystem::absolute(std::filesystem::path(L".build") / L"directory-copy-operation-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    std::filesystem::create_directories(root);
    struct Cleanup
    {
        std::filesystem::path root;
        ~Cleanup() { const auto extended = Extended(root); CleanupOwnedTree(extended); (void)RemoveDirectoryW(extended.c_str()); }
    } cleanup{ root };
    try
    {
        {
            Fixture fixture(root, L"ordinary-tree");
            const auto source_identity = Entry(fixture.source).identity;
            std::uint64_t previous_files{}, previous_directories{};
            unsigned completed_publications{};
            bool progress_valid = true;
            const auto result = CopyDirectorySafely(Request(fixture), {}, [&](const DirectoryCopyProgress& progress) {
                progress_valid &= progress.files_copied >= previous_files && progress.directories_created >= previous_directories &&
                    progress.current_file.transferred_bytes <= progress.current_file.total_bytes;
                previous_files = progress.files_copied; previous_directories = progress.directories_created;
                if (progress.completed) { ++completed_publications; }
            });
            Diagnostic(result, "ordinary-tree");
            passed &= Check(result.copied && result.error == ERROR_SUCCESS && !result.partial_tree_left &&
                result.files_copied == 2 && result.directories_created == 4 && fixture.CompleteDestination() &&
                Entry(fixture.source).identity == source_identity && Entry(fixture.destination).identity != source_identity &&
                Entry(result.path).identity == Entry(fixture.destination).identity && fixture.SourcesPreserved(),
                "directory_copy_nested_empty_unicode_binary_and_all_file_ads_match_without_changing_source_objects");
            passed &= Check(progress_valid && completed_publications == 1 && previous_files == 2 && previous_directories == 4,
                "directory_copy_progress_counts_are_monotonic_and_completed_is_published_once_after_actual_tree_success");
        }
        {
            Fixture fixture(root, L"bounded-empty-root", false);
            auto request = Request(fixture); request.limits = { 0, 1, 0 };
            const auto result = CopyDirectorySafely(request);
            Diagnostic(result, "bounded-empty-root");
            passed &= Check(result.copied && result.error == ERROR_SUCCESS && !result.partial_tree_left &&
                result.files_copied == 0 && result.directories_created == 1 &&
                std::filesystem::is_empty(fixture.destination) && std::filesystem::is_empty(fixture.source),
                "directory_copy_empty_root_succeeds_at_zero_descendants_zero_depth_and_one_directory_limit");
        }
        {
            Fixture fixture(root, L"hardlink-file-names", false);
            const auto first = fixture.source / L"第一名称.txt"; const auto second = fixture.source / L"第二名称.txt";
            Write(first, "owned-hardlink-data");
            const bool created = CreateHardLinkW(second.c_str(), first.c_str(), nullptr) != FALSE;
            passed &= Check(created, "directory_copy_owned_hardlink_fixture_created");
            if (created)
            {
                const auto oracle = Oracle(first);
                const auto result = CopyDirectorySafely(Request(fixture));
                passed &= Check(result.copied && result.error == ERROR_SUCCESS && !result.partial_tree_left && result.files_copied == 2 &&
                    Read(fixture.destination / first.filename()) == "owned-hardlink-data" &&
                    Read(fixture.destination / second.filename()) == "owned-hardlink-data" &&
                    Entry(fixture.destination / first.filename()).identity != Entry(fixture.destination / second.filename()).identity &&
                    Entry(second).identity == oracle.identity && oracle.Matches(),
                    "directory_copy_distinct_hardlink_names_are_both_copied_without_deduplicating_or_mutating_source_object");
            }
        }
        {
            Fixture fixture(root, L"directory-metadata");
            constexpr ULONGLONG Creation = 132224078450000000ULL;
            constexpr ULONGLONG Written = Creation + 987654321ULL;
            const auto nested = fixture.source / L"下一级";
            const bool times_set = SetTimes(fixture.source, Creation, Written) && SetTimes(nested, Creation + 10, Written + 20);
            constexpr DWORD Attributes = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE;
            const bool attributes_set = SetFileAttributesW(fixture.source.c_str(), Attributes) && SetFileAttributesW(nested.c_str(), Attributes);
            const auto root_metadata = Entry(fixture.source); const auto nested_metadata = Entry(nested);
            const auto result = CopyDirectorySafely(Request(fixture));
            Diagnostic(result, "directory-metadata");
            bool matches = false;
            if (result.copied)
            {
                const auto root_copy = Entry(fixture.destination); const auto nested_copy = Entry(fixture.destination / L"下一级");
                matches = (root_copy.attributes & Attributes) == Attributes && (nested_copy.attributes & Attributes) == Attributes &&
                    CompareFileTime(&root_copy.creation_time, &root_metadata.creation_time) == 0 &&
                    CompareFileTime(&root_copy.last_write_time, &root_metadata.last_write_time) == 0 &&
                    CompareFileTime(&nested_copy.creation_time, &nested_metadata.creation_time) == 0 &&
                    CompareFileTime(&nested_copy.last_write_time, &nested_metadata.last_write_time) == 0;
            }
            passed &= Check(times_set && attributes_set && result.copied && result.error == ERROR_SUCCESS && matches &&
                fixture.CompleteDestination() && fixture.SourcesPreserved(),
                "directory_copy_commits_supported_directory_attributes_and_creation_write_times_after_children");
        }
        {
            Fixture fixture(root, L"existing-file-target");
            Write(fixture.destination, "owned-existing-file");
            const auto before = Oracle(fixture.destination);
            const auto result = CopyDirectorySafely(Request(fixture));
            Diagnostic(result, "existing-file-target");
            passed &= Check(!result.copied && result.error != ERROR_SUCCESS && !result.partial_tree_left &&
                result.directories_created == 0 && result.files_copied == 0 && before.Matches() && fixture.SourcesPreserved(),
                "directory_copy_existing_regular_target_is_never_overwritten_or_deleted");
        }
        {
            Fixture fixture(root, L"existing-directory-target");
            std::filesystem::create_directory(fixture.destination);
            Write(fixture.destination / L"保留.txt", "owned-existing-child");
            const auto directory_identity = Entry(fixture.destination).identity;
            const auto before = Oracle(fixture.destination / L"保留.txt");
            const auto result = CopyDirectorySafely(Request(fixture));
            Diagnostic(result, "existing-directory-target");
            passed &= Check(!result.copied && result.error != ERROR_SUCCESS && !result.partial_tree_left &&
                result.directories_created == 0 && result.files_copied == 0 && Entry(fixture.destination).identity == directory_identity &&
                before.Matches() && !Exists(fixture.destination / L"根文件 {0}.bin") && fixture.SourcesPreserved(),
                "directory_copy_existing_directory_is_not_merged_and_its_identity_and_children_are_preserved");
        }
        {
            Fixture fixture(root, L"rival-child-file-after-root-create");
            const auto rival_path = fixture.destination / L"根文件 {0}.bin";
            std::optional<FileOracle> rival;
            bool attempted{};
            const auto result = CopyDirectorySafely(Request(fixture), {}, [&](const DirectoryCopyProgress& progress) {
                if (attempted || !progress.directories_created || progress.completed) { return; }
                attempted = true;
                Write(rival_path, "owned-rival-created-after-root"); rival = Oracle(rival_path);
            });
            Diagnostic(result, "rival-child-file-after-root-create");
            passed &= Check(attempted && rival && rival->Matches() && !result.copied && result.error != ERROR_SUCCESS &&
                result.partial_tree_left && result.directories_created >= 1 && Exists(fixture.destination) && fixture.SourcesPreserved(),
                "directory_copy_child_file_inserted_after_root_claim_is_never_overwritten_or_removed_on_failure");
        }
        {
            Fixture fixture(root, L"rival-child-junction-after-root-create");
            const auto target = fixture.root / L"自有竞态联接目标";
            std::filesystem::create_directory(target); Write(target / L"保留.txt", "owned-rival-junction-sentinel");
            const auto sentinel = Oracle(target / L"保留.txt");
            const auto rival_path = fixture.destination / L"下一级";
            blink::core::FileIdentity rival_identity;
            bool attempted{}, inserted{};
            const auto result = CopyDirectorySafely(Request(fixture), {}, [&](const DirectoryCopyProgress& progress) {
                if (attempted || !progress.directories_created || progress.completed) { return; }
                attempted = true; inserted = CreateOperationTestJunction(rival_path, target);
                if (inserted) { rival_identity = Entry(rival_path).identity; }
            });
            Diagnostic(result, "rival-child-junction-after-root-create");
            passed &= Check(attempted && inserted && Exists(rival_path) &&
                (GetFileAttributesW(rival_path.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) &&
                Entry(rival_path).identity == rival_identity && sentinel.Matches() &&
                !Exists(target / L"资料.txt") && !Exists(target / L"空目录") &&
                !result.copied && result.error != ERROR_SUCCESS && result.partial_tree_left &&
                result.directories_created >= 1 && fixture.SourcesPreserved(),
                "directory_copy_child_junction_inserted_after_root_claim_is_preserved_without_following_or_touching_owned_target");
        }
        for (const bool descendant : { false, true })
        {
            Fixture fixture(root, descendant ? L"reject-descendant" : L"reject-self");
            auto request = Request(fixture);
            request.destination_parent = descendant ? (fixture.source / L"下一级").wstring() : fixture.source_parent.wstring();
            request.destination_parent_identity = Entry(request.destination_parent).identity;
            request.destination_name = descendant ? L"must-not-create" : fixture.source.filename().wstring();
            const auto result = CopyDirectorySafely(request);
            Diagnostic(result, descendant ? "reject-descendant" : "reject-self");
            passed &= Check(!result.copied && result.error == ERROR_INVALID_PARAMETER && !result.partial_tree_left &&
                result.directories_created == 0 && result.files_copied == 0 &&
                (!descendant || !Exists(fixture.source / L"下一级" / L"must-not-create")) && fixture.SourcesPreserved(),
                descendant ? "directory_copy_destination_inside_source_is_rejected_before_any_write" :
                "directory_copy_source_itself_is_rejected_before_any_write");
        }
        for (unsigned identity_kind = 0; identity_kind < 3; ++identity_kind)
        {
            const auto label = L"wrong-identity-" + std::to_wstring(identity_kind);
            Fixture fixture(root, label.c_str()); auto request = Request(fixture);
            if (identity_kind == 0) { request.source.identity.file_id[0] ^= 1; }
            if (identity_kind == 1) { request.source_parent_identity.file_id[0] ^= 1; }
            if (identity_kind == 2) { request.destination_parent_identity.file_id[0] ^= 1; }
            const auto result = CopyDirectorySafely(request);
            Diagnostic(result, "wrong-identity");
            passed &= Check(!result.copied && result.error == ERROR_FILE_INVALID && !result.partial_tree_left &&
                result.directories_created == 0 && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                identity_kind == 0 ? "directory_copy_wrong_source_identity_never_creates_destination" :
                identity_kind == 1 ? "directory_copy_wrong_source_parent_identity_never_creates_destination" :
                "directory_copy_wrong_destination_parent_identity_never_creates_destination");
        }
        {
            Fixture fixture(root, L"wrong-source-id-matches-destination-ancestor");
            auto request = Request(fixture);
            request.source.identity = request.destination_parent_identity;
            const auto result = CopyDirectorySafely(request);
            Diagnostic(result, "wrong-source-id-matches-destination-ancestor");
            passed &= Check(request.source.identity != Entry(fixture.source).identity &&
                !result.copied && result.error == ERROR_FILE_INVALID && !result.partial_tree_left &&
                result.directories_created == 0 && result.files_copied == 0 &&
                !Exists(fixture.destination) && fixture.SourcesPreserved(),
                "directory_copy_source_identity_is_verified_before_its_untrusted_value_is_used_in_ancestor_checks");
        }
        {
            Fixture fixture(root, L"replaced-source-root"); const auto request = Request(fixture);
            const auto original = fixture.source_parent / L"原源目录保留";
            std::filesystem::rename(fixture.source, original);
            std::filesystem::create_directory(fixture.source);
            Write(fixture.source / L"替换者.txt", "owned-replacement");
            const auto replacement = Oracle(fixture.source / L"替换者.txt");
            const auto result = CopyDirectorySafely(request);
            Diagnostic(result, "replaced-source-root");
            passed &= Check(!result.copied && result.error == ERROR_FILE_INVALID && !result.partial_tree_left &&
                !Exists(fixture.destination) && Entry(original).identity == request.source.identity && replacement.Matches() &&
                Read(original / L"根文件 {0}.bin") == fixture.source_files[0].contents &&
                Read(original / L"下一级" / L"资料.txt") == fixture.source_files[1].contents,
                "directory_copy_replaced_source_root_is_rejected_and_both_owned_source_objects_survive");
        }
        {
            Fixture fixture(root, L"replaced-destination-parent"); const auto request = Request(fixture);
            const auto original = fixture.root / L"原目标父目录保留";
            std::filesystem::rename(fixture.destination_parent, original);
            std::filesystem::create_directory(fixture.destination_parent);
            const auto replacement_identity = Entry(fixture.destination_parent).identity;
            const auto result = CopyDirectorySafely(request);
            Diagnostic(result, "replaced-destination-parent");
            passed &= Check(!result.copied && result.error == ERROR_FILE_INVALID && !result.partial_tree_left &&
                !Exists(fixture.destination) && !Exists(original / fixture.destination.filename()) &&
                Entry(fixture.destination_parent).identity == replacement_identity &&
                Entry(original).identity == request.destination_parent_identity && fixture.SourcesPreserved(),
                "directory_copy_replaced_destination_parent_is_rejected_without_writing_either_parent");
        }
        for (unsigned kind = 0; kind < 5; ++kind)
        {
            const auto label = L"planning-bound-" + std::to_wstring(kind);
            Fixture fixture(root, label.c_str()); auto request = Request(fixture);
            if (kind == 0) { request.limits.max_entries = 1; }
            if (kind == 1) { request.limits.max_directories = 1; }
            if (kind == 2) { request.limits.max_depth = 1; }
            if (kind == 3) { request.limits.max_entries = MaximumDirectoryCopyEntries + 1; }
            if (kind == 4) { request.limits.max_path_bytes = 0; }
            const auto result = CopyDirectorySafely(request);
            Diagnostic(result, "planning-bound");
            passed &= Check(!result.copied && result.error == (kind == 3 ? ERROR_INVALID_PARAMETER : ERROR_NOT_SUPPORTED) &&
                !result.partial_tree_left && result.directories_created == 0 && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                kind == 0 ? "directory_copy_entry_bound_rejects_whole_plan_before_creating_root" :
                kind == 1 ? "directory_copy_directory_bound_rejects_whole_plan_before_creating_root" :
                kind == 2 ? "directory_copy_depth_bound_rejects_whole_plan_before_creating_root" :
                kind == 3 ? "directory_copy_limits_cannot_raise_the_hard_safety_cap" :
                "directory_copy_path_memory_budget_rejects_plan_before_creating_root");
        }
        {
            Fixture fixture(root, L"pre-cancel");
            const auto result = CopyDirectorySafely(Request(fixture), std::make_shared<std::atomic_bool>(true));
            passed &= Check(!result.copied && result.error == ERROR_CANCELLED && !result.partial_tree_left &&
                result.directories_created == 0 && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                "directory_copy_precancelled_request_creates_nothing_and_preserves_source_tree");
        }
        {
            Fixture fixture(root, L"cancel-initial-observer"); auto cancellation = std::make_shared<std::atomic_bool>(false);
            bool initial{};
            const auto result = CopyDirectorySafely(Request(fixture), cancellation, [&](const DirectoryCopyProgress& progress) {
                if (!initial && progress.files_copied == 0 && progress.directories_created == 0 && !progress.completed)
                { initial = !Exists(fixture.destination); cancellation->store(true); }
            });
            passed &= Check(initial && !result.copied && result.error == ERROR_CANCELLED && !result.partial_tree_left &&
                result.directories_created == 0 && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                "directory_copy_initial_progress_is_after_preflight_but_before_any_destination_side_effect");
        }
        {
            Fixture fixture(root, L"cancel-after-root-create"); auto cancellation = std::make_shared<std::atomic_bool>(false);
            bool root_observed{};
            const auto result = CopyDirectorySafely(Request(fixture), cancellation, [&](const DirectoryCopyProgress& progress) {
                if (!root_observed && progress.directories_created == 1 && progress.files_copied == 0 && !progress.completed)
                { root_observed = Exists(fixture.destination); cancellation->store(true); }
            });
            Diagnostic(result, "cancel-after-root-create");
            passed &= Check(root_observed && !result.copied && result.error == ERROR_CANCELLED && result.partial_tree_left &&
                result.directories_created == 1 && result.files_copied == 0 && std::filesystem::is_empty(fixture.destination) &&
                fixture.SourcesPreserved(),
                "directory_copy_cancellation_after_first_create_retains_even_empty_owned_root_and_reports_exact_partial_counts");
        }
        {
            Fixture fixture(root, L"cancel-after-file-commit"); auto cancellation = std::make_shared<std::atomic_bool>(false);
            std::wstring completed_file; bool final{};
            const auto result = CopyDirectorySafely(Request(fixture), cancellation, [&](const DirectoryCopyProgress& progress) {
                final |= progress.completed;
                if (progress.files_copied >= 1 && !progress.completed && completed_file.empty())
                { completed_file = progress.current_destination_path; cancellation->store(true); }
            });
            Diagnostic(result, "cancel-after-file-commit");
            const auto actual = CopiedObjects(fixture.destination);
            passed &= Check(!completed_file.empty() && !final && !result.copied && result.error == ERROR_CANCELLED &&
                result.partial_tree_left && result.directories_created >= 1 && result.files_copied == 1 &&
                result.files_copied == actual.first && result.directories_created == actual.second &&
                Exists(fixture.destination) && Exists(completed_file) &&
                (Read(completed_file) == fixture.source_files[0].contents || Read(completed_file) == fixture.source_files[1].contents) &&
                fixture.SourcesPreserved(),
                "directory_copy_cancel_after_first_file_keeps_actual_committed_file_and_reports_owned_partial_tree");
        }
        {
            Fixture fixture(root, L"cancel-inside-current-file", false);
            Write(fixture.source / L"先完成.txt", "owned-small-file");
            const std::string large(3 * 1024 * 1024 + 23, 'd');
            Write(fixture.source / L"大文件.bin", large);
            Write((fixture.source / L"大文件.bin").wstring() + L":Zone.Identifier:$DATA", "[ZoneTransfer]\r\nZoneId=3\r\n");
            const auto small_oracle = Oracle(fixture.source / L"先完成.txt"); const auto large_oracle = Oracle(fixture.source / L"大文件.bin");
            auto cancellation = std::make_shared<std::atomic_bool>(false); std::wstring interrupted;
            const auto result = CopyDirectorySafely(Request(fixture), cancellation, [&](const DirectoryCopyProgress& progress) {
                if (progress.current_file.transferred_bytes > 0 && progress.current_file.transferred_bytes < progress.current_file.total_bytes)
                { interrupted = progress.current_destination_path; cancellation->store(true); }
            });
            Diagnostic(result, "cancel-inside-current-file");
            const auto actual = CopiedObjects(fixture.destination);
            passed &= Check(!interrupted.empty() && !result.copied && result.error == ERROR_CANCELLED && result.partial_tree_left &&
                result.directories_created >= 1 && Exists(fixture.destination) && !Exists(interrupted) &&
                result.files_copied == actual.first && result.directories_created == actual.second &&
                small_oracle.Matches() && large_oracle.Matches() &&
                Read((fixture.source / L"大文件.bin").wstring() + L":Zone.Identifier:$DATA") == "[ZoneTransfer]\r\nZoneId=3\r\n",
                "directory_copy_midfile_cancel_uses_file_engine_cleanup_and_preserves_root_partial_truth_and_source_ads");
        }
        {
            Fixture fixture(root, L"late-cancel"); auto cancellation = std::make_shared<std::atomic_bool>(false); bool final{};
            const auto result = CopyDirectorySafely(Request(fixture), cancellation, [&](const DirectoryCopyProgress& progress) {
                if (progress.completed) { final = true; cancellation->store(true); }
            });
            passed &= Check(final && cancellation->load() && result.copied && result.error == ERROR_SUCCESS &&
                !result.partial_tree_left && result.files_copied == 2 && fixture.CompleteDestination() && fixture.SourcesPreserved(),
                "directory_copy_final_committed_callback_late_cancel_keeps_real_complete_tree_success");
        }
        for (const bool after_create : { false, true })
        {
            Fixture fixture(root, after_create ? L"exception-after-create" : L"exception-before-create"); bool threw{};
            const auto result = CopyDirectorySafely(Request(fixture), {}, [&](const DirectoryCopyProgress& progress) {
                if ((progress.directories_created > 0) == after_create)
                { threw = true; throw std::runtime_error("owned directory observer failure"); }
            });
            Diagnostic(result, after_create ? "exception-after-create" : "exception-before-create");
            passed &= Check(threw && !result.copied && result.error == ERROR_UNHANDLED_EXCEPTION &&
                result.partial_tree_left == after_create && Exists(fixture.destination) == after_create &&
                (after_create ? result.directories_created >= 1 : result.directories_created == 0) && fixture.SourcesPreserved(),
                after_create ? "directory_copy_observer_exception_after_create_preserves_partial_tree_instead_of_false_rollback" :
                "directory_copy_observer_exception_before_create_has_no_destination_side_effect");
        }
        {
            Fixture fixture(root, L"source-leaf-replaced-after-planning");
            const auto selected = fixture.source / L"根文件 {0}.bin";
            const auto original = fixture.source / L"原文件保留.bin";
            bool attempted{}, moved{}; DWORD move_error{};
            const auto result = CopyDirectorySafely(Request(fixture), {}, [&](const DirectoryCopyProgress& progress) {
                if (attempted || !progress.directories_created || progress.completed) { return; }
                attempted = true;
                moved = MoveFileExW(selected.c_str(), original.c_str(), 0) != FALSE;
                move_error = moved ? ERROR_SUCCESS : GetLastError();
                if (moved) { Write(selected, "owned-replacement-must-not-copy"); }
            });
            Diagnostic(result, "source-leaf-replaced-after-planning");
            bool safe = attempted;
            if (moved)
            {
                safe &= !result.copied && result.error == ERROR_FILE_INVALID && result.partial_tree_left &&
                    !Exists(fixture.destination / selected.filename()) &&
                    Entry(original).identity == fixture.source_files[0].identity &&
                    Read(original) == fixture.source_files[0].contents && Read(selected) == "owned-replacement-must-not-copy";
            }
            else
            {
                safe &= (move_error == ERROR_SHARING_VIOLATION || move_error == ERROR_ACCESS_DENIED) &&
                    result.copied && result.error == ERROR_SUCCESS && fixture.CompleteDestination() && fixture.SourcesPreserved();
            }
            std::cout << "INFO directory_copy_source_replacement moved=" << moved << " error=" << move_error << '\n';
            passed &= Check(safe,
                "directory_copy_planned_source_identity_change_is_blocked_or_rejected_without_copying_replacement_bytes");
        }
        {
            Fixture fixture(root, L"source-inventory-changed"); bool added{}, completed{};
            const auto extra = fixture.source / L"复制期间新增.txt";
            const auto result = CopyDirectorySafely(Request(fixture), {}, [&](const DirectoryCopyProgress& progress) {
                completed |= progress.completed;
                if (!added && progress.directories_created && !progress.completed)
                { Write(extra, "owned-new-source-entry"); added = true; }
            });
            Diagnostic(result, "source-inventory-changed");
            const auto actual = CopiedObjects(fixture.destination);
            bool copied_contents_match = true;
            for (const auto& source : fixture.source_files)
            {
                const auto target = fixture.destination / source.path.lexically_relative(fixture.source);
                if (Exists(target)) { copied_contents_match &= Read(target) == source.contents; }
            }
            passed &= Check(added && !completed && !result.copied && result.error != ERROR_SUCCESS && result.partial_tree_left &&
                result.directories_created >= 1 && result.files_copied == actual.first && result.directories_created == actual.second &&
                copied_contents_match &&
                !Exists(fixture.destination / extra.filename()) && Read(extra) == "owned-new-source-entry" && fixture.SourcesPreserved(),
                "directory_copy_final_source_inventory_change_prevents_false_complete_snapshot_and_keeps_actual_copied_tree");
        }
        {
            Fixture fixture(root, L"directory-named-data-stream");
            const auto stream_path = fixture.source.wstring() + L":owned-directory-data:$DATA";
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS;
            parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            bool stream_created{};
            DWORD stream_error{};
            {
                blink::core::UniqueHandle stream(CreateFile3(stream_path.c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, CREATE_NEW, &parameters));
                stream_error = stream ? ERROR_SUCCESS : GetLastError();
                constexpr char Data[] = "owned-directory-stream";
                DWORD written{};
                stream_created = stream && WriteFile(stream.get(), Data, sizeof(Data) - 1, &written, nullptr) &&
                    written == sizeof(Data) - 1 && FlushFileBuffers(stream.get());
            }
            passed &= Check(stream_created, "directory_copy_owned_directory_ads_fixture_is_really_created");
            if (stream_created)
            {
                const auto result = CopyDirectorySafely(Request(fixture));
                Diagnostic(result, "directory-named-data-stream");
                passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_tree_left &&
                    result.directories_created == 0 && !Exists(fixture.destination) &&
                    Read(stream_path) == "owned-directory-stream" && fixture.SourcesPreserved(),
                    "directory_copy_directory_ads_is_explicitly_unsupported_without_losing_stream_data");
            }
            else { std::cout << "INFO directory_copy_directory_ads_fixture_error=" << stream_error << '\n'; }
        }
        {
            Fixture fixture(root, L"source-descendant-junction");
            const auto target = fixture.root / L"自有链接目标";
            std::filesystem::create_directory(target); Write(target / L"保留.txt", "owned-junction-target");
            const auto oracle = Oracle(target / L"保留.txt");
            const auto link = fixture.source / L"目录联接";
            const bool created = CreateOperationTestJunction(link, target);
            passed &= Check(created && Exists(link) && (GetFileAttributesW(link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT),
                "directory_copy_owned_junction_fixture_is_created_without_elevation");
            if (created)
            {
                const auto result = CopyDirectorySafely(Request(fixture));
                Diagnostic(result, "source-descendant-junction");
                passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_tree_left &&
                    result.directories_created == 0 && !Exists(fixture.destination) && oracle.Matches() && fixture.SourcesPreserved(),
                    "directory_copy_reparse_descendant_rejects_entire_plan_without_following_owned_junction");
            }
        }
        {
            Fixture fixture(root, L"destination-parent-junction");
            const auto link = fixture.root / L"目标父联接";
            const bool created = CreateOperationTestJunction(link, fixture.destination_parent);
            passed &= Check(created && Exists(link), "directory_copy_owned_destination_parent_junction_fixture_created");
            if (created)
            {
                auto request = Request(fixture); request.destination_parent = link.wstring();
                const auto result = CopyDirectorySafely(request);
                passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_tree_left &&
                    !Exists(fixture.destination) && fixture.SourcesPreserved(),
                    "directory_copy_destination_parent_reparse_is_rejected_before_any_write");
            }
        }
        {
            Fixture fixture(root, L"source-root-junction");
            const auto link = fixture.source_parent / L"根目录联接";
            const bool created = CreateOperationTestJunction(link, fixture.source);
            passed &= Check(created && Exists(link), "directory_copy_owned_source_root_junction_fixture_created");
            if (created)
            {
                auto request = Request(fixture); request.source = Entry(link);
                const auto result = CopyDirectorySafely(request);
                passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_tree_left &&
                    !Exists(fixture.destination) && fixture.SourcesPreserved(),
                    "directory_copy_source_root_reparse_is_rejected_without_following_target");
            }
        }
        {
            Fixture fixture(root, L"source-file-symlink");
            const auto target = fixture.root / L"自有符号链接目标.txt";
            Write(target, "owned-symlink-target"); const auto oracle = Oracle(target);
            const auto link = fixture.source / L"符号链接.txt";
            const bool created = CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
            const auto error = created ? ERROR_SUCCESS : GetLastError();
            if (!created && (error == ERROR_PRIVILEGE_NOT_HELD || error == ERROR_ACCESS_DENIED || error == ERROR_INVALID_PARAMETER))
            { std::cout << "SKIP directory_copy_source_symlink_fixture_unavailable error=" << error << '\n'; }
            else
            {
                passed &= Check(created && Exists(link) && (GetFileAttributesW(link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT),
                    "directory_copy_true_source_symlink_fixture_created");
                if (created)
                {
                    const auto result = CopyDirectorySafely(Request(fixture));
                    passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && !result.partial_tree_left &&
                        !Exists(fixture.destination) && oracle.Matches(),
                        "directory_copy_source_file_symlink_is_not_followed_or_silently_omitted");
                }
            }
        }
        {
            Fixture fixture(root, L"guard-lifetimes");
            const auto large_path = fixture.source / L"守护大文件.bin";
            const std::string large(3 * 1024 * 1024 + 11, 'g'); Write(large_path, large);
            const auto large_oracle = Oracle(large_path);
            bool directory_guards_seen{}, directory_guards_ok{}, file_guards_seen{}, file_guards_ok{};
            const auto result = CopyDirectorySafely(Request(fixture), {}, [&](const DirectoryCopyProgress& progress) {
                if (!directory_guards_seen && progress.directories_created && !progress.completed)
                {
                    directory_guards_seen = true;
                    directory_guards_ok = RenameBlocked(fixture.source, fixture.source_parent / L"unexpected-source") &&
                        RenameBlocked(fixture.source / L"下一级", fixture.source / L"unexpected-child") &&
                        RenameBlocked(fixture.source_parent, fixture.root / L"unexpected-source-parent") &&
                        RenameBlocked(fixture.destination_parent, fixture.root / L"unexpected-destination-parent") &&
                        RenameBlocked(fixture.destination, fixture.destination_parent / L"unexpected-destination");
                }
                if (!file_guards_seen && progress.current_file.transferred_bytes > 0 &&
                    progress.current_file.transferred_bytes < progress.current_file.total_bytes)
                {
                    file_guards_seen = true;
                    const auto source_file = std::filesystem::path(progress.current_source_path);
                    const auto destination_file = std::filesystem::path(progress.current_destination_path);
                    const bool removed = DeleteFileW(destination_file.c_str()) != FALSE;
                    const auto error = removed ? ERROR_SUCCESS : GetLastError();
                    file_guards_ok = RenameBlocked(source_file, source_file.parent_path() / L"unexpected-source-file.bin") &&
                        !removed && (error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED);
                }
            });
            Diagnostic(result, "guard-lifetimes");
            passed &= Check(directory_guards_seen && directory_guards_ok && file_guards_seen && file_guards_ok &&
                result.copied && fixture.CompleteDestination() && fixture.SourcesPreserved() && large_oracle.Matches() &&
                Read(fixture.destination / large_path.filename()) == large,
                "directory_copy_source_tree_parent_chains_owned_destination_and_active_file_guards_survive_io");
            passed &= Check(RenameRoundTrip(fixture.source, fixture.source_parent / L"released-source") &&
                RenameRoundTrip(fixture.destination, fixture.destination_parent / L"released-destination") &&
                RenameRoundTrip(fixture.source_parent, fixture.root / L"released-source-parent") &&
                RenameRoundTrip(fixture.destination_parent, fixture.root / L"released-destination-parent"),
                "directory_copy_all_source_and_destination_directory_guards_are_released_after_return");
        }
        {
            Fixture fixture(root, L"extended-long-paths", false);
            auto nested = fixture.source; auto long_parent = fixture.destination_parent;
            std::filesystem::path relative;
            for (unsigned index = 0; index < 6; ++index)
            {
                const auto component = std::wstring(45, L'p') + std::to_wstring(index);
                nested /= component; relative /= component; long_parent /= component;
            }
            nested = Extended(nested); long_parent = Extended(long_parent);
            std::filesystem::create_directories(nested); std::filesystem::create_directories(long_parent);
            const auto source_file = nested / L"长路径文件.txt";
            Write(source_file, "owned-long-path-data");
            Write(source_file.wstring() + L":Zone.Identifier:$DATA", "[ZoneTransfer]\r\nZoneId=3\r\n");
            const auto oracle = Oracle(source_file);
            auto request = Request(fixture); request.destination_parent = long_parent.wstring();
            request.destination_parent_identity = Entry(long_parent).identity;
            const auto result = CopyDirectorySafely(request);
            Diagnostic(result, "extended-long-paths");
            const auto copied_file = Extended(long_parent / request.destination_name / relative / source_file.filename());
            passed &= Check(result.copied && result.error == ERROR_SUCCESS && !result.partial_tree_left && result.files_copied == 1 &&
                result.directories_created == 7 && Read(copied_file) == "owned-long-path-data" &&
                Read(copied_file.wstring() + L":Zone.Identifier:$DATA") == "[ZoneTransfer]\r\nZoneId=3\r\n" && oracle.Matches() &&
                Read(source_file.wstring() + L":Zone.Identifier:$DATA") == "[ZoneTransfer]\r\nZoneId=3\r\n",
                "directory_copy_extended_long_source_descendants_and_destination_preserve_file_contents_and_ads");
        }
        {
            Fixture fixture(root, L"exception-after-final"); bool threw{};
            const auto result = CopyDirectorySafely(Request(fixture), {}, [&](const DirectoryCopyProgress& progress) {
                if (progress.completed) { threw = true; throw std::runtime_error("owned postcommit observer failure"); }
            });
            passed &= Check(threw && result.copied && result.error == ERROR_SUCCESS && !result.partial_tree_left &&
                fixture.CompleteDestination() && fixture.SourcesPreserved(),
                "directory_copy_postcommit_observer_exception_cannot_reverse_complete_tree_success");
        }
    }
    catch (const std::exception& error)
    {
        std::cout << "INFO directory_copy_fixture_exception " << error.what() << '\n';
        passed &= Check(false, "directory_copy_owned_fixture_completed_without_exception");
    }
    return passed;
}

bool RunDirectoryCopyConflictTests()
{
    using namespace blink::app;
    bool passed = true;
    const auto root = std::filesystem::absolute(std::filesystem::path(L".build") / L"directory-conflict-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path root; ~Cleanup() { const auto path = Extended(root);
        CleanupOwnedTree(path); (void)RemoveDirectoryW(path.c_str()); } } cleanup{ root };
    try
    {
        {
            Fixture fixture(root, L"skip-vacant"); auto request = Request(fixture); request.conflict_policy = FileCopyConflictPolicy::Skip;
            unsigned progress_calls{};
            const auto result = CopyDirectorySafely(request, {}, [&](const auto&) { ++progress_calls; });
            passed &= Check(result.error == 0 && result.skipped && !result.copied && !result.partial_tree_left &&
                !result.directories_created && !result.files_copied && !progress_calls && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                "directory_conflict_skip_vacant_target_is_explicit_zero_io_choice");
        }
        {
            Fixture fixture(root, L"skip-stale-source"); auto request = Request(fixture);
            request.source_parent = (fixture.root / L"missing").wstring(); request.conflict_policy = FileCopyConflictPolicy::Skip;
            const auto result = CopyDirectorySafely(request);
            passed &= Check(result.error == 0 && result.skipped && !result.copied && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                "directory_conflict_skip_does_not_reopen_source_after_dialog");
        }
        {
            Fixture fixture(root, L"cancel"); auto request = Request(fixture); request.conflict_policy = FileCopyConflictPolicy::Cancel;
            unsigned progress_calls{}; const auto result = CopyDirectorySafely(request, {}, [&](const auto&) { ++progress_calls; });
            passed &= Check(result.error == ERROR_CANCELLED && !result.copied && !result.skipped && !result.partial_tree_left &&
                !result.directories_created && !progress_calls && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                "directory_conflict_cancel_policy_creates_no_tree");
        }
        {
            Fixture fixture(root, L"replace"); auto request = Request(fixture); request.conflict_policy = FileCopyConflictPolicy::Replace;
            const auto result = CopyDirectorySafely(request);
            passed &= Check(result.error == ERROR_NOT_SUPPORTED && !result.copied && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                "directory_conflict_replace_is_explicitly_unsupported");
        }
        {
            Fixture fixture(root, L"unknown"); auto request = Request(fixture); request.conflict_policy = static_cast<FileCopyConflictPolicy>(99);
            const auto result = CopyDirectorySafely(request);
            passed &= Check(result.error == ERROR_INVALID_PARAMETER && !result.copied && !Exists(fixture.destination) && fixture.SourcesPreserved(),
                "directory_conflict_unknown_policy_rejects_before_writes");
        }
        {
            Fixture fixture(root, L"available-original"); auto request = Request(fixture); request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            const auto result = CopyDirectorySafely(request);
            passed &= Check(result.copied && result.error == 0 && !result.skipped && result.path == fixture.destination.wstring() &&
                fixture.CompleteDestination() && fixture.SourcesPreserved(), "directory_conflict_keep_both_uses_vacant_original_name");
        }
        {
            Fixture fixture(root, L"occupied-candidates"); auto request = Request(fixture); request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            std::filesystem::create_directory(fixture.destination); Write(fixture.destination / L"sentinel", "existing-root");
            const auto rival = Oracle(fixture.destination / L"sentinel"); const auto root_id = Entry(fixture.destination).identity;
            const auto second = fixture.destination_parent / (request.destination_name + L" (2)"); Write(second, "rival-file"); const auto file = Oracle(second);
            const auto third = fixture.destination_parent / (request.destination_name + L" (3)");
            bool raced{};
            const auto result = CopyDirectorySafely(request, {}, [&](const auto& progress) {
                if (!raced && !progress.directories_created) { raced = true; std::filesystem::create_directory(third); }
            });
            const auto expected = fixture.destination_parent / (request.destination_name + L" (4)"); fixture.destination = expected;
            passed &= Check(raced && result.copied && result.path == expected.wstring() && result.error == 0 &&
                result.directories_created == 4 && result.files_copied == 2 && !result.partial_tree_left &&
                fixture.CompleteDestination() && rival.Matches() && file.Matches() && Entry(rival.path.parent_path()).identity == root_id &&
                std::filesystem::is_empty(third) && fixture.SourcesPreserved(),
                "directory_conflict_atomic_candidates_preserve_rival_file_directory_and_racing_name");
        }
        {
            Fixture fixture(root, L"dotted-name"); auto request = Request(fixture); request.destination_name = L"资料.v2";
            request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            std::filesystem::create_directory(fixture.destination_parent / request.destination_name);
            const auto result = CopyDirectorySafely(request); fixture.destination = fixture.destination_parent / L"资料.v2 (2)";
            passed &= Check(result.copied && result.path == fixture.destination.wstring() && fixture.CompleteDestination() && fixture.SourcesPreserved(),
                "directory_conflict_number_suffix_follows_whole_directory_name_including_dots");
        }
        {
            Fixture fixture(root, L"unicode-boundary"); auto request = Request(fixture);
            request.destination_name = std::wstring(250, L'a') + L"\U0001F600bbb"; request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            std::filesystem::create_directory(Extended(fixture.destination_parent / request.destination_name));
            const auto result = CopyDirectorySafely(request);
            const auto expected = fixture.destination_parent / (std::wstring(250, L'a') + L" (2)");
            fixture.destination = Extended(expected);
            passed &= Check(result.copied && result.path == expected.wstring() && fixture.CompleteDestination() && fixture.SourcesPreserved(),
                "directory_conflict_long_component_never_splits_utf16_surrogate");
        }
        {
            Fixture fixture(root, L"same-parent-copy"); auto request = Request(fixture);
            request.destination_parent = request.source_parent; request.destination_parent_identity = request.source_parent_identity;
            request.destination_name = request.source.name; request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            const auto source_id = Entry(fixture.source).identity;
            const auto result = CopyDirectorySafely(request); fixture.destination = fixture.source_parent / (request.source.name + L" (2)");
            passed &= Check(result.copied && result.path == fixture.destination.wstring() && fixture.CompleteDestination() &&
                fixture.SourcesPreserved() && Entry(fixture.source).identity == source_id,
                "directory_conflict_keep_both_copies_beside_same_source_without_mutating_source_root");
        }
        {
            Fixture fixture(root, L"child-rival"); auto request = Request(fixture); request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            std::filesystem::create_directory(fixture.destination); const auto original_id = Entry(fixture.destination).identity;
            const auto selected = fixture.destination_parent / (request.destination_name + L" (2)"); bool injected{};
            const auto result = CopyDirectorySafely(request, {}, [&](const auto& progress) {
                if (!injected && progress.directories_created == 1) { injected = true; Write(selected / L"下一级", "child-rival"); }
            });
            passed &= Check(injected && !result.copied && result.partial_tree_left && result.directories_created == 1 &&
                result.path == selected.wstring() && Read(selected / L"下一级") == "child-rival" && Entry(fixture.destination).identity == original_id &&
                !Exists(fixture.destination_parent / (request.destination_name + L" (3)")) && fixture.SourcesPreserved(),
                "directory_conflict_child_failure_never_advances_to_another_root_after_ownership");
        }
        {
            Fixture fixture(root, L"cancel-owned-alternate"); auto request = Request(fixture); request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            std::filesystem::create_directory(fixture.destination); auto cancellation = std::make_shared<std::atomic_bool>(false);
            const auto result = CopyDirectorySafely(request, cancellation, [&](const auto& progress) {
                if (progress.directories_created) { cancellation->store(true); }
            });
            const auto selected = fixture.destination_parent / (request.destination_name + L" (2)");
            passed &= Check(result.error == ERROR_CANCELLED && !result.copied && result.partial_tree_left && result.path == selected.wstring() &&
                result.directories_created == 1 && result.files_copied == 0 && std::filesystem::is_empty(selected) &&
                !Exists(fixture.destination_parent / (request.destination_name + L" (3)")) && fixture.SourcesPreserved(),
                "directory_conflict_cancel_preserves_truthful_selected_partial_root");
        }
        {
            Fixture fixture(root, L"same-source-descendant"); auto request = Request(fixture); request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            request.destination_parent = fixture.source.wstring(); request.destination_parent_identity = Entry(fixture.source).identity;
            const auto result = CopyDirectorySafely(request);
            passed &= Check(result.error == ERROR_INVALID_PARAMETER && !result.copied && !result.partial_tree_left &&
                !Exists(fixture.source / request.destination_name) && fixture.SourcesPreserved(),
                "directory_conflict_keep_both_never_bypasses_source_descendant_rejection");
        }
        {
            Fixture fixture(root, L"wrong-source-id"); auto request = Request(fixture); request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            request.source.identity = request.destination_parent_identity;
            const auto result = CopyDirectorySafely(request);
            passed &= Check(result.error == ERROR_FILE_INVALID && !result.copied && !result.partial_tree_left &&
                !Exists(fixture.destination) && fixture.SourcesPreserved(), "directory_conflict_keep_both_retains_original_source_identity_check");
        }
        {
            Fixture fixture(root, L"attempt-limit", false); auto request = Request(fixture); request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            std::filesystem::create_directory(fixture.destination);
            for (unsigned number = 2; number <= 1000; ++number)
            { std::filesystem::create_directory(fixture.destination_parent / (request.destination_name + L" (" + std::to_wstring(number) + L")")); }
            const auto original_id = Entry(fixture.destination).identity;
            const auto result = CopyDirectorySafely(request);
            passed &= Check((result.error == ERROR_ALREADY_EXISTS || result.error == ERROR_FILE_EXISTS) && !result.copied &&
                !result.skipped && !result.partial_tree_left && !result.directories_created && Entry(fixture.destination).identity == original_id &&
                !Exists(fixture.destination_parent / (request.destination_name + L" (1001)")) && fixture.SourcesPreserved(),
                "directory_conflict_keep_both_attempt_limit_is_finite_and_zero_write");
        }
    }
    catch (const std::exception& error)
    { std::cout << "INFO directory_conflict_fixture_exception " << error.what() << '\n'; passed &= Check(false, "directory_conflict_owned_fixtures_no_exception"); }
    return passed;
}
