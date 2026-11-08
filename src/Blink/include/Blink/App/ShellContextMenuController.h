#pragma once

#include "Blink/App/SearchContextCommands.h"
#include "Blink/App/ShellContextMenu.h"
#include <winrt/Windows.Foundation.h>
#include <functional>
#include <memory>
#include <mutex>

namespace blink::app
{
    // The helper owns third-party COM objects and any native dialogs. Blink
    // awaits only a menu selection, never the lifetime of a selected operation.
    class ShellContextMenuController
    {
    public:
        using Completed = std::function<void(std::uint64_t,
            SearchContextSelection, SearchActionTarget, ShellMenuResult)>;
        using RequestCompleted = std::function<void(std::uint64_t, ShellMenuResult)>;
        ShellContextMenuController();
        ~ShellContextMenuController() noexcept;
        std::uint64_t Show(SearchContextSelection selection, POINT position,
            HWND owner, std::wstring locale, bool extended, bool properties, Completed completed);
        std::uint64_t Show(ShellMenuRequest request, RequestCompleted completed);
        void CancelPending() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsCurrent(std::uint64_t generation) const noexcept;

    private:
        struct State;
        static ShellMenuResult Dispatch(const ShellMenuRequest& request, HANDLE cancel);
        void Cancel(bool closed) noexcept;
        std::shared_ptr<State> state_;
        std::mutex work_mutex_;
        winrt::Windows::Foundation::IAsyncAction work_{ nullptr };
    };
}
