/*
 * Turbo Vision headless frame file format.
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */
#include <tvision/headless.h>

#include <cstdio>
#include <cstring>
#include <limits>

namespace tvision
{
namespace
{

void fail(std::string *error, const std::string &message) noexcept
{
    if (error)
        *error = message;
}

bool writeBytes(FILE *file, const void *data, size_t size) noexcept
{
    return size == 0 || fwrite(data, 1, size, file) == size;
}

bool write8(FILE *file, uint8_t value) noexcept
{
    return writeBytes(file, &value, 1);
}

bool write16(FILE *file, uint16_t value) noexcept
{
    uint8_t bytes[2] = {uint8_t(value), uint8_t(value >> 8)};
    return writeBytes(file, bytes, sizeof(bytes));
}

bool write32(FILE *file, uint32_t value) noexcept
{
    uint8_t bytes[4];
    for (int i = 0; i < 4; ++i)
        bytes[i] = uint8_t(value >> (i*8));
    return writeBytes(file, bytes, sizeof(bytes));
}

bool write64(FILE *file, uint64_t value) noexcept
{
    uint8_t bytes[8];
    for (int i = 0; i < 8; ++i)
        bytes[i] = uint8_t(value >> (i*8));
    return writeBytes(file, bytes, sizeof(bytes));
}

bool readBytes(FILE *file, void *data, size_t size) noexcept
{
    return size == 0 || fread(data, 1, size, file) == size;
}

bool read8(FILE *file, uint8_t &value) noexcept
{
    return readBytes(file, &value, 1);
}

bool read16(FILE *file, uint16_t &value) noexcept
{
    uint8_t bytes[2];
    if (!readBytes(file, bytes, sizeof(bytes)))
        return false;
    value = uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
    return true;
}

bool read32(FILE *file, uint32_t &value) noexcept
{
    uint8_t bytes[4];
    if (!readBytes(file, bytes, sizeof(bytes)))
        return false;
    value = 0;
    for (int i = 0; i < 4; ++i)
        value |= uint32_t(bytes[i]) << (i*8);
    return true;
}

bool read64(FILE *file, uint64_t &value) noexcept
{
    uint8_t bytes[8];
    if (!readBytes(file, bytes, sizeof(bytes)))
        return false;
    value = 0;
    for (int i = 0; i < 8; ++i)
        value |= uint64_t(bytes[i]) << (i*8);
    return true;
}

} // namespace

bool writeHeadlessFrame(const std::string &path, const THeadlessFrame &frame,
                        std::string *error) noexcept
{
    if (frame.screenSize.x <= 0 || frame.screenSize.y <= 0 ||
        frame.cellSize.x <= 0 || frame.cellSize.y <= 0 ||
        frame.cells.size() != size_t(frame.screenSize.x)*frame.screenSize.y ||
        frame.graphics.size() !=
            size_t(frame.screenSize.x)*frame.cellSize.x*
            size_t(frame.screenSize.y)*frame.cellSize.y)
    {
        fail(error, "invalid headless frame dimensions");
        return false;
    }

    const std::string temporary = path + ".tmp";
    FILE *file = fopen(temporary.c_str(), "wb");
    if (!file)
    {
        fail(error, "cannot create headless frame '" + temporary + "'");
        return false;
    }

    bool ok = writeBytes(file, "TVF1", 4) &&
              write16(file, THeadlessFrame::formatVersion) && write16(file, 0) &&
              write32(file, uint32_t(frame.screenSize.x)) &&
              write32(file, uint32_t(frame.screenSize.y)) &&
              write32(file, uint32_t(frame.cellSize.x)) &&
              write32(file, uint32_t(frame.cellSize.y)) &&
              write32(file, uint32_t(int32_t(frame.caretPosition.x))) &&
              write32(file, uint32_t(int32_t(frame.caretPosition.y))) &&
              write32(file, uint32_t(int32_t(frame.caretSize))) &&
              write64(file, frame.sequence) && write64(file, frame.visualHash) &&
              write64(file, frame.graphicsHash) &&
              write32(file, uint32_t(frame.cells.size())) &&
              write64(file, uint64_t(frame.graphics.size()));

    for (const THeadlessCell &cell : frame.cells)
    {
        if (cell.text.size() > 64)
        {
            ok = false;
            break;
        }
        ok = ok && write32(file, getFore(cell.attr).bitCast()) &&
             write32(file, getBack(cell.attr).bitCast()) &&
             write16(file, getStyle(cell.attr)) &&
             write8(file, cell.wide ? 1 : 0) && write8(file, 0) &&
             write32(file, uint32_t(cell.text.size())) &&
             writeBytes(file, cell.text.data(), cell.text.size());
    }
    for (uint32_t pixel : frame.graphics)
        ok = ok && write32(file, pixel);

    if (fflush(file) != 0)
        ok = false;
    if (fclose(file) != 0)
        ok = false;
    if (!ok)
    {
        remove(temporary.c_str());
        fail(error, "failed writing headless frame '" + temporary + "'");
        return false;
    }
    if (rename(temporary.c_str(), path.c_str()) != 0)
    {
        remove(temporary.c_str());
        fail(error, "cannot publish headless frame '" + path + "'");
        return false;
    }
    return true;
}

bool readHeadlessFrame(const std::string &path, THeadlessFrame &frame,
                       std::string *error) noexcept
{
    FILE *file = fopen(path.c_str(), "rb");
    if (!file)
    {
        fail(error, "cannot open headless frame '" + path + "'");
        return false;
    }

    char magic[4];
    uint16_t version = 0, reserved = 0;
    uint32_t cols = 0, rows = 0, cellWidth = 0, cellHeight = 0,
             caretX = 0, caretY = 0, caretSize = 0, cellCount = 0;
    uint64_t sequence = 0, visual = 0, graphicsHash = 0, pixelCount = 0;
    bool ok = readBytes(file, magic, sizeof(magic)) &&
              read16(file, version) && read16(file, reserved) &&
              read32(file, cols) && read32(file, rows) &&
              read32(file, cellWidth) && read32(file, cellHeight) &&
              read32(file, caretX) && read32(file, caretY) &&
              read32(file, caretSize) && read64(file, sequence) &&
              read64(file, visual) && read64(file, graphicsHash) &&
              read32(file, cellCount) && read64(file, pixelCount);
    const uint64_t expectedCells = uint64_t(cols)*rows;
    const uint64_t expectedPixels = expectedCells*cellWidth*cellHeight;
    if (!ok || memcmp(magic, "TVF1", 4) != 0 ||
        version != THeadlessFrame::formatVersion || reserved != 0 ||
        cols == 0 || rows == 0 || cellWidth == 0 || cellHeight == 0 ||
        cols > 4096 || rows > 4096 || cellWidth > 4096 || cellHeight > 4096 ||
        cellCount != expectedCells || pixelCount != expectedPixels ||
        expectedCells > 4u*1024u*1024u || expectedPixels > 100u*1024u*1024u)
    {
        fclose(file);
        fail(error, "invalid headless frame header in '" + path + "'");
        return false;
    }

    THeadlessFrame loaded;
    loaded.screenSize = {short(cols), short(rows)};
    loaded.cellSize = {short(cellWidth), short(cellHeight)};
    loaded.caretPosition = {short(int32_t(caretX)), short(int32_t(caretY))};
    loaded.caretSize = int32_t(caretSize);
    loaded.sequence = sequence;
    loaded.visualHash = visual;
    loaded.graphicsHash = graphicsHash;
    loaded.cells.resize(cellCount);
    for (THeadlessCell &cell : loaded.cells)
    {
        uint32_t fg = 0, bg = 0, textSize = 0;
        uint16_t style = 0;
        uint8_t wide = 0, cellReserved = 0;
        if (!read32(file, fg) || !read32(file, bg) ||
            !read16(file, style) || !read8(file, wide) ||
            !read8(file, cellReserved) || !read32(file, textSize) ||
            wide > 1 || cellReserved != 0 || textSize > 64)
        {
            ok = false;
            break;
        }
        cell.text.resize(textSize);
        if (textSize && !readBytes(file, &cell.text[0], textSize))
        {
            ok = false;
            break;
        }
        TColorDesired foreground, background;
        foreground.bitCast(fg);
        background.bitCast(bg);
        cell.attr = TColorAttr(foreground, background, style);
        cell.wide = wide != 0;
    }
    loaded.graphics.resize(size_t(pixelCount));
    for (uint32_t &pixel : loaded.graphics)
        if (!read32(file, pixel))
        {
            ok = false;
            break;
        }
    int trailing = fgetc(file);
    if (trailing != EOF || ferror(file))
        ok = false;
    if (fclose(file) != 0)
        ok = false;
    if (!ok)
    {
        fail(error, "truncated or malformed headless frame '" + path + "'");
        return false;
    }
    frame = std::move(loaded);
    return true;
}

} // namespace tvision
