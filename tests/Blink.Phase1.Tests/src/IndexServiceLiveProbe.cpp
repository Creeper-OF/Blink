#include "IndexServiceLiveProbe.h"
#include "Blink/App/IndexServiceCoordinator.h"
#include "Blink/App/SearchCoordinator.h"
#include "Blink/App/SearchResultValidation.h"
#include "Blink/App/TabWindowState.h"
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#pragma comment(lib, "Ole32.lib")

namespace
{
    using namespace blink::app;
    namespace ipc = blink::index::ipc;
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;
    std::string Utf8(const std::wstring& value)
    {
        const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>((std::max)(0, size)), '\0');
        if (size) { WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr); }
        return result;
    }
    bool Report(const bool passed, const char* stage, const std::wstring& root, const DWORD error = ERROR_SUCCESS)
    {
        std::cout << "LIVE " << (passed ? "PASS " : "FAIL ") << stage << " root=" << Utf8(root) << " error=" << error << std::endl;
        return passed;
    }
    fs::path ProjectRoot()
    {
        wchar_t module[32768]{};
        const auto size = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
        if (!size || size >= std::size(module)) { return {}; }
        auto path = fs::path(module).parent_path();
        for (int depth = 0; depth < 10 && !path.empty(); ++depth, path = path.parent_path())
        { if (fs::is_regular_file(path / L"Blink.sln")) { return path; } }
        return {};
    }
    struct Connection
    {
        ipc::Client client;
        IndexRpcResult Exchange(ipc::Request request)
        {
            IndexRpcResult result;
            if (!client.IsConnected() && !client.Connect(1000, &result.error)) { return result; }
            result.received = client.Send(std::move(request), result.response, 5000, &result.error);
            if (result.received) { result.expected_request_id = result.response.request_id; }
            return result;
        }
    };
    bool WaitForIndex(Connection& connection, const std::wstring& root, const bool allow_startup_wait)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(600);
        auto next_progress = Clock::now(); DWORD last_error = ERROR_NOT_READY;
        while (Clock::now() < deadline)
        {
            ipc::Request request; request.type = ipc::RequestType::QueryVolumeStatus; request.volume_root = root;
            const auto status = connection.Exchange(request); last_error = ValidateIndexReply(status);
            if (!last_error)
            {
                const auto& reply = status.response; last_error = reply.error;
                if (reply.status == ipc::ResponseStatus::Ok && !reply.error && reply.index_state == ipc::IndexState::Healthy)
                {
                    std::cout << "LIVE INFO index_counts root=" << Utf8(root) << " files=" << reply.files <<
                        " directories=" << reply.directories << " records=" << reply.records_seen << std::endl;
                    return Report(true, "index_ready", root);
                }
                const bool pending = reply.status == ipc::ResponseStatus::Busy || reply.error == ERROR_IO_PENDING ||
                    reply.index_state == ipc::IndexState::CatchingUp || reply.index_state == ipc::IndexState::Rebuilding;
                if (!pending) { return Report(false, "index_ready", root, last_error ? last_error : ERROR_NOT_READY); }
                request.type = ipc::RequestType::QueryIndexProgress; request.operation_id = 0;
                const auto progress = connection.Exchange(request);
                if (!ValidateIndexReply(progress) && progress.response.status == ipc::ResponseStatus::Ok && Clock::now() >= next_progress)
                {
                    std::cout << "LIVE INFO index_progress root=" << Utf8(root) << " operation=" << progress.response.operation_id <<
                        " records=" << progress.response.records_seen << " error=" << progress.response.error << std::endl;
                    next_progress = Clock::now() + std::chrono::seconds(5);
                }
            }
            else if (!allow_startup_wait) { return Report(false, "service_connection", root, last_error); }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return Report(false, "index_ready_timeout", root, last_error ? last_error : ERROR_TIMEOUT);
    }
    std::optional<SearchResult> WaitForSearch(Connection& connection, const std::wstring& root, const fs::path& folder,
        const std::wstring& token, const std::wstring& expected_name, const std::wstring& absent_name, const bool expected_present,
        const char* stage, bool& succeeded)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(60); DWORD last_error = ERROR_NOT_FOUND;
        const auto actual_volume = ResolveSearchFolderScope(root);
        const auto scope = ResolveSearchFolderScope(folder.wstring());
        if (actual_volume.error || scope.error)
        { Report(false, stage, root, actual_volume.error ? actual_volume.error : scope.error); succeeded = false; return {}; }
        SearchQuery query; query.text = token; query.scope = SearchScope::CurrentFolder; query.folder = folder.wstring(); query.limit = 200;
        while (Clock::now() < deadline)
        {
            auto volume = MapSearchReply(query, root, connection.Exchange(MakeSearchRequest(query, root)));
            if (volume.searched)
            {
                BindSearchVolumeIdentity(volume, actual_volume);
                FilterSearchFolderResults(scope, volume);
                const auto found = std::find_if(volume.results.begin(), volume.results.end(), [&](const auto& result) {
                    return result.hit.name == expected_name;
                });
                const bool absent = absent_name.empty() || std::none_of(volume.results.begin(), volume.results.end(), [&](const auto& result) {
                    return result.hit.name == absent_name;
                });
                if (volume.searched && absent && (expected_present ? found != volume.results.end() : volume.results.empty()))
                {
                    succeeded = Report(true, stage, root);
                    return expected_present ? std::optional<SearchResult>{ *found } : std::nullopt;
                }
            }
            last_error = volume.error ? volume.error : ERROR_NOT_FOUND;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        succeeded = false; Report(false, stage, root, last_error); return {};
    }
    bool ValidateAction(const SearchResult& hit, const fs::path& path, const char* stage)
    {
        const auto target = ValidateSearchResult(hit);
        blink::core::FileEntry actual;
        const bool read = blink::core::ReadEntryMetadata(path.wstring(), actual);
        const bool valid = !target.error && read && target.entry.identity == actual.identity &&
            target.entry.full_path == path.wstring() && !target.parent.empty() && target.parent_identity.valid;
        return Report(valid, stage, hit.source_volume, target.error ? target.error : valid ? ERROR_SUCCESS : ERROR_FILE_INVALID);
    }
    bool VerifyDefaultCoordinatorSearch(const fs::path& project, const fs::path& sentinel, const std::wstring& token)
    {
        const bool c_store_present = fs::is_regular_file(project / L".blink-data" / L"index" / L"c.blink5");
        const auto state = std::make_shared<SearchState>();
        SearchCoordinator coordinator(state, {}, [] { return std::vector<std::wstring>{ L"C:\\", L"D:\\" }; });
        const auto generation = coordinator.Search({ token, SearchScope::AllLocalVolumes, SearchMode::Substring }, {});
        const auto action = coordinator.CurrentAction();
        if (!generation || !action) { return Report(false, "default_coordinator_search_start", L"D:\\", ERROR_BUSY); }
        action.get();
        const auto snapshot = state->Snapshot();
        (void)coordinator.Shutdown();
        const auto c = std::find_if(snapshot.volumes.begin(), snapshot.volumes.end(), [](const auto& volume) {
            return volume.root == L"C:\\";
        });
        const auto d = std::find_if(snapshot.volumes.begin(), snapshot.volumes.end(), [](const auto& volume) {
            return volume.root == L"D:\\";
        });
        const bool c_searched = c != snapshot.volumes.end() && c->searched;
        const bool d_searched = d != snapshot.volumes.end() && d->searched;
        const bool complete = snapshot.stage == SearchStage::Complete && c_searched && d_searched;
        const bool c_missing = c != snapshot.volumes.end() && !c->searched &&
            (c->error == ERROR_FILE_NOT_FOUND || c->error == ERROR_PATH_NOT_FOUND) &&
            (c->stage == VolumeIndexStage::RebuildRequired || c->stage == VolumeIndexStage::Unavailable);
        const bool partial = !c_store_present && snapshot.stage == SearchStage::Partial && c_missing && d_searched;
        std::cout << "LIVE INFO default_coordinator_scope coverage=" << (complete ? "complete" : partial ? "partial_missing_c_store" : "failed") <<
            " c_store_present=" << c_store_present << " c_searched=" << c_searched << " d_searched=" << d_searched <<
            " c_error=" << (c == snapshot.volumes.end() ? ERROR_NOT_FOUND : c->error) <<
            " d_error=" << (d == snapshot.volumes.end() ? ERROR_NOT_FOUND : d->error) << std::endl;
        if (!Report(complete || partial, "default_coordinator_search_scope", L"D:\\",
            complete || partial ? ERROR_SUCCESS : snapshot.error ? snapshot.error : ERROR_NOT_READY)) { return false; }
        const auto hit = std::find_if(d->results.begin(), d->results.end(), [&](const auto& result) {
            return result.hit.name == sentinel.filename().wstring() && result.hit.full_path == sentinel.wstring();
        });
        if (!Report(hit != d->results.end(), "default_coordinator_sentinel_found", L"D:\\",
            hit == d->results.end() ? ERROR_NOT_FOUND : ERROR_SUCCESS)) { return false; }
        if (!ValidateAction(*hit, sentinel, "default_coordinator_open_identity")) { return false; }
        const auto target = ValidateSearchResult(*hit);
        const auto listing = blink::core::EnumerateDirectory(target.parent);
        TabWindowState window;
        auto* tab = window.ActiveTab();
        const auto navigation = tab->state->Navigate(target.parent);
        bool located{};
        if (!target.error && !listing.error && navigation)
        {
            (void)tab->state->CommitBrowse(*navigation, listing);
            tab->locate_after_browse = RowIdentity{ hit->hit.identity, hit->hit.name };
            tab->locate_navigation_token = navigation->token;
            located = tab->FinishLocate(navigation->token) && tab->state->IsRowSelected(MakeRowIdentity(target.entry));
        }
        return Report(located, "default_coordinator_locate_row", L"D:\\",
            target.error ? target.error : listing.error ? listing.error : located ? ERROR_SUCCESS : ERROR_FILE_INVALID);
    }
    bool VerifyAdvancedScopes(Connection& connection, const fs::path& sentinel, const std::wstring& token)
    {
        const auto run = [&](SearchQuery query) {
            query.text = token;
            const auto state = std::make_shared<SearchState>();
            int discoveries{};
            SearchCoordinator coordinator(state, {}, [&discoveries] {
                ++discoveries; return std::vector<std::wstring>{ L"C:\\", L"D:\\" };
            });
            const auto generation = coordinator.Search(std::move(query), {});
            const auto action = coordinator.CurrentAction();
            if (generation && action) { action.get(); }
            const auto snapshot = state->Snapshot();
            (void)coordinator.Shutdown();
            return std::pair{ snapshot, discoveries };
        };
        const auto has_sentinel = [&](const SearchSnapshot& snapshot) {
            for (const auto& volume : snapshot.volumes)
            { for (const auto& result : volume.results) { if (result.hit.full_path == sentinel.wstring()) { return true; } } }
            return false;
        };
        TabWindowState window;
        auto* tab = window.ActiveTab(); tab->search_draft = token;
        const auto all = run(tab->SearchIntent(false));
        if (!Report(all.first.stage == SearchStage::Complete && all.first.volumes.size() == 2 &&
            all.second == 1 && has_sentinel(all.first), "scope_this_pc_searches_both_drives", L"D:\\")) { return false; }
        const auto navigation = tab->state->Navigate(sentinel.parent_path().wstring());
        const auto listing = blink::core::EnumerateDirectory(sentinel.parent_path().wstring());
        if (!navigation || listing.error)
        { return Report(false, "scope_navigation", L"D:\\", listing.error ? listing.error : ERROR_INVALID_STATE); }
        const auto committed = tab->state->CommitBrowse(*navigation, listing);
        if (committed.status != CommitBrowseStatus::Committed)
        { return Report(false, "scope_navigation_commit", L"D:\\", committed.error); }
        const auto drive = run(tab->SearchIntent(false));
        if (!Report(drive.first.stage == SearchStage::Complete && drive.first.volumes.size() == 1 &&
            drive.first.volumes.front().root == L"D:\\" && drive.second == 0 && has_sentinel(drive.first),
            "scope_folder_defaults_to_own_drive", L"D:\\")) { return false; }
        SearchQuery c_query; c_query.scope = SearchScope::CurrentVolume; c_query.folder = L"C:\\";
        const auto c = run(c_query);
        if (!Report(c.first.stage == SearchStage::Complete && c.first.volumes.size() == 1 &&
            c.first.volumes.front().root == L"C:\\" && c.second == 0 && !has_sentinel(c.first),
            "scope_current_c_does_not_search_d", L"C:\\")) { return false; }
        SearchQuery selected; selected.scope = SearchScope::SelectedVolumes; selected.selected_volumes = L"d:;D:";
        const auto single = run(selected);
        if (!Report(single.first.stage == SearchStage::Complete && single.first.volumes.size() == 1 &&
            single.first.volumes.front().root == L"D:\\" && single.second == 0 && has_sentinel(single.first),
            "scope_selected_drive_deduplicated", L"D:\\")) { return false; }
        selected.selected_volumes = L"C:;D:";
        const auto both = run(selected);
        if (!Report(both.first.stage == SearchStage::Complete && both.first.volumes.size() == 2 &&
            both.second == 0 && has_sentinel(both.first), "scope_selected_both_drives", L"D:\\")) { return false; }

        const auto nested_folder = sentinel.parent_path() / L"nested";
        fs::create_directories(nested_folder);
        const auto nested_name = L"nested-" + token + L".txt";
        const auto nested = nested_folder / nested_name;
        { std::ofstream stream(nested, std::ios::binary); stream << "Blink nested search scope\n";
          if (!stream) { return Report(false, "scope_nested_fixture", L"D:\\", ERROR_WRITE_FAULT); } }
        bool converged{};
        (void)WaitForSearch(connection, L"D:\\", sentinel.parent_path(), token, nested_name, {}, true,
            "scope_nested_create_converged", converged);
        if (!converged) { return false; }
        SearchQuery folder; folder.scope = SearchScope::SelectedFolder; folder.folder = sentinel.parent_path().wstring();
        const auto recursive = run(folder);
        if (!Report(recursive.first.stage == SearchStage::Complete && recursive.first.volumes.size() == 1 &&
            recursive.first.volumes.front().results.size() == 2 && recursive.second == 0 && has_sentinel(recursive.first),
            "scope_selected_folder_includes_descendants", L"D:\\")) { return false; }
        folder.include_subfolders = false; folder.limit = 1;
        const auto direct = run(folder);
        if (!Report(direct.first.stage == SearchStage::Complete && direct.first.volumes.size() == 1 &&
            direct.first.volumes.front().results.size() == 1 && direct.second == 0 && has_sentinel(direct.first),
            "scope_selected_folder_direct_before_limit", L"D:\\")) { return false; }
        fs::remove(nested); fs::remove(nested_folder);
        (void)WaitForSearch(connection, L"D:\\", sentinel.parent_path(), token, sentinel.filename().wstring(), nested_name, true,
            "scope_nested_delete_converged", converged);
        return converged;
    }
}

