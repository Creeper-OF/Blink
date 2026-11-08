#pragma once

#include "Blink/Core/FileTypes.h"
#include "Blink/Index/UsnJournal.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace blink::index
{
    struct SearchOptions
    {
        std::wstring text;
        bool prefix_match{ true };
        std::wstring extension;
        std::wstring path_prefix;
        std::size_t limit{ 100 };
    };

    struct IndexStats
    {
        std::uint64_t files{};
        std::uint64_t directories{};
        std::uint64_t skipped_directories{};
        std::uint64_t errors{};
        std::uint64_t bytes_indexed{};
        std::uint64_t index_bytes{};
        std::uint64_t search_count{};
        double build_seconds{};
        double last_search_milliseconds{};
        std::size_t resident_bytes{};
        JournalState journal{};
    };

    class FileIndex
    {
    public:
        bool Build(
            const std::wstring& root,
            std::atomic_bool* cancellation = nullptr,
            IndexStats* stats = nullptr);

        [[nodiscard]] std::vector<blink::core::FileEntry> Search(const SearchOptions& options, double* elapsed_ms = nullptr) const;
        bool Save(const std::wstring& path, DWORD* error = nullptr) const;
        bool Load(const std::wstring& path, DWORD* error = nullptr);

        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] IndexStats Stats() const;
        void Clear();

    private:
        struct NameKey
        {
            std::wstring key;
            std::size_t index{};
        };

        std::vector<blink::core::FileEntry> entries_;
        std::vector<NameKey> name_keys_;
        mutable IndexStats stats_{};
        mutable std::mutex mutex_;
    };
}
