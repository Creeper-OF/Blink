#include "SearchIntegrationTests.h"
#include "Blink/App/SearchResultValidation.h"
#include "Blink/App/TabWindowState.h"
#include "Blink/App/SearchRequestQueue.h"
#include "Blink/App/SearchViewModel.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <future>
#include <atomic>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <unordered_map>

namespace
{
    bool Check(bool condition, const char* name)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        return condition;
    }
    blink::app::SearchResult ResultFor(const std::filesystem::path& path)
    {
        blink::core::FileEntry entry, parent;
        (void)blink::core::ReadEntryMetadata(path.wstring(), entry);
        (void)blink::core::ReadEntryMetadata(path.parent_path().wstring(), parent);
        blink::app::SearchResult result;
        result.source_volume = blink::app::SearchDriveRoot(path.wstring());
        result.hit.identity = entry.identity; result.hit.parent_identity = parent.identity;
        result.hit.name = path.filename().wstring(); result.hit.full_path = path.wstring();
        result.hit.is_directory = entry.is_directory; result.hit.attributes = entry.attributes;
        return result;
    }
    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        const auto completion = std::make_shared<std::promise<Status>>(); auto future = completion->get_future();
        action.Completed([completion](auto&&, const Status status) { completion->set_value(status); });
        return future.wait_for(std::chrono::seconds(5)) == std::future_status::ready && future.get() == Status::Completed;
    }
    blink::app::SearchVolumeResult VolumeFor(const blink::app::SearchResult& result)
    {
        blink::app::SearchVolumeResult volume; volume.root = result.source_volume; volume.searched = true;
        volume.stage = blink::app::VolumeIndexStage::Healthy; volume.results = { result }; return volume;
    }
    bool RunVolumeIdentityTests(const blink::app::SearchResult& real)
    {
        using namespace blink::app;
        namespace ipc = blink::index::ipc;
        bool passed = true;
        const auto actual_root = ResolveSearchFolderScope(real.source_volume);
        DWORD native_serial{};
        const bool native_volume = GetVolumeInformationW(real.source_volume.c_str(), nullptr, 0, &native_serial,
            nullptr, nullptr, nullptr, 0) != FALSE;
        auto wire = real;
        wire.hit.identity.volume_serial = native_serial; wire.hit.parent_identity.volume_serial = native_serial;
        const auto unbound = ValidateSearchResult(wire);
        std::cout << "INFO search_volume_binding full_serial=" << actual_root.identity.volume_serial <<
            " usn32_serial=" << native_serial << " high32=" << (actual_root.identity.volume_serial >> 32) <<
            " unbound_error=" << unbound.error << '\n';
        passed &= Check(native_volume && !actual_root.error && actual_root.identity.valid &&
            static_cast<DWORD>(actual_root.identity.volume_serial) == native_serial &&
            real.hit.identity.volume_serial == actual_root.identity.volume_serial &&
            unbound.error == ((actual_root.identity.volume_serial >> 32) ? ERROR_FILE_INVALID : ERROR_SUCCESS),
            "search_real_volume_metadata_exposes_usn32_and_fileidinfo_serials");

        auto volume = VolumeFor(wire);
        BindSearchVolumeIdentity(volume, actual_root);
        const auto target = volume.results.empty() ? SearchActionTarget{ {}, {}, {}, ERROR_FILE_INVALID } :
            ValidateSearchResult(volume.results.front());
        passed &= Check(volume.searched && volume.results.size() == 1 &&
            volume.results.front().hit.identity == real.hit.identity &&
            volume.results.front().hit.parent_identity == real.hit.parent_identity && !target.error &&
            target.entry.identity == real.hit.identity && target.parent_identity == real.hit.parent_identity,
            "search_real_usn32_result_rebinds_to_full_identity_for_open");
        TabWindowState locate_window;
        auto* locate_tab = locate_window.ActiveTab();
        const auto listing = blink::core::EnumerateDirectory(target.parent);
        const auto navigation = locate_tab->state->Navigate(target.parent);
        bool located{};
        if (navigation && !listing.error && !volume.results.empty())
        {
            (void)locate_tab->state->CommitBrowse(*navigation, listing);
            locate_tab->locate_after_browse = RowIdentity{ volume.results.front().hit.identity, volume.results.front().hit.name };
            locate_tab->locate_navigation_token = navigation->token;
            located = locate_tab->FinishLocate(navigation->token) &&
                locate_tab->state->IsRowSelected(MakeRowIdentity(target.entry));
        }
        passed &= Check(located, "search_real_bound_result_locates_the_actual_browse_row");
        const auto folder = ResolveSearchFolderScope(target.parent);
        FilterSearchFolderResults(folder, volume);
        passed &= Check(!folder.error && volume.searched && volume.results.size() == 1 &&
            volume.results.front().hit.parent_identity == folder.identity,
            "search_real_usn32_result_enters_current_folder_with_exact_parent_identity");

        volume = VolumeFor(real); BindSearchVolumeIdentity(volume, actual_root);
        passed &= Check(volume.searched && volume.results.size() == 1 &&
            volume.results.front().hit.identity == real.hit.identity &&
            volume.results.front().hit.parent_identity == real.hit.parent_identity,
            "search_binding_preserves_already_full_identity");
        auto bad_child = wire; bad_child.hit.identity.volume_serial ^= 1;
        auto bad_parent = wire; bad_parent.hit.parent_identity.volume_serial ^= 1;
        volume = VolumeFor(bad_child); BindSearchVolumeIdentity(volume, actual_root);
        auto other = VolumeFor(bad_parent); BindSearchVolumeIdentity(other, actual_root);
        passed &= Check(!volume.searched && volume.results.empty() && volume.error == ERROR_FILE_INVALID &&
            !other.searched && other.results.empty() && other.error == ERROR_FILE_INVALID,
            "search_binding_rejects_wrong_child_or_parent_volume_serial");
        auto wrong_high = actual_root.identity.volume_serial ^ (std::uint64_t{ 1 } << 63);
        if (!(wrong_high >> 32)) { wrong_high |= std::uint64_t{ 1 } << 32; }
        auto bad_high = wire; bad_high.hit.identity.volume_serial = bad_high.hit.parent_identity.volume_serial = wrong_high;
        volume = VolumeFor(bad_high); BindSearchVolumeIdentity(volume, actual_root);
        passed &= Check(!volume.searched && volume.results.empty() && volume.error == ERROR_FILE_INVALID &&
            static_cast<DWORD>(wrong_high) == native_serial,
            "search_binding_rejects_other_nonzero_high_serial_with_matching_low_word");
        auto wrong_root = actual_root; wrong_root.path = actual_root.path == L"C:\\" ? L"D:\\" : L"C:\\";
        volume = VolumeFor(wire); BindSearchVolumeIdentity(volume, wrong_root);
        passed &= Check(!volume.searched && volume.results.empty() && volume.error == ERROR_FILE_INVALID,
            "search_binding_rejects_different_volume_root");
        auto unavailable = actual_root; unavailable.error = ERROR_ACCESS_DENIED;
        volume = VolumeFor(wire); BindSearchVolumeIdentity(volume, unavailable);
        passed &= Check(!volume.searched && volume.results.empty() && volume.error == ERROR_ACCESS_DENIED,
            "search_binding_retains_metadata_error_without_results");

        const auto state = std::make_shared<SearchState>();
        const auto root_reads = std::make_shared<std::atomic<unsigned>>(0);
        const auto worker_only = std::make_shared<std::atomic_bool>(true);
        const auto caller_thread = GetCurrentThreadId();
        SearchCoordinator coordinator(state, [wire](auto) {
            IndexRpcResult reply; reply.received = true; reply.expected_request_id = 43;
            reply.response.request_id = 43; reply.response.status = ipc::ResponseStatus::Ok;
            reply.response.error = ERROR_SUCCESS; reply.response.service_state = ipc::ServiceState::Running;
            reply.response.index_state = ipc::IndexState::Healthy; reply.response.results = { wire.hit };
            return reply;
        }, [root = real.source_volume] { return std::vector<std::wstring>{ root }; },
            [root_reads, worker_only, caller_thread, root = real.source_volume](const auto& path) {
                if (GetCurrentThreadId() == caller_thread) { worker_only->store(false); }
                if (path == root) { ++*root_reads; }
                return ResolveSearchFolderScope(path);
            });
        const auto run = [&](SearchQuery query) {
            const auto completion = std::make_shared<std::promise<void>>(); auto future = completion->get_future();
            const auto id = coordinator.Search(std::move(query), [completion](auto) { completion->set_value(); });
            return id && future.wait_for(std::chrono::seconds(5)) == std::future_status::ready && Finished(coordinator.CurrentAction());
        };
        const bool all_done = run({ real.hit.name, SearchScope::AllLocalVolumes, SearchMode::Exact });
        auto snapshot = state->Snapshot();
        const auto has_real_target = [&] {
            return snapshot.stage == SearchStage::Complete && snapshot.volumes.size() == 1 &&
                snapshot.volumes.front().results.size() == 1 &&
                snapshot.volumes.front().results.front().hit.identity == real.hit.identity &&
                !ValidateSearchResult(snapshot.volumes.front().results.front()).error;
        };
        passed &= Check(all_done && worker_only->load() && root_reads->load() == 1 && has_real_target(),
            "search_coordinator_binds_actual_volume_once_on_worker_for_all_volumes");
        root_reads->store(0);
        const bool folder_done = run({ real.hit.name, SearchScope::CurrentFolder, SearchMode::Exact,
            std::filesystem::path(real.hit.full_path).parent_path().wstring() });
        snapshot = state->Snapshot();
        passed &= Check(folder_done && worker_only->load() && root_reads->load() == 1 && has_real_target() &&
            snapshot.scope_identity == real.hit.parent_identity,
            "search_coordinator_current_folder_uses_full_identity_for_filter_and_open");
        (void)coordinator.Shutdown();
        return passed;
    }
    bool SubmitScopeQuery(blink::app::SearchCoordinator& coordinator, const blink::app::SearchQuery& query, const bool retry = false)
    {
        const auto completion = std::make_shared<std::promise<void>>(); auto future = completion->get_future();
        const auto id = coordinator.Search(query, [completion](auto) { completion->set_value(); }, retry);
        return id && future.wait_for(std::chrono::seconds(5)) == std::future_status::ready && Finished(coordinator.CurrentAction());
    }
    blink::app::IndexRpcResult ScopeReply(std::vector<blink::index::ipc::SearchHit> hits = {})
    {
        namespace ipc = blink::index::ipc;
        blink::app::IndexRpcResult reply; reply.received = true; reply.expected_request_id = 71;
        reply.response.request_id = 71; reply.response.status = ipc::ResponseStatus::Ok; reply.response.error = ERROR_SUCCESS;
        reply.response.service_state = ipc::ServiceState::Running; reply.response.index_state = ipc::IndexState::Healthy;
        reply.response.results = std::move(hits); return reply;
    }
    bool CommitScopeLocation(blink::app::TabRecord& tab, const blink::app::Location& location)
    {
        using namespace blink::app;
        const auto navigation = tab.state->Navigate(location);
        if (!navigation) { return false; }
        CommitBrowseResult committed;
        if (location.IsThisPC()) { committed = tab.state->CommitThisPC(*navigation, {}); }
        else
        {
            blink::core::DirectoryListing listing; listing.path = location.FolderPath();
            listing.directory_identity = { 77, { 7 }, true };
            committed = tab.state->CommitBrowse(*navigation, listing);
        }
        if (committed.status != CommitBrowseStatus::Committed) { return false; }
        tab.OnNavigation(false); return true;
    }
    bool RunTabSearchScopeTests()
    {
        using namespace blink::app;
        bool passed = true;
        TabWindowState window; auto* tab = window.ActiveTab();
        tab->search_draft = L"txt"; tab->search_mode = SearchMode::Extension;
        auto query = tab->SearchIntent(false);
        passed &= Check(tab->search_scope == SearchScope::FollowLocation && query.scope == SearchScope::AllLocalVolumes &&
            query.folder.empty(), "search_follow_default_this_pc_searches_all_indexed_volumes");
        const bool c = CommitScopeLocation(*tab, Location::Folder(L"C:\\Projects\\Alpha"));
        query = tab->SearchIntent(false);
        passed &= Check(c && query.scope == SearchScope::CurrentVolume && query.folder == L"C:\\",
            "search_follow_c_directory_defaults_to_whole_c_drive");
        const bool d = CommitScopeLocation(*tab, Location::Folder(L"D:\\Projects\\Beta"));
        query = tab->SearchIntent(false);
        passed &= Check(d && query.scope == SearchScope::CurrentVolume && query.folder == L"D:\\",
            "search_follow_d_directory_defaults_to_whole_d_drive");
        const bool pc = CommitScopeLocation(*tab, Location::ThisPC());
        query = tab->SearchIntent(false);
        passed &= Check(pc && query.scope == SearchScope::AllLocalVolumes && query.folder.empty() &&
            tab->search_scope == SearchScope::FollowLocation,
            "search_follow_navigation_back_to_this_pc_restores_all_without_pinning_a_drive");
        passed &= Check(query.text == L"txt" && query.mode == SearchMode::Extension &&
            tab->search_draft == L"txt" && tab->search_mode == SearchMode::Extension,
            "search_follow_navigation_preserves_user_draft_and_matching_mode");
        tab->search_scope = SearchScope::CurrentFolder;
        query = tab->SearchIntent(false);
        passed &= Check(query.scope == SearchScope::CurrentFolder && query.folder.empty() &&
            ValidateSearchQuery(query) != ERROR_SUCCESS,
            "search_explicit_current_folder_on_this_pc_reports_invalid_instead_of_searching_all");
        tab->search_scope = SearchScope::AllLocalVolumes;
        const bool explicit_c = CommitScopeLocation(*tab, Location::Folder(L"C:\\Projects\\Explicit"));
        passed &= Check(explicit_c && tab->SearchIntent(false).scope == SearchScope::AllLocalVolumes,
            "search_explicit_all_volumes_is_not_overridden_by_directory_navigation");
        tab->search_scope = SearchScope::SelectedFolder; tab->search_selected_folder = L"D:\\Pinned";
        tab->search_include_subfolders = false;
        query = tab->SearchIntent(false);
        passed &= Check(query.scope == SearchScope::SelectedFolder && query.folder == L"D:\\Pinned" && !query.include_subfolders,
            "search_explicit_folder_and_depth_are_independent_of_current_location");
        const auto second = window.CreateTab(); auto* other = window.Find(second);
        other->search_draft = L"other"; other->search_mode = SearchMode::Prefix;
        other->search_scope = SearchScope::SelectedVolumes; other->search_selected_volumes = L"C:;D:";
        const auto other_query = other->SearchIntent(false);
        passed &= Check(tab->SearchIntent(false) == query && other_query.scope == SearchScope::SelectedVolumes &&
            other_query.selected_volumes == L"C:;D:" && other_query.text == L"other" && other_query.mode == SearchMode::Prefix &&
            other_query.include_subfolders && tab->search_selected_volumes.empty(),
            "search_advanced_scope_depth_draft_and_mode_are_isolated_between_tabs");
        auto submitted = std::make_shared<SearchSnapshot>(); submitted->query = query; tab->search_snapshot = submitted;
        tab->search_draft = L"new draft"; tab->search_mode = SearchMode::Exact;
        tab->search_scope = SearchScope::SelectedVolumes; tab->search_selected_volumes = L"C:";
        tab->search_selected_folder = L"C:\\Changed"; tab->search_include_subfolders = true;
        passed &= Check(tab->SearchIntent(true) == query && tab->SearchIntent(false).selected_volumes == L"C:" &&
            tab->SearchIntent(false).text == L"new draft" && tab->SearchIntent(false).mode == SearchMode::Exact,
            "search_retry_reuses_submitted_scope_depth_and_mode_while_retaining_new_draft");
        return passed;
    }
    bool RunExplicitVolumeCoordinatorTests()
    {
        using namespace blink::app;
        namespace ipc = blink::index::ipc;
        bool passed = true;
        const auto state = std::make_shared<SearchState>();
        const auto requests = std::make_shared<std::vector<ipc::Request>>();
        const auto discoveries = std::make_shared<std::atomic<unsigned>>(0);
        SearchCoordinator coordinator(state, [requests](ipc::Request request) {
            requests->push_back(request); return ScopeReply();
        }, [discoveries] { ++*discoveries; return std::vector<std::wstring>{ L"C:\\", L"D:\\", L"E:\\" }; },
            [](const auto& path) { return SearchFolderScope{ path, { 77, { 1 }, true }, ERROR_SUCCESS }; });
        bool single_drive = true;
        for (const auto& root : { L"C:\\", L"D:\\" })
        {
            requests->clear();
            const bool done = SubmitScopeQuery(coordinator, { L"needle", SearchScope::CurrentVolume, SearchMode::Substring, root });
            const auto snapshot = state->Snapshot();
            single_drive &= done && snapshot.stage == SearchStage::Complete && requests->size() == 1 &&
                requests->front().volume_root == root && discoveries->load() == 0;
        }
        passed &= Check(single_drive, "search_current_volume_never_discovers_or_requests_another_drive");
        SearchQuery query{ L"needle", SearchScope::SelectedVolumes }; query.selected_volumes = L"d;C:;d:\\;C";
        requests->clear(); const bool multi_done = SubmitScopeQuery(coordinator, query);
        passed &= Check(multi_done && state->Snapshot().stage == SearchStage::Complete && requests->size() == 2 &&
            std::count_if(requests->begin(), requests->end(), [](const auto& request) { return request.volume_root == L"C:\\"; }) == 1 &&
            std::count_if(requests->begin(), requests->end(), [](const auto& request) { return request.volume_root == L"D:\\"; }) == 1 &&
            discoveries->load() == 0, "search_selected_volumes_query_only_deduplicated_user_roots_without_discovery");
        requests->clear(); query.selected_volumes = L"D:";
        const bool selected_done = SubmitScopeQuery(coordinator, query);
        passed &= Check(selected_done && requests->size() == 1 && requests->front().volume_root == L"D:\\" && discoveries->load() == 0,
            "search_single_selected_volume_does_not_expand_to_all_local_drives");
        bool invalid = true;
        for (const auto& value : { L"", L"D:\\Folder", L"\\\\host\\share" })
        {
            requests->clear(); query.selected_volumes = value;
            invalid &= SubmitScopeQuery(coordinator, query) && state->Snapshot().stage == SearchStage::Unavailable &&
                state->Snapshot().error != ERROR_SUCCESS && requests->empty() && discoveries->load() == 0;
        }
        passed &= Check(invalid, "search_invalid_selected_volume_input_fails_before_rpc_or_discovery");
        (void)coordinator.Shutdown();

        const auto retry_state = std::make_shared<SearchState>();
        const auto retry_requests = std::make_shared<std::vector<ipc::Request>>();
        const auto ready = std::make_shared<std::atomic_bool>(false);
        SearchCoordinator retry(retry_state, [retry_requests, ready](ipc::Request request) {
            retry_requests->push_back(request); auto reply = ScopeReply();
            if (request.volume_root == L"D:\\" && !ready->load())
            { reply.response.status = ipc::ResponseStatus::Busy; reply.response.error = ERROR_IO_PENDING; reply.response.index_state = ipc::IndexState::CatchingUp; }
            return reply;
        }, []() -> std::vector<std::wstring> { throw std::runtime_error("Explicit retry must not discover drives"); });
        query.selected_volumes = L"C:;D:";
        const bool first_done = SubmitScopeQuery(retry, query);
        auto changed = query; changed.selected_volumes = L"C:";
        const bool rejected = !retry.Search(changed, {}, true);
        ready->store(true);
        const bool retried = SubmitScopeQuery(retry, query, true);
        passed &= Check(first_done && rejected && retried && retry_state->Snapshot().stage == SearchStage::Complete &&
            retry_state->Snapshot().query == query && retry_requests->size() == 3 && retry_requests->back().volume_root == L"D:\\",
            "search_selected_volume_retry_keeps_submission_and_retries_only_unfinished_drive");
        (void)retry.Shutdown();
        return passed;
    }
    bool RunSelectedFolderCoordinatorTests(const std::filesystem::path& root)
    {
        using namespace blink::app;
        namespace fs = std::filesystem;
        namespace ipc = blink::index::ipc;
        bool passed = true;
        const auto folder = root / L"指定范围"; const auto sibling = root / L"指定范围-old";
        fs::create_directories(folder / L"nested"); fs::create_directories(sibling);
        const auto direct = folder / L"needle-direct.txt", deep = folder / L"nested" / L"needle-deep.txt", outside = sibling / L"needle-outside.txt";
        for (const auto& path : { direct, deep, outside }) { std::ofstream stream(path); stream << "owned scope fixture"; }
        const std::vector<ipc::SearchHit> hits{ ResultFor(direct).hit, ResultFor(deep).hit, ResultFor(outside).hit };
        const auto state = std::make_shared<SearchState>(); const auto requests = std::make_shared<std::vector<ipc::Request>>();
        SearchCoordinator coordinator(state, [requests, hits](ipc::Request request) {
            requests->push_back(request); return ScopeReply(hits);
        }, []() -> std::vector<std::wstring> { throw std::runtime_error("Selected folder must not discover drives"); });
        SearchQuery query{ L"needle", SearchScope::SelectedFolder, SearchMode::Substring, folder.wstring() };
        const bool recursive = SubmitScopeQuery(coordinator, query);
        auto snapshot = state->Snapshot();
        passed &= Check(recursive && snapshot.stage == SearchStage::Complete && snapshot.volumes.size() == 1 &&
            snapshot.volumes.front().results.size() == 2 && requests->size() == 1 && requests->front().type == ipc::RequestType::Search &&
            requests->front().path_prefix == folder.wstring() + L"\\",
            "search_selected_folder_recursive_query_keeps_real_nested_items_and_excludes_sibling_prefix");
        query.include_subfolders = false;
        const bool nonrecursive = SubmitScopeQuery(coordinator, query);
        snapshot = state->Snapshot();
        passed &= Check(nonrecursive && snapshot.stage == SearchStage::Complete && snapshot.volumes.size() == 1 &&
            snapshot.volumes.front().results.size() == 1 && snapshot.volumes.front().results.front().hit.full_path == direct.wstring() &&
            requests->size() == 2 && requests->back().type == ipc::RequestType::SearchDirectChildren &&
            requests->back().volume_root == SearchDriveRoot(folder.wstring()) &&
            !ValidateSearchResult(snapshot.volumes.front().results.front()).error,
            "search_selected_folder_nonrecursive_request_and_real_parent_filter_keep_only_direct_item");
        (void)coordinator.Shutdown();
        return passed;
    }
}

