#include "Blink/App/FileDragDropController.h"
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.System.Threading.h>
#include <utility>
#include <map>
#include <shobjidl.h>

namespace blink::app
{
    using namespace winrt;
    using namespace Microsoft::UI::Xaml;
    using namespace winrt::Windows::ApplicationModel::DataTransfer;
    using DragDeferral = decltype(std::declval<DragEventArgs>().GetDeferral());
    namespace
    {
        void FinishLater(std::shared_ptr<IncomingFileDrag> lease, const FileDropOutcome outcome) noexcept
        {
            if (!lease) { return; }
            try { (void)winrt::Windows::System::Threading::ThreadPool::RunAsync([lease = std::move(lease), outcome](auto&&) {
                (void)lease->Finish(outcome.error, outcome.logical_effect);
            }); } catch (...) {}
        }
        FileDropModifiers Modifiers(const DragEventArgs& args)
        {
            using Flags = decltype(args.Modifiers());
            const auto keys = static_cast<unsigned>(args.Modifiers());
            const auto has = [keys](Flags flag) { return (keys & static_cast<unsigned>(flag)) != 0; };
            return { has(Flags::Control), has(Flags::Shift), has(Flags::Alt) };
        }
        DWORD Allowed(const DragEventArgs& args) { return static_cast<DWORD>(args.AllowedOperations()) & (DROPEFFECT_COPY | DROPEFFECT_MOVE); }
        std::optional<FileOperationOrigin> ResolveTarget(const FileDragDropController::TargetResolver& resolver) noexcept
        { try { return resolver ? resolver() : std::nullopt; } catch (...) { return {}; } }
        HRESULT DragObject(const DragEventArgs& args, com_ptr<IDataObject>& object) noexcept
        {
            HRESULT error = E_NOINTERFACE;
            try { if (const auto package = args.Data()) { error = QueryFileDragDataObject(get_unknown(package), object); } } catch (...) { error = to_hresult(); }
            if (FAILED(error)) { try { error = QueryFileDragDataObject(get_unknown(args.DataView()), object); } catch (...) { error = to_hresult(); } }
            return error;
        }
    }
    struct FileDragDropController::Core
    {
        std::mutex mutex;
        std::shared_ptr<IncomingFileDrag> lease;
        FileOperationCancellation cancelled;
        std::uint64_t token{}, operation{};
        bool closed{};
        std::function<void(std::uint64_t, FileDropOutcome)> completed;
        void Observe(const FileOperationSnapshot& snapshot)
        {
            std::shared_ptr<IncomingFileDrag> ending; std::optional<FileDropOutcome> result; std::uint64_t id{};
            {
                std::scoped_lock lock(mutex);
                if (closed || !lease || !operation || snapshot.operation_id != operation) { return; }
                result = CompletedFileDrop(snapshot); if (!result) { return; }
                ending = std::move(lease); id = token; operation = 0;
            }
            try { if (completed) { completed(id, *result); } } catch (...) {}
            FinishLater(std::move(ending), *result);
        }
        void Abort(const std::uint64_t expected = 0)
        {
            std::shared_ptr<IncomingFileDrag> ending; std::uint64_t id{};
            { std::scoped_lock lock(mutex); if (expected && expected != token) { return; }
                if (cancelled) { cancelled->store(true); } ending = std::move(lease); id = token; operation = 0; }
            if (!ending) { return; }
            try { if (completed) { completed(id, { E_ABORT, DROPEFFECT_NONE }); } } catch (...) {}
            FinishLater(std::move(ending), { E_ABORT, DROPEFFECT_NONE });
        }
    };
    struct FileDragDropController::Ui
    {
        struct Preview
        {
            FileDragDataReference reference;
            FileOperationCancellation cancelled{ std::make_shared<std::atomic_bool>() };
            std::optional<PreparedFileDrag> source;
            struct Target { HRESULT error{ E_PENDING }; blink::core::FileIdentity identity; };
            std::map<std::wstring, Target> targets;
        };
        Localization localization;
        Callbacks callbacks;
        Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
        std::weak_ptr<Core> core;
        std::optional<FileDragSelection> outgoing;
        FileDragData outgoing_data;
        std::shared_ptr<Preview> preview;
        com_ptr<::IUnknown> preview_identity;
        TargetResolver resolve;
        DragEventArgs over_args{ nullptr }, drop_args{ nullptr };
        DragDeferral over_deferral{ nullptr }, drop_deferral{ nullptr };
        std::uint64_t incoming_token{}, next_token{};
        DWORD drop_effect{};
        bool closed{};
        void EndOver() noexcept
        {
            if (over_deferral) { try { over_deferral.Complete(); } catch (...) {} }
            over_deferral = nullptr; over_args = nullptr; resolve = {};
        }
        void EndDrop(const FileDropOutcome outcome) noexcept
        {
            if (drop_deferral)
            {
                try
                {
                    // Optimized Move: Blink removes sources itself. Returning
                    // None prevents an external source from deleting them again.
                    drop_args.AcceptedOperation(SUCCEEDED(outcome.error) && drop_effect == DROPEFFECT_COPY && outcome.logical_effect ?
                        DataPackageOperation::Copy : DataPackageOperation::None);
                    drop_deferral.Complete();
                }
                catch (...) {}
            }
            drop_deferral = nullptr; drop_args = nullptr;
        }
    };
    FileDragDropController::~FileDragDropController() noexcept { Close(); }
    void FileDragDropController::Initialize(Localization localization, Callbacks callbacks)
    {
        Close(); core_ = std::make_shared<Core>(); ui_ = std::make_shared<Ui>();
        ui_->localization = std::move(localization); ui_->callbacks = std::move(callbacks);
        ui_->dispatcher = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread(); ui_->core = core_;
        const std::weak_ptr<Ui> weak = ui_; const auto dispatcher = ui_->dispatcher;
        core_->completed = [weak, dispatcher](const std::uint64_t token, FileDropOutcome outcome) {
            (void)dispatcher.TryEnqueue([weak, token, outcome] {
                if (const auto ui = weak.lock(); ui && !ui->closed && ui->incoming_token == token)
                { ui->EndDrop(outcome); ui->incoming_token = 0; if (ui->callbacks.changed) { ui->callbacks.changed(); } }
            });
        };
    }
    HRESULT FileDragDropController::StartSource(const DataPackage& package, const FileOperationOrigin& origin,
        const std::span<const blink::core::FileEntry> entries)
    {
        if (!ui_ || ui_->closed) { return RO_E_CLOSED; }
        FileDragSelection selected; const auto captured = CaptureFileDragSelection(origin, entries, selected); if (captured) { return HRESULT_FROM_WIN32(captured); }
        FileDragData data; auto error = CreateFileDragData(selected.paths, selected.token, data); if (FAILED(error)) { return error; }
        const std::weak_ptr<Ui> weak = ui_; const auto parent = selected.parent_identity;
        const auto refreshed = std::make_shared<std::atomic_bool>(); const auto dispatcher = ui_->dispatcher;
        const std::weak_ptr<FileDragFeedback> feedback = data.feedback;
        data.feedback->changed = [weak, parent, refreshed, dispatcher, feedback](const DWORD effect) {
            if (const auto state = feedback.lock(); state && state->internal_receiver.load()) { return; }
            if (effect != DROPEFFECT_MOVE || refreshed->exchange(true)) { return; }
            (void)dispatcher.TryEnqueue([weak, parent] {
                if (const auto ui = weak.lock(); ui && !ui->closed && ui->callbacks.refresh_source) { ui->callbacks.refresh_source(parent); }
            });
        };
        try
        {
            const auto provider = package.as<IDataObjectProvider>(); error = provider->SetDataObject(data.object.get());
            if (FAILED(error)) { return error; }
            package.RequestedOperation(DataPackageOperation::Copy | DataPackageOperation::Move);
            ui_->outgoing = std::move(selected); ui_->outgoing_data = std::move(data); return S_OK;
        }
        catch (...) { return to_hresult(); }
    }
    void FileDragDropController::ClearPreview(const std::shared_ptr<Ui>& ui) noexcept
    {
        if (!ui) { return; } ui->EndOver();
        if (ui->preview) { ui->preview->cancelled->store(true); } ui->preview.reset(); ui->preview_identity = nullptr;
    }
    void FileDragDropController::EndSource() noexcept
    { if (ui_) { ui_->outgoing.reset(); ui_->outgoing_data = {}; ClearPreview(ui_); } }
    void FileDragDropController::UpdatePending(const std::shared_ptr<Ui>& ui)
    {
        if (!ui || ui->closed || !ui->over_args || !ui->preview) { return; }
        const auto preview = ui->preview;
        const auto reject = [&](const wchar_t* key) {
            ui->over_args.AcceptedOperation(DataPackageOperation::None); ui->over_args.DragUIOverride().Caption(hstring(ui->localization.GetString(key))); ui->EndOver();
        };
        if (ui->callbacks.busy && ui->callbacks.busy()) { reject(L"DragDrop.Busy"); return; }
        const auto target = ResolveTarget(ui->resolve);
        if (!target || !target->location.IsFolder() || (ui->callbacks.preview_current && !ui->callbacks.preview_current(*target)))
        { reject(L"DragDrop.InvalidTarget"); return; }
        if (!preview->source) { ui->over_args.DragUIOverride().Caption(hstring(ui->localization.GetString(L"DragDrop.Preparing"))); return; }
        if (FAILED(preview->source->error)) { reject(L"DragDrop.Unsupported"); return; }
        const auto path = target->location.FolderPath();
        const auto found = preview->targets.find(path);
        if (found == preview->targets.end())
        {
            preview->targets.emplace(path, Ui::Preview::Target{});
            const std::weak_ptr<Ui> weak = ui; const auto dispatcher = ui->dispatcher; const auto expected = target->identity;
            (void)winrt::Windows::System::Threading::ThreadPool::RunAsync([weak, dispatcher, preview, path, expected](auto&&) {
                Ui::Preview::Target result; result.error = ReadFileDropTarget(path, result.identity);
                if (SUCCEEDED(result.error) && expected.valid && expected != result.identity) { result.error = HRESULT_FROM_WIN32(ERROR_FILE_INVALID); }
                (void)dispatcher.TryEnqueue([weak, preview, path, result] {
                    if (const auto live = weak.lock(); live && !live->closed && live->preview == preview)
                    { preview->targets[path] = result; UpdatePending(live); }
                });
            });
            return;
        }
        if (found->second.error == E_PENDING) { return; }
        if (FAILED(found->second.error) || (target->identity.valid && target->identity != found->second.identity))
        { reject(L"DragDrop.InvalidTarget"); return; }
        const auto identity = found->second.identity;
        const auto effect = ChooseFileDropEffect(preview->source->sources, target->location.FolderPath(), identity, Allowed(ui->over_args), Modifiers(ui->over_args));
        ui->over_args.AcceptedOperation(static_cast<DataPackageOperation>(effect));
        ui->over_args.DragUIOverride().Caption(hstring(effect == DROPEFFECT_NONE ? ui->localization.GetString(L"DragDrop.NoOperation") :
            ui->localization.Format(effect == DROPEFFECT_MOVE ? L"DragDrop.MoveTo" : L"DragDrop.CopyTo", target->location.FolderPath())));
        ui->EndOver();
    }
    void FileDragDropController::Over(const DragEventArgs& args, TargetResolver target)
    {
        args.Handled(true); args.AcceptedOperation(DataPackageOperation::None);
        if (!ui_ || ui_->closed || Pending()) { return; }
        auto ui = ui_;
        if (!ResolveTarget(target) || (ui->callbacks.busy && ui->callbacks.busy())) { ui->EndOver(); return; }
        try
        {
            com_ptr<IDataObject> object; const auto captured = DragObject(args, object);
            if (FAILED(captured)) { ui->EndOver(); return; }
            const auto identity = object.as<::IUnknown>();
            if (ui->preview && identity != ui->preview_identity) { ClearPreview(ui); }
            // Retain exactly the latest deferral while source/target metadata
            // is prepared. Repeated DragOver never starts duplicate source IO.
            auto deferral = args.GetDeferral(); ui->EndOver(); ui->over_deferral = std::move(deferral); ui->over_args = args; ui->resolve = std::move(target);
            if (!ui->preview)
            {
                auto preview = std::make_shared<Ui::Preview>();
                const auto error = preview->reference.Capture(object.get());
                if (FAILED(error)) { ui->EndOver(); return; }
                ui->preview = preview; ui->preview_identity = identity;
                const auto own = ui->outgoing; const auto dispatcher = ui->dispatcher; const std::weak_ptr<Ui> weak = ui;
                (void)winrt::Windows::System::Threading::ThreadPool::RunAsync([weak, dispatcher, preview, own](auto&&) {
                    auto source = PrepareFileDrag(preview->reference, own, preview->cancelled);
                    (void)dispatcher.TryEnqueue([weak, preview, source = std::move(source)]() mutable {
                        if (const auto live = weak.lock(); live && !live->closed && live->preview == preview)
                        { preview->source = std::move(source); UpdatePending(live); }
                    });
                });
            }
            UpdatePending(ui);
        }
        catch (...) { ui->EndOver(); }
    }
    void FileDragDropController::Drop(const DragEventArgs& args, TargetResolver resolver)
    {
        args.Handled(true); args.AcceptedOperation(DataPackageOperation::None);
        if (!ui_ || ui_->closed || Pending()) { return; }
        const auto ui = ui_; const auto preview = ui->preview; const auto target = ResolveTarget(resolver);
        if (!preview || !preview->source || FAILED(preview->source->error) || !target || !target->location.IsFolder() ||
            (ui->callbacks.busy && ui->callbacks.busy()) || (ui->callbacks.current && !ui->callbacks.current(*target))) { return; }
        com_ptr<IDataObject> object;
        if (FAILED(DragObject(args, object)) || object.as<::IUnknown>() != ui->preview_identity)
        { ClearPreview(ui); return; }
        auto origin = *target;
        const auto found = preview->targets.find(origin.location.FolderPath());
        if (found == preview->targets.end() || FAILED(found->second.error) ||
            (origin.identity.valid && origin.identity != found->second.identity)) { return; }
        origin.identity = found->second.identity;
        const auto effect = ChooseFileDropEffect(preview->source->sources, origin.location.FolderPath(), origin.identity, Allowed(args), Modifiers(args));
        if (!effect) { return; }
        try
        {
            ui->EndOver(); ui->drop_args = args; ui->drop_deferral = args.GetDeferral(); ui->drop_effect = effect;
            if (ui->outgoing && ui->outgoing->token == preview->source->payload.token && ui->outgoing_data.feedback)
            { ui->outgoing_data.feedback->internal_receiver.store(true); }
            ui->incoming_token = ++ui->next_token;
            const auto lease = std::make_shared<IncomingFileDrag>(preview->reference, effect == DROPEFFECT_MOVE);
            const auto cancelled = std::make_shared<std::atomic_bool>(); const auto core = core_; const auto token = ui->incoming_token;
            { std::scoped_lock lock(core->mutex); core->lease = lease; core->cancelled = cancelled; core->token = token; core->operation = 0; }
            if (ui->callbacks.changed) { ui->callbacks.changed(); }
            const std::weak_ptr<Ui> weak = ui; const auto dispatcher = ui->dispatcher;
            const auto source = preview->source->sources;
            (void)winrt::Windows::System::Threading::ThreadPool::RunAsync([weak, dispatcher, core, lease, cancelled, token, origin, source, effect](auto&&) {
                auto error = lease->Begin(); if (cancelled->load()) { error = E_ABORT; }
                const bool posted = dispatcher.TryEnqueue([weak, core, lease, cancelled, token, origin, source, effect, error]() mutable {
                    const auto live = weak.lock();
                    if (!live || live->closed || live->incoming_token != token || cancelled->load() || FAILED(error) ||
                        (live->callbacks.current && !live->callbacks.current(origin)) || (live->callbacks.busy && live->callbacks.busy()))
                    { core->Abort(token); return; }
                    const auto id = live->callbacks.submit ? live->callbacks.submit(origin, source, effect) : 0;
                    if (!id) { core->Abort(token); return; }
                    { std::scoped_lock lock(core->mutex); if (core->lease == lease) { core->operation = id; } }
                    if (lease->Asynchronous())
                    { live->EndDrop({ S_OK, effect }); }
                    if (live->callbacks.snapshot) { core->Observe(live->callbacks.snapshot()); }
                    if (live->callbacks.changed) { live->callbacks.changed(); }
                });
                if (!posted) { core->Abort(token); }
            });
        }
        catch (...) { ui->EndDrop({ E_FAIL, 0 }); if (core_) { core_->Abort(); } if (ui->callbacks.error) { ui->callbacks.error(to_hresult()); } }
    }
    void FileDragDropController::Leave(const DragEventArgs& args, const UIElement& root)
    {
        if (!ui_ || !root) { return; }
        const auto point = args.GetPosition(root); const auto element = root.try_as<FrameworkElement>();
        if (!element || point.X < 0 || point.Y < 0 || point.X > element.ActualWidth() || point.Y > element.ActualHeight()) { ClearPreview(ui_); }
    }
    FileOperationCoordinator::PublishCallback FileDragDropController::Publication(FileOperationCoordinator::PublishCallback base) const
    {
        const auto core = core_;
        return [base = std::move(base), core](const FileOperationSnapshot& snapshot) {
            try { if (base) { base(snapshot); } }
            catch (...)
            {
                if (snapshot.directory_conflict)
                {
                    if (core) { core->Observe(snapshot); }
                    throw;
                }
            }
            if (core) { core->Observe(snapshot); }
        };
    }
    void FileDragDropController::Advance(const std::uint64_t previous, const std::uint64_t next, const FileOperationSnapshot& snapshot)
    {
        if (!core_ || !next) { return; }
        { std::scoped_lock lock(core_->mutex); if (core_->lease && core_->operation == previous) { core_->operation = next; } }
        core_->Observe(snapshot);
    }
    bool FileDragDropController::Pending() const noexcept
    { if (!core_) { return false; } std::scoped_lock lock(core_->mutex); return static_cast<bool>(core_->lease); }
    bool FileDragDropController::Preparing() const noexcept
    { if (!core_) { return false; } std::scoped_lock lock(core_->mutex); return core_->lease && !core_->operation; }
    void FileDragDropController::CancelPreparation() noexcept
    { if (Preparing()) { if (ui_) { ui_->EndDrop({ E_ABORT, 0 }); } core_->Abort(); } }
    void FileDragDropController::Close() noexcept
    {
        if (ui_) { ui_->closed = true; ClearPreview(ui_); ui_->EndDrop({ E_ABORT, 0 }); ui_->outgoing.reset(); ui_->outgoing_data = {}; }
        if (core_) { { std::scoped_lock lock(core_->mutex); core_->closed = true; } core_->Abort(); }
        ui_.reset(); core_.reset();
    }
}
