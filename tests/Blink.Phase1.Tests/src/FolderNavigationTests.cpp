#include "FolderNavigationTests.h"
#include "Blink/App/BrowseController.h"
#include "Blink/App/FolderChangeState.h"
#include "Blink/App/FolderWatcher.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace blink::app;
    using namespace std::chrono_literals;
    bool Check(const bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }

    struct Gate
    {
        winrt::handle ready{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        winrt::handle enumerated{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        std::atomic_bool released{}, early{};
        std::atomic_uint enumerations{};
        void Release() { released = true; SetEvent(ready.get()); }
        ~Gate() { Release(); }
    };
    winrt::Windows::Foundation::IAsyncAction AwaitGate(std::shared_ptr<Gate> gate)
    { co_await winrt::resume_on_signal(gate->ready.get()); }

    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        auto done = std::make_shared<std::promise<Status>>(); auto future = done->get_future();
        action.Completed([done](auto&&, Status value) { done->set_value(value); });
        return future.wait_for(5s) == std::future_status::ready && future.get() == Status::Completed;
    }

    struct Events
    {
        winrt::handle ready{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        winrt::handle changed{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        winrt::handle unavailable{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        std::atomic_uint invalidations{};
        void Notify(const FolderWatchEvent event)
        {
            if (event.RequiresRefresh()) { ++invalidations; }
            if (event.kind == FolderWatchEventKind::Ready) { SetEvent(ready.get()); }
            if (event.kind == FolderWatchEventKind::Changed || event.kind == FolderWatchEventKind::Overflow)
            { SetEvent(changed.get()); }
            if (event.kind == FolderWatchEventKind::Unavailable) { SetEvent(unavailable.get()); }
        }
    };
}

bool RunFolderNavigationTests()
{
    bool passed = true;
    // A Ready event alone must not cause the extra browse visible after opening
    // a stable folder. Exercise the same event routing and debounce as the UI.
    FolderChangeState changes;
    const auto token = changes.Begin(L"D:\\stable");
    const auto now = FolderChangeState::Clock::now();
    const FolderWatchEvent ready{ FolderWatchEventKind::Ready };
    if (ready.RequiresRefresh()) { (void)changes.Notify(token, now); }
    passed &= Check(!changes.TakeRefresh(token, now + 1s, false) && !changes.HasChanges(),
        "folder_navigation_ready_does_not_schedule_a_second_browse");
    if (ready.RequiresRefresh(true)) { (void)changes.Notify(token, now); }
    passed &= Check(changes.TakeRefresh(token, now + 1s, false),
        "folder_navigation_restored_old_page_still_catches_up_after_rearm");
    const FolderWatchEvent changed{ FolderWatchEventKind::Changed };
    if (changed.RequiresRefresh()) { (void)changes.Notify(token, now); }
    passed &= Check(changes.TakeRefresh(token, now + 1s, false),
        "folder_navigation_real_changes_still_schedule_refresh");
    passed &= Check(FolderWatchEvent{ FolderWatchEventKind::Overflow }.RequiresRefresh() &&
        !FolderWatchEvent{ FolderWatchEventKind::Unavailable, ERROR_ACCESS_DENIED }.RequiresRefresh(),
        "folder_navigation_overflow_reconciles_but_unavailable_remains_error_only");

    // Delaying the watch arm must delay the FIRST enumeration, rather than
    // relying on a second enumeration to fill the initial monitoring gap.
    auto gate = std::make_shared<Gate>();
    auto completed = std::make_shared<std::promise<BrowseResult>>();
    auto result = completed->get_future();
    auto state = std::make_shared<BrowseState>();
    BrowseController controller(state, [gate](const std::wstring& path) {
        ++gate->enumerations;
        if (!gate->released.load()) { gate->early = true; }
        SetEvent(gate->enumerated.get());
        blink::core::DirectoryListing listing; listing.path = path; return listing;
    });
    controller.Browse(L"D:\\delayed-watch", [completed](auto value) { completed->set_value(std::move(value)); }, AwaitGate(gate));
    const bool waited = WaitForSingleObject(gate->enumerated.get(), 150) == WAIT_TIMEOUT;
    gate->Release();
    const bool done = result.wait_for(5s) == std::future_status::ready;
    passed &= Check(waited && done && !gate->early && gate->enumerations == 1,
        "folder_navigation_first_enumeration_waits_for_watch_arm_without_duplicate");
    controller.Shutdown();

    // Superseding or cancelling navigation while readiness is pending must
    // never enumerate or publish the old path, including A -> B -> A.
    for (const bool close : { false, true })
    {
        auto pending = std::make_shared<Gate>();
        auto pending_state = std::make_shared<BrowseState>();
        auto published = std::make_shared<std::atomic_uint>();
        BrowseController pending_controller(pending_state, [pending](const auto& path) {
            ++pending->enumerations; SetEvent(pending->enumerated.get());
            blink::core::DirectoryListing listing; listing.path = path; return listing;
        });
        pending_controller.Browse(L"A", [published](auto) { ++*published; }, AwaitGate(pending));
        if (close) { pending_controller.Shutdown(); } else { pending_controller.CancelPending(); }
        pending->Release();
        passed &= Check(WaitForSingleObject(pending->enumerated.get(), 150) == WAIT_TIMEOUT &&
            pending->enumerations == 0 && *published == 0,
            close ? "folder_navigation_shutdown_during_arm_has_no_late_browse" :
                "folder_navigation_cancel_during_arm_has_no_late_browse");
    }
    {
        auto pending = std::make_shared<Gate>(); auto current_state = std::make_shared<BrowseState>();
        auto old_published = std::make_shared<std::atomic_uint>();
        auto latest = std::make_shared<std::promise<BrowseResult>>(); auto latest_result = latest->get_future();
        BrowseController current(current_state);
        current.Browse(L"A", [old_published, pending](auto) {
            ++*old_published; SetEvent(pending->enumerated.get());
        }, AwaitGate(pending));
        current.Browse(L"B", {});
        current.Browse(L"A", [latest](auto value) { latest->set_value(std::move(value)); });
        const bool latest_done = latest_result.wait_for(5s) == std::future_status::ready;
        pending->Release();
        passed &= Check(latest_done && WaitForSingleObject(pending->enumerated.get(), 150) == WAIT_TIMEOUT &&
            old_published->load() == 0 && current_state->Snapshot().requested_path == L"A",
            "folder_navigation_a_b_a_rejects_old_pending_watch_generation");
        current.Shutdown();
    }

    namespace fs = std::filesystem;
    const auto root = fs::absolute(fs::path(L".build") / L"folder-navigation-fixtures" /
        (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
    fs::create_directories(root / L"目录");
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code error; fs::remove_all(root, error); } } cleanup{ root };
    {
        FolderWatcher watcher; auto events = std::make_shared<Events>();
        const auto path = (root / L"目录").wstring();
        watcher.Start(path, [events](auto event) { events->Notify(event); });
        auto first = watcher.WhenReady(), second = watcher.WhenReady();
        passed &= Check(Finished(first) && Finished(second) && WaitForSingleObject(events->ready.get(), 5000) == WAIT_OBJECT_0,
            "folder_navigation_independent_waiters_observe_real_native_watch_arm");
        auto native_done = std::make_shared<std::promise<BrowseResult>>(); auto native_result = native_done->get_future();
        auto calls = std::make_shared<std::atomic_uint>();
        BrowseController native(std::make_shared<BrowseState>(), [calls](const auto& folder) {
            ++*calls; return blink::core::EnumerateDirectory(folder);
        });
        native.Browse(path, [native_done](auto value) { native_done->set_value(std::move(value)); }, watcher.WhenReady());
        const bool loaded = native_result.wait_for(5s) == std::future_status::ready;
        passed &= Check(loaded && native_result.get().listing.error == ERROR_SUCCESS && calls->load() == 1 && events->invalidations == 0,
            "folder_navigation_real_stable_directory_loads_once_without_ready_invalidation");

        // A real change after arm and before enumeration completes is retained.
        auto in_enumeration = std::make_shared<Gate>();
        auto loaded_changed = std::make_shared<std::promise<BrowseResult>>(); auto changed_result = loaded_changed->get_future();
        BrowseController during(std::make_shared<BrowseState>(), [in_enumeration](const auto& folder) {
            SetEvent(in_enumeration->enumerated.get());
            if (WaitForSingleObject(in_enumeration->ready.get(), 5000) != WAIT_OBJECT_0) { throw std::runtime_error("enumeration gate timeout"); }
            return blink::core::EnumerateDirectory(folder);
        });
        during.Browse(path, [loaded_changed](auto value) { loaded_changed->set_value(std::move(value)); }, watcher.WhenReady());
        const bool entered = WaitForSingleObject(in_enumeration->enumerated.get(), 5000) == WAIT_OBJECT_0;
        std::ofstream(root / L"目录" / L"new.txt") << "owned-data";
        const bool change_seen = WaitForSingleObject(events->changed.get(), 5000) == WAIT_OBJECT_0;
        FolderChangeState busy_changes; const auto busy_token = busy_changes.Begin(path);
        if (events->invalidations) { (void)busy_changes.Notify(busy_token, now); }
        const bool retained = !busy_changes.TakeRefresh(busy_token, now + 1s, true) && busy_changes.HasChanges();
        in_enumeration->Release();
        const bool changed_loaded = changed_result.wait_for(5s) == std::future_status::ready;
        passed &= Check(entered && change_seen && retained && changed_loaded &&
            changed_result.get().listing.entries.size() == 1 && busy_changes.TakeRefresh(busy_token, now + 1s, false),
            "folder_navigation_real_change_during_first_load_is_retained_for_reconcile");
        native.Shutdown(); during.Shutdown();
        passed &= Check(Finished(watcher.Stop()), "folder_navigation_armed_watch_drains_native_io_on_stop");
        passed &= Check(Finished(watcher.WhenReady()), "folder_navigation_stopped_watch_never_blocks_future_browse");
    }
    {
        FolderWatcher watcher; auto events = std::make_shared<Events>(); const auto path = (root / L"missing").wstring();
        watcher.Start(path, [events](auto event) { events->Notify(event); });
        auto failed = std::make_shared<std::promise<BrowseResult>>(); auto failure = failed->get_future();
        BrowseController unavailable(std::make_shared<BrowseState>());
        unavailable.Browse(path, [failed](auto value) { failed->set_value(std::move(value)); }, watcher.WhenReady());
        const bool finished = failure.wait_for(5s) == std::future_status::ready;
        passed &= Check(finished && failure.get().listing.error != ERROR_SUCCESS &&
            WaitForSingleObject(events->unavailable.get(), 5000) == WAIT_OBJECT_0,
            "folder_navigation_watch_start_failure_releases_browse_to_report_real_error");
        unavailable.Shutdown();
        passed &= Check(Finished(watcher.Stop()), "folder_navigation_failed_watch_has_no_pending_native_work");
    }
    {
        bool stopped = true;
        for (unsigned index = 0; index != 16; ++index)
        {
            FolderWatcher watcher; watcher.Start((root / L"目录").wstring(), {});
            auto first = watcher.WhenReady(), second = watcher.WhenReady(); auto work = watcher.Stop();
            stopped &= Finished(first) && Finished(second) && Finished(work);
        }
        passed &= Check(stopped, "folder_navigation_stop_before_arm_releases_all_waiters_and_drains_each_session");
    }
    return passed;
}
