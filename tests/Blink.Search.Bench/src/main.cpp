#include "Blink/Index/Contracts.h"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    namespace ipc = blink::index::ipc;
    using Clock = std::chrono::steady_clock;
    double Milliseconds(const Clock::duration value) { return std::chrono::duration<double, std::milli>(value).count(); }
    std::string Utf8(const std::wstring& text)
    {
        if (text.empty()) { return {}; }
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>((std::max)(0, size)), '\0');
        if (size) { WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr); }
        return result;
    }
    std::string Json(const std::wstring& value)
    {
        std::string result = "\"";
        for (const auto c : Utf8(value))
        {
            if (c == '\\' || c == '"') { result.push_back('\\'); result.push_back(c); }
            else if (c == '\n') { result += "\\n"; }
            else if (c == '\r') { result += "\\r"; }
            else if (c == '\t') { result += "\\t"; }
            else { result.push_back(c); }
        }
        return result + '"';
    }
    struct Query { const char* name; std::wstring text, extension, path_prefix; };
    struct Sample
    {
        ipc::Response response;
        DWORD transport_error{};
        bool received{};
        unsigned requests{}, busy_replies{}, busy_retries{};
        double rpc_ms{}, total_ms{}, busy_wait_ms{}, connect_ms{};
    };
    bool Pending(const ipc::Response& response)
    {
        if (response.status == ipc::ResponseStatus::NotFound) { return false; }
        return response.error == ERROR_IO_PENDING || (response.status == ipc::ResponseStatus::Busy &&
            (response.error == ERROR_SUCCESS || response.error == ERROR_BUSY));
    }
    Sample Measure(ipc::Client& client, const std::wstring& root, const Query& query)
    {
        Sample sample;
        const auto start = Clock::now(), deadline = start + std::chrono::seconds(30);
        for (;;)
        {
            if (!client.IsConnected())
            {
                const auto connect_start = Clock::now();
                const bool connected = client.Connect(1000, &sample.transport_error);
                sample.connect_ms += Milliseconds(Clock::now() - connect_start);
                if (!connected) { break; }
            }
            ipc::Request request; request.type = ipc::RequestType::Search; request.volume_root = root;
            request.text = query.text; request.extension = query.extension; request.path_prefix = query.path_prefix;
            request.limit = 200; request.prefix = false;
            const auto request_start = Clock::now();
            ++sample.requests;
            sample.received = client.Send(request, sample.response, 5000, &sample.transport_error);
            sample.rpc_ms = Milliseconds(Clock::now() - request_start);
            if (!sample.received) { client.Disconnect(); break; }
            if (!Pending(sample.response)) { break; }
            ++sample.busy_replies;
            sample.busy_wait_ms += sample.rpc_ms;
            if (Clock::now() + std::chrono::milliseconds(30) + std::chrono::seconds(6) > deadline) { break; }
            const auto wait_start = Clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            sample.busy_wait_ms += Milliseconds(Clock::now() - wait_start);
            ++sample.busy_retries;
        }
        sample.total_ms = Milliseconds(Clock::now() - start);
        return sample;
    }
    std::wstring Folder(const std::wstring& root)
    {
        wchar_t current[32768]{};
        const auto size = GetCurrentDirectoryW(static_cast<DWORD>(std::size(current)), current);
        std::wstring folder = size && size < std::size(current) ? current : L"";
        if (folder.size() < 3 || std::towupper(folder[0]) != root[0] || folder[1] != L':')
        { folder = root == L"C:\\" ? root + L"Windows" : root; }
        if (!folder.empty() && folder.back() != L'\\') { folder += L'\\'; }
        return folder;
    }
}

