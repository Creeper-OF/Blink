#include "Blink/Core/FileTypes.h"
#include "Blink/Index/DirectoryObserver.h"
#include "Blink/Index/FileIndex.h"
#include "Blink/Properties/PropertyReader.h"
#include "Blink/Provider/Contracts.h"
#include "Blink/Transfer/CopyService.h"

#include <Windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <array>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>

#pragma comment(lib, "Psapi.lib")

namespace
{
    [[nodiscard]] std::wstring ExecutableDirectory()
    {
        std::wstring buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        buffer.resize(length);
        const std::size_t separator = buffer.find_last_of(L"\\/");
        return separator == std::wstring::npos ? L"." : buffer.substr(0, separator);
    }

    [[nodiscard]] blink::core::UniqueHandle CreateSmallFile(const std::wstring& path)
    {
        CREATEFILE3_EXTENDED_PARAMETERS parameters{};
        parameters.dwSize = sizeof(parameters);
        parameters.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
        HANDLE handle = CreateFile3(
            path.c_str(),
            GENERIC_WRITE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            CREATE_ALWAYS,
            &parameters);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return {};
        }
        const char text[] = "Blink Phase 0 identity and transfer test\n";
        DWORD written = 0;
        if (!WriteFile(handle, text, static_cast<DWORD>(sizeof(text) - 1), &written, nullptr))
        {
            return {};
        }
        return blink::core::UniqueHandle(handle);
    }

    [[nodiscard]] blink::core::UniqueHandle CreateSizedFile(const std::wstring& path, std::uint64_t size)
    {
        auto handle = CreateSmallFile(path);
        if (!handle)
        {
            return {};
        }
        LARGE_INTEGER end{};
        end.QuadPart = static_cast<LONGLONG>(size);
        if (!SetFilePointerEx(handle.get(), end, nullptr, FILE_BEGIN) || !SetEndOfFile(handle.get()))
        {
            return {};
        }
        return handle;
    }

    void PrintMemory()
    {
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        {
            std::wcout << L"memory_working_set_bytes=" << counters.WorkingSetSize << L"\n";
        }
    }

    bool RunProviderMode(const std::wstring& host, blink::provider::FakeProviderMode mode, const std::wstring& root)
    {
        blink::provider::ProviderClient client;
        DWORD error = 0;
        if (!client.Start(host, mode, &error))
        {
            std::cerr << "provider_start_failed error=" << error << "\n";
            return false;
        }

        std::promise<blink::provider::ProviderResponse> response_promise;
        auto response_future = response_promise.get_future();
        client.RequestDirectoryAsync(root, [&response_promise](blink::provider::ProviderResponse response) mutable {
            response_promise.set_value(std::move(response));
        });

        if (mode == blink::provider::FakeProviderMode::Hang || mode == blink::provider::FakeProviderMode::Delay)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            const auto started = std::chrono::steady_clock::now();
            const auto listing = blink::core::EnumerateDirectory(root);
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            std::wcout << L"provider_mode=" << blink::provider::ToString(mode)
                       << L" local_browse_entries=" << listing.entries.size()
                       << L" local_browse_ms=" << elapsed << L"\n";
            client.CancelRequest();
        }

        const auto status = response_future.wait_for(std::chrono::seconds(5));
        if (status != std::future_status::ready)
        {
            std::wcerr << L"provider_callback_timeout mode=" << blink::provider::ToString(mode) << L"\n";
            client.Stop(true);
            return false;
        }
        const auto response = response_future.get();
        std::wcout << L"provider_mode=" << blink::provider::ToString(mode)
                   << L" success=" << response.success
                   << L" canceled=" << response.canceled
                   << L" error=" << response.error
                   << L" message=" << response.message << L"\n";
        client.Stop(true);
        return true;
    }
}

