/*
 * Turbo Vision headless display, frame format and automation protocol.
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TScreenCell
#include <tvision/tv.h>
#include <tvision/headless.h>

#include <internal/headless.h>
#include <internal/graphics.h>
#include <internal/sixel.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

#ifdef _TV_UNIX
#include <sys/select.h>
#include <unistd.h>
#endif

namespace tvision
{
namespace
{

constexpr uint32_t messageMagic = 0x31485654u; // "TVH1", little-endian.
constexpr uint16_t protocolVersion = 1;

enum MessageType : uint16_t
{
    msgKey = 1,
    msgMouse = 2,
    msgResize = 3,
    msgClipboard = 4,
    msgCapture = 5,
    msgFrame = 101,
    msgCaptureResult = 102,
    msgError = 103
};

void setError(std::string *error, const std::string &message) noexcept
{
    if (error)
        *error = message;
}

void put16(std::vector<uint8_t> &out, uint16_t value)
{
    out.push_back(uint8_t(value));
    out.push_back(uint8_t(value >> 8));
}

void put32(std::vector<uint8_t> &out, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(value >> (i*8)));
}

void put64(std::vector<uint8_t> &out, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        out.push_back(uint8_t(value >> (i*8)));
}

bool get16(const std::vector<uint8_t> &in, size_t &at, uint16_t &value)
{
    if (at + 2 > in.size())
        return false;
    value = uint16_t(in[at]) | (uint16_t(in[at + 1]) << 8);
    at += 2;
    return true;
}

bool get32(const std::vector<uint8_t> &in, size_t &at, uint32_t &value)
{
    if (at + 4 > in.size())
        return false;
    value = 0;
    for (int i = 0; i < 4; ++i)
        value |= uint32_t(in[at + i]) << (i*8);
    at += 4;
    return true;
}

bool get64(const std::vector<uint8_t> &in, size_t &at, uint64_t &value)
{
    if (at + 8 > in.size())
        return false;
    value = 0;
    for (int i = 0; i < 8; ++i)
        value |= uint64_t(in[at + i]) << (i*8);
    at += 8;
    return true;
}

#ifdef _TV_UNIX

bool writeAll(int fd, const void *data, size_t size) noexcept
{
    const char *p = static_cast<const char *>(data);
    while (size > 0)
    {
        ssize_t n = ::write(fd, p, size);
        if (n > 0)
        {
            p += n;
            size -= size_t(n);
        }
        else if (n < 0 && errno == EINTR)
            continue;
        else
            return false;
    }
    return true;
}

bool readAll(int fd, void *data, size_t size) noexcept
{
    char *p = static_cast<char *>(data);
    while (size > 0)
    {
        ssize_t n = ::read(fd, p, size);
        if (n > 0)
        {
            p += n;
            size -= size_t(n);
        }
        else if (n < 0 && errno == EINTR)
            continue;
        else
            return false;
    }
    return true;
}

bool waitReadable(int fd, int timeoutMs) noexcept
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    timeval timeout;
    timeval *ptimeout = nullptr;
    if (timeoutMs >= 0)
    {
        timeout.tv_sec = timeoutMs/1000;
        timeout.tv_usec = (timeoutMs%1000)*1000;
        ptimeout = &timeout;
    }
    int result;
    do
        result = select(fd + 1, &fds, nullptr, nullptr, ptimeout);
    while (result < 0 && errno == EINTR);
    return result > 0;
}

#else

bool writeAll(int, const void *, size_t) noexcept { return false; }
bool readAll(int, void *, size_t) noexcept { return false; }
bool waitReadable(int, int) noexcept { return false; }

#endif

bool sendMessage(int fd, uint16_t type, const std::vector<uint8_t> &payload,
                 std::string *error = nullptr) noexcept
{
    if (payload.size() > 64u*1024u*1024u)
    {
        setError(error, "headless protocol payload is too large");
        return false;
    }
    std::vector<uint8_t> header;
    header.reserve(12);
    put32(header, messageMagic);
    put16(header, protocolVersion);
    put16(header, type);
    put32(header, uint32_t(payload.size()));
    if (!writeAll(fd, header.data(), header.size()) ||
        (!payload.empty() && !writeAll(fd, payload.data(), payload.size())))
    {
        setError(error, "headless protocol write failed");
        return false;
    }
    return true;
}

bool receiveMessage(int fd, uint16_t &type, std::vector<uint8_t> &payload,
                    int timeoutMs, std::string *error = nullptr) noexcept
{
    if (!waitReadable(fd, timeoutMs))
    {
        setError(error, "headless protocol receive timed out");
        return false;
    }
    uint8_t raw[12];
    if (!readAll(fd, raw, sizeof(raw)))
    {
        setError(error, "headless protocol channel closed");
        return false;
    }
    std::vector<uint8_t> header(raw, raw + sizeof(raw));
    size_t at = 0;
    uint32_t magic = 0, size = 0;
    uint16_t version = 0;
    if (!get32(header, at, magic) || !get16(header, at, version) ||
        !get16(header, at, type) || !get32(header, at, size) ||
        magic != messageMagic || version != protocolVersion ||
        size > 64u*1024u*1024u)
    {
        setError(error, "invalid headless protocol header");
        return false;
    }
    payload.resize(size);
    if (size && !readAll(fd, payload.data(), payload.size()))
    {
        setError(error, "truncated headless protocol payload");
        return false;
    }
    return true;
}

uint64_t fnvBytes(uint64_t hash, const void *data, size_t size) noexcept
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <class T>
uint64_t fnvValue(uint64_t hash, const T &value) noexcept
{
    return fnvBytes(hash, &value, sizeof(value));
}

uint64_t graphicsHash(const std::vector<uint32_t> &pixels) noexcept
{
    return fnvBytes(1469598103934665603ULL, pixels.data(),
                    pixels.size()*sizeof(uint32_t));
}

uint64_t visualHash(const THeadlessFrame &frame) noexcept
{
    uint64_t hash = 1469598103934665603ULL;
    hash = fnvValue(hash, frame.screenSize.x);
    hash = fnvValue(hash, frame.screenSize.y);
    hash = fnvValue(hash, frame.caretPosition.x);
    hash = fnvValue(hash, frame.caretPosition.y);
    hash = fnvValue(hash, frame.caretSize);
    for (const THeadlessCell &cell : frame.cells)
    {
        uint32_t fg = getFore(cell.attr).bitCast();
        uint32_t bg = getBack(cell.attr).bitCast();
        ushort style = getStyle(cell.attr);
        hash = fnvValue(hash, fg);
        hash = fnvValue(hash, bg);
        hash = fnvValue(hash, style);
        hash = fnvValue(hash, cell.wide);
        hash = fnvBytes(hash, cell.text.data(), cell.text.size());
    }
    return fnvValue(hash, frame.graphicsHash);
}

bool parsePoint(const char *value, TPoint &point) noexcept
{
    if (!value)
        return false;
    char *end = nullptr;
    long x = strtol(value, &end, 10);
    if (end == value || (*end != 'x' && *end != 'X'))
        return false;
    char *end2 = nullptr;
    long y = strtol(end + 1, &end2, 10);
    if (end2 == end + 1 || *end2 != '\0' || x <= 0 || y <= 0 ||
        x > 4096 || y > 4096)
        return false;
    point = {short(x), short(y)};
    return true;
}

class HeadlessSession
{
public:
    int fd;
    bool alive {true};
    THeadlessOptions options;
    THeadlessFrame frame;
    std::string clipboard;

    explicit HeadlessSession(int aFd, THeadlessOptions aOptions) :
        fd(aFd), options(aOptions)
    {
        // A headless display is an ideal SIXEL-capable terminal. Keep the
        // geometry synchronized even when the caller only changed cellSize.
        TGraphicProfile &profile = options.graphicProfile;
        if (profile.cellWidth <= 0)
            profile.cellWidth = options.cellSize.x;
        if (profile.cellHeight <= 0)
            profile.cellHeight = options.cellSize.y;
        if (profile.fillWidth <= 0)
            profile.fillWidth = options.cellSize.x;
        if (profile.fillHeight <= 0)
            profile.fillHeight = options.cellSize.y;
        resizeBuffers();
    }

    ~HeadlessSession()
    {
#ifdef _TV_UNIX
        if (fd >= 0)
            close(fd);
#endif
    }

    void resizeBuffers()
    {
        frame.screenSize = options.screenSize;
        frame.cellSize = options.cellSize;
        size_t cells = size_t(options.screenSize.x)*options.screenSize.y;
        size_t pixels = size_t(options.screenSize.x)*options.cellSize.x*
                        size_t(options.screenSize.y)*options.cellSize.y;
        frame.cells.assign(cells, THeadlessCell());
        frame.graphics.assign(pixels, 0);
        frame.caretPosition = {-1, -1};
        frame.caretSize = 0;
    }

    void clearGraphicCells(int x, int y, int width)
    {
        int px0 = std::max(0, x*options.cellSize.x);
        int py0 = std::max(0, y*options.cellSize.y);
        int px1 = std::min(options.screenSize.x*options.cellSize.x,
                           (x + width)*options.cellSize.x);
        int py1 = std::min(options.screenSize.y*options.cellSize.y,
                           (y + 1)*options.cellSize.y);
        int stride = options.screenSize.x*options.cellSize.x;
        for (int py = py0; py < py1; ++py)
            std::fill(frame.graphics.begin() + size_t(py)*stride + px0,
                      frame.graphics.begin() + size_t(py)*stride + px1, 0u);
    }

    void updateHashes()
    {
        frame.graphicsHash = graphicsHash(frame.graphics);
        frame.visualHash = visualHash(frame);
    }

    void publish()
    {
        ++frame.sequence;
        updateHashes();
        std::string text = frame.text();
        uint64_t graphicPixels = 0;
        for (uint32_t p : frame.graphics)
            graphicPixels += (p >> 24) != 0;

        std::vector<uint8_t> payload;
        payload.reserve(80 + text.size());
        put64(payload, frame.sequence);
        put64(payload, frame.visualHash);
        put64(payload, frame.graphicsHash);
        put64(payload, graphicPixels);
        put32(payload, uint32_t(frame.screenSize.x));
        put32(payload, uint32_t(frame.screenSize.y));
        put32(payload, uint32_t(frame.caretPosition.x));
        put32(payload, uint32_t(frame.caretPosition.y));
        put32(payload, uint32_t(frame.caretSize));
        put32(payload, uint32_t(text.size()));
        payload.insert(payload.end(), text.begin(), text.end());
        if (!sendMessage(fd, msgFrame, payload))
            alive = false;
    }

    void sendCaptureResult(bool success, const std::string &message)
    {
        std::vector<uint8_t> payload;
        payload.push_back(success ? 1 : 0);
        payload.insert(payload.end(), message.begin(), message.end());
        if (!sendMessage(fd, msgCaptureResult, payload))
            alive = false;
    }

    void sendProtocolError(const std::string &message)
    {
        std::vector<uint8_t> payload(message.begin(), message.end());
        if (!sendMessage(fd, msgError, payload))
            alive = false;
    }
};

class HeadlessDisplay final : public DisplayAdapter
{
public:
    explicit HeadlessDisplay(HeadlessSession &aSession) : session(aSession) {}

    TPoint reloadScreenInfo() noexcept override
    {
        session.resizeBuffers();
        return session.options.screenSize;
    }

    int getColorCount() noexcept override { return session.options.colorCount; }
    TPoint getFontSize() noexcept override { return session.options.cellSize; }

    void writeCell(TPoint pos, TStringView text, TColorAttr attr,
                   bool doubleWidth) noexcept override
    {
        if (pos.x < 0 || pos.y < 0 || pos.x >= session.options.screenSize.x ||
            pos.y >= session.options.screenSize.y)
            return;
        size_t index = size_t(pos.y)*session.options.screenSize.x + pos.x;
        if (pos.x > 0 && session.frame.cells[index - 1].wide)
            session.frame.cells[index - 1].wide = false;
        if (session.frame.cells[index].wide &&
            pos.x + 1 < session.options.screenSize.x)
            session.frame.cells[index + 1] = THeadlessCell();
        THeadlessCell &cell = session.frame.cells[index];
        cell.text.assign(text.data(), text.size());
        if (cell.text.empty())
            cell.text = " ";
        cell.attr = attr;
        cell.wide = doubleWidth;
        if (doubleWidth && pos.x + 1 < session.options.screenSize.x)
        {
            THeadlessCell &trail = session.frame.cells[index + 1];
            trail.text.clear();
            trail.attr = attr;
            trail.wide = false;
        }
        session.clearGraphicCells(pos.x, pos.y, doubleWidth ? 2 : 1);
    }

    void setCaretPosition(TPoint pos) noexcept override
        { session.frame.caretPosition = pos; }
    void setCaretSize(int size) noexcept override
        { session.frame.caretSize = size; }

    void clearScreen() noexcept override
    {
        std::fill(session.frame.cells.begin(), session.frame.cells.end(),
                  THeadlessCell());
        std::fill(session.frame.graphics.begin(), session.frame.graphics.end(), 0u);
    }

    Boolean supportsGraphics() noexcept override
        { return session.options.graphicProfile.enabled; }
    TGraphicProfile getGraphicProfile() noexcept override
        { return session.options.graphicProfile; }

    void writeGraphicImage(TPoint pos, const uint32_t *pixels, TPoint size,
                           int maxColors, TGraphicDitherMode dither) noexcept override
    {
        std::vector<uint32_t> quantized =
            quantizeSixelPixels(pixels, size, maxColors, dither);
        int stride = session.options.screenSize.x*session.options.cellSize.x;
        int height = session.options.screenSize.y*session.options.cellSize.y;
        int px0 = pos.x*session.options.cellSize.x;
        int py0 = pos.y*session.options.cellSize.y;
        for (int y = 0; y < size.y; ++y)
        {
            int dy = py0 + y;
            if (dy < 0 || dy >= height)
                continue;
            for (int x = 0; x < size.x; ++x)
            {
                int dx = px0 + x;
                if (dx < 0 || dx >= stride)
                    continue;
                uint32_t pixel = quantized[size_t(y)*size.x + x];
                if ((pixel >> 24) != 0)
                    session.frame.graphics[size_t(dy)*stride + dx] = pixel;
            }
        }
    }

    void flush() noexcept override { session.publish(); }

private:
    HeadlessSession &session;
};

class HeadlessInput final : public InputAdapter
{
public:
    explicit HeadlessInput(HeadlessSession &aSession) :
        InputAdapter(aSession.fd), session(aSession) {}

    bool getEvent(TEvent &event) noexcept override
    {
        uint16_t type = 0;
        std::vector<uint8_t> payload;
        std::string error;
        if (!receiveMessage(session.fd, type, payload, -1, &error))
        {
            session.alive = false;
            event.what = evNothing;
            return true;
        }
        size_t at = 0;
        if (type == msgKey)
        {
            uint16_t keyCode = 0, control = 0;
            if (!get16(payload, at, keyCode) || !get16(payload, at, control) ||
                at + 2 > payload.size())
                return malformed(event, "invalid key command");
            bool paste = payload[at++] != 0;
            uint8_t textLength = payload[at++];
            if (textLength > maxCharSize || at + textLength != payload.size())
                return malformed(event, "invalid key text");
            event = {};
            event.what = evKeyDown;
            event.keyDown.keyCode = keyCode;
            event.keyDown.controlKeyState = control | (paste ? kbPaste : 0);
            event.keyDown.textLength = textLength;
            if (textLength)
                memcpy(event.keyDown.text, payload.data() + at, textLength);
            return true;
        }
        if (type == msgMouse)
        {
            uint16_t what = 0, eventFlags = 0, control = 0;
            uint32_t x = 0, y = 0;
            if (!get16(payload, at, what) || !get32(payload, at, x) ||
                !get32(payload, at, y) || at + 2 > payload.size())
                return malformed(event, "invalid mouse command");
            uchar buttons = payload[at++], wheel = payload[at++];
            if (!get16(payload, at, eventFlags) || !get16(payload, at, control) ||
                at != payload.size())
                return malformed(event, "invalid mouse command");
            event = {};
            event.what = what;
            event.mouse.where = {short(int32_t(x)), short(int32_t(y))};
            event.mouse.buttons = buttons;
            event.mouse.wheel = wheel;
            event.mouse.eventFlags = eventFlags;
            event.mouse.controlKeyState = control;
            return true;
        }
        if (type == msgResize)
        {
            uint32_t x = 0, y = 0;
            if (!get32(payload, at, x) || !get32(payload, at, y) ||
                at != payload.size() || x == 0 || y == 0 || x > 4096 || y > 4096)
                return malformed(event, "invalid resize command");
            session.options.screenSize = {short(x), short(y)};
            event = {};
            event.what = evCommand;
            event.message.command = cmScreenChanged;
            event.message.infoPtr = nullptr;
            return true;
        }
        if (type == msgClipboard)
        {
            session.clipboard.assign(reinterpret_cast<const char *>(payload.data()),
                                     payload.size());
            event.what = evNothing;
            return true;
        }
        if (type == msgCapture)
        {
            std::string path(reinterpret_cast<const char *>(payload.data()),
                             payload.size());
            session.updateHashes();
            std::string writeError;
            bool ok = writeHeadlessFrame(path, session.frame, &writeError);
            session.sendCaptureResult(ok, ok ? path : writeError);
            event.what = evNothing;
            return true;
        }
        return malformed(event, "unknown headless command");
    }

private:
    HeadlessSession &session;

    bool malformed(TEvent &event, const char *message) noexcept
    {
        session.sendProtocolError(message);
        event.what = evNothing;
        return true;
    }
};

class HeadlessConsoleAdapter final : public ConsoleAdapter
{
public:
    HeadlessConsoleAdapter(HeadlessSession &aSession,
                           HeadlessDisplay &aDisplay,
                           HeadlessInput &aInput) :
        ConsoleAdapter(aDisplay, {&aInput}),
        session(aSession), displayRef(aDisplay), inputRef(aInput) {}

    ~HeadlessConsoleAdapter()
    {
        delete &inputRef;
        delete &displayRef;
        delete &session;
    }

    bool isAlive() noexcept override { return session.alive; }

    bool setClipboardText(TStringView text) noexcept override
    {
        session.clipboard.assign(text.data(), text.size());
        return true;
    }

    bool requestClipboardText(void (&accept)(TStringView)) noexcept override
    {
        accept(TStringView(session.clipboard.data(), session.clipboard.size()));
        return true;
    }

private:
    HeadlessSession &session;
    HeadlessDisplay &displayRef;
    HeadlessInput &inputRef;
};

} // namespace

std::string THeadlessFrame::text() const
{
    std::string out;
    for (int y = 0; y < screenSize.y; ++y)
    {
        size_t row = size_t(y)*screenSize.x;
        for (int x = 0; x < screenSize.x; ++x)
        {
            const THeadlessCell &cell = cells[row + x];
            if (cell.text.empty())
            {
                if (x == 0 || !cells[row + x - 1].wide)
                    out.push_back(' ');
            }
            else
                out += cell.text;
        }
        if (y + 1 < screenSize.y)
            out.push_back('\n');
    }
    return out;
}

bool THeadlessFrame::hasGraphics() const noexcept
{
    for (uint32_t pixel : graphics)
        if ((pixel >> 24) != 0)
            return true;
    return false;
}

ConsoleAdapter *createHeadlessConsole() noexcept
{
#ifdef _TV_UNIX
    const char *fdText = getenv("TVISION_HEADLESS_FD");
    if (!fdText || !*fdText)
        return nullptr;
    char *end = nullptr;
    long fd = strtol(fdText, &end, 10);
    if (end == fdText || *end != '\0' || fd < 0 || fd > 65535)
        return nullptr;

    THeadlessOptions options;
    const char *screen = getenv("TVISION_HEADLESS_SIZE");
    const char *cell = getenv("TVISION_HEADLESS_CELL");
    if ((screen && !parsePoint(screen, options.screenSize)) ||
        (cell && !parsePoint(cell, options.cellSize)))
        return nullptr;
    const char *graphics = getenv("TVISION_HEADLESS_GRAPHICS");
    if (graphics && *graphics == '0')
        options.graphicProfile.enabled = False;

    HeadlessSession &session = *new HeadlessSession(int(fd), options);
    HeadlessDisplay &display = *new HeadlessDisplay(session);
    HeadlessInput &input = *new HeadlessInput(session);
    return new HeadlessConsoleAdapter(session, display, input);
#else
    return nullptr;
#endif
}

bool THeadlessController::sendKey(ushort keyCode, ushort controlKeyState,
                                  TStringView text, bool paste,
                                  std::string *error) noexcept
{
    if (text.size() > maxCharSize)
    {
        setError(error, "headless key text exceeds one UTF-8 code point");
        return false;
    }
    std::vector<uint8_t> payload;
    put16(payload, keyCode);
    put16(payload, controlKeyState);
    payload.push_back(paste ? 1 : 0);
    payload.push_back(uint8_t(text.size()));
    payload.insert(payload.end(), text.data(), text.data() + text.size());
    return sendMessage(fd_, msgKey, payload, error);
}

bool THeadlessController::sendMouse(ushort what, TPoint where, uchar buttons,
                                    uchar wheel, ushort eventFlags,
                                    ushort controlKeyState,
                                    std::string *error) noexcept
{
    std::vector<uint8_t> payload;
    put16(payload, what);
    put32(payload, uint32_t(int32_t(where.x)));
    put32(payload, uint32_t(int32_t(where.y)));
    payload.push_back(buttons);
    payload.push_back(wheel);
    put16(payload, eventFlags);
    put16(payload, controlKeyState);
    return sendMessage(fd_, msgMouse, payload, error);
}

bool THeadlessController::resize(TPoint size, std::string *error) noexcept
{
    std::vector<uint8_t> payload;
    put32(payload, uint32_t(size.x));
    put32(payload, uint32_t(size.y));
    return sendMessage(fd_, msgResize, payload, error);
}

bool THeadlessController::setClipboard(TStringView text,
                                       std::string *error) noexcept
{
    std::vector<uint8_t> payload(text.data(), text.data() + text.size());
    return sendMessage(fd_, msgClipboard, payload, error);
}

bool THeadlessController::capture(const std::string &path,
                                  std::string *error) noexcept
{
    std::vector<uint8_t> payload(path.begin(), path.end());
    return sendMessage(fd_, msgCapture, payload, error);
}

bool THeadlessController::receive(THeadlessNotification &notification,
                                  int timeoutMs, std::string *error) noexcept
{
    uint16_t type = 0;
    std::vector<uint8_t> payload;
    if (!receiveMessage(fd_, type, payload, timeoutMs, error))
        return false;
    notification = {};
    if (type == msgFrame)
    {
        size_t at = 0;
        uint64_t sequence = 0, visual = 0, graphics = 0, graphicPixels = 0;
        uint32_t cols = 0, rows = 0, caretX = 0, caretY = 0, caretSize = 0,
                 textSize = 0;
        if (!get64(payload, at, sequence) || !get64(payload, at, visual) ||
            !get64(payload, at, graphics) || !get64(payload, at, graphicPixels) ||
            !get32(payload, at, cols) || !get32(payload, at, rows) ||
            !get32(payload, at, caretX) || !get32(payload, at, caretY) ||
            !get32(payload, at, caretSize) || !get32(payload, at, textSize) ||
            at + textSize != payload.size())
        {
            setError(error, "invalid headless frame notification");
            return false;
        }
        notification.type = THeadlessNotificationType::Frame;
        notification.state.sequence = sequence;
        notification.state.visualHash = visual;
        notification.state.graphicsHash = graphics;
        notification.state.graphicPixels = graphicPixels;
        notification.state.screenSize = {short(cols), short(rows)};
        notification.state.caretPosition =
            {short(int32_t(caretX)), short(int32_t(caretY))};
        notification.state.caretSize = int32_t(caretSize);
        notification.state.text.assign(
            reinterpret_cast<const char *>(payload.data() + at), textSize);
        return true;
    }
    if (type == msgCaptureResult)
    {
        if (payload.empty())
        {
            setError(error, "invalid headless capture result");
            return false;
        }
        notification.type = THeadlessNotificationType::Capture;
        notification.success = payload[0] != 0;
        notification.message.assign(
            reinterpret_cast<const char *>(payload.data() + 1), payload.size() - 1);
        return true;
    }
    if (type == msgError)
    {
        notification.type = THeadlessNotificationType::Error;
        notification.message.assign(reinterpret_cast<const char *>(payload.data()),
                                    payload.size());
        return true;
    }
    setError(error, "unknown headless notification");
    return false;
}

} // namespace tvision