int wmain(const int argc, wchar_t* argv[])
{
    unsigned iterations = 3;
    std::filesystem::path output;
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring option = argv[i];
            if (option == L"--iterations" && i + 1 < argc) { iterations = static_cast<unsigned>(std::stoul(argv[++i])); }
            else if (option == L"--output" && i + 1 < argc) { output = argv[++i]; }
            else { std::cerr << "Usage: Blink.Search.Bench.exe [--iterations N] [--output path]\n"; return 2; }
        }
        if (!iterations || iterations > 100) { std::cerr << "iterations must be 1..100\n"; return 2; }
        std::ofstream file;
        if (!output.empty())
        {
            if (!output.parent_path().empty()) { std::filesystem::create_directories(output.parent_path()); }
            file.open(output, std::ios::binary | std::ios::trunc);
            if (!file) { std::cerr << "Cannot create output\n"; return 3; }
        }
        const auto emit = [&](const std::string& line) {
            std::cout << line << std::endl;
            if (file.is_open()) { file << line << std::endl; }
        };
        emit("{\"kind\":\"meta\",\"suite\":1,\"measurement\":\"service_ipc_roundtrip_not_ui\",\"limit\":200,\"busy_interval_ms\":30,\"busy_budget_ms\":30000,\"connect_timeout_ms\":1000,\"send_timeout_ms\":5000,\"iterations\":" + std::to_string(iterations) + "}");
        ipc::Client client;
        unsigned failures{};
        for (unsigned round = 0; round < iterations; ++round)
        {
            for (const std::wstring root : { L"C:\\", L"D:\\" })
            {
                const std::vector<Query> queries{
                    { "unique_no_match", L"__BlinkSearchBenchmark_NoMatch_86f72ce9_c5ba_45a6_8d49_b2470aec9e7d__", {}, {} },
                    { "sparse_name", L"Blink.Phase1", {}, {} },
                    { "broad_name", L"a", {}, {} },
                    { "extension", {}, L".txt", {} },
                    { "current_directory", L"a", {}, Folder(root) }
                };
                for (std::size_t position = 0; position <= queries.size(); ++position)
                {
                    const bool switching = position == 0;
                    const auto& query = queries[switching ? 0 : position - 1];
                    const auto sample = Measure(client, root, query);
                    const bool ok = sample.received && !sample.transport_error && !sample.response.error &&
                        sample.response.status == ipc::ResponseStatus::Ok;
                    if (!ok) { ++failures; }
                    std::ostringstream line; line << std::fixed << std::setprecision(3);
                    line << "{\"kind\":\"sample\",\"round\":" << round << ",\"first_round\":" << (round == 0 ? "true" : "false") <<
                        ",\"root\":" << Json(root) << ",\"phase\":\"" << (switching ? "switch_probe" : "hot") <<
                        "\",\"query\":\"" << query.name << "\",\"text\":" << Json(query.text) << ",\"extension\":" << Json(query.extension) <<
                        ",\"path_prefix\":" << Json(query.path_prefix) << ",\"limit\":200,\"received\":" << (sample.received ? "true" : "false") <<
                        ",\"status\":" << (sample.received ? static_cast<int>(sample.response.status) : -1) <<
                        ",\"error\":" << (sample.received ? sample.response.error : sample.transport_error) <<
                        ",\"index_state\":" << static_cast<unsigned>(sample.response.index_state) <<
                        ",\"result_count\":" << sample.response.results.size() << ",\"requests\":" << sample.requests <<
                        ",\"busy_replies\":" << sample.busy_replies << ",\"busy_retries\":" << sample.busy_retries <<
                        ",\"rpc_ms\":" << sample.rpc_ms << ",\"sample_ms\":" << sample.total_ms <<
                        ",\"busy_wait_ms\":" << sample.busy_wait_ms << ",\"connect_ms\":" << sample.connect_ms <<
                        ",\"warm_eligible\":" << (ok && !switching && !sample.busy_replies ? "true" : "false") << "}";
                    emit(line.str());
                }
            }
        }
        emit("{\"kind\":\"summary\",\"samples\":" + std::to_string(iterations * 12) + ",\"failed_samples\":" + std::to_string(failures) + "}");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 4; }
}
