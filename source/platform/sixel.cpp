/*
 * Sixel graphics support additions and modifications:
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#include <internal/sixel.h>

#include <algorithm>
#include <math.h>
#include <unordered_map>
#include <vector>

namespace tvision
{

namespace
{

struct Rgb
{
    uchar r, g, b;
};

static Rgb unpack(uint32_t p) noexcept
{
    return {
        uchar((p >> 16) & 0xFF),
        uchar((p >> 8) & 0xFF),
        uchar(p & 0xFF),
    };
}

static bool opaque(uint32_t p) noexcept
{
    return ((p >> 24) & 0xFF) >= 128;
}

static int colorKey(Rgb c) noexcept
{
    return (int(c.r) << 16) | (int(c.g) << 8) | int(c.b);
}

static int quantizeKey(Rgb c, int levels) noexcept
{
    if (levels <= 1)
        return 0;
    auto q = [levels] (uchar v) {
        return int(v) * (levels - 1) / 255;
    };
    return q(c.r)*levels*levels + q(c.g)*levels + q(c.b);
}

static Rgb dequantize(int key, int levels) noexcept
{
    if (levels <= 1)
        return {0, 0, 0};
    int b = key % levels;
    int g = (key / levels) % levels;
    int r = key / (levels*levels);
    auto dq = [levels] (int v) {
        return uchar((v*255 + (levels - 1)/2)/(levels - 1));
    };
    return {dq(r), dq(g), dq(b)};
}

static int cubeLevels(int maxColors) noexcept
{
    int levels = 1;
    while ((levels + 1)*(levels + 1)*(levels + 1) <= maxColors)
        ++levels;
    return std::max(1, levels);
}

static int bayer8(int x, int y) noexcept
{
    static const uchar matrix[8][8] = {
        { 0, 48, 12, 60,  3, 51, 15, 63},
        {32, 16, 44, 28, 35, 19, 47, 31},
        { 8, 56,  4, 52, 11, 59,  7, 55},
        {40, 24, 36, 20, 43, 27, 39, 23},
        { 2, 50, 14, 62,  1, 49, 13, 61},
        {34, 18, 46, 30, 33, 17, 45, 29},
        {10, 58,  6, 54,  9, 57,  5, 53},
        {42, 26, 38, 22, 41, 25, 37, 21}
    };
    return matrix[y & 7][x & 7];
}

// Ordered-dither one channel to a level in [0, levels-1] from the Bayer
// threshold t in [0, 63]: floor(v*(levels-1)/255 + (t + 0.5)/64), integer-only
// so the encoder stays byte-deterministic.
static int ditherLevel(int v, int levels, int t) noexcept
{
    int n = v*(levels - 1)*64 + t*255 + 127; // 127 ~= 0.5*255, the (t + 0.5) bias
    int level = n/16320;                     // 16320 == 255*64
    return level > levels - 1 ? levels - 1 : level;
}

// Bayer counterpart of quantizeKey: the three channels use phase-shifted
// thresholds so the stipple does not tint toward a single hue.
static int ditherKey(Rgb c, int levels, int x, int y) noexcept
{
    if (levels <= 1)
        return 0;
    int t = bayer8(x, y);
    int lr = ditherLevel(c.r, levels, t);
    int lg = ditherLevel(c.g, levels, (t + 21) & 63);
    int lb = ditherLevel(c.b, levels, (t + 42) & 63);
    return lr*levels*levels + lg*levels + lb;
}

static void appendNumber(std::string &out, int n)
{
    char buf[16];
    char *p = buf + sizeof(buf);
    unsigned value = n < 0 ? unsigned(-n) : unsigned(n);
    do
    {
        *--p = char('0' + value % 10);
        value /= 10;
    } while (value != 0);
    if (n < 0)
        *--p = '-';
    out.append(p, buf + sizeof(buf) - p);
}

static void appendPaletteColor(std::string &out, int idx, Rgb c)
{
    out.push_back('#');
    appendNumber(out, idx);
    out.append(";2;", 3);
    appendNumber(out, c.r*100/255);
    out.push_back(';');
    appendNumber(out, c.g*100/255);
    out.push_back(';');
    appendNumber(out, c.b*100/255);
}

static void markActive(std::vector<uint64_t> &active, int wordsPerBand,
                       int band, int color) noexcept
{
    active[band*wordsPerBand + color/64] |= uint64_t(1) << (color & 63);
}

static bool isActive(const std::vector<uint64_t> &active, int wordsPerBand,
                     int band, int color) noexcept
{
    return (active[band*wordsPerBand + color/64] & (uint64_t(1) << (color & 63))) != 0;
}

static void appendRun(std::string &out, int ch, int len)
{
    if (len >= 4)
    {
        out.push_back('!');
        appendNumber(out, len);
        out.push_back(char(ch));
    }
    else
        while (len-- > 0)
            out.push_back(char(ch));
}

} // namespace

std::string encodeSixel(const uint32_t *pixels, TPoint size, int maxColors,
                        TGraphicDitherMode dither)
{
    if (!pixels || size.x <= 0 || size.y <= 0)
        return std::string();
    maxColors = std::max(2, std::min(maxColors, 4096));
    int width = size.x;
    int height = size.y;
    int pixelCount = width*height;
    int bandCount = (height + 5)/6;
    int wordsPerBand = (maxColors + 63)/64;

    std::vector<Rgb> palette;
    std::vector<short> index(pixelCount, -1);
    std::vector<uint64_t> active(bandCount*wordsPerBand, 0);

    std::unordered_map<int, int> exact;
    exact.reserve(maxColors*2);
    bool exactPalette = true;
    for (int y = 0; y < height && exactPalette; ++y)
    {
        int band = y/6;
        int row = y*width;
        for (int x = 0; x < width; ++x)
        {
            int i = row + x;
            if (!opaque(pixels[i]))
                continue;
            Rgb rgb = unpack(pixels[i]);
            int key = colorKey(rgb);
            auto found = exact.find(key);
            if (found == exact.end())
            {
                if ((int) palette.size() >= maxColors)
                {
                    exactPalette = false;
                    break;
                }
                found = exact.insert({key, palette.size()}).first;
                palette.push_back(rgb);
            }
            index[i] = short(found->second);
            markActive(active, wordsPerBand, band, found->second);
        }
    }

    if (!exactPalette)
    {
        std::fill(index.begin(), index.end(), short(-1));
        std::fill(active.begin(), active.end(), uint64_t(0));
        palette.clear();
        int levels = cubeLevels(maxColors);
        std::vector<short> quantized(levels*levels*levels, -1);
        for (int y = 0; y < height; ++y)
        {
            int band = y/6;
            int row = y*width;
            for (int x = 0; x < width; ++x)
            {
                int i = row + x;
                if (!opaque(pixels[i]))
                    continue;
                int key = dither == graphicDitherBayer
                    ? ditherKey(unpack(pixels[i]), levels, x, y)
                    : quantizeKey(unpack(pixels[i]), levels);
                int color = quantized[key];
                if (color < 0)
                {
                    color = int(palette.size());
                    quantized[key] = short(color);
                    palette.push_back(dequantize(key, levels));
                }
                index[i] = short(color);
                markActive(active, wordsPerBand, band, color);
            }
        }
    }

    if (palette.empty())
        return std::string();

    std::string out;
    out.reserve(std::max(256, width*bandCount*int(palette.size())/8) +
                int(palette.size())*24 + 64);
    out.append("\x1BPq", 3);
    out.append("\"1;1;", 5);
    appendNumber(out, width);
    out.push_back(';');
    appendNumber(out, height);

    for (size_t i = 0; i < palette.size(); ++i)
        appendPaletteColor(out, i, palette[i]);

    for (int bandY = 0; bandY < height; bandY += 6)
    {
        int bandIndex = bandY/6;
        if (bandY != 0)
            out.push_back('-');
        int bandHeight = std::min(6, height - bandY);
        bool wrotePlane = false;
        for (size_t color = 0; color < palette.size(); ++color)
        {
            if (!isActive(active, wordsPerBand, bandIndex, int(color)))
                continue;

            if (wrotePlane)
                out.push_back('$');
            wrotePlane = true;
            out.push_back('#');
            appendNumber(out, int(color));

            int runChar = -1;
            int runLen = 0;
            for (int x = 0; x < width; ++x)
            {
                int bits = 0;
                for (int bit = 0; bit < bandHeight; ++bit)
                    if (index[(bandY + bit)*width + x] == int(color))
                        bits |= 1 << bit;
                int ch = '?' + bits;
                if (ch == runChar)
                    ++runLen;
                else
                {
                    if (runLen > 0)
                        appendRun(out, runChar, runLen);
                    runChar = ch;
                    runLen = 1;
                }
            }
            // Trailing empty sixels do not paint pixels and can be omitted.
            if (runLen > 0 && runChar != '?')
                appendRun(out, runChar, runLen);
        }
    }

    out.append("\x1B\\", 2);
    return out;
}

} // namespace tvision
