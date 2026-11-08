#include "Blink/Index/FileIndex.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <unordered_set>

namespace blink::index
{
    namespace
    {
        [[nodiscard]] std::wstring Fold(const std::wstring& value)
        {
            std::wstring result = value;
            std::transform(result.begin(), result.end(), result.begin(), [](wchar_t character) {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return result;
        }

        [[nodiscard]] bool StartsWith(const std::wstring& value, const std::wstring& prefix)
        {
            return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
        }

        void WriteBytes(HANDLE handle, const void* data, DWORD size, bool& ok)
        {
            if (!ok)
            {
                return;
            }
            DWORD written = 0;
            ok = WriteFile(handle, data, size, &written, nullptr) != FALSE && written == size;
        }

        void WriteString(HANDLE handle, const std::wstring& value, bool& ok)
        {
            const std::uint32_t length = static_cast<std::uint32_t>(value.size());
            WriteBytes(handle, &length, sizeof(length), ok);
            if (length != 0)
            {
                WriteBytes(handle, value.data(), length * sizeof(wchar_t), ok);
            }
        }

        bool ReadBytes(HANDLE handle, void* data, DWORD size, bool& ok)
        {
            if (!ok)
            {
                return false;
            }
            DWORD read = 0;
            ok = ReadFile(handle, data, size, &read, nullptr) != FALSE && read == size;
            return ok;
        }

        std::wstring ReadString(HANDLE handle, bool& ok)
        {
            std::uint32_t length = 0;
            if (!ReadBytes(handle, &length, sizeof(length), ok) || length > 1'000'000)
            {
                ok = false;
                return {};
            }
            std::wstring value(length, L'\0');
            if (length != 0)
            {
                ReadBytes(handle, value.data(), length * sizeof(wchar_t), ok);
            }
            return value;
        }

        [[nodiscard]] blink::core::UniqueHandle OpenIndexFile(const std::wstring& path, DWORD access, DWORD disposition)
        {
            CREATEFILE3_EXTENDED_PARAMETERS parameters{};
            parameters.dwSize = sizeof(parameters);
            parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            const HANDLE handle = CreateFile3(
                path.c_str(),
                access,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                disposition,
                &parameters);
            return blink::core::UniqueHandle(handle);
        }

        void AddEntry(std::vector<blink::core::FileEntry>& entries, const blink::core::FileEntry& entry, IndexStats& stats)
        {
            entries.push_back(entry);
            if (entry.is_directory)
            {
                ++stats.directories;
            }
            else
            {
                ++stats.files;
                stats.bytes_indexed += entry.size;
            }
        }
    }

    bool FileIndex::Build(const std::wstring& root, std::atomic_bool* cancellation, IndexStats* stats)
    {
        const auto started = std::chrono::steady_clock::now();
        IndexStats local_stats{};
        local_stats.journal = QueryUsnJournal(root);

        std::vector<blink::core::FileEntry> new_entries;
        std::vector<std::wstring> pending{ blink::core::NormalizePath(root) };
        std::unordered_set<std::wstring> visited_directories;

        while (!pending.empty())
        {
            if (cancellation != nullptr && cancellation->load())
            {
                return false;
            }

            const std::wstring current = std::move(pending.back());
            pending.pop_back();
            if (current.empty() || !visited_directories.insert(Fold(current)).second)
            {
                continue;
            }

            const blink::core::DirectoryListing listing = blink::core::EnumerateDirectory(current);
            if (listing.error != ERROR_SUCCESS)
            {
                ++local_stats.errors;
                continue;
            }

            for (const auto& entry : listing.entries)
            {
                if (cancellation != nullptr && cancellation->load())
                {
                    return false;
                }

                AddEntry(new_entries, entry, local_stats);
                if (entry.is_directory)
                {
                    if ((entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    {
                        ++local_stats.skipped_directories;
                        continue;
                    }
                    pending.push_back(entry.full_path);
                }
            }
        }

        std::vector<NameKey> new_keys;
        new_keys.reserve(new_entries.size());
        for (std::size_t index = 0; index < new_entries.size(); ++index)
        {
            new_keys.push_back(NameKey{ Fold(new_entries[index].name), index });
        }
        std::sort(new_keys.begin(), new_keys.end(), [](const NameKey& left, const NameKey& right) {
            if (left.key != right.key)
            {
                return left.key < right.key;
            }
            return left.index < right.index;
        });

        local_stats.resident_bytes = new_entries.capacity() * sizeof(blink::core::FileEntry) + new_keys.capacity() * sizeof(NameKey);
        local_stats.index_bytes = local_stats.resident_bytes;
        local_stats.build_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        {
            std::scoped_lock lock(mutex_);
            entries_ = std::move(new_entries);
            name_keys_ = std::move(new_keys);
            stats_ = local_stats;
        }
        if (stats != nullptr)
        {
            *stats = local_stats;
        }
        return true;
    }

    std::vector<blink::core::FileEntry> FileIndex::Search(const SearchOptions& options, double* elapsed_ms) const
    {
        const auto started = std::chrono::steady_clock::now();
        const std::wstring query = Fold(options.text);
        const std::wstring extension = Fold(options.extension);
        const std::wstring path_prefix = options.path_prefix.empty()
            ? std::wstring{}
            : Fold(blink::core::NormalizePath(options.path_prefix));

        std::vector<blink::core::FileEntry> results;
        std::scoped_lock lock(mutex_);

        auto consider = [&](std::size_t index) {
            if (index >= entries_.size() || results.size() >= options.limit)
            {
                return;
            }
            const auto& entry = entries_[index];
            const std::wstring name = Fold(entry.name);
            const std::wstring full_path = Fold(entry.full_path);
            if (!query.empty())
            {
                const bool match = options.prefix_match ? StartsWith(name, query) : name.find(query) != std::wstring::npos;
                if (!match)
                {
                    return;
                }
            }
            if (!extension.empty() && Fold(entry.extension) != extension)
            {
                return;
            }
            if (!path_prefix.empty() && full_path.find(path_prefix) != 0)
            {
                return;
            }
            results.push_back(entry);
        };

        if (options.prefix_match && !query.empty())
        {
            const auto lower = std::lower_bound(name_keys_.begin(), name_keys_.end(), query, [](const NameKey& key, const std::wstring& value) {
                return key.key < value;
            });
            for (auto iterator = lower; iterator != name_keys_.end() && results.size() < options.limit; ++iterator)
            {
                if (!StartsWith(iterator->key, query))
                {
                    break;
                }
                consider(iterator->index);
            }
        }
        else
        {
            for (std::size_t index = 0; index < entries_.size() && results.size() < options.limit; ++index)
            {
                consider(index);
            }
        }

        const double duration = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        ++stats_.search_count;
        stats_.last_search_milliseconds = duration;
        if (elapsed_ms != nullptr)
        {
            *elapsed_ms = duration;
        }
        return results;
    }

    bool FileIndex::Save(const std::wstring& path, DWORD* error) const
    {
        auto handle = OpenIndexFile(path, GENERIC_WRITE, CREATE_ALWAYS);
        if (!handle)
        {
            if (error != nullptr)
            {
                *error = GetLastError();
            }
            return false;
        }

        std::scoped_lock lock(mutex_);
        const std::uint32_t magic = 0x424C4B30; // BLK0
        const std::uint32_t version = 1;
        const std::uint64_t count = entries_.size();
        bool ok = true;
        WriteBytes(handle.get(), &magic, sizeof(magic), ok);
        WriteBytes(handle.get(), &version, sizeof(version), ok);
        WriteBytes(handle.get(), &count, sizeof(count), ok);
        for (const auto& entry : entries_)
        {
            WriteString(handle.get(), entry.name, ok);
            WriteString(handle.get(), entry.full_path, ok);
            WriteString(handle.get(), entry.extension, ok);
            WriteBytes(handle.get(), &entry.is_directory, sizeof(entry.is_directory), ok);
            WriteBytes(handle.get(), &entry.size, sizeof(entry.size), ok);
            WriteBytes(handle.get(), &entry.allocated_size, sizeof(entry.allocated_size), ok);
            WriteBytes(handle.get(), &entry.creation_time, sizeof(entry.creation_time), ok);
            WriteBytes(handle.get(), &entry.last_access_time, sizeof(entry.last_access_time), ok);
            WriteBytes(handle.get(), &entry.last_write_time, sizeof(entry.last_write_time), ok);
            WriteBytes(handle.get(), &entry.attributes, sizeof(entry.attributes), ok);
            WriteBytes(handle.get(), &entry.reparse_tag, sizeof(entry.reparse_tag), ok);
            WriteBytes(handle.get(), &entry.identity, sizeof(entry.identity), ok);
            if (!ok)
            {
                break;
            }
        }

        if (error != nullptr)
        {
            *error = ok ? ERROR_SUCCESS : GetLastError();
        }
        return ok;
    }

    bool FileIndex::Load(const std::wstring& path, DWORD* error)
    {
        auto handle = OpenIndexFile(path, GENERIC_READ, OPEN_EXISTING);
        if (!handle)
        {
            if (error != nullptr)
            {
                *error = GetLastError();
            }
            return false;
        }

        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        std::uint64_t count = 0;
        bool ok = true;
        ReadBytes(handle.get(), &magic, sizeof(magic), ok);
        ReadBytes(handle.get(), &version, sizeof(version), ok);
        ReadBytes(handle.get(), &count, sizeof(count), ok);
        if (!ok || magic != 0x424C4B30 || version != 1 || count > 100'000'000)
        {
            if (error != nullptr)
            {
                *error = ERROR_INVALID_DATA;
            }
            return false;
        }

        std::vector<blink::core::FileEntry> entries;
        entries.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t index = 0; index < count; ++index)
        {
            blink::core::FileEntry entry;
            entry.name = ReadString(handle.get(), ok);
            entry.full_path = ReadString(handle.get(), ok);
            entry.extension = ReadString(handle.get(), ok);
            ReadBytes(handle.get(), &entry.is_directory, sizeof(entry.is_directory), ok);
            ReadBytes(handle.get(), &entry.size, sizeof(entry.size), ok);
            ReadBytes(handle.get(), &entry.allocated_size, sizeof(entry.allocated_size), ok);
            ReadBytes(handle.get(), &entry.creation_time, sizeof(entry.creation_time), ok);
            ReadBytes(handle.get(), &entry.last_access_time, sizeof(entry.last_access_time), ok);
            ReadBytes(handle.get(), &entry.last_write_time, sizeof(entry.last_write_time), ok);
            ReadBytes(handle.get(), &entry.attributes, sizeof(entry.attributes), ok);
            ReadBytes(handle.get(), &entry.reparse_tag, sizeof(entry.reparse_tag), ok);
            ReadBytes(handle.get(), &entry.identity, sizeof(entry.identity), ok);
            if (!ok)
            {
                break;
            }
            entries.push_back(std::move(entry));
        }
        if (!ok)
        {
            if (error != nullptr)
            {
                *error = GetLastError();
            }
            return false;
        }

        std::vector<NameKey> keys;
        keys.reserve(entries.size());
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            keys.push_back(NameKey{ Fold(entries[index].name), index });
        }
        std::sort(keys.begin(), keys.end(), [](const NameKey& left, const NameKey& right) {
            return left.key == right.key ? left.index < right.index : left.key < right.key;
        });
        {
            std::scoped_lock lock(mutex_);
            entries_ = std::move(entries);
            name_keys_ = std::move(keys);
            stats_ = {};
            stats_.files = std::count_if(entries_.begin(), entries_.end(), [](const auto& entry) { return !entry.is_directory; });
            stats_.directories = entries_.size() - stats_.files;
            stats_.resident_bytes = entries_.capacity() * sizeof(blink::core::FileEntry) + name_keys_.capacity() * sizeof(NameKey);
            stats_.index_bytes = stats_.resident_bytes;
        }
        if (error != nullptr)
        {
            *error = ERROR_SUCCESS;
        }
        return true;
    }

    std::size_t FileIndex::Size() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return entries_.size();
    }

    IndexStats FileIndex::Stats() const
    {
        std::scoped_lock lock(mutex_);
        return stats_;
    }

    void FileIndex::Clear()
    {
        std::scoped_lock lock(mutex_);
        entries_.clear();
        name_keys_.clear();
        stats_ = {};
    }
}
