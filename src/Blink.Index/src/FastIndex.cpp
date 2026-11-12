#include "Blink/Index/FastIndex.h"

#include "Blink/Core/FileTypes.h"

#include <fileapi.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>

#pragma comment(lib, "Kernel32.lib")

namespace blink::index
{
    namespace
    {
        constexpr std::uint32_t kStoreMagic = 0x354B4C42; // BLK5
        constexpr std::uint32_t kStoreVersion = 1;
        constexpr std::uint64_t kMaxStoredLinks = 50'000'000;
        constexpr std::uint32_t kMaxStoredStringChars = 1'000'000;

        struct StoreHeader
        {
            std::uint32_t magic{};
            std::uint32_t version{};
            std::uint64_t volume_serial{};
            std::uint64_t journal_id{};
            std::int64_t first_usn{};
            std::int64_t next_usn{};
            std::int64_t lowest_valid_usn{};
            std::int64_t max_usn{};
            std::int64_t last_usn{};
            std::uint64_t link_count{};
        };

        class Crc32
        {
        public:
            void Update(const void* data, std::size_t size) noexcept
            {
                const auto* bytes = static_cast<const std::uint8_t*>(data);
                for (std::size_t i = 0; i < size; ++i)
                {
                    value_ ^= bytes[i];
                    for (int bit = 0; bit < 8; ++bit)
                    {
                        const std::uint32_t mask = (value_ & 1u) != 0 ? 0xEDB88320u : 0u;
                        value_ = (value_ >> 1) ^ mask;
                    }
                }
            }

            [[nodiscard]] std::uint32_t Value() const noexcept
            {
                return ~value_;
            }

        private:
            std::uint32_t value_{ 0xFFFFFFFFu };
        };

        [[nodiscard]] std::wstring VolumeRootFor(const std::wstring& path)
        {
            wchar_t buffer[32768]{};
            if (!GetVolumePathNameW(path.c_str(), buffer, static_cast<DWORD>(std::size(buffer))))
            {
                return {};
            }
            return buffer;
        }

        [[nodiscard]] std::wstring FoldCase(std::wstring value)
        {
            bool non_ascii = false;
            for (wchar_t& character : value)
            {
                if (character >= L'a' && character <= L'z')
                { character = static_cast<wchar_t>(character - (L'a' - L'A')); }
                else if (character > 0x7F) { non_ascii = true; }
            }
            if (!non_ascii) { return value; }
            if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            { throw std::length_error("Index case mapping input is too long"); }

            const int length = static_cast<int>(value.size());
            // File-name matching is ordinal and independent of the CRT/user
            // locale. Keep this derived key out of BLK5 and rebuild on Load.
            const int required = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE,
                value.data(), length, nullptr, 0, nullptr, nullptr, 0);
            if (required > 0)
            {
                std::wstring mapped(static_cast<std::size_t>(required), L'\0');
                if (required == length && LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(), length,
                    mapped.data(), required, nullptr, nullptr, 0) == required &&
                    CompareStringOrdinal(value.data(), length, mapped.data(), required, TRUE) == CSTR_EQUAL)
                { return mapped; }
            }

