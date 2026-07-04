/*
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#define Uses_MsgBox
#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TGraphicRuntime
#define Uses_TGraphicView
#define Uses_TKeys
#define Uses_TMenuBar
#define Uses_TMenuItem
#define Uses_TRect
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TStatusLine
#define Uses_TSubMenu
#define Uses_TWindow
#include <tvision/tv.h>

#include <internal/graphics.h>

#include <stdio.h>
#include <string>

const ushort cmCellWMinus = 2101;
const ushort cmCellWPlus = 2102;
const ushort cmCellHMinus = 2103;
const ushort cmCellHPlus = 2104;
const ushort cmFillHMinus = 2105;
const ushort cmFillHPlus = 2106;
const ushort cmColors64 = 2107;
const ushort cmColors256 = 2108;
const ushort cmSaveProfile = 2109;

static TGraphicProfile profile;

class TCalibrationGraphicView : public TGraphicView
{
public:
    TCalibrationGraphicView(const TRect &bounds) :
        TGraphicView(bounds, fillGraphic)
    {
    }

    virtual void paintGraphic(TGraphicCanvas &canvas)
    {
        canvas.clear(TGraphicColor(20, 22, 26));
        for (int y = 0; y < canvas.size.y; y += max(1, canvas.fillSize.y))
        {
            TGraphicColor c = ((y / max(1, canvas.fillSize.y)) & 1)
                ? TGraphicColor(35, 42, 55)
                : TGraphicColor(55, 45, 35);
            for (int yy = y; yy < min(canvas.size.y, y + canvas.fillSize.y); ++yy)
                for (int x = 0; x < canvas.size.x; ++x)
                    canvas.setPixel(x, yy, c);
        }
        canvas.rect(0, 0, canvas.size.x, canvas.size.y, TGraphicColor(245, 245, 230));
        canvas.line(0, canvas.size.y/2, canvas.size.x - 1, canvas.size.y/2, TGraphicColor(255, 80, 80));
        canvas.line(canvas.size.x/2, 0, canvas.size.x/2, canvas.size.y - 1, TGraphicColor(80, 190, 255));
        canvas.circle(canvas.size.x/2, canvas.size.y/2,
                      max(4, min(canvas.size.x, canvas.size.y)/3),
                      TGraphicColor(100, 240, 160));
    }
};

class TInfoView : public TView
{
public:
    TInfoView(const TRect &bounds) : TView(bounds) {}

    virtual void draw()
    {
        TDrawBuffer b;
        char line[96];
        TAttrPair c = getColor(1);
        auto put = [&] (int y, const char *text) {
            b.moveChar(0, ' ', c, size.x);
            b.moveStr(0, text, c);
            writeLine(0, y, size.x, 1, b);
        };
        snprintf(line, sizeof(line), "Profile: %s", tvision::SixelConfig::profileKey().c_str());
        put(0, line);
        snprintf(line, sizeof(line), "Config:  %s", tvision::SixelConfig::configPath().c_str());
        put(1, line);
        snprintf(line, sizeof(line), "Cell %dx%d  Fill %dx%d  Colors %d",
                 profile.cellWidth, profile.cellHeight,
                 profile.fillWidth, profile.fillHeight,
                 profile.maxColors);
        put(2, line);
        put(3, "Use Graphics menu to adjust. Save writes this terminal profile.");
        for (int y = 4; y < size.y; ++y)
            put(y, "");
    }
};

class TCalibrationWindow : public TWindow
{
public:
    TInfoView *info;
    TGraphicView *target;

    TCalibrationWindow() :
        TWindowInit(TWindow::initFrame),
        TWindow(TRect(2, 2, 76, 23), "Sixel Calibration", wnNoNumber)
    {
        options |= ofTileable;
        insert(info = new TInfoView(TRect(1, 1, 72, 5)));
        insert(target = new TCalibrationGraphicView(TRect(1, 6, 72, 20)));
    }

    void refresh()
    {
        TGraphicRuntime::setTemporaryProfile(profile);
        info->drawView();
        target->drawView();
    }
};

class TSixelCfg : public TApplication
{
public:
    TCalibrationWindow *win;

    TSixelCfg() :
        TProgInit(initStatusLine, initMenuBar, initDeskTop),
        TApplication(),
        win(0)
    {
        profile = TGraphicRuntime::getProfile();
        if (!profile.enabled)
            profile = tvision::SixelConfig::detectedProfile();
        TGraphicRuntime::setTemporaryProfile(profile);
        deskTop->insert(win = new TCalibrationWindow);
    }

    static TStatusLine *initStatusLine(TRect r)
    {
        r.a.y = r.b.y - 1;
        std::string configItem = std::string("  ") + tvision::SixelConfig::configPath();
        return new TStatusLine(r,
            *new TStatusDef(0, 0xFFFF) +
            *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit) +
            *new TStatusItem("~F10~ Menu", kbF10, cmMenu) +
            *new TStatusItem(configItem.c_str(), kbNoKey, 0)
        );
    }

    static TMenuBar *initMenuBar(TRect r)
    {
        r.b.y = r.a.y + 1;
        return new TMenuBar(r,
            *new TSubMenu("~G~raphics", kbAltG) +
            *new TMenuItem("Cell width -", cmCellWMinus, kbNoKey) +
            *new TMenuItem("Cell width +", cmCellWPlus, kbNoKey) +
            *new TMenuItem("Cell height -", cmCellHMinus, kbNoKey) +
            *new TMenuItem("Cell height +", cmCellHPlus, kbNoKey) +
            *new TMenuItem("Fill height -", cmFillHMinus, kbNoKey) +
            *new TMenuItem("Fill height +", cmFillHPlus, kbNoKey) +
            newLine() +
            *new TMenuItem("64 colors", cmColors64, kbNoKey) +
            *new TMenuItem("256 colors", cmColors256, kbNoKey) +
            newLine() +
            *new TMenuItem("~S~ave profile", cmSaveProfile, kbNoKey) +
            *new TMenuItem("E~x~it", cmQuit, kbAltX)
        );
    }

    virtual void handleEvent(TEvent &event)
    {
        TApplication::handleEvent(event);
        if (event.what != evCommand)
            return;

        bool changed = true;
        switch (event.message.command)
        {
        case cmCellWMinus: profile.cellWidth = max(short(1), short(profile.cellWidth - 1)); profile.fillWidth = profile.cellWidth; break;
        case cmCellWPlus: ++profile.cellWidth; profile.fillWidth = profile.cellWidth; break;
        case cmCellHMinus: profile.cellHeight = max(short(1), short(profile.cellHeight - 1)); break;
        case cmCellHPlus: ++profile.cellHeight; break;
        case cmFillHMinus: profile.fillHeight = max(short(1), short(profile.fillHeight - 1)); break;
        case cmFillHPlus: ++profile.fillHeight; break;
        case cmColors64: profile.maxColors = 64; break;
        case cmColors256: profile.maxColors = 256; break;
        case cmSaveProfile:
            {
                std::string error;
                if (tvision::SixelConfig::writeProfile(tvision::SixelConfig::profileKey(), profile, &error))
                    messageBox("Saved sixel profile.", mfInformation | mfOKButton);
                else
                    messageBox(error.c_str(), mfError | mfOKButton);
            }
            changed = false;
            break;
        default:
            return;
        }
        if (changed && win)
            win->refresh();
        clearEvent(event);
    }
};

int main()
{
    TSixelCfg app;
    app.run();
    return 0;
}
