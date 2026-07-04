/*------------------------------------------------------------*/
/* filename -       tgraphic.cpp                              */
/*                                                            */
/* function(s)                                                */
/*                  TGraphicCanvas and TGraphicView members   */
/*                                                            */
/* Sixel graphics support additions and modifications:         */
/* Copyright (c) 2026 by Christian Klukas                     */
/* Licensed under the MIT License.                            */
/*------------------------------------------------------------*/

#define Uses_TGraphicView
#define Uses_TGraphicRuntime
#define Uses_TDrawBuffer
#define Uses_TPalette
#define Uses_TRect
#include <tvision/tv.h>

#include <algorithm>
#include <stdlib.h>

using std::swap;

#define cpGraphicView "\x01"

uint32_t TGraphicCanvas::pack(TGraphicColor color) noexcept
{
    return (uint32_t(color.a) << 24) |
           (uint32_t(color.r) << 16) |
           (uint32_t(color.g) << 8) |
            uint32_t(color.b);
}

TGraphicCanvas::TGraphicCanvas(TPoint aSize, uint32_t _FAR *aPixels) noexcept :
    pixels(aPixels),
    size(aSize),
    cells(0, 0, 0, 0),
    cellSize{0, 0},
    fillSize{0, 0}
{
}

TGraphicCanvas::TGraphicCanvas( TPoint aSize, uint32_t _FAR *aPixels,
                                TRect aCells, TPoint aCellSize,
                                TPoint aFillSize ) noexcept :
    pixels(aPixels),
    size(aSize),
    cells(aCells),
    cellSize(aCellSize),
    fillSize(aFillSize)
{
}

void TGraphicCanvas::clear(TGraphicColor color) noexcept
{
    if (pixels != 0 && size.x > 0 && size.y > 0)
    {
        uint32_t value = pack(color);
        for (int i = 0; i < size.x*size.y; ++i)
            pixels[i] = value;
    }
}

void TGraphicCanvas::setPixel(int x, int y, TGraphicColor color) noexcept
{
    if (pixels != 0 && 0 <= x && x < size.x && 0 <= y && y < size.y)
        pixels[y*size.x + x] = pack(color);
}

void TGraphicCanvas::line(int x0, int y0, int x1, int y1, TGraphicColor color) noexcept
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;)
    {
        setPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2*err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void TGraphicCanvas::rect(int x, int y, int w, int h, TGraphicColor color) noexcept
{
    if (w <= 0 || h <= 0)
        return;
    line(x, y, x + w - 1, y, color);
    line(x, y, x, y + h - 1, color);
    line(x + w - 1, y, x + w - 1, y + h - 1, color);
    line(x, y + h - 1, x + w - 1, y + h - 1, color);
}

void TGraphicCanvas::circle(int cx, int cy, int r, TGraphicColor color) noexcept
{
    if (r <= 0)
        return;
    int x = r;
    int y = 0;
    int err = 0;
    while (x >= y)
    {
        setPixel(cx + x, cy + y, color);
        setPixel(cx + y, cy + x, color);
        setPixel(cx - y, cy + x, color);
        setPixel(cx - x, cy + y, color);
        setPixel(cx - x, cy - y, color);
        setPixel(cx - y, cy - x, color);
        setPixel(cx + y, cy - x, color);
        setPixel(cx + x, cy - y, color);

        ++y;
        if (err <= 0)
            err += 2*y + 1;
        if (err > 0)
        {
            --x;
            err -= 2*x + 1;
        }
    }
}

TGraphicView::TGraphicView(const TRect &bounds, SizingMode mode) noexcept :
    TView(bounds),
    sizingMode(mode),
    fixedSize{0, 0}
{
    eventMask = 0;
}

void TGraphicView::draw()
{
    TDrawBuffer b;
    TAttrPair color = getColor(1);

    for (int y = 0; y < size.y; ++y)
    {
        b.moveChar(0, ' ', color, size.x);
        writeLine(0, y, size.x, 1, b);
    }
}

void TGraphicView::paintGraphic(TGraphicCanvas &canvas)
{
    canvas.clear(TGraphicColor(0, 0, 0));
}

int TGraphicView::graphicMaxColors(const TGraphicProfile &profile) const noexcept
{
    return profile.maxColors;
}

TPalette &TGraphicView::getPalette() const
{
    static TPalette palette(cpGraphicView, sizeof(cpGraphicView) - 1);
    return palette;
}

TPoint TGraphicView::graphicSize() const noexcept
{
    return fixedSize;
}

void TGraphicView::setFixedGraphicSize(TPoint aSize) noexcept
{
    fixedSize = aSize;
    sizingMode = fixedGraphic;
}
