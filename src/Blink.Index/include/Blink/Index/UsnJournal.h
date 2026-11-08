#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace blink::index
{
    struct JournalState
    {
        bool available{};
        DWORD error{};
        std::wstring volume_root;
        std::wstring file_system;
        std::uint64_t journal_id{};
        std::uint64_t volume_serial{};
        std::int64_t first_usn{};
        std::int64_t next_usn{};
        std::int64_t lowest_valid_usn{};
        std::int64_t max_usn{};
        std::uint64_t maximum_size{};
        std::uint64_t allocation_delta{};
        WORD min_supported_major{};
        WORD max_supported_major{};
        std::uint64_t records_observed{};
    };

    struct UsnRecordSummary
    {
        WORD major_version{};
        std::int64_t usn{};
        std::uint64_t file_reference{};
        std::uint64_t parent_reference{};
        std::array<std::uint8_t, 16> file_reference_128{};
        std::array<std::uint8_t, 16> parent_reference_128{};
        DWORD reason{};
        DWORD attributes{};
        std::wstring name;
    };

    struct UsnReadStats
    {
        std::uint64_t records{};
        std::int64_t next_usn{};
    };

    using UsnRecordCallback = std::function<bool(const UsnRecordSummary&)>;
    using UsnCancelPredicate = std::function<bool()>;

    [[nodiscard]] JournalState QueryUsnJournal(const std::wstring& path);
    [[nodiscard]] std::vector<UsnRecordSummary> EnumerateUsnRecords(
        const std::wstring& path,
        std::size_t max_records,
        DWORD* error = nullptr);
    [[nodiscard]] bool EnumerateUsnRecordsStreaming(
        const std::wstring& path,
        const UsnRecordCallback& callback,
        const UsnCancelPredicate& cancel = {},
        UsnReadStats* stats = nullptr,
        DWORD* error = nullptr);
    [[nodiscard]] std::vector<UsnRecordSummary> ReadUsnChanges(
        const std::wstring& path,
        std::int64_t start_usn,
        std::size_t max_records,
        DWORD* error = nullptr);
    [[nodiscard]] bool ReadUsnChangesStreaming(
        const std::wstring& path,
        std::uint64_t expected_journal_id,
        std::int64_t start_usn,
        const UsnRecordCallback& callback,
        const UsnCancelPredicate& cancel = {},
        UsnReadStats* stats = nullptr,
        DWORD* error = nullptr,
        std::int64_t target_usn = 0);
}
