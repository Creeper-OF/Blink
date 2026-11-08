#include "Blink/App/IndexServiceCoordinator.h"
#include "Blink/App/Location.h"

#include <winrt/Windows.System.Threading.h>
#include <algorithm>
#include <utility>
#include <filesystem>
#include <cstddef>
#include <sddl.h>
#include <shellapi.h>

namespace blink::app
{
    namespace
    {
        namespace ipc = blink::index::ipc;
        std::wstring Argument(const std::wstring& value)
        {
            std::wstring result{ L"\"" }; std::size_t slashes{};
            for (const auto c : value)
            {
                if (c == L'\\') { ++slashes; continue; }
                result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
                result.push_back(c); slashes = 0;
            }
            result.append(slashes * 2, L'\\'); result.push_back(L'"'); return result;
        }
        DWORD EnableInstalledService()
        {
            wchar_t module[32768]{};
            const auto length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
            if (!length || length >= std::size(module)) { return ERROR_BAD_PATHNAME; }
            std::filesystem::path root = std::filesystem::path(module).parent_path();
            for (int depth = 0; depth < 10 && !root.empty() && !std::filesystem::is_regular_file(root / L"Blink.sln"); ++depth)
            { root = root.parent_path(); }
            const auto script = root / L"scripts" / L"enable-index-service.ps1";
            if (root.empty() || !std::filesystem::is_regular_file(script)) { return ERROR_FILE_NOT_FOUND; }
            HANDLE raw_token{};
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) { return GetLastError(); }
            blink::core::UniqueHandle token(raw_token); DWORD bytes{};
            (void)GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
            std::vector<std::byte> storage(bytes);
            if (!GetTokenInformation(token.get(), TokenUser, storage.data(), bytes, &bytes)) { return GetLastError(); }
            LPWSTR raw_sid{};
            if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &raw_sid)) { return GetLastError(); }
            const std::wstring sid(raw_sid); LocalFree(raw_sid);
            wchar_t system[32768]{};
            const auto system_length = GetSystemDirectoryW(system, static_cast<UINT>(std::size(system)));
            if (!system_length || system_length >= std::size(system)) { return ERROR_BAD_PATHNAME; }
            const auto powershell = std::filesystem::path(system) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
#ifdef _DEBUG
            const std::wstring configuration = L"Debug";
#else
            const std::wstring configuration = L"Release";
