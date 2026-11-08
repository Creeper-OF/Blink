#include "Blink/Provider/Contracts.h"
#include "Blink/Provider/FakeProvider.h"

namespace
{
    [[nodiscard]] std::wstring ArgumentValue(int argc, wchar_t** argv, const wchar_t* name)
    {
        for (int index = 0; index + 1 < argc; ++index)
        {
            if (std::wstring(argv[index]) == name)
            {
                return argv[index + 1];
            }
        }
        return {};
    }
}

int wmain(int argc, wchar_t** argv)
{
    const std::wstring pipe_token = ArgumentValue(argc, argv, L"--pipe-token");
    const std::wstring mode = ArgumentValue(argc, argv, L"--mode");
    if (pipe_token.empty())
    {
        return ERROR_INVALID_PARAMETER;
    }
    if (pipe_token.find_first_of(L"\\/\"\t\r\n") != std::wstring::npos)
    {
        return ERROR_INVALID_PARAMETER;
    }
    const std::wstring pipe = L"\\\\.\\pipe\\" + pipe_token;
    return blink::provider::ProviderServer::Run(pipe, blink::provider::ParseMode(mode));
}
