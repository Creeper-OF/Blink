#include "IndexServiceSetupTests.h"
#include "Blink/App/IndexServiceCoordinator.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>

namespace
{
    using namespace blink::app;
    namespace ipc = blink::index::ipc;
    bool Check(const bool value, const char* name)
    { std::cout << (value ? "PASS " : "FAIL ") << name << '\n'; return value; }
    bool Finished(const winrt::Windows::Foundation::IAsyncAction& action)
    {
        using Status = winrt::Windows::Foundation::AsyncStatus;
        if (!action) { return false; }
        if (action.Status() != Status::Started) { return action.Status() == Status::Completed; }
        const auto done = std::make_shared<std::promise<Status>>(); auto result = done->get_future();
        action.Completed([done](auto&&, const Status status) { done->set_value(status); });
        return result.wait_for(std::chrono::seconds(5)) == std::future_status::ready && result.get() == Status::Completed;
    }
    struct Rpc
    {
        std::atomic_uint calls{}, builds{};
        bool healthy{}, existing_operation{};
        DWORD build_error{};
        std::atomic_bool started{};
        IndexRpcResult Exchange(const ipc::Request& request)
        {
            IndexRpcResult value; value.received = true; value.expected_request_id = ++calls;
            auto& reply = value.response; reply.request_id = value.expected_request_id;
            reply.service_state = ipc::ServiceState::Running; reply.status = ipc::ResponseStatus::Ok;
            reply.error = ERROR_SUCCESS; reply.index_state = ipc::IndexState::Healthy;
            if (request.type == ipc::RequestType::QueryIndexProgress)
            {
                if (existing_operation || started.load())
                { reply.operation_id = 77; reply.error = ERROR_IO_PENDING; reply.index_state = ipc::IndexState::Rebuilding; }
                else { reply.status = ipc::ResponseStatus::NotFound; reply.error = ERROR_NOT_FOUND; }
            }
            else if (request.type == ipc::RequestType::QueryVolumeStatus && !healthy)
            {
                reply.status = ipc::ResponseStatus::NotFound; reply.error = ERROR_FILE_NOT_FOUND;
                reply.index_state = ipc::IndexState::Unavailable;
            }
            else if (request.type == ipc::RequestType::StartInitialIndex)
            {
                ++builds;
                if (build_error)
                { reply.status = ipc::ResponseStatus::Failed; reply.error = build_error; }
                else { started.store(true); reply.operation_id = 77; reply.index_state = ipc::IndexState::Rebuilding; }
            }
            return value;
        }
    };
    std::vector<std::wstring> Roots() { return { L"D:\\" }; }
}

