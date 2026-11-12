#include "SearchVisibilityWireTests.h"
#include "Blink/Index/Contracts.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    namespace ipc = blink::index::ipc;
    using Bytes = std::vector<std::byte>;

    bool Check(const bool condition, const char* name)
    {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        return condition;
    }

    template <typename T>
    void Append(Bytes& bytes, const T& value)
    {
        const auto begin = reinterpret_cast<const std::byte*>(&value);
        bytes.insert(bytes.end(), begin, begin + sizeof(T));
    }

    void AppendString(Bytes& bytes, const std::wstring& value)
    {
        Append(bytes, static_cast<std::uint32_t>(value.size()));
        const auto begin = reinterpret_cast<const std::byte*>(value.data());
        bytes.insert(bytes.end(), begin, begin + value.size() * sizeof(wchar_t));
    }

    // A fixed v1 client fixture, independent of the private serializer. New
    // request kinds must fit the same legacy fields, with no extra flag bytes.
    Bytes LegacyRequestFrame(const ipc::Request& request)
    {
        Bytes bytes;
        Append(bytes, std::uint32_t{ 0x31504C42 }); // BLP1
        Append(bytes, std::uint32_t{ 1 }); // request
        Append(bytes, std::uint32_t{ 1 }); // version
        Append(bytes, request.request_id);
        Append(bytes, static_cast<std::uint32_t>(request.type));
        Append(bytes, request.operation_id);
        Append(bytes, static_cast<std::uint8_t>(request.prefix ? 1 : 0));
        Append(bytes, request.limit);
        AppendString(bytes, request.volume_root);
        AppendString(bytes, request.text);
        AppendString(bytes, request.extension);
        AppendString(bytes, request.path_prefix);
        return bytes;
    }

    bool SameRequest(const ipc::Request& actual, const ipc::Request& expected)
    {
        return actual.protocol_version == expected.protocol_version && actual.request_id == expected.request_id &&
            actual.type == expected.type && actual.operation_id == expected.operation_id &&
            actual.volume_root == expected.volume_root && actual.text == expected.text &&
            actual.extension == expected.extension && actual.path_prefix == expected.path_prefix &&
            actual.prefix == expected.prefix && actual.limit == expected.limit;
    }

    void AppendIdentity(Bytes& bytes, const blink::core::FileIdentity& value)
    {
        Append(bytes, value.volume_serial);
        Append(bytes, static_cast<std::uint8_t>(value.valid ? 1 : 0));
        for (const auto byte : value.file_id) { Append(bytes, byte); }
    }

    // Describe the existing v1 response bytes, including a Unicode hit. This
    // checks that adding commands has not silently extended either frame.
    Bytes LegacyResponseFrame(const ipc::Response& response)
    {
        Bytes bytes;
        Append(bytes, std::uint32_t{ 0x31504C42 });
        Append(bytes, std::uint32_t{ 2 }); // response
        Append(bytes, std::uint32_t{ 1 });
        Append(bytes, response.request_id);
        Append(bytes, static_cast<std::uint32_t>(response.status));
        Append(bytes, response.error);
        Append(bytes, static_cast<std::uint32_t>(response.service_state));
        Append(bytes, static_cast<std::uint32_t>(response.index_state));
        Append(bytes, response.operation_id);
        Append(bytes, response.records_seen);
        Append(bytes, response.files);
        Append(bytes, response.directories);
        Append(bytes, response.index_bytes);
        Append(bytes, response.last_usn);
        Append(bytes, response.journal_id);
        AppendString(bytes, response.message);
        Append(bytes, static_cast<std::uint32_t>(response.results.size()));
        for (const auto& hit : response.results)
        {
            AppendIdentity(bytes, hit.identity);
            AppendIdentity(bytes, hit.parent_identity);
            Append(bytes, hit.attributes);
            Append(bytes, hit.last_usn);
            Append(bytes, static_cast<std::uint8_t>(hit.is_directory ? 1 : 0));
            AppendString(bytes, hit.name);
            AppendString(bytes, hit.full_path);
        }
        return bytes;
    }

    struct WireResult
    {
        bool request_matches{};
        bool response_matches{};
        DWORD error{};
    };

    WireResult ExchangeLegacyFrame(const ipc::Request& expected, const unsigned ordinal)
    {
        const auto pipe_name = L"\\\\.\\pipe\\Blink.Phase05.SearchVisibilityWire." +
            std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) +
            L"." + std::to_wstring(ordinal);
        blink::core::UniqueHandle server(CreateNamedPipeW(pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 4096, 4096, 1000, nullptr));
        if (!server) { return { false, false, GetLastError() }; }
        blink::core::UniqueHandle client(CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!client) { return { false, false, GetLastError() }; }
        // The client is already connected; this is the documented
        // ERROR_PIPE_CONNECTED path, so no background thread or sleep is needed.
        if (!ConnectNamedPipe(server.get(), nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
        { return { false, false, GetLastError() }; }
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(client.get(), &mode, nullptr, nullptr))
        { return { false, false, GetLastError() }; }

        const auto request_bytes = LegacyRequestFrame(expected);
        DWORD written{};
        if (!WriteFile(client.get(), request_bytes.data(), static_cast<DWORD>(request_bytes.size()), &written, nullptr))
        { return { false, false, GetLastError() }; }
        if (written != request_bytes.size()) { return { false, false, ERROR_WRITE_FAULT }; }
        ipc::Request actual;
        DWORD error{};
        if (!ipc::ReadRequest(server.get(), actual, &error)) { return { false, false, error }; }
        WireResult result; result.request_matches = SameRequest(actual, expected);

        const bool unknown = static_cast<std::uint32_t>(actual.type) == 0xFFFFFFFFu;
        ipc::Response response;
        response.request_id = actual.request_id;
        response.status = unknown ? ipc::ResponseStatus::InvalidRequest : ipc::ResponseStatus::Ok;
        response.error = unknown ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
        response.service_state = ipc::ServiceState::Running;
        response.index_state = ipc::IndexState::Healthy;
        response.operation_id = 29;
        response.records_seen = 101; response.files = 88; response.directories = 13;
        response.index_bytes = 4097; response.last_usn = 501; response.journal_id = 7001;
        response.message = unknown ? L"Unknown IndexService command" : L"v1 visibility fixture";
        if (!unknown)
        {
            ipc::SearchHit hit;
            hit.identity = { 0x8877665544332211ull, { 3, 4, 5 }, true };
            hit.parent_identity = { hit.identity.volume_serial, { 9, 8, 7 }, true };
            hit.attributes = FILE_ATTRIBUTE_NORMAL; hit.last_usn = 499;
            hit.name = L"\u00c9cole-\u6587\u4ef6.txt";
            hit.full_path = expected.path_prefix + hit.name;
            response.results.push_back(std::move(hit));
        }
        if (!ipc::WriteResponse(server.get(), response, &error))
        { result.error = error; return result; }
        Bytes bytes(4096);
        DWORD read{};
        if (!ReadFile(client.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
        { result.error = GetLastError(); return result; }
        bytes.resize(read);
        result.response_matches = bytes == LegacyResponseFrame(response);
        return result;
    }
}

bool RunSearchVisibilityWireTests()
{
    bool passed = true;
    passed &= Check(ipc::kProtocolVersion == 1 &&
        std::wstring(ipc::kPipeName) == L"\\\\.\\pipe\\Blink.IndexService.v1" &&
        static_cast<std::uint32_t>(ipc::RequestType::Search) == 5 &&
        static_cast<std::uint32_t>(ipc::RequestType::SearchDirectChildren) == 7 &&
        static_cast<std::uint32_t>(ipc::RequestType::SearchVisible) == 8 &&
        static_cast<std::uint32_t>(ipc::RequestType::SearchVisibleDirectChildren) == 9 &&
        static_cast<std::uint32_t>(ipc::RequestType::SearchWithHidden) == 10 &&
        static_cast<std::uint32_t>(ipc::RequestType::SearchWithHiddenDirectChildren) == 11,
        "search_visibility_wire_keeps_legacy_version_pipe_and_command_values");
    passed &= Check(std::wstring(ipc::RequestTypeName(ipc::RequestType::SearchVisible)) == L"SearchVisible" &&
        std::wstring(ipc::RequestTypeName(ipc::RequestType::SearchVisibleDirectChildren)) == L"SearchVisibleDirectChildren" &&
        std::wstring(ipc::RequestTypeName(ipc::RequestType::SearchWithHidden)) == L"SearchWithHidden" &&
        std::wstring(ipc::RequestTypeName(ipc::RequestType::SearchWithHiddenDirectChildren)) == L"SearchWithHiddenDirectChildren" &&
        std::wstring(ipc::RequestTypeName(static_cast<ipc::RequestType>(0xFFFFFFFFu))) == L"Unknown",
        "search_visibility_wire_names_four_readonly_commands_without_accepting_unknown_names");

    struct Case
    {
        ipc::RequestType type;
        const char* request_check;
        const char* response_check;
    };
    const std::array<Case, 7> cases{{
        { ipc::RequestType::Search, "search_visibility_wire_legacy_search_payload_roundtrips",
            "search_visibility_wire_legacy_search_response_stays_v1" },
        { ipc::RequestType::SearchDirectChildren, "search_visibility_wire_legacy_direct_children_payload_roundtrips",
            "search_visibility_wire_legacy_direct_children_response_stays_v1" },
        { ipc::RequestType::SearchVisible, "search_visibility_wire_visible_payload_roundtrips_without_extra_fields",
            "search_visibility_wire_visible_response_stays_v1" },
        { ipc::RequestType::SearchVisibleDirectChildren, "search_visibility_wire_visible_direct_children_payload_roundtrips",
            "search_visibility_wire_visible_direct_children_response_stays_v1" },
        { ipc::RequestType::SearchWithHidden, "search_visibility_wire_hidden_payload_roundtrips_without_extra_fields",
            "search_visibility_wire_hidden_response_stays_v1" },
        { ipc::RequestType::SearchWithHiddenDirectChildren, "search_visibility_wire_hidden_direct_children_payload_roundtrips",
            "search_visibility_wire_hidden_direct_children_response_stays_v1" },
        { static_cast<ipc::RequestType>(0xFFFFFFFFu), "search_visibility_wire_unknown_kind_survives_transport_for_service_rejection",
            "search_visibility_wire_existing_invalid_request_error_response_stays_v1" },
    }};
    for (unsigned index{}; index < cases.size(); ++index)
    {
        ipc::Request request;
        request.type = cases[index].type; request.request_id = 0x0102030405060708ull + index;
        request.operation_id = 0x8877665544332211ull;
        request.volume_root = L"D:\\"; request.text = L"\u00e9cole";
        request.extension = L".TXT"; request.path_prefix = L"D:\\\u00c9cole\\";
        request.prefix = index % 2 == 0; request.limit = 137;
        const auto result = ExchangeLegacyFrame(request, index);
        if (result.error) { std::cout << "INFO search_visibility_wire_case=" << index << " error=" << result.error << '\n'; }
        passed &= Check(result.request_matches, cases[index].request_check);
        passed &= Check(result.response_matches, cases[index].response_check);
    }
    return passed;
}