bool RunSearchIntegrationTests()
{
    using namespace blink::app;
    bool passed = true;
    passed &= RunTabSearchScopeTests();
    passed &= RunExplicitVolumeCoordinatorTests();
    const auto root = std::filesystem::absolute(std::filesystem::path(L".build") /
        L"search-integration-fixtures" / (L"pid-" + std::to_wstring(GetCurrentProcessId())));
    std::filesystem::create_directories(root / L"Unicode目录");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path, e); } } cleanup{root};
    passed &= RunSelectedFolderCoordinatorTests(root);
    const auto path = root / L"Unicode目录" / L"文件.txt";
    { std::ofstream f(path); f << "owned search fixture"; }
    const auto result = ResultFor(path);
    const auto target = ValidateSearchResult(result);
    passed &= Check(target.error == ERROR_SUCCESS && target.entry.identity == result.hit.identity &&
        target.entry.full_path == path.wstring() && target.parent == path.parent_path().wstring(),
        "search_action_real_unicode_file_and_parent_identity_validated");
    const auto directory = ResultFor(path.parent_path());
    passed &= Check(ValidateSearchResult(directory).error == ERROR_SUCCESS && ValidateSearchResult(directory).entry.is_directory,
        "search_action_real_directory_supported");
    auto changed = result; changed.hit.identity.file_id[0] ^= 1;
    passed &= Check(ValidateSearchResult(changed).error == ERROR_FILE_INVALID, "search_action_wrong_file_identity_rejected");
    changed = result; changed.hit.parent_identity.file_id[0] ^= 1;
    passed &= Check(ValidateSearchResult(changed).error == ERROR_FILE_INVALID, "search_action_wrong_parent_identity_rejected");
    changed = result; changed.hit.name = L"different.txt";
    passed &= Check(ValidateSearchResult(changed).error == ERROR_INVALID_DATA, "search_action_name_path_mismatch_rejected");
    changed = result; changed.source_volume = result.source_volume == L"C:\\" ? L"D:\\" : L"C:\\";
    passed &= Check(ValidateSearchResult(changed).error == ERROR_INVALID_DATA, "search_action_wrong_source_volume_rejected");
    const auto link = path.parent_path() / L"硬链接.txt";
    const auto linked = CreateHardLinkW(link.c_str(), path.c_str(), nullptr) != FALSE;
    const auto link_result = ResultFor(link);
    passed &= Check(linked && link_result.hit.identity == result.hit.identity &&
        ValidateSearchResult(link_result).error == ERROR_SUCCESS && !SameSearchLink(result, link_result),
        "search_action_hardlinks_keep_distinct_valid_targets");
    passed &= RunVolumeIdentityTests(result);
    std::filesystem::rename(path, path.parent_path() / L"moved.txt");
    passed &= Check(ValidateSearchResult(result).error != ERROR_SUCCESS, "search_action_renamed_path_rejected");
    { std::ofstream f(path); f << "replacement"; }
    passed &= Check(ValidateSearchResult(result).error == ERROR_FILE_INVALID, "search_action_same_name_replacement_rejected");
    std::filesystem::remove(path);
    passed &= Check(ValidateSearchResult(result).error != ERROR_SUCCESS, "search_action_deleted_path_rejected");

    const auto scope = ResolveSearchFolderScope(path.parent_path().wstring());
    passed &= Check(scope.error == ERROR_SUCCESS && scope.identity == result.hit.parent_identity,
        "search_scope_real_directory_identity_resolved");
    SearchVolumeResult volume; volume.root = result.source_volume; volume.searched = true;
    volume.stage = VolumeIndexStage::Healthy; volume.results = { link_result };
    FilterSearchFolderResults(scope, volume);
    passed &= Check(volume.searched && volume.results.size() == 1, "search_scope_real_current_folder_retains_own_link");
    auto fake_read = [](const std::wstring& p, blink::core::FileEntry& entry) -> DWORD {
        entry.is_directory = true; entry.attributes = FILE_ATTRIBUTE_DIRECTORY;
        entry.identity = { 77, { static_cast<std::uint8_t>(p == L"D:\\Folder" ? 7 : p == L"D:\\folder" ? 8 : 1) }, true };
        return ERROR_SUCCESS;
    };
    const auto case_scope = ResolveSearchFolderScope(L"D:\\Folder", fake_read);
    volume = {}; volume.root = L"D:\\"; volume.searched = true; volume.stage = VolumeIndexStage::Healthy;
    auto outside = result; outside.hit.full_path = L"D:\\folder\\file.txt";
    volume.results = { outside };
    FilterSearchFolderResults(case_scope, volume, fake_read);
    passed &= Check(case_scope.error == ERROR_SUCCESS && volume.searched && volume.results.empty(),
        "search_scope_case_sensitive_sibling_identity_is_excluded");
    volume = {}; volume.root = L"D:\\"; volume.searched = true; volume.stage = VolumeIndexStage::Healthy;
    auto wrong_parent = outside; wrong_parent.hit.full_path = L"D:\\Folder\\file.txt";
    wrong_parent.hit.name = L"file.txt"; wrong_parent.hit.parent_identity = { 77, { 8 }, true };
    volume.results = { wrong_parent };
    FilterSearchFolderResults(case_scope, volume, fake_read);
    passed &= Check(!volume.searched && volume.error == ERROR_FILE_INVALID && volume.results.empty(),
        "search_scope_stale_index_parent_cannot_enter_current_directory");
    auto reparse_read = [fake_read](const std::wstring& p, blink::core::FileEntry& entry) -> DWORD {
        const auto error = fake_read(p, entry);
        if (p == L"D:\\Folder") { entry.attributes |= FILE_ATTRIBUTE_REPARSE_POINT; }
        return error;
    };
    passed &= Check(ResolveSearchFolderScope(L"D:\\Folder\\child", reparse_read).error == ERROR_NOT_SUPPORTED,
        "search_scope_reparse_ancestor_explicitly_unsupported");
    auto stale_scope = case_scope; stale_scope.identity.file_id[0] ^= 1;
    volume.searched = true; volume.results = { outside };
    FilterSearchFolderResults(stale_scope, volume, fake_read);
    passed &= Check(!volume.searched && volume.error == ERROR_FILE_INVALID && volume.results.empty(),
        "search_scope_replaced_directory_rejects_response");
    {
        const auto search_state = std::make_shared<SearchState>();
        const auto calls = std::make_shared<std::atomic<unsigned>>(0);
        SearchCoordinator invalid_scope(search_state, [calls](auto) { ++*calls; return IndexRpcResult{}; }, {},
            [](const auto& folder) { return SearchFolderScope{ folder, {}, ERROR_NOT_SUPPORTED }; });
        const auto finished = std::make_shared<std::promise<void>>(); auto future = finished->get_future();
        (void)invalid_scope.Search({ L"fixture", SearchScope::CurrentFolder, SearchMode::Substring, L"D:\\Folder" },
            [finished](auto) { finished->set_value(); });
        passed &= Check(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready && calls->load() == 0 &&
            search_state->Snapshot().error == ERROR_NOT_SUPPORTED,
            "search_coordinator_scope_preflight_rejects_alias_without_rpc");
        (void)invalid_scope.Shutdown();
    }
    {
        SearchState state;
        SearchQuery query{ L"fixture" }; const auto generation = state.Begin(query);
        SearchSnapshot snapshot; snapshot.query = query; snapshot.stage = SearchStage::Complete;
        snapshot.scope_identity = { 77, { 7 }, true };
        (void)state.Complete(generation, snapshot);
        (void)state.Begin({ L"new query" });
        passed &= Check(!state.Snapshot().scope_identity.valid,
            "search_new_query_does_not_reuse_previous_scope_identity");
    }
    SearchRequestQueue queue;
    queue.Enqueue({ 1, { L"first" }, false });
    queue.Enqueue({ 2, { L"second" }, false });
    queue.Enqueue({ 1, { L"latest" }, false });
    auto ticket = queue.TakeNext();
    passed &= Check(ticket && ticket->request.tab_id == 1 && ticket->request.query.text == L"latest" && queue.Busy(),
        "search_queue_keeps_latest_pending_query_per_tab");
    passed &= Check(!queue.TakeNext(), "search_queue_serializes_window_native_requests");
    passed &= Check(!queue.Finish(999), "search_queue_rejects_stale_completion_ticket");
    if (ticket) { passed &= Check(queue.Finish(ticket->serial), "search_queue_accepts_current_native_completion"); }
    ticket = queue.TakeNext();
    passed &= Check(ticket && ticket->request.tab_id == 2, "search_queue_other_tab_keeps_fifo_position");
    queue.Enqueue({ 3, { L"cancelled" }, false }); queue.RemoveTab(3);
    passed &= Check(!queue.HasPending() && queue.Busy(), "search_queue_remove_pending_does_not_release_active_rpc");
    queue.Close(); queue.Enqueue({ 4, { L"late" }, false });
    passed &= Check(!queue.TakeNext() && !queue.HasPending(), "search_queue_close_rejects_new_intents");

    TabWindowState window;
    const auto first = window.ActiveTabId(); const auto second = window.CreateTab();
    auto* first_tab = window.Find(first); auto* second_tab = window.Find(second);
    passed &= Check(first_tab->search_state && first_tab->search_controller && second_tab->search_state &&
        first_tab->search_state != second_tab->search_state, "search_tab_owns_independent_state_and_controller");
    first_tab->search_draft = L"one"; second_tab->search_draft = L"two";
    first_tab->EnterSearch();
    passed &= Check(first_tab->search_visible && !second_tab->search_visible && second_tab->search_draft == L"two",
        "search_tab_visibility_and_draft_are_isolated");
    auto committed_query = std::make_shared<SearchSnapshot>();
    committed_query->query = { L"submitted", SearchScope::AllLocalVolumes, SearchMode::Prefix };
    second_tab->search_snapshot = committed_query;
    second_tab->search_draft = L"typed only"; second_tab->search_mode = SearchMode::Extension;
    passed &= Check(second_tab->SearchIntent(true).text == L"submitted" && second_tab->SearchIntent(true).mode == SearchMode::Prefix,
        "search_refresh_uses_submitted_query_without_committing_draft");
    passed &= Check(second_tab->SearchIntent(false).text == L"typed only" && second_tab->SearchIntent(false).mode == SearchMode::Extension,
        "search_enter_uses_current_tab_draft_and_options");
    blink::core::DirectoryListing locate_listing; locate_listing.path = L"D:\\Parent";
    blink::core::FileEntry link_a; link_a.name = L"a.txt"; link_a.full_path = L"D:\\Parent\\a.txt";
    link_a.identity = { 77, { 9 }, true };
    auto link_b = link_a; link_b.name = L"b.txt"; link_b.full_path = L"D:\\Parent\\b.txt";
    locate_listing.entries = { link_a, link_b };
    const auto locate_request = second_tab->state->Navigate(locate_listing.path);
    if (locate_request)
    {
        (void)second_tab->state->CommitBrowse(*locate_request, locate_listing);
        second_tab->locate_after_browse = MakeRowIdentity(link_b);
        second_tab->locate_navigation_token = locate_request->token;
        passed &= Check(!second_tab->FinishLocate(locate_request->token + 1) && second_tab->locate_after_browse,
            "search_locate_ignores_stale_browse_completion");
        passed &= Check(second_tab->FinishLocate(locate_request->token) && second_tab->state->IsRowSelected(MakeRowIdentity(link_b)) &&
            !second_tab->state->IsRowSelected(MakeRowIdentity(link_a)),
            "search_locate_selects_exact_hardlink_name_after_commit");
        second_tab->state->SetRowSelection({ MakeRowIdentity(link_a) });
        second_tab->locate_after_browse = RowIdentity{ link_a.identity, L"gone.txt" };
        passed &= Check(!second_tab->FinishLocate(locate_request->token) &&
            second_tab->state->PresentationSnapshot().selected_count == 0 && second_tab->search_action_error == ERROR_FILE_INVALID,
            "search_locate_missing_target_clears_old_selection_and_reports_changed");
    }
    else { passed &= Check(false, "search_locate_navigation_fixture"); }
    first_tab->OnNavigation(true);
    passed &= Check(first_tab->search_visible, "search_tab_watcher_refresh_keeps_search_surface");
    first_tab->OnNavigation(false);
    passed &= Check(!first_tab->search_visible && !first_tab->search_selection,
        "search_tab_explicit_navigation_exits_search");
    if (first_tab->search_state)
    {
        auto retained = first_tab->search_state;
        first_tab->EnterSearch();
        const auto before = first_tab->state->PresentationSnapshot();
        SearchQuery query{ L"fixture" }; const auto token = retained->Begin(query);
        first_tab->LeaveSearch();
        passed &= Check(!retained->IsCurrent(token) && !first_tab->search_visible &&
            first_tab->state->PresentationSnapshot().committed_location == before.committed_location,
            "search_tab_exit_cancels_generation_without_changing_browse");
        (void)window.CloseTab(first);
        passed &= Check(retained->Snapshot().stage == SearchStage::Closed,
            "search_tab_close_invalidates_retained_search_state");
    }
    SearchSnapshot view_snapshot; view_snapshot.stage = SearchStage::Complete;
    SearchVolumeResult visible_volume; visible_volume.searched = true; visible_volume.stage = VolumeIndexStage::Healthy;
    auto visible = link_result; auto hidden = link_result; hidden.hit.name = L"hidden.txt";
    hidden.hit.full_path += L"hidden"; hidden.hit.attributes = FILE_ATTRIBUTE_HIDDEN;
    auto system = link_result; system.hit.attributes = FILE_ATTRIBUTE_SYSTEM;
    visible_volume.results = { visible, hidden, system }; visible_volume.limit_reached = true;
    view_snapshot.volumes = { visible_volume };
    auto view = BuildSearchView(view_snapshot, {}, visible);
    passed &= Check(view.rows.size() == 1 && view.selected_index == 0 && view.limit_reached,
        "search_view_respects_shared_visibility_and_restores_link_selection");
    passed &= Check(BuildSearchView(view_snapshot, { true }, {}).rows.size() == 2,
        "search_view_hidden_toggle_keeps_system_items_excluded");
    view_snapshot.volumes[0].searched = false;
    passed &= Check(BuildSearchView(view_snapshot, { true }, {}).rows.empty(),
        "search_view_does_not_render_rows_from_failed_volume");
    return passed;
}

