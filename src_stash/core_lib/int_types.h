#pragma once

#include <stdint.h>

#define GT_SOURCE_INFO {__LINE__, __FILE__}

namespace lc
{
    struct SourceInfo
    {
        size_t      line = 0;
        const char* file = "";
        //const char* function = "";

        SourceInfo() = default;
        SourceInfo(size_t l, const char* f) : line(l), file(f) {}
    };
}