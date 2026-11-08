#include "Blink/App/SearchCoordinator.h"
#include "Blink/App/Location.h"
#include <winrt/Windows.System.Threading.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace blink::app
{
    namespace
    {
        namespace ipc = blink::index::ipc;
        SearchCoordinator::ExchangeFunction DefaultExchange(const std::shared_ptr<SearchState>& state)
        {
            const auto client = std::make_shared<ipc::Client>();
            using Clock = std::chrono::steady_clock;
            return [client, weak_state = std::weak_ptr<SearchState>(state), generation = std::uint64_t{},
                deadline = Clock::time_point{}](ipc::Request request) mutable {
                const auto state = weak_state.lock();
                if (!state) { return IndexRpcResult{ false, ERROR_CANCELLED }; }
                const auto snapshot = state->Snapshot();
                if (snapshot.stage != SearchStage::Searching || !state->IsCurrent(snapshot.generation))
                { return IndexRpcResult{ false, ERROR_CANCELLED }; }
                if (generation != snapshot.generation)
                {
                    generation = snapshot.generation;
                    deadline = Clock::now() + std::chrono::seconds(120);
                }
                IndexRpcResult reply;
                for (;;)
                {
                    if (!state->IsCurrent(generation)) { return IndexRpcResult{ false, ERROR_CANCELLED }; }
                    // All volumes share this generation's budget. Reserve the
                    // existing Connect + Send bounds before another request.
                    if (Clock::now() + std::chrono::seconds(6) > deadline)
                    {
                        if (!reply.received) { reply.error = ERROR_TIMEOUT; }
                        return reply;
                    }
                    IndexRpcResult next;
                    if (!client->IsConnected() && !client->Connect(1000, &next.error)) { return next; }
                    if (!state->IsCurrent(generation)) { return IndexRpcResult{ false, ERROR_CANCELLED }; }
                    next.received = client->Send(request, next.response, 5000, &next.error);
                    if (next.received) { next.expected_request_id = next.response.request_id; }
                    else { client->Disconnect(); }
                    if (!state->IsCurrent(generation)) { return IndexRpcResult{ false, ERROR_CANCELLED }; }
                    reply = std::move(next);
                    if (ValidateIndexReply(reply) != ERROR_SUCCESS || reply.response.status == ipc::ResponseStatus::NotFound)
                    { return reply; }
                    const auto error = reply.response.error;
                    const bool pending = error == ERROR_IO_PENDING || (reply.response.status == ipc::ResponseStatus::Busy &&
                        (error == ERROR_SUCCESS || error == ERROR_BUSY));
                    if (!pending || Clock::now() + std::chrono::milliseconds(500) + std::chrono::seconds(6) > deadline)
                    { return reply; }
                    if (!state->IsCurrent(generation)) { return IndexRpcResult{ false, ERROR_CANCELLED }; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    if (!state->IsCurrent(generation)) { return IndexRpcResult{ false, ERROR_CANCELLED }; }
                }
            };
        }
        std::vector<std::wstring> DiscoverRoots()
        {
            std::vector<std::wstring> roots;
            for (const auto& item : EnumerateThisPcContent().items)
            {
                if (item.kind == ThisPcItemKind::Volume) { roots.push_back(item.target.FolderPath()); }
            }
            return roots;
        }
        void Collect(SearchSnapshot& snapshot, const SearchCoordinator::ExchangeFunction& exchange,
            const SearchCoordinator::DiscoverFunction& discover, const std::shared_ptr<SearchState>& state,
            const SearchCoordinator::ScopeResolver& resolve_scope, const SearchCoordinator::ScopeFilter& filter_scope)
        {
            snapshot.error = ValidateSearchQuery(snapshot.query);
            if (snapshot.error != ERROR_SUCCESS) { snapshot.stage = SearchStage::Unavailable; return; }
            const bool folder_scope = snapshot.query.scope == SearchScope::CurrentFolder ||
                snapshot.query.scope == SearchScope::SelectedFolder;
            SearchFolderScope scope;
            if (folder_scope)
            {
                scope = resolve_scope(snapshot.query.folder);
                if (scope.error != ERROR_SUCCESS || !scope.identity.valid ||
                    (snapshot.scope_identity.valid && scope.identity != snapshot.scope_identity))
                {
                    snapshot.error = scope.error == ERROR_SUCCESS ? ERROR_FILE_INVALID : scope.error;
                    snapshot.stage = SearchStage::Unavailable; snapshot.volumes.clear(); return;
                }
                snapshot.scope_identity = scope.identity;
            }
            if (snapshot.volumes.empty())
            {
                std::vector<std::wstring> roots;
                if (snapshot.query.scope == SearchScope::AllLocalVolumes) { roots = discover(); }
                else if (snapshot.query.scope == SearchScope::SelectedVolumes)
                {
                    snapshot.error = ParseSearchVolumeRoots(snapshot.query.selected_volumes, roots);
                    if (snapshot.error != ERROR_SUCCESS) { snapshot.stage = SearchStage::Unavailable; return; }
                }
                else { roots = { SearchDriveRoot(snapshot.query.folder) }; }
                for (const auto& candidate : roots)
                {
                    const auto root = SearchDriveRoot(candidate);
                    if (root.empty() || candidate.size() != 3 ||
                        std::any_of(snapshot.volumes.begin(), snapshot.volumes.end(),
                            [&root](const auto& volume) { return volume.root == root; })) { continue; }
                    SearchVolumeResult volume; volume.root = root;
                    snapshot.volumes.push_back(std::move(volume));
                }
            }
            if (snapshot.volumes.empty())
            { snapshot.error = ERROR_NOT_READY; snapshot.stage = SearchStage::Unavailable; return; }
            for (auto& volume : snapshot.volumes)
            {
                if (!state->IsCurrent(snapshot.generation)) { return; }
                if (!volume.searched)
                {
                    volume = MapSearchReply(snapshot.query, volume.root, exchange(MakeSearchRequest(snapshot.query, volume.root)));
                    if (!state->IsCurrent(snapshot.generation)) { return; }
                    if (volume.searched && !volume.results.empty())
                    { BindSearchVolumeIdentity(volume, resolve_scope(volume.root)); }
                }
                if (folder_scope)
                {
                    filter_scope(scope, volume);
                    if (!snapshot.query.include_subfolders && volume.searched)
                    {
                        std::erase_if(volume.results, [&scope](const SearchResult& result) {
                            return result.hit.parent_identity != scope.identity;
                        });
                    }
                }
            }
            const auto completed = std::count_if(snapshot.volumes.begin(), snapshot.volumes.end(),
                [](const auto& volume) { return volume.searched; });
            const auto incomplete = std::any_of(snapshot.volumes.begin(), snapshot.volumes.end(),
                [](const auto& volume) { return volume.searched && volume.query_incomplete; });
            snapshot.stage = static_cast<std::size_t>(completed) == snapshot.volumes.size() && !incomplete ? SearchStage::Complete :
                completed > 0 ? SearchStage::Partial : SearchStage::Unavailable;
            if (incomplete) { snapshot.error = ERROR_MORE_DATA; }
            for (const auto& volume : snapshot.volumes)
            {
                if (!volume.searched)
                { snapshot.error = volume.error == ERROR_SUCCESS ? ERROR_INVALID_DATA : volume.error; break; }
            }
            return;
        }
    }

    SearchCoordinator::SearchCoordinator(std::shared_ptr<SearchState> state, ExchangeFunction exchange, DiscoverFunction discover,
        ScopeResolver resolve_scope, ScopeFilter filter_scope)
        : state_(std::move(state)), exchange_(exchange ? std::move(exchange) : DefaultExchange(state_)),
          discover_(discover ? std::move(discover) : DiscoverRoots),
          resolve_scope_(resolve_scope ? std::move(resolve_scope) :
              ScopeResolver{ [](const std::wstring& path) { return ResolveSearchFolderScope(path); } }),
          filter_scope_(filter_scope ? std::move(filter_scope) :
              ScopeFilter{ [](const SearchFolderScope& scope, SearchVolumeResult& volume) { FilterSearchFolderResults(scope, volume); } }) {}
    SearchCoordinator::~SearchCoordinator() noexcept { (void)Shutdown(); }

    std::uint64_t SearchCoordinator::Search(SearchQuery query, PublishCallback publish, const bool retry)
    {
        std::unique_lock lock(mutex_);
        // Cancel invalidates a generation but does not detach a blocked RPC.
        // Never replace its owned action with another request until it exits.
        if (work_ && work_.Status() == winrt::Windows::Foundation::AsyncStatus::Started) { return 0; }
        const auto generation = state_->Begin(query, retry);
        if (generation == 0) { return 0; }
        const auto state = state_;
        try
        {
            const auto exchange = exchange_;
            const auto discover = discover_;
            const auto resolve_scope = resolve_scope_;
            const auto filter_scope = filter_scope_;
            work_ = winrt::Windows::System::Threading::ThreadPool::RunAsync(
                [state, exchange, discover, resolve_scope, filter_scope, generation, query, publish](auto&&) mutable {
                    if (!state->IsCurrent(generation)) { return; }
                    SearchSnapshot snapshot;
                    snapshot.generation = generation;
                    snapshot.query = std::move(query);
                    try { snapshot = state->Snapshot(); Collect(snapshot, exchange, discover, state, resolve_scope, filter_scope); }
                    catch (const std::bad_alloc&)
                    { snapshot.stage = SearchStage::Unavailable; snapshot.error = ERROR_NOT_ENOUGH_MEMORY; }
                    catch (...)
                    { snapshot.stage = SearchStage::Unavailable; snapshot.error = ERROR_GEN_FAILURE; }
                    if (state->Complete(generation, std::move(snapshot)) && publish)
                    { try { publish(generation); } catch (...) {} }
                });
        }
        catch (...)
        {
            lock.unlock();
            SearchSnapshot snapshot; snapshot.query = std::move(query);
            snapshot.stage = SearchStage::Unavailable; snapshot.error = ERROR_GEN_FAILURE;
            if (state->Complete(generation, std::move(snapshot)) && publish)
            { try { publish(generation); } catch (...) {} }
        }
        return generation;
    }
    winrt::Windows::Foundation::IAsyncAction SearchCoordinator::CurrentAction() const
    {
        std::scoped_lock lock(mutex_);
        return work_;
    }
    void SearchCoordinator::Cancel() noexcept { state_->Cancel(); }
    winrt::Windows::Foundation::IAsyncAction SearchCoordinator::Shutdown() noexcept
    {
        state_->Shutdown();
        std::scoped_lock lock(mutex_);
        return std::exchange(work_, nullptr);
    }
}
