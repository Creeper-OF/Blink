#pragma once
#include "Blink/App/FileDragDropModel.h"
#include "Blink/App/Localization.h"
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>

namespace blink::app
{
    class FileDragDropController
    {
    public:
        using TargetResolver = std::function<std::optional<FileOperationOrigin>()>;
        struct Callbacks
        {
            std::function<bool()> busy;
            std::function<bool(const FileOperationOrigin&)> preview_current, current;
            std::function<std::uint64_t(const FileOperationOrigin&, const ClipboardCopySources&, DWORD)> submit;
            std::function<FileOperationSnapshot()> snapshot;
            std::function<void(HRESULT)> error;
            std::function<void()> changed;
            std::function<void(const blink::core::FileIdentity&)> refresh_source;
        };
        ~FileDragDropController() noexcept;
        void Initialize(Localization localization, Callbacks callbacks);
        [[nodiscard]] HRESULT StartSource(const winrt::Windows::ApplicationModel::DataTransfer::DataPackage& package,
            const FileOperationOrigin& origin, std::span<const blink::core::FileEntry> entries);
        void EndSource() noexcept;
        void Over(const winrt::Microsoft::UI::Xaml::DragEventArgs& args, TargetResolver target);
        void Drop(const winrt::Microsoft::UI::Xaml::DragEventArgs& args, TargetResolver target);
        void Leave(const winrt::Microsoft::UI::Xaml::DragEventArgs& args, const winrt::Microsoft::UI::Xaml::UIElement& root);
        [[nodiscard]] FileOperationCoordinator::PublishCallback Publication(FileOperationCoordinator::PublishCallback base) const;
        void Advance(std::uint64_t previous, std::uint64_t next, const FileOperationSnapshot& snapshot);
        [[nodiscard]] bool Pending() const noexcept;
        [[nodiscard]] bool Preparing() const noexcept;
        void CancelPreparation() noexcept;
        void Close() noexcept;
    private:
        struct Ui;
        struct Core;
        static void UpdatePending(const std::shared_ptr<Ui>& ui);
        static void ClearPreview(const std::shared_ptr<Ui>& ui) noexcept;
        std::shared_ptr<Ui> ui_;
        std::shared_ptr<Core> core_;
    };
}
