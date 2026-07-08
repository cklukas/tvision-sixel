/* ------------------------------------------------------------------------*/
/*                                                                         */
/*   GRAPHICS.H                                                            */
/*                                                                         */
/*   Defines callback-driven graphics views for terminal raster overlays.   */
/*                                                                         */
/*   Sixel graphics support additions and modifications:                    */
/*   Copyright (c) 2026 by Christian Klukas.                                */
/*   Licensed under the MIT License.                                        */
/*                                                                         */
/* ------------------------------------------------------------------------*/

#if !defined( __GRAPHICS_H )
#define __GRAPHICS_H

#if defined( Uses_TGraphicCanvas ) && !defined( __TGraphicCanvas )
#define __TGraphicCanvas

struct TGraphicColor
{
    uchar r, g, b, a;

    TGraphicColor() noexcept : r(0), g(0), b(0), a(255) {}
    TGraphicColor(uchar ar, uchar ag, uchar ab, uchar aa = 255) noexcept :
        r(ar), g(ag), b(ab), a(aa) {}
};

// How the sixel encoder reduces a full-colour raster to the palette:
//   graphicDitherNearest  each pixel maps to its nearest palette entry
//                         (flat colour bands on continuous-tone images).
//   graphicDitherBayer    an ordered 8x8 Bayer matrix perturbs each pixel
//                         before quantisation, trading banding for a stable
//                         stipple that reads as extra shades.
// The library default is Nearest; the CWorks suite pushes its own default
// (Bayer) through TGraphicRuntime::setTemporaryProfile.
enum TGraphicDitherMode
{
    graphicDitherNearest,
    graphicDitherBayer
};

struct TGraphicProfile
{
    Boolean enabled;
    short cellWidth;
    short cellHeight;
    short fillWidth;
    short fillHeight;
    short maxColors;
    TGraphicDitherMode dither;

    TGraphicProfile() noexcept :
        enabled(False),
        cellWidth(0),
        cellHeight(0),
        fillWidth(0),
        fillHeight(0),
        maxColors(256),
        dither(graphicDitherNearest)
    {
    }
};

class TGraphicCanvas
{
    uint32_t _FAR *pixels;

    static uint32_t pack(TGraphicColor) noexcept;

public:

    TPoint size;
    TRect cells;
    TPoint cellSize;
    TPoint fillSize;

    TGraphicCanvas(TPoint aSize, uint32_t _FAR *aPixels) noexcept;
    TGraphicCanvas(TPoint aSize, uint32_t _FAR *aPixels,
                   TRect aCells, TPoint aCellSize, TPoint aFillSize) noexcept;

    uint32_t _FAR *data() noexcept { return pixels; }
    const uint32_t _FAR *data() const noexcept { return pixels; }

    void clear(TGraphicColor color = TGraphicColor()) noexcept;
    void setPixel(int x, int y, TGraphicColor color) noexcept;
    void line(int x0, int y0, int x1, int y1, TGraphicColor color) noexcept;
    void rect(int x, int y, int w, int h, TGraphicColor color) noexcept;
    void circle(int cx, int cy, int r, TGraphicColor color) noexcept;
};

#endif // Uses_TGraphicCanvas

#if defined( Uses_TGraphicView ) && !defined( __TGraphicView )
#define __TGraphicView

class TGraphicView : public TView
{
public:

    enum SizingMode
    {
        fixedGraphic,
        fillGraphic
    };

    TGraphicView(const TRect &bounds, SizingMode mode = fillGraphic) noexcept;

    virtual void draw();
    virtual void paintGraphic(TGraphicCanvas &canvas);
    virtual int graphicMaxColors(const TGraphicProfile &profile) const noexcept;
    virtual TPalette& getPalette() const;

    TPoint graphicSize() const noexcept;
    void setFixedGraphicSize(TPoint size) noexcept;

    SizingMode sizingMode;
    TPoint fixedSize;
};

#endif // Uses_TGraphicView

#if defined( Uses_TGraphicRuntime ) && !defined( __TGraphicRuntime )
#define __TGraphicRuntime

class TGraphicView;

class TGraphicRuntime
{
public:
    static Boolean isConfigured() noexcept;
    static TGraphicProfile getProfile() noexcept;
    // The terminal's live cell size in pixels (character width x height),
    // as reported by the display this instant. {0, 0} when the terminal
    // does not report a pixel size. Lets a caller size sixel graphics to
    // the current font instead of a saved calibration.
    static TPoint detectedCellSize() noexcept;
    static void invalidate() noexcept;
    static void invalidate(TGraphicView *view) noexcept;
    static void setTemporaryProfile(const TGraphicProfile &profile) noexcept;
    static void clearTemporaryProfile() noexcept;
};

#endif // Uses_TGraphicRuntime

#endif // __GRAPHICS_H
