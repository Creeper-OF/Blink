#include "Blink/Index/UsnJournal.h"

#include "Blink/Core/FileTypes.h"

#include <fileapi.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <vector>

#pragma comment(lib, "Kernel32.lib")

namespace blink::index
{
    namespace
    {
        [[nodiscard]] std::wstring VolumeRootFor(const std::wstring& path)
        {
            wchar_t buffer[32768]{};
            if (!GetVolumePathNameW(path.c_str(), buffer, static_cast<DWORD>(std::size(buffer))))
            {
                return {};
            }
            return buffer;
        }

        [[nodiscard]] blink::core::UniqueHandle OpenVolume(const std::wstring& volume_root)
        {
            if (volume_root.size() < 2 || volume_root[1] != L':')
            {
                SetLastError(ERROR_INVALID_NAME);
                return {};
            }

            const std::wstring volume_path = L"\\\\.\\" + volume_root.substr(0, 2);
            CREATEFILE3_EXTENDED_PARAMETERS parameters{};
            parameters.dwSize = sizeof(parameters);
            parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
            parameters.dwFileFlags = 0;
            const HANDLE handle = CreateFile3(
                volume_path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                OPEN_EXISTING,
                &parameters);
            return blink::core::UniqueHandle(handle);
        }

        [[nodiscard]] bool ReadRecord(
            const BYTE* buffer,
            std::size_t remaining,
            UsnRecordSummary& result,
            std::size_t& record_length)
        {
            if (remaining < sizeof(USN_RECORD_COMMON_HEADER))
            {
                return false;
            }

            const auto* common = reinterpret_cast<const USN_RECORD_COMMON_HEADER*>(buffer);
            if (common->RecordLength < sizeof(USN_RECORD_COMMON_HEADER) || common->RecordLength > remaining)
            {
                return false;
            }

            result = {};
            result.major_version = common->MajorVersion;
            record_length = common->RecordLength;
            if (common->MajorVersion == 2 && common->RecordLength >= sizeof(USN_RECORD_V2))
            {
                const auto* record = reinterpret_cast<const USN_RECORD_V2*>(buffer);
                result.usn = record->Usn;
                result.file_reference = record->FileReferenceNumber;
                result.parent_reference = record->ParentFileReferenceNumber;
                result.reason = record->Reason;
                result.attributes = record->FileAttributes;
                if (static_cast<std::size_t>(record->FileNameOffset) + record->FileNameLength <= record->RecordLength)
                {
                    result.name.assign(
                        reinterpret_cast<const wchar_t*>(buffer + record->FileNameOffset),
                        record->FileNameLength / sizeof(wchar_t));
                }
                return true;
            }

            if (common->MajorVersion == 3 && common->RecordLength >= sizeof(USN_RECORD_V3))
            {
                const auto* record = reinterpret_cast<const USN_RECORD_V3*>(buffer);
                result.usn = record->Usn;
                std::copy(
                    std::begin(record->FileReferenceNumber.Identifier),
                    std::end(record->FileReferenceNumber.Identifier),
                    result.file_reference_128.begin());
                std::copy(
                    std::begin(record->ParentFileReferenceNumber.Identifier),
                    std::end(record->ParentFileReferenceNumber.Identifier),
                    result.parent_reference_128.begin());
                result.reason = record->Reason;
                result.attributes = record->FileAttributes;
                if (static_cast<std::size_t>(record->FileNameOffset) + record->FileNameLength <= record->RecordLength)
                {
                    result.name.assign(
                        reinterpret_cast<const wchar_t*>(buffer + record->FileNameOffset),
                        record->FileNameLength / sizeof(wchar_t));
                }
                return true;
            }

            // V4 records are range-tracking records and do not have the same filename layout.
            // They are intentionally reported as unsupported for the path index.
            return false;
        }

        void SetError(DWORD* error, DWORD value) noexcept
        {
            if (error != nullptr)
            {
                *error = value;
            }
        }

        struct ParseResult
        {
            bool valid{ true };
            bool stopped{};
            std::uint64_t records{};
        };

