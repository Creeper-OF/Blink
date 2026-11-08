#pragma once
#include "Blink/App/ClipboardFileTransfer.h"
#include <objidl.h>
#include <shldisp.h>
#include <winrt/base.h>
#include <atomic>
#include <functional>
#include <mutex>

namespace blink::app
{
    struct FileDragFeedback
    {
        std::atomic<DWORD> performed{}, logical{};
        std::atomic_bool in_operation{}, async_mode{ true };
        std::atomic_bool internal_receiver{};
        std::atomic<HRESULT> error{ S_OK };
        std::function<void(DWORD)> changed;
    };
    struct FileDragData
    {
        winrt::com_ptr<IDataObject> object;
        std::shared_ptr<FileDragFeedback> feedback;
    };
    struct FileDragPayload
    {
        HRESULT error{ E_FAIL };
        std::vector<std::wstring> paths;
        std::wstring token;
    };
    [[nodiscard]] HRESULT CreateFileDragData(std::span<const std::wstring> paths, std::wstring token, FileDragData& output) noexcept;
    [[nodiscard]] FileDragPayload ReadFileDragData(IDataObject* object) noexcept;
    [[nodiscard]] HRESULT QueryFileDragDataObject(IUnknown* package_or_object, winrt::com_ptr<IDataObject>& object) noexcept;
    // DataPackage's documented IDataObjectProvider is preferred. A raw
    // IDataObject input also works. No clipboard is read or replaced.
    class FileDragDataReference
    {
    public:
        [[nodiscard]] HRESULT Capture(IUnknown* package_or_object) noexcept;
        [[nodiscard]] HRESULT Resolve(winrt::com_ptr<IDataObject>& object) const noexcept;
        explicit operator bool() const noexcept { return static_cast<bool>(reference_); }
    private:
        winrt::com_ptr<IAgileReference> reference_;
    };
    class IncomingFileDrag
    {
    public:
        explicit IncomingFileDrag(FileDragDataReference reference, bool moving) : reference_(std::move(reference)), moving_(moving) {}
        // Worker-only COM calls; Begin/Finish serialize, including shutdown.
        [[nodiscard]] HRESULT Begin() noexcept;
        [[nodiscard]] bool Asynchronous() const noexcept { return asynchronous_.load(); }
        [[nodiscard]] HRESULT Finish(HRESULT result, DWORD logical_effect) noexcept;
    private:
        FileDragDataReference reference_;
        std::mutex mutex_;
        std::atomic_bool asynchronous_{};
        bool moving_{}, started_{}, ended_{};
    };
}
