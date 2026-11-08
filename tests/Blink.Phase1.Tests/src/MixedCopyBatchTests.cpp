#include "MixedCopyBatchTests.h"
#include "Blink/App/CopyTransferPresentation.h"
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

namespace
{
    using namespace blink::app;
    namespace fs = std::filesystem;
    bool Check(bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        const auto done = std::make_shared<std::promise<Status>>(); auto future = done->get_future();
        action.Completed([done](auto&&, Status value) { done->set_value(value); });
        return future.wait_for(std::chrono::seconds(8)) == std::future_status::ready && future.get() == Status::Completed;
    }
    struct Gate
    {
        std::promise<void> entered, released;
        std::future<void> started{ entered.get_future() };
        std::shared_future<void> proceed{ released.get_future().share() };
        std::atomic_bool signalled{}, finished{};
        void Enter()
        {
            if (!signalled.exchange(true)) { entered.set_value(); }
            if (proceed.wait_for(std::chrono::seconds(8)) != std::future_status::ready) { throw std::runtime_error("mixed gate timeout"); }
        }
        bool Started() { return started.wait_for(std::chrono::seconds(8)) == std::future_status::ready; }
        void Release() noexcept { if (!finished.exchange(true)) { try { released.set_value(); } catch (...) {} } }
        ~Gate() { Release(); }
    };
    blink::core::FileEntry Entry(const fs::path& path)
    {
        blink::core::FileEntry entry;
        if (!blink::core::ReadEntryMetadata(path.wstring(), entry)) { throw std::runtime_error("mixed metadata"); }
        entry.name = path.filename().wstring(); entry.full_path = path.wstring(); return entry;
    }
    void Write(const fs::path& path, const std::string& data)
    { std::ofstream file(path, std::ios::binary); file << data; if (!file) { throw std::runtime_error("mixed write"); } }
    std::string Read(const fs::path& path)
    { std::ifstream file(path, std::ios::binary); return { std::istreambuf_iterator<char>(file), {} }; }
    struct Fixture
    {
        fs::path root, source, target;
        FileCopyRequest file;
        DirectoryCopyRequest a, b;
        explicit Fixture(const fs::path& suite, const wchar_t* name) : root(suite / name), source(root / L"source"), target(root / L"target")
        {
            fs::create_directories(source / L"资料.v2" / L"nested"); fs::create_directory(source / L"empty"); fs::create_directory(target);
            Write(source / L"file.txt", "file-data"); Write(source / L"资料.v2" / L"nested" / L"data.bin", "nested-data");
            Write((source / L"资料.v2" / L"nested" / L"data.bin").wstring() + L":Zone.Identifier:$DATA", "zone-data");
            file = { source.wstring(), Entry(source).identity, Entry(source / L"file.txt"), target.wstring(), Entry(target).identity, L"file.txt" };
            for (auto* request : { &a, &b })
            {
                request->source_parent = source.wstring(); request->source_parent_identity = file.source_parent_identity;
                request->source = Entry(source / (request == &a ? L"资料.v2" : L"empty"));
                request->destination_parent = target.wstring(); request->destination_parent_identity = file.destination_parent_identity;
                request->destination_name = request->source.name;
            }
        }
        CopyItemRequests Items() const { return { file, a, b }; }
        bool Preserved() const
        {
            return Entry(source / L"file.txt").identity == file.source.identity && Read(source / L"file.txt") == "file-data" &&
                Entry(source / a.source.name).identity == a.source.identity && Entry(source / b.source.name).identity == b.source.identity &&
                Read(source / a.source.name / L"nested" / L"data.bin") == "nested-data" &&
                Read((source / a.source.name / L"nested" / L"data.bin").wstring() + L":Zone.Identifier:$DATA") == "zone-data";
        }
        bool CopiedA(const std::wstring& name) const
        { return Read(target / name / L"nested" / L"data.bin") == "nested-data" &&
            Read((target / name / L"nested" / L"data.bin").wstring() + L":Zone.Identifier:$DATA") == "zone-data"; }
    };
}