namespace
{
    struct ScopeCacheFixture
    {
        std::unordered_map<std::wstring, blink::core::FileEntry> entries;
        std::unordered_map<std::wstring, DWORD> errors;
        std::unordered_map<std::wstring, std::size_t> reads;
        std::function<void(const std::wstring&)> before_read;
        std::size_t calls{};

        ScopeCacheFixture()
        {
            Directory(L"D:\\", 1);
            Directory(L"D:\\Scope", 2);
            Directory(L"D:\\Scope\\shared", 3);
        }
        void Directory(const std::wstring& path, const std::uint8_t id)
        {
            blink::core::FileEntry entry;
            entry.full_path = path; entry.is_directory = true; entry.attributes = FILE_ATTRIBUTE_DIRECTORY;
            entry.identity = { 77, { id }, true }; entries[path] = std::move(entry);
        }
        blink::app::SearchResult Hit(const std::wstring& parent, const std::wstring& name, const std::uint8_t id = 32)
        {
            blink::core::FileEntry entry;
            entry.name = name; entry.full_path = blink::core::JoinPath(parent, name);
            entry.attributes = FILE_ATTRIBUTE_NORMAL; entry.identity = { 77, { id }, true };
            entries[entry.full_path] = entry;
            blink::app::SearchResult result; result.source_volume = L"D:\\";
            result.hit.name = name; result.hit.full_path = entry.full_path;
            result.hit.identity = entry.identity; result.hit.parent_identity = entries.at(parent).identity;
            result.hit.attributes = entry.attributes; return result;
        }
        std::vector<blink::app::SearchResult> Hits(const std::wstring& parent, const std::size_t count = 200)
        {
            std::vector<blink::app::SearchResult> results;
            for (std::size_t index{}; index < count; ++index)
            { results.push_back(Hit(parent, L"文件-" + std::to_wstring(index) + L".txt", static_cast<std::uint8_t>(32 + index))); }
            return results;
        }
        blink::app::SearchMetadataReader Reader()
        {
            return [this](const std::wstring& path, blink::core::FileEntry& entry) -> DWORD {
                ++calls; ++reads[path];
                if (before_read) { before_read(path); }
                if (const auto error = errors.find(path); error != errors.end()) { return error->second; }
                const auto found = entries.find(path);
                if (found == entries.end()) { return ERROR_PATH_NOT_FOUND; }
                entry = found->second; return ERROR_SUCCESS;
            };
        }
        std::size_t ReadCount(const std::wstring& path) const
        { const auto found = reads.find(path); return found == reads.end() ? 0 : found->second; }
        void ResetReads() { calls = 0; reads.clear(); }
    };
    blink::app::SearchVolumeResult ScopeCacheVolume(std::vector<blink::app::SearchResult> results)
    {
        blink::app::SearchVolumeResult volume;
        volume.root = L"D:\\"; volume.searched = true; volume.stage = blink::app::VolumeIndexStage::Healthy;
        volume.results = std::move(results); return volume;
    }
    bool ScopeCacheFailure(const blink::app::SearchVolumeResult& volume, const DWORD error)
    {
        return !volume.searched && volume.stage == blink::app::VolumeIndexStage::Unavailable &&
            volume.results.empty() && volume.error == error;
    }
    std::string ScopeFixtureText(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }
}