            // NLS can expand a character (for example sharp S) or reject an
            // unpaired UTF-16 surrogate. Never turn that into an extra ordinal
            // equivalence, discard the name, or split a valid surrogate pair.
            for (std::size_t offset = 0; offset < value.size();)
            {
                if (value[offset] <= 0x7F) { ++offset; continue; }
                const int count = value[offset] >= 0xD800 && value[offset] <= 0xDBFF &&
                    offset + 1 < value.size() && value[offset + 1] >= 0xDC00 && value[offset + 1] <= 0xDFFF ? 2 : 1;
                wchar_t mapped[2]{};
                if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data() + offset, count,
                    mapped, count, nullptr, nullptr, 0) == count &&
                    CompareStringOrdinal(value.data() + offset, count, mapped, count, TRUE) == CSTR_EQUAL)
                { std::copy_n(mapped, count, value.data() + offset); }
                offset += static_cast<std::size_t>(count);
            }
            return value;
        }

        [[nodiscard]] std::wstring FoldExtension(const std::wstring& name)
        {
            const auto separator = name.find_last_of(L"\\/");
            const auto dot = name.find_last_of(L'.');
            if (dot == std::wstring::npos || dot == 0 ||
                (separator != std::wstring::npos && dot < separator)) { return {}; }
            // Core::FileExtension also lowercases through the CRT locale.
            // Read the raw suffix so extension keys use the same Windows fold
            // as cached names and incoming search predicates.
            return FoldCase(name.substr(dot));
        }

        [[nodiscard]] bool StartsWithInsensitive(const std::wstring& value, const std::wstring& prefix)
        {
            if (prefix.size() > value.size())
            {
                return false;
            }
            return CompareStringOrdinal(value.data(), static_cast<int>(prefix.size()),
                prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
        }

        void MarkFastSearchTiming(
            std::atomic<std::int64_t>& target,
            const std::chrono::steady_clock::time_point origin) noexcept
        {
            target.store(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - origin).count(),
                std::memory_order_release);
        }

        [[nodiscard]] blink::core::FileIdentity IdentityFromRecord(
            const UsnRecordSummary& record,
            std::uint64_t volume_serial,
            bool parent)
        {
            blink::core::FileIdentity identity;
            identity.volume_serial = volume_serial;
            identity.valid = true;
            if (record.major_version == 3)
            {
                const auto& source = parent ? record.parent_reference_128 : record.file_reference_128;
                std::copy(source.begin(), source.end(), identity.file_id.begin());
            }
            else if (record.major_version == 2)
            {
                const std::uint64_t reference = parent ? record.parent_reference : record.file_reference;
                std::memcpy(identity.file_id.data(), &reference, sizeof(reference));
            }
            else
            {
                identity.valid = false;
            }
            return identity;
        }

        [[nodiscard]] bool SameCanonicalUsnIdentity(
            const blink::core::FileIdentity& left,
            const blink::core::FileIdentity& right) noexcept
        {
            // FILE_ID_INFO retains the complete 64-bit volume serial, while
            // USN records are paired with GetVolumeInformationW's canonical
            // DWORD serial.  Keep the full FileIdentity values intact and
            // use this comparison only at the boundary between those two
            // identity sources (root/path graph and excluded-root checks).
            return left.valid && right.valid &&
                left.file_id == right.file_id &&
                static_cast<DWORD>(left.volume_serial) == static_cast<DWORD>(right.volume_serial);
        }

        class BufferedWriter
        {
        public:
            explicit BufferedWriter(HANDLE handle, std::size_t capacity = 1024 * 1024)
                : handle_(handle), buffer_(capacity) {}

            ~BufferedWriter()
            {
                (void)Flush();
            }

            [[nodiscard]] bool Write(const void* data, std::size_t size, Crc32& crc)
            {
                crc.Update(data, size);
                const auto* bytes = static_cast<const std::uint8_t*>(data);
                while (size > 0)
                {
                    if (offset_ + size <= buffer_.size())
                    {
                        std::memcpy(buffer_.data() + offset_, bytes, size);
                        offset_ += size;
                        return true;
                    }
                    const std::size_t fill = buffer_.size() - offset_;
                    if (fill > 0)
                    {
                        std::memcpy(buffer_.data() + offset_, bytes, fill);
                        offset_ += fill;
                        bytes += fill;
                        size -= fill;
                    }
                    if (!Flush())
                    {
                        return false;
                    }
                }
                return true;
            }

            [[nodiscard]] bool Flush()
            {
                if (offset_ == 0) return true;
                DWORD written = 0;
                if (!WriteFile(handle_, buffer_.data(), static_cast<DWORD>(offset_), &written, nullptr) ||
                    written != offset_)
                {
                    return false;
                }
                offset_ = 0;
                return true;
            }

        private:
            HANDLE handle_;
            std::vector<std::uint8_t> buffer_;
            std::size_t offset_{ 0 };
        };

        template <typename T>
        [[nodiscard]] bool WriteValue(BufferedWriter& writer, const T& value, Crc32& crc)
        {
            return writer.Write(&value, sizeof(value), crc);
        }

        [[nodiscard]] bool WriteBytes(BufferedWriter& writer, const void* data, std::size_t size, Crc32& crc)
        {
            return writer.Write(data, size, crc);
        }

        class BufferedReader
        {
        public:
            explicit BufferedReader(HANDLE handle) noexcept : handle_(handle) {}

            [[nodiscard]] bool Read(void* data, std::size_t size) noexcept
            {
                auto* bytes = static_cast<std::uint8_t*>(data);
                while (size != 0)
                {
                    if (offset_ == available_)
                    {
                        DWORD read = 0;
                        if (!ReadFile(handle_, buffer_.data(), static_cast<DWORD>(buffer_.size()), &read, nullptr))
                        { return false; }
                        if (!read) { SetLastError(ERROR_HANDLE_EOF); return false; }
                        offset_ = 0;
                        available_ = read;
                    }
                    const auto count = (std::min)(size, available_ - offset_);
                    std::memcpy(bytes, buffer_.data() + offset_, count);
                    bytes += count;
                    offset_ += count;
                    size -= count;
                }
                return true;
            }

        private:
            HANDLE handle_;
            std::array<std::uint8_t, 64 * 1024> buffer_;
            std::size_t offset_{}, available_{};
        };

        template <typename T>
        [[nodiscard]] bool ReadValue(BufferedReader& reader, T& value, Crc32& crc)
        {
            if (!reader.Read(&value, sizeof(value))) { return false; }
            crc.Update(&value, sizeof(value));
            return true;
        }

        [[nodiscard]] bool ReadRaw(BufferedReader& reader, void* data, std::size_t size)
        {
            return reader.Read(data, size);
        }

        [[nodiscard]] bool WriteString(BufferedWriter& writer, const std::wstring& value, Crc32& crc)
        {
            if (value.size() > kMaxStoredStringChars)
            {
                SetLastError(ERROR_INVALID_DATA);
                return false;
            }
            const auto length = static_cast<std::uint32_t>(value.size());
            return WriteValue(writer, length, crc) &&
                WriteBytes(writer, value.data(), value.size() * sizeof(wchar_t), crc);
        }

        [[nodiscard]] bool ReadString(BufferedReader& reader, std::wstring& value, Crc32& crc)
        {
            std::uint32_t length = 0;
            if (!ReadValue(reader, length, crc) || length > kMaxStoredStringChars)
            {
                SetLastError(ERROR_INVALID_DATA);
                return false;
            }
            value.resize(length);
            return ReadRaw(reader, value.data(), static_cast<std::size_t>(length) * sizeof(wchar_t)) &&
                (crc.Update(value.data(), static_cast<std::size_t>(length) * sizeof(wchar_t)), true);
        }

        [[nodiscard]] bool WriteIdentity(BufferedWriter& writer, const core::FileIdentity& identity, Crc32& crc)
        {
            const std::uint8_t valid = identity.valid ? 1 : 0;
            return WriteValue(writer, identity.volume_serial, crc) &&
                WriteValue(writer, valid, crc) &&
                WriteBytes(writer, identity.file_id.data(), identity.file_id.size(), crc);
        }

        [[nodiscard]] bool ReadIdentity(BufferedReader& reader, core::FileIdentity& identity, Crc32& crc)
        {
            std::uint8_t valid = 0;
            if (!ReadValue(reader, identity.volume_serial, crc) ||
                !ReadValue(reader, valid, crc) ||
                !ReadRaw(reader, identity.file_id.data(), identity.file_id.size()))
            {
                return false;
            }
            crc.Update(identity.file_id.data(), identity.file_id.size());
            identity.valid = valid != 0;
            return true;
        }

        [[nodiscard]] bool SamePathInsensitive(const std::wstring& left, const std::wstring& right)
        {
            const auto normalized_left = core::NormalizePath(left), normalized_right = core::NormalizePath(right);
            return normalized_left.size() == normalized_right.size() &&
                StartsWithInsensitive(normalized_left, normalized_right);
        }

        void SetError(DWORD* error, DWORD value) noexcept
        {
            if (error != nullptr)
            {
                *error = value;
            }
        }
    }

    const wchar_t* FastIndexHealthName(FastIndexHealth health) noexcept
    {
        switch (health)
        {
        case FastIndexHealth::Rebuilding:
            return L"Rebuilding";
        case FastIndexHealth::CatchingUp:
            return L"CatchingUp";
        case FastIndexHealth::Healthy:
            return L"Healthy";
        case FastIndexHealth::RebuildRequired:
            return L"RebuildRequired";
        case FastIndexHealth::Unavailable:
            return L"Unavailable";
        case FastIndexHealth::Corrupted:
            return L"Corrupted";
        default:
            return L"Unknown";
        }
    }

    std::size_t FastIndex::IdentityHash::operator()(const core::FileIdentity& identity) const noexcept
    {
        std::size_t value = std::hash<std::uint64_t>{}(identity.volume_serial);
        for (const std::uint8_t byte : identity.file_id)
        {
            value ^= std::hash<std::uint8_t>{}(byte) + static_cast<std::size_t>(0x9E3779B9u) +
                (value << 6) + (value >> 2);
        }
        value ^= std::hash<bool>{}(identity.valid) + static_cast<std::size_t>(0x9E3779B9u) +
            (value << 6) + (value >> 2);
        return value;
    }

    void FastIndex::AddExtensionIndexEntryUnlocked(const Link& link)
    {
        const std::wstring extension = FoldExtension(link.name);
        if (!extension.empty())
        {
            extension_index_[extension].insert(link.identity);
        }
    }

    void FastIndex::RemoveExtensionIndexEntryUnlocked(
        const core::FileIdentity& identity,
        const std::wstring& extension)
    {
        if (extension.empty())
        {
            return;
        }
        const auto bucket = extension_index_.find(extension);
        if (bucket == extension_index_.end())
        {
            return;
        }

        const auto links = links_.find(identity);
        if (links != links_.end())
        {
            const bool still_present = std::any_of(
                links->second.begin(),
                links->second.end(),
                [&extension](const Link& link)
                {
                    return FoldExtension(link.name) == extension;
                });
            if (still_present)
            {
                return;
            }
        }

        bucket->second.erase(identity);
        if (bucket->second.empty())
        {
            extension_index_.erase(bucket);
        }
    }

    void FastIndex::RebuildExtensionIndexUnlocked()
    {
        extension_index_.clear();
        for (auto& [identity, links] : links_)
        {
            (void)identity;
            for (Link& link : links)
            {
                link.folded_name = FoldCase(link.name);
                AddExtensionIndexEntryUnlocked(link);
            }
        }
    }

    void FastIndex::SetInternalExcludedRoots(std::vector<std::wstring> roots)
    {
        std::vector<std::wstring> normalized_roots;
        std::vector<InternalExcludedRootIdentity> root_identities;
        for (const std::wstring& root : roots)
        {
            if (root.empty())
            {
                continue;
            }

            const std::wstring normalized = core::NormalizePath(root);
            if (normalized.empty())
            {
                continue;
            }

            normalized_roots.push_back(normalized);

            // Resolve the exclusion root once, outside FastIndex::mutex_.
            // Incremental USN records carry stable parent identities, so the
            // catch-up path can classify service-owned records without doing
            // per-record filesystem IO or depending on a transient path
            // reconstruction result.
            core::UniqueHandle handle = core::OpenPathForMetadata(normalized, true);
            InternalExcludedRootIdentity root_identity;
            if (handle && core::ReadFileIdentity(handle.get(), root_identity.full_identity))
            {
                // USN identities are built from JournalState::volume_serial,
                // which comes from GetVolumeInformationW and is a DWORD.
                // FILE_ID_INFO exposes a 64-bit VolumeSerialNumber.  Use the
                // separately stored canonical 32-bit volume serial for the
                // InternalExcludedRoot comparison only. Do not overwrite
                // the complete FILE_ID_INFO-derived FileIdentity.
                const std::wstring volume_root = VolumeRootFor(normalized);
                DWORD volume_serial = static_cast<DWORD>(root_identity.full_identity.volume_serial);
                if (!volume_root.empty() && GetVolumeInformationW(
                        volume_root.c_str(),
                        nullptr,
                        0,
                        &volume_serial,
                        nullptr,
                        nullptr,
                        nullptr,
                        0))
                {
                    root_identity.canonical_volume_serial = volume_serial;
                }
                else
                {
                    root_identity.canonical_volume_serial = volume_serial;
                }
                root_identities.push_back(root_identity);
            }
        }

        std::scoped_lock lock(mutex_);
        internal_excluded_roots_ = std::move(normalized_roots);
        internal_excluded_root_identities_ = std::move(root_identities);
    }

    bool FastIndex::BuildInitial(
        const std::wstring& path,
        const UsnCancelPredicate& cancel,
        FastIndexStats* stats,
        const FastIndexProgressCallback& progress)
    {
        const auto started = std::chrono::steady_clock::now();
        std::scoped_lock lock(mutex_);
        links_.clear();
        extension_index_.clear();
        snapshot_ = {};
        stats_ = {};
        stats_.health = FastIndexHealth::Rebuilding;
        snapshot_.health = FastIndexHealth::Rebuilding;
        stats_.failure_stage.clear();

        const JournalState journal = QueryUsnJournal(path);
        if (!journal.available)
        {
            stats_.failure_stage = L"FSCTL_QUERY_USN_JOURNAL";
            snapshot_.volume_root = journal.volume_root;
            snapshot_.volume_serial = journal.volume_serial;
            snapshot_.health = FastIndexHealth::Unavailable;
            snapshot_.error = journal.error;
            stats_.health = snapshot_.health;
            stats_.error = journal.error;
            if (stats != nullptr)
            {
                *stats = stats_;
            }
            return false;
        }

        snapshot_.volume_root = journal.volume_root;
        snapshot_.volume_serial = journal.volume_serial;
        snapshot_.journal_id = journal.journal_id;
        snapshot_.first_usn = journal.first_usn;
        snapshot_.next_usn = journal.next_usn;
        snapshot_.lowest_valid_usn = journal.lowest_valid_usn;
        snapshot_.max_usn = journal.max_usn;
        snapshot_.last_usn = journal.next_usn;
        stats_.used_mft = true;

        core::UniqueHandle root_handle = core::OpenPathForMetadata(journal.volume_root, true);
        if (!root_handle || !core::ReadFileIdentity(root_handle.get(), snapshot_.root_identity))
        {
            const DWORD local_error = GetLastError();
            stats_.failure_stage = L"Volume Handle / root identity";
            snapshot_.health = FastIndexHealth::Unavailable;
            snapshot_.error = local_error;
            stats_.health = snapshot_.health;
            stats_.error = local_error;
            if (stats != nullptr)
            {
                *stats = stats_;
            }
            return false;
        }

        DWORD local_error = ERROR_SUCCESS;
        const auto mft_started = std::chrono::steady_clock::now();
        const bool enumerated = EnumerateUsnRecordsStreaming(
            journal.volume_root,
            [&](const UsnRecordSummary& record)
            {
                ++stats_.records_seen;
                ++stats_.mft_records;
                const auto tree_started = std::chrono::steady_clock::now();
                const bool applied = ApplyRecordUnlocked(record);
                stats_.parent_tree_build_seconds += std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - tree_started).count();
                if (progress)
                {
                    progress(FastIndexProgressStage::EnumeratingMft, stats_);
                }
                return applied;
            },
            cancel,
            nullptr,
            &local_error);
        stats_.mft_enumeration_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - mft_started).count();
        if (!enumerated)
        {
            stats_.failure_stage = L"FSCTL_ENUM_USN_DATA";
            snapshot_.health = local_error == ERROR_CANCELLED
                ? FastIndexHealth::RebuildRequired
                : FastIndexHealth::Unavailable;
            snapshot_.error = local_error;
            stats_.health = snapshot_.health;
            stats_.error = local_error;
            stats_.build_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            RecomputeStatsUnlocked();
            if (stats != nullptr)
            {
                *stats = stats_;
            }
            return false;
        }

        snapshot_.health = FastIndexHealth::CatchingUp;
        stats_.health = snapshot_.health;
        stats_.used_usn = true;
        const auto usn_started = std::chrono::steady_clock::now();
        const auto records_before_usn = stats_.records_seen;
        if (!CatchUpUnlocked(journal.volume_root, cancel, local_error, progress))
        {
            if (stats_.failure_stage.empty())
            {
                stats_.failure_stage = L"FSCTL_READ_USN_JOURNAL";
            }
            stats_.usn_records = stats_.records_seen - records_before_usn;
            stats_.usn_read_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - usn_started).count();
            snapshot_.error = local_error;
            stats_.health = snapshot_.health;
            stats_.error = local_error;
            stats_.build_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            RecomputeStatsUnlocked();
            if (stats != nullptr)
            {
                *stats = stats_;
            }
            return false;
        }

        stats_.usn_records = stats_.records_seen - records_before_usn;
        stats_.usn_read_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - usn_started).count();

        snapshot_.health = FastIndexHealth::Healthy;
        snapshot_.error = ERROR_SUCCESS;
        stats_.health = snapshot_.health;
        stats_.error = ERROR_SUCCESS;
        stats_.build_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        RecomputeStatsUnlocked();
        if (stats != nullptr)
        {
            *stats = stats_;
        }
        return true;
    }

    bool FastIndex::CatchUp(
        const std::wstring& path,
        const UsnCancelPredicate& cancel,
        FastIndexStats* stats,
        const FastIndexProgressCallback& progress)
    {
        std::scoped_lock lock(mutex_);
        const auto started = std::chrono::steady_clock::now();
        // This counter is a per-CatchUp dirty delta. The service uses it to
        // decide whether this operation changed user-visible index state and
        // therefore needs a persistent Save. Keeping the previous operation's
        // value here makes internal-only USN batches look dirty forever.
        stats_.user_mutations_applied = 0;
        const auto records_before_usn = stats_.records_seen;
        DWORD local_error = ERROR_SUCCESS;
        if (!snapshot_.root_identity.valid || snapshot_.volume_root.empty())
        {
            snapshot_.health = FastIndexHealth::RebuildRequired;
            snapshot_.error = ERROR_INVALID_STATE;
            stats_.health = snapshot_.health;
            stats_.error = snapshot_.error;
            if (stats != nullptr)
            {
                *stats = stats_;
            }
            return false;
        }
        snapshot_.health = FastIndexHealth::CatchingUp;
        stats_.health = snapshot_.health;
        const bool completed = CatchUpUnlocked(path, cancel, local_error, progress);
        stats_.usn_records = stats_.records_seen - records_before_usn;
        stats_.usn_read_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        if (!completed)
        {
            snapshot_.error = local_error;
            stats_.error = local_error;
        }
        else
        {
            snapshot_.health = FastIndexHealth::Healthy;
            snapshot_.error = ERROR_SUCCESS;
            stats_.health = snapshot_.health;
            stats_.error = ERROR_SUCCESS;
        }
        RecomputeStatsUnlocked();
        if (stats != nullptr)
        {
            *stats = stats_;
        }
        return completed;
    }

    bool FastIndex::CatchUpUnlocked(
        const std::wstring& path,
        const UsnCancelPredicate& cancel,
        DWORD& error,
        const FastIndexProgressCallback& progress)
    {
        const JournalState current = QueryUsnJournal(path);
        if (!current.available)
        {
            stats_.failure_stage = L"FSCTL_QUERY_USN_JOURNAL";
            snapshot_.health = FastIndexHealth::Unavailable;
            error = current.error;
            return false;
        }
        if (current.volume_serial != 0 && snapshot_.volume_serial != 0 &&
            current.volume_serial != snapshot_.volume_serial)
        {
            snapshot_.health = FastIndexHealth::RebuildRequired;
            error = ERROR_VOLUME_NOT_SUPPORTED;
            return false;
        }
        if (current.journal_id != snapshot_.journal_id)
        {
            snapshot_.health = FastIndexHealth::RebuildRequired;
            error = ERROR_JOURNAL_ENTRY_DELETED;
            return false;
        }
        if (snapshot_.last_usn < current.first_usn ||
            (current.lowest_valid_usn > 0 && snapshot_.last_usn < current.lowest_valid_usn))
        {
            snapshot_.health = FastIndexHealth::RebuildRequired;
            error = ERROR_JOURNAL_ENTRY_DELETED;
            return false;
        }
        if (snapshot_.last_usn > current.next_usn)
        {
            snapshot_.health = FastIndexHealth::RebuildRequired;
            error = ERROR_INVALID_DATA;
            return false;
        }

        snapshot_.first_usn = current.first_usn;
        snapshot_.next_usn = current.next_usn;
        snapshot_.lowest_valid_usn = current.lowest_valid_usn;
        snapshot_.max_usn = current.max_usn;

        const std::int64_t previous_last_usn = snapshot_.last_usn;
        const std::int64_t batch_target_usn = current.next_usn;
        stats_.catchup_start_usn = previous_last_usn;
        stats_.catchup_target_usn = batch_target_usn;
        stats_.catchup_completed_usn = previous_last_usn;
        stats_.catchup_batch_count = 0;

        if (progress)
        {
            progress(FastIndexProgressStage::CatchingUpUsn, stats_);
        }

        if (previous_last_usn == batch_target_usn)
        {
            snapshot_.health = FastIndexHealth::Healthy;
            return true;
        }

        UsnReadStats read_stats{};
        std::int64_t max_record_usn_applied = previous_last_usn;
        bool record_error = false;

        const bool read_ok = ReadUsnChangesStreaming(
            current.volume_root,
            snapshot_.journal_id,
            previous_last_usn,
            [&](const UsnRecordSummary& record)
            {
                ++stats_.records_seen;
                if (!ApplyRecordUnlocked(record))
                {
                    record_error = true;
                    return false;
                }
                if (record.usn > max_record_usn_applied)
                {
                    max_record_usn_applied = record.usn;
                }
                stats_.catchup_completed_usn = max_record_usn_applied;
                if (progress)
                {
                    progress(FastIndexProgressStage::CatchingUpUsn, stats_);
                }
                return !(cancel && cancel());
            },
            cancel,
            &read_stats,
            &error,
            batch_target_usn);

        if (!read_ok || record_error)
        {
            stats_.failure_stage = L"FSCTL_READ_USN_JOURNAL";
            if (max_record_usn_applied > previous_last_usn)
            {
                snapshot_.last_usn = max_record_usn_applied;
            }
            stats_.catchup_completed_usn = snapshot_.last_usn;
            if (error == ERROR_JOURNAL_ENTRY_DELETED)
            {
                snapshot_.health = FastIndexHealth::RebuildRequired;
            }
            else if (error == ERROR_CANCELLED)
            {
                snapshot_.health = FastIndexHealth::Rebuilding;
            }
            else
            {
                snapshot_.health = FastIndexHealth::Unavailable;
            }
            return false;
        }

        if (read_stats.next_usn < previous_last_usn)
        {
            snapshot_.health = FastIndexHealth::RebuildRequired;
            error = ERROR_INVALID_DATA;
            return false;
        }

        snapshot_.last_usn = (read_stats.next_usn >= batch_target_usn) ? batch_target_usn : read_stats.next_usn;
        snapshot_.health = (snapshot_.last_usn >= batch_target_usn) ? FastIndexHealth::Healthy : FastIndexHealth::CatchingUp;
        stats_.catchup_completed_usn = snapshot_.last_usn;
        stats_.catchup_batch_count = 1;
        return true;
    }

    bool FastIndex::IsInternalExcludedPathUnlocked(const Link& link) const
    {
        if (internal_excluded_roots_.empty())
        {
            return false;
        }

        // A USN record identifies its parent by FileReferenceNumber.  Use
        // that identity chain first so service-owned records remain
        // classifiable even when a parent link was not the canonical link or
        // a transient path reconstruction is unavailable.  This is an
        // in-memory walk only; it deliberately does not open any filesystem
        // path while FastIndex::mutex_ is held.
        const auto is_excluded_identity = [this](const core::FileIdentity& identity)
        {
            return std::any_of(
                internal_excluded_root_identities_.begin(),
                internal_excluded_root_identities_.end(),
                [&identity](const InternalExcludedRootIdentity& excluded)
                {
                    return excluded.full_identity.valid &&
                        excluded.full_identity.file_id == identity.file_id &&
                        excluded.canonical_volume_serial == static_cast<DWORD>(identity.volume_serial);
                });
        };

        if (!internal_excluded_root_identities_.empty())
        {
            if (is_excluded_identity(link.identity))
            {
                return true;
            }

            std::unordered_set<core::FileIdentity, IdentityHash> identity_visiting;
            core::FileIdentity current = link.parent_identity;
            std::size_t depth = 0;
            while (current.valid && depth++ <= 256 && identity_visiting.insert(current).second)
            {
                if (is_excluded_identity(current))
                {
                    return true;
                }
                if (SameCanonicalUsnIdentity(current, snapshot_.root_identity))
                {
                    break;
                }

                const auto parent_links = links_.find(current);
                if (parent_links == links_.end() || parent_links->second.empty())
                {
                    break;
                }
                current = parent_links->second.front().parent_identity;
            }
        }

        std::unordered_set<core::FileIdentity, IdentityHash> visiting;
        return IsInternalExcludedResolvedPathUnlocked(ResolveLinkPathUnlocked(link, visiting, 0));
    }

    bool FastIndex::IsInternalExcludedResolvedPathUnlocked(const std::wstring& full_path) const
    {
        if (internal_excluded_roots_.empty()) { return false; }
        const std::wstring normalized_path = core::NormalizePath(full_path);
        if (normalized_path.empty())
        {
            return false;
        }
        for (const std::wstring& root : internal_excluded_roots_)
        {
            if (normalized_path.size() >= root.size() && StartsWithInsensitive(normalized_path, root) &&
                (normalized_path.size() == root.size() || normalized_path[root.size()] == L'\\' ||
                    normalized_path[root.size()] == L'/'))
            {
                return true;
            }
        }
        return false;
    }

    bool FastIndex::ApplyRecordUnlocked(const UsnRecordSummary& record)
    {
        const core::FileIdentity identity = IdentityFromRecord(record, snapshot_.volume_serial, false);
        const core::FileIdentity parent = IdentityFromRecord(record, snapshot_.volume_serial, true);
        if (!identity.valid || !parent.valid || record.name.empty())
        {
            return true;
        }
        if (SameCanonicalUsnIdentity(identity, snapshot_.root_identity))
        {
            return true;
        }

        Link link;
        link.identity = identity;
        link.parent_identity = parent;
        link.name = record.name;
        link.attributes = record.attributes;
        link.last_usn = record.usn;

        const bool is_internal = IsInternalExcludedPathUnlocked(link);

        const DWORD reason = record.reason;
        if ((reason & USN_REASON_FILE_DELETE) != 0)
        {
            if (RemoveLinkUnlocked(link))
            {
                ++stats_.records_deleted;
                if (!is_internal)
                {
                    ++stats_.user_mutations_applied;
                }
            }
            ++stats_.records_applied;
            return true;
        }

        const bool rename_old = (reason & USN_REASON_RENAME_OLD_NAME) != 0;
        const bool rename_new = (reason & USN_REASON_RENAME_NEW_NAME) != 0;
        if (rename_old)
        {
            (void)RemoveLinkUnlocked(link);
            ++stats_.records_renamed;
            if (!is_internal)
            {
                ++stats_.user_mutations_applied;
            }
        }
        if (!rename_old || rename_new || (reason & USN_REASON_FILE_CREATE) != 0)
        {
            (void)AddOrUpdateLinkUnlocked(link);
            if (!is_internal)
            {
                ++stats_.user_mutations_applied;
            }
        }
        ++stats_.records_applied;
        return true;
    }

    bool FastIndex::AddOrUpdateLinkUnlocked(const Link& link)
    {
        const auto existing_identity = links_.find(link.identity);
        const bool new_identity = existing_identity == links_.end();
        auto& links = links_[link.identity];
        bool was_directory = false;
        for (const Link& existing : links)
        {
            was_directory = was_directory || (existing.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }
        for (Link& existing : links)
        {
            if (existing.parent_identity == link.parent_identity && existing.name == link.name)
            {
                if (existing.folded_name.empty()) { existing.folded_name = FoldCase(existing.name); }
                const bool old_directory = (existing.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                existing.attributes = link.attributes;
                existing.last_usn = link.last_usn;
                const bool new_directory = (existing.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (old_directory != new_directory)
                {
                    bool remains_directory = false;
                    for (const Link& current : links)
                    {
                        remains_directory = remains_directory ||
                            (current.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    }
                    if (new_directory && !was_directory)
                    {
                        ++stats_.directories;
                    }
                    else if (old_directory && !remains_directory && stats_.directories != 0)
                    {
                        --stats_.directories;
                    }
                    UpdateLiveCountStatsUnlocked();
                }
                return true;
            }
        }
        Link cached = link;
        cached.folded_name = FoldCase(cached.name);
        links.push_back(std::move(cached));
        AddExtensionIndexEntryUnlocked(link);
        ++stats_.links;
        if (new_identity)
        {
            ++stats_.unique_files;
        }
        if (!was_directory && (link.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            ++stats_.directories;
        }
        UpdateLiveCountStatsUnlocked();
        return true;
    }

    bool FastIndex::RemoveLinkUnlocked(const Link& link)
    {
        const auto it = links_.find(link.identity);
        if (it == links_.end())
        {
            return false;
        }
        auto& links = it->second;
        bool was_directory = false;
        for (const Link& existing : links)
        {
            was_directory = was_directory || (existing.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }
        const auto before = links.size();
        std::unordered_set<std::wstring> removed_extensions;
        for (const Link& existing : links)
        {
            if (existing.parent_identity == link.parent_identity && existing.name == link.name)
            {
                removed_extensions.insert(FoldExtension(existing.name));
            }
        }
        links.erase(
            std::remove_if(
                links.begin(),
                links.end(),
                [&](const Link& existing)
                {
                    return existing.parent_identity == link.parent_identity && existing.name == link.name;
                }),
            links.end());
        const bool removed = before != links.size();
        if (removed)
        {
            for (const std::wstring& extension : removed_extensions)
            {
                RemoveExtensionIndexEntryUnlocked(link.identity, extension);
            }
            stats_.links = stats_.links >= before - links.size()
                ? stats_.links - (before - links.size())
                : 0;
            bool remains_directory = false;
            for (const Link& existing : links)
            {
                remains_directory = remains_directory || (existing.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            }
            if (was_directory && !remains_directory && stats_.directories != 0)
            {
                --stats_.directories;
            }
        }
        if (links.empty())
        {
            links_.erase(it);
            if (removed && stats_.unique_files != 0)
            {
                --stats_.unique_files;
            }
        }
        if (removed)
        {
            UpdateLiveCountStatsUnlocked();
        }
        return removed;
    }

    void FastIndex::UpdateLiveCountStatsUnlocked()
    {
        stats_.files = stats_.unique_files >= stats_.directories
            ? stats_.unique_files - stats_.directories
            : 0;
    }

    void FastIndex::RecomputeStatsUnlocked()
    {
        stats_.links = 0;
        stats_.unique_files = links_.size();
        stats_.directories = 0;
        stats_.resident_bytes = links_.size() * sizeof(Link);
        for (const auto& [identity, links] : links_)
        {
            bool is_directory = false;
            for (const Link& link : links)
            {
                ++stats_.links;
                stats_.resident_bytes += link.name.capacity() * sizeof(wchar_t);
                stats_.resident_bytes += link.folded_name.capacity() * sizeof(wchar_t);
                is_directory = is_directory || (link.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            }
            if (is_directory)
            {
                ++stats_.directories;
            }
        }
        stats_.files = stats_.unique_files >= stats_.directories
            ? stats_.unique_files - stats_.directories
            : 0;
        stats_.health = snapshot_.health;
        stats_.error = snapshot_.error;
    }

    std::wstring FastIndex::ResolveLinkPathUnlocked(
        const Link& link,
        std::unordered_set<core::FileIdentity, IdentityHash>& visiting,
        std::size_t depth) const
    {
        if (depth > 256)
        {
            return {};
        }
        if (SameCanonicalUsnIdentity(link.identity, snapshot_.root_identity))
        {
            return snapshot_.volume_root;
        }
        if (!visiting.insert(link.identity).second)
        {
            return {};
        }

        std::wstring parent_path;
        if (SameCanonicalUsnIdentity(link.parent_identity, snapshot_.root_identity))
        {
            parent_path = snapshot_.volume_root;
        }
        else
        {
            parent_path = ResolveIdentityPathUnlocked(link.parent_identity, visiting, depth + 1);
        }
        visiting.erase(link.identity);
        if (parent_path.empty())
        {
            return {};
        }
        return core::JoinPath(parent_path, link.name);
    }

    std::wstring FastIndex::ResolveIdentityPathUnlocked(
        const core::FileIdentity& identity,
        std::unordered_set<core::FileIdentity, IdentityHash>& visiting,
        std::size_t depth) const
    {
        if (SameCanonicalUsnIdentity(identity, snapshot_.root_identity))
        {
            return snapshot_.volume_root;
        }
        const auto it = links_.find(identity);
        if (it == links_.end() || it->second.empty())
        {
            return {};
        }
        return ResolveLinkPathUnlocked(it->second.front(), visiting, depth);
    }

    std::vector<FastSearchResult> FastIndex::Search(
        const FastSearchOptions& options,
        FastSearchTiming* timing) const
    {
        if (timing != nullptr)
        {
            MarkFastSearchTiming(timing->lock_wait_start_us, timing->request_received);
        }
        std::scoped_lock lock(mutex_);
        if (timing != nullptr)
        {
            MarkFastSearchTiming(timing->lock_acquired_us, timing->request_received);
            MarkFastSearchTiming(timing->search_start_us, timing->request_received);
        }

        const std::wstring query = FoldCase(options.text);
        const std::wstring extension = FoldCase(options.extension);
        const std::wstring path_prefix = options.path_prefix.empty()
            ? std::wstring{}
            : core::NormalizePath(options.path_prefix);
        std::wstring direct_parent = path_prefix;
        while (direct_parent.size() > 3 && direct_parent.back() == L'\\') { direct_parent.pop_back(); }

        struct ResolvedCandidate
        {
            const Link* link{};
            std::wstring full_path;
        };

        std::vector<const Link*> candidate_links;
        std::vector<ResolvedCandidate> resolved_candidates;
        std::vector<FastSearchResult> results;

        // Name/extension predicates are cheap and do not require path
        // reconstruction.  Keep a per-query parent-path cache only for the
        // candidates that survive those predicates; the cache is discarded
        // with this search and does not change the persistent index shape.
        const auto normalized_volume_root = core::NormalizePath(snapshot_.volume_root);
        const bool path_prefix_is_volume_root = !path_prefix.empty() &&
            path_prefix.size() == normalized_volume_root.size() && StartsWithInsensitive(path_prefix, normalized_volume_root);

        std::unordered_map<core::FileIdentity, std::wstring, IdentityHash> resolved_parent_paths;
        std::unordered_set<core::FileIdentity, IdentityHash> resolving_parents;
        std::function<const std::wstring*(const core::FileIdentity&)> resolve_parent_path;
        resolve_parent_path = [&](const core::FileIdentity& identity) -> const std::wstring*
        {
            if (SameCanonicalUsnIdentity(identity, snapshot_.root_identity))
            {
                return &snapshot_.volume_root;
            }

            const auto cached = resolved_parent_paths.find(identity);
            if (cached != resolved_parent_paths.end())
            {
                return cached->second.empty() ? nullptr : &cached->second;
            }

            if (path_prefix.empty() || path_prefix_is_volume_root)
            {
                std::unordered_set<core::FileIdentity, IdentityHash> visiting;
                auto path = ResolveIdentityPathUnlocked(identity, visiting, 1);
                const auto stored = resolved_parent_paths.emplace(identity, std::move(path));
                return stored.first->second.empty() ? nullptr : &stored.first->second;
            }

            resolved_parent_paths.emplace(identity, std::wstring{});
            if (!resolving_parents.insert(identity).second)
            {
                return nullptr;
            }

            const auto links = links_.find(identity);
            if (links == links_.end() || links->second.empty())
            {
                resolving_parents.erase(identity);
                return nullptr;
            }

            const Link& canonical = links->second.front();
            const std::wstring* parent_path = resolve_parent_path(canonical.parent_identity);
            if (parent_path == nullptr || parent_path->empty())
            {
                resolving_parents.erase(identity);
                return nullptr;
            }

            const std::wstring computed = core::JoinPath(*parent_path, canonical.name);
            resolving_parents.erase(identity);
            auto resolved = resolved_parent_paths.find(identity);
            if (resolved == resolved_parent_paths.end())
            {
                return nullptr;
            }
            resolved->second = computed;
            return resolved->second.empty() ? nullptr : &resolved->second;
        };


        const auto finish_timing = [&]()
        {
            if (timing != nullptr)
            {
                timing->result_count.store(results.size(), std::memory_order_release);
                MarkFastSearchTiming(timing->search_end_us, timing->request_received);
            }
        };

        if (timing != nullptr)
        {
            MarkFastSearchTiming(timing->candidate_filter_start_us, timing->request_received);
        }

        const auto append_matching_links = [&](const std::vector<Link>& links)
        {
            for (const Link& link : links)
            {
                if ((link.attributes & options.excluded_attributes) != 0) { continue; }
                const std::wstring& name = link.folded_name;
                const bool text_match = query.empty() ||
                    (options.prefix ? name.starts_with(query) : name.find(query) != std::wstring::npos);
                if (!text_match)
                {
                    continue;
                }
                if (!extension.empty())
                {
                    const auto dot = name.find_last_of(L'.');
                    const auto separator = name.find_last_of(L"\\/");
                    if (dot == std::wstring::npos || dot == 0 || (separator != std::wstring::npos && dot < separator) ||
                        std::wstring_view(name).substr(dot) != extension) { continue; }
                }
                candidate_links.push_back(&link);
            }
        };

        if (!extension.empty() && options.use_derived_extension_index)
        {
            const auto bucket = extension_index_.find(extension);
            if (bucket != extension_index_.end())
            {
                for (const core::FileIdentity& identity : bucket->second)
                {
                    const auto links = links_.find(identity);
                    if (links != links_.end())
                    {
                        append_matching_links(links->second);
                    }
                }
            }
        }
        else
        {
            for (const auto& [identity, links] : links_)
            {
                (void)identity;
                append_matching_links(links);
            }
        }

        if (timing != nullptr)
        {
            MarkFastSearchTiming(timing->candidate_filter_end_us, timing->request_received);
            MarkFastSearchTiming(timing->path_resolution_start_us, timing->request_received);
        }

        resolved_candidates.reserve(options.limit ? (std::min)(candidate_links.size(), options.limit) : candidate_links.size());
        for (const Link* link : candidate_links)
        {
            std::wstring full_path;
            if (SameCanonicalUsnIdentity(link->identity, snapshot_.root_identity))
            {
                if (options.direct_children) { continue; }
                full_path = snapshot_.volume_root;
            }
            else
            {
                const std::wstring* parent_path = resolve_parent_path(link->parent_identity);
                if (parent_path == nullptr || parent_path->empty()) { continue; }
                if (options.direct_children && (direct_parent.empty() ||
                    CompareStringOrdinal(parent_path->c_str(), static_cast<int>(parent_path->size()),
                        direct_parent.c_str(), static_cast<int>(direct_parent.size()), TRUE) != CSTR_EQUAL))
                { continue; }
                full_path = core::JoinPath(*parent_path, link->name);
                if (!path_prefix.empty() && !path_prefix_is_volume_root &&
                    !StartsWithInsensitive(*parent_path, path_prefix) && !StartsWithInsensitive(full_path, path_prefix))
                { continue; }
            }
            if (IsInternalExcludedResolvedPathUnlocked(full_path))
            {
                continue;
            }
            resolved_candidates.push_back(ResolvedCandidate{ link, std::move(full_path) });
            if (options.limit && resolved_candidates.size() >= options.limit) { break; }
        }

        if (timing != nullptr)
        {
            MarkFastSearchTiming(timing->path_resolution_end_us, timing->request_received);
            MarkFastSearchTiming(timing->result_construction_start_us, timing->request_received);
        }

        results.reserve(std::min<std::size_t>(
            resolved_candidates.size(),
            options.limit == 0 ? resolved_candidates.size() : options.limit));
        for (const ResolvedCandidate& candidate : resolved_candidates)
        {
            const Link& link = *candidate.link;
            FastSearchResult result;
            result.identity = link.identity;
            result.parent_identity = link.parent_identity;
            result.name = link.name;
            result.full_path = candidate.full_path;
            result.attributes = link.attributes;
            result.is_directory = (link.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            result.last_usn = link.last_usn;
            results.push_back(std::move(result));
            if (options.limit != 0 && results.size() >= options.limit)
            {
                if (timing != nullptr)
                {
                    MarkFastSearchTiming(timing->result_construction_end_us, timing->request_received);
                }
                finish_timing();
                return results;
            }
        }

        if (timing != nullptr)
        {
            MarkFastSearchTiming(timing->result_construction_end_us, timing->request_received);
            MarkFastSearchTiming(timing->sorting_start_us, timing->request_received);
        }

        std::sort(
            results.begin(),
            results.end(),
            [](const FastSearchResult& left, const FastSearchResult& right)
            {
                return CompareStringOrdinal(left.full_path.data(), static_cast<int>(left.full_path.size()),
                    right.full_path.data(), static_cast<int>(right.full_path.size()), TRUE) == CSTR_LESS_THAN;
            });
        if (timing != nullptr)
        {
            MarkFastSearchTiming(timing->sorting_end_us, timing->request_received);
        }
        finish_timing();
        return results;
    }

    bool FastIndex::Save(const std::wstring& path, DWORD* error) const
    {
        SetError(error, ERROR_SUCCESS);
        std::scoped_lock lock(mutex_);
        if (!snapshot_.root_identity.valid || snapshot_.volume_serial == 0 || path.empty())
        {
            SetError(error, ERROR_INVALID_STATE);
            return false;
        }

        std::uint64_t link_count = 0;
        for (const auto& [identity, links] : links_)
        {
            link_count += links.size();
        }
        if (link_count > kMaxStoredLinks)
        {
            SetError(error, ERROR_FILE_TOO_LARGE);
            return false;
        }

        const std::wstring temp_path = path + L".tmp." + std::to_wstring(GetCurrentProcessId());
        CREATEFILE3_EXTENDED_PARAMETERS parameters{};
        parameters.dwSize = sizeof(parameters);
        parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        core::UniqueHandle file(CreateFile3(
            temp_path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            CREATE_ALWAYS,
            &parameters));
        if (!file)
        {
            SetError(error, GetLastError());
            return false;
        }

        Crc32 crc;
        StoreHeader header;
        header.magic = kStoreMagic;
        header.version = kStoreVersion;
        header.volume_serial = snapshot_.volume_serial;
        header.journal_id = snapshot_.journal_id;
        header.first_usn = snapshot_.first_usn;
        header.next_usn = snapshot_.next_usn;
        header.lowest_valid_usn = snapshot_.lowest_valid_usn;
        header.max_usn = snapshot_.max_usn;
        header.last_usn = snapshot_.last_usn;
        header.link_count = link_count;

        BufferedWriter writer(file.get(), 1024 * 1024);
        bool ok = WriteValue(writer, header, crc) &&
            WriteIdentity(writer, snapshot_.root_identity, crc) &&
            WriteString(writer, snapshot_.volume_root, crc);
        if (ok)
        {
            for (const auto& [identity, links] : links_)
            {
                for (const Link& link : links)
                {
                    ok = WriteIdentity(writer, link.identity, crc) &&
                        WriteIdentity(writer, link.parent_identity, crc) &&
                        WriteValue(writer, link.attributes, crc) &&
                        WriteValue(writer, link.last_usn, crc) &&
                        WriteString(writer, link.name, crc);
                    if (!ok)
                    {
                        break;
                    }
                }
                if (!ok)
                {
                    break;
                }
            }
        }

        const std::uint32_t checksum = crc.Value();
        if (ok)
        {
            ok = WriteValue(writer, checksum, crc) && writer.Flush();
        }
        const DWORD write_error = ok ? ERROR_SUCCESS : GetLastError();
        file.reset();
        if (!ok)
        {
            DeleteFile2W(temp_path.c_str(), 0);
            SetError(error, write_error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : write_error);
            return false;
        }

        if (!MoveFileExW(temp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const DWORD move_error = GetLastError();
            DeleteFile2W(temp_path.c_str(), 0);
            SetError(error, move_error);
            return false;
        }
        return true;
    }

    bool FastIndex::ValidateStoreFile(const std::wstring& path, DWORD* error)
    {
        SetError(error, ERROR_SUCCESS);
        CREATEFILE3_EXTENDED_PARAMETERS parameters{};
        parameters.dwSize = sizeof(parameters);
        parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        core::UniqueHandle file(CreateFile3(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            OPEN_EXISTING,
            &parameters));
        if (!file)
        {
            SetError(error, GetLastError());
            return false;
        }

        BufferedReader reader(file.get());
        Crc32 crc;
        StoreHeader header;
        core::FileIdentity root_identity;
        std::wstring stored_root;
        if (!ReadValue(reader, header, crc) ||
            header.magic != kStoreMagic ||
            header.version != kStoreVersion ||
            header.link_count > kMaxStoredLinks ||
            !ReadIdentity(reader, root_identity, crc) ||
            !ReadString(reader, stored_root, crc))
        {
            SetError(error, ERROR_INVALID_DATA);
            return false;
        }
        return true;
    }

    bool FastIndex::Load(const std::wstring& path, const std::wstring& expected_root, DWORD* error)
    {
        SetError(error, ERROR_SUCCESS);
        const auto fail = [this, error](const DWORD value, const FastIndexHealth health)
        {
            std::scoped_lock lock(mutex_);
            if (!snapshot_.root_identity.valid || snapshot_.volume_root.empty())
            {
                snapshot_.health = health;
                snapshot_.error = value;
                stats_.health = health;
                stats_.error = value;
            }
            SetError(error, value);
            return false;
        };
        try
        {
            CREATEFILE3_EXTENDED_PARAMETERS parameters{};
            parameters.dwSize = sizeof(parameters);
            parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            core::UniqueHandle file(CreateFile3(path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, &parameters));
            if (!file)
            {
                const DWORD value = GetLastError();
                return fail(value, value == ERROR_FILE_NOT_FOUND || value == ERROR_PATH_NOT_FOUND
                    ? FastIndexHealth::RebuildRequired : FastIndexHealth::Unavailable);
            }

            BufferedReader reader(file.get());
            Crc32 crc;
            StoreHeader header;
            core::FileIdentity root_identity;
            std::wstring stored_root;
            if (!ReadValue(reader, header, crc) || header.magic != kStoreMagic || header.version != kStoreVersion ||
                header.link_count > kMaxStoredLinks || !ReadIdentity(reader, root_identity, crc) ||
                !ReadString(reader, stored_root, crc))
            { return fail(ERROR_INVALID_DATA, FastIndexHealth::Corrupted); }

            const std::wstring expected_volume = expected_root.empty() ? stored_root : VolumeRootFor(expected_root);
            if (expected_volume.empty() || !SamePathInsensitive(stored_root, expected_volume))
            { return fail(ERROR_VOLUME_NOT_SUPPORTED, FastIndexHealth::RebuildRequired); }
            DWORD volume_serial = 0;
            if (!GetVolumeInformationW(expected_volume.c_str(), nullptr, 0, &volume_serial, nullptr, nullptr, nullptr, 0))
            { return fail(GetLastError(), FastIndexHealth::RebuildRequired); }
            if (header.volume_serial != 0 && volume_serial != 0 && header.volume_serial != volume_serial)
            { return fail(ERROR_VOLUME_NOT_SUPPORTED, FastIndexHealth::RebuildRequired); }
            if (header.last_usn > header.next_usn)
            { return fail(ERROR_INVALID_DATA, FastIndexHealth::Corrupted); }

            FastIndex prepared;
            for (std::uint64_t i = 0; i < header.link_count; ++i)
            {
                Link link;
                if (!ReadIdentity(reader, link.identity, crc) || !ReadIdentity(reader, link.parent_identity, crc) ||
                    !ReadValue(reader, link.attributes, crc) || !ReadValue(reader, link.last_usn, crc) ||
                    !ReadString(reader, link.name, crc))
                { return fail(ERROR_INVALID_DATA, FastIndexHealth::Corrupted); }
                prepared.links_[link.identity].push_back(std::move(link));
            }
            std::uint32_t stored_checksum = 0;
            if (!ReadRaw(reader, &stored_checksum, sizeof(stored_checksum)) || stored_checksum != crc.Value())
            { return fail(ERROR_CRC, FastIndexHealth::Corrupted); }

            prepared.snapshot_.volume_root = std::move(stored_root);
            prepared.snapshot_.volume_serial = header.volume_serial;
            prepared.snapshot_.journal_id = header.journal_id;
            prepared.snapshot_.first_usn = header.first_usn;
            prepared.snapshot_.next_usn = header.next_usn;
            prepared.snapshot_.lowest_valid_usn = header.lowest_valid_usn;
            prepared.snapshot_.max_usn = header.max_usn;
            prepared.snapshot_.last_usn = header.last_usn;
            prepared.snapshot_.root_identity = root_identity;
            prepared.snapshot_.health = FastIndexHealth::Healthy;
            prepared.RebuildExtensionIndexUnlocked();
            prepared.RecomputeStatsUnlocked();
            {
                std::scoped_lock lock(mutex_);
                links_.swap(prepared.links_);
                extension_index_.swap(prepared.extension_index_);
                std::swap(snapshot_, prepared.snapshot_);
                std::swap(stats_, prepared.stats_);
            }
            return true;
        }
        catch (const std::bad_alloc&)
        { return fail(ERROR_NOT_ENOUGH_MEMORY, FastIndexHealth::Unavailable); }
        catch (...)
        { return fail(ERROR_GEN_FAILURE, FastIndexHealth::Unavailable); }
    }

    FastIndexSnapshot FastIndex::Snapshot() const
    {
        std::scoped_lock lock(mutex_);
        return snapshot_;
    }

    FastIndexStats FastIndex::Stats() const
    {
        std::scoped_lock lock(mutex_);
        FastIndexStats result = stats_;
        result.health = snapshot_.health;
        result.error = snapshot_.error;
        return result;
    }

    bool FastIndex::IsLoaded() const
    {
        std::scoped_lock lock(mutex_);
        return snapshot_.root_identity.valid && !snapshot_.volume_root.empty();
    }

    void FastIndex::Clear()
    {
        std::scoped_lock lock(mutex_);
        links_.clear();
        extension_index_.clear();
        snapshot_ = {};
        stats_ = {};
    }
}
