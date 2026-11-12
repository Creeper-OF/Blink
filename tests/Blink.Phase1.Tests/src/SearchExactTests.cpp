#include "SearchExactTests.h"
#include "Blink/App/SearchCoordinator.h"
#include "Blink/App/SearchViewModel.h"
#include "Blink/App/TabWindowState.h"
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

bool RunSearchVisibilityTests()
{
    using namespace blink::app;
    bool passed = true;
    constexpr DWORD visible_only = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
    const auto attributed = [](std::wstring name, const DWORD attributes, const std::uint8_t id) {
        auto hit = Hit(std::move(name)); hit.attributes = attributes; hit.identity.file_id[0] = id; return hit;
    };
    const auto hidden = attributed(L"needle-hidden.txt", FILE_ATTRIBUTE_HIDDEN, 10);
    const auto system = attributed(L"needle-system.txt", FILE_ATTRIBUTE_SYSTEM, 11);
    const auto normal = attributed(L"needle-visible.txt", FILE_ATTRIBUTE_NORMAL, 12);
    const auto both = attributed(L"needle-both.txt", visible_only, 13);
    SearchQuery query{ L"needle" }; query.limit = 4;
    passed &= Check(query.excluded_attributes == 0 && ValidateSearchQuery(query) == ERROR_SUCCESS &&
        MakeSearchRequest(query, L"D:\\").type == ipc::RequestType::Search,
        "search_visibility_neutral_model_query_preserves_legacy_include_all");
    bool masks_valid = true;
    for (const DWORD mask : { DWORD{}, DWORD{ FILE_ATTRIBUTE_SYSTEM }, visible_only })
    { auto test = query; test.excluded_attributes = mask; masks_valid &= ValidateSearchQuery(test) == ERROR_SUCCESS; }
    passed &= Check(masks_valid, "search_visibility_accepts_neutral_system_only_and_visible_only_policies");
    bool masks_rejected = true;
    for (const DWORD mask : { DWORD{ FILE_ATTRIBUTE_HIDDEN }, DWORD{ FILE_ATTRIBUTE_READONLY }, DWORD{ 0xffffffffu } })
    { auto test = query; test.excluded_attributes = mask; masks_rejected &= ValidateSearchQuery(test) == ERROR_INVALID_PARAMETER; }
    passed &= Check(masks_rejected, "search_visibility_rejects_masks_without_a_matching_wire_command");

    struct CommandCase { DWORD mask; bool direct; ipc::RequestType type, legacy; const char* name; };
    const CommandCase commands[]{
        { 0, false, ipc::RequestType::Search, ipc::RequestType::Search, "neutral_recursive" },
        { 0, true, ipc::RequestType::SearchDirectChildren, ipc::RequestType::SearchDirectChildren, "neutral_direct" },
        { visible_only, false, ipc::RequestType::SearchVisible, ipc::RequestType::Search, "visible_recursive" },
        { visible_only, true, ipc::RequestType::SearchVisibleDirectChildren, ipc::RequestType::SearchDirectChildren, "visible_direct" },
        { FILE_ATTRIBUTE_SYSTEM, false, ipc::RequestType::SearchWithHidden, ipc::RequestType::Search, "with_hidden_recursive" },
        { FILE_ATTRIBUTE_SYSTEM, true, ipc::RequestType::SearchWithHiddenDirectChildren, ipc::RequestType::SearchDirectChildren, "with_hidden_direct" },
    };
    const auto command_query = [&](const CommandCase& test) {
        auto value = query; value.excluded_attributes = test.mask; value.scope = SearchScope::SelectedFolder;
        value.folder = L"D:\\Folder"; value.include_subfolders = !test.direct; value.mode = SearchMode::Prefix; return value;
    };
    const auto same_payload = [](const ipc::Request& left, const ipc::Request& right) {
        return left.protocol_version == right.protocol_version && left.request_id == right.request_id &&
            left.operation_id == right.operation_id && left.volume_root == right.volume_root && left.text == right.text &&
            left.extension == right.extension && left.path_prefix == right.path_prefix &&
            left.prefix == right.prefix && left.limit == right.limit;
    };
    for (const auto& test : commands)
    {
        const auto request = MakeSearchRequest(command_query(test), L"D:\\");
        const auto name = std::string("search_visibility_request_") + test.name;
        passed &= Check(request.type == test.type && request.protocol_version == ipc::kProtocolVersion &&
            request.volume_root == L"D:\\" && request.path_prefix == L"D:\\Folder\\" && request.prefix &&
            request.limit == query.limit && request.text == query.text && request.extension.empty() && request.operation_id == 0,
            name.c_str());
    }
    bool exact_preserved = true, extension_preserved = true;
    for (const auto& test : commands)
    {
        auto value = command_query(test); value.mode = SearchMode::Exact; value.text = L"target.txt";
        const auto exact = MakeSearchRequest(value, L"D:\\");
        exact_preserved &= exact.type == test.type && exact.prefix && exact.limit == kSearchCandidateLimit &&
            exact.text == value.text && exact.path_prefix == L"D:\\Folder\\";
        value.mode = SearchMode::Extension; value.text = L"txt";
        const auto extension = MakeSearchRequest(value, L"D:\\");
        extension_preserved &= extension.type == test.type && !extension.prefix && extension.text.empty() &&
            extension.extension == L".txt" && extension.limit == value.limit;
    }
    passed &= Check(exact_preserved, "search_visibility_all_policies_preserve_exact_candidate_cap_and_folder_scope");
    passed &= Check(extension_preserved, "search_visibility_all_policies_preserve_extension_payload_and_display_cap");
    auto drive_query = query; drive_query.scope = SearchScope::CurrentVolume; drive_query.folder = L"D:\\";
    drive_query.excluded_attributes = visible_only; drive_query.include_subfolders = false;
    const auto drive_request = MakeSearchRequest(drive_query, L"D:\\");
    passed &= Check(drive_request.type == ipc::RequestType::SearchVisible && drive_request.path_prefix.empty(),
        "search_visibility_current_volume_does_not_become_direct_child_search");

    {
        TabWindowState window; auto* tab = window.ActiveTab(); tab->search_draft = L"submitted";
        const auto initial = tab->SearchIntent(false);
        passed &= Check(!window.GetVisibilityPolicy().show_hidden_items && initial.excluded_attributes == visible_only &&
            MakeSearchRequest(initial, L"D:\\").type == ipc::RequestType::SearchVisible,
            "search_visibility_new_tab_binds_default_window_policy_before_submission");
        window.SetVisibilityPolicy({ true });
        auto current = tab->SearchIntent(false);
        passed &= Check(current.excluded_attributes == FILE_ATTRIBUTE_SYSTEM &&
            MakeSearchRequest(current, L"D:\\").type == ipc::RequestType::SearchWithHidden,
            "search_visibility_show_hidden_still_excludes_system_items_before_limit");
        const auto other_id = window.CreateTab(); const auto* other = window.Find(other_id);
        passed &= Check(other && other->SearchIntent(false).excluded_attributes == FILE_ATTRIBUTE_SYSTEM &&
            tab->SearchIntent(false).text == L"submitted",
            "search_visibility_new_tabs_inherit_policy_without_replacing_other_tab_drafts");
        current.scope = SearchScope::SelectedFolder; current.folder = L"D:\\Pinned";
        current.include_subfolders = false; current.mode = SearchMode::Prefix;
        auto committed = std::make_shared<SearchSnapshot>(); committed->query = current; tab->search_snapshot = committed;
        tab->search_draft = L"unsubmitted"; tab->search_mode = SearchMode::Extension;
        const auto reuse = tab->SearchIntent(true);
        passed &= Check(reuse == current && tab->SearchIntent(false).text == L"unsubmitted",
            "search_visibility_retry_preserves_committed_text_mode_scope_and_current_policy");
        const auto staged = tab->SearchIntent(false); const auto generation = tab->search_state->Begin(staged);
        tab->search_visible = true; window.SetVisibilityPolicy({ false });
        const auto changed = tab->SearchIntent(true); const auto cancelled = tab->search_state->Snapshot();
        passed &= Check(generation && !tab->search_visible && cancelled.stage == SearchStage::Idle &&
            cancelled.generation > generation && changed.text == current.text && changed.folder == current.folder &&
            changed.mode == current.mode && !changed.include_subfolders && changed.excluded_attributes == visible_only &&
            other->SearchIntent(false).excluded_attributes == visible_only,
            "search_visibility_policy_change_cancels_old_work_and_rebinds_cached_committed_intent");
        tab->search_snapshot.reset();
        passed &= Check(tab->SearchIntent(true).text == staged.text &&
            tab->SearchIntent(true).excluded_attributes == visible_only,
            "search_visibility_state_snapshot_reuse_also_binds_latest_window_policy");
    }

    query.excluded_attributes = visible_only;
    auto reply = Reply({ hidden, system, normal, both });
    auto volume = MapSearchReply(query, L"D:\\", reply);
    passed &= Check(volume.searched && volume.results.size() == 1 && volume.results.front().hit.name == normal.name &&
        !volume.query_incomplete, "search_visibility_mapper_matches_default_view_policy_after_validating_hits");
    reply.legacy_search_visibility = true; volume = MapSearchReply(query, L"D:\\", reply);
    passed &= Check(volume.searched && volume.results.size() == 1 && volume.query_incomplete && volume.limit_reached,
        "search_visibility_capped_legacy_reply_preserves_visible_prefix_and_incomplete_status");
    reply = Reply({ hidden, system, both, hidden }); reply.legacy_search_visibility = true;
    volume = MapSearchReply(query, L"D:\\", reply);
    passed &= Check(volume.searched && volume.results.empty() && volume.query_incomplete,
        "search_visibility_all_hidden_capped_legacy_reply_is_not_a_known_empty_search");
    reply.response.results.pop_back(); volume = MapSearchReply(query, L"D:\\", reply);
    passed &= Check(volume.searched && volume.results.empty() && !volume.query_incomplete && !volume.limit_reached,
        "search_visibility_short_legacy_reply_can_confirm_no_visible_matches");
    auto with_hidden = query; with_hidden.excluded_attributes = FILE_ATTRIBUTE_SYSTEM;
    volume = MapSearchReply(with_hidden, L"D:\\", Reply({ hidden, system, normal, both }));
    passed &= Check(volume.searched && volume.results.size() == 2 && volume.results[0].hit.name == hidden.name &&
        volume.results[1].hit.name == normal.name && ItemVisibilityPolicy{ true }.Includes(hidden.attributes) &&
        !ItemVisibilityPolicy{ true }.Includes(system.attributes),
        "search_visibility_show_hidden_mapping_keeps_hidden_but_never_system_rows");
    auto neutral = query; neutral.excluded_attributes = 0;
    reply = Reply({ hidden, system, normal, both }); reply.legacy_search_visibility = true;
    volume = MapSearchReply(neutral, L"D:\\", reply);
    passed &= Check(volume.searched && volume.results.size() == 4 && !volume.query_incomplete,
        "search_visibility_neutral_mapping_preserves_old_include_all_contract");
    auto exact_query = query; exact_query.mode = SearchMode::Exact; exact_query.text = L"target.txt"; exact_query.limit = 1;
    auto hidden_exact = hidden; hidden_exact.name = L"target.txt"; hidden_exact.full_path = L"D:\\Folder\\target.txt";
    volume = MapSearchReply(exact_query, L"D:\\", Reply({ hidden_exact, Hit(L"target.txt.bak"), Hit(L"target.txt") }));
    passed &= Check(volume.searched && volume.results.size() == 1 && volume.results.front().hit.name == L"target.txt" &&
        (volume.results.front().hit.attributes & visible_only) == 0 && volume.limit_reached && !volume.query_incomplete,
        "search_visibility_exact_mapping_filters_before_visible_display_cap");
    auto scoped = query; scoped.scope = SearchScope::SelectedFolder; scoped.folder = L"D:\\Folder"; scoped.include_subfolders = false;
    auto nested = normal; nested.name = L"nested.txt"; nested.full_path = L"D:\\Folder\\deep\\nested.txt";
    auto outside = normal; outside.name = L"outside.txt"; outside.full_path = L"D:\\Folder-old\\outside.txt";
    volume = MapSearchReply(scoped, L"D:\\", Reply({ hidden, nested, normal, outside }));
    passed &= Check(volume.searched && volume.results.size() == 1 && volume.results.front().hit.name == normal.name,
        "search_visibility_filter_does_not_relax_direct_child_or_sibling_boundaries");
    bool malformed_rejected = true;
    for (const bool legacy : { false, true })
    {
        auto malformed = hidden; malformed.identity.valid = false;
        auto bad = Reply({ malformed, normal }); bad.legacy_search_visibility = legacy;
        const auto failed = MapSearchReply(query, L"D:\\", bad);
        malformed_rejected &= !failed.searched && failed.error == ERROR_INVALID_DATA && failed.results.empty();
    }
    passed &= Check(malformed_rejected, "search_visibility_hidden_malformed_hit_cannot_hide_real_payload_failure");
    auto failed_reply = Reply({}); failed_reply.response.status = ipc::ResponseStatus::Failed;
    failed_reply.response.error = ERROR_ACCESS_DENIED; failed_reply.legacy_search_visibility = true;
    volume = MapSearchReply(query, L"D:\\", failed_reply);
    passed &= Check(!volume.searched && volume.error == ERROR_ACCESS_DENIED && volume.results.empty(),
        "search_visibility_legacy_marker_never_turns_access_denied_into_empty_success");
    failed_reply.response.status = ipc::ResponseStatus::Busy; failed_reply.response.error = ERROR_IO_PENDING;
    failed_reply.response.index_state = ipc::IndexState::CatchingUp; volume = MapSearchReply(query, L"D:\\", failed_reply);
    passed &= Check(!volume.searched && volume.error == ERROR_IO_PENDING && volume.results.empty(),
        "search_visibility_busy_reply_remains_unconfirmed_without_results");
    reply = Reply({ hidden, system, normal, both, normal }); reply.legacy_search_visibility = true;
    volume = MapSearchReply(query, L"D:\\", reply);
    passed &= Check(!volume.searched && volume.error == ERROR_INVALID_DATA && volume.results.empty(),
        "search_visibility_legacy_fallback_does_not_accept_an_oversized_wire_reply");
    volume = MapSearchReply(query, L"D:\\", Reply({}));
    passed &= Check(volume.searched && volume.results.empty() && !volume.query_incomplete && !volume.limit_reached,
        "search_visibility_supported_filtered_search_can_confirm_known_empty");

    {
        SearchState state; const auto generation = state.Begin(query);
        SearchSnapshot snapshot; snapshot.query = query; snapshot.stage = SearchStage::Partial;
        SearchVolumeResult capped; capped.root = L"C:\\"; capped.searched = true; capped.query_incomplete = true;
        SearchVolumeResult complete; complete.root = L"D:\\"; complete.searched = true;
        SearchVolumeResult pending; pending.root = L"E:\\";
        snapshot.volumes = { capped, complete, pending };
        const bool committed = state.Complete(generation, snapshot);
        auto changed = query; changed.excluded_attributes = FILE_ATTRIBUTE_SYSTEM;
        const bool policy_rejected = !state.Begin(changed, true);
        const auto retry = state.Begin(query, true); const auto retried = state.Snapshot();
        passed &= Check(committed && policy_rejected && retry > generation && retried.volumes.size() == 3 &&
            !retried.volumes[0].searched && retried.volumes[1].searched && !retried.volumes[2].searched,
            "search_visibility_retry_rechecks_incomplete_volumes_and_retains_complete_policy_matched_volumes");
    }

    struct Rejection { ipc::ResponseStatus status; DWORD error; const char* name; };
    const Rejection rejections[]{
        { ipc::ResponseStatus::Unsupported, ERROR_NOT_SUPPORTED, "unsupported" },
        { ipc::ResponseStatus::Unsupported, ERROR_INVALID_FUNCTION, "invalid_function" },
        { ipc::ResponseStatus::InvalidRequest, ERROR_INVALID_PARAMETER, "legacy_invalid_request" },
    };
    for (const auto& test : commands)
    {
        if (!test.mask) { continue; }
        for (const auto& rejection : rejections)
        {
            const auto state = std::make_shared<SearchState>(); const auto value = command_query(test);
            (void)state->Begin(value); std::vector<ipc::Request> requests;
            const auto exchange = MakeSearchExchange(state, [&](ipc::Request request) {
                requests.push_back(std::move(request)); auto answer = Reply({ hidden, normal });
                if (requests.size() == 1) { answer.response.status = rejection.status; answer.response.error = rejection.error; }
                return answer;
            });
            const auto request = MakeSearchRequest(value, L"D:\\"); const auto answer = exchange(request);
            const auto name = std::string("search_visibility_factory_") + test.name + "_" + rejection.name;
            passed &= Check(requests.size() == 2 && requests[0].type == test.type && requests[1].type == test.legacy &&
                same_payload(request, requests[0]) && same_payload(request, requests[1]) &&
                answer.received && answer.response.status == ipc::ResponseStatus::Ok && answer.legacy_search_visibility &&
                answer.response.results.size() == 2, name.c_str());
        }
    }
    struct NoFallback { ipc::ResponseStatus status; DWORD error; const char* name; bool received{ true }; };
    const NoFallback unchanged[]{
        { ipc::ResponseStatus::Busy, ERROR_IO_PENDING, "busy" },
        { ipc::ResponseStatus::Failed, ERROR_ACCESS_DENIED, "access_denied" },
        { ipc::ResponseStatus::Unsupported, ERROR_REVISION_MISMATCH, "revision_mismatch" },
        { ipc::ResponseStatus::InvalidRequest, ERROR_ACCESS_DENIED, "invalid_request_access_denied" },
        { ipc::ResponseStatus::Failed, ERROR_BROKEN_PIPE, "broken_transport", false },
    };
    for (const auto& test : unchanged)
    {
        const auto state = std::make_shared<SearchState>(); (void)state->Begin(query); unsigned calls{};
        const auto exchange = MakeSearchExchange(state, [&](ipc::Request) {
            ++calls; auto answer = Reply({}); answer.received = test.received;
            answer.error = test.received ? ERROR_SUCCESS : test.error;
            answer.response.status = test.status; answer.response.error = test.error; return answer;
        });
        const auto answer = exchange(MakeSearchRequest(query, L"D:\\"));
        const auto name = std::string("search_visibility_factory_does_not_fallback_on_") + test.name;
        passed &= Check(calls == 1 && answer.received == test.received && answer.response.error == test.error &&
            !answer.legacy_search_visibility, name.c_str());
    }
    {
        const auto state = std::make_shared<SearchState>(); (void)state->Begin(query); unsigned calls{};
        const auto exchange = MakeSearchExchange(state, [&](ipc::Request) {
            ++calls; auto answer = Reply({}); answer.response.status = ipc::ResponseStatus::Unsupported;
            answer.response.error = ERROR_NOT_SUPPORTED; ++answer.expected_request_id; return answer;
        });
        const auto answer = exchange(MakeSearchRequest(query, L"D:\\"));
        passed &= Check(calls == 1 && ValidateIndexReply(answer) == ERROR_REVISION_MISMATCH && !answer.legacy_search_visibility,
            "search_visibility_factory_never_uses_uncorrelated_rejection_for_fallback");
    }
    {
        const auto state = std::make_shared<SearchState>(); (void)state->Begin(neutral); unsigned calls{};
        const auto exchange = MakeSearchExchange(state, [&](ipc::Request) {
            ++calls; auto answer = Reply({}); answer.response.status = ipc::ResponseStatus::Unsupported;
            answer.response.error = ERROR_NOT_SUPPORTED; return answer;
        });
        const auto answer = exchange(MakeSearchRequest(neutral, L"D:\\"));
        passed &= Check(calls == 1 && !answer.legacy_search_visibility && answer.response.error == ERROR_NOT_SUPPORTED,
            "search_visibility_factory_does_not_retry_an_unsupported_legacy_command");
    }
    {
        const auto state = std::make_shared<SearchState>(); (void)state->Begin(query); unsigned calls{};
        const auto exchange = MakeSearchExchange(state, [&](ipc::Request) {
            ++calls; state->Cancel(); auto answer = Reply({});
            answer.response.status = ipc::ResponseStatus::Unsupported; answer.response.error = ERROR_NOT_SUPPORTED; return answer;
        });
        const auto answer = exchange(MakeSearchRequest(query, L"D:\\"));
        passed &= Check(calls == 1 && !answer.received && answer.error == ERROR_CANCELLED && !answer.legacy_search_visibility,
            "search_visibility_factory_cancellation_after_rejection_prevents_legacy_followup");
    }
    {
        const auto state = std::make_shared<SearchState>(); (void)state->Begin(query); unsigned calls{};
        const auto exchange = MakeSearchExchange(state, [&](ipc::Request) {
            ++calls; auto answer = Reply({}); answer.response.status = ipc::ResponseStatus::Unsupported;
            answer.response.error = ERROR_NOT_SUPPORTED; return answer;
        });
        const auto answer = exchange(MakeSearchRequest(query, L"D:\\"));
        passed &= Check(calls == 2 && answer.legacy_search_visibility && answer.response.error == ERROR_NOT_SUPPORTED,
            "search_visibility_factory_retries_rejected_new_command_at_most_once");
    }
    {
        const auto state = std::make_shared<SearchState>(); (void)state->Begin(query); unsigned calls{};
        const auto exchange = MakeSearchExchange(state, [&](ipc::Request) { ++calls; return Reply({ normal }); });
        const auto answer = exchange(MakeSearchRequest(query, L"D:\\"));
        passed &= Check(calls == 1 && answer.received && !answer.legacy_search_visibility && answer.response.results.size() == 1,
            "search_visibility_supported_service_gets_one_request_without_legacy_marker");
    }
    {
        struct Probe { std::atomic_bool upgraded{}; std::vector<ipc::Request> requests; };
        const auto probe = std::make_shared<Probe>(); const auto state = std::make_shared<SearchState>();
        auto exchange = MakeSearchExchange(state, [probe, normal, hidden](ipc::Request request) {
            probe->requests.push_back(request);
            if (request.volume_root == L"C:\\")
            {
                auto hit = normal; hit.full_path = L"C:\\Folder\\" + hit.name; return Reply({ hit });
            }
            if (!probe->upgraded.load() && request.type == ipc::RequestType::SearchVisible)
            {
                auto answer = Reply({}); answer.response.status = ipc::ResponseStatus::InvalidRequest;
                answer.response.error = ERROR_INVALID_PARAMETER; return answer;
            }
            if (!probe->upgraded.load()) { return Reply({ hidden, hidden, hidden, hidden }); }
            return Reply({ normal });
        });
        SearchCoordinator coordinator(state, std::move(exchange), [] { return std::vector<std::wstring>{ L"C:\\", L"D:\\" }; },
            [](const auto& path) { return SearchFolderScope{ path, { 77, { 1 }, true }, ERROR_SUCCESS }; });
        const auto generation = coordinator.Search(query, {}); const bool done = generation && Finished(coordinator.CurrentAction());
        const auto initial = state->Snapshot();
        passed &= Check(done && initial.stage == SearchStage::Partial && initial.error == ERROR_MORE_DATA &&
            initial.volumes.size() == 2 && initial.volumes[0].searched && initial.volumes[0].results.size() == 1 &&
            initial.volumes[1].searched && initial.volumes[1].query_incomplete && initial.volumes[1].results.empty() &&
            probe->requests.size() == 3,
            "search_visibility_capped_old_service_publishes_healthy_volume_and_honest_partial_search");
        probe->upgraded.store(true);
        const auto retry = coordinator.Search(query, {}, true); const bool retried = retry && Finished(coordinator.CurrentAction());
        const auto final = state->Snapshot();
        passed &= Check(done && retried && retry > generation && final.stage == SearchStage::Complete &&
            final.error == ERROR_SUCCESS && final.volumes.size() == 2 && final.volumes[0].results.size() == 1 &&
            final.volumes[1].searched && !final.volumes[1].query_incomplete && final.volumes[1].results.size() == 1 &&
            SameSearchLink(initial.volumes[0].results.front(), final.volumes[0].results.front()) &&
            probe->requests.size() == 4 && probe->requests.back().volume_root == L"D:\\" &&
            probe->requests.back().type == ipc::RequestType::SearchVisible,
            "search_visibility_retry_after_service_upgrade_requeries_only_incomplete_volume_using_new_command");
        (void)coordinator.Shutdown();
    }
    return passed;
}
