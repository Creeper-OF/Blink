#include "FileCopyBatchTests.h"
#include "Blink/App/FileOperationCoordinator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace blink::app;

    bool Check(bool condition, const char* name)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        return condition;
    }

    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        const auto done = std::make_shared<std::promise<Status>>();
        auto result = done->get_future();
        action.Completed([done](auto&&, Status status) { done->set_value(status); });
        return result.wait_for(std::chrono::seconds(5)) == std::future_status::ready && result.get() == Status::Completed;
    }

    struct Gate
    {
        std::promise<void> entered;
        std::future<void> started{ entered.get_future() };
        std::promise<void> release;
        std::shared_future<void> proceed{ release.get_future().share() };
        std::atomic_bool signalled{}, released{};
        ~Gate() { Release(); }
        void Enter()
        {
            if (!signalled.exchange(true)) { entered.set_value(); }
            if (proceed.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
            { throw std::runtime_error("owned batch fixture gate timed out"); }
        }
        bool Started() { return started.wait_for(std::chrono::seconds(5)) == std::future_status::ready; }
        void Release() noexcept
        {
            if (!released.exchange(true)) { try { release.set_value(); } catch (...) {} }
        }
    };

    blink::core::FileEntry Entry(const std::filesystem::path& path)
    {
        blink::core::FileEntry entry;
        if (!blink::core::ReadEntryMetadata(path.wstring(), entry))
        { throw std::runtime_error("cannot read owned batch fixture metadata"); }
        entry.name = path.filename().wstring();
        entry.full_path = path.wstring();
        return entry;
    }

    void Write(const std::filesystem::path& path, const std::string& value)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(value.data(), static_cast<std::streamsize>(value.size()));
        if (!file) { throw std::runtime_error("cannot write owned batch fixture"); }
    }

    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    }

    bool Exists(const std::filesystem::path& path)
    { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

    struct Fixture
    {
        std::filesystem::path root, source, destination;
        std::vector<FileCopyRequest> requests;
        explicit Fixture(const std::filesystem::path& suite, const wchar_t* name)
            : root(suite / name), source(root / L"源目录"), destination(root / L"目标目录")
        {
            std::filesystem::create_directories(source);
            std::filesystem::create_directories(destination);
            for (const auto* leaf : { L"第一项.txt", L"第二项.txt", L"第三项.txt" })
            {
                Write(source / leaf, "owned-batch-" + std::to_string(requests.size()));
                requests.push_back({ source.wstring(), Entry(source).identity, Entry(source / leaf),
                    destination.wstring(), Entry(destination).identity, leaf });
            }
            Write((source / requests[0].source.name).wstring() + L":Zone.Identifier:$DATA", "[ZoneTransfer]\r\nZoneId=3\r\n");
        }
        bool SourcePreserved(std::size_t index) const
        {
            const auto path = source / requests[index].source.name;
            return Exists(path) && Entry(path).identity == requests[index].source.identity &&
                Read(path) == "owned-batch-" + std::to_string(index);
        }
        bool DestinationMatches(std::size_t index) const
        {
            return Exists(destination / requests[index].destination_name) &&
                Read(destination / requests[index].destination_name) == "owned-batch-" + std::to_string(index);
        }
    };

    struct Calls
    {
        std::mutex mutex;
        std::vector<std::wstring> names;
        std::vector<DWORD> threads;
        std::atomic_uint active{}, maximum{};
        void Start(const FileCopyRequest& request)
        {
            const auto current = ++active;
            auto previous = maximum.load();
            while (current > previous && !maximum.compare_exchange_weak(previous, current)) {}
            std::scoped_lock lock(mutex);
            names.push_back(request.source.name);
            threads.push_back(GetCurrentThreadId());
        }
        void End() { --active; }
    };

    FileCopyResult Native(const std::shared_ptr<Calls>& calls, const FileCopyRequest& request,
        const FileOperationCancellation& cancelled, const FileCopyProgressCallback& progress)
    {
        calls->Start(request);
        struct Leave { std::shared_ptr<Calls> calls; ~Leave() { calls->End(); } } leave{ calls };
        return CopyFileSafely(request, cancelled, progress);
    }

    bool HistoryContains(const FileCopyBatchItem& item, DWORD error)
    {
        return std::any_of(item.previous_results.begin(), item.previous_results.end(), [error](const auto& result) {
            return result.error == error && !result.copied;
        });
    }
}

