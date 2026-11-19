#include "Blink/App/ClipboardSender.h"
#include <ole2.h>
#include <atomic>
#include <list>
#include <future>
#include <mutex>
#include <thread>
#include <new>
#include <optional>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "User32.lib")

namespace blink::app
{
    namespace
    {
        struct OleApartment
        {
            HRESULT result{ OleInitialize(nullptr) };
            ~OleApartment() { Close(); }
            void Close() noexcept { if (SUCCEEDED(result)) { OleUninitialize(); result = E_FAIL; } }
        };
        HRESULT Failure(const HRESULT result) noexcept { return FAILED(result) ? result : E_UNEXPECTED; }
        void Notify(std::function<void(HRESULT)> callback, const HRESULT result) noexcept
        { try { if (callback) { callback(result); } } catch (...) {} }
        HRESULT DiscardExpired(const HWND owner) noexcept
        {
            if (!owner) { return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); }
            if (!OpenClipboard(nullptr)) { return CLIPBRD_E_CANT_OPEN; }
            // Recheck atomically under the Win32 lock. No OLE/RPC call occurs
            // here, and our retained object prevents its final COM release.
            const bool same_owner = GetClipboardOwner() == owner;
            const bool emptied = !same_owner || EmptyClipboard();
            const auto error = emptied ? S_OK : HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_GEN_FAILURE);
            CloseClipboard();
            return error;
        }
        bool ExpiredSource(HRESULT error) noexcept
        { return error == HRESULT_FROM_WIN32(ERROR_FILE_INVALID) || error == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || error == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND); }
    }
    struct ClipboardSender::State : std::enable_shared_from_this<State>
    {
        struct Request
        {
            std::vector<std::wstring> paths;
            ClipboardTransferIntent intent{};
            FileOperationCancellation cancelled;
            ClipboardSourceCallback callback;
            ClipboardSourceValidation validation;
            std::shared_ptr<std::promise<ClipboardSendResult>> ready;
        };
        explicit State(ClipboardSenderApis value) : apis(std::move(value)) {}
        ~State() { if (event) { CloseHandle(event); } }
        ClipboardSenderApis apis;
        HANDLE event{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };
        std::mutex mutex;
        std::list<Request> requests;
        std::function<void(HRESULT)> closing_callback;
        std::atomic_bool started{}, ended{};
        bool closing{};

        static void Finish(Request& request, ClipboardSendResult result) noexcept
        { try { request.ready->set_value(result); } catch (...) {} }
        void RequestClose(std::function<void(HRESULT)> callback) noexcept
        {
            HRESULT immediate{ S_OK }; bool notify{};
            try
            {
                {
                    const std::scoped_lock lock(mutex);
                    if (ended.load()) { notify = true; }
                    else if (!started.load()) { closing = true; ended.store(true); notify = true; }
                    else if (closing)
                    { immediate = HRESULT_FROM_WIN32(ERROR_BUSY); notify = true; }
                    else
                    { closing = true; closing_callback = std::move(callback); (void)SetEvent(event); }
                }
                if (notify) { Notify(std::move(callback), immediate); }
            }
            catch (...) { Notify(std::move(callback), E_FAIL); }
        }
        ClipboardSendResult Submit(Request request)
        {
            if (!event)
            { return { HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY) }; }
            auto future = request.ready->get_future();
            {
                const std::scoped_lock lock(mutex);
                if (closing || ended.load()) { return { HRESULT_FROM_WIN32(ERROR_CANCELLED) }; }
                requests.push_back(std::move(request));
                if (!started.load())
                {
                    try
                    {
                        auto self = shared_from_this();
                        std::thread([self] { self->Run(); }).detach();
                        started.store(true);
                    }
                    catch (...)
                    { requests.pop_back(); throw; }
                }
                (void)SetEvent(event);
            }
            return future.get();
        }
        ClipboardSendResult Send(Request& request, winrt::com_ptr<IDataObject>& current,
            ClipboardSourceValidation& current_validation, HWND& owner) noexcept
        {
            try
            {
                const auto cancelled = [&] { return request.cancelled && request.cancelled->load(); };
                if (cancelled()) { return { HRESULT_FROM_WIN32(ERROR_CANCELLED) }; }
                winrt::com_ptr<IDataObject> object;
                auto result = apis.create ? apis.create(request.paths, request.intent, request.callback, request.validation, object) :
                    CreateClipboardSourceData(request.paths, request.intent, request.callback, request.validation, object);
                if (result != S_OK) { return { Failure(result) }; }
                if (!object) { return { E_UNEXPECTED }; }
                if (request.validation)
                {
                    result = request.validation();
                    if (result != S_OK) { return { Failure(result) }; }
                }
                if (cancelled()) { return { HRESULT_FROM_WIN32(ERROR_CANCELLED) }; }
                result = apis.publish ? apis.publish(object.get()) : OleSetClipboard(object.get());
                if (result != S_OK) { return { Failure(result) }; }
                // Publication is already committed, including cancellation or
                // close racing after the OLE call began. Preserve its result.
                current = std::move(object);
                current_validation = std::move(request.validation);
                try { owner = apis.owner ? apis.owner() : GetClipboardOwner(); } catch (...) { owner = nullptr; }
                DWORD sequence{};
                try { sequence = apis.sequence ? apis.sequence() : GetClipboardSequenceNumber(); } catch (...) {}
                return { S_OK, sequence };
            }
            catch (const std::bad_alloc&) { return { E_OUTOFMEMORY }; }
            catch (...) { return { E_FAIL }; }
        }
        HRESULT Flush(IDataObject* object, const ClipboardSourceValidation& validation, HWND owner) noexcept
        {
            if (!object) { return S_OK; }
            try
            {
                const auto current = apis.current ? apis.current(object) : OleIsCurrentClipboard(object);
                if (current == S_FALSE) { return S_OK; } // Never replace another owner's Clipboard.
                if (current != S_OK) { return Failure(current); }
                if (validation)
                {
                    const auto valid = validation();
                    if (ExpiredSource(valid))
                    {
                        const auto discarded = apis.discard ? apis.discard(owner) : DiscardExpired(owner);
                        // Source paths no longer identify the cut/copied files.
                        // Withdraw only our expired offer instead of rendering
                        // a replacement file or trapping normal window close.
                        return discarded == S_OK ? S_FALSE : Failure(discarded);
                    }
                    if (valid != S_OK) { return Failure(valid); }
                    // Validation can perform metadata I/O. Ownership may have
                    // changed during that time; do not flush the new owner's data.
                    const auto still_current = apis.current ? apis.current(object) : OleIsCurrentClipboard(object);
                    if (still_current == S_FALSE) { return S_OK; }
                    if (still_current != S_OK) { return Failure(still_current); }
                }
                const auto result = apis.flush ? apis.flush() : OleFlushClipboard();
                return result == S_OK ? S_OK : Failure(result);
            }
            catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
            catch (...) { return E_FAIL; }
        }
        void FailPending(HRESULT error) noexcept
        {
            std::list<Request> pending;
            { const std::scoped_lock lock(mutex); pending.swap(requests); }
            for (auto& request : pending) { Finish(request, { error }); }
        }
        void Run() noexcept
        {
            OleApartment apartment;
            winrt::com_ptr<IDataObject> current;
            ClipboardSourceValidation current_validation;
            HWND owner{};
            std::function<void(HRESULT)> finished;
            HRESULT exit_error{ S_OK };
            try
            {
                if (FAILED(apartment.result)) { exit_error = apartment.result; }
                else
                {
                    MSG message{}; (void)PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
                    for (;;)
                    {
                        std::optional<Request> request;
                        bool should_close{};
                        {
                            const std::scoped_lock lock(mutex);
                            should_close = closing;
                            if (!should_close && !requests.empty())
                            { request.emplace(std::move(requests.front())); requests.pop_front(); }
                        }
                        if (should_close)
                        {
                            FailPending(HRESULT_FROM_WIN32(ERROR_CANCELLED));
                            const auto result = Flush(current.get(), current_validation, owner);
                            {
                                const std::scoped_lock lock(mutex);
                                finished = std::move(closing_callback);
                                if (FAILED(result)) { closing = false; }
                            }
                            if (SUCCEEDED(result)) { exit_error = result; break; }
                            Notify(std::move(finished), result);
                            continue; // Keep serving the source so closing can be retried.
                        }
                        if (request) { Finish(*request, Send(*request, current, current_validation, owner)); continue; }
                        const auto wait = MsgWaitForMultipleObjectsEx(1, &event, INFINITE,
                            QS_ALLINPUT, MWMO_INPUTAVAILABLE | MWMO_ALERTABLE);
                        if (wait == WAIT_OBJECT_0 || wait == WAIT_IO_COMPLETION) { continue; }
                        if (wait == WAIT_OBJECT_0 + 1)
                        {
                            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                            {
                                if (message.message == WM_QUIT) { exit_error = E_ABORT; break; }
                                TranslateMessage(&message); DispatchMessageW(&message);
                            }
                            if (FAILED(exit_error)) { break; }
                        }
                        else
                        { const auto error = GetLastError(); exit_error = HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE); break; }
                    }
                }
            }
            catch (const std::bad_alloc&) { exit_error = E_OUTOFMEMORY; }
            catch (...) { exit_error = E_FAIL; }
            {
                const std::scoped_lock lock(mutex);
                closing = true;
                if (!finished) { finished = std::move(closing_callback); }
            }
            FailPending(FAILED(exit_error) ? exit_error : HRESULT_FROM_WIN32(ERROR_CANCELLED));
            if (FAILED(exit_error) && current) { (void)Flush(current.get(), current_validation, owner); }
            current = nullptr;
            current_validation = {};
            apartment.Close();
            ended.store(true);
            Notify(std::move(finished), exit_error);
        }
    };
    ClipboardSender::ClipboardSender(ClipboardSenderApis apis) : state_(std::make_shared<State>(std::move(apis))) {}
    ClipboardSender::~ClipboardSender() noexcept { Shutdown(); }
    bool ClipboardSender::Active() const noexcept { return state_->started.load() && !state_->ended.load(); }
    void ClipboardSender::Shutdown(std::function<void(HRESULT)> callback) noexcept
    { state_->RequestClose(std::move(callback)); }
    ClipboardSendResult ClipboardSender::Publish(const std::span<const std::wstring> paths,
        const ClipboardTransferIntent intent, const FileOperationCancellation& cancelled,
        ClipboardSourceCallback callback, ClipboardSourceValidation validation)
    {
        try
        {
            if (cancelled && cancelled->load()) { return { HRESULT_FROM_WIN32(ERROR_CANCELLED) }; }
            if (EncodeClipboardFilePaths(paths).empty() || !ClipboardDropEffectValue(intent)) { return { E_INVALIDARG }; }
            State::Request request{ { paths.begin(), paths.end() }, intent, cancelled,
                std::move(callback), std::move(validation), std::make_shared<std::promise<ClipboardSendResult>>() };
            return state_->Submit(std::move(request));
        }
        catch (const std::bad_alloc&) { return { E_OUTOFMEMORY }; }
        catch (...) { return { E_FAIL }; }
    }
}
