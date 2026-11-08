#include "Blink/Provider/FakeProvider.h"

namespace blink::provider::fake
{
    std::wstring NormalDirectoryResponse(const std::wstring& path)
    {
        return L"OK\tFakeProvider normal response for " + path + L"\nentry-a\nentry-b";
    }

    std::wstring ErrorResponse()
    {
        return L"ERROR\tFakeProvider returned a deliberate error";
    }
}

