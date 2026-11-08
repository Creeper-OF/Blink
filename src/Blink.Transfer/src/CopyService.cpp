#include "Blink/Transfer/CopyService.h"

#include <winbase.h>

#include <utility>

#pragma comment(lib, "Kernel32.lib")

namespace blink::transfer
{
    void CopyControl::Cancel() noexcept
    {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&cancel_flag), TRUE);
    }

    void CopyControl::Pause() noexcept
    {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(&pause_flag), TRUE);
    }

    bool CopyControl::IsCanceled() const noexcept
    {
        return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(const_cast<BOOL*>(&cancel_flag)), 0, 0) != FALSE;
    }

    bool CopyControl::IsPaused() const noexcept
    {
        return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(const_cast<BOOL*>(&pause_flag)), 0, 0) != FALSE;
    }

    namespace
    {
        COPYFILE2_MESSAGE_ACTION CALLBACK ProgressRoutine(
            const COPYFILE2_MESSAGE* message,
            PVOID context)
        {
            auto* control = static_cast<CopyControl*>(context);
            if (control == nullptr)
            {
                return COPYFILE2_PROGRESS_CONTINUE;
            }
            if (control->IsCanceled())
            {
                return COPYFILE2_PROGRESS_CANCEL;
            }
            if (control->IsPaused())
            {
                return COPYFILE2_PROGRESS_PAUSE;
            }

            if (message != nullptr && control->progress_callback)
            {
                CopyProgress progress;
                switch (message->Type)
                {
                case COPYFILE2_CALLBACK_CHUNK_FINISHED:
                    progress.total_bytes = message->Info.ChunkFinished.uliTotalFileSize.QuadPart;
                    progress.transferred_bytes = message->Info.ChunkFinished.uliTotalBytesTransferred.QuadPart;
                    break;
                case COPYFILE2_CALLBACK_STREAM_FINISHED:
                    progress.total_bytes = message->Info.StreamFinished.uliTotalFileSize.QuadPart;
                    progress.transferred_bytes = message->Info.StreamFinished.uliTotalBytesTransferred.QuadPart;
                    break;
                default:
                    return COPYFILE2_PROGRESS_CONTINUE;
                }
                control->progress_callback(progress);
            }
            return COPYFILE2_PROGRESS_CONTINUE;
        }
    }

    CopyTask CopyFileAsync(
        std::wstring source,
        std::wstring destination,
        std::function<void(const CopyProgress&)> progress_callback)
    {
        auto control = std::make_shared<CopyControl>();
        control->progress_callback = std::move(progress_callback);
        CopyTask task;
        task.control = control;
        task.completion = std::async(std::launch::async, [control, source = std::move(source), destination = std::move(destination)] {
            COPYFILE2_EXTENDED_PARAMETERS parameters{};
            parameters.dwSize = sizeof(parameters);
            parameters.dwCopyFlags = COPY_FILE_RESTARTABLE;
            parameters.pfCancel = &control->cancel_flag;
            parameters.pProgressRoutine = ProgressRoutine;
            parameters.pvCallbackContext = control.get();

            CopyResult result;
            result.status = CopyFile2(source.c_str(), destination.c_str(), &parameters);
            result.canceled = control->IsCanceled();
            result.paused = control->IsPaused() || result.status == HRESULT_FROM_WIN32(ERROR_REQUEST_PAUSED);
            result.partial_file_left = result.paused;
            return result;
        });
        return task;
    }
}
