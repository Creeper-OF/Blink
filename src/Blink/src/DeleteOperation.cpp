#include "Blink/App/DeleteOperation.h"
#include "Blink/App/DirectoryLease.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <sherrors.h>
#include <wrl.h>
#include <algorithm>
#include <string_view>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

namespace blink::app
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        constexpr DWORD BaseFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NO_CONNECTED_ELEMENTS |
            FOFX_EARLYFAILURE | FOFX_NOCOPYHOOKS;
        bool Stopping(const FileOperationCancellation& cancelled) noexcept { return cancelled && cancelled->load(); }
        HRESULT Error() noexcept { const auto error = GetLastError(); return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE); }
        bool LocalParent(std::wstring_view path) noexcept
        {
            if (path.starts_with(L"\\\\?\\")) { path.remove_prefix(4); }
            if (path.size() < 3 || path.size() > 32760 || path[1] != L':' || path[2] != L'\\' ||
                !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
                path.find_first_of(L"/\0", 0, 2) != std::wstring_view::npos || path.find(L':', 2) != std::wstring_view::npos) { return false; }
            while (path.size() > 3 && path.back() == L'\\') { path.remove_suffix(1); }
            for (std::size_t offset = 3; offset < path.size();)
            {
                const auto end = path.find(L'\\', offset);
                const auto part = path.substr(offset, end == std::wstring_view::npos ? path.size() - offset : end - offset);
                if (part.empty() || part == L"." || part == L"..") { return false; }
                if (end == std::wstring_view::npos) { break; }
                offset = end + 1;
            }
            return true;
        }
        HRESULT Validate(const DeleteRequest& request) noexcept
        {
            if (request.mode != DeleteMode::Recycle && request.mode != DeleteMode::Permanent) { return E_INVALIDARG; }
            if (request.mode == DeleteMode::Permanent && !request.permanent_confirmed) { return E_ACCESSDENIED; }
            if (!request.parent_identity.valid || !request.source.identity.valid ||
                request.parent_identity.volume_serial != request.source.identity.volume_serial ||
                request.source.is_directory != ((request.source.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
            { return HRESULT_FROM_WIN32(ERROR_FILE_INVALID); }
            if (!LocalParent(request.parent)) { return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); }
            const auto name = ValidateNewFolderName(request.source.name);
            if (name) { return HRESULT_FROM_WIN32(name); }
            if (request.parent.size() + request.source.name.size() + 5 >= 32767) { return HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE); }
            return S_OK;
        }
        HRESULT VerifySource(const std::wstring& path, const DeleteRequest& request)
        {
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
            blink::core::UniqueHandle handle(CreateFile3(path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, &parameters));
            if (!handle) { return Error(); }
            blink::core::FileIdentity identity; FILE_ATTRIBUTE_TAG_INFO tag{}; FILE_STANDARD_INFO standard{};
            if (!blink::core::ReadFileIdentity(handle.get(), identity) ||
                !GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &tag, sizeof(tag)) ||
                !GetFileInformationByHandleEx(handle.get(), FileStandardInfo, &standard, sizeof(standard))) { return Error(); }
            if (identity != request.source.identity || standard.DeletePending ||
                request.source.is_directory != ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0))
            { return HRESULT_FROM_WIN32(ERROR_FILE_INVALID); }
            return S_OK;
        }
        bool OriginalLinkGone(const std::wstring& path, const blink::core::FileIdentity& expected)
        {
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
            blink::core::UniqueHandle handle(CreateFile3(path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, &parameters));
            if (!handle) { const auto error = GetLastError(); return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }
            blink::core::FileIdentity current;
            return blink::core::ReadFileIdentity(handle.get(), current) && current != expected;
        }
        std::wstring ItemName(IShellItem* item, SIGDN kind)
        {
            PWSTR raw{}; if (!item || FAILED(item->GetDisplayName(kind, &raw))) { return {}; }
            struct Free { PWSTR value; ~Free() { CoTaskMemFree(value); } } free{ raw };
            return raw ? std::wstring(raw) : std::wstring{};
        }
        class Sink final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IFileOperationProgressSink>
        {
        public:
            const DeleteRequest* request{};
            const std::wstring* native_path{};
            const std::wstring* root_path{};
            const FileOperationCancellation* cancelled{};
            const DeleteProgressCallback* progress{};
            ComPtr<IShellItem> root, recycled;
            DeleteProgress update;
            HRESULT item_error{ E_PENDING }, observer_error{ S_OK }, callback_error{ S_OK };
            bool started{}, root_candidate{}, recycle_veto{};
            bool IsRoot(IShellItem* item)
            {
                int order{};
                if (!item || !root) { return false; }
                if (item == root.Get() || (SUCCEEDED(item->Compare(root.Get(), SICHINT_CANONICAL, &order)) && order == 0)) { return true; }
                const auto name = ItemName(item, SIGDN_FILESYSPATH);
                auto actual = std::wstring_view(name), expected = std::wstring_view(*root_path);
                if (actual.starts_with(L"\\\\?\\")) { actual.remove_prefix(4); }
                if (expected.starts_with(L"\\\\?\\")) { expected.remove_prefix(4); }
                return actual.size() == expected.size() && actual.size() <= 32767 &&
                    CompareStringOrdinal(actual.data(), static_cast<int>(actual.size()), expected.data(), static_cast<int>(expected.size()), TRUE) == CSTR_EQUAL;
            }
            HRESULT Report() noexcept
            {
                if (Stopping(*cancelled)) { return HRESULT_FROM_WIN32(ERROR_CANCELLED); }
                try { if (*progress) { (*progress)(update); } return S_OK; }
                catch (...) { observer_error = E_FAIL; return observer_error; }
            }
            HRESULT STDMETHODCALLTYPE StartOperations() override { return Report(); }
            HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override { return S_OK; }
            HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD flags, IShellItem* item) override
            {
                try
                {
                    if (Stopping(*cancelled)) { return HRESULT_FROM_WIN32(ERROR_CANCELLED); }
                    if (request->mode == DeleteMode::Recycle && !(flags & TSF_DELETE_RECYCLE_IF_POSSIBLE))
                    { recycle_veto = true; callback_error = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); return callback_error; }
                    update.path = ItemName(item, SIGDN_FILESYSPATH);
                    if (update.path.size() > 32767) { callback_error = E_INVALIDARG; return callback_error; }
                    const auto reported = Report();
                    if (FAILED(reported)) { return reported; }
                    if (IsRoot(item))
                    {
                        const auto verified = VerifySource(*native_path, *request);
                        if (FAILED(verified)) { callback_error = verified; return verified; }
                        started = true;
                    }
                    return Stopping(*cancelled) ? HRESULT_FROM_WIN32(ERROR_CANCELLED) : S_OK;
                }
                catch (...) { callback_error = E_OUTOFMEMORY; return callback_error; }
            }
            HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD, IShellItem* item, HRESULT result, IShellItem* created) override
            {
                try
                {
                    const bool completed = result == S_OK || result == COPYENGINE_S_DONT_PROCESS_CHILDREN;
                    if (completed) { ++update.observed_removed; }
                    if (IsRoot(item))
                    {
                        item_error = result;
                        // Nonempty permanent folders report NOT_HANDLED before
                        // Shell traverses their children. Only the final path
                        // check after PerformOperations proves root removal.
                        root_candidate = completed || (request->source.is_directory && result == COPYENGINE_S_NOT_HANDLED);
                        recycled = created;
                    }
                    if (FAILED(result)) { update.path = ItemName(item, SIGDN_FILESYSPATH); }
                    // Completed native effects are recorded before observers.
                    return Report();
                }
                catch (...) { callback_error = E_OUTOFMEMORY; return callback_error; }
            }
            HRESULT STDMETHODCALLTYPE UpdateProgress(UINT total, UINT done) override
            { update.total_work = total; update.completed_work = (std::min)(total, done); return Report(); }
            HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
            HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
            HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
            HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
            HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
            HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) override { return S_OK; }
            HRESULT STDMETHODCALLTYPE PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
            HRESULT STDMETHODCALLTYPE PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override { return S_OK; }
            HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
            HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
            HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }
        };
    }
    HRESULT ValidateDeleteRequests(const std::vector<DeleteRequest>& requests, std::size_t& invalid) noexcept
    {
        invalid = 0;
        if (requests.empty() || requests.size() > MaximumDeleteItems) { return E_INVALIDARG; }
        std::size_t characters{};
        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            invalid = index; const auto& request = requests[index];
            if (request.mode != requests.front().mode) { return E_INVALIDARG; }
            if (const auto error = Validate(request); FAILED(error)) { return error; }
            const auto count = request.parent.size() + request.source.name.size() + 2;
            constexpr std::size_t maximum = 4 * 1024 * 1024 / sizeof(wchar_t);
            if (count > maximum - characters) { return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); }
            characters += count;
        }
        return S_OK;
    }
    DeleteResult DeleteEntrySafely(const DeleteRequest& request, const FileOperationCancellation& cancelled,
        const DeleteProgressCallback& progress) noexcept
    {
        DeleteResult result;
        try
        {
            result.path = blink::core::JoinPath(request.parent, request.source.name);
            if (request.skip) { result.error = S_OK; result.skipped = true; return result; }
            if (const auto error = Validate(request); FAILED(error)) { result.error = error; return result; }
            if (Stopping(cancelled)) { result.error = HRESULT_FROM_WIN32(ERROR_CANCELLED); return result; }
            APTTYPE apartment{}; APTTYPEQUALIFIER qualifier{};
            result.error = CoGetApartmentType(&apartment, &qualifier);
            if (FAILED(result.error)) { return result; }
            if (apartment != APTTYPE_STA && apartment != APTTYPE_MAINSTA) { result.error = RPC_E_WRONG_THREAD; return result; }
            auto parent = detail::AcquireDirectoryLease(request.parent, request.parent_identity, cancelled);
            if (parent.error) { result.error = HRESULT_FROM_WIN32(parent.error); return result; }
            result.path = blink::core::JoinPath(parent.path, request.source.name);
            const auto native = blink::core::JoinPath(parent.native_path, request.source.name);
            if (const auto error = VerifySource(native, request); FAILED(error)) { result.error = error; return result; }
            ComPtr<IShellItem> item; ComPtr<IFileOperation> operation;
            result.error = SHCreateItemFromParsingName(result.path.c_str(), nullptr, IID_PPV_ARGS(&item));
            if (FAILED(result.error)) { return result; }
            result.error = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&operation));
            if (FAILED(result.error)) { return result; }
            result.error = operation->SetOperationFlags(BaseFlags | (request.mode == DeleteMode::Recycle ? FOFX_RECYCLEONDELETE : 0));
            if (FAILED(result.error)) { return result; }
            auto sink = Microsoft::WRL::Make<Sink>();
            if (!sink) { result.error = E_OUTOFMEMORY; return result; }
            sink->request = &request; sink->native_path = &native; sink->cancelled = &cancelled; sink->progress = &progress;
            sink->root_path = &result.path;
            sink->root = item; sink->update.path = result.path;
            DWORD cookie{}; result.error = operation->Advise(sink.Get(), &cookie);
            if (FAILED(result.error)) { return result; }
            struct Unadvise
            {
                IFileOperation* operation; DWORD cookie;
                ~Unadvise() { (void)operation->Unadvise(cookie); }
            } unadvise{ operation.Get(), cookie };
            result.error = operation->DeleteItem(item.Get(), nullptr);
            if (FAILED(result.error)) { return result; }
            const auto performed = operation->PerformOperations();
            BOOL aborted{}; const auto aborted_result = operation->GetAnyOperationsAborted(&aborted);
            result.removed = sink->root_candidate && OriginalLinkGone(native, request.source.identity);
            result.recycled = result.removed && sink->recycled;
            result.observed_removed = sink->update.observed_removed;
            if (result.removed)
            {
                result.error = S_OK;
                if ((request.mode == DeleteMode::Recycle) != result.recycled) { result.post_commit_error = E_UNEXPECTED; }
                if (sink->recycled)
                {
                    try { result.recycle_item = ItemName(sink->recycled.Get(), SIGDN_DESKTOPABSOLUTEPARSING); }
                    catch (...) { result.post_commit_error = E_OUTOFMEMORY; }
                }
                return result; // Late stop/observer failure cannot undo deletion.
            }
            result.possible_partial = request.source.is_directory && sink->started;
            result.error = sink->recycle_veto ? HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) :
                FAILED(sink->callback_error) ? sink->callback_error : FAILED(sink->observer_error) ? sink->observer_error :
                Stopping(cancelled) || aborted || performed == COPYENGINE_E_USER_CANCELLED ? HRESULT_FROM_WIN32(ERROR_CANCELLED) :
                FAILED(sink->item_error) && sink->item_error != E_PENDING ? sink->item_error :
                FAILED(performed) ? performed : FAILED(aborted_result) ? aborted_result : E_FAIL;
            result.failure_path = sink->update.path;
        }
        catch (const std::bad_alloc&) { if (result.removed) { result.post_commit_error = E_OUTOFMEMORY; } else { result.error = E_OUTOFMEMORY; } }
        catch (...) { if (result.removed) { result.post_commit_error = E_FAIL; } else { result.error = E_FAIL; } }
        return result;
    }
}