bool RunFileCopyBatchTests()
{
    using namespace blink::app;
    bool passed = true;
    const auto root = std::filesystem::absolute(std::filesystem::path(L".build") / L"file-copy-batch-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    std::filesystem::create_directories(root);
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{ root };
    try
    {
        {
            Fixture fixture(root, L"ordered-native");
            const auto calls = std::make_shared<Calls>();
            const auto thread = GetCurrentThreadId();
            FileOperationCoordinator operation({}, [calls](const auto& request, const auto& cancelled, const auto& progress) {
                return Native(calls, request, cancelled, progress);
            });
            const auto id = operation.CopyBatch(fixture.requests);
            const bool finished = Finished(operation.CurrentAction());
            const auto snapshot = operation.Snapshot();
            passed &= Check(id != 0 && finished && snapshot.copy_batch && snapshot.stage == FileOperationStage::Completed &&
                snapshot.copy_batch->items.size() == 3 && snapshot.copy_batch->copied_count == 3 &&
                snapshot.copy_batch->skipped_count == 0 && snapshot.copy_batch->failed_count == 0 &&
                snapshot.copy_batch->remaining_count == 0 && snapshot.copy_batch->partial_count == 0 &&
                !snapshot.copy_batch->resumable,
                "file_copy_batch_three_real_files_finish_with_exact_queue_accounting");
            passed &= Check(calls->names == std::vector<std::wstring>{ fixture.requests[0].source.name,
                fixture.requests[1].source.name, fixture.requests[2].source.name } && calls->maximum == 1 &&
                calls->threads.size() == 3 && calls->threads[0] != thread &&
                calls->threads[0] == calls->threads[1] && calls->threads[1] == calls->threads[2],
                "file_copy_batch_preserves_order_on_one_owned_worker_without_parallel_mutations");
            passed &= Check(fixture.DestinationMatches(0) && fixture.DestinationMatches(1) && fixture.DestinationMatches(2) &&
                fixture.SourcePreserved(0) && fixture.SourcePreserved(1) && fixture.SourcePreserved(2) &&
                Read((fixture.destination / fixture.requests[0].destination_name).wstring() + L":Zone.Identifier:$DATA") ==
                    "[ZoneTransfer]\r\nZoneId=3\r\n" &&
                Read((fixture.source / fixture.requests[0].source.name).wstring() + L":Zone.Identifier:$DATA") ==
                    "[ZoneTransfer]\r\nZoneId=3\r\n",
                "file_copy_batch_real_unicode_names_ads_and_all_source_identities_are_preserved");
        }
        {
            Fixture fixture(root, L"duplicate-source");
            const auto calls = std::make_shared<Calls>();
            FileOperationCoordinator operation({}, [calls](const auto& request, const auto& cancelled, const auto& progress) {
                return Native(calls, request, cancelled, progress);
            });
            const auto id = operation.CopyBatch({ fixture.requests[0], fixture.requests[0] });
            const bool stopped = Finished(operation.CurrentAction());
            const auto conflict = operation.Snapshot();
            passed &= Check(id != 0 && stopped && conflict.copy_batch && conflict.copy_batch->items.size() == 2 &&
                calls->names.size() == 2 && conflict.copy_batch->copied_count == 1 && conflict.copy_batch->failed_count == 1 &&
                conflict.copy_batch->resumable && fixture.DestinationMatches(0),
                "file_copy_batch_duplicate_source_is_attempted_and_becomes_explicit_conflict_without_deduplication");
            const auto resumed = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Skip);
            const bool done = Finished(operation.CurrentAction());
            const auto snapshot = operation.Snapshot();
            passed &= Check(resumed > id && done && snapshot.copy_batch && snapshot.copy_batch->copied_count == 1 &&
                snapshot.copy_batch->skipped_count == 1 && snapshot.copy_batch->failed_count == 0 &&
                snapshot.copy_batch->remaining_count == 0 && snapshot.copy_batch->items[0].result.copied &&
                snapshot.copy_batch->items[1].result.skipped && HistoryContains(snapshot.copy_batch->items[1], ERROR_FILE_EXISTS) &&
                fixture.SourcePreserved(0),
                "file_copy_batch_duplicate_conflict_skip_retains_both_entries_and_prior_failure_history");
        }
        {
            Fixture fixture(root, L"hardlink-distinct-names");
            const auto original = fixture.source / fixture.requests[0].source.name;
            const auto link = fixture.source / L"硬链接名称.txt";
            const bool linked = CreateHardLinkW(link.c_str(), original.c_str(), nullptr) != FALSE;
            if (!linked) { throw std::runtime_error("cannot create owned batch hard-link fixture"); }
            auto linked_request = fixture.requests[0];
            linked_request.source = Entry(link); linked_request.destination_name = link.filename().wstring();
            FileOperationCoordinator operation;
            const auto id = operation.CopyBatch({ fixture.requests[0], linked_request });
            const bool finished = Finished(operation.CurrentAction());
            const auto snapshot = operation.Snapshot();
            passed &= Check(id != 0 && finished && snapshot.copy_batch && snapshot.copy_batch->copied_count == 2 &&
                snapshot.copy_batch->items.size() == 2 && snapshot.copy_batch->items[0].request.source.identity ==
                    snapshot.copy_batch->items[1].request.source.identity &&
                snapshot.copy_batch->items[0].request.source.name != snapshot.copy_batch->items[1].request.source.name &&
                fixture.DestinationMatches(0) && Read(fixture.destination / linked_request.destination_name) == "owned-batch-0" &&
                Entry(fixture.destination / linked_request.destination_name).identity !=
                    Entry(fixture.destination / fixture.requests[0].destination_name).identity &&
                Entry(link).identity == fixture.requests[0].source.identity && fixture.SourcePreserved(0),
                "file_copy_batch_hardlink_names_remain_distinct_queue_items_and_distinct_destination_objects");
        }
        for (unsigned invalid_kind = 0; invalid_kind < 3; ++invalid_kind)
        {
            const auto scope = L"preflight-" + std::to_wstring(invalid_kind);
            Fixture fixture(root, scope.c_str());
            auto requests = fixture.requests;
            if (invalid_kind == 0) { requests.back().destination_name = L"..\\must-not-escape"; }
            if (invalid_kind == 1) { requests.back().source.identity.valid = false; }
            if (invalid_kind == 2) { requests.back().source.attributes |= FILE_ATTRIBUTE_REPARSE_POINT; }
            const auto calls = std::make_shared<std::atomic_uint>(0);
            FileOperationCoordinator operation({}, [calls](auto&&, auto&&, auto&&) {
                ++*calls; return FileCopyResult{ ERROR_SUCCESS, true };
            });
            const auto id = operation.CopyBatch(std::move(requests));
            const bool finished = Finished(operation.CurrentAction());
            const auto snapshot = operation.Snapshot();
            const auto expected = invalid_kind == 0 ? ERROR_INVALID_NAME :
                invalid_kind == 1 ? ERROR_FILE_INVALID : ERROR_NOT_SUPPORTED;
            passed &= Check(id != 0 && finished && snapshot.stage == FileOperationStage::Failed &&
                snapshot.result.error == expected && calls->load() == 0 && snapshot.copy_batch &&
                snapshot.copy_batch->copied_count == 0 && snapshot.copy_batch->failed_count == 0 &&
                snapshot.copy_batch->remaining_count == 3 && !snapshot.copy_batch->resumable &&
                std::all_of(snapshot.copy_batch->items.begin(), snapshot.copy_batch->items.end(), [](const auto& item) {
                    return item.stage == FileCopyBatchItemStage::Pending;
                }) &&
                !Exists(fixture.destination / fixture.requests[0].destination_name) &&
                !Exists(fixture.destination / fixture.requests[1].destination_name) &&
                !Exists(fixture.destination / fixture.requests[2].destination_name) &&
                fixture.SourcePreserved(0) && fixture.SourcePreserved(1) && fixture.SourcePreserved(2),
                invalid_kind == 0 ? "file_copy_batch_invalid_last_leaf_rejects_entire_input_before_any_executor" :
                invalid_kind == 1 ? "file_copy_batch_invalid_last_identity_rejects_entire_input_before_any_executor" :
                "file_copy_batch_unsupported_last_source_rejects_entire_input_before_any_executor");
        }
        {
            Fixture fixture(root, L"collision-skip-resume");
            const auto occupied = fixture.destination / fixture.requests[1].destination_name;
            Write(occupied, "owned-existing-target");
            const auto existing_identity = Entry(occupied).identity;
            const auto gate = std::make_shared<Gate>();
            const auto calls = std::make_shared<Calls>();
            FileOperationCoordinator operation({}, [calls](const auto& request, const auto& cancelled, const auto& progress) {
                return Native(calls, request, cancelled, progress);
            });
            const auto id = operation.CopyBatch(fixture.requests, [gate](const auto& snapshot) {
                if (snapshot.copy_batch && snapshot.copy_batch->resumable && !gate->signalled.load()) { gate->Enter(); }
            });
            const bool published = gate->Started();
            const auto action = operation.CurrentAction();
            const auto blocked = operation.Snapshot();
            passed &= Check(published && blocked.copy_batch && blocked.copy_batch->resumable && operation.IsBusy() &&
                action.Status() == winrt::Windows::Foundation::AsyncStatus::Started &&
                operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Skip) == 0 && calls->names.size() == 2 &&
                !Exists(fixture.destination / fixture.requests[2].destination_name),
                "file_copy_batch_conflict_publication_cannot_resume_until_real_native_action_returns");
            gate->Release();
            const bool stopped = Finished(action);
            const auto before = operation.Snapshot();
            const auto first_target_identity = Entry(fixture.destination / fixture.requests[0].destination_name).identity;
            passed &= Check(stopped && !operation.IsBusy() && before.copy_batch && before.copy_batch->copied_count == 1 &&
                before.copy_batch->failed_count == 1 && before.copy_batch->remaining_count == 1 &&
                before.copy_batch->items[0].result.copied && before.copy_batch->items[1].result.error == ERROR_FILE_EXISTS &&
                before.copy_batch->items[2].stage == FileCopyBatchItemStage::Pending &&
                operation.ResumeCopyBatch(id + 99, FileCopyConflictPolicy::Skip) == 0,
                "file_copy_batch_stops_at_second_collision_and_rejects_stale_resume_generation");
            passed &= Check(operation.HasPendingCopyBatch() && operation.CopyBatch(fixture.requests) == 0 &&
                operation.Copy(fixture.requests[2]) == 0 &&
                operation.CreateFolder({ fixture.destination.wstring(), fixture.requests[0].destination_parent_identity, L"must-not-interleave" }) == 0 &&
                operation.Rename({ fixture.source.wstring(), fixture.requests[0].source_parent_identity,
                    fixture.requests[0].source, L"must-not-interleave.txt" }) == 0,
                "file_copy_batch_pending_tail_survives_worker_exit_and_blocks_unrelated_mutation_until_decision");
            const auto resumed = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Skip);
            const bool finished = Finished(operation.CurrentAction());
            const auto after = operation.Snapshot();
            passed &= Check(resumed > id && finished && after.copy_batch && after.stage == FileOperationStage::Completed &&
                after.copy_batch->copied_count == 2 && after.copy_batch->skipped_count == 1 &&
                after.copy_batch->failed_count == 0 && after.copy_batch->remaining_count == 0 &&
                after.copy_batch->items[0].result.path == before.copy_batch->items[0].result.path &&
                after.copy_batch->items[1].result.skipped && HistoryContains(after.copy_batch->items[1], ERROR_FILE_EXISTS) &&
                fixture.DestinationMatches(0) && fixture.DestinationMatches(2) &&
                Entry(fixture.destination / fixture.requests[0].destination_name).identity == first_target_identity &&
                Entry(occupied).identity == existing_identity && Read(occupied) == "owned-existing-target",
                "file_copy_batch_skip_resume_keeps_first_commit_and_existing_target_then_copies_only_pending_item");
            passed &= Check(before.copy_batch->copied_count == 1 && before.copy_batch->failed_count == 1 &&
                before.copy_batch->remaining_count == 1 && before.copy_batch->items[2].stage == FileCopyBatchItemStage::Pending,
                "file_copy_batch_prior_terminal_snapshot_remains_immutable_after_resume");
        }
        {
            Fixture fixture(root, L"collision-keep-both");
            const auto occupied = fixture.destination / fixture.requests[1].destination_name;
            Write(occupied, "owned-conflict");
            const auto occupied_id = Entry(occupied).identity;
            FileOperationCoordinator operation;
            const auto id = operation.CopyBatch(fixture.requests);
            const bool stopped = Finished(operation.CurrentAction());
            const auto before = operation.Snapshot();
            const auto resumed = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::KeepBoth);
            const bool finished = Finished(operation.CurrentAction());
            const auto after = operation.Snapshot();
            passed &= Check(stopped && before.copy_batch && before.copy_batch->resumable && resumed > id && finished &&
                after.copy_batch && after.copy_batch->copied_count == 3 && after.copy_batch->failed_count == 0 &&
                after.copy_batch->remaining_count == 0 && after.copy_batch->items[1].result.copied &&
                after.copy_batch->items[1].result.path.ends_with(L"第二项 (2).txt") &&
                Read(after.copy_batch->items[1].result.path) == "owned-batch-1" && HistoryContains(after.copy_batch->items[1], ERROR_FILE_EXISTS) &&
                Entry(occupied).identity == occupied_id && Read(occupied) == "owned-conflict" &&
                fixture.DestinationMatches(0) && fixture.DestinationMatches(2) &&
                fixture.SourcePreserved(0) && fixture.SourcePreserved(1) && fixture.SourcePreserved(2),
                "file_copy_batch_keep_both_resume_retains_numbered_actual_path_history_and_all_other_commits");
        }
        {
            Fixture fixture(root, L"explicit-name-retry");
            const auto occupied = fixture.destination / fixture.requests[1].destination_name;
            Write(occupied, "owned-name-conflict");
            const auto occupied_id = Entry(occupied).identity;
            FileOperationCoordinator operation;
            const auto id = operation.CopyBatch(fixture.requests);
            const bool stopped = Finished(operation.CurrentAction());
            const auto first_id = Entry(fixture.destination / fixture.requests[0].destination_name).identity;
            const auto rejected = operation.ResumeCopyBatch(id, static_cast<FileCopyConflictPolicy>(99));
            const auto renamed = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Fail, std::wstring{ L"明确改名重试.txt" });
            const bool finished = Finished(operation.CurrentAction());
            const auto snapshot = operation.Snapshot();
            passed &= Check(stopped && rejected == 0 && renamed > id && finished && snapshot.copy_batch &&
                snapshot.copy_batch->copied_count == 3 && snapshot.copy_batch->remaining_count == 0 &&
                snapshot.copy_batch->items[1].request.destination_name == L"明确改名重试.txt" &&
                snapshot.copy_batch->items[1].result.path.ends_with(L"明确改名重试.txt") &&
                Read(snapshot.copy_batch->items[1].result.path) == "owned-batch-1" &&
                HistoryContains(snapshot.copy_batch->items[1], ERROR_FILE_EXISTS) &&
                Entry(fixture.destination / fixture.requests[0].destination_name).identity == first_id &&
                Entry(occupied).identity == occupied_id && Read(occupied) == "owned-name-conflict" &&
                fixture.DestinationMatches(2),
                "file_copy_batch_explicit_name_retry_changes_only_current_conflict");
        }
        {
            Fixture fixture(root, L"renamed-destination-resume");
            Write(fixture.destination / fixture.requests[1].destination_name, "owned-relocated-conflict");
            FileOperationCoordinator operation;
            const auto id = operation.CopyBatch(fixture.requests);
            const bool stopped = Finished(operation.CurrentAction());
            const auto before = operation.Snapshot();
            const auto old_parent = fixture.destination.wstring();
            const auto first_identity = Entry(fixture.destination / fixture.requests[0].destination_name).identity;
            const auto new_parent = fixture.root / L"改名后的目标目录";
            std::filesystem::rename(fixture.destination, new_parent);
            fixture.destination = new_parent;
            const auto resumed = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::KeepBoth, {}, {}, new_parent.wstring());
            const bool finished = Finished(operation.CurrentAction());
            const auto after = operation.Snapshot();
            passed &= Check(stopped && before.copy_batch && before.copy_batch->resumable && resumed > id && finished &&
                after.copy_batch && after.copy_batch->copied_count == 3 && after.copy_batch->remaining_count == 0 &&
                after.copy_batch->items[0].request.destination_parent == old_parent &&
                after.copy_batch->items[0].result.path == before.copy_batch->items[0].result.path &&
                after.copy_batch->items[1].request.destination_parent == new_parent.wstring() &&
                after.copy_batch->items[2].request.destination_parent == new_parent.wstring() &&
                after.copy_batch->items[1].request.destination_parent_identity == fixture.requests[1].destination_parent_identity &&
                after.copy_batch->items[2].request.destination_parent_identity == fixture.requests[2].destination_parent_identity &&
                Entry(new_parent / fixture.requests[0].destination_name).identity == first_identity &&
                Read(after.copy_batch->items[1].result.path) == "owned-batch-1" && fixture.DestinationMatches(2) &&
                Read(new_parent / fixture.requests[1].destination_name) == "owned-relocated-conflict",
                "file_copy_batch_explicit_same_identity_parent_rebind_moves_only_unfinished_requests_and_preserves_commit_history");
        }
        {
            Fixture fixture(root, L"source-changed-after-capture");
            const auto calls = std::make_shared<Calls>();
            auto executions = std::make_shared<std::atomic_uint>(0);
            FileOperationCoordinator operation({}, [&, calls, executions](const auto& request, const auto& cancelled, const auto& progress) {
                auto result = Native(calls, request, cancelled, progress);
                if (++*executions == 1 && result.copied)
                {
                    std::filesystem::rename(fixture.source / fixture.requests[1].source.name, fixture.source / L"旧第二项保留.txt");
                    Write(fixture.source / fixture.requests[1].source.name, "owned-replacement");
                }
                return result;
            });
            const auto id = operation.CopyBatch(fixture.requests);
            const bool finished = Finished(operation.CurrentAction());
            const auto snapshot = operation.Snapshot();
            passed &= Check(id != 0 && finished && snapshot.copy_batch && snapshot.stage == FileOperationStage::Failed &&
                snapshot.copy_batch->copied_count == 1 && snapshot.copy_batch->failed_count == 1 &&
                snapshot.copy_batch->remaining_count == 1 && snapshot.copy_batch->resumable && calls->names.size() == 2 &&
                snapshot.copy_batch->items[1].result.error == ERROR_FILE_INVALID &&
                operation.HasPendingCopyBatch() && fixture.DestinationMatches(0) &&
                !Exists(fixture.destination / fixture.requests[1].destination_name) &&
                !Exists(fixture.destination / fixture.requests[2].destination_name) &&
                Entry(fixture.source / L"旧第二项保留.txt").identity == fixture.requests[1].source.identity &&
                Read(fixture.source / L"旧第二项保留.txt") == "owned-batch-1" &&
                Read(fixture.source / fixture.requests[1].source.name) == "owned-replacement",
                "file_copy_batch_nonconflict_source_change_preserves_prior_commit_and_both_source_objects_without_implicit_skip");
        }
        {
            Fixture fixture(root, L"partial-error-truth");
            auto executions = std::make_shared<std::atomic_uint>(0);
            FileOperationCoordinator operation({}, [&, executions](const auto& request, const auto& cancelled, const auto& progress) {
                if (++*executions != 2) { return CopyFileSafely(request, cancelled, progress); }
                const auto partial = fixture.destination / request.destination_name;
                Write(partial, "owned-partial-prefix");
                return FileCopyResult{ ERROR_WRITE_FAULT, false, true, partial.wstring() };
            });
            const auto id = operation.CopyBatch(fixture.requests);
            const bool finished = Finished(operation.CurrentAction());
            const auto snapshot = operation.Snapshot();
            passed &= Check(id != 0 && finished && snapshot.copy_batch && snapshot.stage == FileOperationStage::Failed &&
                snapshot.copy_batch->copied_count == 1 && snapshot.copy_batch->failed_count == 1 &&
                snapshot.copy_batch->remaining_count == 1 && snapshot.copy_batch->partial_count == 1 &&
                snapshot.copy_batch->resumable && snapshot.copy_batch->items[1].result.partial_file_left &&
                snapshot.copy_batch->items[1].result.error == ERROR_WRITE_FAULT &&
                Read(snapshot.copy_batch->items[1].result.path) == "owned-partial-prefix" && fixture.DestinationMatches(0) &&
                !Exists(fixture.destination / fixture.requests[2].destination_name) &&
                fixture.SourcePreserved(0) && fixture.SourcePreserved(1) && fixture.SourcePreserved(2),
                "file_copy_batch_executor_partial_error_preserves_actual_path_count_and_prior_success");
            const auto old_partial_path = snapshot.copy_batch ? snapshot.copy_batch->items[1].result.path : std::wstring{};
            const auto first_identity = Entry(fixture.destination / fixture.requests[0].destination_name).identity;
            const auto retry = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Fail, std::wstring{ L"完整重试文件.txt" });
            const bool retry_finished = Finished(operation.CurrentAction());
            const auto completed = operation.Snapshot();
            passed &= Check(retry > id && retry_finished && completed.copy_batch && completed.stage == FileOperationStage::Completed &&
                completed.copy_batch->copied_count == 3 && completed.copy_batch->failed_count == 0 &&
                completed.copy_batch->remaining_count == 0 && completed.copy_batch->partial_count == 1 &&
                completed.copy_batch->items[1].result.copied && !completed.copy_batch->items[1].result.partial_file_left &&
                std::any_of(completed.copy_batch->items[1].previous_results.begin(), completed.copy_batch->items[1].previous_results.end(),
                    [&old_partial_path](const auto& previous) {
                        return previous.error == ERROR_WRITE_FAULT && previous.partial_file_left && previous.path == old_partial_path;
                    }) && Read(old_partial_path) == "owned-partial-prefix" &&
                Read(completed.copy_batch->items[1].result.path) == "owned-batch-1" && fixture.DestinationMatches(2) &&
                Entry(fixture.destination / fixture.requests[0].destination_name).identity == first_identity &&
                fixture.SourcePreserved(0) && fixture.SourcePreserved(1) && fixture.SourcePreserved(2),
                "file_copy_batch_explicit_partial_retry_preserves_residual_path_history_after_later_success");
        }
        {
            Fixture fixture(root, L"cancel-after-first-commit");
            const auto gate = std::make_shared<Gate>();
            const auto calls = std::make_shared<Calls>();
            auto executions = std::make_shared<std::atomic_uint>(0);
            FileOperationCoordinator operation({}, [calls, gate, executions](const auto& request, const auto& cancelled, const auto& progress) {
                auto result = Native(calls, request, cancelled, progress);
                if (++*executions == 1 && result.copied) { gate->Enter(); }
                return result;
            });
            const auto id = operation.CopyBatch(fixture.requests);
            const bool entered = gate->Started();
            operation.Cancel();
            passed &= Check(entered && id != 0 && fixture.DestinationMatches(0) && operation.IsBusy() &&
                operation.Snapshot().stage == FileOperationStage::Cancelling && operation.CopyBatch(fixture.requests) == 0,
                "file_copy_batch_cancel_during_first_committed_executor_keeps_worker_owned_and_rejects_duplicate_queue");
            gate->Release();
            const bool finished = Finished(operation.CurrentAction());
            const auto snapshot = operation.Snapshot();
            passed &= Check(finished && snapshot.copy_batch && snapshot.stage == FileOperationStage::Cancelled &&
                snapshot.copy_batch->copied_count == 1 && snapshot.copy_batch->remaining_count == 2 &&
                snapshot.copy_batch->items[0].result.copied && snapshot.copy_batch->items[1].stage == FileCopyBatchItemStage::Pending &&
                snapshot.copy_batch->items[2].stage == FileCopyBatchItemStage::Pending && calls->names.size() == 1 &&
                fixture.DestinationMatches(0) && !Exists(fixture.destination / fixture.requests[1].destination_name) &&
                !Exists(fixture.destination / fixture.requests[2].destination_name),
                "file_copy_batch_cancel_after_first_real_commit_keeps_success_and_leaves_later_items_pending");
        }
        {
            Fixture fixture(root, L"cancel-waiting-decision");
            Write(fixture.destination / fixture.requests[1].destination_name, "owned-conflict");
            FileOperationCoordinator operation;
            const auto id = operation.CopyBatch(fixture.requests);
            const bool stopped = Finished(operation.CurrentAction());
            const auto before = operation.Snapshot();
            operation.Cancel();
            const auto after = operation.Snapshot();
            passed &= Check(id != 0 && stopped && before.copy_batch && before.copy_batch->resumable &&
                after.copy_batch && after.stage == FileOperationStage::Cancelled && after.cancel_requested &&
                !after.copy_batch->resumable && after.copy_batch->copied_count == 1 &&
                after.copy_batch->failed_count == 1 && after.copy_batch->remaining_count == 1 &&
                operation.ResumeCopyBatch(id, FileCopyConflictPolicy::KeepBoth) == 0 &&
                fixture.DestinationMatches(0) && Read(fixture.destination / fixture.requests[1].destination_name) == "owned-conflict" &&
                !Exists(fixture.destination / fixture.requests[2].destination_name),
                "file_copy_batch_cancel_while_waiting_for_conflict_decision_retains_results_and_never_starts_pending_item");
        }
        {
            Fixture fixture(root, L"resume-cancel-decision");
            Write(fixture.destination / fixture.requests[1].destination_name, "owned-cancel-conflict");
            const auto calls = std::make_shared<Calls>();
            FileOperationCoordinator operation({}, [calls](const auto& request, const auto& cancelled, const auto& progress) {
                return Native(calls, request, cancelled, progress);
            });
            const auto id = operation.CopyBatch(fixture.requests);
            const bool stopped = Finished(operation.CurrentAction());
            const auto previous = operation.Snapshot();
            const auto resumed = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Cancel);
            const bool finished = Finished(operation.CurrentAction());
            const auto snapshot = operation.Snapshot();
            passed &= Check(id != 0 && stopped && previous.copy_batch && previous.copy_batch->resumable &&
                resumed > id && finished && snapshot.copy_batch && snapshot.stage == FileOperationStage::Cancelled &&
                snapshot.copy_batch->copied_count == 1 && snapshot.copy_batch->failed_count == 1 &&
                snapshot.copy_batch->remaining_count == 1 && snapshot.copy_batch->cancelled_count == 0 &&
                !snapshot.copy_batch->resumable && !operation.HasPendingCopyBatch() && calls->names.size() == 2 &&
                snapshot.copy_batch->items[1].result.error == previous.copy_batch->items[1].result.error &&
                snapshot.copy_batch->items[2].stage == FileCopyBatchItemStage::Pending &&
                fixture.DestinationMatches(0) && !Exists(fixture.destination / fixture.requests[2].destination_name),
                "file_copy_batch_cancel_resolution_gets_new_finished_action_without_reexecuting_conflict_or_pending_files");
        }
        {
            Fixture fixture(root, L"late-cancel-last-commit");
            const auto gate = std::make_shared<Gate>();
            auto executions = std::make_shared<std::atomic_uint>(0);
            FileOperationCoordinator operation({}, [gate, executions](const auto& request, const auto& cancelled, const auto& progress) {
                auto result = CopyFileSafely(request, cancelled, progress);
                if (++*executions == 3 && result.copied) { gate->Enter(); }
                return result;
            });
            const auto id = operation.CopyBatch(fixture.requests);
            const bool entered = gate->Started();
            operation.Cancel(); gate->Release();
            const bool finished = Finished(operation.CurrentAction());
            const auto snapshot = operation.Snapshot();
            passed &= Check(id != 0 && entered && finished && snapshot.copy_batch && snapshot.cancel_requested &&
                snapshot.stage == FileOperationStage::Completed && snapshot.copy_batch->copied_count == 3 &&
                snapshot.copy_batch->cancelled_count == 0 && snapshot.copy_batch->remaining_count == 0 &&
                fixture.DestinationMatches(0) && fixture.DestinationMatches(1) && fixture.DestinationMatches(2),
                "file_copy_batch_last_item_already_committed_keeps_entire_batch_completed_after_late_cancel");
        }
        {
            Fixture fixture(root, L"real-ads-cancellation");
            const std::string stream_contents(3 * 1024 * 1024 + 19, 'q');
            const auto source_stream = (fixture.source / fixture.requests[0].source.name).wstring() + L":large-owned:$DATA";
            Write(source_stream, stream_contents);
            auto cancelled = std::make_shared<std::atomic_bool>(false);
            auto executions = std::make_shared<std::atomic_uint>(0);
            FileOperationCoordinator* owner{};
            FileOperationCoordinator operation({}, [&owner, cancelled, executions](const auto& request, const auto& control, const auto& progress) {
                ++*executions;
                return CopyFileSafely(request, control, [&](const FileCopyProgress& value) {
                    progress(value);
                    if (value.transferred_bytes > 1024 * 1024 && value.transferred_bytes < value.total_bytes && !cancelled->exchange(true))
                    { owner->Cancel(); }
                });
            });
            owner = &operation;
            const auto id = operation.CopyBatch(fixture.requests);
            const bool finished = Finished(operation.CurrentAction());
            const auto snapshot = operation.Snapshot();
            passed &= Check(id != 0 && finished && cancelled->load() && executions->load() == 1 && snapshot.copy_batch &&
                snapshot.stage == FileOperationStage::Cancelled && snapshot.copy_batch->copied_count == 0 &&
                snapshot.copy_batch->cancelled_count == 1 && snapshot.copy_batch->remaining_count == 2 &&
                snapshot.copy_batch->partial_count == 0 && snapshot.copy_batch->items[0].result.error == ERROR_CANCELLED &&
                !Exists(fixture.destination / fixture.requests[0].destination_name) && fixture.SourcePreserved(0) &&
                Read(source_stream) == stream_contents && fixture.SourcePreserved(1) && fixture.SourcePreserved(2),
                "file_copy_batch_real_ads_cancel_cleans_owned_target_and_preserves_all_source_streams_and_pending_items");
        }
        {
            Fixture fixture(root, L"close-inflight");
            const auto gate = std::make_shared<Gate>();
            const auto calls = std::make_shared<Calls>();
            auto published = std::make_shared<std::atomic_uint>(0);
            auto operation = std::make_unique<FileOperationCoordinator>(FileOperationCoordinator::ExecuteFunction{},
                [gate, calls](const auto& request, const auto& cancelled, const auto& progress) {
                    auto result = Native(calls, request, cancelled, progress);
                    if (result.copied) { gate->Enter(); }
                    return result;
                });
            const auto id = operation->CopyBatch(fixture.requests, [published](auto) { ++*published; });
            const bool entered = gate->Started();
            const auto action = operation->CurrentAction();
            const auto count_at_close = published->load();
            const auto before = std::chrono::steady_clock::now();
            operation->Shutdown();
            const auto elapsed = std::chrono::steady_clock::now() - before;
            passed &= Check(id != 0 && entered && elapsed < std::chrono::milliseconds(250) && operation->Snapshot().closed &&
                operation->IsBusy() && operation->CopyBatch(fixture.requests) == 0,
                "file_copy_batch_shutdown_is_immediate_and_keeps_inflight_native_action_owned");
            operation.reset(); gate->Release();
            passed &= Check(Finished(action) && published->load() == count_at_close && calls->names.size() == 1 &&
                fixture.DestinationMatches(0) && !Exists(fixture.destination / fixture.requests[1].destination_name) &&
                !Exists(fixture.destination / fixture.requests[2].destination_name),
                "file_copy_batch_worker_outlives_closed_owner_and_suppresses_late_ui_without_losing_first_commit");
        }
        {
            Fixture fixture(root, L"current-bytes-versus-aggregate");
            const auto gate = std::make_shared<Gate>();
            auto executions = std::make_shared<std::atomic_uint>(0);
            FileOperationCoordinator operation({}, [gate, executions](const auto& request, const auto& cancelled,
                const FileCopyProgressCallback& progress) {
                auto result = CopyFileSafely(request, cancelled, progress);
                if (++*executions == 1 && result.copied) { gate->Enter(); }
                return result;
            });
            const auto id = operation.CopyBatch(fixture.requests);
            const bool entered = gate->Started();
            const auto current = operation.Snapshot();
            passed &= Check(id != 0 && entered && current.copy_batch && current.stage == FileOperationStage::Running &&
                current.copy_progress.total_bytes > 0 && current.copy_progress.transferred_bytes == current.copy_progress.total_bytes &&
                current.copy_batch->remaining_count > 0 && current.copy_batch->copied_count < current.copy_batch->items.size() &&
                current.copy_batch->items[1].stage == FileCopyBatchItemStage::Pending &&
                current.copy_batch->items[2].stage == FileCopyBatchItemStage::Pending &&
                !Exists(fixture.destination / fixture.requests[1].destination_name),
                "file_copy_batch_current_item_100_percent_never_claims_pending_queue_is_complete");
            gate->Release();
            passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().copy_batch &&
                operation.Snapshot().copy_batch->copied_count == 3 && operation.Snapshot().copy_batch->remaining_count == 0,
                "file_copy_batch_aggregate_finishes_only_after_every_real_item_commit");
        }
        {
            Fixture fixture(root, L"previous-item-progress");
            auto old_progress = std::make_shared<FileCopyProgressCallback>();
            const auto gate = std::make_shared<Gate>();
            auto executions = std::make_shared<std::atomic_uint>(0);
            FileOperationCoordinator operation({}, [old_progress, gate, executions](const auto& request, const auto& cancelled,
                const FileCopyProgressCallback& progress) {
                const auto number = ++*executions;
                if (number == 1) { *old_progress = progress; }
                if (number == 2) { progress({ 100, 20 }); gate->Enter(); }
                return CopyFileSafely(request, cancelled, progress);
            });
            const auto id = operation.CopyBatch(fixture.requests);
            const bool entered = gate->Started();
            const auto before = operation.Snapshot();
            if (*old_progress) { (*old_progress)({ 777777, 777777 }); }
            const auto after = operation.Snapshot();
            passed &= Check(id != 0 && entered && before.copy_batch && after.copy_batch &&
                before.copy_batch->current_index == 1 && after.copy_batch->current_index == 1 &&
                before.copy_progress.total_bytes == after.copy_progress.total_bytes &&
                before.copy_progress.transferred_bytes == after.copy_progress.transferred_bytes &&
                after.copy_batch->copied_count == 1 && after.copy_batch->remaining_count == 2,
                "file_copy_batch_previous_item_progress_cannot_forge_current_item_or_aggregate_within_same_operation");
            gate->Release();
            passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().copy_batch &&
                operation.Snapshot().copy_batch->copied_count == 3,
                "file_copy_batch_continues_after_rejected_progress_from_previous_item");
        }
        {
            Fixture fixture(root, L"old-generation-progress");
            auto progress_from_old = std::make_shared<FileCopyProgressCallback>();
            const auto gate = std::make_shared<Gate>();
            auto executions = std::make_shared<std::atomic_uint>(0);
            FileOperationCoordinator operation({}, [progress_from_old, gate, executions](const auto& request, const auto& cancelled,
                const FileCopyProgressCallback& progress) {
                const auto number = ++*executions;
                if (number == 1) { *progress_from_old = progress; }
                if (number == 2) { progress({ 100, 25 }); gate->Enter(); }
                return CopyFileSafely(request, cancelled, progress);
            });
            const auto first = operation.CopyBatch({ fixture.requests[0] });
            const bool first_done = Finished(operation.CurrentAction());
            const auto second = operation.CopyBatch({ fixture.requests[1], fixture.requests[2] });
            const bool entered = gate->Started();
            const auto before = operation.Snapshot();
            if (*progress_from_old) { (*progress_from_old)({ 999999, 999999 }); }
            const auto after = operation.Snapshot();
            passed &= Check(first != 0 && first_done && second > first && entered && before.copy_batch && after.copy_batch &&
                after.operation_id == second && before.copy_progress.total_bytes == after.copy_progress.total_bytes &&
                before.copy_progress.transferred_bytes == after.copy_progress.transferred_bytes &&
                after.copy_batch->copied_count == 0 && after.copy_batch->remaining_count == 2,
                "file_copy_batch_old_operation_progress_cannot_mutate_a_new_running_queue_or_forge_aggregate_completion");
            gate->Release();
            passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().copy_batch &&
                operation.Snapshot().copy_batch->copied_count == 2 && fixture.DestinationMatches(1) && fixture.DestinationMatches(2),
                "file_copy_batch_new_generation_finishes_normally_after_rejected_stale_progress");
        }
    }
    catch (const std::exception& exception)
    {
        std::cout << "INFO file_copy_batch_fixture_exception " << exception.what() << '\n';
        passed &= Check(false, "file_copy_batch_owned_fixture_completed_without_exception");
    }
    return passed;
}
