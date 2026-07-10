/*
 * Turbo Vision headless display and automation protocol.
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */
#ifndef TVISION_HEADLESS_H
#define TVISION_HEADLESS_H

#define Uses_TColorAttr
#define Uses_TGraphicRuntime
#define Uses_TPoint
#define Uses_TRect
#include <tvision/tv.h>

#include <cstdint>
#include <string>
#include <vector>

namespace tvision
{

struct THeadlessOptions
{
    TPoint screenSize {80, 25};
    TPoint cellSize {10, 20};
    int colorCount {256*256*256};
    TGraphicProfile graphicProfile;

    THeadlessOptions() noexcept
    {
        graphicProfile.enabled = True;
        graphicProfile.maxColors = 256;
        graphicProfile.dither = graphicDitherBayer;
    }
};

struct THeadlessCell
{
    std::string text {" "};
    TColorAttr attr {};
    bool wide {false};
};

struct THeadlessFrame
{
    static constexpr uint16_t formatVersion = 1;

    uint64_t sequence {0};
    uint64_t visualHash {0};
    uint64_t graphicsHash {0};
    TPoint screenSize {};
    TPoint cellSize {};
    TPoint caretPosition {-1, -1};
    int caretSize {0};
    std::vector<THeadlessCell> cells;
    // Transparent 0 or opaque 0xAARRGGBB, screen-sized in pixels.
    std::vector<uint32_t> graphics;

    std::string text() const;
    bool hasGraphics() const noexcept;
};

bool writeHeadlessFrame(const std::string &path, const THeadlessFrame &frame,
                        std::string *error = nullptr) noexcept;
bool readHeadlessFrame(const std::string &path, THeadlessFrame &frame,
                       std::string *error = nullptr) noexcept;

enum class THeadlessNotificationType : uint16_t
{
    None = 0,
    Frame = 1,
    Capture = 2,
    Error = 3
};

struct THeadlessState
{
    uint64_t sequence {0};
    uint64_t visualHash {0};
    uint64_t graphicsHash {0};
    uint64_t graphicPixels {0};
    TPoint screenSize {};
    TPoint caretPosition {-1, -1};
    int caretSize {0};
    std::string text;
};

struct THeadlessNotification
{
    THeadlessNotificationType type {THeadlessNotificationType::None};
    THeadlessState state;
    bool success {false};
    std::string message;
};

// The controller side of a duplex headless channel. The descriptor is
// borrowed and must be a connected SOCK_STREAM endpoint. The application gets
// the peer descriptor through TVISION_HEADLESS_FD.
class THeadlessController
{
public:
    explicit THeadlessController(int fd) noexcept : fd_(fd) {}

    bool sendKey(ushort keyCode, ushort controlKeyState = 0,
                 TStringView text = {}, bool paste = false,
                 std::string *error = nullptr) noexcept;
    bool sendMouse(ushort what, TPoint where, uchar buttons = 0,
                   uchar wheel = 0, ushort eventFlags = 0,
                   ushort controlKeyState = 0,
                   std::string *error = nullptr) noexcept;
    bool resize(TPoint size, std::string *error = nullptr) noexcept;
    bool setClipboard(TStringView text, std::string *error = nullptr) noexcept;
    bool capture(const std::string &path, std::string *error = nullptr) noexcept;

    // timeoutMs < 0 waits indefinitely; 0 polls.
    bool receive(THeadlessNotification &notification, int timeoutMs,
                 std::string *error = nullptr) noexcept;

private:
    int fd_ {-1};
};

} // namespace tvision

#endif // TVISION_HEADLESS_H