bool RunMixedCopyBatchTests()
{
    using namespace blink::app;
    bool passed = true;
    const auto root = fs::absolute(fs::path(L".build") / L"mixed-copy-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    fs::create_directories(root);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{ root };
    try
    {
        {
            Fixture f(root, L"capture"); ClipboardCopySources sources, bound;
            const std::vector<std::wstring> paths{ (f.source / f.a.source.name).wstring(), (f.source / L"file.txt").wstring(),
                (f.source / f.b.source.name).wstring(), (f.source / f.a.source.name).wstring() };
            const auto error = CaptureClipboardCopySources(paths, sources); const auto* items = std::get_if<CopyItemRequests>(&sources);
            passed &= Check(error == 0 && items && items->size() == 4 && std::holds_alternative<DirectoryCopyRequest>((*items)[0]) &&
                std::holds_alternative<FileCopyRequest>((*items)[1]) && std::get<DirectoryCopyRequest>((*items)[3]).source.identity == f.a.source.identity &&
                SameCapturedCopySources(sources, sources) && f.Preserved(), "mixed_capture_order_duplicates_and_real_identities");
            const auto prepare = PrepareCopyDestination(sources, f.target.wstring(), f.file.destination_parent_identity, bound);
            const auto* ready = std::get_if<CopyItemRequests>(&bound);
            passed &= Check(prepare == 0 && ready && ready->size() == 4 &&
                std::get<DirectoryCopyRequest>((*ready)[0]).destination_parent_identity == f.file.destination_parent_identity &&
                std::get<FileCopyRequest>((*ready)[1]).destination_name == L"file.txt", "mixed_destination_binding_keeps_per_item_types_and_ids");
            ClipboardCopyCache cache{ sources, paths, 321 }; ClipboardFilePaths payload{ ERROR_SUCCESS, paths, 321 };
            passed &= Check(cache.MatchesPayload(payload), "mixed_clipboard_cache_matches_exact_count_sequence_and_payload");
            payload.paths.pop_back(); passed &= Check(!cache.MatchesPayload(payload), "mixed_clipboard_cache_rejects_missing_duplicate_path");
            if (auto* values = std::get_if<CopyItemRequests>(&bound))
            {
                auto changed = sources; auto& request = std::get<DirectoryCopyRequest>(std::get<CopyItemRequests>(changed)[0]);
                request.source.identity.file_id[0] ^= 1;
                passed &= Check(!SameCapturedCopySources(sources, changed), "mixed_cache_changed_directory_identity_is_not_reclassified_as_external");
            }
            const auto bad = CaptureClipboardCopySources(std::vector<std::wstring>{ paths[0], paths[1], (f.source / L"missing").wstring() }, sources);
            passed &= Check(bad == ERROR_FILE_NOT_FOUND && std::holds_alternative<std::monostate>(sources) && f.Preserved(),
                "mixed_capture_invalid_later_source_clears_entire_typed_output");
        }
        {
            Fixture f(root, L"ordered"); std::vector<std::wstring> calls; std::atomic_uint active{}, maximum{};
            const auto record = [&](const auto& request, auto&& execute) {
                const auto count = ++active; maximum.store((std::max)(maximum.load(), count)); calls.push_back(request.source.name);
                auto result = execute(); --active; return result;
            };
            FileOperationCoordinator operation({}, [&](const auto& request, const auto& cancel, const auto& progress) {
                return record(request, [&] { return CopyFileSafely(request, cancel, progress); });
            }, [&](const auto& request, const auto& cancel, const auto& progress) {
                return record(request, [&] { return CopyDirectorySafely(request, cancel, progress); });
            });
            const auto id = operation.CopyItems(f.Items()); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Completed && value.copy_batch &&
                value.copy_batch->copied_count == 3 && value.copy_batch->remaining_count == 0 && maximum == 1 &&
                calls == std::vector<std::wstring>{ L"file.txt", f.a.source.name, f.b.source.name } &&
                f.CopiedA(f.a.source.name) && Read(f.target / L"file.txt") == "file-data" && fs::is_directory(f.target / f.b.source.name) && f.Preserved(),
                "mixed_queue_real_files_two_directories_ads_and_empty_tree_are_serial_and_ordered");
        }
        {
            Fixture f(root, L"skip"); fs::create_directory(f.target / f.a.source.name); Write(f.target / f.a.source.name / L"keep", "incumbent");
            const auto incumbent = Entry(f.target / f.a.source.name).identity;
            std::atomic_uint files{}; FileOperationCoordinator operation({}, [&](const auto& request, const auto& cancel, const auto& progress) {
                ++files; return CopyFileSafely(request, cancel, progress);
            });
            const auto id = operation.CopyItems(f.Items()); const bool stopped = Finished(operation.CurrentAction()); const auto before = operation.Snapshot();
            passed &= Check(id && stopped && before.copy_batch && before.copy_batch->copied_count == 1 && before.copy_batch->remaining_count == 1 &&
                before.copy_batch->current_index == 1 && before.copy_batch->resumable && before.directory_copy_result &&
                !before.directory_copy_result->partial_tree_left && operation.HasPendingCopyBatch() &&
                operation.CreateFolder({ f.target.wstring(), f.file.destination_parent_identity, L"blocked" }) == 0,
                "mixed_queue_directory_root_conflict_stops_and_protects_pending_tail");
            const auto resumed = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Skip); const bool done = Finished(operation.CurrentAction());
            const auto after = operation.Snapshot();
            passed &= Check(resumed > id && done && after.copy_batch && after.copy_batch->copied_count == 2 && after.copy_batch->skipped_count == 1 &&
                files == 1 && Entry(f.target / f.a.source.name).identity == incumbent && Read(f.target / f.a.source.name / L"keep") == "incumbent" &&
                fs::is_directory(f.target / f.b.source.name) && f.Preserved(), "mixed_queue_skip_directory_continues_without_recopying_committed_file");
        }
        {
            Fixture f(root, L"duplicate"); FileOperationCoordinator operation;
            const auto id = operation.CopyItems({ f.a, f.a, f.file }); const bool stopped = Finished(operation.CurrentAction());
            const auto resumed = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::KeepBoth); const bool done = Finished(operation.CurrentAction());
            const auto value = operation.Snapshot(); const auto second = f.a.source.name + L" (2)";
            passed &= Check(id && stopped && resumed > id && done && value.copy_batch && value.copy_batch->copied_count == 3 &&
                value.copy_batch->items[1].directory_result && value.copy_batch->items[1].directory_result->path == (f.target / second).wstring() &&
                f.CopiedA(f.a.source.name) && f.CopiedA(second) && Entry(f.target / second).identity != Entry(f.target / f.a.source.name).identity && f.Preserved(),
                "mixed_queue_duplicate_directory_keep_both_has_distinct_roots_and_preserves_tail");
        }
        for (const bool keep_both : { false, true })
        {
            Fixture f(root, keep_both ? L"partial-retry" : L"partial-skip"); std::atomic_bool injected{};
            FileOperationCoordinator operation({}, {}, [&](const auto& request, const auto& cancel, const auto& progress) {
                return CopyDirectorySafely(request, cancel, [&](const auto& value) {
                    if (value.directories_created == 1 && !injected.exchange(true)) { Write(f.target / request.destination_name / L"nested", "rival"); }
                    if (progress) { progress(value); }
                });
            });
            const auto id = operation.CopyItems({ f.a, f.file }); const bool stopped = Finished(operation.CurrentAction()); const auto before = operation.Snapshot();
            passed &= Check(id && stopped && before.directory_copy_result && before.directory_copy_result->partial_tree_left &&
                before.directory_copy_result->directories_created == 1 && before.copy_batch && before.copy_batch->partial_count == 1 &&
                !before.result.partial_file_left && before.copy_batch->items[1].stage == FileCopyBatchItemStage::Pending &&
                Read(f.target / f.a.source.name / L"nested") == "rival", "mixed_queue_partial_tree_is_independent_of_file_partial_and_tail_pending");
            const auto resumed = operation.ResumeCopyBatch(id, keep_both ? FileCopyConflictPolicy::KeepBoth : FileCopyConflictPolicy::Skip);
            const bool done = Finished(operation.CurrentAction()); const auto after = operation.Snapshot();
            passed &= Check(resumed > id && done && after.stage == FileOperationStage::Completed && after.copy_batch &&
                after.copy_batch->partial_count == 1 && after.copy_batch->items[0].directory_partial_attempts == 1 &&
                after.copy_batch->items[0].previous_directory_results.size() == 1 && !after.result.partial_file_left &&
                Read(f.target / f.a.source.name / L"nested") == "rival" && Read(f.target / L"file.txt") == "file-data" &&
                (!keep_both || f.CopiedA(f.a.source.name + L" (2)")) && f.Preserved(),
                "mixed_queue_explicit_retry_retains_prior_partial_root_and_count_without_rollback");
        }
        {
            Fixture f(root, L"cancel-root"); FileOperationCoordinator* current{};
            FileOperationCoordinator operation({}, {}, [&](const auto& request, const auto& cancel, const auto& progress) {
                return CopyDirectorySafely(request, cancel, [&](const auto& value) {
                    if (progress) { progress(value); } if (value.directories_created == 1) { current->Cancel(); }
                });
            }); current = &operation;
            const auto id = operation.CopyItems(f.Items()); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Cancelled && value.copy_batch && value.copy_batch->copied_count == 1 &&
                value.copy_batch->cancelled_count == 1 && value.copy_batch->remaining_count == 1 && value.copy_batch->partial_count == 1 &&
                !value.result.partial_file_left && fs::is_directory(f.target / f.a.source.name) && !fs::exists(f.target / f.b.source.name) && f.Preserved(),
                "mixed_queue_cancel_after_empty_root_preserves_committed_prefix_and_untouched_tail");
        }
        {
            Fixture f(root, L"late-cancel"); FileOperationCoordinator* current{};
            FileOperationCoordinator operation({}, {}, [&](const auto& request, const auto& cancel, const auto& progress) {
                return CopyDirectorySafely(request, cancel, [&](const auto& value) {
                    if (progress) { progress(value); } if (value.completed && request.source.identity == f.b.source.identity) { current->Cancel(); }
                });
            }); current = &operation;
            const auto id = operation.CopyItems(f.Items()); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Completed && value.copy_batch && value.copy_batch->copied_count == 3 &&
                value.copy_batch->cancelled_count == 0 && value.copy_batch->remaining_count == 0 && f.Preserved(),
                "mixed_queue_last_directory_commit_wins_over_late_cancel");
        }
        {
            Fixture f(root, L"later-source-change"); const auto moved = f.source / L"preserved-original";
            FileOperationCoordinator operation({}, [&](const auto& request, const auto& cancel, const auto& progress) {
                auto result = CopyFileSafely(request, cancel, progress); fs::rename(f.source / f.a.source.name, moved);
                fs::create_directory(f.source / f.a.source.name); return result;
            });
            const auto id = operation.CopyItems(f.Items()); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Failed && value.result.error == ERROR_FILE_INVALID && value.copy_batch &&
                value.copy_batch->copied_count == 1 && value.copy_batch->remaining_count == 1 && !fs::exists(f.target / f.a.source.name) &&
                Entry(moved).identity == f.a.source.identity && Read(moved / L"nested" / L"data.bin") == "nested-data" &&
                Read(f.target / L"file.txt") == "file-data", "mixed_queue_later_source_identity_replacement_stops_without_undoing_prefix");
        }
        for (unsigned invalid = 0; invalid < 4; ++invalid)
        {
            Fixture f(root, (L"invalid-tail-" + std::to_wstring(invalid)).c_str()); auto a = f.a;
            if (invalid == 0) { a.source.is_directory = false; }
            if (invalid == 1) { a.destination_parent_identity.file_id[0] ^= 1; }
            if (invalid == 2) { a.limits.max_entries = MaximumDirectoryCopyEntries + 1; }
            if (invalid == 3) { a.limits.max_directories = 0; }
            std::atomic_uint calls{}; FileOperationCoordinator operation({}, [&](const auto&, const auto&, const auto&) { ++calls; return FileCopyResult{}; });
            const auto id = operation.CopyItems({ f.file, a }); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Failed && calls == 0 && value.copy_batch &&
                value.copy_batch->copied_count == 0 && fs::is_empty(f.target) && f.Preserved(), "mixed_queue_structurally_invalid_later_directory_rejects_before_any_write");
        }
        {
            Fixture f(root, L"stale-item"); const auto gate = std::make_shared<Gate>(); DirectoryCopyProgressCallback stale;
            FileOperationCoordinator operation({}, {}, [&](const auto& request, const auto& cancel, const auto& progress) {
                if (request.source.identity == f.a.source.identity) { stale = progress; }
                else { gate->Enter(); }
                return CopyDirectorySafely(request, cancel, progress);
            });
            const auto id = operation.CopyItems({ f.a, f.b }); const bool entered = gate->Started(); const auto before = operation.Snapshot();
            if (stale) { stale({ 3, 2, L"stale-source", L"stale-destination", { 999, 999 }, true }); }
            const auto after = operation.Snapshot();
            passed &= Check(id && entered && before.copy_batch && before.copy_batch->current_index == 1 &&
                after.directory_copy_progress.current_destination_path == before.directory_copy_progress.current_destination_path &&
                after.directory_copy_progress.directories_created == 0 && !after.directory_copy_progress.completed,
                "mixed_queue_previous_directory_callback_cannot_advance_current_item");
            gate->Release(); passed &= Check(Finished(operation.CurrentAction()) && operation.Snapshot().copy_batch->copied_count == 2 && f.Preserved(),
                "mixed_queue_new_directory_completes_after_rejected_stale_callback");
        }
        {
            Fixture f(root, L"shutdown"); const auto gate = std::make_shared<Gate>(); std::atomic_uint publications{};
            FileOperationCoordinator operation({}, {}, [gate](const auto& request, const auto& cancel, const auto& progress) {
                gate->Enter(); return CopyDirectorySafely(request, cancel, progress);
            });
            const auto id = operation.CopyItems({ f.a, f.file }, [&](const auto&) { ++publications; });
            const bool entered = gate->Started(); operation.Shutdown(); const auto count = publications.load(); gate->Release();
            const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && entered && done && value.closed && value.stage == FileOperationStage::Cancelled && publications == count &&
                value.copy_batch && value.copy_batch->remaining_count == 1 && fs::is_empty(f.target) && f.Preserved(),
                "mixed_queue_shutdown_suppresses_publication_and_leaves_pending_file_untouched");
        }
        {
            Fixture f(root, L"invalid-skip"); FileOperationCoordinator operation({}, {}, [](const auto& request, const auto& cancel, const auto& progress) {
                auto result = CopyDirectorySafely(request, cancel, progress); result.copied = false; result.skipped = true; return result;
            });
            const auto id = operation.CopyItems({ f.a, f.file }); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Failed && value.directory_copy_result &&
                !value.directory_copy_result->skipped && value.directory_copy_result->partial_tree_left && value.result.error == ERROR_GEN_FAILURE &&
                !value.result.partial_file_left && value.copy_batch && value.copy_batch->partial_count == 1 &&
                value.copy_batch->items[1].stage == FileCopyBatchItemStage::Pending && f.CopiedA(f.a.source.name) && f.Preserved(),
                "mixed_queue_invalid_skip_cannot_hide_real_directory_effects");
        }
        {
            Fixture f(root, L"executor-exception"); const auto actual = f.target / L"资料.v2 (2)";
            FileOperationCoordinator operation({}, {}, [&](const auto& request, const auto&, const DirectoryCopyProgressCallback& progress) -> DirectoryCopyResult {
                fs::create_directory(actual);
                progress({ 0, 1, request.source.full_path, actual.wstring(), {}, false });
                throw std::runtime_error("owned executor exception after root creation");
            });
            const auto id = operation.CopyItems({ f.a, f.file }); const bool done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(id && done && value.stage == FileOperationStage::Failed && value.result.error == ERROR_GEN_FAILURE &&
                value.directory_copy_result && value.directory_copy_result->partial_tree_left &&
                value.directory_copy_result->directories_created == 1 && value.directory_copy_result->path == actual.wstring() &&
                !value.result.partial_file_left && value.copy_batch && value.copy_batch->partial_count == 1 &&
                value.copy_batch->items[1].stage == FileCopyBatchItemStage::Pending && fs::is_directory(actual) && f.Preserved(),
                "mixed_queue_executor_exception_preserves_observed_actual_root_and_tree_facts");
        }
        {
            Fixture f(root, L"terminal-action"); const auto gate = std::make_shared<Gate>();
            FileOperationCoordinator operation;
            const auto id = operation.CopyItems(f.Items(), [gate](const auto& value) {
                if (value.stage == FileOperationStage::Completed) { gate->Enter(); }
            });
            const bool entered = gate->Started(); const auto value = operation.Snapshot();
            passed &= Check(id && entered && value.stage == FileOperationStage::Completed && value.copy_batch &&
                value.copy_batch->copied_count == 3 && operation.IsBusy() && operation.CopyItems({ f.b }) == 0 &&
                operation.ResumeCopyBatch(id, FileCopyConflictPolicy::KeepBoth) == 0,
                "mixed_queue_terminal_publication_does_not_release_live_native_action");
            gate->Release(); passed &= Check(Finished(operation.CurrentAction()) && !operation.IsBusy() && f.Preserved(),
                "mixed_queue_actual_action_exit_releases_admission_without_changing_commit");
        }
        {
            Fixture f(root, L"conflict-presentation"); FileOperationSnapshot value;
            value.kind = FileOperationKind::Copy; value.stage = FileOperationStage::Failed;
            value.copy_request = FileCopyProjection(f.a); value.directory_copy_request = f.a;
            value.directory_copy_result = DirectoryCopyResult{ ERROR_ALREADY_EXISTS }; value.result.error = ERROR_ALREADY_EXISTS;
            passed &= Check(CopyQueueHasRootConflict(value), "mixed_directory_zero_effect_root_collision_allows_decision");
            value.directory_copy_result->directories_created = 1;
            passed &= Check(!CopyQueueHasRootConflict(value), "mixed_directory_any_creation_prevents_automatic_root_conflict_retry");
            value.directory_copy_result->directories_created = 0; value.result.error = ERROR_INVALID_PARAMETER;
            passed &= Check(!CopyQueueHasRootConflict(value), "mixed_directory_invalid_descendant_destination_is_not_a_name_conflict");
            value.directory_copy_request->destination_parent_identity = value.directory_copy_request->source_parent_identity;
            passed &= Check(CopyQueueHasRootConflict(value), "mixed_directory_same_parent_same_name_can_offer_keep_both");
        }
        {
            Fixture f(root, L"history-bound"); FileOperationCoordinator operation({}, {}, [](const auto& request, const auto& cancel, const auto& progress) {
                if (request.conflict_policy == FileCopyConflictPolicy::Skip) { return CopyDirectorySafely(request, cancel, progress); }
                const auto path = fs::path(request.destination_parent) / request.destination_name; fs::create_directory(path);
                DirectoryCopyResult result; result.error = ERROR_ACCESS_DENIED; result.partial_tree_left = true;
                result.directories_created = 1; result.path = path.wstring(); return result;
            });
            auto id = operation.CopyItems({ f.a, f.file }); bool done = Finished(operation.CurrentAction());
            for (unsigned n = 1; n < 40 && done; ++n)
            {
                id = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Fail, L"partial-" + std::to_wstring(n));
                done = id && Finished(operation.CurrentAction());
            }
            const auto before = operation.Snapshot();
            passed &= Check(done && before.copy_batch && before.copy_batch->items[0].directory_partial_attempts == 40 &&
                before.copy_batch->items[0].previous_directory_results.size() == 32 && before.copy_batch->partial_count == 1 &&
                !before.result.partial_file_left, "mixed_queue_directory_partial_history_is_bounded_with_truthful_attempt_count");
            const auto resume = operation.ResumeCopyBatch(id, FileCopyConflictPolicy::Skip); done = Finished(operation.CurrentAction()); const auto value = operation.Snapshot();
            passed &= Check(resume > id && done && value.stage == FileOperationStage::Completed && value.copy_batch &&
                value.copy_batch->items[0].directory_partial_attempts == 40 && value.copy_batch->partial_count == 1 &&
                fs::is_directory(f.target / L"partial-39") && f.Preserved(), "mixed_queue_skip_after_many_partial_attempts_keeps_history_and_completes_tail");
        }
    }
    catch (const std::exception& exception)
    { std::cout << "INFO mixed_copy_fixture_exception " << exception.what() << '\n'; passed &= Check(false, "mixed_copy_owned_fixture_no_exception"); }
    return passed;
}