int RunIndexServiceLiveProbe(const int argc, wchar_t* argv[])
{
    if (argc < 3)
    {
        std::cout << "LIVE FAIL usage: Blink.Phase1.Tests.exe --live-index-probe <drive-root> [--enable | --build]" << std::endl;
        return 80;
    }
    const auto root = SearchDriveRoot(argv[2]);
    if (root.empty() || std::wstring(argv[2]).size() != 3) { Report(false, "arguments", root, ERROR_INVALID_PARAMETER); return 80; }
    bool enable{}, build{};
    for (int i = 3; i < argc; ++i)
    {
        const std::wstring value(argv[i]);
        if (value == L"--enable") { enable = true; }
        else if (value == L"--build") { build = true; }
        else { Report(false, "arguments", root, ERROR_INVALID_PARAMETER); return 80; }
    }
    if (enable && build) { Report(false, "arguments", root, ERROR_INVALID_PARAMETER); return 80; }
    try
    {
        if (enable || build)
        {
            const auto state = std::make_shared<IndexServiceState>(); IndexServiceCoordinator coordinator(state);
            const auto generation = enable ? coordinator.EnableService(root, {}) : coordinator.BuildIndex(root, {});
            const auto action = coordinator.CurrentAction();
            if (!generation || !action) { Report(false, "start_request", root, ERROR_BUSY); return 81; }
            action.get(); const auto snapshot = state->Snapshot();
            if (snapshot.stage != IndexConnectionStage::Available)
            { Report(false, enable ? "enable_service" : "build_request", root, snapshot.error); return 81; }
            Report(true, enable ? "enable_service" : "build_request", root);
        }
        Connection connection;
        if (!WaitForIndex(connection, root, enable)) { return 82; }
        if (!enable && !build) { return Report(true, "index_status_read_only", root) ? 0 : 82; }
        const auto project = ProjectRoot();
        if (project.empty()) { Report(false, "project_root", root, ERROR_PATH_NOT_FOUND); return 83; }
        const auto project_volume = SearchDriveRoot(project.wstring());
        if (project_volume != root)
        {
            std::cout << "LIVE SKIP sentinel_flow root=" << Utf8(root) << " reason=project_fixture_on_" << Utf8(project_volume) << std::endl;
            return Report(true, "index_status_only", root) ? 0 : 83;
        }
        GUID value{}; wchar_t guid[40]{};
        if (FAILED(CoCreateGuid(&value)) || !StringFromGUID2(value, guid, static_cast<int>(std::size(guid))))
        { Report(false, "fixture_name", root, ERROR_GEN_FAILURE); return 83; }
        const std::wstring token = std::wstring(guid).substr(1, 36);
        const auto folder = project / L"data" / L"index-repair-fixtures" / (L"owned-" + token);
        struct Cleanup
        {
            fs::path folder;
            ~Cleanup() { std::error_code error; fs::remove_all(folder, error); }
        } cleanup{ folder };
        fs::create_directories(folder);
        const auto original_name = L"blink-index-probe-" + token + L".txt";
        const auto renamed_name = L"blink-index-probe-" + token + L"-renamed.txt";
        const auto original = folder / original_name, renamed = folder / renamed_name;
        { std::ofstream stream(original, std::ios::binary); stream << "Blink live index repair sentinel\n";
          if (!stream) { Report(false, "fixture_create", root, ERROR_WRITE_FAULT); return 83; } }
        bool succeeded{};
        const auto created = WaitForSearch(connection, root, folder, token, original_name, {}, true, "sentinel_create_converged", succeeded);
        if (!succeeded || !created || !ValidateAction(*created, original, "sentinel_open_and_locate_identity")) { return 84; }
        if (!VerifyDefaultCoordinatorSearch(project, original, token)) { return 84; }
        if (!VerifyAdvancedScopes(connection, original, token)) { return 84; }
        fs::rename(original, renamed);
        const auto changed = WaitForSearch(connection, root, folder, token, renamed_name, original_name, true, "sentinel_rename_converged", succeeded);
        if (!succeeded || !changed || !ValidateAction(*changed, renamed, "sentinel_renamed_open_and_locate_identity")) { return 84; }
        if (!fs::remove(renamed)) { Report(false, "fixture_delete", root, ERROR_FILE_NOT_FOUND); return 83; }
        (void)WaitForSearch(connection, root, folder, token, {}, renamed_name, false, "sentinel_delete_converged", succeeded);
        if (!succeeded) { return 84; }
        std::error_code cleanup_error; fs::remove_all(folder, cleanup_error);
        if (cleanup_error) { Report(false, "fixture_cleanup", root, static_cast<DWORD>(cleanup_error.value())); return 85; }
        return Report(true, "sentinel_flow_complete", root) ? 0 : 84;
    }
    catch (const winrt::hresult_error& error)
    { Report(false, "exception", root, static_cast<DWORD>(error.code().value)); return 86; }
    catch (const fs::filesystem_error& error)
    { Report(false, "filesystem_exception", root, static_cast<DWORD>(error.code().value())); return 86; }
    catch (...)
    { Report(false, "exception", root, ERROR_GEN_FAILURE); return 86; }
}
