/*
 * Sixel graphics support additions and modifications:
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#ifndef TVISION_SIXEL_H
#define TVISION_SIXEL_H

#define Uses_TGraphicCanvas
#include <tvision/tv.h>

#include <string>
#include <vector>

namespace tvision
{

std::string encodeSixel(const uint32_t *pixels, TPoint size, int maxColors,
                        TGraphicDitherMode dither = graphicDitherNearest);

// Apply the exact palette selection and dithering used by encodeSixel and
// return the resulting pixels as opaque 0xAARRGGBB values. Transparent input
// pixels remain transparent. Headless displays use this to show the image a
// conforming SIXEL decoder receives without reimplementing the quantizer.
std::vector<uint32_t> quantizeSixelPixels(
    const uint32_t *pixels, TPoint size, int maxColors,
    TGraphicDitherMode dither = graphicDitherNearest);

} // namespace tvision

#endif // TVISION_SIXEL_H
