#include "SearchExactTests.h"
#include "Blink/App/SearchCoordinator.h"
#include "Blink/App/SearchViewModel.h"
#include <algorithm>
#include <atomic>
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
