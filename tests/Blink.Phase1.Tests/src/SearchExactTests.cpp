#include "SearchExactTests.h"
#include "Blink/App/SearchCoordinator.h"
#include "Blink/App/SearchViewModel.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>

namespace
{
    namespace ipc = blink::index::ipc;
    bool Check(bool condition, const char* name)
    { std::cout << (condition ? "PASS " : "FAIL ") << name << '\n'; return condition; }
    ipc::SearchHit Hit(std::wstring name)
    {
        ipc::SearchHit hit; hit.identity = { 77, { 9 }, true }; hit.parent_identity = { 77, { 7 }, true };
        hit.name = std::move(name); hit.full_path = L"D:\\Folder\\" + hit.name; hit.attributes = FILE_ATTRIBUTE_NORMAL;
        return hit;
    }
    blink::app::IndexRpcResult Reply(std::vector<ipc::SearchHit> results)
    {
        blink::app::IndexRpcResult reply; reply.received = true; reply.expected_request_id = 42;
        reply.response.request_id = 42; reply.response.status = ipc::ResponseStatus::Ok;
        reply.response.error = ERROR_SUCCESS; reply.response.service_state = ipc::ServiceState::Running;
        reply.response.index_state = ipc::IndexState::Healthy; reply.response.results = std::move(results);
        return reply;
    }
    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        const auto completion = std::make_shared<std::promise<Status>>(); auto future = completion->get_future();
        action.Completed([completion](auto&&, Status status) { completion->set_value(status); });
        return future.wait_for(std::chrono::seconds(5)) == std::future_status::ready && future.get() == Status::Completed;
    }
    bool RunScopeQueryTests()
    {
        using namespace blink::app;
        bool passed = true;
        std::vector<std::wstring> roots;
        passed &= Check(ParseSearchVolumeRoots(L"C:;D:", roots) == ERROR_SUCCESS &&
            roots == std::vector<std::wstring>{ L"C:\\", L"D:\\" },
            "search_scope_parses_explicit_drive_colon_list");
        bool alternate_forms = true;
        for (const auto& value : { L"c,d,C", L"C D", L" C:\\;d:;C: " })
        {
            alternate_forms &= ParseSearchVolumeRoots(value, roots) == ERROR_SUCCESS &&
                roots == std::vector<std::wstring>{ L"C:\\", L"D:\\" };
        }
        passed &= Check(alternate_forms, "search_scope_drive_formats_normalize_case_and_deduplicate");
        passed &= Check(ParseSearchVolumeRoots(L"d:\\", roots) == ERROR_SUCCESS &&
            roots == std::vector<std::wstring>{ L"D:\\" }, "search_scope_single_explicit_volume_stays_single");
        bool invalid_rejected = true;
        for (const auto& value : { L"", L" ;,  ", L"D:\\Folder", L"\\\\host\\share", L"1:", L"C::", L"C:/", L"C:;D:\\Folder" })
        {
            roots = { L"Z:\\" };
            invalid_rejected &= ParseSearchVolumeRoots(value, roots) != ERROR_SUCCESS && roots.empty();
        }
        passed &= Check(invalid_rejected, "search_scope_invalid_drive_input_does_not_keep_a_partial_or_previous_list");

        SearchQuery query{ L"needle", SearchScope::FollowLocation };
        passed &= Check(ValidateSearchQuery(query) != ERROR_SUCCESS,
            "search_scope_unresolved_follow_location_never_silently_searches_all_volumes");
        query.scope = SearchScope::SelectedVolumes; query.selected_volumes = L"c;D:";
        passed &= Check(ValidateSearchQuery(query) == ERROR_SUCCESS,
            "search_scope_selected_volumes_query_accepts_explicit_roots");
        query.selected_volumes = L"D:\\Folder";
        passed &= Check(ValidateSearchQuery(query) != ERROR_SUCCESS,
            "search_scope_selected_volumes_query_rejects_directory_as_drive_list");
        query.scope = SearchScope::SelectedFolder; query.folder = L"\\\\host\\share";
        passed &= Check(ValidateSearchQuery(query) != ERROR_SUCCESS,
            "search_scope_selected_folder_rejects_unsupported_network_location");
        query.folder = L"D:\\needle-folder"; query.include_subfolders = true;
        const auto recursive = MakeSearchRequest(query, L"D:\\");
        passed &= Check(ValidateSearchQuery(query) == ERROR_SUCCESS && recursive.type == ipc::RequestType::Search &&
            recursive.volume_root == L"D:\\" && recursive.path_prefix == L"D:\\needle-folder\\",
            "search_scope_recursive_selected_folder_uses_bounded_directory_prefix");
        query.include_subfolders = false;
        const auto direct = MakeSearchRequest(query, L"D:\\");
        passed &= Check(direct.type == ipc::RequestType::SearchDirectChildren && static_cast<unsigned>(direct.type) == 7 &&
            direct.path_prefix == recursive.path_prefix && direct.limit == query.limit &&
            direct.protocol_version == ipc::kProtocolVersion && static_cast<unsigned>(SearchScope::AllLocalVolumes) == 0 &&
            static_cast<unsigned>(SearchScope::CurrentFolder) == 1,
            "search_scope_direct_children_use_server_filter_before_limit_with_existing_wire_layout");
        const auto at = [](const std::wstring& path, const std::uint8_t id) {
            auto hit = Hit(path.substr(path.find_last_of(L'\\') + 1)); hit.full_path = path;
            hit.identity.file_id[0] = id; return hit;
        };
        const auto reply = Reply({ at(L"D:\\needle-folder\\needle.txt", 9),
            at(L"D:\\needle-folder\\deep\\needle.txt", 10), at(L"D:\\needle-folder-old\\needle.txt", 11) });
        auto volume = MapSearchReply(query, L"D:\\", reply);
        passed &= Check(volume.searched && volume.results.size() == 1 &&
            volume.results.front().hit.full_path == L"D:\\needle-folder\\needle.txt",
            "search_scope_direct_mapping_excludes_nested_and_same_prefix_sibling_paths");
        query.include_subfolders = true; volume = MapSearchReply(query, L"D:\\", reply);
        passed &= Check(volume.searched && volume.results.size() == 2 &&
            std::none_of(volume.results.begin(), volume.results.end(), [](const auto& result) {
                return result.hit.full_path.starts_with(L"D:\\needle-folder-old");
            }), "search_scope_recursive_mapping_keeps_descendants_without_sibling_prefix_leakage");
        query.include_subfolders = false; query.folder = L"D:\\";
        volume = MapSearchReply(query, L"D:\\", Reply({ at(L"D:\\needle.txt", 9), at(L"D:\\deep\\needle.txt", 10) }));
        passed &= Check(volume.searched && volume.results.size() == 1 && volume.results.front().hit.full_path == L"D:\\needle.txt",
            "search_scope_direct_drive_root_keeps_only_immediate_children");
        query.folder = L"D:\\needle-folder"; query.mode = SearchMode::Exact; query.text = L"needle.txt";
        const auto exact = MakeSearchRequest(query, L"D:\\");
        passed &= Check(exact.type == ipc::RequestType::SearchDirectChildren && exact.prefix && exact.limit == kSearchCandidateLimit,
            "search_scope_exact_direct_query_keeps_exact_candidate_semantics");
        query.scope = SearchScope::CurrentVolume; query.folder = L"D:\\";
        passed &= Check(!MapSearchReply(query, L"C:\\", Reply({ at(L"C:\\needle.txt", 9) })).searched,
            "search_scope_current_volume_rejects_results_labelled_as_another_drive");
        query.scope = SearchScope::SelectedVolumes; query.selected_volumes = L"D:";
        passed &= Check(!MapSearchReply(query, L"C:\\", Reply({ at(L"C:\\needle.txt", 9) })).searched,
            "search_scope_selected_volumes_reject_unselected_drive_replies");

        query.scope = SearchScope::SelectedFolder; query.folder = L"D:\\needle-folder"; query.include_subfolders = false;
        SearchState state; const auto generation = state.Begin(query);
        SearchSnapshot snapshot; snapshot.query = query; snapshot.stage = SearchStage::Complete;
        const bool committed = state.Complete(generation, snapshot);
        auto changed = query; changed.folder = L"D:\\different";
        const bool directory_fixed = !state.Begin(changed, true);
        changed = query; changed.include_subfolders = true;
        const bool depth_fixed = !state.Begin(changed, true);
        passed &= Check(committed && directory_fixed && depth_fixed && state.Begin(query, true) > generation,
            "search_scope_retry_preserves_submitted_directory_and_recursion_condition");
        return passed;
    }
}
bool RunSearchExactTests()
{
    using namespace blink::app;
    bool passed = true;
    passed &= RunScopeQueryTests();
    const auto exact = SearchMode::Exact;
    SearchQuery query{ L"target.txt", SearchScope::AllLocalVolumes, exact, {}, 200 };
    passed &= Check(ValidateSearchQuery(query) == ERROR_SUCCESS, "search_exact_query_uses_app_private_mode");
    const auto request = MakeSearchRequest(query, L"D:\\");
    passed &= Check(request.type == ipc::RequestType::Search && request.prefix && request.limit == 1000 &&
        request.text == query.text && request.protocol_version == ipc::kProtocolVersion,
        "search_exact_uses_existing_prefix_wire_contract_and_candidate_cap");
    auto reply = Reply({ Hit(L"target.txt"), Hit(L"target.txt.bak"), Hit(L"TARGET.TXT") });
    auto volume = MapSearchReply(query, L"D:\\", reply);
    passed &= Check(volume.searched && volume.results.size() == 2 && !volume.query_incomplete,
        "search_exact_filters_prefix_only_names_and_keeps_case_matches");
    reply = Reply({ Hit(L"target.txt.bak") });
    volume = MapSearchReply(query, L"D:\\", reply);
    passed &= Check(volume.searched && volume.results.empty() && !volume.query_incomplete,
        "search_exact_short_candidate_set_can_confirm_known_miss");
    std::vector<ipc::SearchHit> crowded;
    for (unsigned index = 0; index < 1000; ++index) { crowded.push_back(Hit(L"target.txt-" + std::to_wstring(index))); }
    reply = Reply(crowded); volume = MapSearchReply(query, L"D:\\", reply);
    passed &= Check(volume.searched && volume.results.empty() && volume.query_incomplete,
        "search_exact_full_candidate_set_is_not_a_complete_known_miss");
    reply.response.results.back() = Hit(L"target.txt");
    volume = MapSearchReply(query, L"D:\\", reply);
    passed &= Check(volume.searched && volume.results.size() == 1 && volume.query_incomplete,
        "search_exact_checks_candidates_beyond_display_limit");
    query.limit = 1;
    reply = Reply({ Hit(L"target.txt"), Hit(L"TARGET.TXT") });
    volume = MapSearchReply(query, L"D:\\", reply);
    passed &= Check(volume.results.size() == 1 && volume.limit_reached && !volume.query_incomplete,
        "search_exact_display_cap_is_separate_from_candidate_truncation");
    query.limit = 200;
    reply = Reply({ Hit(L"target.txt"), Hit(L"target.txt") });
    reply.response.results[1].parent_identity.file_id[0] = 8;
    reply.response.results[1].full_path = L"D:\\Other\\target.txt";
    passed &= Check(MapSearchReply(query, L"D:\\", reply).results.size() == 2,
        "search_exact_hardlinks_in_different_parents_remain_independent");
    reply = Reply(crowded); reply.response.results.push_back(Hit(L"target.txt"));
    passed &= Check(!MapSearchReply(query, L"D:\\", reply).searched &&
        MapSearchReply(query, L"D:\\", reply).error == ERROR_INVALID_DATA,
        "search_exact_rejects_payload_exceeding_wire_candidate_limit");
    query.text = L"文件.txt";
    volume = MapSearchReply(query, L"D:\\", Reply({ Hit(L"文件.txt"), Hit(L"文件.txt副本") }));
    passed &= Check(volume.results.size() == 1 && volume.results[0].hit.name == query.text,
        "search_exact_preserves_unicode_full_name");

    query.text = L"target.txt";
    const auto state = std::make_shared<SearchState>();
    SearchCoordinator coordinator(state, [crowded](auto) { return Reply(crowded); },
        [] { return std::vector<std::wstring>{ L"D:\\" }; },
        [](const auto& root) { return SearchFolderScope{ root, { 77, { 1 }, true }, ERROR_SUCCESS }; });
    const auto completion = std::make_shared<std::promise<void>>(); auto future = completion->get_future();
    (void)coordinator.Search(query, [completion](auto) { completion->set_value(); });
    const auto done = future.wait_for(std::chrono::seconds(5)) == std::future_status::ready && Finished(coordinator.CurrentAction());
    const auto snapshot = state->Snapshot();
    passed &= Check(done && snapshot.stage == SearchStage::Partial && snapshot.error == ERROR_MORE_DATA &&
        snapshot.volumes.size() == 1 && snapshot.volumes[0].query_incomplete,
        "search_exact_coordinator_publishes_partial_instead_of_empty_complete");
    const auto view = BuildSearchView(snapshot, {}, {});
    passed &= Check(view.query_incomplete && view.rows.empty(),
        "search_exact_view_preserves_incomplete_marker_with_zero_visible_rows");
    (void)coordinator.Shutdown();
    return passed;
}

