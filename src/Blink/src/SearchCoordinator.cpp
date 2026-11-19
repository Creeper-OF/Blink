#include "Blink/App/SearchCoordinator.h"
#include "Blink/App/Location.h"
#include <winrt/Windows.System.Threading.h>
#include <algorithm>
#include <atomic>
#include <future>
#include <system_error>
#include <utility>

namespace blink::app
{
    namespace
    {
        namespace ipc = blink::index::ipc;
        std::vector<std::wstring> DiscoverRoots()
        {
            std::vector<std::wstring> roots;
            for (const auto& item : EnumerateThisPcContent().items)
            {
                if (item.kind == ThisPcItemKind::Volume) { roots.push_back(item.target.FolderPath()); }
            }
            return roots;
        }
        void FilterVolume(const SearchQuery& query, const SearchFolderScope* scope, SearchVolumeResult& volume,
            const SearchCoordinator::ScopeFilter& filter_scope)
        {
            if (!scope) { return; }
            filter_scope(*scope, volume);
            if (!query.include_subfolders && volume.searched)
            {
                std::erase_if(volume.results, [scope](const SearchResult& result) {
                    return result.hit.parent_identity != scope->identity;
                });
            }
        }

        void Collect(SearchSnapshot& snapshot, const SearchCoordinator::ExchangeFunction& exchange,
            const SearchCoordinator::ExchangeFunction& companion, const bool use_default_transport,
            const SearchCoordinator::DiscoverFunction& discover, const std::shared_ptr<SearchState>& state,
            const SearchCoordinator::ScopeResolver& resolve_scope,
            const SearchCoordinator::ScopeFilter& filter_scope, const SearchCoordinator::ProgressCallback& progress)
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
            const auto* checked_scope = folder_scope ? &scope : nullptr;
            std::vector<std::wstring> pending;
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
                    if (!root.empty() && candidate.size() == 3 &&
                        std::find(pending.begin(), pending.end(), root) == pending.end()) { pending.push_back(root); }
                }
            }
            else
            {
                for (const auto& volume : snapshot.volumes)
                { if (!volume.searched) { pending.push_back(volume.root); } }
                std::erase_if(snapshot.volumes, [](const auto& volume) { return !volume.searched; });
                for (auto& volume : snapshot.volumes) { FilterVolume(snapshot.query, checked_scope, volume, filter_scope); }
            }
            if (pending.empty() && snapshot.volumes.empty())
            { snapshot.error = ERROR_NOT_READY; snapshot.stage = SearchStage::Unavailable; return; }

            snapshot.volumes.reserve(snapshot.volumes.size() + pending.size());
            std::atomic_size_t next{};
            std::mutex completion_mutex;
            const auto collect_pending = [&](const SearchCoordinator::ExchangeFunction& send)
            {
                while (state->IsCurrent(snapshot.generation))
                {
                    const auto index = next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= pending.size()) { return; }
                    const auto& root = pending[index];
                    SearchVolumeResult volume;
                    try
                    {
                        volume = MapSearchReply(snapshot.query, root, send(MakeSearchRequest(snapshot.query, root)));
                        if (!state->IsCurrent(snapshot.generation)) { return; }
                        if (volume.searched && !volume.results.empty())
                        { BindSearchVolumeIdentity(volume, resolve_scope(volume.root)); }
                        FilterVolume(snapshot.query, checked_scope, volume, filter_scope);
                    }
                    catch (const std::bad_alloc&)
                    { volume = {}; volume.root = root; volume.stage = VolumeIndexStage::Unavailable; volume.error = ERROR_NOT_ENOUGH_MEMORY; }
                    catch (...)
                    { volume = {}; volume.root = root; volume.stage = VolumeIndexStage::Unavailable; volume.error = ERROR_GEN_FAILURE; }

                    std::scoped_lock lock(completion_mutex);
                    if (!state->IsCurrent(snapshot.generation)) { return; }
                    snapshot.volumes.push_back(std::move(volume));
                    ++snapshot.revision;
                    if (progress && snapshot.revision < pending.size())
                    {
                        try { progress(snapshot); } catch (...) {}
                    }
                }
            };

            // Keep the one window-owned action. At most two independent
            // connections take pending volumes, and both workers are joined
            // before completion or cancellation releases that action.
            std::future<void> secondary;
            if (pending.size() > 1 && (use_default_transport || companion))
            {
                try
                {
                    auto second_exchange = use_default_transport ? MakeSearchExchange(state) : companion;
                    secondary = std::async(std::launch::async, [&, second_exchange = std::move(second_exchange)] {
                        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
                        catch (const winrt::hresult_error&) { return; }
                        struct Uninitialize { ~Uninitialize() { winrt::uninit_apartment(); } } apartment;
                        collect_pending(second_exchange);
                    });
                }
                catch (const std::system_error&) {}
                catch (const std::bad_alloc&) {}
            }
            collect_pending(exchange);
            if (secondary.valid()) { secondary.get(); }
            if (!state->IsCurrent(snapshot.generation)) { return; }

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
        }
    }

    SearchCoordinator::ExchangeFunction MakeSearchExchange(const std::shared_ptr<SearchState>& state,
        SearchCoordinator::ExchangeFunction raw_transport)
    {
        const auto client = raw_transport ? std::shared_ptr<ipc::Client>{} : std::make_shared<ipc::Client>();
        return [weak_state = std::weak_ptr<SearchState>(state), client, raw_transport = std::move(raw_transport)](
            ipc::Request request) {
            const auto state = weak_state.lock();
            if (!state) { return IndexRpcResult{ false, ERROR_CANCELLED }; }
            const auto snapshot = state->Snapshot();
            const auto generation = snapshot.generation;
            if (snapshot.stage != SearchStage::Searching || !state->IsCurrent(generation))
            { return IndexRpcResult{ false, ERROR_CANCELLED }; }

            const auto exchange_once = [&](ipc::Request next) {
                if (!state->IsCurrent(generation)) { return IndexRpcResult{ false, ERROR_CANCELLED }; }
                IndexRpcResult reply;
                if (raw_transport) { reply = raw_transport(std::move(next)); }
                else if (client->IsConnected() || client->Connect(1000, &reply.error))
                {
                    if (!state->IsCurrent(generation)) { return IndexRpcResult{ false, ERROR_CANCELLED }; }
                    reply.received = client->Send(std::move(next), reply.response, 5000, &reply.error);
                    if (reply.received) { reply.expected_request_id = reply.response.request_id; }
                    else { client->Disconnect(); }
                }
                if (!state->IsCurrent(generation)) { return IndexRpcResult{ false, ERROR_CANCELLED }; }
                return reply;
            };

            const bool filtered = request.type == ipc::RequestType::SearchVisible ||
                request.type == ipc::RequestType::SearchVisibleDirectChildren ||
                request.type == ipc::RequestType::SearchWithHidden ||
                request.type == ipc::RequestType::SearchWithHiddenDirectChildren;
            if (!filtered) { return exchange_once(std::move(request)); }

            auto reply = exchange_once(request);
            const bool unsupported = ValidateIndexReply(reply) == ERROR_SUCCESS &&
                ((reply.response.status == ipc::ResponseStatus::Unsupported &&
                    (reply.response.error == ERROR_NOT_SUPPORTED || reply.response.error == ERROR_INVALID_FUNCTION)) ||
                 (reply.response.status == ipc::ResponseStatus::InvalidRequest &&
                    reply.response.error == ERROR_INVALID_PARAMETER));
            if (unsupported)
            {
                const bool direct = request.type == ipc::RequestType::SearchVisibleDirectChildren ||
                    request.type == ipc::RequestType::SearchWithHiddenDirectChildren;
                request.type = direct ? ipc::RequestType::SearchDirectChildren : ipc::RequestType::Search;
                reply = exchange_once(std::move(request));
                reply.legacy_search_visibility = true;
            }
            // Collect must reach the other volumes after Busy/IO_PENDING.
            // Only a rejected new command gets one legacy compatibility call.
            return reply;
        };
    }

    SearchCoordinator::SearchCoordinator(std::shared_ptr<SearchState> state, ExchangeFunction exchange, DiscoverFunction discover,
        ScopeResolver resolve_scope, ScopeFilter filter_scope, ExchangeFunction companion_exchange)
        : state_(std::move(state)), use_default_transport_(!exchange),
          exchange_(exchange ? std::move(exchange) : MakeSearchExchange(state_)),
          companion_exchange_(std::move(companion_exchange)),
          discover_(discover ? std::move(discover) : DiscoverRoots),
          resolve_scope_(resolve_scope ? std::move(resolve_scope) :
              ScopeResolver{ [](const std::wstring& path) { return ResolveSearchFolderScope(path); } }),
          filter_scope_(filter_scope ? std::move(filter_scope) :
              ScopeFilter{ [](const SearchFolderScope& scope, SearchVolumeResult& volume) { FilterSearchFolderResults(scope, volume); } }) {}
    SearchCoordinator::~SearchCoordinator() noexcept { (void)Shutdown(); }

    std::uint64_t SearchCoordinator::Search(SearchQuery query, PublishCallback publish, const bool retry, ProgressCallback progress)
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
            const auto companion = companion_exchange_;
            const auto use_default_transport = use_default_transport_;
            const auto discover = discover_;
            const auto resolve_scope = resolve_scope_;
            const auto filter_scope = filter_scope_;
            work_ = winrt::Windows::System::Threading::ThreadPool::RunAsync(
                [state, exchange, companion, use_default_transport, discover, resolve_scope, filter_scope,
                    generation, query, publish, progress](auto&&) mutable {
                    if (!state->IsCurrent(generation)) { return; }
                    SearchSnapshot snapshot;
                    snapshot.generation = generation;
                    snapshot.query = std::move(query);
                    try
                    {
                        snapshot = state->Snapshot();
                        Collect(snapshot, exchange, companion, use_default_transport, discover, state, resolve_scope, filter_scope, progress);
                    }
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