        [[nodiscard]] ParseResult ParseRecordBuffer(
            const BYTE* buffer,
            DWORD bytes,
            std::size_t first_offset,
            const UsnRecordCallback& callback)
        {
            ParseResult result;
            if (bytes <= first_offset)
            {
                return result;
            }

            std::size_t offset = first_offset;
            while (offset < bytes)
            {
                UsnRecordSummary record;
                std::size_t record_length = 0;
                if (!ReadRecord(buffer + offset, bytes - offset, record, record_length))
                {
                    result.valid = false;
                    return result;
                }

                ++result.records;
                if (callback && !callback(record))
                {
                    result.stopped = true;
                    return result;
                }
                offset += record_length;
            }
            return result;
        }

        [[nodiscard]] bool IsEndOfEnumerationError(DWORD error) noexcept
        {
            return error == ERROR_HANDLE_EOF || error == ERROR_NO_MORE_FILES;
        }

        [[nodiscard]] bool IsNtfs(const std::wstring& file_system) noexcept
        {
            return _wcsicmp(file_system.c_str(), L"NTFS") == 0;
        }

        [[nodiscard]] bool IsRefs(const std::wstring& file_system) noexcept
        {
            return _wcsicmp(file_system.c_str(), L"ReFS") == 0;
        }
    }

    JournalState QueryUsnJournal(const std::wstring& path)
    {
        JournalState result;
        result.volume_root = VolumeRootFor(path);
        if (result.volume_root.empty())
        {
            result.error = GetLastError();
            return result;
        }

        wchar_t file_system_name[MAX_PATH]{};
        DWORD volume_serial = 0;
        if (GetVolumeInformationW(
                result.volume_root.c_str(),
                nullptr,
                0,
                &volume_serial,
                nullptr,
                nullptr,
                file_system_name,
                static_cast<DWORD>(std::size(file_system_name))))
        {
            result.file_system = file_system_name;
            result.volume_serial = volume_serial;
        }

        blink::core::UniqueHandle volume = OpenVolume(result.volume_root);
        if (!volume)
        {
            result.error = GetLastError();
            return result;
        }

        USN_JOURNAL_DATA_V1 data{};
        DWORD returned = 0;
        if (!DeviceIoControl(
                volume.get(),
                FSCTL_QUERY_USN_JOURNAL,
                nullptr,
                0,
                &data,
                sizeof(data),
                &returned,
                nullptr))
        {
            result.error = GetLastError();
            return result;
        }

        result.available = true;
        result.error = ERROR_SUCCESS;
        result.journal_id = data.UsnJournalID;
        result.first_usn = data.FirstUsn;
        result.next_usn = data.NextUsn;
        result.lowest_valid_usn = data.LowestValidUsn;
        result.max_usn = data.MaxUsn;
        result.maximum_size = data.MaximumSize;
        result.allocation_delta = data.AllocationDelta;
        result.min_supported_major = data.MinSupportedMajorVersion;
        result.max_supported_major = data.MaxSupportedMajorVersion;
        return result;
    }

