#pragma once

#include "Blink/Provider/Contracts.h"

#include <string>

namespace blink::provider::fake
{
    [[nodiscard]] std::wstring NormalDirectoryResponse(const std::wstring& path);
    [[nodiscard]] std::wstring ErrorResponse();
}

