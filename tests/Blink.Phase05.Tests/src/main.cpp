#include "Blink/Index/Contracts.h"
#include "Blink/Index/FastIndex.h"
#include "Blink/IndexService/PipeImpersonationGuard.h"
#include "Blink/IndexService/ProgressSnapshot.h"
#include "SearchVisibilityWireTests.h"

#include <Windows.h>
#include <winioctl.h>
#include <aclapi.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Advapi32.lib")

namespace blink::index
{
    // Test-only access is enabled only for this ordinary test executable. It
    // exercises the derived structure without changing the production API or
    // the BLK5 format.
    class FastIndexTestAccess
    {
    public:
        struct Spec
        {
            blink::core::FileIdentity identity{};
            blink::core::FileIdentity parent_identity{};
            std::wstring name;
            DWORD attributes{};
            std::int64_t last_usn{};
        };

        static void Reset(
            FastIndex& index,
            const blink::core::FileIdentity& root_identity,
            DWORD volume_serial,
            const std::wstring& volume_root = L"D:\\")
        {
            std::scoped_lock lock(index.mutex_);
            index.links_.clear();
            index.extension_index_.clear();
            index.snapshot_ = {};
            index.snapshot_.volume_root = volume_root;
            index.snapshot_.volume_serial = volume_serial;
            index.snapshot_.journal_id = 123;
            index.snapshot_.first_usn = 1;
            index.snapshot_.next_usn = 100;
            index.snapshot_.lowest_valid_usn = 1;
            index.snapshot_.max_usn = 100;
            index.snapshot_.last_usn = 1;
            index.snapshot_.root_identity = root_identity;
            index.snapshot_.health = FastIndexHealth::Healthy;
            index.snapshot_.error = ERROR_SUCCESS;
            index.stats_ = {};
            index.stats_.health = FastIndexHealth::Healthy;
            index.stats_.error = ERROR_SUCCESS;
        }

        static void Seed(FastIndex& index, const std::vector<Spec>& specs)
        {
            std::scoped_lock lock(index.mutex_);
            for (const Spec& spec : specs)
            {
                FastIndex::Link link;
                link.identity = spec.identity;
                link.parent_identity = spec.parent_identity;
                link.name = spec.name;
                link.attributes = spec.attributes;
                link.last_usn = spec.last_usn;
                index.links_[link.identity].push_back(std::move(link));
            }
            index.RebuildExtensionIndexUnlocked();
            index.RecomputeStatsUnlocked();
        }

        static bool Add(FastIndex& index, const Spec& spec)
        {
            std::scoped_lock lock(index.mutex_);
            FastIndex::Link link;
            link.identity = spec.identity;
            link.parent_identity = spec.parent_identity;
            link.name = spec.name;
            link.attributes = spec.attributes;
            link.last_usn = spec.last_usn;
            return index.AddOrUpdateLinkUnlocked(link);
        }

        static bool Remove(FastIndex& index, const Spec& spec)
        {
            std::scoped_lock lock(index.mutex_);
            FastIndex::Link link;
            link.identity = spec.identity;
            link.parent_identity = spec.parent_identity;
            link.name = spec.name;
            link.attributes = spec.attributes;
            link.last_usn = spec.last_usn;
            return index.RemoveLinkUnlocked(link);
        }

        static bool Apply(FastIndex& index, const Spec& spec, const DWORD reason)
        {
            std::scoped_lock lock(index.mutex_);
            UsnRecordSummary record;
            record.major_version = 2; record.usn = spec.last_usn; record.reason = reason;
            record.attributes = spec.attributes; record.name = spec.name;
            std::memcpy(&record.file_reference, spec.identity.file_id.data(), sizeof(record.file_reference));
            std::memcpy(&record.parent_reference, spec.parent_identity.file_id.data(), sizeof(record.parent_reference));
            return index.ApplyRecordUnlocked(record);
        }
    };
}