bool RunSearchResponseTests()
{
    using namespace blink::app;
    bool passed = true;
    const SearchQuery query{ L"needle", SearchScope::AllLocalVolumes };
    const auto scope = [](const std::wstring& root) {
        return SearchFolderScope{ root, { 77, { 1 }, true }, ERROR_SUCCESS };
    };

    struct Trace
    {
        std::vector<std::wstring> roots;
        unsigned c_calls{}, d_calls{};
        bool search_requests_only{ true };
        std::atomic_bool retry_ready{};
    };
    const auto trace = std::make_shared<Trace>();
    const auto state = std::make_shared<SearchState>();
    const auto publications = std::make_shared<std::atomic<unsigned>>(0);
    const auto publish = [publications](auto) { ++*publications; };
    auto exchange = MakeSearchExchange(state, [trace](ipc::Request request) {
        trace->roots.push_back(request.volume_root);
        trace->search_requests_only &= request.type == ipc::RequestType::Search &&
            request.protocol_version == ipc::kProtocolVersion;
        if (request.volume_root == L"C:\\")
        {
            ++trace->c_calls;
            if (!trace->retry_ready.load())
            {
                // A mistaken inner retry fails promptly instead of keeping
                // this test waiting for a production-scale Busy budget.
                if (trace->c_calls != 1) { return IndexRpcResult{ false, ERROR_RETRY }; }
                auto reply = Reply({});
                reply.response.status = ipc::ResponseStatus::Busy;
                reply.response.error = ERROR_IO_PENDING;
                reply.response.index_state = ipc::IndexState::CatchingUp;
                return reply;
            }
            auto hit = Hit(L"needle-c.txt"); hit.full_path = L"C:\\Folder\\" + hit.name;
            return Reply({ hit });
        }
        if (request.volume_root == L"D:\\")
        {
            ++trace->d_calls;
            return Reply({ Hit(trace->d_calls == 1 ? L"needle-healthy.txt" : L"unexpected-requery.txt") });
        }
        return IndexRpcResult{ false, ERROR_INVALID_DRIVE };
    });
    SearchCoordinator coordinator(state, std::move(exchange),
        [] { return std::vector<std::wstring>{ L"C:\\", L"D:\\" }; }, scope);
    const auto first_generation = coordinator.Search(query, publish);
    const bool first_done = first_generation && Finished(coordinator.CurrentAction());
    const auto first = state->Snapshot();
    passed &= Check(first_done && trace->roots == std::vector<std::wstring>{ L"C:\\", L"D:\\" } &&
        trace->c_calls == 1 && trace->d_calls == 1 && trace->search_requests_only,
        "search_response_busy_is_returned_once_before_the_healthy_volume_request");
    passed &= Check(first_done && publications->load() == 1 && first.generation == first_generation &&
        first.stage == SearchStage::Partial && first.error == ERROR_IO_PENDING && first.volumes.size() == 2 &&
        !first.volumes[0].searched && first.volumes[0].stage == VolumeIndexStage::Unconfirmed &&
        first.volumes[0].error == ERROR_IO_PENDING && first.volumes[1].searched &&
        first.volumes[1].results.size() == 1 && first.volumes[1].results.front().hit.name == L"needle-healthy.txt",
        "search_response_busy_and_healthy_volumes_publish_one_partial_snapshot_with_results");

    trace->retry_ready.store(true);
    const auto retry_generation = coordinator.Search(query, publish, true);
    const bool retry_done = retry_generation && Finished(coordinator.CurrentAction());
    const auto retried = state->Snapshot();
    passed &= Check(first_done && retry_done && retry_generation > first_generation && publications->load() == 2 &&
        retried.stage == SearchStage::Complete && retried.error == ERROR_SUCCESS && retried.volumes.size() == 2 &&
        retried.volumes[0].searched && retried.volumes[0].results.size() == 1 &&
        retried.volumes[0].results.front().hit.name == L"needle-c.txt" && retried.volumes[1].searched,
        "search_response_explicit_retry_completes_the_previously_busy_volume");
    passed &= Check(first_done && retry_done && trace->c_calls == 2 && trace->d_calls == 1 &&
        trace->roots == std::vector<std::wstring>{ L"C:\\", L"D:\\", L"C:\\" } &&
        first.volumes.size() == 2 && first.volumes[1].results.size() == 1 &&
        retried.volumes.size() == 2 && retried.volumes[1].results.size() == 1 &&
        SameSearchLink(first.volumes[1].results.front(), retried.volumes[1].results.front()),
        "search_response_retry_reuses_successful_volume_results_without_another_transport_call");
    (void)coordinator.Shutdown();

    struct ResponseCase
    {
        const char* name;
        ipc::ResponseStatus status;
        DWORD error;
        ipc::IndexState index_state;
        DWORD expected_error;
        VolumeIndexStage expected_stage;
        bool received{ true };
    };
    const ResponseCase cases[]{
        { "search_response_pending_busy_returns_without_retry", ipc::ResponseStatus::Busy, ERROR_IO_PENDING,
            ipc::IndexState::CatchingUp, ERROR_IO_PENDING, VolumeIndexStage::Unconfirmed },
        { "search_response_busy_error_returns_without_retry", ipc::ResponseStatus::Busy, ERROR_BUSY,
            ipc::IndexState::CatchingUp, ERROR_IO_PENDING, VolumeIndexStage::Unconfirmed },
        { "search_response_busy_without_error_remains_unconfirmed", ipc::ResponseStatus::Busy, ERROR_SUCCESS,
            ipc::IndexState::Rebuilding, ERROR_IO_PENDING, VolumeIndexStage::Unconfirmed },
        { "search_response_pending_with_ok_is_not_a_successful_search", ipc::ResponseStatus::Ok, ERROR_IO_PENDING,
            ipc::IndexState::Healthy, ERROR_IO_PENDING, VolumeIndexStage::Unconfirmed },
        { "search_response_real_error_is_preserved_without_retry", ipc::ResponseStatus::Failed, ERROR_ACCESS_DENIED,
            ipc::IndexState::Unavailable, ERROR_ACCESS_DENIED, VolumeIndexStage::Unavailable },
        { "search_response_missing_store_is_not_retried_or_reported_as_healthy", ipc::ResponseStatus::NotFound, ERROR_FILE_NOT_FOUND,
            ipc::IndexState::RebuildRequired, ERROR_FILE_NOT_FOUND, VolumeIndexStage::RebuildRequired },
        { "search_response_transport_failure_remains_unavailable", ipc::ResponseStatus::Failed, ERROR_BROKEN_PIPE,
            ipc::IndexState::Unavailable, ERROR_BROKEN_PIPE, VolumeIndexStage::Unavailable, false },
    };
    for (const auto& test : cases)
    {
        const auto response_state = std::make_shared<SearchState>();
        const auto calls = std::make_shared<std::atomic<unsigned>>(0);
        const auto published = std::make_shared<std::atomic<unsigned>>(0);
        auto response_exchange = MakeSearchExchange(response_state, [calls, test](auto) {
            if (++*calls != 1) { return IndexRpcResult{ false, ERROR_RETRY }; }
            auto reply = Reply({});
            reply.received = test.received; reply.error = test.received ? ERROR_SUCCESS : test.error;
            reply.response.status = test.status; reply.response.error = test.error;
            reply.response.index_state = test.index_state;
            return reply;
        });
        SearchCoordinator response_coordinator(response_state, std::move(response_exchange),
            [] { return std::vector<std::wstring>{ L"C:\\" }; }, scope);
        const auto generation = response_coordinator.Search(query, [published](auto) { ++*published; });
        const bool done = generation && Finished(response_coordinator.CurrentAction());
        const auto snapshot = response_state->Snapshot();
        passed &= Check(done && calls->load() == 1 && published->load() == 1 &&
            snapshot.stage == SearchStage::Unavailable && snapshot.error == test.expected_error &&
            snapshot.volumes.size() == 1 && !snapshot.volumes.front().searched &&
            snapshot.volumes.front().results.empty() && snapshot.volumes.front().stage == test.expected_stage &&
            snapshot.volumes.front().error == test.expected_error, test.name);
        (void)response_coordinator.Shutdown();
    }

    auto cancelled_state = std::make_shared<SearchState>();
    const auto cancelled_calls = std::make_shared<std::atomic<unsigned>>(0);
    const auto cancelled_exchange = MakeSearchExchange(cancelled_state, [cancelled_calls](auto) {
        ++*cancelled_calls; return Reply({});
    });
    (void)cancelled_state->Begin(query); cancelled_state->Cancel();
    const auto cancelled_reply = cancelled_exchange(MakeSearchRequest(query, L"C:\\"));
    cancelled_state.reset();
    const auto expired_reply = cancelled_exchange(MakeSearchRequest(query, L"C:\\"));
    passed &= Check(!cancelled_reply.received && cancelled_reply.error == ERROR_CANCELLED &&
        !expired_reply.received && expired_reply.error == ERROR_CANCELLED && cancelled_calls->load() == 0,
        "search_response_cancelled_or_expired_owner_never_calls_raw_transport");

    const auto replaced_state = std::make_shared<SearchState>();
    const auto replaced_calls = std::make_shared<std::atomic<unsigned>>(0);
    const auto old_generation = replaced_state->Begin(query);
    auto newer_query = query; newer_query.text = L"newer-query";
    const auto replaced_exchange = MakeSearchExchange(replaced_state, [replaced_state, replaced_calls, newer_query](auto) {
        ++*replaced_calls;
        replaced_state->Cancel(); (void)replaced_state->Begin(newer_query);
        return Reply({ Hit(L"needle-old.txt") });
    });
    const auto replaced_reply = replaced_exchange(MakeSearchRequest(query, L"D:\\"));
    const auto newer_snapshot = replaced_state->Snapshot();
    passed &= Check(!replaced_reply.received && replaced_reply.error == ERROR_CANCELLED && replaced_calls->load() == 1 &&
        newer_snapshot.generation > old_generation && newer_snapshot.stage == SearchStage::Searching &&
        newer_snapshot.query == newer_query,
        "search_response_transport_result_from_an_old_generation_is_discarded");

    struct Gate
    {
        std::promise<void> entered, release;
        std::atomic<unsigned> calls{}, publications{};
    };
    const auto gate = std::make_shared<Gate>();
    auto entered = gate->entered.get_future();
    const auto release = gate->release.get_future().share();
    const auto blocked_state = std::make_shared<SearchState>();
    auto blocked_exchange = MakeSearchExchange(blocked_state, [gate, release](auto) {
        if (++gate->calls != 1) { return IndexRpcResult{ false, ERROR_RETRY }; }
        gate->entered.set_value();
        if (release.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        { return IndexRpcResult{ false, ERROR_TIMEOUT }; }
        return Reply({ Hit(L"needle-old.txt") });
    });
    SearchCoordinator blocked(blocked_state, std::move(blocked_exchange),
        [] { return std::vector<std::wstring>{ L"C:\\", L"D:\\" }; }, scope);
    const auto blocked_generation = blocked.Search(query, [gate](auto) { ++gate->publications; });
    const auto action = blocked.CurrentAction();
    const bool entered_transport = entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    blocked.Cancel();
    const bool still_owned = action && action.Status() == winrt::Windows::Foundation::AsyncStatus::Started;
    const auto replacement = blocked.Search(newer_query, [gate](auto) { ++gate->publications; });
    gate->release.set_value();
    const bool blocked_done = Finished(action);
    const auto cancelled_snapshot = blocked_state->Snapshot();
    passed &= Check(blocked_generation && entered_transport && still_owned && replacement == 0 && blocked_done,
        "search_response_cancellation_keeps_a_blocked_action_owned_until_transport_returns");
    passed &= Check(entered_transport && blocked_done && gate->calls.load() == 1 && gate->publications.load() == 0 &&
        cancelled_snapshot.generation > blocked_generation && cancelled_snapshot.stage == SearchStage::Idle &&
        cancelled_snapshot.volumes.empty(),
        "search_response_cancelled_query_neither_publishes_nor_submits_a_later_volume");
    (void)blocked.Shutdown();
    return passed;
}
