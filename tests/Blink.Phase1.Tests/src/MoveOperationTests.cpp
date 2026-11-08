#include "MoveOperationTests.h"
#include "Blink/App/MoveOperation.h"
#include "Blink/App/HandleRename.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <cwchar>
#include <winioctl.h>

bool CreateOperationTestJunction(const std::filesystem::path&, const std::filesystem::path&);
namespace
{
    namespace fs = std::filesystem;
    using namespace blink::app;
    bool Check(bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    fs::path Extended(const fs::path& path)
    { const auto p = path.wstring(); return p.starts_with(L"\\\\?\\") ? path : fs::path(L"\\\\?\\" + p); }
    bool Exists(const fs::path& path) { return GetFileAttributesW(Extended(path).c_str()) != INVALID_FILE_ATTRIBUTES; }
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry entry;
        if (!blink::core::ReadEntryMetadata(Extended(path).wstring(), entry)) { throw std::runtime_error("move fixture metadata"); }
        entry.name = path.filename().wstring(); entry.full_path = path.wstring(); return entry;
    }
    void Write(const fs::path& path, const std::string& data)
    { std::ofstream file(Extended(path), std::ios::binary); file << data; if (!file) { throw std::runtime_error("move fixture write"); } }
    std::string Read(const fs::path& path)
    { std::ifstream file(Extended(path), std::ios::binary); return { std::istreambuf_iterator<char>(file), {} }; }
    void Cleanup(const fs::path& root) noexcept
    {
        WIN32_FIND_DATAW entry{}; HANDLE find = FindFirstFileW((Extended(root) / L"*").c_str(), &entry);
        if (find == INVALID_HANDLE_VALUE) { return; }
        do
        {
            if (!std::wcscmp(entry.cFileName, L".") || !std::wcscmp(entry.cFileName, L"..")) { continue; }
            const auto p = Extended(root) / entry.cFileName;
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            { if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { Cleanup(p); } RemoveDirectoryW(p.c_str()); }
            else { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(p.c_str()); }
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    struct Fixture
    {
        fs::path base, source_parent, target_parent, source;
        bool directory{};
        MoveRequest request;
        Fixture(const fs::path& suite, const std::wstring& label, bool is_directory = false)
            : base(suite / label), source_parent(base / L"source"), target_parent(base / L"target"), directory(is_directory)
        {
            fs::create_directories(Extended(source_parent)); fs::create_directory(Extended(target_parent));
            source = source_parent / (directory ? L"资料.v2" : L"源数据.v2.txt");
            if (directory) { fs::create_directories(Extended(source / L"nested")); }
            Write(Data(source), "owned-move-data"); Write(Data(source).wstring() + L":Zone.Identifier:$DATA", "owned-zone-data");
            request = { source_parent.wstring(), Entry(source_parent).identity, Entry(source), target_parent.wstring(),
                Entry(target_parent).identity, source.filename().wstring() };
        }
        fs::path Data(const fs::path& root) const { return directory ? root / L"nested" / L"data.bin" : root; }
        bool Matches(const fs::path& path) const
        { return Exists(path) && Entry(path).identity == request.source.identity && Read(Data(path)) == "owned-move-data" &&
            Read(Data(path).wstring() + L":Zone.Identifier:$DATA") == "owned-zone-data"; }
        bool Preserved() const { return Matches(source); }
        fs::path Destination() const { return target_parent / request.destination_name; }
    };
    bool ReparseError(DWORD error)
    { return error == ERROR_NOT_SUPPORTED || error == ERROR_REPARSE_POINT_ENCOUNTERED || error == ERROR_CANT_RESOLVE_FILENAME; }
}

bool RunMoveOperationTests()
{
    bool passed = true;
    const auto root = fs::absolute(fs::path(L".build") / L"move-operation-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    fs::create_directories(root);
    struct Final { fs::path root; ~Final() { Cleanup(root); RemoveDirectoryW(Extended(root).c_str()); } } final{ root };
    try
    {
        for (const bool directory : { false, true })
        {
            Fixture f(root, directory ? L"directory" : L"file", directory);
            const auto result = MoveEntrySafely(f.request);
            passed &= Check(result.moved && !result.skipped && !result.unchanged && result.error == 0 && result.post_commit_error == 0 &&
                result.source_path == f.source.wstring() && result.path == f.Destination().wstring() && !Exists(f.source) && f.Matches(f.Destination()),
                "move_real_file_or_nonempty_directory_preserves_identity_contents_and_ads");
        }
        for (const bool directory : { false, true })
        {
            Fixture f(root, directory ? L"noop-dir" : L"noop-file", directory); auto request = f.request;
            request.destination_parent = f.source_parent.wstring(); request.destination_parent_identity = request.source_parent_identity;
            auto result = MoveEntrySafely(request);
            passed &= Check(!result.moved && result.unchanged && result.error == 0 && f.Preserved(), "move_same_parent_exact_name_is_truthful_noop");
            request.destination_name = directory ? L"资料.V2" : L"源数据.V2.TXT";
            result = MoveEntrySafely(request); const auto listing = blink::core::EnumerateDirectory(f.source_parent.wstring());
            passed &= Check(result.moved && result.error == 0 && f.Matches(f.source_parent / request.destination_name) &&
                std::any_of(listing.entries.begin(), listing.entries.end(), [&](const auto& e) { return e.name == request.destination_name && e.identity == f.request.source.identity; }),
                "move_same_parent_case_only_name_updates_stored_name_and_identity");
        }
        for (const bool directory : { false, true })
        {
            Fixture f(root, directory ? L"occupied-dir" : L"occupied-file");
            if (directory) { fs::create_directory(f.Destination()); Write(f.Destination() / L"marker", "incumbent"); }
            else { Write(f.Destination(), "incumbent"); }
            const auto incumbent = Entry(f.Destination()).identity; const auto result = MoveEntrySafely(f.request);
            passed &= Check(!result.moved && (result.error == ERROR_ALREADY_EXISTS || result.error == ERROR_FILE_EXISTS) &&
                f.Preserved() && Entry(f.Destination()).identity == incumbent && Read(directory ? f.Destination() / L"marker" : f.Destination()) == "incumbent",
                "move_existing_file_or_directory_never_replaced");
        }
        {
            Fixture f(root, L"hardlinks"); const auto other = f.source_parent / L"other.txt";
            const bool linked = CreateHardLinkW(other.c_str(), f.source.c_str(), nullptr) != FALSE; const auto result = MoveEntrySafely(f.request);
            passed &= Check(linked && result.moved && !Exists(f.source) && f.Matches(other) && f.Matches(f.Destination()),
                "move_only_selected_hardlink_name_changes");
        }
        {
            MoveRequest request; request.source_parent = L"Z:\\missing"; request.source.name = L"missing";
            request.destination_parent = L"Z:\\missing-target"; request.destination_name = L"new";
            unsigned calls{}; const auto progress = [&](const auto&) { ++calls; };
            request.conflict_policy = FileCopyConflictPolicy::Skip; auto result = MoveEntrySafely(request, {}, progress);
            passed &= Check(result.skipped && !result.moved && result.error == 0 && calls == 0, "move_explicit_skip_is_zero_filesystem_io");
            request.conflict_policy = FileCopyConflictPolicy::Cancel; result = MoveEntrySafely(request, {}, progress);
            passed &= Check(result.error == ERROR_CANCELLED && !result.moved && calls == 0, "move_explicit_cancel_is_zero_filesystem_io");
            request.source.is_directory = true; request.conflict_policy = FileCopyConflictPolicy::Replace; result = MoveEntrySafely(request, {}, progress);
            passed &= Check(result.error == ERROR_NOT_SUPPORTED && !result.moved && calls == 0, "move_directory_replace_remains_unsupported_without_source_access");
            request.conflict_policy = static_cast<FileCopyConflictPolicy>(99); result = MoveEntrySafely(request, {}, progress);
            passed &= Check(result.error == ERROR_INVALID_PARAMETER && !result.moved && calls == 0, "move_unknown_policy_rejected_before_source_access");
        }
        for (unsigned invalid = 0; invalid < 7; ++invalid)
        {
            Fixture f(root, L"invalid-" + std::to_wstring(invalid)); auto request = f.request;
            DWORD expected = ERROR_FILE_INVALID;
            if (invalid == 0) { request.source.identity = request.destination_parent_identity; }
            if (invalid == 1) { request.source_parent_identity.file_id[0] ^= 1; }
            if (invalid == 2) { request.destination_parent_identity.file_id[0] ^= 1; }
            if (invalid == 3) { request.source.is_directory = true; }
            if (invalid == 4) { request.destination_name = L"..\\escape"; expected = ERROR_INVALID_NAME; }
            if (invalid == 5) { request.source_parent = L"\\\\server\\share"; expected = ERROR_NOT_SUPPORTED; }
            if (invalid == 6) { request.source.attributes |= FILE_ATTRIBUTE_DIRECTORY; }
            const auto result = MoveEntrySafely(request);
            passed &= Check(!result.moved && result.error == expected && f.Preserved() && !Exists(f.Destination()),
                "move_invalid_identity_type_name_or_parent_has_no_side_effect");
        }
        {
            Fixture f(root, L"descendant", true); auto request = f.request;
            request.destination_parent = (f.source / L"nested").wstring(); request.destination_parent_identity = Entry(f.source / L"nested").identity;
            request.destination_name = L"inside"; auto result = MoveEntrySafely(request);
            passed &= Check(!result.moved && result.error == ERROR_INVALID_PARAMETER && f.Preserved() && !Exists(f.source / L"nested" / L"inside"),
                "move_directory_into_own_descendant_rejected_after_actual_identity_check");
            request.source.identity = Entry(root).identity; result = MoveEntrySafely(request);
            passed &= Check(!result.moved && result.error == ERROR_FILE_INVALID && f.Preserved(), "move_wrong_source_id_that_names_target_ancestor_keeps_identity_error_priority");
        }
        {
            Fixture f(root, L"guarded"); bool source_blocked{}, source_parent_blocked{}, target_parent_blocked{}, writer_blocked{};
            const auto result = MoveEntrySafely(f.request, {}, {}, [&](HANDLE, HANDLE, const std::wstring&) {
                source_blocked = !MoveFileExW(f.source.c_str(), (f.source_parent / L"stolen").c_str(), 0);
                source_parent_blocked = !MoveFileExW(f.source_parent.c_str(), (f.base / L"stolen-source").c_str(), 0);
                target_parent_blocked = !MoveFileExW(f.target_parent.c_str(), (f.base / L"stolen-target").c_str(), 0);
                CREATEFILE3_EXTENDED_PARAMETERS p{}; p.dwSize = sizeof(p); p.dwFileFlags = FILE_FLAG_OPEN_REPARSE_POINT;
                blink::core::UniqueHandle writer(CreateFile3(f.source.c_str(), FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    OPEN_EXISTING, &p)); writer_blocked = !writer;
                return ERROR_ACCESS_DENIED;
            });
            passed &= Check(!result.moved && result.error == ERROR_ACCESS_DENIED && source_blocked && source_parent_blocked &&
                target_parent_blocked && writer_blocked && f.Preserved(), "move_source_and_both_parent_chains_held_until_native_returns");
        }
        {
            Fixture f(root, L"cancel"); const auto cancel = std::make_shared<std::atomic_bool>(false);
            const auto result = MoveEntrySafely(f.request, cancel, [&](const MoveProgress& progress) {
                if (progress.phase == MovePhase::Committing) { cancel->store(true); }
            });
            passed &= Check(!result.moved && result.error == ERROR_CANCELLED && f.Preserved() && !Exists(f.Destination()),
                "move_cancel_at_commit_boundary_preserves_source_and_absent_destination");
        }
        {
            Fixture f(root, L"late-cancel"); const auto cancel = std::make_shared<std::atomic_bool>(false);
            const auto result = MoveEntrySafely(f.request, cancel, [&](const MoveProgress& progress) {
                if (progress.phase == MovePhase::Completed) { cancel->store(true); throw std::runtime_error("late observer"); }
            });
            passed &= Check(result.moved && result.error == 0 && cancel->load() && f.Matches(f.Destination()) && !Exists(f.source),
                "move_real_commit_wins_over_late_cancel_and_observer_exception");
        }
        {
            Fixture f(root, L"stale-source"); const auto old = f.source_parent / L"preserved-original";
            fs::rename(f.source, old); Write(f.source, "new-source"); const auto replacement = Entry(f.source).identity;
            const auto result = MoveEntrySafely(f.request);
            passed &= Check(!result.moved && result.error == ERROR_FILE_INVALID && f.Matches(old) &&
                Entry(f.source).identity == replacement && Read(f.source) == "new-source" && !Exists(f.Destination()),
                "move_replaced_source_path_never_moves_new_identity");
        }
        {
            Fixture f(root, L"stale-destination"); const auto old = f.base / L"preserved-target";
            fs::rename(f.target_parent, old); fs::create_directory(f.target_parent); const auto result = MoveEntrySafely(f.request);
            passed &= Check(!result.moved && result.error == ERROR_FILE_INVALID && f.Preserved() && fs::is_empty(old) && fs::is_empty(f.target_parent),
                "move_replaced_destination_parent_rejected_before_rename");
        }
        for (const bool directory : { false, true })
        {
            Fixture f(root, directory ? L"keep-directory" : L"keep-file", directory); auto request = f.request;
            request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            const auto second = directory ? L"资料.v2 (2)" : L"源数据.v2 (2).txt";
            const auto third = directory ? L"资料.v2 (3)" : L"源数据.v2 (3).txt";
            Write(f.Destination(), "incumbent"); const auto incumbent = Entry(f.Destination()).identity; bool rival{};
            const auto result = MoveEntrySafely(request, {}, [&](const MoveProgress& progress) {
                if (progress.phase == MovePhase::Committing && fs::path(progress.destination_path).filename() == second && !rival)
                { Write(f.target_parent / second, "rival"); rival = true; }
            });
            passed &= Check(result.moved && result.error == 0 && rival && result.path == (f.target_parent / third).wstring() &&
                f.Matches(f.target_parent / third) && Entry(f.Destination()).identity == incumbent && Read(f.Destination()) == "incumbent" &&
                Read(f.target_parent / second) == "rival" && !Exists(f.source), "move_keep_both_atomically_handles_rivals_and_preserves_file_extension_or_directory_name");
        }
        {
            Fixture f(root, L"utf16"); auto request = f.request;
            request.destination_name = std::wstring(246, L'a') + L"\U0001f680bbb.txt";
            request.conflict_policy = FileCopyConflictPolicy::KeepBoth; Write(f.target_parent / request.destination_name, "keep");
            const auto result = MoveEntrySafely(request); const auto expected = std::wstring(246, L'a') + L" (2).txt";
            passed &= Check(result.moved && result.path == (f.target_parent / expected).wstring() && f.Matches(f.target_parent / expected) &&
                Read(f.target_parent / request.destination_name) == "keep", "move_keep_both_truncation_preserves_utf16_pair_and_extension");
        }
        {
            Fixture f(root, L"long-path"); fs::path a = f.source_parent, b = f.target_parent;
            for (unsigned n = 0; n < 6; ++n) { a /= std::wstring(45, L'a') + std::to_wstring(n); b /= std::wstring(45, L'b') + std::to_wstring(n); }
            fs::create_directories(Extended(a)); fs::create_directories(Extended(b));
            const auto source = a / L"Unicode {0}.txt"; Write(source, "long-data"); const auto id = Entry(source).identity;
            MoveRequest request{ a.wstring(), Entry(a).identity, Entry(source), b.wstring(), Entry(b).identity, L"长路径.txt" };
            const auto result = MoveEntrySafely(request);
            passed &= Check(result.moved && result.error == 0 && !Exists(source) && Entry(b / request.destination_name).identity == id &&
                Read(b / request.destination_name) == "long-data", "move_extended_long_parent_paths_preserve_real_identity_and_bytes");
        }
        {
            Fixture f(root, L"root-junction"); const auto outside = f.base / L"outside"; fs::create_directory(outside);
            Write(outside / L"sentinel", "sentinel"); const auto sentinel = Entry(outside / L"sentinel").identity;
            bool point{};
            const auto result = MoveEntrySafely(f.request, {}, {}, [&](HANDLE source, HANDLE parent, const std::wstring& name) {
                point = CreateOperationTestJunction(f.target_parent, outside);
                return detail::RenameRelativeByHandle(source, parent, name);
            });
            passed &= Check(point && !result.moved && ReparseError(result.error) && f.Preserved() && !Exists(outside / f.request.destination_name) &&
                Entry(outside / L"sentinel").identity == sentinel && Read(outside / L"sentinel") == "sentinel",
                "move_actual_target_parent_junction_at_native_boundary_cannot_redirect_rename");
        }
        for (const bool source_parent : { true, false })
        {
            Fixture f(root, source_parent ? L"source-chain" : L"target-chain"); const auto link = f.base / L"junction";
            const bool point = CreateOperationTestJunction(link, source_parent ? f.source_parent : f.target_parent);
            auto request = f.request;
            if (source_parent) { request.source_parent = link.wstring(); }
            else { request.destination_parent = link.wstring(); }
            const auto result = MoveEntrySafely(request);
            passed &= Check(point && !result.moved && ReparseError(result.error) && f.Preserved() && !Exists(f.Destination()),
                "move_source_or_target_parent_chain_junction_is_not_followed");
        }
        {
            Fixture f(root, L"limit"); auto request = f.request; request.destination_name = L"bounded.txt"; request.conflict_policy = FileCopyConflictPolicy::KeepBoth;
            Write(f.target_parent / L"bounded.txt", "keep");
            for (unsigned n = 2; n <= 1000; ++n) { Write(f.target_parent / (L"bounded (" + std::to_wstring(n) + L").txt"), "keep"); }
            const auto result = MoveEntrySafely(request);
            passed &= Check(!result.moved && (result.error == ERROR_ALREADY_EXISTS || result.error == ERROR_FILE_EXISTS) && f.Preserved() &&
                !Exists(f.target_parent / L"bounded (1001).txt") && Read(f.target_parent / L"bounded (1000).txt") == "keep",
                "move_keep_both_has_finite_1000_candidate_limit_without_clobber");
        }
        {
            Fixture f(root, L"cross-volume", true); wchar_t temp[32768]{};
            const auto length = GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
            const auto other = length ? fs::path(temp) / L"BlinkMoveDirectory" /
                (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())) : fs::path{};
            if (length) { fs::create_directories(other); }
            struct OtherCleanup { fs::path path; ~OtherCleanup() { if (!path.empty()) { Cleanup(path); RemoveDirectoryW(path.c_str()); } } } other_cleanup{ other };
            const auto identity = length ? Entry(other).identity : blink::core::FileIdentity{};
            if (identity.valid && identity.volume_serial != f.request.source.identity.volume_serial)
            {
                auto request = f.request; request.destination_parent = other.wstring(); request.destination_parent_identity = identity;
                const auto result = MoveEntrySafely(request); const auto destination = other / request.destination_name;
                passed &= Check(result.moved && result.source_removed && !Exists(f.source) &&
                    Read(f.Data(destination)) == "owned-move-data" && Read(f.Data(destination).wstring() + L":Zone.Identifier:$DATA") == "owned-zone-data",
                    "move_real_other_volume_directory_uses_shared_copy_and_original_file_removal");
            }
            else { std::cout << "SKIP move_other_volume_fixture unavailable\n"; }
        }
    }
    catch (const std::exception& e)
    { std::cout << "INFO move_fixture_exception " << e.what() << '\n'; passed &= Check(false, "move_owned_fixture_no_exception"); }
    return passed;
}