    bool EnumerateUsnRecordsStreaming(
        const std::wstring& path,
        const UsnRecordCallback& callback,
        const UsnCancelPredicate& cancel,
        UsnReadStats* stats,
        DWORD* error)
    {
        SetError(error, ERROR_SUCCESS);
        if (stats != nullptr)
        {
            *stats = {};
        }

        const JournalState journal = QueryUsnJournal(path);
        if (!journal.available)
        {
            SetError(error, journal.error);
            return false;
        }
        if (!IsNtfs(journal.file_system) && !IsRefs(journal.file_system))
        {
            SetError(error, ERROR_NOT_SUPPORTED);
            return false;
        }

        blink::core::UniqueHandle volume = OpenVolume(journal.volume_root);
        if (!volume)
        {
            SetError(error, GetLastError());
            return false;
        }

        MFT_ENUM_DATA_V0 ntfs_input{};
        MFT_ENUM_DATA_V1 refs_input{};
        void* input_ptr = nullptr;
        DWORD input_size = 0;
        DWORDLONG* start_file_ref_ptr = nullptr;
        if (IsNtfs(journal.file_system))
        {
            ntfs_input.LowUsn = 0;
            ntfs_input.HighUsn = MAXLONGLONG;
            input_ptr = &ntfs_input;
            input_size = sizeof(ntfs_input);
            start_file_ref_ptr = &ntfs_input.StartFileReferenceNumber;
        }
        else
        {
            refs_input.LowUsn = 0;
            refs_input.HighUsn = MAXLONGLONG;
            refs_input.MinMajorVersion = 2;
            refs_input.MaxMajorVersion = 3;
            input_ptr = &refs_input;
            input_size = sizeof(refs_input);
            start_file_ref_ptr = &refs_input.StartFileReferenceNumber;
        }

        std::vector<BYTE> buffer(1024 * 1024);
        DWORD returned = 0;
        for (;;)
        {
            if (cancel && cancel())
            {
                SetError(error, ERROR_CANCELLED);
                return false;
            }

            if (!DeviceIoControl(
                    volume.get(),
                    FSCTL_ENUM_USN_DATA,
                    input_ptr,
                    input_size,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    &returned,
                    nullptr))
            {
                const DWORD device_error = GetLastError();
                if (IsEndOfEnumerationError(device_error))
                {
                    if (stats != nullptr)
                    {
                        stats->next_usn = journal.next_usn;
                    }
                    return true;
                }
                SetError(error, device_error);
                return false;
            }

            if (returned < sizeof(DWORDLONG))
            {
                if (stats != nullptr)
                {
                    stats->next_usn = journal.next_usn;
                }
                return true;
            }

            const DWORDLONG next_reference = *reinterpret_cast<const DWORDLONG*>(buffer.data());
            const ParseResult parsed = ParseRecordBuffer(buffer.data(), returned, sizeof(DWORDLONG), callback);
            if (!parsed.valid)
            {
                SetError(error, ERROR_INVALID_DATA);
                return false;
            }
            if (stats != nullptr)
            {
                stats->records += parsed.records;
            }
            if (parsed.stopped)
            {
                SetError(error, ERROR_CANCELLED);
                return false;
            }
            if (next_reference == *start_file_ref_ptr && parsed.records == 0)
            {
                SetError(error, ERROR_INVALID_DATA);
                return false;
            }
            *start_file_ref_ptr = next_reference;
        }
    }

    std::vector<UsnRecordSummary> EnumerateUsnRecords(const std::wstring& path, std::size_t max_records, DWORD* error)
    {
        std::vector<UsnRecordSummary> records;
        DWORD local_error = ERROR_SUCCESS;
        const bool completed = EnumerateUsnRecordsStreaming(
            path,
            [&](const UsnRecordSummary& record)
            {
                records.push_back(record);
                return max_records == 0 || records.size() < max_records;
            },
            {},
            nullptr,
            &local_error);

        if (!completed && local_error == ERROR_CANCELLED && max_records != 0 && records.size() >= max_records)
        {
            local_error = ERROR_SUCCESS;
        }
        SetError(error, local_error);
        return records;
    }

