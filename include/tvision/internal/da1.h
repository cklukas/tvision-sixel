#ifndef TVISION_DA1_H
#define TVISION_DA1_H

#include <stddef.h>

namespace tvision
{

// Result of parsing DEC Primary Device Attributes responses.  A response can
// advertise SIXEL with parameter 4; a valid response without it explicitly
// says that SIXEL is not advertised.  Unknown covers an absent or malformed
// response and lets the caller apply its timeout/fallback policy.
enum class Da1SixelResult
{
    Unknown,
    Unsupported,
    Supported,
};

inline Da1SixelResult parseDa1Sixel(const char *data, size_t size) noexcept
{
    Da1SixelResult result = Da1SixelResult::Unknown;
    for (size_t i = 0; i + 3 < size; ++i)
    {
        if (data[i] != '\x1b' || data[i + 1] != '[' || data[i + 2] != '?')
            continue;

        size_t p = i + 3;
        bool valid = true;
        bool haveParameter = false;
        int parameter = 0;
        bool sixel = false;
        for (; p < size && data[p] != 'c'; ++p)
        {
            const char ch = data[p];
            if (ch >= '0' && ch <= '9')
            {
                haveParameter = true;
                if (parameter <= 1000000)
                    parameter = parameter * 10 + (ch - '0');
            }
            else if (ch == ';')
            {
                if (haveParameter && parameter == 4)
                    sixel = true;
                haveParameter = false;
                parameter = 0;
            }
            else
            {
                valid = false;
                break;
            }
        }
        if (p == size || !valid)
            continue;
        if (haveParameter && parameter == 4)
            sixel = true;
        result = sixel ? Da1SixelResult::Supported : Da1SixelResult::Unsupported;
        if (sixel)
            return result;
        i = p;
    }
    return result;
}

} // namespace tvision

#endif // TVISION_DA1_H