namespace
{
    [[nodiscard]] std::string ToUtf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }
        const int required = WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required <= 0)
        {
            return {};
        }
        std::string result(static_cast<std::size_t>(required), '\0');
        (void)WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required,
            nullptr,
            nullptr);
        return result;
    }

    void WriteUtf8Line(const std::wstring& line)
    {
        std::cout << ToUtf8(line) << '\n';
    }

    [[nodiscard]] std::wstring GetOption(
        int argc,
        wchar_t* argv[],
        const std::wstring& name,
        const std::wstring& fallback = {})
    {
        for (int index = 1; index + 1 < argc; ++index)
        {
            if (argv[index] == name)
            {
                return argv[index + 1];
            }
        }
        return fallback;
    }

    [[nodiscard]] bool HasOption(int argc, wchar_t* argv[], const std::wstring& name)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] == name)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t GetSampleCount(int argc, wchar_t* argv[])
    {
        const std::wstring value = GetOption(argc, argv, L"--samples", L"5");
        try
        {
            const unsigned long long parsed = std::stoull(value);
            return static_cast<std::size_t>(std::clamp<unsigned long long>(parsed, 3, 100));
        }
        catch (...)
        {
            return 5;
        }
    }

    [[nodiscard]] std::wstring FullPath(const std::wstring& path)
    {
        wchar_t buffer[32768]{};
        const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(buffer)), buffer, nullptr);
        return length == 0 ? std::wstring{} : std::wstring(buffer, length);
    }

    [[nodiscard]] bool StartProcess(
        const std::wstring& executable,
        const std::wstring& data_root,
        PROCESS_INFORMATION& process_info)
    {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        std::wstring command_line = L"\"" + executable + L"\" --console --data-root \"" + data_root + L"\"";
        return CreateProcessW(
            executable.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process_info) != FALSE;
    }

    void CloseProcess(PROCESS_INFORMATION& process_info)
    {
        if (process_info.hProcess != nullptr)
        {
            TerminateProcess(process_info.hProcess, ERROR_PROCESS_ABORTED);
            WaitForSingleObject(process_info.hProcess, 5000);
            CloseHandle(process_info.hProcess);
            process_info.hProcess = nullptr;
        }
        if (process_info.hThread != nullptr)
        {
            CloseHandle(process_info.hThread);
            process_info.hThread = nullptr;
        }
    }

    [[nodiscard]] std::wstring CurrentProcessUserSid()
    {
        HANDLE token = INVALID_HANDLE_VALUE;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            return {};
        }
        DWORD bytes = 0;
        (void)GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        if (bytes == 0)
        {
            CloseHandle(token);
            return {};
        }
        std::vector<std::byte> buffer(bytes);
        const bool queried = GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes) != FALSE;
        CloseHandle(token);
        if (!queried)
        {
            return {};
        }
        LPWSTR text = nullptr;
        const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
        if (!ConvertSidToStringSidW(user->User.Sid, &text))
        {
            return {};
        }
        const std::wstring sid(text);
        LocalFree(text);
        return sid;
    }

    [[nodiscard]] bool CheckPipeSecurityTelemetry(const std::wstring& data_root)
    {
        const std::filesystem::path path = std::filesystem::path(data_root) / L"security" / L"pipe-security.json";
        std::string content;
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            std::ifstream input(path, std::ios::binary);
            if (input)
            {
                content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
                if (!content.empty())
                {
                    break;
                }
            }
            Sleep(50);
        }
        const std::string sid = ToUtf8(CurrentProcessUserSid());
        const bool passed =
            content.find("\"descriptor_query\": \"PASS\"") != std::string::npos &&
            content.find("\"pipe_reject_remote_clients\": true") != std::string::npos &&
            content.find("\"pipe_client_rights_hex\": \"0x00120183\"") != std::string::npos &&
            content.find("\"authorized_client_sid\": \"" + sid + "\"") != std::string::npos &&
            content.find(";;;AU") == std::string::npos &&
            content.find(";;;BA") == std::string::npos &&
            content.find("S-1-1-0") == std::string::npos;
        std::wcout << L"pipe_security_telemetry=" << (passed ? L"passed" : L"failed")
                   << L" path=" << path.wstring() << L"\n";
        return passed;
    }

    void PrintResponse(const wchar_t* label, const blink::index::ipc::Response& response)
    {
        std::wstring line = label;
        line += L" status=";
        line += blink::index::ipc::ResponseStatusName(response.status);
        line += L" error=" + std::to_wstring(response.error);
        line += L" index=";
        line += blink::index::ipc::IndexStateName(response.index_state);
        line += L" operation=" + std::to_wstring(response.operation_id);
        line += L" records=" + std::to_wstring(response.records_seen);
        line += L" message=" + response.message;

        const int required = WideCharToMultiByte(
            CP_UTF8,
            0,
            line.data(),
            static_cast<int>(line.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        std::string utf8(static_cast<std::size_t>(std::max(required, 0)), '\0');
        if (required != 0)
        {
            (void)WideCharToMultiByte(
                CP_UTF8,
                0,
                line.data(),
                static_cast<int>(line.size()),
                utf8.data(),
                required,
                nullptr,
                nullptr);
        }
        std::cout << utf8 << '\n';
    }

    [[nodiscard]] std::wstring Lower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), towlower);
        return value;
    }

    [[nodiscard]] bool IsPathWithin(const std::wstring& path, const std::wstring& scope)
    {
        const std::wstring normalized_path = Lower(blink::core::NormalizePath(path));
        const std::wstring normalized_scope = Lower(blink::core::NormalizePath(scope));
        if (normalized_path == normalized_scope)
        {
            return true;
        }
        return normalized_path.size() > normalized_scope.size() &&
            normalized_path.starts_with(normalized_scope) &&
            normalized_path[normalized_scope.size()] == L'\\';
    }

    [[nodiscard]] std::wstring IdentityHex(const blink::core::FileIdentity& identity)
    {
        std::wostringstream stream;
        stream << std::uppercase << std::hex << std::setfill(L'0') << std::setw(16) << identity.volume_serial << L':';
        for (const std::uint8_t byte : identity.file_id)
        {
            stream << std::setw(2) << static_cast<unsigned int>(byte);
        }
        return stream.str();
    }

    [[nodiscard]] bool SameUsnIdentity(
        const blink::core::FileIdentity& expected,
        const blink::core::FileIdentity& actual)
    {
        return expected.valid && actual.valid &&
            expected.file_id == actual.file_id &&
            static_cast<DWORD>(expected.volume_serial) == static_cast<DWORD>(actual.volume_serial);
    }

    struct OfflineSearchCase
    {
        std::wstring name;
        blink::index::FastSearchOptions options;
        std::wstring expected_path;
        std::wstring outside_path;
        std::wstring additional_expected_path;
        bool expects_hit{};
    };

    struct OfflineBenchmarkResult
    {
        std::wstring name;
        std::wstring text;
        std::wstring extension;
        std::wstring path_prefix;
        std::vector<double> samples_ms;
        std::vector<double> candidate_filter_ms;
        std::vector<double> path_resolution_ms;
        std::vector<double> result_construction_ms;
        std::vector<double> sorting_ms;
        std::size_t result_count{};
        bool over_five_seconds{};
    };

    [[nodiscard]] double TimingStageMilliseconds(
        const std::atomic<std::int64_t>& start,
        const std::atomic<std::int64_t>& end)
    {
        const std::int64_t start_us = start.load(std::memory_order_acquire);
        const std::int64_t end_us = end.load(std::memory_order_acquire);
        if (start_us < 0 || end_us < start_us)
        {
            return -1.0;
        }
        return static_cast<double>(end_us - start_us) / 1000.0;
    }

    [[nodiscard]] std::vector<std::wstring> SearchKeys(
        blink::index::FastIndex& index,
        blink::index::FastSearchOptions options)
    {
        const auto hits = index.Search(options);
        std::vector<std::wstring> keys;
        keys.reserve(hits.size());
        for (const auto& hit : hits)
        {
            keys.push_back(Lower(hit.full_path) + L"|" + IdentityHex(hit.identity));
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    [[nodiscard]] bool SameSearchResults(
        blink::index::FastIndex& index,
        blink::index::FastSearchOptions options)
    {
        const auto derived = SearchKeys(index, options);
        options.use_derived_extension_index = false;
        const auto reference = SearchKeys(index, options);
        return derived == reference;
    }

    [[nodiscard]] int RunDerivedExtensionIndexUnitTest()
    {
        DWORD volume_serial = 0;
        if (!GetVolumeInformationW(L"D:\\", nullptr, 0, &volume_serial, nullptr, nullptr, nullptr, 0))
        {
            WriteUtf8Line(L"search_derived_extension_unit=SKIP volume_serial_error=" + std::to_wstring(GetLastError()));
            return 91;
        }

        const auto make_identity = [volume_serial](std::uint8_t marker)
        {
            blink::core::FileIdentity identity;
            identity.volume_serial = volume_serial;
            identity.valid = true;
            identity.file_id.fill(marker);
            return identity;
        };

        const blink::core::FileIdentity root = make_identity(0xF0);
        const blink::core::FileIdentity hard_identity = make_identity(0x01);
        const blink::core::FileIdentity cpp_identity = make_identity(0x02);
        const blink::core::FileIdentity new_identity = make_identity(0x03);
        const blink::core::FileIdentity internal_directory_identity = make_identity(0x04);
        const blink::core::FileIdentity internal_file_identity = make_identity(0x05);
        const blink::core::FileIdentity parent = root;
        const std::wstring scope = L"D:\\";
        const auto spec = [&](const blink::core::FileIdentity& identity, const wchar_t* name, std::int64_t usn)
        {
            return blink::index::FastIndexTestAccess::Spec{
                identity,
                parent,
                name,
                FILE_ATTRIBUTE_ARCHIVE,
                usn};
        };

        blink::index::FastIndex index;
        blink::index::FastIndexTestAccess::Reset(index, root, volume_serial);
        blink::index::FastIndexTestAccess::Seed(index, {
            spec(hard_identity, L"HardSource.txt", 2),
            spec(hard_identity, L"HardAlias.txt", 3),
            spec(cpp_identity, L"Existing.cpp", 4),
            spec(internal_directory_identity, L"InternalExcluded", 5),
            blink::index::FastIndexTestAccess::Spec{
                internal_file_identity,
                internal_directory_identity,
                L"Service.txt",
                FILE_ATTRIBUTE_ARCHIVE,
                6}});
        index.SetInternalExcludedRoots({ L"D:\\InternalExcluded" });

        blink::index::FastSearchOptions txt;
        txt.extension = L".txt";
        txt.path_prefix = scope;
        txt.limit = 200;
        const auto initial_txt = SearchKeys(index, txt);
        const auto has_path_key = [](const std::vector<std::wstring>& keys, const wchar_t* path)
        {
            const std::wstring prefix = Lower(path) + L"|";
            return std::any_of(keys.begin(), keys.end(), [&prefix](const std::wstring& key)
            {
                return key.starts_with(prefix);
            });
        };
        const bool initial_hard_links = initial_txt.size() == 2 &&
            has_path_key(initial_txt, L"D:\\HardSource.txt") &&
            has_path_key(initial_txt, L"D:\\HardAlias.txt") &&
            SameSearchResults(index, txt);
        const bool internal_excluded = !has_path_key(initial_txt, L"D:\\InternalExcluded\\Service.txt");

        const std::wstring store_path = L".blink-data\\acceptance\\offline-derived-extension-unit.blink5";
        std::error_code store_error;
        std::filesystem::create_directories(std::filesystem::path(store_path).parent_path(), store_error);
        DWORD save_error = ERROR_SUCCESS;
        const bool saved = index.Save(FullPath(store_path), &save_error);
        blink::index::FastIndex loaded;
        DWORD load_error = ERROR_SUCCESS;
        const bool loaded_ok = saved && loaded.Load(FullPath(store_path), L"D:\\", &load_error);
        loaded.SetInternalExcludedRoots({ L"D:\\InternalExcluded" });
        const bool load_rebuild = loaded_ok && SearchKeys(loaded, txt).size() == 2 && SameSearchResults(loaded, txt);

        const auto new_txt = spec(new_identity, L"Created.txt", 5);
        const auto old_alias = spec(hard_identity, L"HardAlias.txt", 6);
        const auto renamed_alias = spec(hard_identity, L"HardAlias.cpp", 7);
        const bool create_bucket = loaded_ok && blink::index::FastIndexTestAccess::Add(loaded, new_txt) &&
            SearchKeys(loaded, txt).size() == 3 && SameSearchResults(loaded, txt);
        const bool rename_bucket = create_bucket &&
            blink::index::FastIndexTestAccess::Remove(loaded, old_alias) &&
            blink::index::FastIndexTestAccess::Add(loaded, renamed_alias) &&
            SearchKeys(loaded, txt).size() == 2 &&
            SameSearchResults(loaded, txt);
        blink::index::FastSearchOptions cpp;
        cpp.extension = L".cpp";
        cpp.path_prefix = scope;
        cpp.limit = 200;
        const bool rename_added_bucket = rename_bucket && SearchKeys(loaded, cpp).size() == 2 &&
            SameSearchResults(loaded, cpp);
        const bool delete_bucket = rename_added_bucket &&
            blink::index::FastIndexTestAccess::Remove(loaded, spec(hard_identity, L"HardSource.txt", 8)) &&
            SearchKeys(loaded, txt).size() == 1 && SameSearchResults(loaded, txt);

        blink::index::FastSearchOptions mixed_query;
        mixed_query.text = L"MIXED"; mixed_query.prefix = true; mixed_query.extension = L".TXT";
        const auto mixed_txt = spec(new_identity, L"MiXeD.TxT", 10);
        const auto mixed_bin = spec(new_identity, L"mIxEd.BiN", 11);
        const bool mixed_created = blink::index::FastIndexTestAccess::Remove(loaded, new_txt) &&
            blink::index::FastIndexTestAccess::Add(loaded, mixed_txt) && SearchKeys(loaded, mixed_query).size() == 1;
        const bool mixed_renamed = blink::index::FastIndexTestAccess::Remove(loaded, mixed_txt) &&
            blink::index::FastIndexTestAccess::Add(loaded, mixed_bin) && SearchKeys(loaded, mixed_query).empty();
        mixed_query.extension = L".BIN";
        const bool mixed_updated = blink::index::FastIndexTestAccess::Add(loaded, mixed_bin) &&
            SearchKeys(loaded, mixed_query).size() == 1 && SameSearchResults(loaded, mixed_query);
        const bool mixed_deleted = blink::index::FastIndexTestAccess::Remove(loaded, mixed_bin) && SearchKeys(loaded, mixed_query).empty();
        const bool mixed_ok = mixed_created && mixed_renamed && mixed_updated && mixed_deleted;
        WriteUtf8Line(std::wstring(mixed_ok ? L"PASS " : L"FAIL ") +
            L"fast_index_casefold_cache_tracks_create_case_rename_and_delete");

        blink::index::FastIndex scoped;
        blink::index::FastIndexTestAccess::Reset(scoped, root, volume_serial);
        const auto skipped_dir = make_identity(0x41), chosen_dir = make_identity(0x42);
        blink::index::FastIndexTestAccess::Seed(scoped, {
            { skipped_dir, root, L"Skipped", FILE_ATTRIBUTE_DIRECTORY, 1 },
            { chosen_dir, root, L"Chosen", FILE_ATTRIBUTE_DIRECTORY, 1 },
            { make_identity(0x43), skipped_dir, L"Match-Ignored.txt", FILE_ATTRIBUTE_ARCHIVE, 2 },
            { make_identity(0x44), chosen_dir, L"Match-A.txt", FILE_ATTRIBUTE_ARCHIVE, 3 },
            { make_identity(0x45), chosen_dir, L"Match-B.txt", FILE_ATTRIBUTE_ARCHIVE, 4 },
            { make_identity(0x46), root, L"Outside-Only.txt", FILE_ATTRIBUTE_ARCHIVE, 5 } });
        scoped.SetInternalExcludedRoots({ scope + L"Skipped" });
        blink::index::FastSearchOptions capped;
        capped.text = L"MATCH"; capped.extension = L".TXT"; capped.path_prefix = scope; capped.limit = 2;
        const auto capped_hits = scoped.Search(capped);
        const bool capped_ok = capped_hits.size() == 2 && std::all_of(capped_hits.begin(), capped_hits.end(), [](const auto& hit) {
            return hit.name == L"Match-A.txt" || hit.name == L"Match-B.txt";
        });
        WriteUtf8Line(std::wstring(capped_ok ? L"PASS " : L"FAIL ") +
            L"fast_index_limit_applies_after_internal_exclusions");
        capped.text.clear(); capped.limit = 20;
        capped.path_prefix = blink::core::JoinPath(scope, L"Chosen") + scope.back();
        const auto scoped_hits = scoped.Search(capped);
        const bool scoped_ok = scoped_hits.size() == 2 && std::all_of(scoped_hits.begin(), scoped_hits.end(), [&](const auto& hit) {
            return hit.parent_identity == chosen_dir && hit.full_path.starts_with(capped.path_prefix);
        }) && SameSearchResults(scoped, capped);
        WriteUtf8Line(std::wstring(scoped_ok ? L"PASS " : L"FAIL ") +
            L"fast_index_cached_parent_paths_preserve_folder_scope");

        const auto nested_dir = make_identity(0x47), sibling_dir = make_identity(0x48);
        blink::index::FastIndexTestAccess::Seed(scoped, {
            { nested_dir, chosen_dir, L"Nested", FILE_ATTRIBUTE_DIRECTORY, 6 },
            { sibling_dir, root, L"ChosenSibling", FILE_ATTRIBUTE_DIRECTORY, 7 },
            { make_identity(0x49), nested_dir, L"Match-Nested.txt", FILE_ATTRIBUTE_ARCHIVE, 8 },
            { make_identity(0x4a), sibling_dir, L"Match-Sibling.txt", FILE_ATTRIBUTE_ARCHIVE, 9 } });
        auto direct = capped;
        direct.text = L"MATCH"; direct.extension = L".txt";
        direct.direct_children = true; direct.limit = 20;
        const auto direct_hits = scoped.Search(direct);
        const bool direct_scope = direct_hits.size() == 2 && std::all_of(direct_hits.begin(), direct_hits.end(),
            [&](const auto& hit) { return hit.parent_identity == chosen_dir &&
                (hit.name == L"Match-A.txt" || hit.name == L"Match-B.txt"); });
        WriteUtf8Line(std::wstring(direct_scope ? L"PASS " : L"FAIL ") +
            L"fast_index_direct_children_excludes_descendants_and_siblings");
        direct.limit = 1;
        const auto direct_capped = scoped.Search(direct);
        const bool direct_limit = direct_capped.size() == 1 && direct_capped.front().parent_identity == chosen_dir;
        WriteUtf8Line(std::wstring(direct_limit ? L"PASS " : L"FAIL ") +
            L"fast_index_direct_children_limit_applies_after_scope");
        direct.path_prefix = Lower(scope); direct.text = L"outside"; direct.limit = 20;
        const auto direct_root_hits = scoped.Search(direct);
        const bool direct_root = direct_root_hits.size() == 1 && direct_root_hits.front().parent_identity == root &&
            direct_root_hits.front().name == L"Outside-Only.txt";
        WriteUtf8Line(std::wstring(direct_root ? L"PASS " : L"FAIL ") +
            L"fast_index_direct_children_drive_root_casefold");
        direct.path_prefix.clear();
        const bool direct_empty = scoped.Search(direct).empty() &&
            std::wstring(blink::index::ipc::RequestTypeName(blink::index::ipc::RequestType::SearchDirectChildren)) == L"SearchDirectChildren";
        WriteUtf8Line(std::wstring(direct_empty ? L"PASS " : L"FAIL ") +
            L"fast_index_direct_children_no_implicit_global_scope");

        std::error_code remove_error;
        std::filesystem::remove(FullPath(store_path), remove_error);
        const bool passed = initial_hard_links && internal_excluded && load_rebuild && create_bucket && rename_bucket &&
            rename_added_bucket && delete_bucket && mixed_ok && capped_ok && scoped_ok &&
            direct_scope && direct_limit && direct_root && direct_empty;
        WriteUtf8Line(
            L"search_derived_extension_unit=" + std::wstring(passed ? L"PASS" : L"FAIL") +
            L" initial_hard_links=" + (initial_hard_links ? L"true" : L"false") +
            L" internal_excluded=" + (internal_excluded ? L"true" : L"false") +
            L" load_rebuild=" + (load_rebuild ? L"true" : L"false") +
            L" create_bucket=" + (create_bucket ? L"true" : L"false") +
            L" rename_bucket=" + (rename_bucket ? L"true" : L"false") +
            L" rename_added_bucket=" + (rename_added_bucket ? L"true" : L"false") +
            L" delete_bucket=" + (delete_bucket ? L"true" : L"false") +
            L" save_error=" + std::to_wstring(save_error) +
            L" load_error=" + std::to_wstring(load_error));
        return passed ? 0 : 92;
    }

    [[nodiscard]] bool RunSearchCorrectnessTests()
    {
        namespace fs = std::filesystem;
        using blink::index::FastIndex;
        using blink::index::FastIndexTestAccess;
        using blink::index::FastSearchOptions;
        using Spec = FastIndexTestAccess::Spec;
        bool passed = true;
        std::size_t checks{};
        const auto check = [&](const bool value, const wchar_t* name) {
            ++checks; passed = value && passed;
            WriteUtf8Line(std::wstring(value ? L"PASS " : L"FAIL ") + name);
        };
        DWORD serial{};
        const bool local_volume = GetVolumeInformationW(L"D:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0) != FALSE;
        check(local_volume, L"index_search_correctness_local_fixture_volume_available");
        if (!local_volume) { return false; }
        const auto identity = [serial](const std::uint64_t number) {
            blink::core::FileIdentity value; value.volume_serial = serial; value.valid = true;
            std::memcpy(value.file_id.data(), &number, sizeof(number)); return value;
        };
        const auto root_id = identity(1);
        const auto names = [](FastIndex& index, const FastSearchOptions& options) {
            std::vector<std::wstring> values;
            for (const auto& hit : index.Search(options)) { values.push_back(hit.name); }
            std::sort(values.begin(), values.end()); return values;
        };
        const auto visible = [](const auto& hits, const std::size_t count, const DWORD excluded) {
            return hits.size() == count && std::all_of(hits.begin(), hits.end(), [excluded](const auto& hit) {
                return (hit.attributes & excluded) == 0;
            });
        };
        constexpr DWORD excluded = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
        FastIndex crowded;
        FastIndexTestAccess::Reset(crowded, root_id, serial);
        std::vector<Spec> records;
        for (std::uint64_t index{}; index < 2050; ++index)
        { records.push_back({ identity(1000 + index), root_id, L"Needle-" + std::to_wstring(index) + L".txt", FILE_ATTRIBUTE_ARCHIVE, 2 }); }
        FastIndexTestAccess::Seed(crowded, records);
        FastSearchOptions options; options.text = L"needle"; options.extension = L".TXT"; options.limit = 2010;
        const auto first_candidates = crowded.Search(options);
        bool seeded = first_candidates.size() == 2010;
        for (std::size_t index{}; index < first_candidates.size(); ++index)
        {
            const auto& hit = first_candidates[index];
            seeded &= FastIndexTestAccess::Add(crowded, { hit.identity, hit.parent_identity, hit.name,
                static_cast<DWORD>(FILE_ATTRIBUTE_ARCHIVE | (index < 1005 ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_SYSTEM)), 3 });
        }
        options.limit = 16;
        const auto old_cap = crowded.Search(options);
        check(seeded && old_cap.size() == 16 && std::all_of(old_cap.begin(), old_cap.end(), [](const auto& hit) {
            return (hit.attributes & excluded) != 0;
        }), L"index_visibility_fixture_has_over_1000_hidden_and_system_hits_before_legacy_cap");
        options.excluded_attributes = excluded;
        check(visible(crowded.Search(options), 16, excluded),
            L"index_visibility_hidden_and_system_do_not_consume_visible_result_limit");
        options.excluded_attributes = FILE_ATTRIBUTE_HIDDEN;
        const auto hidden_only = crowded.Search(options);
        check(visible(hidden_only, 16, FILE_ATTRIBUTE_HIDDEN) && std::any_of(hidden_only.begin(), hidden_only.end(), [](const auto& hit) {
            return (hit.attributes & FILE_ATTRIBUTE_SYSTEM) != 0;
        }), L"index_visibility_hidden_mask_leaves_system_matches_available");
        options.excluded_attributes = FILE_ATTRIBUTE_SYSTEM;
        const auto system_only = crowded.Search(options);
        check(visible(system_only, 16, FILE_ATTRIBUTE_SYSTEM) && std::any_of(system_only.begin(), system_only.end(), [](const auto& hit) {
            return (hit.attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        }), L"index_visibility_system_mask_can_include_hidden_matches");
        options.excluded_attributes = 0;
        check(names(crowded, options) == [&] {
            std::vector<std::wstring> result; for (const auto& hit : old_cap) { result.push_back(hit.name); }
            std::sort(result.begin(), result.end()); return result;
        }(), L"index_visibility_zero_mask_preserves_legacy_capped_search");
        options.excluded_attributes = excluded; options.limit = 0;
        const auto all_visible = crowded.Search(options);
        check(visible(all_visible, 40, excluded) && SameSearchResults(crowded, options),
            L"index_visibility_extension_bucket_and_full_scan_keep_same_complete_visible_set");
        options.limit = 16; options.prefix = true;
        check(visible(crowded.Search(options), 16, excluded),
            L"index_visibility_prefix_filter_applies_before_result_limit");
        options.prefix = false; options.text = L"EEDLE-";
        check(visible(crowded.Search(options), 16, excluded),
            L"index_visibility_substring_filter_applies_before_result_limit");
        const auto exact_name = all_visible.empty() ? std::wstring{ L"Needle-missing.txt" } : all_visible.front().name;
        const bool exact_ready = !all_visible.empty() && FastIndexTestAccess::Add(crowded,
            { identity(9999), root_id, exact_name + L".extra", FILE_ATTRIBUTE_HIDDEN, 4 });
        options.text = exact_name; options.prefix = true; options.extension.clear(); options.limit = 1;
        check(exact_ready && names(crowded, options) == std::vector<std::wstring>{ exact_name },
            L"index_visibility_exact_search_prefix_candidates_exclude_hidden_before_cap");
        options.text = L"NEEDLE"; options.prefix = false; options.limit = 200;
        check(visible(crowded.Search(options), 40, excluded),
            L"index_visibility_sparse_visible_matches_do_not_borrow_excluded_rows");

        FastIndex scoped;
        FastIndexTestAccess::Reset(scoped, root_id, serial);
        const auto chosen = identity(10), nested = identity(11), sibling = identity(12), unicode_parent = identity(13);
        const auto unicode_child = identity(14), hard = identity(20), mutable_id = identity(21);
        FastIndexTestAccess::Seed(scoped, {
            { chosen, root_id, L"Chosen", FILE_ATTRIBUTE_DIRECTORY, 1 },
            { nested, chosen, L"Nested", FILE_ATTRIBUTE_DIRECTORY, 1 },
            { sibling, root_id, L"ChosenSibling", FILE_ATTRIBUTE_DIRECTORY, 1 },
            { unicode_parent, root_id, L"Ångström", FILE_ATTRIBUTE_DIRECTORY, 1 },
            { unicode_child, unicode_parent, L"Каталог", FILE_ATTRIBUTE_DIRECTORY, 1 },
            { identity(30), chosen, L"scope-hit-visible.txt", FILE_ATTRIBUTE_ARCHIVE, 2 },
            { identity(31), chosen, L"scope-hit-hidden.txt", FILE_ATTRIBUTE_HIDDEN, 2 },
            { identity(32), chosen, L"scope-hit-system.txt", FILE_ATTRIBUTE_SYSTEM, 2 },
            { identity(33), nested, L"scope-hit-nested.txt", FILE_ATTRIBUTE_ARCHIVE, 2 },
            { identity(34), sibling, L"scope-hit-sibling.txt", FILE_ATTRIBUTE_ARCHIVE, 2 },
            { hard, chosen, L"hard-source.txt", FILE_ATTRIBUTE_ARCHIVE, 2 },
            { hard, chosen, L"hard-alias.txt", FILE_ATTRIBUTE_ARCHIVE, 2 },
            { mutable_id, chosen, L"mutable.txt", FILE_ATTRIBUTE_ARCHIVE, 2 },
            { identity(35), unicode_child, L"unicode-parent-hit.txt", FILE_ATTRIBUTE_ARCHIVE, 2 } });
        FastSearchOptions scope; scope.text = L"scope-hit"; scope.path_prefix = L"d:\\chosen\\"; scope.limit = 20;
        scope.excluded_attributes = excluded;
        check(names(scoped, scope) == std::vector<std::wstring>{ L"scope-hit-nested.txt", L"scope-hit-visible.txt" },
            L"index_visibility_recursive_scope_filters_attributes_without_sibling_prefix_leak");
        scope.direct_children = true;
        check(names(scoped, scope) == std::vector<std::wstring>{ L"scope-hit-visible.txt" },
            L"index_visibility_direct_children_filter_preserves_scope_before_limit");
        scope.path_prefix = L"D:\\ChosenSibling\\";
        check(names(scoped, scope) == std::vector<std::wstring>{ L"scope-hit-sibling.txt" },
            L"index_visibility_sibling_scope_does_not_return_other_folder_matches");
        scope.path_prefix = L"D:\\";
        check(scoped.Search(scope).empty(), L"index_visibility_direct_drive_root_excludes_nested_matches");
        scope = {}; scope.text = L"hard-"; scope.excluded_attributes = excluded;
        const auto hard_hits = scoped.Search(scope);
        check(hard_hits.size() == 2 && hard_hits[0].identity == hard_hits[1].identity && hard_hits[0].name != hard_hits[1].name,
            L"index_visibility_retains_distinct_hard_links_for_same_file_identity");
        scope.text = L"mutable";
        const bool before_attribute = scoped.Search(scope).size() == 1;
        const bool became_hidden = FastIndexTestAccess::Apply(scoped,
            { mutable_id, chosen, L"mutable.txt", FILE_ATTRIBUTE_HIDDEN, 3 }, USN_REASON_BASIC_INFO_CHANGE);
        check(before_attribute && became_hidden && scoped.Search(scope).empty(),
            L"index_visibility_usn_attribute_change_hides_previously_cached_name");
        check(FastIndexTestAccess::Apply(scoped, { mutable_id, chosen, L"mutable.txt", FILE_ATTRIBUTE_ARCHIVE, 4 },
            USN_REASON_BASIC_INFO_CHANGE) && scoped.Search(scope).size() == 1,
            L"index_visibility_usn_attribute_change_restores_visible_match");

        FastIndex unicode;
        FastIndexTestAccess::Reset(unicode, root_id, serial);
        const std::vector<std::wstring> unicode_names{ L"Ångström.TXT", L"ΟΣ.TXT", L"ПрИвЕт.ТХТ", L"Straße.txt",
            L"STRASSE.txt", L"éclair.txt", L"e\u0301clair.txt", L"Istanbul.txt", L"İstanbul.txt", L"ıstanbul.txt" };
        std::vector<Spec> unicode_records;
        for (std::size_t index{}; index < unicode_names.size(); ++index)
        { unicode_records.push_back({ identity(100 + index), root_id, unicode_names[index], FILE_ATTRIBUTE_ARCHIVE, 2 }); }
        FastIndexTestAccess::Seed(unicode, unicode_records);
        FastSearchOptions query; query.text = L"ångSTRÖM"; query.prefix = true;
        check(names(unicode, query) == std::vector<std::wstring>{ L"Ångström.TXT" },
            L"index_unicode_latin_casefold_does_not_depend_on_crt_c_locale");
        const auto sigma_expected = [&](const std::wstring& text) {
            std::vector<std::wstring> expected;
            if (CompareStringOrdinal(unicode_names[1].data(), static_cast<int>(text.size()),
                text.data(), static_cast<int>(text.size()), TRUE) == CSTR_EQUAL)
            { expected.push_back(unicode_names[1]); }
            return expected;
        };
        query.text = L"ος";
        const bool final_sigma = names(unicode, query) == sigma_expected(query.text);
        query.text = L"οσ";
        const auto ordinary_sigma = names(unicode, query);
        check(final_sigma && ordinary_sigma == sigma_expected(query.text) &&
            ordinary_sigma == std::vector<std::wstring>{ L"ΟΣ.TXT" },
            L"index_unicode_greek_sigma_variants_follow_windows_ordinal_oracle");
        query.text = L"привет"; query.extension = L".тхт";
        check(names(unicode, query) == std::vector<std::wstring>{ L"ПрИвЕт.ТХТ" } && SameSearchResults(unicode, query),
            L"index_unicode_cyrillic_name_and_extension_bucket_use_same_casefold");
        FastSearchOptions unicode_scope; unicode_scope.text = L"unicode-parent"; unicode_scope.path_prefix = L"d:\\åNGSTRÖM\\каталог\\";
        unicode_scope.direct_children = true; unicode_scope.excluded_attributes = excluded;
        check(names(scoped, unicode_scope) == std::vector<std::wstring>{ L"unicode-parent-hit.txt" },
            L"index_unicode_folder_scope_and_direct_parent_use_ordinal_comparison");
        bool ordinal_matrix = true;
        const std::vector<std::wstring> ordinal_queries{ L"STRASSE", L"straße", L"STRAẞE", L"ÉCLAIR", L"E\u0301CLAIR",
            L"istanbul", L"İSTANBUL", L"ıSTANBUL" };
        query.extension.clear();
        for (const auto& text : ordinal_queries)
        {
            query.text = text;
            std::vector<std::wstring> expected;
            for (const auto& name : unicode_names)
            {
                if (name.size() >= text.size() && CompareStringOrdinal(name.data(), static_cast<int>(text.size()),
                    text.data(), static_cast<int>(text.size()), TRUE) == CSTR_EQUAL) { expected.push_back(name); }
            }
            std::sort(expected.begin(), expected.end()); ordinal_matrix &= names(unicode, query) == expected;
        }
        check(ordinal_matrix, L"index_unicode_sharp_s_combining_marks_and_turkish_i_match_windows_ordinal_oracle");
        FastIndex malformed;
        FastIndexTestAccess::Reset(malformed, root_id, serial);
        const auto unusual_name = L"odd-" + std::wstring(1, static_cast<wchar_t>(0xD800)) + L"-é.txt";
        FastIndexTestAccess::Seed(malformed, { { identity(200), root_id, unusual_name, FILE_ATTRIBUTE_ARCHIVE, 2 } });
        query = {}; query.text = L"-É.TXT";
        check(names(malformed, query) == std::vector<std::wstring>{ unusual_name },
            L"index_unicode_unpaired_utf16_is_preserved_while_other_characters_are_folded");
        const auto long_name = std::wstring(240, L'é') + L".TXT";
        (void)FastIndexTestAccess::Add(unicode, { identity(201), root_id, long_name, FILE_ATTRIBUTE_ARCHIVE, 2 });
        query.text = std::wstring(240, L'É'); query.prefix = true;
        check(names(unicode, query) == std::vector<std::wstring>{ long_name },
            L"index_unicode_explicit_length_mapping_keeps_long_filename_and_query");
        scoped.SetInternalExcludedRoots({ L"d:\\åNGSTRÖM\\каталог" });
        check(scoped.Search(unicode_scope).empty() && names(scoped, scope) == std::vector<std::wstring>{ L"mutable.txt" },
            L"index_unicode_internal_exclusion_is_case_insensitive_without_excluding_unrelated_scope");
        scoped.SetInternalExcludedRoots({ L"D:\\Chosen" });
        scope.text = L"scope-hit"; scope.path_prefix.clear();
        check(names(scoped, scope) == std::vector<std::wstring>{ L"scope-hit-sibling.txt" },
            L"index_unicode_internal_exclusion_keeps_directory_boundary_semantics");

        const auto fixture = fs::absolute(fs::path(L".build") / L"index-search-correctness-fixtures" /
            (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
        struct Cleanup { fs::path root; ~Cleanup() { std::error_code ignored; fs::remove_all(root, ignored); } } cleanup{ fixture };
        fs::create_directories(fixture);
        const auto store = fixture / L"unicode.blink5", visible_store = fixture / L"visibility.blink5";
        DWORD error{};
        FastIndex loaded, visible_loaded;
        bool rebuilt = unicode.Save(store.wstring(), &error) && FastIndex::ValidateStoreFile(store.wstring(), &error) &&
            loaded.Load(store.wstring(), L"D:\\", &error);
        query = {}; query.prefix = true;
        for (const auto& text : ordinal_queries)
        { query.text = text; rebuilt = rebuilt && names(loaded, query) == names(unicode, query); }
        query.text = L"привет"; query.extension = L".тхт";
        rebuilt = rebuilt && names(loaded, query) == std::vector<std::wstring>{ L"ПрИвЕт.ТХТ" } && SameSearchResults(loaded, query);
        check(rebuilt, L"index_unicode_blk5_load_rebuilds_name_keys_and_derived_unicode_extension_index");
        options.text = L"needle"; options.extension = L".TXT"; options.limit = 200;
        check(crowded.Save(visible_store.wstring(), &error) && visible_loaded.Load(visible_store.wstring(), L"D:\\", &error) &&
            visible(visible_loaded.Search(options), 40, excluded),
            L"index_visibility_blk5_roundtrip_preserves_attributes_and_visible_limit_semantics");
        const auto old_latin = unicode_records.front();
        Spec renamed{ old_latin.identity, old_latin.parent_identity, L"ångSTRÖM.BIN", FILE_ATTRIBUTE_ARCHIVE, 8 };
        const bool renamed_ok = FastIndexTestAccess::Apply(loaded, old_latin, USN_REASON_RENAME_OLD_NAME) &&
            FastIndexTestAccess::Apply(loaded, renamed, USN_REASON_RENAME_NEW_NAME);
        query.text = L"ÅNGSTRÖM"; query.extension = L".TXT";
        const bool old_bucket_gone = loaded.Search(query).empty(); query.extension = L".bin";
        check(renamed_ok && old_bucket_gone && names(loaded, query) == std::vector<std::wstring>{ renamed.name } && SameSearchResults(loaded, query),
            L"index_unicode_usn_case_rename_refreshes_name_cache_and_extension_bucket");
        check(FastIndexTestAccess::Apply(loaded, renamed, USN_REASON_FILE_DELETE) && loaded.Search(query).empty(),
            L"index_unicode_usn_delete_removes_cached_name_and_derived_extension_match");
        WriteUtf8Line(L"search_correctness_unit=" + std::wstring(passed ? L"PASS" : L"FAIL") + L" checks=" + std::to_wstring(checks));
        return passed;
    }

    [[nodiscard]] bool RunLoadIsolationUnitTest()
    {
        namespace fs = std::filesystem;
        using blink::index::FastIndex;
        using blink::index::FastIndexHealth;
        using blink::index::FastIndexTestAccess;
        const auto root = fs::absolute(fs::path(L".build") / L"index-load-fixtures" /
            (L"pid-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64())));
        struct Cleanup
        {
            fs::path root;
            ~Cleanup() { std::error_code ignored; fs::remove_all(root, ignored); }
        } cleanup{ root };
        try
        {
            fs::create_directories(root);
            DWORD d_serial{}, c_serial{};
            if (!GetVolumeInformationW(L"D:\\", nullptr, 0, &d_serial, nullptr, nullptr, nullptr, 0) ||
                !GetVolumeInformationW(L"C:\\", nullptr, 0, &c_serial, nullptr, nullptr, nullptr, 0))
            {
                WriteUtf8Line(L"FAIL fast_index_load_isolation_fixture_volumes_available");
                return false;
            }
            const auto identity = [](const DWORD serial, const std::uint8_t marker)
            {
                blink::core::FileIdentity value; value.volume_serial = serial; value.file_id.fill(marker); value.valid = true;
                return value;
            };
            const auto d_root = identity(d_serial, 0xD0), c_root = identity(c_serial, 0xC0);
            FastIndex original;
            FastIndexTestAccess::Reset(original, d_root, d_serial);
            FastIndexTestAccess::Seed(original, { { identity(d_serial, 1), d_root, L"keep.txt", FILE_ATTRIBUTE_ARCHIVE, 2 } });
            FastIndex other;
            FastIndexTestAccess::Reset(other, c_root, c_serial, L"C:\\");
            FastIndexTestAccess::Seed(other, { { identity(c_serial, 2), c_root, L"other.txt", FILE_ATTRIBUTE_ARCHIVE, 2 } });
            const auto good = root / L"d.blink5", corrupt = root / L"c.blink5", missing = root / L"missing" / L"c.blink5";
            DWORD error{};
            const bool saved = original.Save(good.wstring(), &error) && other.Save(corrupt.wstring(), &error);
            {
                std::fstream stream(corrupt, std::ios::in | std::ios::out | std::ios::binary);
                char value{}; stream.seekg(-1, std::ios::end); stream.read(&value, 1); value ^= 1;
                stream.seekp(-1, std::ios::end); stream.write(&value, 1);
                if (!stream) { throw std::runtime_error("corrupt checksum fixture"); }
            }
            FastIndex active;
            const bool ready = saved && active.Load(good.wstring(), L"D:\\", &error);
            blink::index::FastSearchOptions query; query.extension = L".txt"; query.path_prefix = L"D:\\";
            const auto before = active.Snapshot(); const auto counts = active.Stats(); const auto results = SearchKeys(active, query);
            const auto preserved = [&]()
            {
                const auto now = active.Snapshot(); const auto stats = active.Stats();
                return active.IsLoaded() && now.volume_root == before.volume_root && now.root_identity == before.root_identity &&
                    now.health == before.health && now.error == before.error && now.journal_id == before.journal_id &&
                    now.last_usn == before.last_usn && stats.files == counts.files && stats.links == counts.links &&
                    SearchKeys(active, query) == results && SameSearchResults(active, query);
            };
            const bool missing_ok = ready && !active.Load(missing.wstring(), L"C:\\", &error) &&
                error == ERROR_PATH_NOT_FOUND && preserved();
            WriteUtf8Line(std::wstring(missing_ok ? L"PASS " : L"FAIL ") +
                L"fast_index_failed_missing_volume_load_preserves_active_index");
            const bool corrupt_ok = ready && !active.Load(corrupt.wstring(), L"C:\\", &error) && error == ERROR_CRC && preserved();
            WriteUtf8Line(std::wstring(corrupt_ok ? L"PASS " : L"FAIL ") +
                L"fast_index_failed_crc_volume_load_preserves_active_index");
            FastIndex empty;
            const bool initial_missing = !empty.Load(missing.wstring(), L"C:\\", &error) && error == ERROR_PATH_NOT_FOUND &&
                !empty.IsLoaded() && empty.Snapshot().health == FastIndexHealth::RebuildRequired;
            const bool initial_corrupt = !empty.Load(corrupt.wstring(), L"C:\\", &error) && error == ERROR_CRC &&
                !empty.IsLoaded() && empty.Snapshot().health == FastIndexHealth::Corrupted;
            const bool recovery = empty.Load(good.wstring(), L"D:\\", &error) && !error &&
                empty.Snapshot().health == FastIndexHealth::Healthy && SearchKeys(empty, query) == results;
            const bool fresh_ok = ready && initial_missing && initial_corrupt && recovery;
            WriteUtf8Line(std::wstring(fresh_ok ? L"PASS " : L"FAIL ") +
                L"fast_index_unloaded_failures_report_error_and_valid_load_recovers");
            constexpr std::size_t record_count = 5000;
            std::vector<FastIndexTestAccess::Spec> records;
            records.reserve(record_count);
            for (std::size_t i = 0; i < record_count; ++i)
            {
                blink::core::FileIdentity file;
                file.volume_serial = d_serial; file.valid = true;
                const std::uint64_t reference = 1000 + i;
                std::memcpy(file.file_id.data(), &reference, sizeof(reference));
                const auto name = L"buffer-boundary-" + std::to_wstring(i) + L"-" +
                    std::wstring(80 + i % 89, L'x') + (i % 2 ? L".txt" : L".bin");
                records.push_back({ file, d_root, name, FILE_ATTRIBUTE_ARCHIVE, 2 });
            }
            FastIndex bulk;
            FastIndexTestAccess::Reset(bulk, d_root, d_serial);
            FastIndexTestAccess::Seed(bulk, records);
            const auto buffered_store = root / L"buffered-reader.blink5";
            const bool bulk_saved = bulk.Save(buffered_store.wstring(), &error);
            const auto store_bytes = bulk_saved ? fs::file_size(buffered_store) : 0;
            FastIndex restored;
            const bool bulk_loaded = bulk_saved && FastIndex::ValidateStoreFile(buffered_store.wstring(), &error) &&
                restored.Load(buffered_store.wstring(), before.volume_root, &error);
            blink::index::FastSearchOptions all_txt; all_txt.extension = L".txt"; all_txt.limit = record_count;
            auto all_bin = all_txt; all_bin.extension = L".bin";
            const auto expected_txt = SearchKeys(bulk, all_txt), expected_bin = SearchKeys(bulk, all_bin);
            const bool buffered_ok = bulk_loaded && store_bytes > 20 * 64 * 1024 &&
                restored.Snapshot().root_identity == d_root && restored.Snapshot().health == FastIndexHealth::Healthy &&
                restored.Stats().files == record_count && restored.Stats().links == record_count &&
                expected_txt.size() == record_count / 2 && expected_bin.size() == record_count / 2 &&
                SearchKeys(restored, all_txt) == expected_txt && SearchKeys(restored, all_bin) == expected_bin &&
                SameSearchResults(restored, all_txt) && SameSearchResults(restored, all_bin);
            WriteUtf8Line(L"INFO fast_index_buffered_load records=" + std::to_wstring(record_count) +
                L" store_bytes=" + std::to_wstring(store_bytes) + L" reader_buffer_bytes=65536 error=" + std::to_wstring(error));
            WriteUtf8Line(std::wstring(buffered_ok ? L"PASS " : L"FAIL ") +
                L"fast_index_buffered_load_crosses_buffers_and_preserves_search_results");
            return missing_ok && corrupt_ok && fresh_ok && buffered_ok;
        }
        catch (const std::exception&)
        {
            WriteUtf8Line(L"FAIL fast_index_load_isolation_fixture_completed");
            return false;
        }
    }

    [[nodiscard]] double Percentile(std::vector<double> values, double percentile)
    {
        if (values.empty())
        {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const double position = percentile * static_cast<double>(values.size() - 1);
        const std::size_t lower = static_cast<std::size_t>(std::floor(position));
        const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
        if (lower == upper)
        {
            return values[lower];
        }
        const double fraction = position - static_cast<double>(lower);
        return values[lower] + (values[upper] - values[lower]) * fraction;
    }

    [[nodiscard]] std::wstring CsvQuote(const std::wstring& value)
    {
        std::wstring escaped = value;
        std::size_t offset = 0;
        while ((offset = escaped.find(L'"', offset)) != std::wstring::npos)
        {
            escaped.insert(offset, 1, L'"');
            offset += 2;
        }
        return L"\"" + escaped + L"\"";
    }

    [[nodiscard]] bool ReadIdentityForPath(
        const std::wstring& path,
        blink::core::FileIdentity& identity)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            return false;
        }
        const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        blink::core::UniqueHandle handle = blink::core::OpenPathForMetadata(path, directory);
        return handle && blink::core::ReadFileIdentity(handle.get(), identity);
    }

    [[nodiscard]] int RunOfflineSearch(int argc, wchar_t* argv[])
    {
        const std::wstring store_path = FullPath(GetOption(argc, argv, L"--store"));
        const std::wstring volume_root = FullPath(GetOption(argc, argv, L"--volume-root", L"D:\\"));
        const std::wstring fixture_root = FullPath(GetOption(argc, argv, L"--fixture-root"));
        const std::wstring output_path = FullPath(GetOption(
            argc,
            argv,
            L"--output",
            L".blink-data\\acceptance\\offline-search-results.csv"));
        const std::size_t sample_count = GetSampleCount(argc, argv);
        const bool reference_full_scan = HasOption(argc, argv, L"--reference-full-scan");

        if (store_path.empty() || fixture_root.empty() || volume_root.empty())
        {
            WriteUtf8Line(L"offline_search_invalid_arguments=store, volume-root, and fixture-root are required");
            return 20;
        }

        blink::index::FastIndex index;
        DWORD load_error = ERROR_SUCCESS;
        if (!index.Load(store_path, volume_root, &load_error))
        {
            WriteUtf8Line(L"offline_search_load_failed=" + std::to_wstring(load_error));
            return 21;
        }

        const std::wstring baseline = blink::core::JoinPath(fixture_root, L"Baseline");
        const std::wstring rename_directory = blink::core::JoinPath(baseline, L"RenameDirectory");
        const std::wstring stable = blink::core::JoinPath(baseline, L"StableFile-Phase05.txt");
        const std::wstring rename_child = blink::core::JoinPath(rename_directory, L"RenameChild-Phase05.txt");
        const std::wstring outside_scoped = blink::core::JoinPath(
            blink::core::JoinPath(baseline, L"OutsideScopedDirectory"),
            L"RenameChild-Phase05.txt");

        std::vector<OfflineSearchCase> cases;
        auto add_case = [&cases, reference_full_scan](
            const wchar_t* name,
            const wchar_t* text,
            bool prefix,
            const wchar_t* extension,
            const std::wstring& path_prefix,
            const std::wstring& expected_path,
            const std::wstring& outside_path = {},
            bool expects_hit = true)
        {
            OfflineSearchCase query;
            query.name = name;
            query.options.text = text;
            query.options.prefix = prefix;
            query.options.extension = extension;
            query.options.path_prefix = path_prefix;
            query.options.limit = 200;
            if (reference_full_scan && !query.options.extension.empty())
            {
                query.options.use_derived_extension_index = false;
            }
            query.expected_path = expected_path;
            query.outside_path = outside_path;
            query.expects_hit = expects_hit;
            cases.push_back(std::move(query));
        };

        add_case(L"known_exact", L"StableFile-Phase05.txt", false, L"", fixture_root, stable);
        add_case(L"known_prefix", L"StableFile", true, L"", fixture_root, stable);
        add_case(L"known_substring", L"Phase05", false, L"", fixture_root, stable);
        add_case(L"known_extension", L"", false, L".txt", fixture_root, stable);
        add_case(
            L"known_directory_scoped",
            L"RenameChild-Phase05.txt",
            false,
            L"",
            rename_directory,
            rename_child,
            outside_scoped);
        add_case(
            L"known_directory_scoped_extension",
            L"",
            false,
            L".txt",
            rename_directory,
            rename_child);
        add_case(
            L"known_miss",
            L"Blink-Phase05-Known-Miss-20260811",
            false,
            L"",
            fixture_root,
            {},
            {},
            false);

        std::vector<OfflineSearchCase> correctness_cases = cases;
        std::vector<OfflineBenchmarkResult> benchmarks;
        std::wstring csv =
            L"record_type,query_type,query_text,extension,path_prefix,result_count,expected_path,"
            L"expected_identity,matching_identity,expected_path_match,scope_valid,outside_scope_hits,additional_expected_path,"
            L"reference_result_count,reference_equal,status,min_ms,median_ms,p95_ms,max_ms,"
            L"candidate_filter_median_ms,path_resolution_median_ms,result_construction_median_ms,sorting_median_ms\n";
        bool correctness_passed = true;

        for (const OfflineSearchCase& query : correctness_cases)
        {
            blink::core::FileIdentity expected_identity;
            const bool expected_identity_read = query.expects_hit &&
                ReadIdentityForPath(query.expected_path, expected_identity);
            const std::vector<blink::index::FastSearchResult> hits = index.Search(query.options);
            bool matching_identity = false;
            bool expected_path_match = !query.expects_hit;
            bool scope_valid = true;
            std::size_t outside_scope_hits = 0;
            std::wstring matching_identity_text;
            bool additional_expected_match = query.additional_expected_path.empty();
            for (const auto& hit : hits)
            {
                if (!query.outside_path.empty() && Lower(hit.full_path) == Lower(query.outside_path))
                {
                    ++outside_scope_hits;
                }
                if (!query.options.path_prefix.empty() && !IsPathWithin(hit.full_path, query.options.path_prefix))
                {
                    scope_valid = false;
                }
                if (expected_identity_read && SameUsnIdentity(expected_identity, hit.identity))
                {
                    matching_identity = true;
                    matching_identity_text = IdentityHex(hit.identity);
                }
                if (query.expects_hit && Lower(hit.full_path) == Lower(query.expected_path))
                {
                    expected_path_match = true;
                }
                if (!query.additional_expected_path.empty() &&
                    Lower(hit.full_path) == Lower(query.additional_expected_path))
                {
                    additional_expected_match = true;
                }
            }

            std::size_t reference_result_count = 0;
            bool reference_equal = true;
            if (!query.options.extension.empty())
            {
                blink::index::FastSearchOptions reference_options = query.options;
                reference_options.extension.clear();
                const auto reference_hits = index.Search(reference_options);
                std::vector<std::wstring> reference_keys;
                std::vector<std::wstring> indexed_keys;
                for (const auto& reference_hit : reference_hits)
                {
                    if (Lower(blink::core::FileExtension(reference_hit.name)) == Lower(query.options.extension))
                    {
                        reference_keys.push_back(Lower(reference_hit.full_path) + L"|" + IdentityHex(reference_hit.identity));
                    }
                }
                for (const auto& indexed_hit : hits)
                {
                    indexed_keys.push_back(Lower(indexed_hit.full_path) + L"|" + IdentityHex(indexed_hit.identity));
                }
                std::sort(reference_keys.begin(), reference_keys.end());
                std::sort(indexed_keys.begin(), indexed_keys.end());
                reference_result_count = reference_keys.size();
                reference_equal = reference_keys == indexed_keys;
            }

            const bool passed = query.expects_hit
                ? expected_identity_read && matching_identity && expected_path_match && additional_expected_match &&
                    scope_valid && outside_scope_hits == 0 && reference_equal
                : hits.empty();
            correctness_passed = correctness_passed && passed;
            const std::wstring status = passed ? L"PASS" : L"FAIL";
            const std::wstring expected_text = query.expects_hit ? query.expected_path : L"";
            const std::wstring expected_identity_text = expected_identity_read ? IdentityHex(expected_identity) : L"";
            csv += L"correctness," + CsvQuote(query.name) + L"," + CsvQuote(query.options.text) + L"," +
                CsvQuote(query.options.extension) + L"," + CsvQuote(query.options.path_prefix) + L"," +
                std::to_wstring(hits.size()) + L"," + CsvQuote(expected_text) + L"," +
                CsvQuote(expected_identity_text) + L"," + CsvQuote(matching_identity_text) + L"," +
                (expected_path_match ? L"true" : L"false") + L"," +
                (scope_valid ? L"true" : L"false") + L"," + std::to_wstring(outside_scope_hits) + L"," +
                CsvQuote(query.additional_expected_path) + L"," + std::to_wstring(reference_result_count) + L"," +
                (reference_equal ? L"true" : L"false") + L"," + status + L",,,,,,,,\n";
            WriteUtf8Line(
                L"offline_search_correctness " + query.name + L" status=" + status +
                L" result_count=" + std::to_wstring(hits.size()) +
                L" expected_identity_match=" + (matching_identity ? L"true" : L"false") +
                L" expected_path_match=" + (expected_path_match ? L"true" : L"false") +
                L" additional_expected_match=" + (additional_expected_match ? L"true" : L"false") +
                L" scope_valid=" + (scope_valid ? L"true" : L"false") +
                L" reference_equal=" + (reference_equal ? L"true" : L"false") +
                L" outside_scope_hits=" + std::to_wstring(outside_scope_hits));
        }

        for (const OfflineSearchCase& query : correctness_cases)
        {
            if (!query.expects_hit)
            {
                continue;
            }
            OfflineBenchmarkResult benchmark;
            benchmark.name = query.name;
            benchmark.text = query.options.text;
            benchmark.extension = query.options.extension;
            benchmark.path_prefix = query.options.path_prefix;
            benchmark.samples_ms.reserve(sample_count);
            benchmark.candidate_filter_ms.reserve(sample_count);
            benchmark.path_resolution_ms.reserve(sample_count);
            benchmark.result_construction_ms.reserve(sample_count);
            benchmark.sorting_ms.reserve(sample_count);
            for (std::size_t sample = 0; sample < sample_count; ++sample)
            {
                blink::index::FastSearchTiming timing(std::chrono::steady_clock::now());
                const auto started = std::chrono::steady_clock::now();
                const auto hits = index.Search(query.options, &timing);
                const auto elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count();
                benchmark.samples_ms.push_back(elapsed);
                benchmark.candidate_filter_ms.push_back(TimingStageMilliseconds(
                    timing.candidate_filter_start_us,
                    timing.candidate_filter_end_us));
                benchmark.path_resolution_ms.push_back(TimingStageMilliseconds(
                    timing.path_resolution_start_us,
                    timing.path_resolution_end_us));
                benchmark.result_construction_ms.push_back(TimingStageMilliseconds(
                    timing.result_construction_start_us,
                    timing.result_construction_end_us));
                benchmark.sorting_ms.push_back(TimingStageMilliseconds(
                    timing.sorting_start_us,
                    timing.sorting_end_us));
                benchmark.result_count = hits.size();
            }
            benchmark.over_five_seconds = *std::max_element(
                benchmark.samples_ms.begin(), benchmark.samples_ms.end()) >= 5000.0;
            const double minimum = *std::min_element(benchmark.samples_ms.begin(), benchmark.samples_ms.end());
            const double maximum = *std::max_element(benchmark.samples_ms.begin(), benchmark.samples_ms.end());
            const double median = Percentile(benchmark.samples_ms, 0.50);
            const double p95 = Percentile(benchmark.samples_ms, 0.95);
            const double candidate_filter_median = Percentile(benchmark.candidate_filter_ms, 0.50);
            const double path_resolution_median = Percentile(benchmark.path_resolution_ms, 0.50);
            const double result_construction_median = Percentile(benchmark.result_construction_ms, 0.50);
            const double sorting_median = Percentile(benchmark.sorting_ms, 0.50);
            const std::wstring status = benchmark.over_five_seconds ? L"OVER_5S" : L"PASS";
            csv += L"benchmark," + CsvQuote(benchmark.name) + L"," + CsvQuote(benchmark.text) + L"," +
                CsvQuote(benchmark.extension) + L"," + CsvQuote(benchmark.path_prefix) + L"," +
                std::to_wstring(benchmark.result_count) + L",,,true,true,true,0,,0,true," + status + L"," +
                std::to_wstring(minimum) + L"," + std::to_wstring(median) + L"," +
                std::to_wstring(p95) + L"," + std::to_wstring(maximum) + L"," +
                std::to_wstring(candidate_filter_median) + L"," +
                std::to_wstring(path_resolution_median) + L"," +
                std::to_wstring(result_construction_median) + L"," +
                std::to_wstring(sorting_median) + L"\n";
            WriteUtf8Line(
                L"offline_search_benchmark " + benchmark.name +
                L" samples=" + std::to_wstring(sample_count) +
                L" result_count=" + std::to_wstring(benchmark.result_count) +
                L" min_ms=" + std::to_wstring(minimum) +
                L" median_ms=" + std::to_wstring(median) +
                L" p95_ms=" + std::to_wstring(p95) +
                L" max_ms=" + std::to_wstring(maximum) +
                L" candidate_filter_median_ms=" + std::to_wstring(candidate_filter_median) +
                L" path_resolution_median_ms=" + std::to_wstring(path_resolution_median) +
                L" result_construction_median_ms=" + std::to_wstring(result_construction_median) +
                L" sorting_median_ms=" + std::to_wstring(sorting_median) +
                L" status=" + status);
            benchmarks.push_back(std::move(benchmark));
        }

        const std::filesystem::path output_directory = std::filesystem::path(output_path).parent_path();
        std::error_code output_error;
        std::filesystem::create_directories(output_directory, output_error);
        if (output_error)
        {
            WriteUtf8Line(L"offline_search_output_directory_failed=" + std::to_wstring(output_error.value()));
            return 22;
        }
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        const std::string utf8_csv = ToUtf8(csv);
        output.write(utf8_csv.data(), static_cast<std::streamsize>(utf8_csv.size()));
        output.flush();
        if (!output)
        {
            WriteUtf8Line(L"offline_search_output_write_failed");
            return 23;
        }

        const bool benchmark_over_five_seconds = std::any_of(
            benchmarks.begin(),
            benchmarks.end(),
            [](const OfflineBenchmarkResult& result) { return result.over_five_seconds; });
        WriteUtf8Line(
            L"offline_search_correctness=" + std::wstring(correctness_passed ? L"PASS" : L"FAIL") +
            L" benchmark_over_5s=" + (benchmark_over_five_seconds ? L"true" : L"false") +
            L" mode=" + (reference_full_scan ? L"reference_full_scan" : L"derived_extension_index") +
            L" output=" + output_path);
        return correctness_passed ? (benchmark_over_five_seconds ? 24 : 0) : 25;
    }

    [[nodiscard]] bool RunProgressSnapshotUnitTest()
    {
        using blink::index::service::ProgressSnapshot;
        using blink::index::service::ProgressState;

        auto progress = std::make_shared<ProgressState>();
        std::mutex simulated_fast_index_mutex;
        std::atomic<bool> started{ false };
        std::atomic<bool> finished{ false };
        std::thread operation(
            [&]()
            {
                // This models a FastIndex main-mutex holder.  ProgressState
                // must remain readable while this unrelated long operation is
                // still holding the simulated index lock.
                std::scoped_lock fast_index_lock(simulated_fast_index_mutex);
                started.store(true);
                for (std::uint64_t record = 1; record <= 120; ++record)
                {
                    ProgressSnapshot snapshot;
                    snapshot.operation_id = 7;
                    snapshot.initial = true;
                    snapshot.stage = blink::index::service::ProgressStage::EnumeratingMft;
                    snapshot.index_state = blink::index::ipc::IndexState::Rebuilding;
                    snapshot.records_seen = record;
                    snapshot.files = record;
                    snapshot.completed = false;
                    progress->Publish(snapshot);
                    Sleep(5);
                }
                finished.store(true);
            });

        while (!started.load())
        {
            std::this_thread::yield();
        }

        bool fast_reads = true;
        bool observed_progress = false;
        std::uint64_t previous_records = 0;
        while (!finished.load())
        {
            const auto read_started = std::chrono::steady_clock::now();
            const ProgressSnapshot snapshot = progress->Read();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - read_started).count();
            fast_reads = fast_reads && elapsed < 50;
            observed_progress = observed_progress || snapshot.records_seen > previous_records;
            previous_records = snapshot.records_seen;
            Sleep(1);
        }
        operation.join();

        const ProgressSnapshot final_snapshot = progress->Read();
        const bool passed = fast_reads && observed_progress && final_snapshot.records_seen == 120;
        std::wcout << L"query_index_progress_snapshot_unit=" << (passed ? L"passed" : L"failed")
                   << L" max_read_bound_ms=50"
                   << L" final_records=" << final_snapshot.records_seen << L"\n";
        return passed;
    }

    using blink::index::service::PipeImpersonationState;
    using blink::index::service::PipeImpersonationTelemetry;
    using blink::index::service::ScopedPipeImpersonation;

    struct PipeGuardTelemetryCapture
    {
        std::atomic<int> impersonate_events{ 0 };
        std::atomic<int> revert_attempt_events{ 0 };
        std::atomic<int> revert_success_events{ 0 };
        std::atomic<int> revert_error_events{ 0 };
        std::atomic<DWORD> callback_thread_id{ 0 };

        static void Emit(void* context, const char* stage, bool, DWORD) noexcept
        {
            auto* capture = static_cast<PipeGuardTelemetryCapture*>(context);
            capture->callback_thread_id.store(GetCurrentThreadId(), std::memory_order_relaxed);
            if (stage == nullptr)
            {
                return;
            }
            if (std::strcmp(stage, "impersonate_success") == 0)
            {
                capture->impersonate_events.fetch_add(1, std::memory_order_relaxed);
            }
            else if (std::strcmp(stage, "revert_attempt") == 0)
            {
                capture->revert_attempt_events.fetch_add(1, std::memory_order_relaxed);
            }
            else if (std::strcmp(stage, "revert_success") == 0)
            {
                capture->revert_success_events.fetch_add(1, std::memory_order_relaxed);
            }
            else if (std::strcmp(stage, "revert_error") == 0)
            {
                capture->revert_error_events.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    struct PipeAclCaseResult
    {
        bool first_created{};
        DWORD first_error{};
        bool second_created{};
        DWORD second_error{};
    };

    [[nodiscard]] bool InvalidPipeHandle(HANDLE handle) noexcept
    {
        return handle == nullptr || handle == INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] std::wstring FormatAccessMask(DWORD rights)
    {
        wchar_t buffer[16]{};
        (void)swprintf_s(buffer, 16, L"%08X", rights);
        return buffer;
    }

    [[nodiscard]] PipeAclCaseResult RunPipeAclCase(
        const std::wstring& label,
        DWORD server_rights,
        const std::wstring& sid)
    {
        const std::wstring pipe_name =
            L"\\\\.\\pipe\\Blink.Phase05.AclRegression." +
            label +
            L"." +
            std::to_wstring(GetCurrentProcessId()) +
            L"." +
            std::to_wstring(GetTickCount64());
        const std::wstring sddl =
            L"D:P(A;;0x" + FormatAccessMask(server_rights) + L";;;" + sid + L")";
        constexpr DWORD pipe_mode =
            PIPE_TYPE_MESSAGE |
            PIPE_READMODE_MESSAGE |
            PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS;

        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                sddl.c_str(),
                SDDL_REVISION_1,
                &descriptor,
                nullptr))
        {
            return { false, GetLastError(), false, ERROR_FUNCTION_FAILED };
        }

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.lpSecurityDescriptor = descriptor;
        HANDLE first = CreateNamedPipeW(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX,
            pipe_mode,
            16,
            4096,
            4096,
            0,
            &security);
        const bool first_created = !InvalidPipeHandle(first);
        const DWORD first_error = first_created ? ERROR_SUCCESS : GetLastError();
        LocalFree(descriptor);
        descriptor = nullptr;

        HANDLE second = INVALID_HANDLE_VALUE;
        DWORD second_error = ERROR_SUCCESS;
        if (first_created)
        {
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    sddl.c_str(),
                    SDDL_REVISION_1,
                    &descriptor,
                    nullptr))
            {
                second_error = GetLastError();
            }
            else
            {
                security.lpSecurityDescriptor = descriptor;
                second = CreateNamedPipeW(
                    pipe_name.c_str(),
                    PIPE_ACCESS_DUPLEX,
                    pipe_mode,
                    16,
                    4096,
                    4096,
                    0,
                    &security);
                second_error = InvalidPipeHandle(second) ? GetLastError() : ERROR_SUCCESS;
            }
        }
        if (descriptor != nullptr)
        {
            LocalFree(descriptor);
        }

        const PipeAclCaseResult result{
            first_created,
            first_error,
            !InvalidPipeHandle(second),
            second_error};
        if (!InvalidPipeHandle(second))
        {
            CloseHandle(second);
        }
        if (!InvalidPipeHandle(first))
        {
            CloseHandle(first);
        }
        return result;
    }

    [[nodiscard]] bool RunNamedPipeServerAclRegression()
    {
        constexpr DWORD expected_client_rights = 0x00120183u;
        constexpr DWORD expected_server_rights = 0x0012019Fu;
        constexpr DWORD vulnerable_server_rights = 0x00120187u;
        static_assert((expected_client_rights & FILE_CREATE_PIPE_INSTANCE) == 0);
        static_assert((expected_server_rights & FILE_CREATE_PIPE_INSTANCE) != 0);
        static_assert(expected_server_rights ==
            (FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE));

        const std::wstring sid = CurrentProcessUserSid();
        if (sid.empty())
        {
            std::wcerr << L"pipe_server_acl_regression_sid_failed=" << GetLastError() << L"\n";
            return false;
        }

        const PipeAclCaseResult case_a = RunPipeAclCase(L"A", vulnerable_server_rights, sid);
        const PipeAclCaseResult case_b = RunPipeAclCase(L"B", expected_server_rights, sid);
        const bool passed =
            case_a.first_created &&
            !case_a.second_created &&
            case_a.second_error == ERROR_ACCESS_DENIED &&
            case_b.first_created &&
            case_b.second_created &&
            case_b.second_error == ERROR_SUCCESS;
        std::wcout << L"pipe_server_acl_case_a=" << (passed ? L"observed" : L"unexpected")
                   << L" first=" << (case_a.first_created ? L"success" : L"failure")
                   << L" second=" << (case_a.second_created ? L"success" : L"failure")
                   << L" error=" << case_a.second_error << L"\n";
        std::wcout << L"pipe_server_acl_case_b=" << (passed ? L"observed" : L"unexpected")
                   << L" first=" << (case_b.first_created ? L"success" : L"failure")
                   << L" second=" << (case_b.second_created ? L"success" : L"failure")
                   << L" error=" << case_b.second_error << L"\n";
        std::wcout << L"pipe_server_acl_masks=client:0x00120183 vulnerable_server:0x00120187 expected_server:0x0012019F\n";
        return passed;
    }

    [[nodiscard]] bool RunPipeGuardScenario(bool simulate_query_failure, bool simulate_exception)
    {
        const std::wstring pipe_name =
            L"\\\\.\\pipe\\Blink.Phase05.Impersonation." +
            std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(GetTickCount64());
        HANDLE server = CreateNamedPipeW(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1,
            4096,
            4096,
            0,
            nullptr);
        if (server == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        std::atomic<bool> release_client{ false };
        std::atomic<DWORD> client_error{ ERROR_SUCCESS };
        std::thread client(
            [&]()
            {
                HANDLE client_pipe = CreateFileW(
                    pipe_name.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    0,
                    nullptr,
                    OPEN_EXISTING,
                    0,
                    nullptr);
                if (client_pipe == INVALID_HANDLE_VALUE)
                {
                    client_error.store(GetLastError(), std::memory_order_relaxed);
                    return;
                }
                const char marker = 'B';
                DWORD written = 0;
                if (!WriteFile(client_pipe, &marker, sizeof(marker), &written, nullptr) || written != sizeof(marker))
                {
                    client_error.store(GetLastError(), std::memory_order_relaxed);
                }
                while (!release_client.load(std::memory_order_acquire))
                {
                    Sleep(1);
                }
                CloseHandle(client_pipe);
            });

        bool passed = true;
        const BOOL connected_call = ConnectNamedPipe(server, nullptr);
        const DWORD connect_error = GetLastError();
        const bool connected = connected_call || connect_error == ERROR_PIPE_CONNECTED;
        if (!connected)
        {
            CloseHandle(server);
            release_client.store(true, std::memory_order_release);
            client.join();
            return false;
        }

        char marker = 0;
        DWORD read = 0;
        if (connected && (!ReadFile(server, &marker, sizeof(marker), &read, nullptr) || read != sizeof(marker) || marker != 'B'))
        {
            passed = false;
        }

        PipeGuardTelemetryCapture telemetry_capture;
        PipeImpersonationState state;
        bool exception_caught = false;
        bool identity_query_failed = false;
        bool identity_query_succeeded = false;
        if (connected)
        {
            try
            {
                {
                    const PipeImpersonationTelemetry telemetry{
                        &PipeGuardTelemetryCapture::Emit,
                        &telemetry_capture
                    };
                    ScopedPipeImpersonation impersonation(server, state, telemetry);
                    if (!impersonation.Succeeded())
                    {
                        passed = false;
                    }
                    else
                    {
                        HANDLE token_handle = INVALID_HANDLE_VALUE;
                        const bool token_opened = OpenThreadToken(
                            GetCurrentThread(), TOKEN_QUERY, TRUE, &token_handle) != FALSE;
                        if (!token_opened)
                        {
                            passed = false;
                        }
                        else
                        {
                            blink::core::UniqueHandle token(token_handle);
                            DWORD required = 0;
                            if (simulate_query_failure)
                            {
                                const BOOL query = GetTokenInformation(
                                    INVALID_HANDLE_VALUE,
                                    TokenUser,
                                    nullptr,
                                    0,
                                    &required);
                                identity_query_failed = query == FALSE;
                            }
                            else
                            {
                                (void)GetTokenInformation(token.get(), TokenUser, nullptr, 0, &required);
                                std::vector<std::byte> buffer(required);
                                identity_query_succeeded = required != 0 &&
                                    GetTokenInformation(token.get(), TokenUser, buffer.data(), required, &required) != FALSE;
                            }
                        }

                        if (simulate_exception)
                        {
                            throw std::runtime_error("simulated identity query exception");
                        }
                        if (!simulate_query_failure && !impersonation.RevertNow())
                        {
                            passed = false;
                        }
                    }
                }
            }
            catch (...)
            {
                exception_caught = true;
            }
        }

        HANDLE residual_token = INVALID_HANDLE_VALUE;
        const bool residual_token_opened = OpenThreadToken(
            GetCurrentThread(), TOKEN_QUERY, TRUE, &residual_token) != FALSE;
        if (residual_token_opened)
        {
            CloseHandle(residual_token);
            passed = false;
        }
        else if (GetLastError() != ERROR_NO_TOKEN)
        {
            passed = false;
        }

        release_client.store(true, std::memory_order_release);
        client.join();
        CloseHandle(server);

        passed = passed && client_error.load(std::memory_order_relaxed) == ERROR_SUCCESS;
        passed = passed && state.impersonated && state.revert_attempted &&
            state.revert_succeeded && !state.revert_failed;
        passed = passed && telemetry_capture.impersonate_events.load(std::memory_order_relaxed) == 1;
        passed = passed && telemetry_capture.revert_attempt_events.load(std::memory_order_relaxed) >= 1;
        passed = passed && telemetry_capture.revert_success_events.load(std::memory_order_relaxed) >= 1;
        passed = passed && telemetry_capture.revert_error_events.load(std::memory_order_relaxed) == 0;
        passed = passed && telemetry_capture.callback_thread_id.load(std::memory_order_relaxed) == GetCurrentThreadId();
        if (simulate_query_failure)
        {
            passed = passed && identity_query_failed;
        }
        else if (simulate_exception)
        {
            passed = passed && exception_caught;
        }
        else
        {
            passed = passed && identity_query_succeeded;
        }
        return passed;
    }

    [[nodiscard]] bool CheckPipeIdentitySourceContract()
    {
        std::ifstream input(L"src/Blink.IndexService/src/main.cpp", std::ios::binary);
        std::string source{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
        const std::size_t identity_start = source.find("GetClientIdentity(");
        const std::size_t identity_end = source.find("CreateDataRoot", identity_start);
        const std::string identity = identity_start == std::string::npos || identity_end == std::string::npos
            ? std::string{}
            : source.substr(identity_start, identity_end - identity_start);

        constexpr DWORD kExpectedClientRights = 0x00120183;
        constexpr DWORD kExpectedServerRights = 0x0012019F;
        static_assert((kExpectedClientRights & FILE_CREATE_PIPE_INSTANCE) == 0);
        static_assert((kExpectedServerRights & FILE_CREATE_PIPE_INSTANCE) != 0);

        const bool passed =
            !identity.empty() &&
            identity.find("ScopedPipeImpersonation") != std::string::npos &&
            identity.find("RevertNow") != std::string::npos &&
            identity.find("fatal_security_failure") != std::string::npos &&
            identity.find("OpenProcessToken") == std::string::npos &&
            identity.find("OpenProcess(") == std::string::npos &&
            source.find("create_named_pipe_return") != std::string::npos &&
            (kExpectedClientRights & FILE_CREATE_PIPE_INSTANCE) == 0 &&
            source.find("FILE_GENERIC_READ") != std::string::npos &&
            source.find("FILE_GENERIC_WRITE") != std::string::npos;
        std::wcout << L"pipe_impersonation_source_contract=" << (passed ? L"passed" : L"failed")
                   << L" client_rights=0x00120183"
                   << L" server_rights=0x0012019F\n";
        return passed;
    }

    [[nodiscard]] bool RunIdlePipeReadCancellationProbe()
    {
        const std::wstring pipe_name =
            L"\\\\.\\pipe\\Blink.Phase05.ClientLifetime." +
            std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(GetTickCount64());
        HANDLE server = CreateNamedPipeW(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1,
            4096,
            4096,
            0,
            nullptr);
        if (InvalidPipeHandle(server))
        {
            std::wcerr << L"client_lifetime_server_create_error=" << GetLastError() << L"\n";
            return false;
        }

        std::atomic<bool> release_client{ false };
        std::atomic<DWORD> client_error{ ERROR_SUCCESS };
        std::thread client(
            [&]()
            {
                HANDLE client_pipe = CreateFileW(
                    pipe_name.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    0,
                    nullptr,
                    OPEN_EXISTING,
                    0,
                    nullptr);
                if (InvalidPipeHandle(client_pipe))
                {
                    client_error.store(GetLastError(), std::memory_order_release);
                    return;
                }
                while (!release_client.load(std::memory_order_acquire))
                {
                    Sleep(1);
                }
                CloseHandle(client_pipe);
            });

        const BOOL connected_call = ConnectNamedPipe(server, nullptr);
        const DWORD connect_error = GetLastError();
        const bool connected = connected_call || connect_error == ERROR_PIPE_CONNECTED;
        if (!connected)
        {
            release_client.store(true, std::memory_order_release);
            client.join();
            CloseHandle(server);
            std::wcerr << L"client_lifetime_connect_error=" << connect_error << L"\n";
            return false;
        }

        std::atomic<HANDLE> cancel_thread{ nullptr };
        std::atomic<bool> handler_ready{ false };
        std::atomic<bool> handler_finished{ false };
        std::atomic<DWORD> handler_error{ ERROR_SUCCESS };
        std::thread handler(
            [&]()
            {
                HANDLE duplicate = nullptr;
                if (!DuplicateHandle(
                        GetCurrentProcess(),
                        GetCurrentThread(),
                        GetCurrentProcess(),
                        &duplicate,
                        THREAD_ALL_ACCESS,
                        FALSE,
                        0))
                {
                    handler_error.store(GetLastError(), std::memory_order_release);
                    handler_ready.store(true, std::memory_order_release);
                    handler_finished.store(true, std::memory_order_release);
                    return;
                }
                cancel_thread.store(duplicate, std::memory_order_release);
                handler_ready.store(true, std::memory_order_release);
                char byte = 0;
                DWORD read = 0;
                if (!ReadFile(server, &byte, sizeof(byte), &read, nullptr))
                {
                    handler_error.store(GetLastError(), std::memory_order_release);
                }
                handler_finished.store(true, std::memory_order_release);
            });

        for (int attempt = 0; attempt < 2'000 &&
             !handler_ready.load(std::memory_order_acquire); ++attempt)
        {
            Sleep(1);
        }

        bool canceled = false;
        DWORD cancel_error = ERROR_SUCCESS;
        const HANDLE handler_cancel_thread = cancel_thread.load(std::memory_order_acquire);
        if (!InvalidPipeHandle(handler_cancel_thread))
        {
            canceled = CancelSynchronousIo(handler_cancel_thread) != FALSE;
            cancel_error = canceled ? ERROR_SUCCESS : GetLastError();
        }
        if (!handler_finished.load(std::memory_order_acquire))
        {
            (void)DisconnectNamedPipe(server);
        }
        handler.join();

        release_client.store(true, std::memory_order_release);
        client.join();
        (void)DisconnectNamedPipe(server);
        CloseHandle(server);

        const HANDLE duplicate = cancel_thread.exchange(nullptr);
        if (!InvalidPipeHandle(duplicate))
        {
            CloseHandle(duplicate);
        }

        const DWORD observed_handler_error = handler_error.load(std::memory_order_acquire);
        const bool passed =
            client_error.load(std::memory_order_acquire) == ERROR_SUCCESS &&
            handler_ready.load(std::memory_order_acquire) &&
            handler_finished.load(std::memory_order_acquire) &&
            (canceled || cancel_error == ERROR_NOT_FOUND) &&
            observed_handler_error != ERROR_SUCCESS;
        std::wcout << L"client_lifetime_idle_read_cancel=" << (passed ? L"passed" : L"failed")
                   << L" canceled=" << (canceled ? L"true" : L"false")
                   << L" cancel_error=" << cancel_error
                   << L" handler_error=" << observed_handler_error << L"\n";
        return passed;
    }

    [[nodiscard]] bool CheckClientSessionLifetimeSourceContract()
    {
        std::ifstream input(L"src/Blink.IndexService/src/main.cpp", std::ios::binary);
        const std::string source{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
        const std::size_t pipe_start = source.find("void PipeLoop()");
        const std::size_t handle_start = source.find("void HandleClient", pipe_start);
        const std::size_t stop_start = source.find("void Stop()");
        const std::size_t stop_end = source.find("void SchedulerLoop", stop_start);
        const std::string pipe_region = pipe_start != std::string::npos &&
                handle_start != std::string::npos
            ? source.substr(pipe_start, handle_start - pipe_start)
            : std::string{};
        const std::string stop_region = stop_start != std::string::npos &&
                stop_end != std::string::npos
            ? source.substr(stop_start, stop_end - stop_start)
            : std::string{};
        const std::size_t pipe_join = stop_region.find("pipe_thread_.join()");
        const std::size_t cancel_sessions = stop_region.find("CancelClientSessions()");
        const std::size_t join_sessions = stop_region.find("JoinAllClientSessions()");
        const bool passed =
            source.find("struct ClientSession") != std::string::npos &&
            source.find("client_sessions_mutex_") != std::string::npos &&
            source.find("CancelSynchronousIo(cancel_thread)") != std::string::npos &&
            source.find("void StartClientSession(HANDLE pipe)") != std::string::npos &&
            pipe_region.find("StartClientSession(pipe)") != std::string::npos &&
            pipe_region.find(".detach()") == std::string::npos &&
            pipe_join != std::string::npos &&
            cancel_sessions != std::string::npos &&
            join_sessions != std::string::npos &&
            pipe_join < cancel_sessions &&
            cancel_sessions < join_sessions;
        std::wcout << L"client_session_lifetime_source_contract="
                   << (passed ? L"passed" : L"failed") << L"\n";
        return passed;
    }

    [[nodiscard]] bool RunClientSessionLifetimeTests()
    {
        const bool cancel_probe = RunIdlePipeReadCancellationProbe();
        const bool source_contract = CheckClientSessionLifetimeSourceContract();
        return cancel_probe && source_contract;
    }

    [[nodiscard]] bool RunPipeImpersonationSecurityTests()
    {
        const bool normal = RunPipeGuardScenario(false, false);
        const bool query_failure = RunPipeGuardScenario(true, false);
        const bool exception_path = RunPipeGuardScenario(false, true);
        const bool server_acl = RunNamedPipeServerAclRegression();
        const bool source_contract = CheckPipeIdentitySourceContract();
        std::wcout << L"pipe_impersonation_normal=" << (normal ? L"passed" : L"failed") << L"\n";
        std::wcout << L"pipe_identity_query_failure_revert=" << (query_failure ? L"passed" : L"failed") << L"\n";
        std::wcout << L"pipe_impersonation_exception_revert=" << (exception_path ? L"passed" : L"failed") << L"\n";
        std::wcout << L"pipe_server_acl_second_instance=" << (server_acl ? L"passed" : L"failed") << L"\n";
        std::wcout << L"pipe_client_rights_no_create_instance=" << (source_contract ? L"passed" : L"failed") << L"\n";
        return normal && query_failure && exception_path && server_acl && source_contract;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    const bool offline_suite = argc > 2 && std::wstring(argv[1]) == L"--search-index-only" &&
        std::wstring(argv[2]) == L"--suite";
    if ((argc > 1 && std::wstring(argv[1]) == L"--suite") || offline_suite)
    {
        const int suite_argument = offline_suite ? 3 : 2;
        if (argc == suite_argument + 1)
        {
            const std::wstring suite = argv[suite_argument];
            if (suite == L"search-correctness") { return RunSearchCorrectnessTests() ? 0 : 94; }
            if (suite == L"search-visibility-wire") { return RunSearchVisibilityWireTests() ? 0 : 95; }
        }
        std::wcerr << L"usage: Blink.Phase05.Tests.exe [--search-index-only] --suite search-correctness|search-visibility-wire\n";
        return 2;
    }
    if (argc > 1 && std::wstring(argv[1]) == L"--offline-search")
    {
        return RunOfflineSearch(argc, argv);
    }

    if (argc > 1 && std::wstring(argv[1]) == L"--progress-only")
    {
        return RunProgressSnapshotUnitTest() ? 0 : 90;
    }

    if (argc > 1 && std::wstring(argv[1]) == L"--security-only")
    {
        return RunPipeImpersonationSecurityTests() ? 0 : 91;
    }

    if (argc > 1 && std::wstring(argv[1]) == L"--lifetime-only")
    {
        return RunClientSessionLifetimeTests() ? 0 : 92;
    }

    if (argc > 1 && std::wstring(argv[1]) == L"--search-index-only")
    {
        if (!RunProgressSnapshotUnitTest())
        {
            return 90;
        }
        const int derived = RunDerivedExtensionIndexUnitTest();
        if (derived != 0) { return derived; }
        if (!RunLoadIsolationUnitTest()) { return 93; }
        const bool correctness = RunSearchCorrectnessTests();
        const bool wire = RunSearchVisibilityWireTests();
        return correctness && wire ? 0 : 94;
    }

    if (!RunProgressSnapshotUnitTest())
    {
        return 90;
    }
    if (!RunSearchCorrectnessTests()) { return 94; }
    if (!RunSearchVisibilityWireTests()) { return 95; }

    const std::wstring service_exe = FullPath(L"x64\\Debug\\Blink.IndexService.exe");
    const std::wstring data_root = L".build\\phase05-service-data";
    std::error_code cleanup_error;
    std::filesystem::remove_all(data_root, cleanup_error);
    std::filesystem::create_directories(std::filesystem::path(data_root) / L"index", cleanup_error);
    {
        std::ofstream corrupt_store(std::filesystem::path(data_root) / L"index" / L"d.blink5", std::ios::binary | std::ios::trunc);
        corrupt_store << "corrupt-store-probe";
        corrupt_store.flush();
        if (!corrupt_store)
        {
            std::wcerr << L"failed_to_create_corrupt_store_file\n";
            return 99;
        }
    }

    PROCESS_INFORMATION process_info{};
    if (!StartProcess(service_exe, FullPath(data_root), process_info))
    {
        std::wcerr << L"start_service_failed=" << GetLastError() << L"\n";
        return 1;
    }

    blink::index::ipc::Client client;
    DWORD error = ERROR_SUCCESS;
    bool connected = false;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (client.Connect(100, &error))
        {
            connected = true;
            break;
        }
        if (WaitForSingleObject(process_info.hProcess, 0) != WAIT_TIMEOUT)
        {
            break;
        }
        Sleep(100);
    }
    if (!connected)
    {
        std::wcerr << L"connect_service_failed=" << error << L"\n";
        CloseProcess(process_info);
        return 2;
    }

    if (!CheckPipeSecurityTelemetry(FullPath(data_root)))
    {
        client.Disconnect();
        CloseProcess(process_info);
        return 11;
    }

    blink::index::ipc::Request request;
    blink::index::ipc::Response response;
    request.type = blink::index::ipc::RequestType::QueryServiceStatus;
    const bool status_ok = client.Send(request, response, 2000, &error);
    if (!status_ok)
    {
        std::wcerr << L"query_status_transport_failed=" << error << L"\n";
        CloseProcess(process_info);
        return 3;
    }
    PrintResponse(L"service_status", response);

    request = {};
    request.type = blink::index::ipc::RequestType::QueryVolumeStatus;
    request.volume_root = L"D:\\";
    const bool corrupt_ok = client.Send(request, response, 2000, &error);
    if (!corrupt_ok)
    {
        std::wcerr << L"corrupt_store_transport_failed=" << error << L"\n";
        CloseProcess(process_info);
        return 10;
    }
    PrintResponse(L"corrupt_store", response);
    const bool corrupt_rejected = response.index_state == blink::index::ipc::IndexState::Corrupted;

    request = {};
    request.type = blink::index::ipc::RequestType::StartInitialIndex;
    request.volume_root = L"not-a-volume";
    const bool invalid_ok = client.Send(request, response, 2000, &error);
    if (!invalid_ok)
    {
        std::wcerr << L"invalid_request_transport_failed=" << error << L"\n";
        CloseProcess(process_info);
        return 4;
    }
    PrintResponse(L"invalid_root", response);
    const bool invalid_rejected = response.status == blink::index::ipc::ResponseStatus::InvalidRequest;

    request = {};
    request.type = blink::index::ipc::RequestType::StartInitialIndex;
    request.volume_root = L"D:\\";
    const bool start_ok = client.Send(request, response, 2000, &error);
    if (!start_ok)
    {
        std::wcerr << L"start_index_transport_failed=" << error << L"\n";
        CloseProcess(process_info);
        return 5;
    }
    PrintResponse(L"start_initial", response);
    const std::uint64_t operation_id = response.operation_id;

    bool finished = false;
    blink::index::ipc::Response progress;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        request = {};
        request.type = blink::index::ipc::RequestType::QueryIndexProgress;
        request.operation_id = operation_id;
        if (!client.Send(request, progress, 2000, &error))
        {
            std::wcerr << L"progress_transport_failed=" << error << L"\n";
            CloseProcess(process_info);
            return 6;
        }
        if (attempt == 0 || progress.error != ERROR_IO_PENDING)
        {
            PrintResponse(L"progress", progress);
        }
        if (progress.error != ERROR_IO_PENDING)
        {
            finished = true;
            break;
        }
        Sleep(100);
    }
    if (!finished)
    {
        std::wcerr << L"operation_did_not_finish\n";
        CloseProcess(process_info);
        return 7;
    }

    client.Disconnect();
    CloseProcess(process_info);
    blink::index::ipc::Client stopped_client;
    DWORD stopped_error = ERROR_SUCCESS;
    const bool reconnected_after_stop = stopped_client.Connect(100, &stopped_error);
    if (reconnected_after_stop)
    {
        std::wcerr << L"service_reconnected_after_process_kill\n";
        stopped_client.Disconnect();
        return 9;
    }
    std::wcout << L"service_stopped_transport_error=" << stopped_error << L"\n";
    std::filesystem::remove_all(data_root, cleanup_error);

    if (!invalid_rejected || !corrupt_rejected)
    {
        std::wcerr << L"invalid_or_corrupt_store_was_not_rejected\n";
        return 8;
    }
    std::wcout << L"phase05_ipc_service_tests=passed\n";
    return 0;
}
