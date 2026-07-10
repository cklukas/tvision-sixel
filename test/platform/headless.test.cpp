/*
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */
#define Uses_TEvent
#define Uses_TKeys
#include <test.h>
#include <tvision/tv.h>
#include <tvision/headless.h>

#include <internal/sixel.h>

#include <cstdio>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

TEST(HeadlessFrame, RoundTripsCellsGraphicsAndCaret)
{
    tvision::THeadlessFrame source;
    source.sequence = 42;
    source.visualHash = 123;
    source.graphicsHash = 456;
    source.screenSize = {2, 1};
    source.cellSize = {10, 20};
    source.caretPosition = {1, 0};
    source.caretSize = 25;
    source.cells.resize(2);
    source.cells[0].text = "A";
    source.cells[0].attr = TColorAttr(0x1F);
    source.cells[1].text = "界";
    source.cells[1].attr = TColorAttr(TColorDesired(0x112233),
                                     TColorDesired(0x445566), slBold);
    source.graphics.assign(400, 0);
    source.graphics[17] = 0xFFAABBCC;

    std::string path =
#ifdef _WIN32
        "headless-frame-test.tvf";
#else
        std::string("/tmp/headless-frame-test-") + std::to_string(getpid()) + ".tvf";
#endif
    std::string error;
    ASSERT_TRUE(tvision::writeHeadlessFrame(path, source, &error)) << error;
    tvision::THeadlessFrame decoded;
    ASSERT_TRUE(tvision::readHeadlessFrame(path, decoded, &error)) << error;
    EXPECT_EQ(source.sequence, decoded.sequence);
    EXPECT_EQ(source.visualHash, decoded.visualHash);
    EXPECT_EQ(source.graphicsHash, decoded.graphicsHash);
    EXPECT_EQ(source.screenSize, decoded.screenSize);
    EXPECT_EQ(source.cellSize, decoded.cellSize);
    EXPECT_EQ(source.caretPosition, decoded.caretPosition);
    EXPECT_EQ(source.caretSize, decoded.caretSize);
    ASSERT_EQ(source.cells.size(), decoded.cells.size());
    EXPECT_EQ(source.cells[1].text, decoded.cells[1].text);
    EXPECT_EQ(source.cells[1].attr, decoded.cells[1].attr);
    EXPECT_EQ(source.graphics, decoded.graphics);
    EXPECT_EQ("A界", decoded.text());
    EXPECT_TRUE(decoded.hasGraphics());
    remove(path.c_str());
}

TEST(HeadlessQuantizer, IsTheSamePostSixelPaletteUsedByCaptures)
{
    uint32_t input[] = {
        0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF,
        0x00000000, 0xFF808080, 0xFF404040, 0xFFC0C0C0,
    };
    std::vector<uint32_t> nearest = tvision::quantizeSixelPixels(
        input, {4, 2}, 4, graphicDitherNearest);
    std::vector<uint32_t> bayer = tvision::quantizeSixelPixels(
        input, {4, 2}, 4, graphicDitherBayer);
    ASSERT_EQ(8u, nearest.size());
    ASSERT_EQ(8u, bayer.size());
    EXPECT_EQ(0u, nearest[4]);
    EXPECT_EQ(0u, bayer[4]);
    for (size_t i = 0; i < nearest.size(); ++i)
        if (i != 4)
            EXPECT_EQ(0xFFu, nearest[i] >> 24);
}

#ifndef _WIN32
static uint16_t little16(const unsigned char *p)
{
    return uint16_t(p[0]) | uint16_t(p[1]) << 8;
}

static bool readAll(int fd, void *data, size_t size)
{
    char *p = static_cast<char *>(data);
    while (size)
    {
        ssize_t count = read(fd, p, size);
        if (count <= 0)
            return false;
        p += count;
        size -= size_t(count);
    }
    return true;
}

TEST(HeadlessProtocol, EncodesPasteAndAtomicMouseEvents)
{
    int channels[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, channels));
    tvision::THeadlessController controller(channels[0]);
    std::string error;
    ASSERT_TRUE(controller.sendKey(kbNoKey, kbLeftCtrl, "x", true, &error));
    ASSERT_TRUE(controller.sendMouse(evMouseDown, {7, 9}, mbLeftButton,
                                     0, meDoubleClick, kbShift, &error));
    ASSERT_TRUE(controller.sendMouse(evMouseUp, {7, 9}, 0, 0, 0, kbShift, &error));

    unsigned char header[12];
    ASSERT_TRUE(readAll(channels[1], header, sizeof(header)));
    EXPECT_EQ(1, little16(header + 6));
    std::vector<unsigned char> key(7);
    ASSERT_TRUE(readAll(channels[1], key.data(), key.size()));
    EXPECT_EQ(kbLeftCtrl, little16(key.data() + 2));
    EXPECT_EQ(1, key[4]);
    EXPECT_EQ('x', key[6]);

    for (int expected : {2, 2})
    {
        ASSERT_TRUE(readAll(channels[1], header, sizeof(header)));
        EXPECT_EQ(expected, little16(header + 6));
        unsigned payloadSize = unsigned(header[8]) | unsigned(header[9]) << 8 |
                               unsigned(header[10]) << 16 | unsigned(header[11]) << 24;
        std::vector<unsigned char> payload(payloadSize);
        ASSERT_TRUE(readAll(channels[1], payload.data(), payload.size()));
    }
    close(channels[0]);
    close(channels[1]);
}
#endif
