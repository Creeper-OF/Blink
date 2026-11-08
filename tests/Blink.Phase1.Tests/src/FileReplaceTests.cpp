#include "FileReplaceTests.h"
#include "Blink/App/FileOperationCoordinator.h"
#include "Blink/App/CopyTransferPresentation.h"
#include "Blink/App/MoveTransferPresentation.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

bool CreateOperationTestJunction(const std::filesystem::path&, const std::filesystem::path&);
namespace
{
    namespace fs = std::filesystem;
    using namespace blink::app;
    bool Check(bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    fs::path Extended(const fs::path& path)
    { return path.wstring().starts_with(L"\\\\?\\") ? path : fs::path(L"\\\\?\\" + path.wstring()); }
    void Write(const fs::path& path, const std::string& data)
    { std::ofstream stream(Extended(path), std::ios::binary); stream << data; if (!stream) { throw std::runtime_error("replace fixture write"); } }
    std::string Read(const fs::path& path)
    { std::ifstream stream(Extended(path), std::ios::binary); return { std::istreambuf_iterator<char>(stream), {} }; }
    bool CanOpen(const fs::path& path)
    {
        blink::core::UniqueHandle file(CreateFileW(Extended(path).c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        return static_cast<bool>(file);
    }
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry value;
        if (!blink::core::ReadEntryMetadata(Extended(path).wstring(), value)) { throw std::runtime_error("replace fixture metadata"); }
        value.name = path.filename().wstring(); value.full_path = path.wstring(); return value;
    }
    void Cleanup(const fs::path& root) noexcept
    {
        WIN32_FIND_DATAW value{}; const auto pattern = Extended(root) / L"*"; const auto find = FindFirstFileW(pattern.c_str(), &value);
        if (find == INVALID_HANDLE_VALUE) { return; }
        do
        {
            if (!std::wcscmp(value.cFileName, L".") || !std::wcscmp(value.cFileName, L"..")) { continue; }
            const auto child = Extended(root) / value.cFileName;
            if (value.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            { if (!(value.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { Cleanup(child); } SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL); RemoveDirectoryW(child.c_str()); }
            else { SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(child.c_str()); }
        } while (FindNextFileW(find, &value));
        FindClose(find);
    }
    struct OwnedRoot
    {
        fs::path root;
        ~OwnedRoot() { Cleanup(root); RemoveDirectoryW(Extended(root).c_str()); }
    };
    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        auto done = std::make_shared<std::promise<Status>>(); auto future = done->get_future();
        action.Completed([done](auto&&, Status status) { done->set_value(status); });
        return future.wait_for(std::chrono::seconds(8)) == std::future_status::ready && future.get() == Status::Completed;
    }
    bool NoStaging(const fs::path& parent)
    {
        for (const auto& file : fs::directory_iterator(Extended(parent)))
        { if (file.path().filename().wstring().starts_with(L".blink-replace-")) { return false; } }
        return true;
    }
    struct Fixture
    {
        fs::path root, source_parent, target_parent, source, target;
        Fixture(const fs::path& suite, const wchar_t* name) : root(suite / name), source_parent(root / L"来源"),
            target_parent(root / L"目标"), source(source_parent / L"内容.txt"), target(target_parent / L"同名.txt")
        { fs::create_directories(Extended(source_parent)); fs::create_directories(Extended(target_parent)); Write(source, "new-content"); Write(target, "old-content"); }
        FileCopyRequest Request() const
        { return { source_parent.wstring(), Entry(source_parent).identity, Entry(source), target_parent.wstring(), Entry(target_parent).identity,
            target.filename().wstring(), FileCopyConflictPolicy::Replace }; }
        bool SourceIntact() const { return Read(source) == "new-content"; }
    };
}

bool RunFileReplaceTests()
{
    bool passed = true;
    const auto suffix = L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto root = fs::absolute(fs::path(L".build") / L"file-replace-fixtures" / suffix);
    fs::create_directories(root); OwnedRoot cleanup{ root };
    try
    {
        {
            Fixture f(root, L"copy"); const auto old = Entry(f.target).identity;
            Write(f.source.wstring() + L":Zone.Identifier", "new-zone"); Write(f.target.wstring() + L":stale", "old-stream");
            const auto source = Entry(f.source);
            const auto result = CopyFileSafely(f.Request());
            passed &= Check(result.copied && !result.error && !result.partial_file_left && result.path == f.target.wstring() &&
                Read(f.target) == "new-content" && Entry(f.target).identity != old && f.SourceIntact(),
                "replace_copy_commits_new_file_at_original_name_without_changing_source");
            passed &= Check(Read(f.target.wstring() + L":Zone.Identifier") == "new-zone" &&
                !CanOpen(f.target.wstring() + L":stale") && NoStaging(f.target_parent),
                "replace_copy_replaces_old_ads_inventory_and_removes_staging_name");
            const auto target = Entry(f.target);
            passed &= Check(CompareFileTime(&target.last_write_time, &source.last_write_time) == 0,
                "replace_copy_preserves_source_basic_write_time");
        }
        {
            Fixture f(root, L"hardlink"); const auto alias = f.target_parent / L"old-alias.txt";
            fs::create_hard_link(f.target, alias); const auto old = Entry(alias).identity; const auto result = CopyFileSafely(f.Request());
            passed &= Check(result.copied && Read(alias) == "old-content" && Entry(alias).identity == old &&
                Entry(f.target).identity != old && Read(f.target) == "new-content", "replace_copy_changes_only_target_link_and_preserves_other_hardlink_bytes");
        }
        {
            Fixture f(root, L"self"); auto request = f.Request(); request.destination_parent = f.source_parent.wstring();
            request.destination_parent_identity = Entry(f.source_parent).identity; request.destination_name = f.source.filename().wstring();
            const auto same = CopyFileSafely(request); const auto alias = f.target_parent / L"source-alias.txt"; fs::create_hard_link(f.source, alias);
            request = f.Request(); request.destination_name = alias.filename().wstring(); const auto linked = CopyFileSafely(request);
            passed &= Check(!same.copied && same.error == ERROR_INVALID_PARAMETER && !linked.copied && linked.error == ERROR_INVALID_PARAMETER &&
                f.SourceIntact() && Read(alias) == "new-content" && NoStaging(f.target_parent), "replace_copy_rejects_source_path_and_source_hardlink_alias");
        }
        {
            Fixture f(root, L"empty"); Write(f.source, ""); const auto result = CopyFileSafely(f.Request());
            passed &= Check(result.copied && !result.error && Read(f.target).empty() && NoStaging(f.target_parent),
                "replace_empty_source_replaces_existing_nonempty_target");
        }
        {
            Fixture f(root, L"missing"); fs::remove(f.target); const auto result = CopyFileSafely(f.Request());
            passed &= Check(result.copied && !result.error && Read(f.target) == "new-content" && NoStaging(f.target_parent),
                "replace_copy_creates_destination_when_original_name_is_absent");
        }
        {
            Fixture f(root, L"readonly-target"); const auto old = Entry(f.target).identity;
            const bool set = SetFileAttributesW(Extended(f.target).c_str(), FILE_ATTRIBUTE_READONLY) != FALSE; bool notified{};
            const auto result = CopyFileSafely(f.Request(), {}, [&](const auto&) { notified = true; });
            passed &= Check(set && !result.copied && result.error == ERROR_ACCESS_DENIED && !notified &&
                Entry(f.target).identity == old && Read(f.target) == "old-content" && f.SourceIntact() && NoStaging(f.target_parent),
                "replace_readonly_target_fails_without_changing_attributes_or_old_content");
        }
        {
            Fixture f(root, L"readonly-source"); const bool set = SetFileAttributesW(Extended(f.source).c_str(), FILE_ATTRIBUTE_READONLY) != FALSE;
            const auto result = CopyFileSafely(f.Request());
            passed &= Check(set && result.copied && !result.error && Read(f.target) == "new-content" &&
                (Entry(f.target).attributes & FILE_ATTRIBUTE_READONLY) && NoStaging(f.target_parent),
                "replace_readonly_source_copies_its_attributes_before_commit");
        }
        {
            Fixture f(root, L"directory-target"); fs::remove(f.target); fs::create_directory(f.target); Write(f.target / L"keep", "sentinel");
            const auto result = CopyFileSafely(f.Request());
            passed &= Check(!result.copied && result.error == ERROR_NOT_SUPPORTED && Read(f.target / L"keep") == "sentinel" &&
                f.SourceIntact() && NoStaging(f.target_parent), "replace_file_never_removes_a_directory_to_make_room");
        }
        {
            Fixture f(root, L"junction-target"); fs::remove(f.target); const auto outside = f.root / L"outside";
            fs::create_directory(outside); Write(outside / L"keep", "sentinel"); const bool made = CreateOperationTestJunction(f.target, outside);
            const auto result = CopyFileSafely(f.Request());
            passed &= Check(made && !result.copied && result.error && Read(outside / L"keep") == "sentinel" &&
                (Entry(f.target).attributes & FILE_ATTRIBUTE_REPARSE_POINT) && f.SourceIntact() && NoStaging(f.target_parent),
                "replace_detected_junction_target_is_not_followed_or_removed");
        }
        {
            Fixture f(root, L"cancel"); const auto old = Entry(f.target).identity; Write(f.source, std::string(3 * 1024 * 1024, 'x'));
            const auto cancelled = std::make_shared<std::atomic_bool>(); bool midway{};
            const auto result = CopyFileSafely(f.Request(), cancelled, [&](const auto& value) {
                if (value.transferred_bytes && value.transferred_bytes < value.total_bytes) { midway = true; cancelled->store(true); }
            });
            passed &= Check(midway && !result.copied && result.error == ERROR_CANCELLED && !result.partial_file_left &&
                Entry(f.target).identity == old && Read(f.target) == "old-content" && NoStaging(f.target_parent),
                "replace_cancel_during_copy_preserves_old_destination_and_cleans_temp");
            const auto stopped = CopyFileSafely(f.Request(), cancelled);
            passed &= Check(!stopped.copied && stopped.error == ERROR_CANCELLED && Read(f.target) == "old-content" && NoStaging(f.target_parent),
                "replace_prestarted_cancel_performs_no_target_mutation");
        }
        {
            Fixture f(root, L"observer-failure"); Write(f.source, std::string(3 * 1024 * 1024, 'x')); bool midway{};
            const auto result = CopyFileSafely(f.Request(), {}, [&](const auto& value) {
                if (value.transferred_bytes) { midway = true; throw std::runtime_error("observer"); }
            });
            passed &= Check(midway && !result.copied && result.error == ERROR_UNHANDLED_EXCEPTION && !result.partial_file_left &&
                Read(f.target) == "old-content" && NoStaging(f.target_parent), "replace_copy_observer_failure_preserves_old_target_and_cleans_temp");
        }
        {
            Fixture f(root, L"late-cancel"); const auto cancelled = std::make_shared<std::atomic_bool>();
            const auto result = CopyFileSafely(f.Request(), cancelled, [&](const auto& value) {
                if (value.transferred_bytes == value.total_bytes) { cancelled->store(true); throw std::runtime_error("late observer"); }
            });
            passed &= Check(result.copied && !result.error && cancelled->load() && Read(f.target) == "new-content" && NoStaging(f.target_parent),
                "replace_committed_copy_survives_late_cancel_and_observer_exception");
        }
        {
            Fixture f(root, L"target-changed"); const auto backup = f.target_parent / L"old-backup.txt"; bool changed{};
            const auto result = CopyFileSafely(f.Request(), {}, [&](const auto& value) {
                if (!changed && !value.transferred_bytes) { changed = true; fs::rename(f.target, backup); Write(f.target, "rival"); }
            });
            passed &= Check(changed && !result.copied && result.error == ERROR_FILE_INVALID && Read(backup) == "old-content" &&
                Read(f.target) == "rival" && f.SourceIntact() && NoStaging(f.target_parent),
                "replace_observed_target_identity_change_aborts_before_name_commit");
        }
        {
            Fixture f(root, L"busy-at-commit"); blink::core::UniqueHandle busy;
            const auto result = CopyFileSafely(f.Request(), {}, [&](const auto& value) {
                if (!value.transferred_bytes && !busy) { busy.reset(CreateFileW(Extended(f.target).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)); }
            });
            const bool held = static_cast<bool>(busy); busy.reset();
            passed &= Check(held && !result.copied && (result.error == ERROR_SHARING_VIOLATION || result.error == ERROR_ACCESS_DENIED) &&
                !result.partial_file_left && Read(f.target) == "old-content" && f.SourceIntact() && NoStaging(f.target_parent),
                "replace_busy_destination_keeps_old_file_and_cleans_completed_staging_copy");
        }
        {
            auto long_root = root; for (unsigned i = 0; i < 10; ++i) { long_root /= L"中文目录与足够长度0123456789"; }
            Fixture f(long_root, L"长路径"); const auto result = CopyFileSafely(f.Request());
            std::cout << "INFO replace_long_path chars=" << f.target.wstring().size() << " original_fixture_chars=" << f.target.wstring().size() - 40
                << " error=" << result.error << " copied=" << result.copied << '\n';
            passed &= Check(f.target.wstring().size() > MAX_PATH && result.copied && !result.error &&
                Read(f.target) == "new-content" && NoStaging(f.target_parent), "replace_unicode_long_paths_commit_to_requested_leaf");
        }
        {
            Fixture f(root, L"move"); const auto source = Entry(f.source).identity; const auto alias = f.target_parent / L"old-alias.txt";
            fs::create_hard_link(f.target, alias); const auto result = MoveEntrySafely(f.Request());
            passed &= Check(result.moved && !result.error && !fs::exists(f.source) && Entry(f.target).identity == source &&
                Read(f.target) == "new-content" && Read(alias) == "old-content", "replace_same_volume_move_preserves_source_identity_and_old_hardlink_alias");
        }
        {
            Fixture f(root, L"move-cancel"); const auto cancelled = std::make_shared<std::atomic_bool>();
            const auto result = MoveEntrySafely(f.Request(), cancelled, [&](const auto& value) {
                if (value.phase == MovePhase::Committing) { cancelled->store(true); }
            });
            passed &= Check(!result.moved && result.error == ERROR_CANCELLED && f.SourceIntact() && Read(f.target) == "old-content",
                "replace_move_cancel_before_rename_preserves_both_original_files");
        }
        {
            Fixture f(root, L"move-target-change"); bool changed{}; const auto backup = f.target_parent / L"old-backup.txt";
            const auto result = MoveEntrySafely(f.Request(), {}, [&](const auto& value) {
                if (!changed && value.phase == MovePhase::Preparing) { changed = true; fs::rename(f.target, backup); Write(f.target, "rival"); }
            });
            passed &= Check(changed && !result.moved && result.error == ERROR_FILE_INVALID && f.SourceIntact() &&
                Read(f.target) == "rival" && Read(backup) == "old-content", "replace_move_observed_target_change_keeps_source_and_rival");
        }
        {
            Fixture f(root, L"copy-resume"); Write(f.source_parent / L"tail.txt", "tail"); auto first = f.Request(); first.conflict_policy = FileCopyConflictPolicy::Fail;
            auto tail = first; tail.source = Entry(f.source_parent / L"tail.txt"); tail.destination_name = L"tail.txt";
            FileOperationCoordinator operation; const auto id = operation.CopyBatch({ first, tail }); const bool failed = Finished(operation.CurrentAction());
            const auto before = operation.Snapshot(); const auto resumed = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Replace);
            const bool done = Finished(operation.CurrentAction()); const auto after = operation.Snapshot();
            passed &= Check(id && failed && CopyQueueHasRootConflict(before) && resumed > id && done && after.stage == FileOperationStage::Completed &&
                after.copy_batch && after.copy_batch->copied_count == 2 && !after.copy_batch->remaining_count &&
                Read(f.target) == "new-content" && Read(f.target_parent / L"tail.txt") == "tail" && f.SourceIntact(),
                "replace_copy_conflict_resumes_existing_queue_and_continues_remaining_items");
        }
        {
            Fixture f(root, L"move-resume"); Write(f.source_parent / L"tail.txt", "tail"); auto first = f.Request(); first.conflict_policy = FileCopyConflictPolicy::Fail;
            auto tail = first; tail.source = Entry(f.source_parent / L"tail.txt"); tail.destination_name = L"tail.txt";
            FileOperationCoordinator operation; const auto id = operation.MoveEntries({ first, tail }); const bool failed = Finished(operation.CurrentAction());
            const auto before = operation.Snapshot(); const auto resumed = operation.ResumeMoveBatch(id, FileCopyConflictPolicy::Replace);
            const bool done = Finished(operation.CurrentAction()); const auto after = operation.Snapshot();
            passed &= Check(id && failed && MoveQueueHasRootConflict(before) && resumed > id && done && after.stage == FileOperationStage::Completed &&
                after.move_batch && after.move_batch->moved_count == 2 && !after.move_batch->remaining_count &&
                Read(f.target) == "new-content" && Read(f.target_parent / L"tail.txt") == "tail" && !fs::exists(f.source),
                "replace_move_conflict_resumes_existing_queue_and_moves_tail");
        }
        {
            wchar_t temp[32768]{}; const auto length = GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
            if (!length || length >= std::size(temp)) { throw std::runtime_error("replace cross-volume temp"); }
            const auto other = fs::path(temp) / L"BlinkFileReplace" / suffix; fs::create_directories(other); OwnedRoot cleanup_other{ other };
            Fixture f(root, L"cross-volume"); auto request = f.Request(); request.destination_parent = other.wstring();
            request.destination_parent_identity = Entry(other).identity; const auto target = other / request.destination_name;
            Write(target, "old-cross-volume"); const bool distinct = request.source.identity.volume_serial != request.destination_parent_identity.volume_serial;
            const auto copied = CopyFileSafely(request);
            passed &= Check(distinct && copied.copied && !copied.error && Read(target) == "new-content" && f.SourceIntact() && NoStaging(other),
                "replace_real_cross_volume_copy_replaces_only_after_complete_copy");
            Write(target, "old-again"); const auto moved = MoveEntrySafely(request);
            passed &= Check(distinct && moved.moved && !moved.error && moved.source_removed && moved.copy_result && moved.copy_result->copied &&
                Read(target) == "new-content" && !fs::exists(f.source) && NoStaging(other), "replace_real_cross_volume_move_removes_source_after_target_commit");
        }
    }
    catch (const std::exception& error) { std::cout << "FAIL replace_unexpected_exception " << error.what() << '\n'; passed = false; }
    catch (...) { passed &= Check(false, "replace_unexpected_exception"); }
    return passed;
}
