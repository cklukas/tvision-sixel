/*
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#ifndef SIXELDEMO_MOONPHASE_H
#define SIXELDEMO_MOONPHASE_H

#define Uses_TGraphicCanvas
#include <tvision/tv.h>

#include <time.h>

struct TMoonObserver
{
    double latitudeDeg;
    double longitudeDeg;
    int utcOffsetMinutes;
};

struct TMoonState
{
    double illuminatedFraction;
    double phase;
    double brightLimbAngleRad;
    double altitudeDeg;
    double azimuthDeg;
    double ageDays;
    const char *phaseName;
};

TMoonState calculateMoonState(time_t utcTime, const TMoonObserver &observer);
void drawMoonIcon(TGraphicCanvas &canvas, int cx, int cy, int radius, const TMoonState &state);
void drawMoonDetailView(TGraphicCanvas &canvas, const TMoonState &state,
                        const char *cityName, time_t cityTime);

#endif
