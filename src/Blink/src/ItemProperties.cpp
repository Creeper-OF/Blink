#include "Blink/App/ItemProperties.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace blink::app
{
    namespace
    {
        bool Stopped(const PropertiesCancellation& value) noexcept { return value && value->load(); }
        std::wstring NativePath(const std::wstring& path)
        {
            const auto normalized = blink::core::NormalizePath(path);
            if (normalized.starts_with(L"\\\\?\\")) { return normalized; }
            return normalized.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + normalized.substr(2) : L"\\\\?\\" + normalized;
        }
        DWORD LastError() noexcept { const auto error = GetLastError(); return error ? error : ERROR_GEN_FAILURE; }
        FILETIME Time(const LARGE_INTEGER& value) noexcept { return { value.LowPart, static_cast<DWORD>(value.HighPart) }; }
        DWORD ReadBasic(const std::wstring& path, const blink::core::FileIdentity& expected, blink::core::FileEntry& entry)
        {
            CREATEFILE3_EXTENDED_PARAMETERS parameters{}; parameters.dwSize = sizeof(parameters);
            parameters.dwFileFlags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
            blink::core::UniqueHandle handle(CreateFile3(NativePath(path).c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, &parameters));
            if (!handle) { return LastError(); }
            FILE_BASIC_INFO basic{}; FILE_STANDARD_INFO standard{}; FILE_ATTRIBUTE_TAG_INFO tag{};
            if (!blink::core::ReadFileIdentity(handle.get(), entry.identity) ||
                !GetFileInformationByHandleEx(handle.get(), FileBasicInfo, &basic, sizeof(basic)) ||
                !GetFileInformationByHandleEx(handle.get(), FileStandardInfo, &standard, sizeof(standard)) ||
                !GetFileInformationByHandleEx(handle.get(), FileAttributeTagInfo, &tag, sizeof(tag))) { return LastError(); }
            if (expected.valid && expected != entry.identity) { return ERROR_FILE_INVALID; }
            if (standard.DeletePending) { return ERROR_DELETE_PENDING; }
            entry.full_path = path; entry.attributes = tag.FileAttributes; entry.reparse_tag = tag.ReparseTag;
            entry.is_directory = (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            entry.size = standard.EndOfFile.QuadPart > 0 ? standard.EndOfFile.QuadPart : 0;
            entry.allocated_size = standard.AllocationSize.QuadPart > 0 ? standard.AllocationSize.QuadPart : 0;
            entry.creation_time = Time(basic.CreationTime); entry.last_write_time = Time(basic.LastWriteTime);
            entry.last_access_time = Time(basic.LastAccessTime); entry.extension = blink::core::FileExtension(entry.name);
            if (!entry.is_directory && (entry.attributes & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE)))
            {
                FILE_COMPRESSION_INFO compression{};
                if (!GetFileInformationByHandleEx(handle.get(), FileCompressionInfo, &compression, sizeof(compression))) { return LastError(); }
                entry.allocated_size = compression.CompressedFileSize.QuadPart > 0 ? compression.CompressedFileSize.QuadPart : 0;
            }
            return ERROR_SUCCESS;
        }
        void Unavailable(ItemPropertiesSnapshot& result, const DWORD error, const std::wstring& path)
        {
            ++result.unavailable_count;
            if (!result.error) { result.error = error ? error : ERROR_GEN_FAILURE; result.failure_path = path; }
        }
        void Add(std::uint64_t& total, const std::uint64_t value)
        {
            if (value > (std::numeric_limits<std::uint64_t>::max)() - total) { throw std::overflow_error("properties size overflow"); }
            total += value;
        }
        void CountFile(ItemPropertiesSnapshot& result, const blink::core::FileEntry& entry)
        { Add(result.files, 1); Add(result.bytes, entry.size); Add(result.allocated_bytes, entry.allocated_size); }
    }
    HRESULT CaptureItemProperties(const TabState& state, ItemPropertiesRequest& request)
    {
        request = {};
        try
        {
            const auto page = state.PresentationSnapshot();
            if (!page.committed_location.IsFolder() || page.has_pending || !state.CommittedDirectoryIdentity().valid) { return E_INVALIDARG; }
            const auto selected = state.SelectedCommittedRows(MaximumPropertiesItems);
            if (!selected || selected->empty()) { return E_INVALIDARG; }
            request = { page.committed_location.FolderPath(), state.CommittedDirectoryIdentity(), *selected }; return S_OK;
        }
        catch (const std::bad_alloc&) { request = {}; return E_OUTOFMEMORY; }
        catch (...) { request = {}; return E_FAIL; }
    }
    ItemPropertiesSnapshot ReadItemProperties(const ItemPropertiesRequest& request, const PropertiesCancellation& cancelled,
        const PropertiesProgress& progress) noexcept
    {
        ItemPropertiesSnapshot result;
        try
        {
            result.selected_count = request.entries.size();
            if (request.entries.empty() || request.entries.size() > MaximumPropertiesItems || request.parent.empty() ||
                !request.parent_identity.valid) { result.error = ERROR_INVALID_PARAMETER; result.finished = true; return result; }
            for (const auto& entry : request.entries)
            {
                if (entry.name.empty() || entry.name == L"." || entry.name == L".." || entry.name.find_first_of(L"\\/:\0", 0, 4) != std::wstring::npos)
                { result.error = ERROR_INVALID_PARAMETER; result.finished = true; return result; }
                if (entry.is_directory) { ++result.selected_folders; } else { ++result.selected_files; }
            }
            result.items.resize(request.entries.size());
            const auto report = [&] { if (progress) { progress(result); } };
            report();
            if (!Stopped(cancelled))
            {
                blink::core::FileEntry parent;
                const auto error = ReadBasic(request.parent, request.parent_identity, parent);
                if (error || !parent.is_directory)
                { Unavailable(result, error ? error : ERROR_DIRECTORY, request.parent); result.finished = true; report(); return result; }
            }
            struct Directory { std::wstring path; blink::core::FileIdentity identity; };
            std::vector<Directory> pending;
            for (std::size_t index = 0; index < request.entries.size() && !Stopped(cancelled); ++index)
            {
                const auto& original = request.entries[index]; auto& item = result.items[index]; item.entry.name = original.name;
                const auto path = blink::core::JoinPath(request.parent, original.name);
                item.error = original.identity.valid ? ReadBasic(path, original.identity, item.entry) : ERROR_FILE_INVALID;
                if (!item.error && item.entry.is_directory != original.is_directory) { item.error = ERROR_FILE_INVALID; }
                if (item.error) { Unavailable(result, item.error, path); report(); continue; }
                ++result.read_count;
                if (item.entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) { ++result.links_not_followed; }
                if (!item.entry.is_directory) { CountFile(result, item.entry); }
                else if (!(item.entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT)) { pending.push_back({ path, item.entry.identity }); }
                report();
            }
            while (!pending.empty() && !Stopped(cancelled))
            {
                const auto directory = std::move(pending.back()); pending.pop_back();
                blink::core::FileEntry current;
                const auto checked = ReadBasic(directory.path, directory.identity, current);
                if (checked || (current.attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                { Unavailable(result, checked ? checked : ERROR_FILE_INVALID, directory.path); report(); continue; }
                const auto listing = blink::core::EnumerateDirectory(NativePath(directory.path));
                if (listing.directory_identity != directory.identity)
                { Unavailable(result, listing.error ? listing.error : ERROR_FILE_INVALID, directory.path); report(); continue; }
                if (listing.error) { Unavailable(result, listing.error, directory.path); }
                for (auto entry : listing.entries)
                {
                    if (Stopped(cancelled)) { break; }
                    const auto path = blink::core::JoinPath(directory.path, entry.name);
                    if (!entry.identity.valid) { Unavailable(result, ERROR_FILE_INVALID, path); continue; }
                    if (entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) { ++result.links_not_followed; }
                    if (entry.is_directory)
                    {
                        Add(result.folders, 1);
                        if (!(entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT)) { pending.push_back({ path, entry.identity }); }
                    }
                    else
                    {
                        if (entry.attributes & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE | FILE_ATTRIBUTE_REPARSE_POINT))
                        {
                            const auto error = ReadBasic(path, entry.identity, entry);
                            if (error) { Unavailable(result, error, path); continue; }
                        }
                        CountFile(result, entry);
                    }
                }
                report();
            }
            result.cancelled = Stopped(cancelled); result.finished = true;
            if (result.cancelled && !result.error) { result.error = ERROR_CANCELLED; }
            report();
        }
        catch (const std::bad_alloc&) { result.error = ERROR_NOT_ENOUGH_MEMORY; result.finished = true; }
        catch (const std::overflow_error&) { result.error = ERROR_ARITHMETIC_OVERFLOW; result.finished = true; }
        catch (...) { result.error = ERROR_UNHANDLED_EXCEPTION; result.finished = true; }
        return result;
    }
}