bool RunSearchScopeCacheTests()
{
    using namespace blink::app;
    namespace fs = std::filesystem;
    bool passed = true;
    const std::wstring folder = L"D:\\Scope", shared = L"D:\\Scope\\shared";
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader();
        const auto scope = ResolveSearchFolderScope(folder, reader);
        const auto hits = fixture.Hits(shared); auto volume = ScopeCacheVolume(hits);
        fixture.ResetReads(); FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(volume.searched && volume.results.size() == 200 &&
            std::equal(volume.results.begin(), volume.results.end(), hits.begin(), [](const auto& left, const auto& right) {
                return left.hit.full_path == right.hit.full_path && left.hit.identity == right.hit.identity &&
                    left.hit.parent_identity == right.hit.parent_identity;
            }), "search_scope_cache_preserves_all_200_same_parent_results_and_order");
        passed &= Check(fixture.calls <= 8 && fixture.ReadCount(shared) == 2,
            "search_scope_cache_200_same_parent_uses_at_most_8_reads_instead_of_1002_and_rechecks_parent");
        std::cout << "INFO search_scope_cache same_parent_old_reads=1002 actual_reads=" << fixture.calls << '\n';
    }
    {
        ScopeCacheFixture fixture; std::wstring parent = folder;
        for (std::uint8_t index{}; index < 20; ++index)
        { parent += L"\\depth-" + std::to_wstring(index); fixture.Directory(parent, static_cast<std::uint8_t>(10 + index)); }
        const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto volume = ScopeCacheVolume(fixture.Hits(parent)); fixture.ResetReads();
        FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(volume.searched && volume.results.size() == 200 && fixture.calls <= 46 && fixture.ReadCount(parent) == 2,
            "search_scope_cache_deep_shared_chain_uses_at_most_46_reads_instead_of_4802");
        std::cout << "INFO search_scope_cache deep_chain_old_reads=4802 actual_reads=" << fixture.calls << '\n';
    }
    {
        ScopeCacheFixture fixture; std::vector<SearchResult> hits;
        for (std::uint8_t index{}; index < 5; ++index)
        {
            const auto parent = folder + L"\\parent-" + std::to_wstring(index);
            fixture.Directory(parent, static_cast<std::uint8_t>(10 + index));
            auto children = fixture.Hits(parent, 40); hits.insert(hits.end(), children.begin(), children.end());
        }
        const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto volume = ScopeCacheVolume(std::move(hits)); fixture.ResetReads();
        FilterSearchFolderResults(scope, volume, reader);
        bool parents_rechecked = true;
        for (unsigned index{}; index < 5; ++index)
        { parents_rechecked &= fixture.ReadCount(folder + L"\\parent-" + std::to_wstring(index)) == 2; }
        passed &= Check(volume.searched && volume.results.size() == 200 && fixture.calls <= 24 && parents_rechecked,
            "search_scope_cache_mixed_parents_share_prefix_reads_but_each_parent_is_freshly_rechecked");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader();
        const auto scope = ResolveSearchFolderScope(L"\\\\?\\D:\\Scope\\", reader);
        auto hits = fixture.Hits(shared);
        for (std::size_t index{}; index < hits.size(); ++index)
        {
            hits[index].hit.full_path = index % 2 ? L"\\\\?\\" + hits[index].hit.full_path :
                folder + L"\\shared\\..\\shared\\" + hits[index].hit.name;
        }
        auto volume = ScopeCacheVolume(hits); fixture.ResetReads(); FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(scope.path == folder && volume.searched && volume.results.size() == 200 && fixture.calls <= 8 &&
            volume.results.front().hit.full_path == hits.front().hit.full_path,
            "search_scope_cache_extended_and_lexical_aliases_share_canonical_spelling_without_rewriting_hits");
    }
    {
        ScopeCacheFixture fixture; fixture.Directory(L"D:\\scope", 8); fixture.Directory(L"D:\\scope\\child", 9);
        const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto hits = fixture.Hits(shared, 2); hits.push_back(fixture.Hit(L"D:\\scope\\child", L"outside.txt"));
        auto volume = ScopeCacheVolume(std::move(hits)); fixture.ResetReads(); FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(volume.searched && volume.results.size() == 2 && fixture.ReadCount(L"D:\\scope") == 1 &&
            fixture.ReadCount(L"D:\\scope\\child") == 0,
            "search_scope_cache_case_sensitive_scope_spelling_with_different_identity_stays_outside");
    }
    {
        ScopeCacheFixture fixture; fixture.Directory(folder + L"\\Child", 8); fixture.Directory(folder + L"\\child", 9);
        const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto volume = ScopeCacheVolume({ fixture.Hit(folder + L"\\Child", L"a.txt"), fixture.Hit(folder + L"\\child", L"b.txt") });
        fixture.ResetReads(); FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(volume.searched && volume.results.size() == 2 &&
            volume.results[0].hit.parent_identity != volume.results[1].hit.parent_identity &&
            fixture.ReadCount(folder + L"\\Child") == 2 && fixture.ReadCount(folder + L"\\child") == 2,
            "search_scope_cache_case_sensitive_parents_keep_separate_identities_and_fresh_checks");
    }
    {
        ScopeCacheFixture fixture; fixture.Directory(L"D:\\scope", 2); fixture.Directory(L"D:\\scope\\child", 9);
        const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto volume = ScopeCacheVolume({ fixture.Hit(L"D:\\scope\\child", L"a.txt") }); fixture.ResetReads();
        fixture.before_read = [&](const auto& path) {
            if (path == L"D:\\scope" && fixture.ReadCount(path) == 2) { fixture.entries.at(path).identity.file_id[0] = 8; }
        };
        FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(ScopeCacheFailure(volume, ERROR_FILE_INVALID),
            "search_scope_cache_freshly_rechecks_actual_scope_spelling_not_just_leaf_parent_identity");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto hits = fixture.Hits(shared, 1); auto outside = hits.front();
        for (const auto& path : { L"D:\\Scope-old\\file.txt", L"C:\\Scope\\file.txt", L"D:\\Other\\file.txt", L"D:\\Scope" })
        { outside.hit.full_path = path; hits.push_back(outside); }
        auto volume = ScopeCacheVolume(std::move(hits)); fixture.ResetReads(); FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(volume.searched && volume.results.size() == 1 && fixture.calls <= 8 &&
            fixture.ReadCount(L"D:\\Scope-old") == 0 && fixture.ReadCount(L"C:\\") == 0,
            "search_scope_cache_outside_paths_and_sibling_prefixes_never_enter_metadata_validation");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(L"D:\\", reader);
        auto volume = ScopeCacheVolume(fixture.Hits(folder)); fixture.ResetReads(); FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(volume.searched && volume.results.size() == 200 && fixture.calls <= 5 && fixture.ReadCount(folder) == 2,
            "search_scope_cache_drive_root_retains_descendants_and_fresh_parent_check");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader(); auto scope = ResolveSearchFolderScope(folder, reader);
        auto volume = ScopeCacheVolume(fixture.Hits(shared, 1)); scope.identity.file_id[0] ^= 1;
        FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(ScopeCacheFailure(volume, ERROR_FILE_INVALID),
            "search_scope_cache_stale_initial_scope_identity_clears_the_volume");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto volume = ScopeCacheVolume(fixture.Hits(shared, 1)); fixture.errors[folder] = ERROR_ACCESS_DENIED;
        FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(ScopeCacheFailure(volume, ERROR_ACCESS_DENIED),
            "search_scope_cache_preserves_initial_scope_metadata_error");
    }
    {
        ScopeCacheFixture fixture; fixture.Directory(folder + L"\\denied", 9);
        const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto hits = fixture.Hits(shared, 1); hits.push_back(fixture.Hit(folder + L"\\denied", L"b.txt"));
        auto volume = ScopeCacheVolume(std::move(hits)); fixture.errors[folder + L"\\denied"] = ERROR_SHARING_VIOLATION;
        FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(ScopeCacheFailure(volume, ERROR_SHARING_VIOLATION),
            "search_scope_cache_later_parent_metadata_error_discards_already_filtered_prefix");
    }
    {
        ScopeCacheFixture fixture; fixture.Directory(folder + L"\\junction", 8); fixture.Directory(folder + L"\\junction\\child", 9);
        const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto volume = ScopeCacheVolume({ fixture.Hit(folder + L"\\junction\\child", L"a.txt") });
        fixture.entries.at(folder + L"\\junction").attributes |= FILE_ATTRIBUTE_REPARSE_POINT;
        fixture.ResetReads(); FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(ScopeCacheFailure(volume, ERROR_NOT_SUPPORTED) && fixture.ReadCount(folder + L"\\junction\\child") == 0,
            "search_scope_cache_reparse_ancestor_stays_unsupported_without_reading_through_it");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto hits = fixture.Hits(shared, 2); hits.back().hit.parent_identity.file_id[0] ^= 1;
        auto volume = ScopeCacheVolume(std::move(hits)); FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(ScopeCacheFailure(volume, ERROR_FILE_INVALID),
            "search_scope_cache_still_checks_every_indexed_parent_identity");
    }
    {
        ScopeCacheFixture fixture; fixture.Directory(folder + L"\\trigger", 9);
        const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto hits = fixture.Hits(shared); hits.insert(hits.begin() + 1, fixture.Hit(folder + L"\\trigger", L"a.txt"));
        auto volume = ScopeCacheVolume(std::move(hits)); fixture.ResetReads();
        fixture.before_read = [&](const auto& path) {
            if (path == folder + L"\\trigger") { fixture.entries.at(shared).identity.file_id[0] = 10; }
        };
        FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(ScopeCacheFailure(volume, ERROR_FILE_INVALID) && fixture.ReadCount(shared) == 2,
            "search_scope_cache_parent_replaced_mid_batch_is_rejected_by_fresh_final_check");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto volume = ScopeCacheVolume(fixture.Hits(shared)); fixture.ResetReads();
        fixture.before_read = [&](const auto& path) {
            if (path == shared && fixture.ReadCount(path) == 2) { fixture.errors[path] = ERROR_ACCESS_DENIED; }
        };
        FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(ScopeCacheFailure(volume, ERROR_ACCESS_DENIED),
            "search_scope_cache_preserves_final_parent_metadata_error_and_clears_results");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto volume = ScopeCacheVolume(fixture.Hits(shared)); fixture.ResetReads();
        fixture.before_read = [&](const auto& path) {
            if (path == shared && fixture.ReadCount(path) == 2) { fixture.entries.at(path).attributes |= FILE_ATTRIBUTE_REPARSE_POINT; }
        };
        FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(ScopeCacheFailure(volume, ERROR_NOT_SUPPORTED),
            "search_scope_cache_parent_becomes_reparse_during_batch_is_not_hidden");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        auto volume = ScopeCacheVolume(fixture.Hits(shared));
        fixture.before_read = [&](const auto& path) {
            if (path == shared) { fixture.entries.at(folder).identity.file_id[0] = 10; }
        };
        FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(ScopeCacheFailure(volume, ERROR_FILE_INVALID),
            "search_scope_cache_scope_replaced_after_initial_validation_is_freshly_rejected");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        const auto hits = fixture.Hits(shared); auto first = ScopeCacheVolume(hits); FilterSearchFolderResults(scope, first, reader);
        fixture.entries.at(shared).identity.file_id[0] = 10; fixture.ResetReads();
        auto second = ScopeCacheVolume(hits); FilterSearchFolderResults(scope, second, reader);
        passed &= Check(first.searched && first.results.size() == 200 && ScopeCacheFailure(second, ERROR_FILE_INVALID) &&
            fixture.ReadCount(folder) != 0 && fixture.ReadCount(shared) != 0,
            "search_scope_cache_independent_filter_call_reopens_changed_directories");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        const auto hit = fixture.Hit(shared, L"a.txt"); auto volume = ScopeCacheVolume({ hit });
        FilterSearchFolderResults(scope, volume, reader); fixture.ResetReads();
        fixture.entries.at(hit.hit.full_path).identity.file_id[0] ^= 1;
        const auto target = ValidateSearchResult(hit, reader);
        passed &= Check(volume.searched && target.error == ERROR_FILE_INVALID &&
            fixture.ReadCount(shared) == 1 && fixture.ReadCount(hit.hit.full_path) == 1,
            "search_scope_cache_does_not_supply_open_locate_validation_with_cached_file_or_parent");
    }
    {
        ScopeCacheFixture fixture; const auto reader = fixture.Reader(); const auto scope = ResolveSearchFolderScope(folder, reader);
        const auto hit = fixture.Hit(shared, L"a.txt"); auto volume = ScopeCacheVolume({ hit });
        FilterSearchFolderResults(scope, volume, reader); fixture.ResetReads();
        fixture.before_read = [&](const auto& path) {
            if (path == hit.hit.full_path) { fixture.entries.at(shared).identity.file_id[0] = 10; }
        };
        const auto target = ValidateSearchResult(hit, reader);
        passed &= Check(volume.searched && target.error == ERROR_FILE_INVALID &&
            fixture.ReadCount(shared) == 2 && fixture.ReadCount(hit.hit.full_path) == 1,
            "search_scope_cache_leaves_open_locate_post_file_parent_revalidation_fresh");
    }
    {
        const auto root = fs::absolute(fs::path(L".build") / L"search-integration-fixtures" /
            (L"scope-cache-pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
        struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{ root };
        const auto real_folder = root / L"Unicode目录", real_parent = real_folder / L"共享父目录";
        const auto trigger = real_folder / L"trigger", file = real_parent / L"文件.txt";
        fs::create_directories(real_parent); fs::create_directories(trigger);
        const std::string original = "owned scope cache fixture\n";
        { std::ofstream stream(file, std::ios::binary); stream << original; }
        { std::ofstream stream(trigger / L"trigger.txt"); stream << "owned trigger"; }
        std::size_t calls{};
        const SearchMetadataReader reader = [&](const auto& path, auto& entry) -> DWORD {
            ++calls;
            if (blink::core::ReadEntryMetadata(path, entry)) { return ERROR_SUCCESS; }
            const auto error = GetLastError(); return error ? error : ERROR_FILE_INVALID;
        };
        const auto scope = ResolveSearchFolderScope(real_parent.wstring(), reader); const auto depth = calls;
        const auto hit = ResultFor(file); auto volume = VolumeFor(hit); volume.results.assign(200, hit); calls = 0;
        FilterSearchFolderResults(scope, volume, reader);
        passed &= Check(!scope.error && volume.searched && volume.results.size() == 200 && calls <= 2 * depth &&
            volume.results.front().hit.identity == hit.hit.identity &&
            volume.results.front().hit.parent_identity == scope.identity && ScopeFixtureText(file) == original,
            "search_scope_cache_real_unicode_parent_200_hits_use_two_chain_reads_and_leave_contents_unchanged");

        const auto broader = ResolveSearchFolderScope(real_folder.wstring()); const auto trigger_hit = ResultFor(trigger / L"trigger.txt");
        const auto saved = real_folder / L"原目录保留"; bool replaced{};
        const SearchMetadataReader replacing_reader = [&](const auto& path, auto& entry) -> DWORD {
            if (!replaced && path == trigger.wstring())
            {
                fs::rename(real_parent, saved); fs::create_directory(real_parent);
                { std::ofstream stream(file); stream << "replacement directory contents"; }
                replaced = true;
            }
            return reader(path, entry);
        };
        volume = VolumeFor(hit); volume.results = { hit, trigger_hit, hit };
        FilterSearchFolderResults(broader, volume, replacing_reader);
        blink::core::FileEntry new_parent, saved_parent;
        const bool identities = blink::core::ReadEntryMetadata(real_parent.wstring(), new_parent) &&
            blink::core::ReadEntryMetadata(saved.wstring(), saved_parent);
        passed &= Check(replaced && identities && ScopeCacheFailure(volume, ERROR_FILE_INVALID) &&
            new_parent.identity != hit.hit.parent_identity && saved_parent.identity == hit.hit.parent_identity &&
            ScopeFixtureText(saved / file.filename()) == original,
            "search_scope_cache_real_parent_replacement_mid_batch_rejects_old_hits_without_changing_original_file");

        const auto removed_scope = ResolveSearchFolderScope(real_parent.wstring()); const auto replacement_hit = ResultFor(file);
        fs::rename(real_parent, real_folder / L"替换目录保留");
        blink::core::FileEntry missing_parent; const auto missing_error = reader(real_parent.wstring(), missing_parent);
        volume = VolumeFor(replacement_hit); FilterSearchFolderResults(removed_scope, volume, reader);
        passed &= Check(!removed_scope.error && missing_error != ERROR_SUCCESS && ScopeCacheFailure(volume, missing_error),
            "search_scope_cache_real_missing_parent_preserves_actual_metadata_error_and_clears_results");
    }
    return passed;
}
