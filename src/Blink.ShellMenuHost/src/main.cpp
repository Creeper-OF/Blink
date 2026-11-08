#include "Blink/App/ShellContextMenu.h"
#include "Blink/Core/FileTypes.h"

#include <Windows.h>
#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "shell32")
#pragma comment(lib, "ole32")
#pragma comment(lib, "shlwapi")
#pragma comment(lib, "user32")
#pragma comment(lib, "uuid")

namespace
{
    using Microsoft::WRL::ComPtr;
    using Command = blink::app::ShellMenuCommand;
    constexpr UINT FirstShellId = 1;
    constexpr UINT LastShellId = 0x7fff;
    constexpr UINT FirstBlinkId = 0x8000;
    constexpr wchar_t WindowClass[] = L"Blink.ShellMenuHost.Window";
    bool reply_sent{};
    bool probe_mode{};

    HRESULT LastErrorResult() noexcept
    { const auto error = GetLastError(); return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE); }
    bool Equal(const std::wstring_view left, const std::wstring_view right) noexcept
    {
        return left.size() == right.size() && CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
            right.data(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
    }
    bool WriteOutput(const std::string_view value) noexcept
    {
        const auto output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!output || output == INVALID_HANDLE_VALUE) { return false; }
        std::size_t offset{};
        while (offset < value.size())
        {
            DWORD written{};
            if (!WriteFile(output, value.data() + offset, static_cast<DWORD>(value.size() - offset), &written, nullptr) || !written)
            { return false; }
            offset += written;
        }
        return true;
    }
    bool Reply(const char* kind, const Command command, const HRESULT error)
    {
        if (reply_sent) { return false; }
        char line[128]{};
        const auto length = std::snprintf(line, sizeof(line), "BLINK_SHELL_MENU_V1 %s %lu %08lX\n", kind,
            static_cast<unsigned long>(command), static_cast<unsigned long>(error));
        if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(line)) { return false; }
        reply_sent = WriteOutput(std::string_view(line, static_cast<std::size_t>(length)));
        return reply_sent;
    }
    std::string Utf8(const std::wstring& text)
    {
        if (text.empty()) { return {}; }
        const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) { return {}; }
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }
    std::string JsonString(const std::wstring& text)
    {
        constexpr char hex[] = "0123456789abcdef";
        std::string result = "\"";
        for (const auto character : Utf8(text))
        {
            const auto value = static_cast<unsigned char>(character);
            if (value == '"' || value == '\\') { result += '\\'; result += character; }
            else if (value < 32) { result += "\\u00"; result += hex[value >> 4]; result += hex[value & 15]; }
            else { result += character; }
        }
        result += '"'; return result;
    }
    void ProbeError(const HRESULT error)
    {
        char line[160]{};
        const auto size = std::snprintf(line, sizeof(line),
            "BLINK_SHELL_MENU_PROBE_V1 {\"event\":\"done\",\"count\":0,\"error\":\"%08lX\"}\n", static_cast<unsigned long>(error));
        if (size > 0 && static_cast<std::size_t>(size) < sizeof(line)) { (void)WriteOutput(std::string_view(line, static_cast<std::size_t>(size))); }
    }

    struct Options
    {
        struct AdditionalTarget { std::wstring path, identity; };
        std::wstring path, identity, parent_identity;
        std::vector<AdditionalTarget> additional;
        POINT point{};
        HWND owner{};
        HANDLE cancel{};
        bool chinese{ PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE };
        bool extended{}, probe{}, properties{};
    };
    bool UnsignedArgument(const std::wstring& text, std::uintptr_t& value) noexcept
    {
        if (text.empty() || text.front() == L'-') { return false; }
        errno = 0; wchar_t* end{};
        const auto parsed = std::wcstoull(text.c_str(), &end, 0);
        if (errno == ERANGE || !end || *end || parsed > (std::numeric_limits<std::uintptr_t>::max)()) { return false; }
        value = static_cast<std::uintptr_t>(parsed); return true;
    }
    bool Coordinate(const std::wstring& text, LONG& value) noexcept
    {
        if (text.empty()) { return false; }
        errno = 0; wchar_t* end{};
        const auto parsed = std::wcstoll(text.c_str(), &end, 10);
        if (errno == ERANGE || !end || *end || parsed < (std::numeric_limits<LONG>::min)() ||
            parsed > (std::numeric_limits<LONG>::max)()) { return false; }
        value = static_cast<LONG>(parsed); return true;
    }
    HRESULT Parse(const int count, wchar_t** arguments, Options& options)
    {
        GetCursorPos(&options.point);
        for (int index = 1; index < count; ++index)
        {
            const std::wstring_view key(arguments[index]);
            if (key == L"--probe") { options.probe = true; continue; }
            if (key == L"--extended") { options.extended = true; continue; }
            if (++index >= count) { return E_INVALIDARG; }
            const std::wstring value(arguments[index]);
            if (key == L"--path") { options.path = value; }
            else if (key == L"--identity") { options.identity = value; }
            else if (key == L"--parent-identity") { options.parent_identity = value; }
            else if (key == L"--additional-path")
            {
                if (!options.additional.empty() && options.additional.back().identity.empty()) { return E_INVALIDARG; }
                options.additional.push_back({ value, {} });
            }
            else if (key == L"--additional-identity")
            {
                if (options.additional.empty() || !options.additional.back().identity.empty()) { return E_INVALIDARG; }
                options.additional.back().identity = value;
            }
            else if (key == L"--x") { if (!Coordinate(value, options.point.x)) { return E_INVALIDARG; } }
            else if (key == L"--y") { if (!Coordinate(value, options.point.y)) { return E_INVALIDARG; } }
            else if (key == L"--owner" || key == L"--cancel-handle")
            {
                std::uintptr_t handle{};
                if (!UnsignedArgument(value, handle)) { return E_INVALIDARG; }
                if (key == L"--owner") { options.owner = reinterpret_cast<HWND>(handle); }
                else { options.cancel = reinterpret_cast<HANDLE>(handle); }
            }
            else if (key == L"--locale")
            {
                if (value != L"zh-CN" && value != L"en-US") { return E_INVALIDARG; }
                options.chinese = value == L"zh-CN";
            }
            else if (key == L"--verb")
            { if (value != L"properties") { return E_INVALIDARG; } options.properties = true; }
            else { return E_INVALIDARG; }
        }
        if (options.path.empty() || options.path.size() > 32767 || options.identity.empty() ||
            options.parent_identity.empty() || (options.probe && options.properties)) { return E_INVALIDARG; }
        options.path = blink::core::NormalizePath(options.path);
        for (auto& target : options.additional)
        {
            if (target.path.empty() || target.path.size() > 32767 || target.identity.empty()) { return E_INVALIDARG; }
            target.path = blink::core::NormalizePath(target.path);
            if (target.path.empty()) { return E_INVALIDARG; }
        }
        return options.path.empty() ? E_INVALIDARG : S_OK;
    }
    std::wstring Extended(const std::wstring& path)
    { return path.starts_with(L"\\\\?\\") || path.starts_with(L"\\\\") ? path : L"\\\\?\\" + path; }
    HRESULT ValidateTarget(const Options& options)
    {
        blink::core::FileEntry parent;
        const auto directory = std::filesystem::path(options.path).parent_path().wstring();
        if (directory.empty()) { return E_INVALIDARG; }
        struct PathLess
        {
            bool operator()(const std::wstring& left, const std::wstring& right) const noexcept
            { return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN; }
        };
        std::set<std::wstring, PathLess> paths;
        const auto validate = [&](const std::wstring& path, const std::wstring& identity) -> HRESULT {
            if (!Equal(std::filesystem::path(path).parent_path().wstring(), directory) || !paths.insert(path).second)
            { return E_INVALIDARG; }
            blink::core::FileEntry entry;
            if (!blink::core::ReadEntryMetadata(Extended(path), entry)) { return LastErrorResult(); }
            return Equal(entry.identity.ToString(), identity) ? S_OK : HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
        };
        if (const auto error = validate(options.path, options.identity); FAILED(error)) { return error; }
        for (const auto& target : options.additional)
        { if (const auto error = validate(target.path, target.identity); FAILED(error)) { return error; } }
        if (!blink::core::ReadEntryMetadata(Extended(directory), parent)) { return LastErrorResult(); }
        if (!parent.is_directory || !Equal(parent.identity.ToString(), options.parent_identity))
        { return HRESULT_FROM_WIN32(ERROR_FILE_INVALID); }
        return S_OK;
    }

    class ThreadReference final : public IUnknown
    {
    public:
        ThreadReference() noexcept : changed_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** value) noexcept override
        {
            if (!value) { return E_POINTER; }
            *value = nullptr;
            if (id != IID_IUnknown) { return E_NOINTERFACE; }
            *value = static_cast<IUnknown*>(this); AddRef(); return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() noexcept override
        { const auto count = InterlockedIncrement(&references_); SetEvent(changed_); return static_cast<ULONG>(count); }
        ULONG STDMETHODCALLTYPE Release() noexcept override
        {
            const auto count = InterlockedDecrement(&references_); SetEvent(changed_);
            if (!count) { delete this; }
            return static_cast<ULONG>(count);
        }
        LONG Count() noexcept { return InterlockedCompareExchange(&references_, 0, 0); }
        HANDLE Changed() const noexcept { return changed_; }
    private:
        ~ThreadReference() { if (changed_) { CloseHandle(changed_); } }
        LONG references_{ 1 };
        HANDLE changed_{};
    };
    struct ThreadScope
    {
        ThreadReference* reference{};
        LONG baseline{};
        ~ThreadScope()
        { if (reference) { (void)SHSetThreadRef(nullptr); reference->Release(); } }
        HRESULT Initialize()
        {
            reference = new ThreadReference();
            if (!reference->Changed()) { return LastErrorResult(); }
            const auto error = SHSetThreadRef(reference);
            if (FAILED(error)) { return error; }
            baseline = reference->Count(); return S_OK;
        }
    };
    struct MenuContext
    {
        HWND window{};
        ComPtr<IContextMenu> menu;
        ComPtr<IContextMenu2> menu2;
        ComPtr<IContextMenu3> menu3;
        std::atomic_bool selected{}, cancelled{};
        HANDLE cancellation{}, wait{};
        ~MenuContext()
        {
            StopCancellation();
            if (window) { DestroyWindow(window); }
            if (cancellation) { CloseHandle(cancellation); }
        }
        static void CALLBACK Cancel(void* parameter, BOOLEAN) noexcept
        {
            auto& context = *static_cast<MenuContext*>(parameter);
            if (!context.selected.load())
            {
                context.cancelled.store(true);
                PostMessageW(context.window, WM_CANCELMODE, 0, 0);
                PostMessageW(context.window, WM_CLOSE, 0, 0);
            }
        }
        HRESULT Monitor(const HANDLE event)
        {
            cancellation = event;
            if (event && !RegisterWaitForSingleObject(&wait, event, Cancel, this, INFINITE, WT_EXECUTEONLYONCE))
            { return LastErrorResult(); }
            return S_OK;
        }
        void StopCancellation() noexcept
        { if (wait) { (void)UnregisterWaitEx(wait, INVALID_HANDLE_VALUE); wait = nullptr; } }
        bool CommitSelection() noexcept
        {
            selected.store(true); StopCancellation();
            const bool stopped = cancelled.load() || (cancellation && WaitForSingleObject(cancellation, 0) == WAIT_OBJECT_0);
            if (cancellation) { CloseHandle(cancellation); cancellation = nullptr; }
            return !stopped;
        }
        void ReleaseMenu() noexcept { menu3.Reset(); menu2.Reset(); menu.Reset(); }
    };
    LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        auto* context = reinterpret_cast<MenuContext*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            context = static_cast<MenuContext*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
        }
        if (context)
        {
            if (message == WM_CLOSE || message == WM_CANCELMODE)
            {
                if (!context->selected.load()) { context->cancelled.store(true); EndMenu(); }
                return 0;
            }
            if (message == WM_TIMER) { return 0; }
            if (message == WM_INITMENUPOPUP || message == WM_UNINITMENUPOPUP || message == WM_DRAWITEM || message == WM_DELETEITEM ||
                message == WM_MEASUREITEM || message == WM_MENUCHAR)
            {
                LRESULT result{};
                if (context->menu3 && SUCCEEDED(context->menu3->HandleMenuMsg2(message, wparam, lparam, &result))) { return result; }
                if (context->menu2 && SUCCEEDED(context->menu2->HandleMenuMsg(message, wparam, lparam))) { return 0; }
            }
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    HRESULT CreateOwner(MenuContext& context)
    {
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW registration{}; registration.lpfnWndProc = WindowProcedure;
        registration.hInstance = instance; registration.lpszClassName = WindowClass;
        if (!RegisterClassW(&registration) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { return LastErrorResult(); }
        context.window = CreateWindowExW(WS_EX_TOOLWINDOW, WindowClass, L"Blink Windows menu", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, instance, &context);
        return context.window ? S_OK : LastErrorResult();
    }
    bool VisibleHostWindows(HWND owner) noexcept
    {
        struct Query { HWND owner; DWORD process; bool visible; } query{ owner, GetCurrentProcessId(), false };
        EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
            auto& current = *reinterpret_cast<Query*>(parameter); DWORD process{};
            GetWindowThreadProcessId(window, &process);
            if (window != current.owner && process == current.process && IsWindowVisible(window))
            { current.visible = true; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&query));
        return query.visible;
    }
    void PumpUntilIdle(MenuContext& context, ThreadScope& lifetime)
    {
        bool window_timer{};
        for (;;)
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT) { continue; }
                const auto root = message.hwnd ? GetAncestor(message.hwnd, GA_ROOT) : nullptr;
                wchar_t name[32]{};
                if (root && root != context.window && GetClassNameW(root, name, static_cast<int>(std::size(name))) &&
                    !std::wcscmp(name, L"#32770") && IsDialogMessageW(root, &message)) { continue; }
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            const bool windows = VisibleHostWindows(context.window);
            if (lifetime.reference->Count() <= lifetime.baseline && !windows) { break; }
            if (windows && !window_timer) { window_timer = SetTimer(context.window, 1, 250, nullptr) != 0; }
            else if (!windows && window_timer) { KillTimer(context.window, 1); window_timer = false; }
            const auto changed = lifetime.reference->Changed();
            if (MsgWaitForMultipleObjectsEx(1, &changed, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE | MWMO_ALERTABLE) == WAIT_FAILED)
            { break; }
        }
        if (window_timer) { KillTimer(context.window, 1); }
    }
    std::wstring CanonicalVerb(IContextMenu* menu, const UINT id)
    {
        if (!menu || id < FirstShellId || id > LastShellId) { return {}; }
        wchar_t verb[512]{};
        if (FAILED(menu->GetCommandString(id - FirstShellId, GCS_VERBW, nullptr,
            reinterpret_cast<LPSTR>(verb), static_cast<UINT>(std::size(verb))))) { return {}; }
        verb[std::size(verb) - 1] = L'\0'; return verb;
    }
    Command BlinkVerb(const std::wstring& verb)
    {
        if (Equal(verb, L"open")) { return Command::Open; }
        if (Equal(verb, L"copy")) { return Command::Copy; }
        if (Equal(verb, L"cut")) { return Command::Cut; }
        if (Equal(verb, L"paste")) { return Command::PasteParent; }
        if (Equal(verb, L"rename")) { return Command::Rename; }
        if (Equal(verb, L"delete"))
        { return (GetKeyState(VK_SHIFT) & 0x8000) ? Command::PermanentDelete : Command::Delete; }
        return Command::None;
    }
    std::wstring MenuLabel(const HMENU menu, const UINT position)
    {
        MENUITEMINFOW item{}; item.cbSize = sizeof(item); item.fMask = MIIM_STRING;
        if (!GetMenuItemInfoW(menu, position, TRUE, &item) || !item.cch) { return {}; }
        std::wstring label(static_cast<std::size_t>(item.cch) + 1, L'\0');
        item.dwTypeData = label.data(); item.cch = static_cast<UINT>(label.size());
        if (!GetMenuItemInfoW(menu, position, TRUE, &item)) { return {}; }
        label.resize(item.cch); return label;
    }
    void PrepareItems(const HMENU menu, IContextMenu* context, const Options& options, const unsigned depth = 0)
    {
        if (depth > 32) { return; }
        const auto count = GetMenuItemCount(menu);
        for (int index = 0; index < count; ++index)
        {
            MENUITEMINFOW item{}; item.cbSize = sizeof(item); item.fMask = MIIM_ID | MIIM_SUBMENU;
            if (!GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &item)) { continue; }
            if (Equal(CanonicalVerb(context, item.wID), L"paste"))
            {
                MENUITEMINFOW replacement{}; replacement.cbSize = sizeof(replacement); replacement.fMask = MIIM_STRING | MIIM_STATE;
                replacement.dwTypeData = const_cast<LPWSTR>(options.chinese ? L"粘贴到所在目录" : L"Paste into containing folder");
                replacement.fState = IsClipboardFormatAvailable(CF_HDROP) ? MFS_ENABLED : MFS_DISABLED;
                SetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &replacement);
            }
            if (item.hSubMenu) { PrepareItems(item.hSubMenu, context, options, depth + 1); }
        }
    }
    HRESULT AddBlinkItems(const HMENU menu, const Options& options)
    {
        if (!AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) ||
            !AppendMenuW(menu, MF_STRING, FirstBlinkId + static_cast<UINT>(Command::CopyFullPath), options.chinese ? L"复制完整路径" : L"Copy full path") ||
            !AppendMenuW(menu, MF_STRING, FirstBlinkId + static_cast<UINT>(Command::CopyParentPath), options.chinese ? L"复制所在目录路径" : L"Copy containing folder path") ||
            !AppendMenuW(menu, MF_STRING, FirstBlinkId + static_cast<UINT>(Command::Locate), options.chinese ? L"在 Blink 中定位" : L"Locate in Blink") ||
            !AppendMenuW(menu, MF_STRING | (IsClipboardFormatAvailable(CF_HDROP) ? MF_ENABLED : MF_GRAYED),
                FirstBlinkId + static_cast<UINT>(Command::PasteParent), options.chinese ? L"粘贴到所在目录" : L"Paste into containing folder"))
        { return LastErrorResult(); }
        return S_OK;
    }
    void EnumerateMenu(const HMENU menu, MenuContext& context, std::uint64_t& count, const unsigned depth = 0)
    {
        if (depth > 32) { return; }
        const auto items = GetMenuItemCount(menu);
        for (int index = 0; index < items; ++index)
        {
            MENUITEMINFOW item{}; item.cbSize = sizeof(item); item.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_FTYPE | MIIM_STATE;
            if (!GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &item) || (item.fType & MFT_SEPARATOR)) { continue; }
            ++count;
            const auto record = "BLINK_SHELL_MENU_PROBE_V1 {\"event\":\"item\",\"label\":" + JsonString(MenuLabel(menu, static_cast<UINT>(index))) +
                ",\"verb\":" + JsonString(CanonicalVerb(context.menu.Get(), item.wID)) + ",\"id\":" + std::to_string(item.wID) +
                ",\"depth\":" + std::to_string(depth) + ",\"submenu\":" + (item.hSubMenu ? "true" : "false") +
                ",\"owner_draw\":" + ((item.fType & MFT_OWNERDRAW) ? "true" : "false") +
                ",\"enabled\":" + ((item.fState & (MFS_DISABLED | MFS_GRAYED)) ? "false" : "true") + "}\n";
            (void)WriteOutput(record);
            if (item.hSubMenu)
            {
                SendMessageW(context.window, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(item.hSubMenu), MAKELPARAM(index, FALSE));
                EnumerateMenu(item.hSubMenu, context, count, depth + 1);
                SendMessageW(context.window, WM_UNINITMENUPOPUP, reinterpret_cast<WPARAM>(item.hSubMenu), 0);
            }
        }
    }
    void NativeError(const HWND owner, const HRESULT error, const bool chinese)
    {
        wchar_t text[1024]{};
        const DWORD message = HRESULT_FACILITY(error) == FACILITY_WIN32 ? HRESULT_CODE(error) : static_cast<DWORD>(error);
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, message, 0, text, static_cast<DWORD>(std::size(text)), nullptr);
        wchar_t code[32]{}; std::swprintf(code, std::size(code), L"0x%08lX", static_cast<unsigned long>(error));
        wchar_t detail[1536]{};
        std::swprintf(detail, std::size(detail), L"%ls\n%ls\n%ls",
            chinese ? L"Windows 未能执行此菜单命令。" : L"Windows could not invoke this menu command.", code, text);
        MessageBoxW(owner, detail, L"Blink", MB_OK | MB_ICONERROR);
    }
    struct Popup
    {
        HMENU value{};
        ~Popup() { Reset(); }
        void Reset() noexcept { if (value) { DestroyMenu(value); value = nullptr; } }
    };
    HRESULT Run(const int count, wchar_t** arguments)
    {
        Options options;
        auto error = Parse(count, arguments, options);
        if (FAILED(error)) { if (probe_mode) { ProbeError(error); } else { (void)Reply("error", Command::None, error); } return error; }
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        error = OleInitialize(nullptr);
        if (FAILED(error)) { if (probe_mode) { ProbeError(error); } else { (void)Reply("error", Command::None, error); } return error; }
        struct ComScope { ~ComScope() { OleUninitialize(); } } com;
        error = ValidateTarget(options);
        if (FAILED(error)) { if (probe_mode) { ProbeError(error); } else { (void)Reply("error", Command::None, error); } return error; }
        ThreadScope lifetime;
        if (FAILED(error = lifetime.Initialize())) { if (probe_mode) { ProbeError(error); } else { (void)Reply("error", Command::None, error); } return error; }
        MenuContext context;
        ComPtr<IShellFolder> parent;
        Popup popup;
        const auto finish = [&](const HRESULT result) {
            context.StopCancellation(); popup.Reset(); context.ReleaseMenu(); parent.Reset();
            PumpUntilIdle(context, lifetime); return result;
        };
        const auto fail = [&](const HRESULT failure) {
            if (probe_mode) { ProbeError(failure); } else { (void)Reply("error", Command::None, failure); }
            return finish(failure);
        };
        if (FAILED(error = CreateOwner(context))) { return fail(error); }
        if (FAILED(error = context.Monitor(options.cancel))) { return fail(error); }
        struct PidlScope
        {
            std::vector<PIDLIST_ABSOLUTE> values;
            ~PidlScope() { for (const auto value : values) { CoTaskMemFree(value); } }
        } pidls;
        std::vector<PCUITEMID_CHILD> children;
        pidls.values.reserve(options.additional.size() + 1);
        children.reserve(options.additional.size() + 1);
        const auto add_child = [&](const std::wstring& path) -> HRESULT {
            PIDLIST_ABSOLUTE absolute{};
            const auto parsed = SHParseDisplayName(path.c_str(), nullptr, &absolute, 0, nullptr);
            if (FAILED(parsed)) { return parsed; }
            pidls.values.push_back(absolute);
            PCUITEMID_CHILD child{};
            if (!parent)
            {
                const auto bound = SHBindToParent(absolute, IID_PPV_ARGS(&parent), &child);
                if (FAILED(bound)) { return bound; }
            }
            else { child = ILFindLastID(absolute); }
            children.push_back(child);
            return S_OK;
        };
        if (FAILED(error = add_child(options.path))) { return fail(error); }
        for (const auto& target : options.additional)
        { if (FAILED(error = add_child(target.path))) { return fail(error); } }
        if (FAILED(error = parent->GetUIObjectOf(context.window, static_cast<UINT>(children.size()), children.data(), IID_IContextMenu, nullptr,
            reinterpret_cast<void**>(context.menu.GetAddressOf())))) { return fail(error); }
        (void)context.menu.As(&context.menu2); (void)context.menu.As(&context.menu3);
        popup.value = CreatePopupMenu();
        if (!popup.value) { return fail(LastErrorResult()); }
        UINT flags = CMF_NORMAL | CMF_ITEMMENU | CMF_CANRENAME | CMF_SYNCCASCADEMENU;
        if (options.extended || (GetKeyState(VK_SHIFT) & 0x8000)) { flags |= CMF_EXTENDEDVERBS; }
        if (FAILED(error = context.menu->QueryContextMenu(popup.value, 0, FirstShellId, LastShellId, flags))) { return fail(error); }
        PrepareItems(popup.value, context.menu.Get(), options);
        if (options.probe)
        {
            std::uint64_t items{}; EnumerateMenu(popup.value, context, items);
            (void)WriteOutput("BLINK_SHELL_MENU_PROBE_V1 {\"event\":\"done\",\"count\":" + std::to_string(items) + ",\"error\":\"00000000\"}\n");
            return finish(S_OK);
        }
        if (context.cancelled.load()) { (void)Reply("cancel", Command::None, S_OK); return finish(S_OK); }
        UINT selected{};
        if (!options.properties)
        {
            if (FAILED(error = AddBlinkItems(popup.value, options))) { return fail(error); }
            if (options.owner && IsWindow(options.owner)) { SetForegroundWindow(options.owner); }
            SetForegroundWindow(context.window);
            SetLastError(ERROR_SUCCESS);
            selected = TrackPopupMenuEx(popup.value, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                options.point.x, options.point.y, context.window, nullptr);
            PostMessageW(context.window, WM_NULL, 0, 0);
            if (!selected)
            {
                const auto menu_error = GetLastError();
                if (menu_error && !context.cancelled.load()) { return fail(HRESULT_FROM_WIN32(menu_error)); }
                (void)Reply("cancel", Command::None, S_OK);
                return finish(S_OK);
            }
        }
        if (!context.CommitSelection()) { (void)Reply("cancel", Command::None, S_OK); return finish(S_OK); }
        if (FAILED(error = ValidateTarget(options))) { return fail(error); }
        const auto verb = options.properties ? L"properties" : CanonicalVerb(context.menu.Get(), selected);
        const bool blink_item = selected > FirstBlinkId && selected <= FirstBlinkId + static_cast<UINT>(Command::PasteParent);
        if (!options.properties && !blink_item && (selected < FirstShellId || selected > LastShellId)) { return fail(E_INVALIDARG); }
        Command action = blink_item ? static_cast<Command>(selected - FirstBlinkId) : BlinkVerb(verb);
        if (action != Command::None)
        {
            if (action == Command::PasteParent && !IsClipboardFormatAvailable(CF_HDROP)) { return fail(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)); }
            (void)Reply("blink", action, S_OK);
            return finish(S_OK);
        }
        if (!Reply("native", Command::None, S_OK)) { return finish(HRESULT_FROM_WIN32(ERROR_CANCELLED)); }
        CMINVOKECOMMANDINFOEX invoke{}; invoke.cbSize = sizeof(invoke);
        invoke.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE | CMIC_MASK_ASYNCOK;
        if (GetKeyState(VK_SHIFT) & 0x8000) { invoke.fMask |= CMIC_MASK_SHIFT_DOWN; }
        if (GetKeyState(VK_CONTROL) & 0x8000) { invoke.fMask |= CMIC_MASK_CONTROL_DOWN; }
        invoke.hwnd = context.window; invoke.nShow = SW_SHOWNORMAL; invoke.ptInvoke = options.point;
        invoke.lpVerb = options.properties ? "properties" : MAKEINTRESOURCEA(selected - FirstShellId);
        invoke.lpVerbW = options.properties ? L"properties" : MAKEINTRESOURCEW(selected - FirstShellId);
        error = context.menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&invoke));
        if (FAILED(error)) { NativeError(context.window, error, options.chinese); }
        return finish(error);
    }
}

int wmain(int argc, wchar_t** argv)
{
    for (int index = 1; index < argc; ++index) { if (!std::wcscmp(argv[index], L"--probe")) { probe_mode = true; } }
    try { return static_cast<int>(Run(argc, argv)); }
    catch (const std::bad_alloc&)
    {
        if (!reply_sent) { if (probe_mode) { ProbeError(E_OUTOFMEMORY); } else { (void)Reply("error", Command::None, E_OUTOFMEMORY); } }
        return static_cast<int>(E_OUTOFMEMORY);
    }
    catch (...)
    {
        if (!reply_sent) { if (probe_mode) { ProbeError(E_FAIL); } else { (void)Reply("error", Command::None, E_FAIL); } }
        return static_cast<int>(E_FAIL);
    }
}
