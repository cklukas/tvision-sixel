/*
 * Sixel graphics support additions and modifications:
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#ifndef TVISION_INTERNAL_GRAPHICS_H
#define TVISION_INTERNAL_GRAPHICS_H

#define Uses_TGraphicView
#define Uses_TGraphicRuntime
#define Uses_TScreenCell
#include <tvision/tv.h>

#include <string>
#include <vector>

class TGraphicView;

namespace tvision
{

class DisplayAdapter;

struct GraphicFragment
{
    TGraphicView *view {nullptr};
    TRect localCells;
    TRect globalCells;
    bool shadowed {false};
};

struct GraphicFrame
{
    std::vector<GraphicFragment> fragments;
};

struct GraphicOverlayState
{
    std::vector<TRect> damage;
    std::vector<TGraphicView *> damageViews;
    std::vector<bool> damageShadowed;
    std::vector<TGraphicView *> touchedViews;
    bool touched {true};
    bool allTouched {true};

    void touch(TGraphicView *view = nullptr) noexcept;
    void finishFrame(const GraphicFrame &) noexcept;
    void clearTouches() noexcept;
};

struct SixelDetectionInfo
{
    std::string profileKey;
    std::string configPath;
    std::string termProgram;
    std::string termProgramVersion;
    std::string term;
    std::string colorterm;
    std::string xtermVersion;
    std::string vteVersion;
    std::string sixelProfileOverride;
    std::string sixelConfigOverride;
    int screenCols {0};
    int screenRows {0};
    int fontWidth {0};
    int fontHeight {0};
    int colorCount {0};
    int screenMode {0};
};

struct SixelConfig
{
    static TGraphicProfile activeProfile(TPoint fontSize) noexcept;
    static TGraphicProfile detectedProfile() noexcept;
    static SixelDetectionInfo detect();
    static std::string configPath();
    static std::string profileKey();
    static bool writeProfile(const std::string &key, const TGraphicProfile &profile, std::string *error = nullptr);
};

GraphicFrame collectGraphicFrame() noexcept;
std::vector<TRect> eraseStaleGraphicOverlay( DisplayAdapter &, const GraphicOverlayState &,
                                             const TScreenCell *, TPoint,
                                             const GraphicFrame & ) noexcept;
GraphicFrame drawGraphicOverlay(DisplayAdapter &, const GraphicOverlayState &,
                                const std::vector<TRect> *dirtyCells = nullptr) noexcept;

} // namespace tvision

#endif // TVISION_INTERNAL_GRAPHICS_H
