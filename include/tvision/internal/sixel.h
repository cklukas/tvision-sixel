/*
 * Sixel graphics support additions and modifications:
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#ifndef TVISION_SIXEL_H
#define TVISION_SIXEL_H

#define Uses_TPoint
#include <tvision/tv.h>

#include <string>

namespace tvision
{

std::string encodeSixel(const uint32_t *pixels, TPoint size, int maxColors);

} // namespace tvision

#endif // TVISION_SIXEL_H