int wmain(int argc, wchar_t** argv)
{
    // Keep the default relative so this prototype does not depend on console
    // handling of the Chinese repository path. Run it from the repository root.
    const std::filesystem::path default_root = std::filesystem::path(L".build") / L"phase0-test";
    const std::wstring root = argc > 1 ? argv[1] : default_root.wstring();
    const std::wstring host = argc > 2 ? argv[2] : (std::filesystem::path(ExecutableDirectory()) / L"Blink.ProviderHost.exe").wstring();

    DWORD error = 0;
    const bool root_created = blink::core::CreateDirectoryModern(root, &error);
    std::wcout << L"root=" << root << L" root_created=" << root_created << L" root_error=" << error << L"\n";
    const std::wstring source = blink::core::JoinPath(root, L"identity-source.bin");
    const std::wstring renamed = blink::core::JoinPath(root, L"identity-renamed.bin");
    const std::wstring destination = blink::core::JoinPath(root, L"copy.bin");
    const std::wstring cancel_source = blink::core::JoinPath(root, L"cancel-source.bin");
    const std::wstring cancel_destination = blink::core::JoinPath(root, L"cancel-copy.bin");
    {
        auto file = CreateSmallFile(source);
        if (!file)
        {
            std::wcerr << L"create_test_file_failed error=" << GetLastError() << L"\n";
            return 1;
        }
    }

    blink::core::FileEntry first{};
    if (!blink::core::ReadEntryMetadata(source, first))
    {
        std::wcerr << L"read_source_metadata_failed error=" << GetLastError() << L"\n";
        return 1;
    }
    if (!blink::core::RenameFileModern(source, renamed, &error))
    {
        std::wcerr << L"rename_failed error=" << error << L" " << blink::core::Win32ErrorMessage(error) << L"\n";
        return 1;
    }
    blink::core::FileEntry second{};
    if (!blink::core::ReadEntryMetadata(renamed, second))
    {
        std::wcerr << L"read_renamed_metadata_failed error=" << GetLastError() << L"\n";
        return 1;
    }
    std::wcout << L"identity_same_after_rename=" << (first.identity == second.identity)
               << L" identity=" << second.identity.ToString() << L"\n";

    blink::index::FileIndex index;
    blink::index::IndexStats stats{};
    if (!index.Build(root, nullptr, &stats))
    {
        std::wcerr << L"index_build_failed\n";
        return 1;
    }
    double search_ms = 0;
    const auto search_results = index.Search(blink::index::SearchOptions{ L"identity", false, L".bin", root, 20 }, &search_ms);
    std::wcout << L"index_files=" << stats.files
               << L" index_directories=" << stats.directories
               << L" index_build_seconds=" << stats.build_seconds
               << L" search_results=" << search_results.size()
               << L" search_ms=" << search_ms
               << L" usn_available=" << stats.journal.available
               << L" usn_error=" << stats.journal.error << L"\n";
    const std::wstring index_file = blink::core::JoinPath(root, L"phase0.index");
    index.Save(index_file, &error);

    const auto properties = blink::properties::ReadFileProperties(renamed, true, true);
    std::wcout << L"properties_available=" << properties.available
               << L" metadata_count=" << properties.metadata.size()
               << L" acl_entries=" << properties.security.entries.size()
               << L" hard_links=" << properties.hard_link_count
               << L" streams=" << properties.streams.size()
               << L"\n";

    auto copy = blink::transfer::CopyFileAsync(renamed, destination, [](const blink::transfer::CopyProgress& progress) {
        if (progress.total_bytes != 0 && progress.transferred_bytes == progress.total_bytes)
        {
            std::wcout << L"copy_progress=100\n";
        }
    });
    const auto copy_result = copy.completion.get();
    std::wcout << L"copy_status=0x" << std::hex << static_cast<unsigned long>(copy_result.status)
               << std::dec << L" canceled=" << copy_result.canceled << L"\n";

    {
        auto cancel_file = CreateSizedFile(cancel_source, 64ull * 1024ull * 1024ull);
    }
    auto canceled_copy = blink::transfer::CopyFileAsync(cancel_source, cancel_destination);
    canceled_copy.control->Cancel();
    const auto canceled_result = canceled_copy.completion.get();
    std::wcout << L"copy_cancel_status=0x" << std::hex << static_cast<unsigned long>(canceled_result.status)
               << std::dec << L" canceled=" << canceled_result.canceled << L"\n";

    const auto usn_records = blink::index::EnumerateUsnRecords(root, 64, &error);
    std::wcout << L"usn_probe_records=" << usn_records.size() << L" usn_probe_error=" << error << L"\n";

    const std::wstring observer_file = blink::core::JoinPath(root, L"observer-trigger.bin");
    std::vector<blink::index::DirectoryChange> observed_changes;
    DWORD observer_error = ERROR_SUCCESS;
    std::thread observer([&]() {
        observed_changes = blink::index::WatchDirectoryOnce(root, std::chrono::seconds(3), false, &observer_error);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        auto trigger = CreateSmallFile(observer_file);
    }
    observer.join();
    const bool observer_saw_file = std::any_of(
        observed_changes.begin(),
        observed_changes.end(),
        [&](const blink::index::DirectoryChange& change) { return change.name == L"observer-trigger.bin"; });
    std::wcout << L"directory_observer_changes=" << observed_changes.size()
               << L" saw_trigger=" << observer_saw_file
               << L" observer_error=" << observer_error << L"\n";

    const std::array<blink::provider::FakeProviderMode, 6> modes{
        blink::provider::FakeProviderMode::Normal,
        blink::provider::FakeProviderMode::Delay,
        blink::provider::FakeProviderMode::Hang,
        blink::provider::FakeProviderMode::Error,
        blink::provider::FakeProviderMode::Disconnect,
        blink::provider::FakeProviderMode::Crash,
    };
    for (const auto mode : modes)
    {
        RunProviderMode(host, mode, root);
    }

    PrintMemory();
    (void)blink::core::DeleteFileModern(destination, nullptr);
    (void)blink::core::DeleteFileModern(renamed, nullptr);
    (void)blink::core::DeleteFileModern(cancel_source, nullptr);
    (void)blink::core::DeleteFileModern(cancel_destination, nullptr);
    (void)blink::core::DeleteFileModern(index_file, nullptr);
    (void)blink::core::DeleteFileModern(observer_file, nullptr);
    (void)blink::core::RemoveDirectoryModern(root, nullptr);
    return 0;
}
