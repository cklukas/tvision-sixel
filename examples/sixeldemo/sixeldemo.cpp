/*
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#define Uses_TApplication
#define Uses_TButton
#define Uses_TDialog
#define Uses_TDeskTop
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TFileDialog
#define Uses_TGroup
#define Uses_TGraphicRuntime
#define Uses_TGraphicView
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TMenuBar
#define Uses_TMenuItem
#define Uses_MsgBox
#define Uses_TRect
#define Uses_TStatusDef
#define Uses_TStatusItem
#define Uses_TStatusLine
#define Uses_TStaticText
#define Uses_TSubMenu
#define Uses_TView
#define Uses_TWindow
#include <tvision/tv.h>

#include "formulaplot.h"
#include "moonphase.h"

#include <math.h>
#include <stdint.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#endif

#include <string>
#include <vector>

const ushort cmFixedImage = 2001;
const ushort cmFillImage = 2002;
const ushort cmRgbRange = 2004;
const ushort cmDitherRange = 2005;
const ushort cmOpenImage = 2006;
const ushort cmMandelbrotImage = 2007;
const ushort cmUpdateMandelbrot = 2008;
const ushort cmFormulaPlot = 2009;
const ushort cmClockLosAngeles = 2010;
const ushort cmClockChicago = 2011;
const ushort cmClockNewYork = 2012;
const ushort cmClockLondon = 2013;
const ushort cmClockBerlin = 2014;
const ushort cmClockMoscow = 2015;
const ushort cmClockDubai = 2016;
const ushort cmClockMumbai = 2017;
const ushort cmClockBeijing = 2018;
const ushort cmClockTokyo = 2019;
const ushort cmClockSydney = 2020;
const ushort cmImageNoDither = 90;
const ushort cmImageEfficientDither = 91;
const ushort cmImageNormalDither = 92;
const ushort cmImageAdaptiveDither = 93;
const ushort cmImageTrueColor = 94;

const int fixedGraphicPixelWidth = 160;
const int fixedGraphicPixelHeight = 96;
const int imageTrueColorMaxColors = 4096;

#define cpGraphicClientBackground "\x01"

enum TDstRule
{
    dstNone,
    dstUnitedStates,
    dstEurope,
    dstAustraliaSydney
};

// Current demo rules only. This is not a historical IANA time zone database.
struct TWorldClockCity
{
    const char *name;
    int standardOffsetMinutes;
    TDstRule dstRule;
    double latitudeDeg;
    double longitudeDeg;
    ushort command;
};

static const TWorldClockCity worldClockCities[] = {
    {"Los Angeles", -8*60, dstUnitedStates, 34.0522, -118.2437, cmClockLosAngeles},
    {"Chicago", -6*60, dstUnitedStates, 41.8781, -87.6298, cmClockChicago},
    {"New York", -5*60, dstUnitedStates, 40.7128, -74.0060, cmClockNewYork},
    {"London", 0, dstEurope, 51.5074, -0.1278, cmClockLondon},
    {"Berlin", 1*60, dstEurope, 52.5200, 13.4050, cmClockBerlin},
    {"Moscow", 3*60, dstNone, 55.7558, 37.6173, cmClockMoscow},
    {"Dubai", 4*60, dstNone, 25.2048, 55.2708, cmClockDubai},
    {"Mumbai", 5*60 + 30, dstNone, 19.0760, 72.8777, cmClockMumbai},
    {"Beijing", 8*60, dstNone, 39.9042, 116.4074, cmClockBeijing},
    {"Tokyo", 9*60, dstNone, 35.6762, 139.6503, cmClockTokyo},
    {"Sydney", 10*60, dstAustraliaSydney, -33.8688, 151.2093, cmClockSydney}
};

static bool isLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int monthDays(int year, int month)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return month == 2 && isLeapYear(year) ? 29 : days[month - 1];
}

static long long daysBeforeYear(int year)
{
    long long days = 0;
    if (year >= 1970)
        for (int y = 1970; y < year; ++y)
            days += isLeapYear(y) ? 366 : 365;
    else
        for (int y = year; y < 1970; ++y)
            days -= isLeapYear(y) ? 366 : 365;
    return days;
}

static long long daysBeforeMonth(int year, int month)
{
    long long days = 0;
    for (int m = 1; m < month; ++m)
        days += monthDays(year, m);
    return days;
}

static time_t utcDateTime(int year, int month, int day, int hour, int minute, int second)
{
    long long days = daysBeforeYear(year) + daysBeforeMonth(year, month) + day - 1;
    return time_t(days*86400 + hour*3600 + minute*60 + second);
}

static int utcYear(time_t utcTime)
{
    tm *utc = gmtime(&utcTime);
    return utc != 0 ? utc->tm_year + 1900 : 1970;
}

static int dayOfWeek(int year, int month, int day)
{
    long long days = daysBeforeYear(year) + daysBeforeMonth(year, month) + day - 1;
    int dow = int((days + 4) % 7);
    return dow < 0 ? dow + 7 : dow; // Sunday is 0, 1970-01-01 was Thursday.
}

static int firstSunday(int year, int month)
{
    int firstDow = dayOfWeek(year, month, 1);
    return 1 + (7 - firstDow) % 7;
}

static int secondSunday(int year, int month)
{
    return firstSunday(year, month) + 7;
}

static int lastSunday(int year, int month)
{
    int last = monthDays(year, month);
    int lastDow = dayOfWeek(year, month, last);
    return last - lastDow;
}

static time_t localRuleTimeUtc(int year, int month, int day, int hour,
                               int offsetMinutes)
{
    return utcDateTime(year, month, day, hour, 0, 0) - time_t(offsetMinutes)*60;
}

static bool daylightSavingActive(time_t utcTime, int standardOffsetMinutes,
                                 TDstRule rule)
{
    int year = utcYear(utcTime);
    switch (rule)
    {
    case dstUnitedStates:
    {
        time_t start = localRuleTimeUtc(year, 3, secondSunday(year, 3), 2,
                                        standardOffsetMinutes);
        time_t end = localRuleTimeUtc(year, 11, firstSunday(year, 11), 2,
                                      standardOffsetMinutes + 60);
        return utcTime >= start && utcTime < end;
    }
    case dstEurope:
    {
        time_t start = utcDateTime(year, 3, lastSunday(year, 3), 1, 0, 0);
        time_t end = utcDateTime(year, 10, lastSunday(year, 10), 1, 0, 0);
        return utcTime >= start && utcTime < end;
    }
    case dstAustraliaSydney:
    {
        time_t start = localRuleTimeUtc(year, 10, firstSunday(year, 10), 2,
                                        standardOffsetMinutes);
        time_t end = localRuleTimeUtc(year, 4, firstSunday(year, 4), 3,
                                      standardOffsetMinutes + 60);
        return utcTime < end || utcTime >= start;
    }
    case dstNone:
    default:
        return false;
    }
}

static int cityUtcOffsetMinutes(time_t utcTime, int standardOffsetMinutes,
                                TDstRule rule)
{
    return standardOffsetMinutes +
           (daylightSavingActive(utcTime, standardOffsetMinutes, rule) ? 60 : 0);
}

enum TImageDitherMode
{
    imageNoDither,
    imageEfficientDither,
    imageAdaptiveDither,
    imageNormalDither,
    imageTrueColor
};

static TMenuItem *imageNoDitherItem = 0;
static TMenuItem *imageEfficientDitherItem = 0;
static TMenuItem *imageAdaptiveDitherItem = 0;
static TMenuItem *imageNormalDitherItem = 0;
static TMenuItem *imageTrueColorItem = 0;
static TMenuItem *imageMenuItem = 0;

static void setMenuItemName(TMenuItem *item, const char *name)
{
    if (item == 0)
        return;
    delete[] (char *) item->name;
    item->name = newStr(name);
}

static void setImageCommandState(Boolean enable)
{
    if (imageMenuItem != 0)
        imageMenuItem->disabled = Boolean(!enable);
    if (enable)
    {
        TView::enableCommand(cmImageNoDither);
        TView::enableCommand(cmImageEfficientDither);
        TView::enableCommand(cmImageAdaptiveDither);
        TView::enableCommand(cmImageNormalDither);
        TView::enableCommand(cmImageTrueColor);
    }
    else
    {
        TView::disableCommand(cmImageNoDither);
        TView::disableCommand(cmImageEfficientDither);
        TView::disableCommand(cmImageAdaptiveDither);
        TView::disableCommand(cmImageNormalDither);
        TView::disableCommand(cmImageTrueColor);
    }
    if (TProgram::menuBar != 0)
        TProgram::menuBar->drawView();
}

static void updateImageMenuLabels(TImageDitherMode mode)
{
    setMenuItemName(imageNoDitherItem,
                    mode == imageNoDither ? "(*) ~N~o dithering" : "( ) ~N~o dithering");
    setMenuItemName(imageEfficientDitherItem,
                    mode == imageEfficientDither ? "(*) ~E~fficient dithering" : "( ) ~E~fficient dithering");
    setMenuItemName(imageAdaptiveDitherItem,
                    mode == imageAdaptiveDither ? "(*) ~A~daptive dithering" : "( ) ~A~daptive dithering");
    setMenuItemName(imageNormalDitherItem,
                    mode == imageNormalDither ? "(*) N~o~rmal dithering" : "( ) N~o~rmal dithering");
    setMenuItemName(imageTrueColorItem,
                    mode == imageTrueColor ? "(*) ~T~rue color palette" : "( ) ~T~rue color palette");
    if (TProgram::menuBar != 0)
        TProgram::menuBar->drawView();
}

static TGraphicColor demoClientBlue()
{
    // Demo windows use a graphics-backed client fill so stale raster areas are
    // restored by the graphics layer instead of approximate terminal colors.
    return TGraphicColor(3, 33, 192);
}

static TPoint graphicPixelSizeForCells(TPoint cells, const TGraphicProfile &profile)
{
    if (cells.x <= 0 || cells.y <= 0)
        return {0, 0};
    int cellWidth = profile.enabled && profile.cellWidth > 0 ? profile.cellWidth : 8;
    int cellHeight = profile.enabled && profile.cellHeight > 0 ? profile.cellHeight : 16;
    int fillWidth = profile.enabled && profile.fillWidth > 0 ? profile.fillWidth : cellWidth;
    int fillHeight = profile.enabled && profile.fillHeight > 0 ? profile.fillHeight : cellHeight;
    return {
        short((cells.x - 1)*cellWidth + fillWidth),
        short((cells.y - 1)*cellHeight + fillHeight)
    };
}

static TPoint pixelAtCellCenter(TPoint cell, TPoint cells, TPoint pixels, const TGraphicProfile &profile)
{
    int cellWidth = profile.enabled && profile.cellWidth > 0 ? profile.cellWidth : 8;
    int cellHeight = profile.enabled && profile.cellHeight > 0 ? profile.cellHeight : 16;
    int fillWidth = profile.enabled && profile.fillWidth > 0 ? profile.fillWidth : cellWidth;
    int fillHeight = profile.enabled && profile.fillHeight > 0 ? profile.fillHeight : cellHeight;
    int sampleWidth = cell.x + 1 == cells.x ? fillWidth : cellWidth;
    int sampleHeight = cell.y + 1 == cells.y ? fillHeight : cellHeight;
    return {
        short(min(max(0, int(pixels.x) - 1), cell.x*cellWidth + sampleWidth/2)),
        short(min(max(0, int(pixels.y) - 1), cell.y*cellHeight + sampleHeight/2))
    };
}

static TRect fixedGraphicBounds()
{
    TGraphicProfile profile = TGraphicRuntime::getProfile();
    int cellWidth = profile.enabled && profile.cellWidth > 0 ? profile.cellWidth : 8;
    int cellHeight = profile.enabled && profile.cellHeight > 0 ? profile.cellHeight : 16;
    int width = (fixedGraphicPixelWidth + cellWidth - 1)/cellWidth;
    int height = (fixedGraphicPixelHeight + cellHeight - 1)/cellHeight;
    return TRect(1, 1, 1 + max(1, width), 1 + max(1, height));
}

static int cellsForPixelLimit(int pixels, int maxCells, int cellSize, int fillSize)
{
    if (maxCells <= 1 || pixels <= fillSize)
        return max(1, min(maxCells, 1));
    return max(1, min(maxCells, (pixels - fillSize)/cellSize + 1));
}

static TRect centeredSquareGraphicBounds(TPoint windowSize)
{
    TPoint inner {
        short(max(1, int(windowSize.x) - 2)),
        short(max(1, int(windowSize.y) - 2))
    };
    TGraphicProfile profile = TGraphicRuntime::getProfile();
    int cellWidth = profile.enabled && profile.cellWidth > 0 ? profile.cellWidth : 8;
    int cellHeight = profile.enabled && profile.cellHeight > 0 ? profile.cellHeight : 16;
    int fillWidth = profile.enabled && profile.fillWidth > 0 ? profile.fillWidth : cellWidth;
    int fillHeight = profile.enabled && profile.fillHeight > 0 ? profile.fillHeight : cellHeight;
    TPoint pixels = graphicPixelSizeForCells(inner, profile);

    int width = inner.x;
    int height = inner.y;
    if (pixels.x > pixels.y)
        width = cellsForPixelLimit(pixels.y, inner.x, cellWidth, fillWidth);
    else if (pixels.y > pixels.x)
        height = cellsForPixelLimit(pixels.x, inner.y, cellHeight, fillHeight);

    int x = 1 + (inner.x - width)/2;
    int y = 1 + (inner.y - height)/2;
    return TRect(x, y, x + width, y + height);
}

static TGraphicColor randomPaintColor()
{
    static unsigned long state = 0;
    if (state == 0)
        state = (unsigned long) time(0) ^ 0x6D2B79F5UL;
    state = state*1664525UL + 1013904223UL;
    uchar r = uchar((state >> 16) & 0xFF);
    state = state*1664525UL + 1013904223UL;
    uchar g = uchar((state >> 16) & 0xFF);
    state = state*1664525UL + 1013904223UL;
    uchar b = uchar((state >> 16) & 0xFF);
    return TGraphicColor(r, g, b);
}

static int bayer8(int x, int y)
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

static int sixelBlockDither(int x, int y, TPoint canvasSize)
{
    static const uchar matrix[6][8] = {
        { 0, 32,  8, 40,  2, 34, 10, 42},
        {48, 16, 56, 24, 50, 18, 58, 26},
        {12, 44,  4, 36, 14, 46,  6, 38},
        {60, 28, 52, 20, 62, 30, 54, 22},
        { 3, 35, 11, 43,  1, 33,  9, 41},
        {51, 19, 59, 27, 49, 17, 57, 25}
    };
    long area = long(canvasSize.x)*long(canvasSize.y);
    int blockWidth = area >= 3840L*2160L ? 16 : area >= 1920L*1080L ? 8 : 4;
    int blockHeight = area >= 3840L*2160L ? 12 : 6;
    return matrix[(y/blockHeight) % 6][(x/blockWidth) & 7];
}

static void fillRect(TGraphicCanvas &canvas, int x0, int y0, int w, int h, TGraphicColor color)
{
    int x1 = max(0, x0);
    int y1 = max(0, y0);
    int x2 = min(canvas.size.x, x0 + w);
    int y2 = min(canvas.size.y, y0 + h);
    for (int y = y1; y < y2; ++y)
        for (int x = x1; x < x2; ++x)
            canvas.setPixel(x, y, color);
}

static const char *digitPattern(char ch)
{
    switch (ch)
    {
    case '0': return "111"
                     "101"
                     "101"
                     "101"
                     "111";
    case '1': return "010"
                     "110"
                     "010"
                     "010"
                     "111";
    case '2': return "111"
                     "001"
                     "111"
                     "100"
                     "111";
    case '3': return "111"
                     "001"
                     "111"
                     "001"
                     "111";
    case '4': return "101"
                     "101"
                     "111"
                     "001"
                     "001";
    case '5': return "111"
                     "100"
                     "111"
                     "001"
                     "111";
    case '6': return "111"
                     "100"
                     "111"
                     "101"
                     "111";
    case '7': return "111"
                     "001"
                     "010"
                     "010"
                     "010";
    case '8': return "111"
                     "101"
                     "111"
                     "101"
                     "111";
    case '9': return "111"
                     "101"
                     "111"
                     "001"
                     "111";
    case 'x':
    case 'X': return "101"
                     "101"
                     "010"
                     "101"
                     "101";
    case ' ': return "000"
                     "000"
                     "000"
                     "000"
                     "000";
    default: return "000"
                    "000"
                    "000"
                    "000"
                    "000";
    }
}

static void drawSmallText(TGraphicCanvas &canvas, int x0, int y0,
                          const char *text, int scale,
                          TGraphicColor color, TGraphicColor background)
{
    int len = strlen(text);
    int digitW = 3*scale;
    int digitH = 5*scale;
    int gap = scale;
    int pad = 3;
    fillRect(canvas, x0, y0, len*digitW + max(0, len - 1)*gap + 2*pad,
             digitH + 2*pad, background);

    int x = x0 + pad;
    for (int i = 0; i < len; ++i)
    {
        const char *pattern = digitPattern(text[i]);
        for (int py = 0; py < 5; ++py)
            for (int px = 0; px < 3; ++px)
                if (pattern[py*3 + px] == '1')
                    fillRect(canvas, x + px*scale, y0 + pad + py*scale,
                             scale, scale, color);
        x += digitW + gap;
    }
}

static void drawCounter(TGraphicCanvas &canvas, unsigned long value)
{
    char text[16];
    snprintf(text, sizeof(text), "%lu", value);
    int len = strlen(text);
    int scale = canvas.size.x >= 140 && canvas.size.y >= 80 ? 2 : 1;
    int digitW = 3*scale;
    int digitH = 5*scale;
    int gap = scale;
    int pad = 3;
    int w = len*digitW + max(0, len - 1)*gap + 2*pad;
    int h = digitH + 2*pad;
    int x0 = max(0, canvas.size.x - w - 4);
    int y0 = max(0, canvas.size.y - h - 4);

    drawSmallText(canvas, x0, y0, text, scale,
                  TGraphicColor(255, 245, 120), TGraphicColor(0, 0, 0));
    canvas.rect(x0, y0, w, h, TGraphicColor(250, 250, 250));
}

class TDiagnosticGraphicView : public TGraphicView
{
public:
    bool fixedContent;
    unsigned long paintCount;

    TDiagnosticGraphicView(const TRect &bounds, SizingMode mode, bool aFixedContent = false) :
        TGraphicView(bounds, aFixedContent ? fillGraphic : mode),
        fixedContent(aFixedContent),
        paintCount(0)
    {
        if (!fixedContent && mode == fixedGraphic)
            setFixedGraphicSize({fixedGraphicPixelWidth, fixedGraphicPixelHeight});
        if (sizingMode == fillGraphic)
            growMode = gfGrowHiX | gfGrowHiY;
    }

    void paintDiagnostic(TGraphicCanvas &canvas, int x0, int y0, int w, int h)
    {
        if (w <= 0 || h <= 0)
            return;
        for (int y = y0; y < y0 + h; ++y)
            for (int x = x0; x < x0 + w; ++x)
                canvas.setPixel(x, y, TGraphicColor(18, 24, 30));
        canvas.rect(x0, y0, w, h, TGraphicColor(240, 240, 240));
        canvas.line(x0, y0 + h/2, x0 + w - 1, y0 + h/2, TGraphicColor(240, 80, 60));
        canvas.line(x0 + w/2, y0, x0 + w/2, y0 + h - 1, TGraphicColor(80, 180, 250));
        int r = max(4, min(w, h)/3);
        canvas.circle(x0 + w/2, y0 + h/2, r, TGraphicColor(100, 240, 160));

        TGraphicColor border = randomPaintColor();
        int borderWidth = min(5, max(1, min(w, h)/2));
        for (int i = 0; i < borderWidth; ++i)
            canvas.rect(x0 + i, y0 + i, w - 2*i, h - 2*i, border);
    }

    virtual void paintGraphic(TGraphicCanvas &canvas)
    {
        ++paintCount;
        canvas.clear(TGraphicColor(13, 19, 26));
        if (fixedContent)
        {
            int w = min(fixedGraphicPixelWidth, canvas.size.x);
            int h = min(fixedGraphicPixelHeight, canvas.size.y);
            paintDiagnostic(canvas, 0, 0, w, h);
        }
        else
            paintDiagnostic(canvas, 0, 0, canvas.size.x, canvas.size.y);
        if (!fixedContent && sizingMode == fillGraphic)
        {
            char dims[32];
            snprintf(dims, sizeof(dims), "%dx%d", canvas.size.x, canvas.size.y);
            drawSmallText(canvas, 4, 4, dims, 2,
                          TGraphicColor(255, 245, 120),
                          TGraphicColor(0, 0, 0));
        }
        drawCounter(canvas, paintCount);
    }
};

class TGraphicClientBackground : public TGraphicView
{
public:
    TGraphicClientBackground(const TRect &bounds) :
        TGraphicView(bounds, fillGraphic)
    {
        growMode = gfGrowHiX | gfGrowHiY;
    }

    virtual void draw()
    {
        TDrawBuffer b;
        TAttrPair color = getColor(1);
        b.moveChar(0, ' ', color, size.x);
        for (int y = 0; y < size.y; ++y)
            writeLine(0, y, size.x, 1, b);
    }

    virtual TPalette &getPalette() const
    {
        static TPalette palette(cpGraphicClientBackground, sizeof(cpGraphicClientBackground) - 1);
        return palette;
    }

    virtual void paintGraphic(TGraphicCanvas &canvas)
    {
        canvas.clear(demoClientBlue());
    }
};

static void insertClientBackground(TWindow *window)
{
    window->insert(new TGraphicClientBackground(TRect(1, 1, window->size.x - 1, window->size.y - 1)));
}

class TClockGraphicView : public TGraphicView
{
public:
    const char *cityName;
    unsigned long paintCount;
    time_t lastUpdate;
    int standardOffsetMinutes;
    TDstRule dstRule;
    double latitudeDeg;
    double longitudeDeg;
    bool showMoonDetails;

    TClockGraphicView(const TRect &bounds, const char *aCityName,
                      int aStandardOffsetMinutes, TDstRule aDstRule, double aLatitudeDeg,
                      double aLongitudeDeg) :
        TGraphicView(bounds, fillGraphic),
        cityName(aCityName),
        paintCount(0),
        lastUpdate(0),
        standardOffsetMinutes(aStandardOffsetMinutes),
        dstRule(aDstRule),
        latitudeDeg(aLatitudeDeg),
        longitudeDeg(aLongitudeDeg),
        showMoonDetails(false)
    {
        eventMask |= evMouseDown;
    }

    void update()
    {
        time_t now = time(0);
        if (now != lastUpdate)
        {
            lastUpdate = now;
            TGraphicRuntime::invalidate(this);
        }
    }

    virtual void paintGraphic(TGraphicCanvas &canvas)
    {
        ++paintCount;
        time_t utcNow = time(0);
        int utcOffsetMinutes = cityUtcOffsetMinutes(utcNow, standardOffsetMinutes, dstRule);
        time_t cityNow = utcNow + time_t(utcOffsetMinutes)*60;
        TMoonObserver observer {latitudeDeg, longitudeDeg, utcOffsetMinutes};
        TMoonState moon = calculateMoonState(utcNow, observer);

        if (showMoonDetails)
        {
            drawMoonDetailView(canvas, moon, cityName, cityNow);
            drawCounter(canvas, paintCount);
            return;
        }

        tm *t = gmtime(&cityNow);
        double sec = t ? t->tm_sec : 0;
        double minv = t ? t->tm_min + sec/60.0 : 0;
        double hour = t ? (t->tm_hour % 12) + minv/60.0 : 0;
        int morningHours = t ? (t->tm_hour >= 12 ? 12 : t->tm_hour) : 0;

        const double pi = 3.14159265358979323846;
        canvas.clear(TGraphicColor(0, 0, 0, 0));

        int cx = canvas.size.x/2;
        int cy = canvas.size.y/2;
        int minDim = min(canvas.size.x, canvas.size.y);
        int margin = max(10, minDim/18);
        int r = max(8, minDim/2 - margin);
        int rim = max(2, r/24);

        auto filledCircle = [&] (int x0, int y0, int radius, TGraphicColor color) {
            if (radius <= 0)
                return;
            int rr = radius*radius;
            for (int y = -radius; y <= radius; ++y)
                for (int x = -radius; x <= radius; ++x)
                    if (x*x + y*y <= rr)
                        canvas.setPixel(x0 + x, y0 + y, color);
        };

        auto thickLine = [&] (int x0, int y0, int x1, int y1, int width, TGraphicColor color) {
            int dx = x1 - x0;
            int dy = y1 - y0;
            int steps = max(abs(dx), abs(dy));
            int radius = max(1, width/2);
            if (steps == 0)
            {
                filledCircle(x0, y0, radius, color);
                return;
            }
            for (int i = 0; i <= steps; ++i)
            {
                double f = double(i)/double(steps);
                int x = int(x0 + dx*f + (dx >= 0 ? 0.5 : -0.5));
                int y = int(y0 + dy*f + (dy >= 0 ? 0.5 : -0.5));
                filledCircle(x, y, radius, color);
            }
        };

        auto pointAt = [&] (double units, double scale) {
            double a = units/60.0*2.0*pi - pi/2.0;
            return TPoint{
                short(cx + int(cos(a)*r*scale + (cos(a) >= 0 ? 0.5 : -0.5))),
                short(cy + int(sin(a)*r*scale + (sin(a) >= 0 ? 0.5 : -0.5)))
            };
        };

        auto pixelPointAt = [&] (double units, double scale) {
            double a = units/60.0*2.0*pi - pi/2.0;
            return TPoint{
                short(cx + int(cos(a)*r*scale + (cos(a) >= 0 ? 0.5 : -0.5))),
                short(cy + int(sin(a)*r*scale + (sin(a) >= 0 ? 0.5 : -0.5)))
            };
        };

        filledCircle(cx, cy, r + rim, TGraphicColor(212, 222, 225));
        filledCircle(cx, cy, r, TGraphicColor(18, 25, 32));
        filledCircle(cx, cy, max(1, r - rim), TGraphicColor(13, 19, 26));

        for (int i = 0; i < 60; ++i)
        {
            bool hourMark = i % 5 == 0;
            double outer = 0.93;
            double inner = hourMark ? 0.79 : 0.87;
            TPoint a = pointAt(i, inner);
            TPoint b = pointAt(i, outer);
            int width = hourMark ? max(2, r/34) : max(1, r/80);
            TGraphicColor color = hourMark ? TGraphicColor(230, 236, 230) : TGraphicColor(95, 115, 130);
            thickLine(a.x, a.y, b.x, b.y, width, color);
        }

        for (int i = 0; i < 12; ++i)
        {
            TPoint p = pointAt(i*5, 0.69);
            bool passed = morningHours == 12 || (i >= 1 && i <= morningHours);
            TGraphicColor color = passed ?
                TGraphicColor(240, 244, 236) :
                TGraphicColor(80, 105, 120);
            filledCircle(p.x, p.y, max(1, r/48), color);
        }

        auto hand = [&] (double units, double scale, double backScale, int width, TGraphicColor color) {
            TPoint a = pointAt(units, -backScale);
            TPoint b = pointAt(units, scale);
            thickLine(a.x, a.y, b.x, b.y, width, color);
        };

        TPoint moonPos = pixelPointAt(0, 0.43);
        drawMoonIcon(canvas, moonPos.x, moonPos.y, max(4, r/9), moon);

        hand(hour*5.0, 0.50, 0.08, max(4, r/12), TGraphicColor(235, 190, 70));
        hand(minv, 0.72, 0.12, max(3, r/18), TGraphicColor(100, 200, 235));
        hand(sec, 0.84, 0.18, max(1, r/44), TGraphicColor(250, 80, 80));

        filledCircle(cx, cy, max(4, r/13), TGraphicColor(28, 36, 44));
        filledCircle(cx, cy, max(2, r/22), TGraphicColor(240, 244, 236));
        filledCircle(cx, cy, max(1, r/42), TGraphicColor(250, 80, 80));

        fillRect(canvas, 4, 4, 10, 10, randomPaintColor());
        drawCounter(canvas, paintCount);
    }

    virtual void handleEvent(TEvent &event)
    {
        TGraphicView::handleEvent(event);
        if (event.what == evMouseDown && mouseInView(event.mouse.where))
        {
            showMoonDetails = !showMoonDetails;
            TGraphicRuntime::invalidate(this);
            drawView();
            clearEvent(event);
        }
    }
};

class TColorInfoOverlay : public TView
{
public:
    char text[48];

    TColorInfoOverlay(const TRect &bounds) :
        TView(bounds)
    {
        text[0] = '\0';
        growMode = gfGrowLoX | gfGrowHiX | gfGrowLoY | gfGrowHiY;
    }

    void setInfo(int px, int py, TGraphicColor color)
    {
        char next[sizeof(text)];
        snprintf(next, sizeof(next), " x:%d y:%d rgb:%u,%u,%u ",
                 px, py, unsigned(color.r), unsigned(color.g), unsigned(color.b));
        if (strcmp(text, next) != 0)
        {
            strcpy(text, next);
            drawView();
        }
    }

    virtual void draw()
    {
        TDrawBuffer b;
        TAttrPair color = getColor(1);
        b.moveChar(0, ' ', color, size.x);
        int textLen = min(int(strlen(text)), int(size.x));
        if (textLen > 0)
            b.moveStr(size.x - textLen, text, color, textLen);
        writeLine(0, 0, size.x, 1, b);
    }
};

class TColorRangeGraphicView : public TGraphicView
{
public:
    bool dithered;
    unsigned long paintCount;
    TColorInfoOverlay *infoOverlay;

    TColorRangeGraphicView(const TRect &bounds, bool aDithered) :
        TGraphicView(bounds, fillGraphic),
        dithered(aDithered),
        paintCount(0),
        infoOverlay(0)
    {
        growMode = gfGrowHiX | gfGrowHiY;
        eventMask |= evMouseMove;
    }

    static int clampByte(int value)
    {
        return max(0, min(255, value));
    }

    static int quantize(int value, int levels, int threshold)
    {
        int scaled = value*(levels - 1);
        int base = scaled/255;
        int rem = scaled - base*255;
        if (base < levels - 1 && rem*64 > threshold*255)
            ++base;
        return (base*255)/(levels - 1);
    }

    TGraphicColor colorFor(int x, int y, int width, int band, int bandY, int bandHeight) const
    {
        int w = max(1, width - 1);
        int h = max(1, bandHeight - 1);
        int horizontal = (x*255)/w;
        int vertical = (bandY*255)/h;
        int r = 0, g = 0, b = 0;

        switch (band)
        {
        case 0:
            r = horizontal;
            g = vertical;
            b = 32;
            break;
        case 1:
            r = horizontal;
            g = 32;
            b = vertical;
            break;
        case 2:
            r = 32;
            g = horizontal;
            b = vertical;
            break;
        default:
            r = horizontal;
            g = 255 - horizontal;
            b = vertical;
            break;
        }

        if (dithered)
        {
            int t = bayer8(x, y);
            r = quantize(r, 6, t);
            g = quantize(g, 6, (t + 21) & 63);
            b = quantize(b, 6, (t + 42) & 63);
        }

        return TGraphicColor(clampByte(r), clampByte(g), clampByte(b));
    }

    TGraphicColor colorAtPixel(int x, int y, int width, int height) const
    {
        int bands = 4;
        int bandHeight = max(1, height/bands);
        int band = min(bands - 1, y/max(1, bandHeight));
        int top = (band*height)/bands;
        int bottom = ((band + 1)*height)/bands;
        return colorFor(x, y, width, band, y - top, max(1, bottom - top));
    }

    void updateInfo(TPoint mouse)
    {
        if (infoOverlay == 0)
            return;
        TPoint cell = makeLocal(mouse);
        if (0 <= cell.x && cell.x < size.x && 0 <= cell.y && cell.y < size.y)
        {
            TGraphicProfile profile = TGraphicRuntime::getProfile();
            TPoint pixels = graphicPixelSizeForCells(size, profile);
            TPoint pixel = pixelAtCellCenter(cell, size, pixels, profile);
            TGraphicColor color = colorAtPixel(pixel.x, pixel.y, pixels.x, pixels.y);
            infoOverlay->setInfo(pixel.x, pixel.y, color);
        }
    }

    virtual void handleEvent(TEvent &event)
    {
        TGraphicView::handleEvent(event);
        if (event.what == evMouseMove || event.what == evMouseDown)
            updateInfo(event.mouse.where);
    }

    virtual void paintGraphic(TGraphicCanvas &canvas)
    {
        ++paintCount;
        canvas.clear(TGraphicColor(8, 10, 14));
        int bands = 4;
        int bandHeight = max(1, canvas.size.y/bands);
        for (int y = 0; y < canvas.size.y; ++y)
        {
            int band = min(bands - 1, y/max(1, bandHeight));
            int top = (band*canvas.size.y)/bands;
            int bottom = ((band + 1)*canvas.size.y)/bands;
            int localY = y - top;
            int localH = max(1, bottom - top);
            for (int x = 0; x < canvas.size.x; ++x)
                canvas.setPixel(x, y, colorFor(x, y, canvas.size.x, band, localY, localH));
        }

        TGraphicColor line(230, 230, 230);
        for (int i = 1; i < bands; ++i)
        {
            int y = (i*canvas.size.y)/bands;
            canvas.line(0, y, canvas.size.x - 1, y, line);
        }

        canvas.rect(0, 0, canvas.size.x, canvas.size.y, TGraphicColor(245, 245, 245));
        drawCounter(canvas, paintCount);
    }
};

struct TLoadedImage
{
    int width {0};
    int height {0};
    std::vector<TGraphicColor> pixels;

    bool empty() const
    {
        return width <= 0 || height <= 0 || pixels.empty();
    }
};

static uint16_t readLe16(const std::vector<uchar> &data, size_t offset)
{
    if (offset + 2 > data.size())
        return 0;
    return uint16_t(data[offset]) | (uint16_t(data[offset + 1]) << 8);
}

static uint32_t readLe32(const std::vector<uchar> &data, size_t offset)
{
    if (offset + 4 > data.size())
        return 0;
    return uint32_t(data[offset]) |
           (uint32_t(data[offset + 1]) << 8) |
           (uint32_t(data[offset + 2]) << 16) |
           (uint32_t(data[offset + 3]) << 24);
}

static bool loadBmp(const char *path, TLoadedImage &image)
{
    FILE *file = fopen(path, "rb");
    if (file == 0)
        return false;

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length <= 0)
    {
        fclose(file);
        return false;
    }

    std::vector<uchar> data((size_t(length)));
    bool ok = fread(data.data(), 1, data.size(), file) == data.size();
    fclose(file);
    if (!ok || data.size() < 54 || data[0] != 'B' || data[1] != 'M')
        return false;

    uint32_t pixelOffset = readLe32(data, 10);
    uint32_t dibSize = readLe32(data, 14);
    int width = int(readLe32(data, 18));
    int signedHeight = int(readLe32(data, 22));
    uint16_t planes = readLe16(data, 26);
    uint16_t bpp = readLe16(data, 28);
    uint32_t compression = readLe32(data, 30);
    if (dibSize < 40 || width <= 0 || signedHeight == 0 ||
        planes != 1 || (bpp != 24 && bpp != 32) ||
        (compression != 0 && compression != 3))
        return false;

    int height = signedHeight < 0 ? -signedHeight : signedHeight;
    bool topDown = signedHeight < 0;
    int stride = int(((uint64_t(width)*bpp + 31)/32)*4);
    if (pixelOffset >= data.size() ||
        uint64_t(pixelOffset) + uint64_t(stride)*height > data.size())
        return false;

    image.width = width;
    image.height = height;
    image.pixels.assign(size_t(width)*height, TGraphicColor());
    for (int y = 0; y < height; ++y)
    {
        int sy = topDown ? y : height - 1 - y;
        const uchar *row = &data[pixelOffset + size_t(sy)*stride];
        for (int x = 0; x < width; ++x)
        {
            const uchar *p = row + size_t(x)*(bpp/8);
            uchar b = p[0];
            uchar g = p[1];
            uchar r = p[2];
            uchar a = bpp == 32 ? p[3] : 255;
            image.pixels[size_t(y)*width + x] = TGraphicColor(r, g, b, a);
        }
    }
    return true;
}

static std::string shellQuote(const char *s)
{
    std::string out("'");
    for (const char *p = s; *p; ++p)
    {
        if (*p == '\'')
            out += "'\\''";
        else
            out += *p;
    }
    out += "'";
    return out;
}

static bool convertImageToBmp(const char *source, char *target, size_t targetSize)
{
#ifdef _WIN32
    snprintf(target, targetSize, "sixeldemo-image-XXXXXX.bmp");
    if (_mktemp_s(target, targetSize) != 0)
        return false;
    FILE *created = fopen(target, "wb");
    if (!created)
        return false;
    fclose(created);
#else
    snprintf(target, targetSize, "/tmp/sixeldemo-image-XXXXXX.bmp");
    int fd = mkstemps(target, 4);
    if (fd < 0)
        return false;
    close(fd);
#endif

    std::string in = shellQuote(source);
    std::string out = shellQuote(target);
    std::string bmp32 = shellQuote((std::string("BMP32:") + target).c_str());
    std::string commands[3] = {
        std::string("command -v sips >/dev/null 2>&1 && sips -s format bmp ") +
            in + " --out " + out + " >/dev/null 2>&1",
        std::string("command -v magick >/dev/null 2>&1 && magick ") +
            in + " " + bmp32 + " >/dev/null 2>&1",
        std::string("command -v convert >/dev/null 2>&1 && convert ") +
            in + " " + bmp32 + " >/dev/null 2>&1",
    };

    for (std::string &command : commands)
        if (system(command.c_str()) == 0)
            return true;

    remove(target);
    target[0] = '\0';
    return false;
}

static bool loadImageFile(const char *path, TLoadedImage &image)
{
    if (loadBmp(path, image))
        return true;

    char converted[MAXPATH];
    if (!convertImageToBmp(path, converted, sizeof(converted)))
        return false;
    bool ok = loadBmp(converted, image);
    remove(converted);
    return ok;
}

static TGraphicColor blendOver(TGraphicColor src, TGraphicColor dst)
{
    if (src.a == 255)
        return src;
    int a = src.a;
    int ia = 255 - a;
    return TGraphicColor(
        uchar((int(src.r)*a + int(dst.r)*ia)/255),
        uchar((int(src.g)*a + int(dst.g)*ia)/255),
        uchar((int(src.b)*a + int(dst.b)*ia)/255)
    );
}

static int clampByte(int value)
{
    return max(0, min(255, value));
}

static int colorDistance2(TGraphicColor a, TGraphicColor b)
{
    int dr = int(a.r) - int(b.r);
    int dg = int(a.g) - int(b.g);
    int db = int(a.b) - int(b.b);
    return dr*dr + dg*dg + db*db;
}

static TGraphicColor nearest16Color(TGraphicColor color)
{
    static const TGraphicColor palette[16] = {
        TGraphicColor(0, 0, 0),       TGraphicColor(0, 0, 170),
        TGraphicColor(0, 170, 0),     TGraphicColor(0, 170, 170),
        TGraphicColor(170, 0, 0),     TGraphicColor(170, 0, 170),
        TGraphicColor(170, 85, 0),    TGraphicColor(170, 170, 170),
        TGraphicColor(85, 85, 85),    TGraphicColor(85, 85, 255),
        TGraphicColor(85, 255, 85),   TGraphicColor(85, 255, 255),
        TGraphicColor(255, 85, 85),   TGraphicColor(255, 85, 255),
        TGraphicColor(255, 255, 85),  TGraphicColor(255, 255, 255),
    };

    int best = 0;
    int bestDistance = 0x7FFFFFFF;
    for (int i = 0; i < 16; ++i)
    {
        int dr = int(color.r) - int(palette[i].r);
        int dg = int(color.g) - int(palette[i].g);
        int db = int(color.b) - int(palette[i].b);
        int distance = dr*dr + dg*dg + db*db;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }
    return palette[best];
}

static TGraphicColor quantizeForProfile(TGraphicColor color, int x, int y,
                                        int maxColors, TPoint canvasSize,
                                        TImageDitherMode ditherMode)
{
    if (ditherMode == imageTrueColor)
        return color;

    if (maxColors > 256)
        return color;

    if (ditherMode != imageNoDither)
    {
        int dither = ditherMode == imageNormalDither ?
            bayer8(x, y) :
            sixelBlockDither(x, y, canvasSize);
        int threshold = (dither*255)/63 - 128;
        int spread = maxColors <= 16 ? 42 : 24;
        color.r = uchar(clampByte(int(color.r) + (threshold*spread)/128));
        color.g = uchar(clampByte(int(color.g) + (threshold*spread)/128));
        color.b = uchar(clampByte(int(color.b) + (threshold*spread)/128));
    }

    if (maxColors <= 16)
        return nearest16Color(color);

    int levels = maxColors <= 64 ? 4 : 6;
    return TGraphicColor(
        uchar(((int(color.r)*(levels - 1) + 127)/255)*255/(levels - 1)),
        uchar(((int(color.g)*(levels - 1) + 127)/255)*255/(levels - 1)),
        uchar(((int(color.b)*(levels - 1) + 127)/255)*255/(levels - 1))
    );
}

class TImageGraphicView : public TGraphicView
{
public:
    TLoadedImage image;
    TImageDitherMode ditherMode;

    TImageGraphicView(const TRect &bounds, const TLoadedImage &aImage) :
        TGraphicView(bounds, fillGraphic),
        image(aImage),
        ditherMode(imageEfficientDither)
    {
        growMode = gfGrowHiX | gfGrowHiY;
    }

    TGraphicColor sampledColor(int x, int y, int dstW, int dstH) const
    {
        int syi = min(image.height - 1, (y*image.height)/dstH);
        int sxi = min(image.width - 1, (x*image.width)/dstW);
        return blendOver(image.pixels[size_t(syi)*image.width + sxi], demoClientBlue());
    }

    void paintOrdered(TGraphicCanvas &canvas, int dstX, int dstY,
                      int dstW, int dstH, int maxColors)
    {
        for (int y = 0; y < dstH; ++y)
            for (int x = 0; x < dstW; ++x)
            {
                TGraphicColor color = sampledColor(x, y, dstW, dstH);
                color = quantizeForProfile(color, dstX + x, dstY + y,
                                           maxColors, canvas.size, ditherMode);
                canvas.setPixel(dstX + x, dstY + y, color);
            }
    }

    void paintAdaptive(TGraphicCanvas &canvas, int dstX, int dstY,
                       int dstW, int dstH, int maxColors)
    {
        long area = long(canvas.size.x)*long(canvas.size.y);
        int blockW = area >= 3840L*2160L ? 16 : area >= 1920L*1080L ? 8 : 4;
        int blockH = area >= 3840L*2160L ? 12 : 6;
        int uniformThreshold = maxColors <= 16 ? 1200 : maxColors <= 64 ? 700 : 420;
        int detailThreshold = maxColors <= 16 ? 2600 : maxColors <= 64 ? 1800 : 1200;

        for (int by = 0; by < dstH; by += blockH)
            for (int bx = 0; bx < dstW; bx += blockW)
            {
                int bw = min(blockW, dstW - bx);
                int bh = min(blockH, dstH - by);
                int count = bw*bh;
                long sr = 0, sg = 0, sb = 0;
                for (int y = 0; y < bh; ++y)
                    for (int x = 0; x < bw; ++x)
                    {
                        TGraphicColor color = sampledColor(bx + x, by + y, dstW, dstH);
                        sr += color.r;
                        sg += color.g;
                        sb += color.b;
                    }

                TGraphicColor avg(
                    uchar(sr/count),
                    uchar(sg/count),
                    uchar(sb/count)
                );
                TGraphicColor quantizedAvg = quantizeForProfile(avg, dstX + bx, dstY + by,
                                                                maxColors, canvas.size,
                                                                imageNoDither);
                long variance = 0;
                for (int y = 0; y < bh; ++y)
                    for (int x = 0; x < bw; ++x)
                        variance += colorDistance2(sampledColor(bx + x, by + y, dstW, dstH), avg);
                variance /= count;

                bool uniform =
                    colorDistance2(avg, quantizedAvg) <= uniformThreshold &&
                    variance <= detailThreshold;

                for (int y = 0; y < bh; ++y)
                    for (int x = 0; x < bw; ++x)
                    {
                        TGraphicColor color = quantizedAvg;
                        if (!uniform)
                        {
                            color = sampledColor(bx + x, by + y, dstW, dstH);
                            color = quantizeForProfile(color, dstX + bx + x, dstY + by + y,
                                                       maxColors, canvas.size,
                                                       imageEfficientDither);
                        }
                        canvas.setPixel(dstX + bx + x, dstY + by + y, color);
                    }
            }
    }

    virtual void paintGraphic(TGraphicCanvas &canvas)
    {
        canvas.clear(demoClientBlue());
        if (image.empty() || canvas.size.x <= 0 || canvas.size.y <= 0)
            return;

        double sx = double(canvas.size.x)/double(image.width);
        double sy = double(canvas.size.y)/double(image.height);
        double scale = sx < sy ? sx : sy;
        int dstW = max(1, min(canvas.size.x, int(image.width*scale + 0.5)));
        int dstH = max(1, min(canvas.size.y, int(image.height*scale + 0.5)));
        int dstX = (canvas.size.x - dstW)/2;
        int dstY = (canvas.size.y - dstH)/2;

        TGraphicProfile profile = TGraphicRuntime::getProfile();
        int maxColors = ditherMode == imageTrueColor ?
            min(imageTrueColorMaxColors, profile.enabled ? int(profile.maxColors) : 256) :
            profile.enabled ? profile.maxColors : 16;
        if (ditherMode == imageAdaptiveDither)
            paintAdaptive(canvas, dstX, dstY, dstW, dstH, maxColors);
        else
            paintOrdered(canvas, dstX, dstY, dstW, dstH, maxColors);
    }

    virtual int graphicMaxColors(const TGraphicProfile &profile) const noexcept
    {
        return ditherMode == imageTrueColor ?
            min(imageTrueColorMaxColors, int(profile.maxColors)) :
            profile.maxColors;
    }
};

struct TMandelbrotParams
{
    double centerX {-0.75};
    double centerY {0.0};
    double zoom {1.0};
    int maxIterations {256};
    double escapeRadius {2.0};
};

class TMandelbrotGraphicView : public TGraphicView
{
public:
    TMandelbrotParams params;
    std::vector<TGraphicColor> pixels;
    std::vector<TGraphicColor> nextPixels;
    int imageWidth;
    int imageHeight;
    int calcWidth;
    int calcHeight;
    int nextRow;
    bool calculating;

    TMandelbrotGraphicView(const TRect &bounds) :
        TGraphicView(bounds, fillGraphic),
        imageWidth(0),
        imageHeight(0),
        calcWidth(0),
        calcHeight(0),
        nextRow(0),
        calculating(false)
    {
        growMode = gfGrowHiX | gfGrowHiY;
    }

    void startCalculation()
    {
        TGraphicProfile profile = TGraphicRuntime::getProfile();
        TPoint target = graphicPixelSizeForCells(size, profile);
        calcWidth = max(1, int(target.x));
        calcHeight = max(1, int(target.y));
        nextPixels.assign(size_t(calcWidth)*calcHeight, TGraphicColor(0, 0, 0));
        nextRow = 0;
        calculating = true;
    }

    static TGraphicColor colorFor(double mu, int maxIterations)
    {
        double t = max(0.0, min(1.0, mu/double(maxIterations)));
        double u = 1.0 - t;
        int r = int(9.0*u*t*t*t*255.0 + 0.5);
        int g = int(15.0*u*u*t*t*255.0 + 0.5);
        int b = int(8.5*u*u*u*t*255.0 + 0.5);
        return TGraphicColor(uchar(clampByte(r)),
                             uchar(clampByte(g)),
                             uchar(clampByte(b)));
    }

    void calculateRow(int y)
    {
        double aspect = calcHeight > 0 ? double(calcHeight)/double(calcWidth) : 1.0;
        double spanX = 3.2/params.zoom;
        double spanY = spanX*aspect;
        double escape2 = params.escapeRadius*params.escapeRadius;

        for (int x = 0; x < calcWidth; ++x)
        {
            double cr = params.centerX + (double(x)/double(max(1, calcWidth - 1)) - 0.5)*spanX;
            double ci = params.centerY + (double(y)/double(max(1, calcHeight - 1)) - 0.5)*spanY;
            double zr = 0.0;
            double zi = 0.0;
            double zr2 = 0.0;
            double zi2 = 0.0;
            int iter = 0;

            while (iter < params.maxIterations && zr2 + zi2 <= escape2)
            {
                zi = 2.0*zr*zi + ci;
                zr = zr2 - zi2 + cr;
                zr2 = zr*zr;
                zi2 = zi*zi;
                ++iter;
            }

            TGraphicColor color(5, 8, 15);
            if (iter < params.maxIterations)
            {
                double magnitude = sqrt(max(zr2 + zi2, 1.0));
                double smooth = double(iter) + 1.0 - log(log(magnitude))/log(2.0);
                color = colorFor(smooth, params.maxIterations);
            }
            nextPixels[size_t(y)*calcWidth + x] = color;
        }
    }

    bool stepCalculation()
    {
        if (!calculating)
            return false;

        int rows = max(1, 4096/max(1, calcWidth));
        while (rows-- > 0 && nextRow < calcHeight)
        {
            calculateRow(nextRow);
            ++nextRow;
        }

        if (nextRow < calcHeight)
            return false;

        pixels.swap(nextPixels);
        nextPixels.clear();
        imageWidth = calcWidth;
        imageHeight = calcHeight;
        calculating = false;
        TGraphicRuntime::invalidate(this);
        return true;
    }

    virtual void paintGraphic(TGraphicCanvas &canvas)
    {
        canvas.clear(TGraphicColor(5, 8, 15));
        if (pixels.empty() || imageWidth <= 0 || imageHeight <= 0)
            return;

        for (int y = 0; y < canvas.size.y; ++y)
        {
            int sy = min(imageHeight - 1, (y*imageHeight)/max(1, int(canvas.size.y)));
            for (int x = 0; x < canvas.size.x; ++x)
            {
                int sx = min(imageWidth - 1, (x*imageWidth)/max(1, int(canvas.size.x)));
                canvas.setPixel(x, y, pixels[size_t(sy)*imageWidth + sx]);
            }
        }
    }

    virtual int graphicMaxColors(const TGraphicProfile &) const noexcept
    {
        return imageTrueColorMaxColors;
    }
};

static void setButtonTitle(TButton *button, const char *title)
{
    if (button == 0)
        return;
    delete[] (char *) button->title;
    button->title = newStr(title);
    button->drawView();
}

class TMandelbrotWindow : public TWindow
{
public:
    TMandelbrotGraphicView *mandelbrot;
    TStaticText *titleText;
    TStaticText *centerXLabel;
    TStaticText *centerYLabel;
    TStaticText *zoomLabel;
    TStaticText *iterationsLabel;
    TStaticText *escapeLabel;
    TStaticText *paletteText;
    TInputLine *centerXInput;
    TInputLine *centerYInput;
    TInputLine *zoomInput;
    TInputLine *iterationsInput;
    TInputLine *escapeInput;
    TButton *updateButton;
    int spinnerFrame;
    clock_t lastSpinnerClock;

    TMandelbrotWindow() :
        TWindowInit(TWindow::initFrame),
        TWindow(TRect(6, 4, 76, 25), "Mandelbrot", wnNoNumber),
        mandelbrot(0),
        titleText(0),
        centerXLabel(0),
        centerYLabel(0),
        zoomLabel(0),
        iterationsLabel(0),
        escapeLabel(0),
        paletteText(0),
        centerXInput(0),
        centerYInput(0),
        zoomInput(0),
        iterationsInput(0),
        escapeInput(0),
        updateButton(0),
        spinnerFrame(0),
        lastSpinnerClock(0)
    {
        options |= ofTileable;
        insertClientBackground(this);
        mandelbrot = new TMandelbrotGraphicView(mandelbrotBounds());
        titleText = new TStaticText(TRect(2, 1, 24, 2), "Mandelbrot params");
        centerXLabel = new TStaticText(labelBounds(3), "center re");
        centerYLabel = new TStaticText(labelBounds(4), "center im");
        zoomLabel = new TStaticText(labelBounds(5), "zoom");
        iterationsLabel = new TStaticText(labelBounds(6), "max iter");
        escapeLabel = new TStaticText(labelBounds(7), "escape");
        paletteText = new TStaticText(TRect(2, 8, 25, 9), "palette    smooth");
        centerXInput = new TInputLine(inputBounds(3), 16);
        centerYInput = new TInputLine(inputBounds(4), 16);
        zoomInput = new TInputLine(inputBounds(5), 16);
        iterationsInput = new TInputLine(inputBounds(6), 16);
        escapeInput = new TInputLine(inputBounds(7), 16);
        updateButton = new TButton(buttonBounds(), "~U~pdate view",
                                   cmUpdateMandelbrot, bfNormal);
        insert(mandelbrot);
        insert(titleText);
        insert(centerXLabel);
        insert(centerYLabel);
        insert(zoomLabel);
        insert(iterationsLabel);
        insert(escapeLabel);
        insert(paletteText);
        insert(centerXInput);
        insert(centerYInput);
        insert(zoomInput);
        insert(iterationsInput);
        insert(escapeInput);
        insert(updateButton);
        updateFields();
        centerXInput->focus();
        startUpdate();
    }

    TRect labelBounds(int y) const
    {
        return TRect(2, y, min(12, int(size.x) - 2), min(y + 1, int(size.y) - 1));
    }

    TRect inputBounds(int y) const
    {
        return TRect(13, y, min(25, int(size.x) - 2), min(y + 1, int(size.y) - 1));
    }

    TRect buttonBounds() const
    {
        int top = min(10, max(1, int(size.y) - 3));
        return TRect(2, top, min(24, int(size.x) - 2), min(top + 2, int(size.y) - 1));
    }

    TRect mandelbrotBounds() const
    {
        int left = min(26, max(2, int(size.x) - 8));
        if (left >= int(size.x) - 1)
            left = 1;
        return TRect(left, 1, max(left + 1, int(size.x) - 1), max(2, int(size.y) - 1));
    }

    static void setFieldText(TInputLine *input, const char *text)
    {
        if (input == 0)
            return;
        snprintf(input->data, size_t(input->maxLen) + 1, "%s", text);
        input->selectAll(False, False);
        input->drawView();
    }

    void updateFields()
    {
        char text[32];
        snprintf(text, sizeof(text), "%.6f", mandelbrot->params.centerX);
        setFieldText(centerXInput, text);
        snprintf(text, sizeof(text), "%.6f", mandelbrot->params.centerY);
        setFieldText(centerYInput, text);
        snprintf(text, sizeof(text), "%.3f", mandelbrot->params.zoom);
        setFieldText(zoomInput, text);
        snprintf(text, sizeof(text), "%d", mandelbrot->params.maxIterations);
        setFieldText(iterationsInput, text);
        snprintf(text, sizeof(text), "%.3f", mandelbrot->params.escapeRadius);
        setFieldText(escapeInput, text);
    }

    static bool isBlankTail(const char *text)
    {
        while (*text != '\0')
        {
            if (*text != ' ' && *text != '\t')
                return false;
            ++text;
        }
        return true;
    }

    static double readDouble(TInputLine *input, double fallback,
                             double minValue, double maxValue)
    {
        if (input == 0)
            return fallback;
        char *end = 0;
        double value = strtod(input->data, &end);
        if (end == input->data || !isBlankTail(end))
            return fallback;
        if (value < minValue)
            value = minValue;
        if (value > maxValue)
            value = maxValue;
        return value;
    }

    static int readInt(TInputLine *input, int fallback,
                       int minValue, int maxValue)
    {
        if (input == 0)
            return fallback;
        char *end = 0;
        long value = strtol(input->data, &end, 10);
        if (end == input->data || !isBlankTail(end))
            return fallback;
        if (value < minValue)
            value = minValue;
        if (value > maxValue)
            value = maxValue;
        return int(value);
    }

    void readFields()
    {
        mandelbrot->params.centerX =
            readDouble(centerXInput, mandelbrot->params.centerX, -2.5, 1.5);
        mandelbrot->params.centerY =
            readDouble(centerYInput, mandelbrot->params.centerY, -1.5, 1.5);
        mandelbrot->params.zoom =
            readDouble(zoomInput, mandelbrot->params.zoom, 0.05, 1000000.0);
        mandelbrot->params.maxIterations =
            readInt(iterationsInput, mandelbrot->params.maxIterations, 16, 4096);
        mandelbrot->params.escapeRadius =
            readDouble(escapeInput, mandelbrot->params.escapeRadius, 2.0, 16.0);
        updateFields();
    }

    void startUpdate()
    {
        if (mandelbrot == 0)
            return;
        readFields();
        mandelbrot->startCalculation();
        spinnerFrame = 0;
        lastSpinnerClock = 0;
        setButtonTitle(updateButton, u8"⠋ Updating");
    }

    void updateButtonAnimation()
    {
        if (mandelbrot == 0 || !mandelbrot->calculating || updateButton == 0)
            return;

        clock_t now = clock();
        if (lastSpinnerClock != 0 &&
            now - lastSpinnerClock < CLOCKS_PER_SEC/12)
            return;

        static const char *frames[] = {
            u8"⠋ Updating", u8"⠙ Updating", u8"⠹ Updating", u8"⠸ Updating",
            u8"⠼ Updating", u8"⠴ Updating", u8"⠦ Updating", u8"⠧ Updating",
            u8"⠇ Updating", u8"⠏ Updating"
        };
        lastSpinnerClock = now;
        spinnerFrame = (spinnerFrame + 1) %
            int(sizeof(frames)/sizeof(frames[0]));
        setButtonTitle(updateButton, frames[spinnerFrame]);
    }

    void update()
    {
        if (mandelbrot == 0 || !mandelbrot->calculating)
            return;
        updateButtonAnimation();
        if (mandelbrot->stepCalculation())
            setButtonTitle(updateButton, "~U~pdate view");
    }

    virtual void changeBounds(const TRect &bounds)
    {
        TWindow::changeBounds(bounds);
        if (mandelbrot != 0)
            mandelbrot->changeBounds(mandelbrotBounds());
        if (titleText != 0)
            titleText->changeBounds(TRect(2, 1, min(24, int(size.x) - 2), min(2, int(size.y) - 1)));
        if (centerXLabel != 0)
            centerXLabel->changeBounds(labelBounds(3));
        if (centerYLabel != 0)
            centerYLabel->changeBounds(labelBounds(4));
        if (zoomLabel != 0)
            zoomLabel->changeBounds(labelBounds(5));
        if (iterationsLabel != 0)
            iterationsLabel->changeBounds(labelBounds(6));
        if (escapeLabel != 0)
            escapeLabel->changeBounds(labelBounds(7));
        if (paletteText != 0)
            paletteText->changeBounds(TRect(2, 8, min(25, int(size.x) - 2), min(9, int(size.y) - 1)));
        if (centerXInput != 0)
            centerXInput->changeBounds(inputBounds(3));
        if (centerYInput != 0)
            centerYInput->changeBounds(inputBounds(4));
        if (zoomInput != 0)
            zoomInput->changeBounds(inputBounds(5));
        if (iterationsInput != 0)
            iterationsInput->changeBounds(inputBounds(6));
        if (escapeInput != 0)
            escapeInput->changeBounds(inputBounds(7));
        if (updateButton != 0)
            updateButton->changeBounds(buttonBounds());
    }

    virtual TPalette &getPalette() const
    {
        static TPalette blue(cpBlueDialog, sizeof(cpBlueDialog) - 1);
        static TPalette cyan(cpCyanDialog, sizeof(cpCyanDialog) - 1);
        static TPalette gray(cpGrayDialog, sizeof(cpGrayDialog) - 1);
        static TPalette *palettes[] = { &blue, &cyan, &gray };
        return *palettes[palette];
    }

    virtual void sizeLimits(TPoint &min, TPoint &max)
    {
        TWindow::sizeLimits(min, max);
        if (min.x < 40)
            min.x = 40;
        if (min.y < 14)
            min.y = 14;
    }

    virtual void handleEvent(TEvent &event)
    {
        TWindow::handleEvent(event);
        if (event.what == evCommand && event.message.command == cmUpdateMandelbrot)
        {
            if (mandelbrot == 0 || !mandelbrot->calculating)
                startUpdate();
            clearEvent(event);
        }
    }
};

class TGraphicWindow : public TWindow
{
public:
    TGraphicWindow(const char *title, TGraphicView *view) :
        TWindowInit(TWindow::initFrame),
        TWindow(TRect(4, 3, 44, 17), title, wnNoNumber)
    {
        options |= ofTileable;
        insertClientBackground(this);
        insert(view);
    }
};

class TClockGraphicWindow : public TWindow
{
public:
    TClockGraphicView *clock;

    TClockGraphicWindow(const char *cityName, int standardOffsetMinutes,
                        TDstRule dstRule,
                        double latitudeDeg, double longitudeDeg) :
        TWindowInit(TWindow::initFrame),
        TWindow(TRect(4, 3, 44, 17), cityName, wnNoNumber),
        clock(0)
    {
        options |= ofTileable;
        insertClientBackground(this);
        clock = new TClockGraphicView(centeredSquareGraphicBounds(size), cityName,
                                      standardOffsetMinutes, dstRule,
                                      latitudeDeg, longitudeDeg);
        insert(clock);
    }

    virtual void changeBounds(const TRect &bounds)
    {
        TWindow::changeBounds(bounds);
        if (clock != 0)
            clock->changeBounds(centeredSquareGraphicBounds(size));
    }
};

class TColorGraphicWindow : public TWindow
{
public:
    TColorGraphicWindow(const char *title, bool dithered) :
        TWindowInit(TWindow::initFrame),
        TWindow(TRect(4, 3, 44, 17), title, wnNoNumber)
    {
        options |= ofTileable;
        TRect inner(1, 1, 39, 13);
        TColorRangeGraphicView *view = new TColorRangeGraphicView(inner, dithered);
        TColorInfoOverlay *overlay = new TColorInfoOverlay(TRect(6, size.y - 1, size.x - 2, size.y));
        view->infoOverlay = overlay;
        insertClientBackground(this);
        insert(view);
        insert(overlay);
    }
};

class TImageGraphicWindow : public TWindow
{
public:
    TImageGraphicView *imageView;

    TImageGraphicWindow(const char *title, const TLoadedImage &image) :
        TWindowInit(TWindow::initFrame),
        TWindow(TRect(6, 4, 66, 24), title, wnNoNumber),
        imageView(0)
    {
        options |= ofTileable;
        TRect inner(1, 1, size.x - 1, size.y - 1);
        insertClientBackground(this);
        imageView = new TImageGraphicView(inner, image);
        insert(imageView);
    }

    TImageDitherMode ditherMode() const
    {
        return imageView != 0 ? imageView->ditherMode : imageEfficientDither;
    }

    void setDitherMode(TImageDitherMode mode)
    {
        if (imageView == 0 || imageView->ditherMode == mode)
            return;
        imageView->ditherMode = mode;
        updateImageMenuLabels(mode);
        TGraphicRuntime::invalidate(imageView);
    }

    virtual void setState(ushort aState, Boolean enable)
    {
        TWindow::setState(aState, enable);
        if ((aState & sfSelected) != 0)
        {
            setImageCommandState(enable);
            if (enable)
                updateImageMenuLabels(ditherMode());
        }
    }
};

class TFixedGraphicWindow : public TWindow
{
public:
    TFixedGraphicWindow() :
        TWindowInit(TWindow::initFrame),
        TWindow(TRect(4, 3, 44, 17), "Fixed Sixel", wnNoNumber)
    {
        options |= ofTileable;
        TRect graphic = fixedGraphicBounds();
        insertClientBackground(this);
        insert(new TDiagnosticGraphicView(graphic, TGraphicView::fixedGraphic));
        insert(new TStaticText(TRect(graphic.b.x + 2, graphic.a.y,
                                     size.x - 2, min(size.y - 1, graphic.a.y + 5)),
                               "<-- fixed sixel image\n"
                               "<-- 160x96 raster\n"
                               "<-- text label area"));
        insert(new TStaticText(TRect(graphic.a.x, graphic.b.y + 1,
                                     size.x - 2, min(size.y - 1, graphic.b.y + 5)),
                               "^ image above\n"
                               "^ normal TStaticText"));
    }
};

static void updateLiveViews(TView *view)
{
    if (view == 0 || !(view->state & sfVisible))
        return;

    TClockGraphicView *clock = dynamic_cast<TClockGraphicView *>(view);
    if (clock != 0)
        clock->update();

    TMandelbrotWindow *mandelbrot = dynamic_cast<TMandelbrotWindow *>(view);
    if (mandelbrot != 0)
        mandelbrot->update();

    TGroup *group = dynamic_cast<TGroup *>(view);
    if (group != 0)
        for (TView *child = group->first(); child != 0; child = child->nextView())
            updateLiveViews(child);
}

class TSixelDemo : public TApplication
{
public:
    TSixelDemo() :
        TProgInit(initStatusLine, initMenuBar, initDeskTop),
        TApplication()
    {
        setImageCommandState(False);
        updateImageMenuLabels(imageEfficientDither);
    }

    virtual void idle()
    {
        TProgram::idle();
        updateLiveViews(this);
    }

    static TStatusLine *initStatusLine(TRect r)
    {
        r.a.y = r.b.y - 1;
        return new TStatusLine(r,
            *new TStatusDef(0, 0xFFFF) +
            *new TStatusItem("~Alt-X~ Exit", kbAltX, cmQuit) +
            *new TStatusItem("~F10~ Menu", kbF10, cmMenu)
        );
    }

    static TMenuBar *initMenuBar(TRect r)
    {
        r.b.y = r.a.y + 1;
        TSubMenu *file = new TSubMenu("~F~ile", kbAltF);
        *file +
            *new TMenuItem("~O~pen Image...", cmOpenImage, kbNoKey) +
            newLine() +
            *new TMenuItem("E~x~it", cmQuit, kbAltX);

        TSubMenu *image = new TSubMenu("~I~mage", kbAltI);
        imageMenuItem = image;
        imageNoDitherItem = new TMenuItem("( ) ~N~o dithering", cmImageNoDither, kbNoKey);
        imageEfficientDitherItem = new TMenuItem("(*) ~E~fficient dithering", cmImageEfficientDither, kbNoKey);
        imageAdaptiveDitherItem = new TMenuItem("( ) ~A~daptive dithering", cmImageAdaptiveDither, kbNoKey);
        imageNormalDitherItem = new TMenuItem("( ) N~o~rmal dithering", cmImageNormalDither, kbNoKey);
        imageTrueColorItem = new TMenuItem("( ) ~T~rue color palette", cmImageTrueColor, kbNoKey);
        *image +
            *imageNoDitherItem +
            *imageEfficientDitherItem +
            *imageAdaptiveDitherItem +
            *imageNormalDitherItem +
            newLine() +
            *imageTrueColorItem;
        image->disabled = True;

        TSubMenu *graphics = new TSubMenu("~G~raphics", kbAltG);
        *graphics +
            *new TMenuItem("~F~ixed diagnostic", cmFixedImage, kbNoKey) +
            *new TMenuItem("F~i~ll diagnostic", cmFillImage, kbNoKey) +
            *new TMenuItem("~M~andelbrot", cmMandelbrotImage, kbNoKey) +
            *new TMenuItem("Formula ~p~lotter", cmFormulaPlot, kbNoKey) +
            *new TMenuItem("~R~GB ranges", cmRgbRange, kbNoKey) +
            *new TMenuItem("~D~ithered RGB ranges", cmDitherRange, kbNoKey);

        TSubMenu *worldClock = new TSubMenu("~W~orld Clock", kbAltW);
        *worldClock +
            *new TMenuItem("Los ~A~ngeles", cmClockLosAngeles, kbNoKey) +
            *new TMenuItem("~C~hicago", cmClockChicago, kbNoKey) +
            *new TMenuItem("~N~ew York", cmClockNewYork, kbNoKey) +
            *new TMenuItem("~L~ondon", cmClockLondon, kbNoKey) +
            *new TMenuItem("~B~erlin", cmClockBerlin, kbNoKey) +
            *new TMenuItem("~M~oscow", cmClockMoscow, kbNoKey) +
            *new TMenuItem("~D~ubai", cmClockDubai, kbNoKey) +
            *new TMenuItem("M~u~mbai", cmClockMumbai, kbNoKey) +
            *new TMenuItem("Bei~j~ing", cmClockBeijing, kbNoKey) +
            *new TMenuItem("~T~okyo", cmClockTokyo, kbNoKey) +
            *new TMenuItem("~S~ydney", cmClockSydney, kbNoKey);

        return new TMenuBar(r, *file + *image + *graphics + *worldClock);
    }

    void openImage()
    {
        char fileName[MAXPATH];
        strcpy(fileName, "*.*");
        if (executeDialog(new TFileDialog("*.*", "Open Image",
                                          "~N~ame", fdOpenButton, 101),
                          fileName) == cmCancel)
            return;

        TLoadedImage image;
        if (!loadImageFile(fileName, image))
        {
            messageBox(mfError | mfOKButton,
                       "Could not open image '%s'.\n"
                       "Supported directly: BMP.\n"
                       "Other formats need sips, magick, or convert.",
                       fileName);
            return;
        }

        const char *base = strrchr(fileName, '/');
        base = base != 0 ? base + 1 : fileName;
        char title[80];
        snprintf(title, sizeof(title), "Image: %.64s", base);
        deskTop->insert(new TImageGraphicWindow(title, image));
    }

    TImageGraphicWindow *activeImageWindow() const
    {
        return deskTop != 0 ? dynamic_cast<TImageGraphicWindow *>(deskTop->current) : 0;
    }

    void setActiveImageDitherMode(TImageDitherMode mode)
    {
        TImageGraphicWindow *window = activeImageWindow();
        if (window != 0)
            window->setDitherMode(mode);
    }

    bool openWorldClock(ushort command)
    {
        for (const TWorldClockCity &city : worldClockCities)
            if (city.command == command)
            {
                deskTop->insert(new TClockGraphicWindow(city.name, city.standardOffsetMinutes,
                                                        city.dstRule,
                                                        city.latitudeDeg, city.longitudeDeg));
                return true;
            }
        return false;
    }

    virtual void handleEvent(TEvent &event)
    {
        TApplication::handleEvent(event);
        if (event.what != evCommand)
            return;

        if (openWorldClock(event.message.command))
        {
            clearEvent(event);
            return;
        }

        TRect inner(1, 1, 39, 13);
        switch (event.message.command)
        {
        case cmOpenImage:
            openImage();
            break;
        case cmImageNoDither:
            setActiveImageDitherMode(imageNoDither);
            break;
        case cmImageEfficientDither:
            setActiveImageDitherMode(imageEfficientDither);
            break;
        case cmImageAdaptiveDither:
            setActiveImageDitherMode(imageAdaptiveDither);
            break;
        case cmImageNormalDither:
            setActiveImageDitherMode(imageNormalDither);
            break;
        case cmImageTrueColor:
            setActiveImageDitherMode(imageTrueColor);
            break;
        case cmFixedImage:
            deskTop->insert(new TFixedGraphicWindow());
            break;
        case cmFillImage:
            deskTop->insert(new TGraphicWindow("Fill Sixel", new TDiagnosticGraphicView(inner, TGraphicView::fillGraphic)));
            break;
        case cmMandelbrotImage:
            deskTop->insert(new TMandelbrotWindow());
            break;
        case cmFormulaPlot:
            deskTop->insert(createFormulaPlotWindow());
            break;
        case cmRgbRange:
            deskTop->insert(new TColorGraphicWindow("RGB Ranges", false));
            break;
        case cmDitherRange:
            deskTop->insert(new TColorGraphicWindow("Dithered RGB", true));
            break;
        default:
            return;
        }
        clearEvent(event);
    }
};

int main()
{
    TSixelDemo app;
    app.run();
    return 0;
}