bool RunIndexServiceSetupTests()
{
    bool passed = true;
    {
        const auto rpc = std::make_shared<Rpc>(); const auto setups = std::make_shared<std::atomic_uint>();
        const auto state = std::make_shared<IndexServiceState>();
        IndexServiceCoordinator coordinator(state, [rpc](auto request) { return rpc->Exchange(request); }, Roots,
            [setups] { ++*setups; return ERROR_SUCCESS; });
        const auto id = coordinator.EnableService(L"D:\\", {}); const bool finished = Finished(coordinator.CurrentAction());
        const auto result = state->Snapshot();
        passed &= Check(id && finished && *setups == 1 && rpc->builds == 1 && result.stage == IndexConnectionStage::Available &&
            result.selected_root == L"D:\\" && result.operation_running && result.operation_id == 77,
            "index_enable_sets_up_once_and_starts_initial_index_for_missing_store");
    }
    {
        const auto rpc = std::make_shared<Rpc>(); rpc->healthy = true;
        const auto setups = std::make_shared<std::atomic_uint>(); const auto state = std::make_shared<IndexServiceState>();
        IndexServiceCoordinator coordinator(state, [rpc](auto request) { return rpc->Exchange(request); }, Roots,
            [setups] { ++*setups; return ERROR_SUCCESS; });
        const auto id = coordinator.EnableService(L"D:\\", {}); const bool finished = Finished(coordinator.CurrentAction());
        const auto result = state->Snapshot();
        passed &= Check(id && finished && *setups == 1 && rpc->builds == 0 && result.stage == IndexConnectionStage::Available &&
            result.volumes.size() == 1 && result.volumes[0].stage == VolumeIndexStage::Healthy && !result.operation_running,
            "index_enable_keeps_existing_healthy_store_without_rebuilding");
    }
    {
        const auto rpc = std::make_shared<Rpc>(); const auto state = std::make_shared<IndexServiceState>();
        IndexServiceCoordinator coordinator(state, [rpc](auto request) { return rpc->Exchange(request); }, Roots,
            [] { return ERROR_CANCELLED; });
        const auto id = coordinator.EnableService(L"D:\\", {}); const bool finished = Finished(coordinator.CurrentAction());
        passed &= Check(id && finished && rpc->calls == 0 && rpc->builds == 0 &&
            state->Snapshot().stage == IndexConnectionStage::Unavailable && state->Snapshot().error == ERROR_CANCELLED,
            "index_enable_uac_refusal_reports_cancel_without_sending_rpc");
    }
    {
        const auto rpc = std::make_shared<Rpc>(); rpc->existing_operation = true;
        const auto state = std::make_shared<IndexServiceState>();
        IndexServiceCoordinator coordinator(state, [rpc](auto request) { return rpc->Exchange(request); }, Roots,
            [] { return ERROR_SUCCESS; });
        const auto id = coordinator.BuildIndex(L"D:\\", {}); const bool finished = Finished(coordinator.CurrentAction());
        passed &= Check(id && finished && rpc->calls == 1 && rpc->builds == 0 && state->Snapshot().operation_running &&
            state->Snapshot().operation_id == 77, "index_build_preserves_existing_operation_without_starting_another");
    }
    {
        const auto rpc = std::make_shared<Rpc>(); rpc->build_error = ERROR_DISK_FULL;
        const auto state = std::make_shared<IndexServiceState>();
        IndexServiceCoordinator coordinator(state, [rpc](auto request) { return rpc->Exchange(request); }, Roots,
            [] { return ERROR_SUCCESS; });
        const auto id = coordinator.BuildIndex(L"D:\\", {}); const bool finished = Finished(coordinator.CurrentAction());
        passed &= Check(id && finished && rpc->builds == 1 && state->Snapshot().stage == IndexConnectionStage::Unavailable &&
            state->Snapshot().error == ERROR_DISK_FULL, "index_build_request_failure_reports_actual_service_error");
    }
    {
        struct Gate { std::mutex mutex; std::condition_variable changed; bool entered{}, release{}; std::atomic_uint published{}; };
        const auto gate = std::make_shared<Gate>(); const auto rpc = std::make_shared<Rpc>();
        const auto state = std::make_shared<IndexServiceState>();
        IndexServiceCoordinator coordinator(state, [rpc](auto request) { return rpc->Exchange(request); }, Roots, [gate] {
            std::unique_lock lock(gate->mutex); gate->entered = true; gate->changed.notify_all();
            (void)gate->changed.wait_for(lock, std::chrono::seconds(5), [&] { return gate->release; });
            return ERROR_SUCCESS;
        });
        const auto id = coordinator.EnableService(L"D:\\", [gate](auto) { ++gate->published; });
        bool entered{};
        { std::unique_lock lock(gate->mutex); entered = gate->changed.wait_for(lock, std::chrono::seconds(5), [&] { return gate->entered; }); }
        const auto action = coordinator.Shutdown();
        { std::scoped_lock lock(gate->mutex); gate->release = true; gate->changed.notify_all(); }
        const bool finished = Finished(action);
        passed &= Check(id && entered && finished && state->Snapshot().stage == IndexConnectionStage::Closed && rpc->calls == 0 &&
            gate->published == 0 && !coordinator.EnableService(L"D:\\", {}),
            "index_enable_close_suppresses_late_setup_result_and_initial_request");
    }
    return passed;
}
