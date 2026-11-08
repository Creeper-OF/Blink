#pragma once

#include "Blink/App/IndexServiceState.h"

#include <winrt/Windows.Foundation.h>
#include <functional>
#include <memory>
#include <mutex>

namespace blink::app
{
    class IndexServiceCoordinator
    {
    public:
        using ExchangeFunction = std::function<IndexRpcResult(blink::index::ipc::Request)>;
        using DiscoverFunction = std::function<std::vector<std::wstring>()>;
        using PublishCallback = std::function<void(std::uint64_t)>;
        using EnableFunction = std::function<DWORD()>;
        explicit IndexServiceCoordinator(std::shared_ptr<IndexServiceState> state,
            ExchangeFunction exchange = {}, DiscoverFunction discover = {}, EnableFunction enable = {});
        ~IndexServiceCoordinator() noexcept;
        [[nodiscard]] std::uint64_t Refresh(std::wstring selected_root, PublishCallback publish);
        [[nodiscard]] std::uint64_t EnableService(std::wstring selected_root, PublishCallback publish);
        [[nodiscard]] std::uint64_t BuildIndex(std::wstring selected_root, PublishCallback publish);
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction CurrentAction() const;
        winrt::Windows::Foundation::IAsyncAction Shutdown() noexcept;

    private:
        [[nodiscard]] std::uint64_t Start(std::wstring selected_root, PublishCallback publish, bool enable, bool build);
        std::shared_ptr<IndexServiceState> state_;
        ExchangeFunction exchange_;
        DiscoverFunction discover_;
        EnableFunction enable_;
        mutable std::mutex mutex_;
        winrt::Windows::Foundation::IAsyncAction work_{ nullptr };
    };
}
