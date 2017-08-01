#pragma once
#include <foundation/int_types.h>

#define GT_TOOLS_PROTOCOL_ID 0xc0ffefe

namespace gt
{
    namespace toolsProtocol
    {
        enum class ClientPacketID : uint16_t
        {
            CONNECTION
        };

    }
}