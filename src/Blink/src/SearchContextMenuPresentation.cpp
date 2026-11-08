#include "Blink/App/SearchContextMenuPresentation.h"
#include "Blink/App/ContextMenuAppearance.h"
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <utility>

namespace blink::app
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;

    struct SearchContextMenuPresentation::Ui
    {
        enum class Action { Command, Properties, SystemOptions };
        MenuFlyout menu{ nullptr };
        Callbacks callbacks;
        SearchContextSelection selection;
        Windows::Foundation::Point client_position{};
        std::uint64_t serial{};
        bool closed{ true };

        bool Current(const std::uint64_t value) const noexcept
        { return !closed && serial == value; }

        void Invalidate() noexcept
        {
            closed = true;
            ++serial;
            callbacks = {};
            auto previous = std::exchange(menu, nullptr);
            if (previous) { try { previous.Hide(); } catch (...) {} }
        }

        void Select(const std::uint64_t value, const Action action, const ShellMenuCommand command)
        {
            if (!Current(value)) { return; }
            auto selected = selection;
            auto invoke = callbacks;
            const auto position = client_position;
            // Hide can raise Closed synchronously or later. Invalidate first,
            // then call the owner, which may immediately show a native helper.
            Invalidate();
            switch (action)
            {
            case Action::Command:
                if (invoke.command) { invoke.command(std::move(selected), command); }
                break;
            case Action::Properties:
                if (invoke.properties) { invoke.properties(std::move(selected)); }
                break;
            case Action::SystemOptions:
                if (invoke.system_options) { invoke.system_options(std::move(selected), position); }
                break;
            }
        }

        void Dismissed(const std::uint64_t value)
        {
            if (!Current(value)) { return; }
            auto notify = std::move(callbacks.closed);
            closed = true;
            ++serial;
            callbacks = {};
            menu = nullptr;
            // State is already retired if the owner reenters Hide or Show.
            if (notify) { notify(); }
        }
    };

    SearchContextMenuPresentation::~SearchContextMenuPresentation() noexcept { Hide(); }

    bool SearchContextMenuPresentation::Show(const FrameworkElement& anchor,
        const Windows::Foundation::Point client_position, SearchContextSelection selection,
        const Localization& localization, const bool mutation_busy, const bool can_paste, Callbacks callbacks)
    {
        Hide();
        if (!anchor || !anchor.XamlRoot()) { return false; }
        const auto from_client = anchor.TransformToVisual(nullptr).Inverse();
        if (!from_client) { return false; }
        const auto local_position = from_client.TransformPoint(client_position);
        if (!ui_) { ui_ = std::make_shared<Ui>(); }
        const auto ui = ui_;
        const auto epoch = ++ui->serial;
        ui->closed = false;
        ui->selection = std::move(selection);
        ui->client_position = client_position;
        ui->callbacks = std::move(callbacks);
        ui->menu = MenuFlyout();

        try
        {
            ApplyContextMenuAppearance(ui->menu, anchor, L"SearchContextMenu");

            const std::weak_ptr<Ui> weak = ui;
            const auto add = [&](const wchar_t* key, const wchar_t* id, const wchar_t* glyph,
                const wchar_t* shortcut, const Ui::Action action, const ShellMenuCommand command, const bool enabled) {
                MenuFlyoutItem item;
                item.Text(hstring(localization.GetString(key)));
                StyleContextMenuItem(item, glyph, shortcut);
                const bool has_callback = action == Ui::Action::Command ? static_cast<bool>(ui->callbacks.command) :
                    action == Ui::Action::Properties ? static_cast<bool>(ui->callbacks.properties) :
                    static_cast<bool>(ui->callbacks.system_options);
                item.IsEnabled(enabled && has_callback);
                Automation::AutomationProperties::SetAutomationId(item, id);
                item.Click([weak, epoch, action, command](auto&&, auto&&) {
                    if (const auto live = weak.lock()) { live->Select(epoch, action, command); }
                });
                ui->menu.Items().Append(item);
            };
            const auto separator = [&] { ui->menu.Items().Append(MenuFlyoutSeparator()); };
            const bool writable = !mutation_busy;
            using Action = Ui::Action;

            add(L"Command.Open", L"SearchMenuOpen", L"\xE8E5", L"Enter", Action::Command, ShellMenuCommand::Open, true);
            separator();
            add(L"Command.Cut", L"SearchMenuCut", L"\xE8C6", L"Ctrl+X", Action::Command, ShellMenuCommand::Cut, writable);
            add(L"Command.Copy", L"SearchMenuCopy", L"\xE8C8", L"Ctrl+C", Action::Command, ShellMenuCommand::Copy, writable);
            add(L"Search.MenuPasteParent", L"SearchMenuPasteParent", L"\xE77F", L"Ctrl+V", Action::Command, ShellMenuCommand::PasteParent, writable && can_paste);
            separator();
            add(L"Search.MenuCopyPath", L"SearchMenuCopyPath", L"\xE71B", L"", Action::Command, ShellMenuCommand::CopyFullPath, writable);
            add(L"Search.MenuCopyParentPath", L"SearchMenuCopyParentPath", L"\xE8B7", L"", Action::Command, ShellMenuCommand::CopyParentPath, writable);
            add(L"Search.Locate", L"SearchMenuLocate", L"\xE81D", L"Ctrl+Enter", Action::Command, ShellMenuCommand::Locate, true);
            separator();
            add(L"Command.Rename", L"SearchMenuRename", L"\xE8AC", L"F2", Action::Command, ShellMenuCommand::Rename, writable);
            add(L"Command.Delete", L"SearchMenuDelete", L"\xE74D", L"Delete", Action::Command, ShellMenuCommand::Delete, writable);
            add(L"Command.Properties", L"SearchMenuProperties", L"\xE946", L"Alt+Enter", Action::Properties, ShellMenuCommand::None, true);
            separator();
            add(L"Search.MenuSystemOptions", L"SearchMenuSystemOptions", L"\xE712", L"", Action::SystemOptions, ShellMenuCommand::None, true);

            ui->menu.Closed([weak, epoch](auto&&, auto&&) {
                if (const auto live = weak.lock()) { live->Dismissed(epoch); }
            });
            Primitives::FlyoutShowOptions options;
            options.Position(local_position);
            ui->menu.ShowAt(anchor, options);
            return true;
        }
        catch (...)
        {
            ui->Invalidate();
            throw;
        }
    }

    void SearchContextMenuPresentation::Hide() noexcept
    {
        if (ui_) { ui_->Invalidate(); }
    }
}