    bool ReadUsnChangesStreaming(
        const std::wstring& path,
        std::uint64_t expected_journal_id,
        std::int64_t start_usn,
        const UsnRecordCallback& callback,
        const UsnCancelPredicate& cancel,
        UsnReadStats* stats,
        DWORD* error,
        std::int64_t target_usn)
    {
        SetError(error, ERROR_SUCCESS);
        if (stats != nullptr)
        {
            *stats = {};
        }

        const JournalState journal = QueryUsnJournal(path);
        if (!journal.available)
        {
            SetError(error, journal.error);
            return false;
        }
        if (!IsNtfs(journal.file_system) && !IsRefs(journal.file_system))
        {
            SetError(error, ERROR_NOT_SUPPORTED);
            return false;
        }
        if (expected_journal_id != 0 && expected_journal_id != journal.journal_id)
        {
            SetError(error, ERROR_JOURNAL_ENTRY_DELETED);
            return false;
        }
        if (start_usn < journal.first_usn || start_usn > journal.next_usn)
        {
            SetError(error, ERROR_JOURNAL_ENTRY_DELETED);
            return false;
        }

        blink::core::UniqueHandle volume = OpenVolume(journal.volume_root);
        if (!volume)
        {
            SetError(error, GetLastError());
            return false;
        }

        READ_USN_JOURNAL_DATA_V0 ntfs_input{};
        READ_USN_JOURNAL_DATA_V1 refs_input{};
        void* input_ptr = nullptr;
        DWORD input_size = 0;
        USN* start_usn_ptr = nullptr;
        if (IsNtfs(journal.file_system))
        {
            ntfs_input.StartUsn = start_usn;
            ntfs_input.ReasonMask = 0xFFFFFFFF;
            ntfs_input.ReturnOnlyOnClose = FALSE;
            ntfs_input.Timeout = 0;
            ntfs_input.BytesToWaitFor = 0;
            ntfs_input.UsnJournalID = journal.journal_id;
            input_ptr = &ntfs_input;
            input_size = sizeof(ntfs_input);
            start_usn_ptr = &ntfs_input.StartUsn;
        }
        else
        {
            refs_input.StartUsn = start_usn;
            refs_input.ReasonMask = 0xFFFFFFFF;
            refs_input.ReturnOnlyOnClose = FALSE;
            refs_input.Timeout = 0;
            refs_input.BytesToWaitFor = 0;
            refs_input.UsnJournalID = journal.journal_id;
            refs_input.MinMajorVersion = 2;
            refs_input.MaxMajorVersion = 3;
            input_ptr = &refs_input;
            input_size = sizeof(refs_input);
            start_usn_ptr = &refs_input.StartUsn;
        }

        bool target_reached = false;
        const UsnRecordCallback effective_callback = [&](const UsnRecordSummary& rec)
        {
            if (target_usn > 0 && rec.usn >= target_usn)
            {
                target_reached = true;
                return false;
            }
            if (callback)
            {
                return callback(rec);
            }
            return true;
        };

        std::vector<BYTE> buffer(1024 * 1024);
        DWORD returned = 0;
        for (;;)
        {
            if (cancel && cancel())
            {
                SetError(error, ERROR_CANCELLED);
                return false;
            }

            if (!DeviceIoControl(
                    volume.get(),
                    FSCTL_READ_USN_JOURNAL,
                    input_ptr,
                    input_size,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    &returned,
                    nullptr))
            {
                const DWORD device_error = GetLastError();
                if (device_error == ERROR_HANDLE_EOF || device_error == ERROR_NO_MORE_FILES)
                {
                    if (stats != nullptr)
                    {
                        stats->next_usn = *start_usn_ptr;
                    }
                    return true;
                }
                SetError(error, device_error);
                return false;
            }

            if (returned < sizeof(USN))
            {
                if (stats != nullptr)
                {
                    stats->next_usn = *start_usn_ptr;
                }
                return true;
            }

            const USN next_usn = *reinterpret_cast<const USN*>(buffer.data());
            const ParseResult parsed = ParseRecordBuffer(buffer.data(), returned, sizeof(USN), effective_callback);
            if (!parsed.valid)
            {
                SetError(error, ERROR_INVALID_DATA);
                return false;
            }
            if (stats != nullptr)
            {
                stats->records += parsed.records;
                stats->next_usn = next_usn;
            }
            if (parsed.stopped)
            {
                if (target_reached)
                {
                    if (stats != nullptr && target_usn > 0)
                    {
                        stats->next_usn = target_usn;
                    }
                    return true;
                }
                SetError(error, ERROR_CANCELLED);
                return false;
            }
            if (next_usn <= *start_usn_ptr)
            {
                return true;
            }
            if (target_usn > 0 && next_usn >= target_usn)
            {
                if (stats != nullptr)
                {
                    stats->next_usn = target_usn;
                }
                return true;
            }
            *start_usn_ptr = next_usn;
        }
    }

    std::vector<UsnRecordSummary> ReadUsnChanges(
        const std::wstring& path,
        std::int64_t start_usn,
        std::size_t max_records,
        DWORD* error)
    {
        std::vector<UsnRecordSummary> records;
        DWORD local_error = ERROR_SUCCESS;
        const bool completed = ReadUsnChangesStreaming(
            path,
            0,
            start_usn,
            [&](const UsnRecordSummary& record)
            {
                records.push_back(record);
                return max_records == 0 || records.size() < max_records;
            },
            {},
            nullptr,
            &local_error);

        if (!completed && local_error == ERROR_CANCELLED && max_records != 0 && records.size() >= max_records)
        {
            local_error = ERROR_SUCCESS;
        }
        SetError(error, local_error);
        return records;
    }
}
