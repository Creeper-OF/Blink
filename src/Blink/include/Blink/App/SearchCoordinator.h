#pragma once

#include "Blink/App/SearchState.h"
#include "Blink/App/SearchResultValidation.h"
#include <winrt/Windows.Foundation.h>
#include <functional>
#include <memory>

namespace blink::app
{
    class SearchCoordinator
    {
    public:
        using ExchangeFunction = std::function<IndexRpcResult(blink::index::ipc::Request)>;
        using DiscoverFunction = std::function<std::vector<std::wstring>()>;
        using PublishCallback = std::function<void(std::uint64_t)>;
        // Invoked in revision order while the snapshot is owned by the
        // coordinator. Copy it before queueing work on the UI thread.
        using ProgressCallback = std::function<void(const SearchSnapshot&)>;
        using ScopeResolver = std::function<SearchFolderScope(const std::wstring&)>;
        using ScopeFilter = std::function<void(const SearchFolderScope&, SearchVolumeResult&)>;
        explicit SearchCoordinator(std::shared_ptr<SearchState> state,
            ExchangeFunction exchange = {}, DiscoverFunction discover = {},
            ScopeResolver resolve_scope = {}, ScopeFilter filter_scope = {}, ExchangeFunction companion_exchange = {});
        ~SearchCoordinator() noexcept;
        [[nodiscard]] std::uint64_t Search(SearchQuery query, PublishCallback publish, bool retry = false,
            ProgressCallback progress = {});
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction CurrentAction() const;
        void Cancel() noexcept;
        // Logical cancellation never takes the Client's I/O mutex on the UI.
        winrt::Windows::Foundation::IAsyncAction Shutdown() noexcept;
    private:
        std::shared_ptr<SearchState> state_;
        bool use_default_transport_{};
        ExchangeFunction exchange_;
        ExchangeFunction companion_exchange_;
        DiscoverFunction discover_;
        ScopeResolver resolve_scope_;
        ScopeFilter filter_scope_;
        mutable std::mutex mutex_;
        winrt::Windows::Foundation::IAsyncAction work_{ nullptr };
    };

    // Busy returns immediately; a rejected visibility command may fall back
    // once to legacy Search. Raw transport shares the production checks.
    [[nodiscard]] SearchCoordinator::ExchangeFunction MakeSearchExchange(
        const std::shared_ptr<SearchState>& state,
        SearchCoordinator::ExchangeFunction raw_transport = {});
}