#endif
            const auto parameters = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + Argument(script.wstring()) +
                L" -Configuration " + configuration + L" -AuthorizedClientSid " + Argument(sid);
            SHELLEXECUTEINFOW execute{ sizeof(execute) }; execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            execute.lpVerb = L"runas"; execute.lpFile = powershell.c_str(); execute.lpParameters = parameters.c_str();
            execute.lpDirectory = root.c_str(); execute.nShow = SW_HIDE;
            if (!ShellExecuteExW(&execute)) { return GetLastError(); }
            blink::core::UniqueHandle process(execute.hProcess);
            if (!process) { return ERROR_INVALID_HANDLE; }
            if (WaitForSingleObject(process.get(), 120000) != WAIT_OBJECT_0) { return ERROR_TIMEOUT; }
            DWORD exit_code{};
            return GetExitCodeProcess(process.get(), &exit_code) ? exit_code : GetLastError();
        }
        std::wstring DriveRoot(std::wstring root)
        {
            if (root.size() != 3 || root[1] != L':' || root[2] != L'\\' ||
                !((root[0] >= L'A' && root[0] <= L'Z') || (root[0] >= L'a' && root[0] <= L'z')))
            {
                return {};
            }
            if (root[0] >= L'a' && root[0] <= L'z') { root[0] -= L'a' - L'A'; }
            return root;
        }

        IndexServiceCoordinator::ExchangeFunction DefaultExchange()
        {
            const auto client = std::make_shared<ipc::Client>();
            return [client](ipc::Request request) {
                IndexRpcResult result;
                if (!client->IsConnected() && !client->Connect(1000, &result.error)) { return result; }
                result.received = client->Send(std::move(request), result.response, 5000, &result.error);
                if (result.received)
                {
                    // Client::Send assigned the ID and checked its matching
                    // response. Preserve that validated ID at this boundary.
                    result.expected_request_id = result.response.request_id;
                }
                else
                {
                    client->Disconnect(); // Worker only; never take its I/O mutex on the UI.
                }
                return result;
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

        bool UseReply(IndexServiceSnapshot& snapshot, const IndexRpcResult& reply)
        {
            snapshot.error = ValidateIndexReply(reply);
            if (snapshot.error != ERROR_SUCCESS)
            {
                snapshot.stage = IndexConnectionStage::Unavailable;
                return false;
            }
            snapshot.last_request_id = reply.response.request_id;
            snapshot.service_state = reply.response.service_state;
            return true;
        }

        IndexServiceSnapshot CollectStatus(const IndexServiceCoordinator::ExchangeFunction& exchange,
            const IndexServiceCoordinator::DiscoverFunction& discover,
            const std::shared_ptr<IndexServiceState>& state, const std::uint64_t generation,
            std::wstring selected_root)
        {
            IndexServiceSnapshot snapshot;
            snapshot.stage = IndexConnectionStage::Available;
            for (auto root : discover())
            {
                root = DriveRoot(std::move(root));
                if (!root.empty() && std::none_of(snapshot.volumes.begin(), snapshot.volumes.end(),
                        [&root](const auto& existing) { return existing.root == root; }))
                {
                    VolumeIndexStatus volume;
                    volume.root = std::move(root);
                    snapshot.volumes.push_back(std::move(volume));
                }
            }
            selected_root = DriveRoot(std::move(selected_root));
            if (std::none_of(snapshot.volumes.begin(), snapshot.volumes.end(),
                    [&selected_root](const auto& volume) { return volume.root == selected_root; }))
            {
                selected_root = snapshot.volumes.empty() ? std::wstring{} : snapshot.volumes.front().root;
            }
            snapshot.selected_root = selected_root;
            if (!state->IsCurrent(generation)) { return snapshot; }
            ipc::Request request;
            request.type = ipc::RequestType::QueryIndexProgress;
            const auto progress = exchange(request);
            if (!UseReply(snapshot, progress)) { return snapshot; }
            if (progress.response.status != ipc::ResponseStatus::Ok &&
                progress.response.status != ipc::ResponseStatus::NotFound)
            {
                snapshot.stage = IndexConnectionStage::Unavailable;
                snapshot.error = progress.response.error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : progress.response.error;
                return snapshot;
            }
            if (progress.response.status == ipc::ResponseStatus::Ok)
            {
                snapshot.operation_id = progress.response.operation_id;
                snapshot.operation_state = progress.response.index_state;
                snapshot.operation_records = progress.response.records_seen;
                snapshot.operation_running = progress.response.error == ERROR_IO_PENDING;
                if (snapshot.operation_running)
                {
                    if (snapshot.operation_id == 0)
                    {
                        snapshot.stage = IndexConnectionStage::Unavailable;
                        snapshot.error = ERROR_INVALID_DATA;
                    }
                    // Busy snapshots are service-wide. Do not read stats or
                    // relabel every volume as the active operation's volume.
                    return snapshot;
                }
            }
            if (!state->IsCurrent(generation)) { return snapshot; }
            request.type = ipc::RequestType::QueryServiceStatus;
            const auto service = exchange(request);
            if (!UseReply(snapshot, service)) { return snapshot; }
            if (service.response.status != ipc::ResponseStatus::Ok || service.response.error != ERROR_SUCCESS)
            {
                snapshot.stage = IndexConnectionStage::Unavailable;
                snapshot.error = service.response.error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : service.response.error;
                return snapshot;
            }
            if (!state->IsCurrent(generation) || selected_root.empty()) { return snapshot; }
            request.type = ipc::RequestType::QueryVolumeStatus;
            request.volume_root = selected_root;
            const auto volume = exchange(request);
            if (!UseReply(snapshot, volume)) { return snapshot; }
            for (auto& item : snapshot.volumes)
            {
                if (item.root == selected_root) { item = MapVolumeIndexStatus(selected_root, volume); }
            }
            return snapshot;
        }
    }

    IndexServiceCoordinator::IndexServiceCoordinator(std::shared_ptr<IndexServiceState> state,
        ExchangeFunction exchange, DiscoverFunction discover, EnableFunction enable)
        : state_(std::move(state)), exchange_(exchange ? std::move(exchange) : DefaultExchange()),
          discover_(discover ? std::move(discover) : DiscoverRoots), enable_(enable ? std::move(enable) : EnableInstalledService) {}
    IndexServiceCoordinator::~IndexServiceCoordinator() noexcept { (void)Shutdown(); }

    std::uint64_t IndexServiceCoordinator::Refresh(std::wstring selected_root, PublishCallback publish)
    { return Start(std::move(selected_root), std::move(publish), false, false); }
    std::uint64_t IndexServiceCoordinator::EnableService(std::wstring selected_root, PublishCallback publish)
    { return Start(std::move(selected_root), std::move(publish), true, true); }
    std::uint64_t IndexServiceCoordinator::BuildIndex(std::wstring selected_root, PublishCallback publish)
    { return Start(std::move(selected_root), std::move(publish), false, true); }
    std::uint64_t IndexServiceCoordinator::Start(std::wstring selected_root, PublishCallback publish, const bool enable, const bool build)
    {
        if (build && !enable && DriveRoot(selected_root).empty()) { return 0; }
        const auto generation = state_->Begin();
        if (generation == 0) { return 0; }
        const auto state = state_;
        const auto exchange = exchange_;
        const auto discover = discover_;
        const auto enable_service = enable_;
        std::unique_lock lock(mutex_);
        try
        {
            work_ = winrt::Windows::System::Threading::ThreadPool::RunAsync(
                [state, exchange, discover, enable_service, enable, build, generation, root = std::move(selected_root), publish](auto&&) mutable {
                    if (!state->IsCurrent(generation)) { return; }
                    IndexServiceSnapshot snapshot;
                    try
                    {
                        const auto setup_error = enable ? enable_service() : ERROR_SUCCESS;
                        if (setup_error)
                        { snapshot.stage = IndexConnectionStage::Unavailable; snapshot.error = setup_error; snapshot.selected_root = root; }
                        else if (state->IsCurrent(generation))
                        {
                            snapshot = CollectStatus(exchange, discover, state, generation, root);
                            const auto volume = std::find_if(snapshot.volumes.begin(), snapshot.volumes.end(), [&](const auto& value) {
                                return value.root == snapshot.selected_root;
                            });
                            const bool needs_initial = volume != snapshot.volumes.end() &&
                                (volume->stage == VolumeIndexStage::RebuildRequired ||
                                    (!enable && volume->stage == VolumeIndexStage::Corrupted) ||
                                    volume->error == ERROR_FILE_NOT_FOUND || volume->error == ERROR_PATH_NOT_FOUND);
                            if (build && needs_initial && snapshot.stage == IndexConnectionStage::Available &&
                                !snapshot.operation_running && !snapshot.selected_root.empty() && state->IsCurrent(generation))
                            {
                                ipc::Request request; request.type = ipc::RequestType::StartInitialIndex;
                                request.volume_root = snapshot.selected_root;
                                const auto reply = exchange(request);
                                if (UseReply(snapshot, reply))
                                {
                                    if (reply.response.status == ipc::ResponseStatus::Ok || reply.response.status == ipc::ResponseStatus::Busy)
                                    { snapshot = CollectStatus(exchange, discover, state, generation, snapshot.selected_root); }
                                    else
                                    { snapshot.stage = IndexConnectionStage::Unavailable; snapshot.error = reply.response.error ? reply.response.error : ERROR_GEN_FAILURE; }
                                }
                            }
                        }
                    }
                    catch (const std::bad_alloc&)
                    { snapshot.stage = IndexConnectionStage::Unavailable; snapshot.error = ERROR_NOT_ENOUGH_MEMORY; }
                    catch (...)
                    { snapshot.stage = IndexConnectionStage::Unavailable; snapshot.error = ERROR_GEN_FAILURE; }
                    if (state->Complete(generation, std::move(snapshot)) && publish)
                    {
                        try { publish(generation); } catch (...) { /* Dispatcher shutdown is terminal. */ }
                    }
                });
        }
        catch (...)
        {
            lock.unlock();
            IndexServiceSnapshot failure;
            failure.stage = IndexConnectionStage::Unavailable;
            failure.error = ERROR_GEN_FAILURE;
            if (state->Complete(generation, std::move(failure)) && publish)
            {
                try { publish(generation); } catch (...) { /* Dispatcher shutdown is terminal. */ }
            }
        }
        return generation;
    }

    winrt::Windows::Foundation::IAsyncAction IndexServiceCoordinator::CurrentAction() const
    { std::scoped_lock lock(mutex_); return work_; }

    winrt::Windows::Foundation::IAsyncAction IndexServiceCoordinator::Shutdown() noexcept
    {
        state_->Shutdown();
        std::scoped_lock lock(mutex_);
        // Cancellation is logical. A shared worker owns the Client until the
        // existing bounded Send returns; UI close never calls Disconnect/waits.
        return std::exchange(work_, nullptr);
    }
}
