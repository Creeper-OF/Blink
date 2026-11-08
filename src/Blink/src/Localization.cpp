#include "Blink/App/Localization.h"

#if !defined(BLINK_LOCALIZATION_DISABLE_RESOURCE_LOADER)
#include <Windows.h>
#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>
#endif

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <utility>

namespace blink::app
{
    namespace
    {
        [[nodiscard]] std::wstring ReplaceValue(
            std::wstring value,
            const std::wstring_view replacement)
        {
            constexpr std::wstring_view placeholder = L"{0}";
            const auto position = value.find(placeholder);
            if (position != std::wstring::npos)
            {
                value.replace(position, placeholder.size(), replacement);
            }
            return value;
        }

        [[nodiscard]] std::wstring ReplaceValues(
            std::wstring value,
            const std::vector<std::wstring>& values)
        {
            std::wstring result;
            std::size_t offset{};
            while (offset < value.size())
            {
                const auto position = value.find(L'{', offset);
                if (position == std::wstring::npos)
                {
                    result.append(value, offset, std::wstring::npos);
                    break;
                }
                result.append(value, offset, position - offset);
                offset = position;
                bool replaced = false;
                for (std::size_t index = 0; index < values.size(); ++index)
                {
                    const auto placeholder = L"{" + std::to_wstring(index) + L"}";
                    if (value.compare(offset, placeholder.size(), placeholder) == 0)
                    {
                        // Parse only the resource template. A path containing
                        // literal {1} must not be reinterpreted as a parameter.
                        result.append(values[index]);
                        offset += placeholder.size();
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) { result.push_back(value[offset++]); }
            }
            return result;
        }

        [[nodiscard]] std::wstring FallbackString(const std::wstring_view key)
        {
            // This is only a safe fallback for test/degraded environments in
            // which the generated PRI is unavailable. Normal packaged-resource
            // lookup remains the source of the active language.
            static const std::unordered_map<std::wstring, std::wstring> values{
                { L"DragDrop.Busy", L"Another file operation is in progress." },
                { L"DragDrop.InvalidTarget", L"Drop onto an available folder or drive." },
                { L"DragDrop.Preparing", L"Preparing file transfer…" },
                { L"DragDrop.Unsupported", L"These items cannot be transferred." },
                { L"DragDrop.NoOperation", L"This location or key combination does not support this drop." },
                { L"DragDrop.MoveTo", L"Move to {0}" },
                { L"DragDrop.CopyTo", L"Copy to {0}" },
                { L"DragDrop.SourceUnavailable", L"The selection could not be dragged. Refresh the folder and try again." },
                { L"DragDrop.Failed", L"Could not transfer these items: {0}" },
                { L"DirectoryCopy.Skipped", L"Folder copy skipped: {0}" },
                { L"DirectoryCopyConflict.Title", L"Folder name already exists" },
                { L"DirectoryCopyConflict.Description", L"Merge into the existing folder, or choose Skip, Keep both, or Cancel. Conflicting files are handled individually." },
                { L"DirectoryCopyConflict.Merge", L"Merge" },
                { L"DirectoryCopyConflict.ReplaceUnavailable", L"This item cannot be merged with the destination. Choose Skip or Keep both." },
                { L"DirectoryCopyConflict.KeepBothDescription", L"Copy to the first available numbered folder name. If a new tree was already created, a failure keeps that partial tree for inspection." },
                { L"Clipboard.CopiedItems", L"Copied {0} items to the clipboard." },
                { L"DirectoryCopy.Name", L"New destination folder name" },
                { L"DirectoryCopy.Scanning", L"Checking folder contents: {0}" },
                { L"DirectoryCopy.Running", L"Copying folder to: {0}" },
                { L"DirectoryCopy.Completed", L"Folder copied to: {0}" },
                { L"DirectoryCopy.Cancelled", L"Folder copy cancelled: {0}" },
                { L"DirectoryCopy.Cancelling", L"Cancelling folder copy: {0}" },
                { L"DirectoryCopy.Failed", L"Could not copy folder to: {0}" },
                { L"DirectoryCopy.WaitingForConflict", L"Waiting for a choice for: {0}" },
                { L"DirectoryCopy.SkippedCounts", L"{0} files and {1} folders skipped." },
                { L"DirectoryCopy.Counts", L"{0} files copied; {1} folders created." },
                { L"DirectoryCopy.FailurePath", L"Failed at: {0}" },
                { L"DirectoryCopy.PartialHistory", L"{0} attempts left incomplete folder trees. Earlier copies remain; inspect paths in the tooltip." },
                { L"DirectoryCopy.HistoryLimited", L"Only the first 32 paths within the display limit are shown; the count includes all incomplete attempts." },
                { L"DirectoryCopy.RetryNewName", L"Enter a new folder name to start another copy. Earlier incomplete copies remain at their reported paths." },
                { L"DirectoryCopy.PartialPath", L"The earlier incomplete folder remains at: {0}" },
                { L"Copy.Unsupported", L"This file’s storage attributes or data streams cannot be copied completely yet. The source file was preserved." },
                { L"CopyConflict.PrepareFailed", L"Could not prepare the conflict choices. Try again." },
                { L"CopyConflict.Title", L"File already exists" },
                { L"CopyConflict.Description", L"The destination already contains an item with this name. Choose what to do." },
                { L"CopyConflict.Source", L"Source: {0}" },
                { L"CopyConflict.Destination", L"Destination: {0}" },
                { L"CopyConflict.Replace", L"Replace" },
                { L"CopyConflict.Skip", L"Skip" },
                { L"CopyConflict.KeepBoth", L"Keep both" },
                { L"CopyConflict.Cancel", L"Cancel" },
                { L"CopyConflict.ReplaceUnavailable", L"This item cannot replace the destination. Choose Skip or Keep both." },
                { L"CopyConflict.KeepBothDescription", L"Keep the existing item and add a number to the copy’s name." },
                { L"Operation.CopySkipped", L"Skipped: {0}" },

                { L"Delete.ConfirmTitle", L"Permanently delete these items?" },
                { L"Delete.ConfirmPermanent", L"Permanently delete" },
                { L"Delete.ConfirmDescription", L"Permanently delete {0} selected items, including folder contents? They will not go to the Recycle Bin." },
                { L"Delete.RecycleRunning", L"Moving selected items to the Recycle Bin." },
                { L"Delete.PermanentRunning", L"Permanently deleting selected items." },
                { L"Delete.Completed", L"Delete queue completed." },
                { L"Delete.Cancelled", L"Delete queue cancelled. Completed removals remain in effect." },
                { L"Delete.Cancelling", L"Cancelling the delete queue." },
                { L"Delete.Failed", L"Delete queue stopped." },
                { L"Delete.Summary", L"{0} items: {1} recycled, {2} permanently deleted, {3} skipped, {4} failed, {5} cancelled, {6} remaining." },
                { L"Delete.Current", L"Item {0}: {1}" },
                { L"Delete.Progress", L"System progress: {0}/{1}." },
                { L"Delete.Partial", L"{0} folder items may be partly removed. Check their remaining contents before retrying or skipping." },
                { L"Delete.Warning", L"{0} removals completed with a follow-up warning. Review their paths in the tooltip." },
                { L"Delete.RecycleUnavailable", L"The system Recycle Bin cannot be used here. No permanent deletion was requested." },
                { L"Delete.TargetChanged", L"The selected item or parent folder changed. Refresh and try again." },
                { L"Delete.Retry", L"Retry current item" },
                { L"Delete.Skip", L"Skip current item" },
                { L"Delete.DialogFailed", L"The permanent-delete confirmation could not be shown." },
                { L"Delete.CaptureFailed", L"The selection could not be read. Refresh the folder or select no more than 1024 items." },
                { L"Properties.Name", L"Name" },
                { L"Properties.Location", L"Location" },
                { L"Properties.Selection", L"Selection" },
                { L"Properties.SelectionValue", L"{0} items: {1} files, {2} folders" },
                { L"Properties.Type", L"Type" },
                { L"Properties.Size", L"Size" },
                { L"Properties.Allocated", L"Size on disk" },
                { L"Properties.Contains", L"Contents" },
                { L"Properties.ContainsValue", L"{0} files, {1} subfolders" },
                { L"Properties.Created", L"Created" },
                { L"Properties.Modified", L"Modified" },
                { L"Properties.Accessed", L"Accessed" },
                { L"Properties.Attributes", L"Attributes" },
                { L"Properties.CommonAttributes", L"Common attributes" },
                { L"Properties.ReadOnly", L"Read-only" },
                { L"Properties.Hidden", L"Hidden" },
                { L"Properties.System", L"System" },
                { L"Properties.Archive", L"Archive" },
                { L"Properties.Compressed", L"Compressed" },
                { L"Properties.Encrypted", L"Encrypted" },
                { L"Properties.Sparse", L"Sparse" },
                { L"Properties.Link", L"Link" },
                { L"Properties.Offline", L"Offline" },
                { L"Properties.None", L"None" },
                { L"Properties.Unavailable", L"Unavailable" },
                { L"Properties.MultipleTypes", L"Multiple types" },
                { L"Properties.MultipleValues", L"Multiple values" },
                { L"Properties.Bytes", L"{0} ({1} bytes)" },
                { L"Properties.Counted", L"Counted so far: {0}" },
                { L"Properties.Reading", L"Reading properties and calculating folder contents…" },
                { L"Properties.Cancelled", L"Calculation cancelled. Counts may be incomplete." },
                { L"Properties.Partial", L"Some properties could not be read. Counts are incomplete." },
                { L"Properties.Complete", L"Calculation complete." },
                { L"Properties.UnavailableCount", L"{0} locations could not be read." },
                { L"Properties.Close", L"Close" },
                { L"Properties.CaptureFailed", L"The selection could not be read. Refresh the folder or select no more than 1024 items." },
                { L"Properties.ReadFailed", L"The properties window could not be opened. Refresh the folder and try again." },
                { L"Properties.FolderNote", L"Folder totals include descendants. Link targets are excluded; hardlink names are counted separately." },
                { L"Move.DirectoryProgress", L"Transferred {0} files and created {1} folders; removed {2} source files and {3} source folders." },
                { L"Move.MergeSkipped", L"Skipped {0} files and {1} folders; items not moved remain at the source." },
                { L"Move.Partial", L"{0} items were not fully moved. Destination copies and remaining sources are kept." },
                { L"Move.SkipRemaining", L"Skip this item and continue" },
                { L"Move.Title", L"Move" },
                { L"Move.Name", L"Destination name" },
                { L"Move.Running", L"Moving items." },
                { L"Move.Completed", L"Move queue completed." },
                { L"Move.Unchanged", L"The selected items are already in this folder." },
                { L"Move.Skipped", L"Move queue skipped." },
                { L"Move.Cancelled", L"Move queue cancelled. Completed moves remain in their destinations." },
                { L"Move.Cancelling", L"Cancelling the move queue." },
                { L"Move.Failed", L"Move queue stopped." },
                { L"Move.Summary", L"{0} items: {1} moved, {2} unchanged, {3} skipped, {4} failed, {5} cancelled, {6} remaining." },
                { L"Move.Current", L"Item {0}: {1}" },
                { L"Move.Warning", L"{0} items moved with a follow-up check warning. Review their paths in the tooltip." },
                { L"Move.CrossVolume", L"The item could not be moved between these volumes." },
                { L"Move.Unsupported", L"This item or storage type cannot be moved yet." },
                { L"Move.TargetChanged", L"The source item or destination folder changed. Refresh and try again." },
                { L"Move.AlreadySubmitted", L"This cut selection has already been submitted. Finish or cancel its move queue, then cut again for another move." },
                { L"MoveConflict.Title", L"Move destination already exists" },
                { L"MoveConflict.Description", L"An item with this name already exists. Choose an available option: replace a file, merge folders, skip, keep both, or cancel." },
                { L"MoveConflict.KeepBothDescription", L"Keep the existing item and move the source under a numbered name." },
                { L"Clipboard.CutItems", L"{0} items ready to move. Paste into a destination folder to move them." },
                { L"Copy.Name", L"Destination file name" },
                { L"Copy.TargetChanged", L"The source item or destination folder changed. Refresh and copy again." },
                { L"Clipboard.CopyLimited", L"Transfer ordinary local files and folders. Links, virtual items and special storage are not supported." },
                { L"Clipboard.Preparing", L"Preparing clipboard items…" },
                { L"Clipboard.Copied", L"Copied to clipboard: {0}" },
                { L"Clipboard.TooManyFiles", L"You can transfer up to 1,024 items at a time." },
                { L"Clipboard.CopiedMany", L"Copied {0} files to the clipboard." },
                { L"Clipboard.Cancelling", L"Cancelling item preparation. Please wait." },
                { L"Clipboard.Cancelled", L"Item preparation cancelled." },
                { L"Operation.CopyBatchRunning", L"Copying files." },
                { L"Operation.CopyBatchCompleted", L"Copy queue completed." },
                { L"Operation.CopyBatchCancelled", L"Copy queue cancelled." },
                { L"Operation.CopyBatchCancelling", L"Cancelling the copy queue." },
                { L"Operation.CopyBatchFailed", L"Copy queue stopped." },
                { L"Operation.CopyBatchSummary", L"{0} items: {1} copied, {2} skipped, {3} failed, {4} cancelled, {5} remaining." },
                { L"Operation.CopyBatchCurrent", L"File {0}: {1}" },
                { L"Operation.CopyBatchPartial", L"{0} items reported incomplete copies. Check their paths in the tooltip." },
                { L"Clipboard.Failed", L"File clipboard operation failed: {0}" },
                { L"Clipboard.Unavailable", L"The file clipboard is unavailable. Try again." },
                { L"Operation.CopyCompleted", L"Copied to: {0}" },
                { L"Operation.CopyCancelled", L"Copy cancelled: {0}" },
                { L"Operation.CopyCancelling", L"Cancelling copy: {0}" },
                { L"Operation.CopyFailed", L"Could not copy to “{0}”: {1}" },
                { L"Operation.CopyRunning", L"Copying to: {0}" },
                { L"Operation.CopyBytes", L" ({0} / {1} bytes)" },
                { L"Operation.CopyPartial", L"An incomplete destination file remains. Inspect it before retrying." },

                { L"NewFolder.Unavailable", L"Creating a folder here is unavailable. Refresh or open the actual local folder." },
                { L"Rename.Name", L"New name" },
                { L"Operation.TargetChanged", L"The selected item changed. Refresh before trying again." },
                { L"Operation.RenameCompleted", L"Renamed to: {0}" },
                { L"Operation.RenameUnchanged", L"Name unchanged: {0}" },
                { L"Operation.RenameCancelled", L"Rename cancelled: {0}" },
                { L"Operation.RenameCancelling", L"Cancelling rename: {0}" },
                { L"Operation.RenameFailed", L"Could not rename to “{0}”: {1}" },
                { L"Operation.RenameRunning", L"Renaming to: {0}" },
                { L"NewFolder.Create", L"Create" },
                { L"NewFolder.Name", L"Folder name" },
                { L"NewFolder.InvalidName", L"Enter a valid name without paths, reserved names, or trailing dots/spaces." },
                { L"NewFolder.DefaultName", L"New folder" },
                { L"NewFolder.Conflict", L"An item with this name already exists. Choose another name." },
                { L"NewFolder.RetryName", L"Retry / change name" },
                { L"NewFolder.DialogFailed", L"Could not start the file operation. Please try again." },
                { L"Operation.Cancel", L"Cancel" },
                { L"Operation.Completed", L"Created folder: {0}" },
                { L"Operation.Cancelled", L"Folder creation cancelled: {0}" },
                { L"Operation.Cancelling", L"Cancelling folder creation: {0}" },
                { L"Operation.Failed", L"Could not create “{0}”: {1}" },
                { L"Operation.Running", L"Creating folder: {0}" },
                { L"Operation.DirectoryChanged", L"The destination folder changed. Refresh before trying again." },
                { L"Operation.Unsupported", L"Creating folders here is not supported yet. Open the actual local folder." },
                { L"Search.Scope", L"Search scope" },
                { L"Search.Advanced", L"Advanced search" },
                { L"Search.FollowLocation", L"Follow current location" },
                { L"Search.SelectedVolumes", L"Selected drives" },
                { L"Search.SelectedVolumesHint", L"For example C:;D: (or comma / space separated)" },
                { L"Search.SelectedFolder", L"Selected folder" },
                { L"Search.FolderHint", L"Enter the full folder path" },
                { L"Search.UseCurrentFolder", L"Use current location" },
                { L"Search.IncludeSubfolders", L"Include subfolders" },
                { L"Search.FolderOnly", L"This folder only" },
                { L"Search.ScopeSummary", L"Scope: {0}" },
                { L"Search.ScopeDraftSummary", L"Scope: {0} · changes apply when you search" },
                { L"Search.ScopeNotSet", L"Choose a search location" },
                { L"Search.AdvancedHint", L"Follow location searches all drives from This PC, or the current drive from a folder. Press Enter or Search to apply changes." },
                { L"Search.CurrentFolderUnavailable", L"The current location is not a folder" },
                { L"Search.Mode", L"Match mode" },
                { L"Search.AllVolumes", L"All local drives" },
                { L"Search.CurrentFolder", L"Current folder" },
                { L"Search.Substring", L"Name contains" },
                { L"Search.Prefix", L"Name starts with" },
                { L"Search.Extension", L"Extension" },
                { L"Search.Submit", L"Search" },
                { L"Search.Retry", L"Retry unfinished volumes" },
                { L"Search.Leave", L"Back to folder" },
                { L"Search.Locate", L"Locate" },
                { L"Search.VolumeStates", L"Volume index states" },
                { L"Search.Queued", L"Search queued. Please wait." },
                { L"Search.Searching", L"Searching for “{0}”; {1} items received." },
                { L"Search.Preparing", L"The index is preparing. Retry “{0}” shortly." },
                { L"Search.Partial", L"“{0}”: showing {1} items; some volumes are unfinished." },
                { L"Search.Unavailable", L"“{0}”: indexed search is unavailable. Folder browsing remains available." },
                { L"Search.Results", L"“{0}”: {1} results." },
                { L"Search.Limit", L"The per-volume limit of {0} results was reached. Narrow the search scope." },
                { L"Search.Validating", L"Checking that the result is still valid…" },
                { L"Search.Unsupported", L"Indexed search or result actions are not supported for this directory or link." },
                { L"Search.ActionError", L"This result could not be used (error {0})." },
                { L"Search.MenuError", L"Cannot open the Windows context menu ({0})." },
                { L"Search.NativeDispatched", L"Handed to Windows. Check the system or application window for progress." },
                { L"Search.PathCopied", L"File path copied." },
                { L"Search.MenuPasteParent", L"Paste into containing folder" },
                { L"Search.MenuCopyPath", L"Copy full path" },
                { L"Search.MenuCopyParentPath", L"Copy containing folder path" },
                { L"Search.MenuSystemOptions", L"Compression and more system options…" },
                { L"Search.ParentCopied", L"Containing folder path copied." },
                { L"Search.ErrorCode", L"Error {0}" },
                { L"Search.ResultChanged", L"The result changed. Search again before opening or locating it." },
                { L"Search.InvalidQuery", L"Enter valid search text and a search location." },
                { L"Search.Exact", L"Exact name" },
                { L"Search.ExactPartial", L"“{0}”: showing {1} items; exact results could not be fully checked." },
                { L"Search.ExactLimit", L"Name candidates reached the per-volume limit of {0}. Exact matches may be missing. Narrow the scope." },
                { L"App.Title", L"Blink" },
                { L"Nav.Places", L"Places" },
                { L"Nav.Drives", L"Drives" },
                { L"Nav.Home", L"Home" },
                { L"Nav.Desktop", L"Desktop" },
                { L"Nav.Downloads", L"Downloads" },
                { L"Nav.Documents", L"Documents" },
                { L"Location.ThisPC", L"This PC" },
                { L"ThisPC.Places", L"Folder" },
                { L"ThisPC.Drive", L"Drive" },
                { L"Toolbar.Back", L"Back" },
                { L"Toolbar.Forward", L"Forward" },
                { L"Toolbar.Up", L"Up" },
                { L"Toolbar.Refresh", L"Refresh" },
                { L"Toolbar.PathPlaceholder", L"Enter a folder path" },
                { L"Toolbar.SearchPlaceholder", L"Search names or extensions" },
                { L"Toolbar.SortBy", L"Sort by {0}" },
                { L"Toolbar.View", L"View" },
                { L"Toolbar.Sort", L"Sort" },
                { L"View.Details", L"Details" },
                { L"View.List", L"List" },
                { L"View.Grid", L"Grid" },
                { L"View.Show", L"Show" },
                { L"View.ShowHiddenItems", L"Hidden items" },
                { L"View.ShowHiddenItemsHint", L"Applies to all tabs and the navigation tree. System items stay hidden." },
                { L"Sort.Ascending", L"Ascending" },
                { L"Sort.Descending", L"Descending" },
                { L"FileSize.Bytes", L"B" },
                { L"FileSize.Kilobytes", L"KB" },
                { L"FileSize.Megabytes", L"MB" },
                { L"FileSize.Gigabytes", L"GB" },
                { L"FileSize.Terabytes", L"TB" },
                { L"Common.NotAvailable", L"-" },
                { L"Column.Name", L"Name" },
                { L"Column.Type", L"Type" },
                { L"Column.Size", L"Size" },
                { L"Column.Modified", L"Modified" },
                { L"Command.Open", L"Open" },
                { L"Command.Cut", L"Cut" },
                { L"Command.Copy", L"Copy" },
                { L"Command.Rename", L"Rename" },
                { L"Command.Delete", L"Delete" },
                { L"Command.PermanentDelete", L"Permanently delete" },
                { L"Command.Properties", L"Properties" },
                { L"Command.Paste", L"Paste" },
                { L"Command.NewFolder", L"New folder" },
                { L"Command.View", L"View" },
                { L"Command.UnavailableSuffix", L" (not available yet)" },
                { L"Status.CommandNotImplemented", L"{0} is not available yet." },
                { L"Status.WatchUnavailable", L"Automatic updates unavailable ({0}). Use Refresh." },
                { L"Status.CommandBusy", L"The folder is updating. Try again shortly." },
                { L"Status.CommandStale", L"The page has changed. Open the menu again." },
                { L"Status.CommandFolderRequired", L"Open a folder to use this command." },
                { L"Status.CommandSelectionRequired", L"Select an item first." },
                { L"Status.CommandSingleRequired", L"Select one item for this command." },
                { L"Status.Selected", L"{0} selected" },
                { L"Status.SelectAllFailed", L"Unable to select all items. Try again." },
                { L"Status.OpeningFile", L"Opening {0}" },
                { L"Status.OpenDispatched", L"Open request sent to Windows: {0}" },
                { L"Status.OpenChanged", L"This item has changed. Refresh the folder before opening it." },
                { L"Status.OpenNoAssociation", L"No default app is associated with this file: {0}" },
                { L"Status.OpenTimedOut", L"The open request did not finish in time. Check the target app before retrying." },
                { L"Status.OpenFailed", L"Unable to open {0}. (HRESULT {1})" },
                { L"Index.PanelTitle", L"Index service status" },
                { L"Index.Volume", L"Local volume" },
                { L"Index.RefreshStatus", L"Refresh status / reconnect" },
                { L"Index.Enable", L"Enable indexing" },
                { L"Index.EnableDescription", L"Enabling indexing for the first time requires Windows administrator confirmation." },
                { L"Index.BuildSelected", L"Index selected drive" },
                { L"Index.BuildDescription", L"Build the index for the selected drive to search its files." },
                { L"Index.EnableCancelled", L"Administrator confirmation was cancelled. Indexing has not been enabled." },
                { L"Index.Checking", L"Index: Checking…" },
                { L"Index.Connected", L"Index: Connected" },
                { L"Index.Disconnected", L"Index: Unavailable" },
                { L"Index.ServiceRunning", L"The index service is running." },
                { L"Index.ServiceStarting", L"The index service is starting." },
                { L"Index.ServiceStopping", L"The index service is stopping." },
                { L"Index.ServiceStopped", L"The index service is stopped." },
                { L"Index.ServiceFaulted", L"The index service reported a failure." },
                { L"Index.ConnectionError", L"Could not read the index service ({0}). Local browsing is still available." },
                { L"Index.OperationProgress", L"Service operation in progress: {0} records processed." },
                { L"Index.VolumeCounts", L"{0} files, {1} folders" },
                { L"Index.VolumeError", L"Error: {0}" },
                { L"Index.Healthy", L"Ready to search" },
                { L"Index.CatchingUp", L"Catching up" },
                { L"Index.RebuildRequired", L"Rebuild required" },
                { L"Index.Rebuilding", L"Rebuilding" },
                { L"Index.Unavailable", L"Unavailable" },
                { L"Index.Corrupted", L"Index data damaged" },
                { L"Index.Unconfirmed", L"Status not confirmed" },
                { L"Status.IndexNotConnected", L"Index: Not connected" },
                { L"Status.Loading", L"Loading  {0}" },
                { L"Status.LoadingThisPC", L"Loading This PC" },
                { L"Status.ThisPCReadyCount", L"Ready  -  {0} locations" },
                { L"Status.ThisPCFailed", L"Unable to refresh This PC. Showing the previous locations. (Win32 {0})" },
                { L"Status.Empty", L"Ready  -  empty folder" },
                { L"Status.ChooseFolder", L"Ready  -  choose a folder" },
                { L"Status.ReadyCount", L"Ready  -  {0} items" },
                { L"Status.InvalidPath", L"Folder path is invalid" },
                { L"Status.ZeroSelected", L"0 selected" },
                { L"Browse.PathNotFound", L"Path not found. Showing the previous folder. (Win32 {0})" },
                { L"Browse.AccessDenied", L"Access denied. Showing the previous folder. (Win32 {0})" },
                { L"Browse.Failed", L"Unable to open this folder. Showing the previous folder. (Win32 {0})" },
                { L"FileType.Folder", L"Folder" },
                { L"FileType.TextDocument", L"Text document" },
                { L"FileType.CppSource", L"C++ source" },
                { L"FileType.Image", L"Image" },
                { L"FileType.CompressedArchive", L"Compressed archive" },
                { L"FileType.Application", L"Application" },
                { L"FileType.File", L"File" },
                { L"FileType.FileSuffix", L"{0} file" },
                { L"Status.BuildingIndex", L"Building index on a worker thread..." },
                { L"Status.IndexBuildFailed", L"Index build failed" },
                { L"Status.IndexReady", L"Index ready: {0} files, {1} directories, {2}s; {3}" },
                { L"Status.SavedYes", L"saved=yes" },
                { L"Status.SavedError", L"saved=no, error={0}" },
                { L"Status.SearchingLocal", L"Searching without rescanning..." },
                { L"Status.LocalSearchResult", L"Index search: " },
                { L"Status.QueryingIndexService", L"Querying IndexService..." },
                { L"Status.IndexServiceUnavailable", L"IndexService unavailable" },
                { L"Status.IndexService", L"IndexService: {0}, files={1}, dirs={2}, error={3}" },
                { L"Status.StartingFastIndex", L"Starting privileged MFT/USN index..." },
                { L"Status.FastIndexRequest", L"MFT/USN request: {0}, operation={1}, error={2}" },
                { L"Status.SearchingFastIndex", L"Searching IndexService without a DFS rescan..." },
                { L"Status.FastSearchState", L"Fast search state={0}, error={1}" },
                { L"Status.FastSearch", L"Fast search: {0} results; state={1}" },
                { L"Status.SelectOneFile", L"Select one file first" },
                { L"Status.ReadingProperties", L"Reading properties on a worker thread..." },
                { L"Status.PropertiesFailed", L"Properties failed" },
                { L"Status.Properties", L"Properties: {0}; ACL={1}; metadata={2}; streams={3}" },
                { L"Status.CopyFileProbeRequiresFile", L"CopyFile2 probe requires a file selection" },
                { L"Status.CopyRunning", L"CopyFile2 running in the background..." },
                { L"Status.CopyResult", L"CopyFile2: status=0x" },
                { L"Status.CopyCanceled", L", canceled" },
                { L"Status.CopyComplete", L", complete" },
                { L"Status.StartingProvider", L"Starting isolated ProviderHost: " },
                { L"Status.ProviderStartFailed", L"ProviderHost start failed" },
                { L"Status.Provider", L"Provider " },
                { L"Status.ProviderResult", L"Provider {0}: {1}, {2}, error={3}" },
                { L"Status.SuccessYes", L"success=yes" },
                { L"Status.SuccessNo", L"success=no" },
                { L"Status.CanceledYes", L"canceled=yes" },
                { L"Status.CanceledNo", L"canceled=no" },
            };
            const auto found = values.find(std::wstring(key));
            return found == values.end() ? std::wstring(key) : found->second;
        }

#if !defined(BLINK_LOCALIZATION_DISABLE_RESOURCE_LOADER)
        [[nodiscard]] std::filesystem::path ApplicationPriPath()
        {
            std::wstring buffer(32768, L'\0');
            const DWORD length = GetModuleFileNameW(
                nullptr,
                buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (length == 0 || length >= buffer.size())
            {
                return {};
            }

            buffer.resize(length);
            auto path = std::filesystem::path(std::move(buffer));
            path.replace_extension(L".pri");
            return path;
        }

        [[nodiscard]] std::wstring ResourcePathForKey(const std::wstring_view key)
        {
            std::wstring resource_path(key);
            std::replace(resource_path.begin(), resource_path.end(), L'.', L'/');
            return resource_path;
        }
#endif
    }

    std::wstring Localization::GetString(const std::wstring_view key) const
    {
#if !defined(BLINK_LOCALIZATION_DISABLE_RESOURCE_LOADER)
        try
        {
            const auto pri_path = ApplicationPriPath();
            if (pri_path.empty())
            {
                return FallbackString(key);
            }

            const auto loader =
                winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader(
                    winrt::hstring(pri_path.c_str()),
                    winrt::hstring(L"Resources"));
            const auto try_get = [&loader](const std::wstring_view resource_key)
                -> std::wstring
            {
                const auto value = loader.GetString(winrt::hstring(resource_key));
                return value.empty() ? std::wstring{} : std::wstring(value.c_str());
            };

            auto value = try_get(ResourcePathForKey(key));
            if (value.empty())
            {
                value = try_get(key);
            }
            if (!value.empty())
            {
                return value;
            }
        }
        catch (...)
        {
            // A missing resource must not make the browser unusable. The key
            // remains a diagnosable fallback while the build keeps resource
            // generation explicit in the project file.
        }
#endif
        return FallbackString(key);
    }

    std::wstring Localization::Format(
        const std::wstring_view key,
        const std::wstring_view value) const
    {
        return ReplaceValue(GetString(key), value);
    }

    std::wstring Localization::Format(
        const std::wstring_view key,
        const std::uint64_t value) const
    {
        return Format(key, std::to_wstring(value));
    }

    std::wstring Localization::Format(
        const std::wstring_view key,
        const std::uint64_t first,
        const std::uint64_t second) const
    {
        return Format(key, {
            std::to_wstring(first),
            std::to_wstring(second) });
    }

    std::wstring Localization::Format(
        const std::wstring_view key,
        std::vector<std::wstring> values) const
    {
        return ReplaceValues(GetString(key), values);
    }

    bool Localization::IsThisPcDisplayText(const std::wstring_view text) const
    {
        std::wstring_view trimmed = text;
        while (!trimmed.empty() && (trimmed.front() == L' ' || trimmed.front() == L'\t'))
        {
            trimmed.remove_prefix(1);
        }
        while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L'\t'))
        {
            trimmed.remove_suffix(1);
        }
        if (trimmed.empty())
        {
            return false;
        }

        const auto localized_this_pc = GetString(L"Location.ThisPC");
        const std::wstring candidate(trimmed);
        return _wcsicmp(candidate.c_str(), localized_this_pc.c_str()) == 0;
    }
}
