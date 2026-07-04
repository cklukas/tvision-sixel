/*
 * Sixel graphics support additions and modifications:
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#define Uses_TApplication
#define Uses_TGraphicView
#define Uses_TRect
#define Uses_TScreenCell
#include <tvision/tv.h>

#include <internal/graphics.h>
#include <internal/platform.h>

#include <algorithm>
#include <map>
#include <string.h>
#include <utility>
#include <vector>

extern TPoint shadowSize;

namespace tvision
{

namespace
{

struct PaintItem
{
    TView *view;
    TRect rect;
    std::vector<TRect> shadows;
};

struct RegionPiece
{
    TRect rect;
    bool shadowed;
};

static bool empty(const TRect &r) noexcept
{
    return r.a.x >= r.b.x || r.a.y >= r.b.y;
}

static TRect globalBounds(TView *view) noexcept
{
    TPoint origin = view->makeGlobal({0, 0});
    return TRect(origin, origin + view->size);
}

static TRect visibleBounds(TView *view) noexcept
{
    TRect r = globalBounds(view);
    for (TView *v = view->owner; v != 0; v = v->owner)
        r.intersect(globalBounds(v));
    return r;
}

static std::vector<TRect> shadowBounds(TView *view)
{
    std::vector<TRect> shadows;
    if (!(view->state & sfShadow))
        return shadows;
    TRect r = globalBounds(view);
    if (shadowSize.x > 0)
    {
        TRect right(r.b.x, r.a.y + shadowSize.y, r.b.x + shadowSize.x, r.b.y);
        if (!empty(right))
            shadows.push_back(right);
    }
    if (shadowSize.y > 0)
    {
        TRect bottom(r.a.x + shadowSize.x, r.b.y, r.b.x + shadowSize.x, r.b.y + shadowSize.y);
        if (!empty(bottom))
            shadows.push_back(bottom);
    }
    return shadows;
}

static bool isAncestorOf(TView *ancestor, TView *view) noexcept
{
    for (TView *v = view ? view->owner : 0; v != 0; v = v->owner)
        if (v == ancestor)
            return true;
    return false;
}

static void appendItems(TView *view, std::vector<PaintItem> &items) noexcept
{
    if (!view || !(view->state & sfVisible))
        return;

    items.push_back({view, visibleBounds(view), shadowBounds(view)});

    TGroup *group = static_cast<TGroup *>(nullptr);
    if (dynamic_cast<TGraphicView *>(view) == 0)
        group = dynamic_cast<TGroup *>(view);
    if (group != 0)
        for (TView *child = group->first(); child != 0; child = child->nextView())
            appendItems(child, items);
}

static void appendOutsideAndOverlap( const RegionPiece &piece, TRect overlap,
                                     std::vector<RegionPiece> &next )
{
    TRect r = piece.rect;
    if (r.a.y < overlap.a.y)
        next.push_back({TRect(r.a.x, r.a.y, r.b.x, overlap.a.y), piece.shadowed});
    if (overlap.b.y < r.b.y)
        next.push_back({TRect(r.a.x, overlap.b.y, r.b.x, r.b.y), piece.shadowed});
    if (r.a.x < overlap.a.x)
        next.push_back({TRect(r.a.x, overlap.a.y, overlap.a.x, overlap.b.y), piece.shadowed});
    if (overlap.b.x < r.b.x)
        next.push_back({TRect(overlap.b.x, overlap.a.y, r.b.x, overlap.b.y), piece.shadowed});
}

static void subtractRect(std::vector<RegionPiece> &regions, TRect cut)
{
    if (empty(cut))
        return;

    std::vector<RegionPiece> next;
    for (RegionPiece piece : regions)
    {
        TRect overlap = piece.rect;
        overlap.intersect(cut);
        if (empty(overlap))
        {
            next.push_back(piece);
            continue;
        }
        appendOutsideAndOverlap(piece, overlap, next);
    }
    regions.swap(next);
}

static void applyShadow(std::vector<RegionPiece> &regions, TRect shadow)
{
    if (empty(shadow))
        return;

    std::vector<RegionPiece> next;
    for (RegionPiece piece : regions)
    {
        TRect overlap = piece.rect;
        overlap.intersect(shadow);
        if (empty(overlap))
        {
            next.push_back(piece);
            continue;
        }
        appendOutsideAndOverlap(piece, overlap, next);
        next.push_back({overlap, true});
    }
    regions.swap(next);
}

static TPoint graphicPixelSize(TGraphicView &view, const TGraphicProfile &profile) noexcept
{
    if (view.sizingMode == TGraphicView::fixedGraphic && view.fixedSize.x > 0 && view.fixedSize.y > 0)
        return view.fixedSize;
    if (view.size.x <= 0 || view.size.y <= 0)
        return {0, 0};
    return {
        short((view.size.x - 1)*profile.cellWidth + profile.fillWidth),
        short((view.size.y - 1)*profile.cellHeight + profile.fillHeight),
    };
}

static TPoint graphicCellFootprint(TGraphicView &view, const TGraphicProfile &profile) noexcept
{
    if (view.size.x <= 0 || view.size.y <= 0)
        return {0, 0};
    return {
        short((view.size.x - 1)*profile.cellWidth + profile.fillWidth),
        short((view.size.y - 1)*profile.cellHeight + profile.fillHeight),
    };
}

static TRect pixelRectForCells(const TRect &cells, const TGraphicProfile &profile) noexcept
{
    if (empty(cells))
        return TRect();
    return TRect(
        cells.a.x*profile.cellWidth,
        cells.a.y*profile.cellHeight,
        (cells.b.x - 1)*profile.cellWidth + profile.fillWidth,
        (cells.b.y - 1)*profile.cellHeight + profile.fillHeight
    );
}

static void copyPixels( const std::vector<uint32_t> &src, TPoint srcSize,
                        const TRect &from, std::vector<uint32_t> &dst )
{
    TPoint dstSize = from.b - from.a;
    dst.resize(dstSize.x*dstSize.y);
    for (int y = 0; y < dstSize.y; ++y)
        memcpy(&dst[y*dstSize.x],
               &src[(from.a.y + y)*srcSize.x + from.a.x],
               dstSize.x*sizeof(uint32_t));
}

static void applyGraphicShadow(std::vector<uint32_t> &pixels) noexcept
{
    // Raster equivalent of Turbo Vision's text shadow: preserve shape while
    // darkening every visible pixel under a shadow rectangle.
    for (uint32_t &pixel : pixels)
    {
        uint32_t a = pixel & 0xFF000000;
        uint32_t r = (pixel >> 16) & 0xFF;
        uint32_t g = (pixel >> 8) & 0xFF;
        uint32_t b = pixel & 0xFF;
        r = (r*42)/100;
        g = (g*42)/100;
        b = (b*42)/100;
        pixel = a | (r << 16) | (g << 8) | b;
    }
}

static bool containsView(const std::vector<TGraphicView *> &views, TGraphicView *view) noexcept
{
    for (TGraphicView *candidate : views)
        if (candidate == view)
            return true;
    return false;
}

static bool intersectsAny(const std::vector<TRect> &rects, TRect rect) noexcept
{
    for (TRect candidate : rects)
    {
        candidate.intersect(rect);
        if (!empty(candidate))
            return true;
    }
    return false;
}

static bool intersectsForeignDamage(const GraphicOverlayState &state,
                                    TGraphicView *view, TRect rect) noexcept
{
    for (size_t i = 0; i < state.damage.size(); ++i)
    {
        TGraphicView *damageView =
            i < state.damageViews.size() ? state.damageViews[i] : nullptr;
        if (damageView == view)
            continue;
        TRect candidate = state.damage[i];
        candidate.intersect(rect);
        if (!empty(candidate))
            return true;
    }
    return false;
}

static bool intersectsChangedShadowDamage( const GraphicOverlayState &state,
                                           TGraphicView *view, TRect rect,
                                           bool shadowed ) noexcept
{
    for (size_t i = 0; i < state.damage.size(); ++i)
    {
        TGraphicView *damageView =
            i < state.damageViews.size() ? state.damageViews[i] : nullptr;
        bool damageShadowed =
            i < state.damageShadowed.size() ? state.damageShadowed[i] : false;
        if (damageView != view || damageShadowed == shadowed)
            continue;
        TRect candidate = state.damage[i];
        candidate.intersect(rect);
        if (!empty(candidate))
            return true;
    }
    return false;
}

static uint32_t rgbFromBios(int color) noexcept
{
    static const uint32_t table[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
        0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
        0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
    };
    return table[color & 0x0F];
}

static uint32_t rgbFromColor(TColorDesired color, bool foreground) noexcept
{
#ifdef __BORLANDC__
    (void) foreground;
    return rgbFromBios(color);
#else
    if (color.isRGB())
        return color.asRGB();
    if (color.isXTerm())
    {
        uint8_t idx = color.asXTerm();
        if (idx >= 16)
            return XTerm256toRGB(idx);
        return rgbFromBios(XTerm16toBIOS(idx));
    }
    return rgbFromBios(color.toBIOS(foreground));
#endif
}

static uint32_t graphicBackgroundPixel(TGraphicView &view) noexcept
{
    TColorAttr attr = view.getColor(1)[0];
    return 0xFF000000 | rgbFromColor(getBack(attr), false);
}

static uint32_t blendOver(uint32_t src, uint32_t dst) noexcept
{
    uint32_t alpha = (src >> 24) & 0xFF;
    if (alpha == 255)
        return src;
    if (alpha == 0)
        return dst;

    uint32_t inv = 255 - alpha;
    uint32_t sr = (src >> 16) & 0xFF;
    uint32_t sg = (src >> 8) & 0xFF;
    uint32_t sb = src & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t db = dst & 0xFF;

    uint32_t r = (sr*alpha + dr*inv + 127)/255;
    uint32_t g = (sg*alpha + dg*inv + 127)/255;
    uint32_t b = (sb*alpha + db*inv + 127)/255;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void resolveAlpha(std::vector<uint32_t> &pixels, uint32_t background) noexcept
{
    for (uint32_t &pixel : pixels)
        if (((pixel >> 24) & 0xFF) != 255)
            pixel = blendOver(pixel, background);
}

static void subtractCellRect(std::vector<TRect> &regions, TRect cut)
{
    if (empty(cut))
        return;

    std::vector<TRect> next;
    for (TRect r : regions)
    {
        TRect overlap = r;
        overlap.intersect(cut);
        if (empty(overlap))
        {
            next.push_back(r);
            continue;
        }

        if (r.a.y < overlap.a.y)
            next.push_back(TRect(r.a.x, r.a.y, r.b.x, overlap.a.y));
        if (overlap.b.y < r.b.y)
            next.push_back(TRect(r.a.x, overlap.b.y, r.b.x, r.b.y));
        if (r.a.x < overlap.a.x)
            next.push_back(TRect(r.a.x, overlap.a.y, overlap.a.x, overlap.b.y));
        if (overlap.b.x < r.b.x)
            next.push_back(TRect(overlap.b.x, overlap.a.y, r.b.x, overlap.b.y));
    }
    regions.swap(next);
}

} // namespace

void GraphicOverlayState::touch(TGraphicView *view) noexcept
{
    touched = true;
    if (view == 0)
    {
        allTouched = true;
        touchedViews.clear();
    }
    else if (!allTouched)
    {
        for (TGraphicView *candidate : touchedViews)
            if (candidate == view)
                return;
        touchedViews.push_back(view);
    }
}

void GraphicOverlayState::finishFrame(const GraphicFrame &frame) noexcept
{
    damage.clear();
    damageViews.clear();
    damageShadowed.clear();
    for (const GraphicFragment &fragment : frame.fragments)
    {
        damage.push_back(fragment.globalCells);
        damageViews.push_back(fragment.view);
        damageShadowed.push_back(fragment.shadowed);
    }
    clearTouches();
}

void GraphicOverlayState::clearTouches() noexcept
{
    touched = false;
    allTouched = false;
    touchedViews.clear();
}

GraphicFrame collectGraphicFrame() noexcept
{
    GraphicFrame frame;
    TProgram *app = TProgram::application;
    if (app == 0)
        return frame;

    std::vector<PaintItem> items;
    for (TView *child = app->first(); child != 0; child = child->nextView())
        appendItems(child, items);

    for (size_t i = 0; i < items.size(); ++i)
    {
        TGraphicView *graphic = dynamic_cast<TGraphicView *>(items[i].view);
        if (graphic == 0 || empty(items[i].rect))
            continue;

        std::vector<RegionPiece> regions {{items[i].rect, false}};
        for (size_t j = 0; j < i; ++j)
        {
            if (isAncestorOf(items[j].view, graphic))
                continue;
            subtractRect(regions, items[j].rect);
            for (TRect shadow : items[j].shadows)
                applyShadow(regions, shadow);
            if (regions.empty())
                break;
        }

        TPoint globalOrigin = globalBounds(graphic).a;
        for (const RegionPiece &piece : regions)
        {
            TRect r = piece.rect;
            if (empty(r))
                continue;
            TRect local = r;
            local.move(-globalOrigin.x, -globalOrigin.y);
            frame.fragments.push_back({graphic, local, r, piece.shadowed});
        }
    }
    return frame;
}

std::vector<TRect> eraseStaleGraphicOverlay( DisplayAdapter &display, const GraphicOverlayState &state,
                                             const TScreenCell *screenBuffer,
                                             TPoint screenSize,
                                             const GraphicFrame &currentGraphics ) noexcept
{
    TRect screen(0, 0, screenSize.x, screenSize.y);
    std::vector<TRect> stale = state.damage;
    for (const GraphicFragment &fragment : currentGraphics.fragments)
        subtractCellRect(stale, fragment.globalCells);

    for (TRect rect : stale)
    {
        rect.intersect(screen);
        if (empty(rect))
            continue;

        if (display.supportsGraphics())
        {
            TGraphicProfile profile = display.getGraphicProfile();
            if (profile.enabled &&
                profile.cellWidth > 0 && profile.cellHeight > 0 &&
                profile.fillWidth > 0 && profile.fillHeight > 0)
            {
                int pixelWidth = (rect.b.x - rect.a.x - 1)*profile.cellWidth + profile.fillWidth;
                int pixelHeight = (rect.b.y - rect.a.y - 1)*profile.cellHeight + profile.fillHeight;
                std::vector<uint32_t> pixels(pixelWidth*pixelHeight, 0xFF000000);
                for (int cy = rect.a.y; cy < rect.b.y; ++cy)
                {
                    int py0 = (cy - rect.a.y)*profile.cellHeight;
                    int py1 = min(pixelHeight, py0 + (cy + 1 == rect.b.y ? profile.fillHeight : profile.cellHeight));
                    for (int cx = rect.a.x; cx < rect.b.x; ++cx)
                    {
                        int px0 = (cx - rect.a.x)*profile.cellWidth;
                        int px1 = min(pixelWidth, px0 + (cx + 1 == rect.b.x ? profile.fillWidth : profile.cellWidth));
                        uint32_t argb = 0xFF000000 |
                            rgbFromColor(getBack(screenBuffer[cy*screenSize.x + cx].attr), false);
                        for (int py = py0; py < py1; ++py)
                            for (int px = px0; px < px1; ++px)
                                pixels[py*pixelWidth + px] = argb;
                    }
                }
                display.writeGraphicImage(rect.a, pixels.data(), {pixelWidth, pixelHeight}, profile.maxColors);
            }
        }
    }
    return stale;
}

GraphicFrame drawGraphicOverlay(DisplayAdapter &display, const GraphicOverlayState &state,
                                const std::vector<TRect> *dirtyCells) noexcept
{
    struct RenderedGraphic
    {
        TPoint size;
        std::vector<uint32_t> pixels;
    };

    GraphicFrame emptyFrame;
    if (!display.supportsGraphics())
        return emptyFrame;

    TGraphicProfile profile = display.getGraphicProfile();
    if (!profile.enabled)
        return emptyFrame;

    GraphicFrame frame = collectGraphicFrame();
    std::map<TGraphicView *, RenderedGraphic> rendered;
    const std::vector<TGraphicView *> *dirtyViews =
        !state.allTouched && state.touched ? &state.touchedViews : nullptr;
    const std::vector<TRect> *effectiveDirtyCells =
        state.allTouched ? nullptr : dirtyCells;

    for (const GraphicFragment &fragment : frame.fragments)
    {
        TGraphicView &view = *fragment.view;
        bool shouldDraw = state.allTouched;
        if (!shouldDraw && dirtyViews != 0 && containsView(*dirtyViews, &view))
            shouldDraw = true;
        if (!shouldDraw && effectiveDirtyCells != 0 &&
            intersectsAny(*effectiveDirtyCells, fragment.globalCells))
            shouldDraw = true;
        if (!shouldDraw && intersectsForeignDamage(state, &view, fragment.globalCells))
            shouldDraw = true;
        if (!shouldDraw && intersectsChangedShadowDamage(state, &view, fragment.globalCells, fragment.shadowed))
            shouldDraw = true;
        if (!shouldDraw)
            continue;

        TPoint contentSize = graphicPixelSize(view, profile);
        TPoint outputSize = graphicCellFootprint(view, profile);
        if (contentSize.x <= 0 || contentSize.y <= 0 ||
            outputSize.x <= 0 || outputSize.y <= 0)
            continue;

        auto found = rendered.find(&view);
        if (found == rendered.end())
        {
            RenderedGraphic graphic;
            graphic.size = outputSize;
            uint32_t background = graphicBackgroundPixel(view);
            graphic.pixels.assign(outputSize.x*outputSize.y, background);

            if (contentSize.x == outputSize.x && contentSize.y == outputSize.y)
            {
                TGraphicCanvas canvas(outputSize, graphic.pixels.data(),
                                      TRect(0, 0, view.size.x, view.size.y),
                                      {profile.cellWidth, profile.cellHeight},
                                      {profile.fillWidth, profile.fillHeight});
                view.paintGraphic(canvas);
            }
            else
            {
                std::vector<uint32_t> content(contentSize.x*contentSize.y);
                TGraphicCanvas canvas(contentSize, content.data(),
                                      TRect(0, 0, view.size.x, view.size.y),
                                      {profile.cellWidth, profile.cellHeight},
                                      {profile.fillWidth, profile.fillHeight});
                view.paintGraphic(canvas);

                int copyWidth = min(contentSize.x, outputSize.x);
                int copyHeight = min(contentSize.y, outputSize.y);
                for (int y = 0; y < copyHeight; ++y)
                    memcpy(&graphic.pixels[y*outputSize.x],
                           &content[y*contentSize.x],
                           copyWidth*sizeof(uint32_t));
            }
            resolveAlpha(graphic.pixels, background);

            found = rendered.insert({&view, std::move(graphic)}).first;
        }

        TRect pixels = pixelRectForCells(fragment.localCells, profile);
        TPoint fullSize = found->second.size;
        pixels.intersect(TRect(0, 0, fullSize.x, fullSize.y));
        if (empty(pixels))
            continue;

        TPoint cropSize = pixels.b - pixels.a;
        int maxColors = view.graphicMaxColors(profile);
        if (!fragment.shadowed &&
            pixels.a.x == 0 && pixels.a.y == 0 &&
            pixels.b.x == fullSize.x && pixels.b.y == fullSize.y)
            display.writeGraphicImage(fragment.globalCells.a, found->second.pixels.data(),
                                      cropSize, maxColors);
        else
        {
            std::vector<uint32_t> cropped;
            copyPixels(found->second.pixels, fullSize, pixels, cropped);
            if (fragment.shadowed)
                applyGraphicShadow(cropped);
            display.writeGraphicImage(fragment.globalCells.a, cropped.data(),
                                      cropSize, maxColors);
        }
    }
    display.flush();
    return frame;
}

} // namespace tvision
