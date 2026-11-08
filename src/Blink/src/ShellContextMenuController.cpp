#include "Blink/App/ShellContextMenuController.h"
#include <winrt/Windows.System.Threading.h>
#include <filesystem>
#include <vector>
#include <iterator>
#pragma comment(lib, "ole32")

namespace blink::app
{
    namespace
    {
        struct CancelSignal
        {
            blink::core::UniqueHandle event;
            CancelSignal()
            {
                SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
                event.reset(CreateEventW(&security, TRUE, FALSE, nullptr));
                if (!event) { winrt::throw_last_error(); }
            }
        };
        ShellMenuResult Error(const DWORD error)
        { return { ShellMenuResultKind::Error, ShellMenuCommand::None, HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE) }; }
        ShellMenuResult Cancelled()
        { return { ShellMenuResultKind::Cancelled, ShellMenuCommand::None, S_OK }; }
    }
    struct ShellContextMenuController::State
    {
        mutable std::mutex mutex;
        std::uint64_t generation{};
        bool closed{};
        std::shared_ptr<CancelSignal> cancel;
        bool Current(const std::uint64_t value) const noexcept
        { std::scoped_lock lock(mutex); return !closed && generation == value; }
    };
    ShellContextMenuController::ShellContextMenuController() : state_(std::make_shared<State>()) {}
    ShellContextMenuController::~ShellContextMenuController() noexcept { Shutdown(); }
    bool ShellContextMenuController::IsCurrent(const std::uint64_t generation) const noexcept
    { return state_->Current(generation); }
    void ShellContextMenuController::Cancel(const bool closed) noexcept
    {
        std::scoped_lock lock(state_->mutex);
        state_->closed = state_->closed || closed;
        ++state_->generation;
        if (state_->cancel) { (void)SetEvent(state_->cancel->event.get()); }
        state_->cancel.reset();
    }
    void ShellContextMenuController::CancelPending() noexcept { Cancel(false); }
    void ShellContextMenuController::Shutdown() noexcept { Cancel(true); }

    ShellMenuResult ShellContextMenuController::Dispatch(const ShellMenuRequest& request, HANDLE cancel)
    {
        if (WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) { return Cancelled(); }
        GUID guid{};
        if (FAILED(CoCreateGuid(&guid))) { return Error(ERROR_NOT_ENOUGH_MEMORY); }
        wchar_t guid_text[40]{};
        StringFromGUID2(guid, guid_text, static_cast<int>(std::size(guid_text)));
        const auto pipe_name = L"\\\\.\\pipe\\Blink.Menu.Result." + std::to_wstring(GetCurrentProcessId()) + L"." + guid_text;
        blink::core::UniqueHandle reader(CreateNamedPipeW(pipe_name.c_str(),
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 4096, 4096, 0, nullptr));
        if (!reader) { return Error(GetLastError()); }
        SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
        blink::core::UniqueHandle writer(CreateFileW(pipe_name.c_str(), GENERIC_WRITE,
            0, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!writer) { return Error(GetLastError()); }
        // The local client has already connected; no blocking ConnectNamedPipe.
        blink::core::UniqueHandle null_input(CreateFileW(L"NUL", GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
        blink::core::UniqueHandle null_error(CreateFileW(L"NUL", GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
        if (!null_input || !null_error) { return Error(GetLastError()); }
        SIZE_T attribute_bytes{};
        (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
        std::vector<std::byte> attribute_buffer(attribute_bytes);
        const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_buffer.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes)) { return Error(GetLastError()); }
        struct Attributes { LPPROC_THREAD_ATTRIBUTE_LIST value; ~Attributes() { DeleteProcThreadAttributeList(value); } } attribute_scope{ attributes };
        const HANDLE inherited[]{ writer.get(), cancel, null_input.get(), null_error.get() };
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            const_cast<HANDLE*>(inherited), sizeof(inherited), nullptr, nullptr)) { return Error(GetLastError()); }
        std::wstring module(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        if (!length || length >= module.size()) { return Error(ERROR_BAD_PATHNAME); }
        module.resize(length);
        const auto host = (std::filesystem::path(module).parent_path() / L"Blink.ShellMenuHost.exe").wstring();
        auto command = BuildShellMenuCommandLine(host, request, reinterpret_cast<std::uintptr_t>(cancel));
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = null_input.get();
        startup.StartupInfo.hStdOutput = writer.get();
        startup.StartupInfo.hStdError = null_error.get();
        startup.lpAttributeList = attributes;
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(host.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &child))
        { return Error(GetLastError()); }
        blink::core::UniqueHandle process(child.hProcess), thread(child.hThread);
        writer.reset();
        blink::core::UniqueHandle read_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!read_event) { (void)SetEvent(cancel); return Error(GetLastError()); }
        std::string lines;
        std::size_t bytes_seen{};
        for (;;)
        {
            char buffer[2048]{};
            OVERLAPPED pending{}; pending.hEvent = read_event.get();
            (void)ResetEvent(read_event.get());
            DWORD count{};
            if (!ReadFile(reader.get(), buffer, sizeof(buffer), &count, &pending))
            {
                const auto error = GetLastError();
                if (error != ERROR_IO_PENDING) { return Error(error); }
                const HANDLE waits[]{ read_event.get(), cancel, process.get() };
                const auto waited = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
                if (waited != WAIT_OBJECT_0)
                {
                    (void)CancelIoEx(reader.get(), &pending);
                    (void)GetOverlappedResult(reader.get(), &pending, &count, TRUE);
                    if (waited == WAIT_OBJECT_0 + 1) { return Cancelled(); }
                    return Error(waited == WAIT_OBJECT_0 + 2 ? ERROR_PROCESS_ABORTED : GetLastError());
                }
                if (!GetOverlappedResult(reader.get(), &pending, &count, FALSE)) { return Error(GetLastError()); }
            }
            if (!count) { return Error(ERROR_BROKEN_PIPE); }
            bytes_seen += count;
            if (bytes_seen > 65536) { (void)SetEvent(cancel); return Error(ERROR_INVALID_DATA); }
            lines.append(buffer, count);
            for (auto end = lines.find('\n'); end != lines.npos; end = lines.find('\n'))
            {
                ShellMenuResult result;
                if (ParseShellMenuReply(std::string_view(lines.data(), end + 1), result)) { return result; }
                lines.erase(0, end + 1);
            }
        }
    }

    std::uint64_t ShellContextMenuController::Show(SearchContextSelection selection,
        POINT position, HWND owner, std::wstring locale, bool extended, bool properties, Completed completed)
    {
        CancelPending();
        std::scoped_lock work_lock(work_mutex_);
        std::uint64_t generation{};
        auto cancel = std::make_shared<CancelSignal>();
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->closed) { return 0; }
            generation = ++state_->generation; state_->cancel = cancel;
        }
        const auto state = state_;
        work_ = winrt::Windows::System::Threading::ThreadPool::RunAsync(
            [state, generation, cancel, selection = std::move(selection), position, owner,
                locale = std::move(locale), extended, properties, completed = std::move(completed)](auto&&) mutable {
                if (!state->Current(generation)) { return; }
                SearchActionTarget target; ShellMenuResult result;
                try
                {
                    target = ValidateSearchResult(selection.result);
                    if (target.error != ERROR_SUCCESS) { result = Error(target.error); }
                    else if (!state->Current(generation)) { return; }
                    else
                    {
                        ShellMenuRequest request{ target.entry, target.parent_identity, position, owner,
                            std::move(locale), extended, properties ? L"properties" : L"" };
                        result = Dispatch(request, cancel->event.get());
                    }
                }
                catch (...) { result = { ShellMenuResultKind::Error, ShellMenuCommand::None, winrt::to_hresult() }; }
                {
                    std::scoped_lock lock(state->mutex);
                    if (state->closed || state->generation != generation) { return; }
                    state->cancel.reset();
                }
                if (completed) { completed(generation, std::move(selection), std::move(target), result); }
            });
        return generation;
    }

    std::uint64_t ShellContextMenuController::Show(ShellMenuRequest request, RequestCompleted completed)
    {
        CancelPending();
        std::scoped_lock work_lock(work_mutex_);
        std::uint64_t generation{};
        auto cancel = std::make_shared<CancelSignal>();
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->closed) { return 0; }
            generation = ++state_->generation; state_->cancel = cancel;
        }
        const auto state = state_;
        work_ = winrt::Windows::System::Threading::ThreadPool::RunAsync(
            [state, generation, cancel, request = std::move(request), completed = std::move(completed)](auto&&) mutable {
                if (!state->Current(generation)) { return; }
                ShellMenuResult result;
                try
                {
                    const auto error = ValidateShellMenuRequest(request);
                    if (error) { result = Error(error); }
                    else if (!state->Current(generation)) { return; }
                    else { result = Dispatch(request, cancel->event.get()); }
                }
                catch (...) { result = { ShellMenuResultKind::Error, ShellMenuCommand::None, winrt::to_hresult() }; }
                {
                    std::scoped_lock lock(state->mutex);
                    if (state->closed || state->generation != generation) { return; }
                    state->cancel.reset();
                }
                if (completed) { completed(generation, result); }
            });
        return generation;
    }
}
