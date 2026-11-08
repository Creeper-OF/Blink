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
        using ScopeResolver = std::function<SearchFolderScope(const std::wstring&)>;
        using ScopeFilter = std::function<void(const SearchFolderScope&, SearchVolumeResult&)>;
        explicit SearchCoordinator(std::shared_ptr<SearchState> state,
            ExchangeFunction exchange = {}, DiscoverFunction discover = {},
            ScopeResolver resolve_scope = {}, ScopeFilter filter_scope = {});
        ~SearchCoordinator() noexcept;
        [[nodiscard]] std::uint64_t Search(SearchQuery query, PublishCallback publish, bool retry = false);
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction CurrentAction() const;
        void Cancel() noexcept;
        // Logical cancellation never takes the Client's I/O mutex on the UI.
        winrt::Windows::Foundation::IAsyncAction Shutdown() noexcept;
    private:
        std::shared_ptr<SearchState> state_;
        ExchangeFunction exchange_;
        DiscoverFunction discover_;
        ScopeResolver resolve_scope_;
        ScopeFilter filter_scope_;
        mutable std::mutex mutex_;
        winrt::Windows::Foundation::IAsyncAction work_{ nullptr };
    };
}
