/*
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#define Uses_TGraphicCanvas
#include <tvision/tv.h>

#include "moonphase.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{

const double pi = 3.14159265358979323846;
const double twoPi = 2.0*pi;
const double deg = pi/180.0;
const double rad = 180.0/pi;
const double synodicMonth = 29.530588853;

struct Equatorial
{
    double ra;
    double dec;
    double distanceKm;
    double lon;
    double lat;
};

static double normalizeDegrees(double value)
{
    value = std::fmod(value, 360.0);
    if (value < 0.0)
        value += 360.0;
    return value;
}

static double normalizeRadians(double value)
{
    value = std::fmod(value, twoPi);
    if (value < 0.0)
        value += twoPi;
    return value;
}

static double julianDay(time_t utcTime)
{
    return double(utcTime)/86400.0 + 2440587.5;
}

static double obliquity(double T)
{
    return (23.439291111 - 0.013004167*T - 1.63889e-7*T*T + 5.03611e-7*T*T*T)*deg;
}

static Equatorial eclipticToEquatorial(double lon, double lat, double distanceKm, double eps)
{
    double sinLon = std::sin(lon);
    double cosLon = std::cos(lon);
    double sinLat = std::sin(lat);
    double cosLat = std::cos(lat);

    double ra = std::atan2(sinLon*std::cos(eps) - std::tan(lat)*std::sin(eps), cosLon);
    double dec = std::asin(sinLat*std::cos(eps) + cosLat*std::sin(eps)*sinLon);
    return {normalizeRadians(ra), dec, distanceKm, lon, lat};
}

static Equatorial sunPosition(double T)
{
    double L0 = normalizeDegrees(280.46646 + 36000.76983*T + 0.0003032*T*T);
    double M = normalizeDegrees(357.52911 + 35999.05029*T - 0.0001537*T*T)*deg;
    double e = 0.016708634 - 0.000042037*T - 0.0000001267*T*T;
    double C = (1.914602 - 0.004817*T - 0.000014*T*T)*std::sin(M) +
               (0.019993 - 0.000101*T)*std::sin(2*M) +
               0.000289*std::sin(3*M);
    double trueLong = L0 + C;
    double omega = (125.04 - 1934.136*T)*deg;
    double lambda = (trueLong - 0.00569 - 0.00478*std::sin(omega))*deg;
    double R = 149597870.7*(1.000001018*(1 - e*e))/(1 + e*std::cos(M + C*deg));
    return eclipticToEquatorial(lambda, 0.0, R, obliquity(T));
}

struct LunarTerm
{
    int D, M, Mp, F;
    int L;
    int R;
};

struct LunarLatTerm
{
    int D, M, Mp, F;
    int B;
};

static const LunarTerm lunarTerms[] = {
    {0,0,1,0,6288774,-20905355}, {2,0,-1,0,1274027,-3699111},
    {2,0,0,0,658314,-2955968}, {0,0,2,0,213618,-569925},
    {0,1,0,0,-185116,48888}, {0,0,0,2,-114332,-3149},
    {2,0,-2,0,58793,246158}, {2,-1,-1,0,57066,-152138},
    {2,0,1,0,53322,-170733}, {2,-1,0,0,45758,-204586},
    {0,1,-1,0,-40923,-129620}, {1,0,0,0,-34720,108743},
    {0,1,1,0,-30383,104755}, {2,0,0,-2,15327,10321},
    {0,0,1,2,-12528,0}, {0,0,1,-2,10980,79661},
    {4,0,-1,0,10675,-34782}, {0,0,3,0,10034,-23210},
    {4,0,-2,0,8548,-21636}, {2,1,-1,0,-7888,24208},
    {2,1,0,0,-6766,30824}, {1,0,-1,0,-5163,-8379},
    {1,1,0,0,4987,-16675}, {2,-1,1,0,4036,-12831},
    {2,0,2,0,3994,-10445}, {4,0,0,0,3861,-11650},
    {2,0,-3,0,3665,14403}, {0,1,-2,0,-2689,-7003},
    {2,0,-1,2,-2602,0}, {2,-1,-2,0,2390,10056},
    {1,0,1,0,-2348,6322}, {2,-2,0,0,2236,-9884},
    {0,1,2,0,-2120,5751}, {0,2,0,0,-2069,0},
    {2,-2,-1,0,2048,-4950}, {2,0,1,-2,-1773,4130},
    {2,0,0,2,-1595,0}, {4,-1,-1,0,1215,-3958},
    {0,0,2,2,-1110,0}, {3,0,-1,0,-892,3258},
    {2,1,1,0,-810,2616}, {4,-1,-2,0,759,-1897},
    {0,2,-1,0,-713,-2117}, {2,2,-1,0,-700,2354},
    {2,1,-2,0,691,0}, {2,-1,0,-2,596,0},
    {4,0,1,0,549,-1423}, {0,0,4,0,537,-1117},
    {4,-1,0,0,520,-1571}, {1,0,-2,0,-487,-1739},
    {2,1,0,-2,-399,0}, {0,0,2,-2,-381,-4421},
    {1,1,1,0,351,0}, {3,0,-2,0,-340,0},
    {4,0,-3,0,330,0}, {2,-1,2,0,327,0},
    {0,2,1,0,-323,1165}, {1,1,-1,0,299,0},
    {2,0,3,0,294,0}, {2,0,-1,-2,0,8752}
};

static const LunarLatTerm lunarLatTerms[] = {
    {0,0,0,1,5128122}, {0,0,1,1,280602}, {0,0,1,-1,277693},
    {2,0,0,-1,173237}, {2,0,-1,1,55413}, {2,0,-1,-1,46271},
    {2,0,0,1,32573}, {0,0,2,1,17198}, {2,0,1,-1,9266},
    {0,0,2,-1,8822}, {2,-1,0,-1,8216}, {2,0,-2,-1,4324},
    {2,0,1,1,4200}, {2,1,0,-1,-3359}, {2,-1,-1,1,2463},
    {2,-1,0,1,2211}, {2,-1,-1,-1,2065}, {0,1,-1,-1,-1870},
    {4,0,-1,-1,1828}, {0,1,0,1,-1794}, {0,0,0,3,-1749},
    {0,1,-1,1,-1565}, {1,0,0,1,-1491}, {0,1,1,1,-1475},
    {0,1,1,-1,-1410}, {0,1,0,-1,-1344}, {1,0,0,-1,-1335},
    {0,0,3,1,1107}, {4,0,0,-1,1021}, {4,0,-1,1,833},
    {0,0,1,-3,777}, {4,0,-2,1,671}, {2,0,0,-3,607},
    {2,0,2,-1,596}, {2,-1,1,-1,491}, {2,0,-2,1,-451},
    {0,0,3,-1,439}, {2,0,2,1,422}, {2,0,-3,-1,421},
    {2,1,-1,1,-366}, {2,1,0,1,-351}, {4,0,0,1,331},
    {2,-1,1,1,315}, {2,-2,0,-1,302}, {0,0,1,3,-283},
    {2,1,1,-1,-229}, {1,1,0,-1,223}, {1,1,0,1,223},
    {0,1,-2,-1,-220}, {2,1,-1,-1,-220}, {1,0,1,1,-185},
    {2,-1,-2,-1,181}, {0,1,2,1,-177}, {4,0,-2,-1,176},
    {4,-1,-1,-1,166}, {1,0,1,-1,-164}, {4,0,1,-1,132},
    {1,0,-1,-1,-119}, {4,-1,0,-1,115}, {2,-2,0,1,107}
};

static double eccentricityFactor(int M, double E)
{
    int a = std::abs(M);
    if (a == 1)
        return E;
    if (a == 2)
        return E*E;
    return 1.0;
}

static Equatorial moonPosition(double T)
{
    double Lp = normalizeDegrees(218.3164477 + 481267.88123421*T - 0.0015786*T*T +
                                 T*T*T/538841.0 - T*T*T*T/65194000.0);
    double D = normalizeDegrees(297.8501921 + 445267.1114034*T - 0.0018819*T*T +
                                T*T*T/545868.0 - T*T*T*T/113065000.0);
    double M = normalizeDegrees(357.5291092 + 35999.0502909*T - 0.0001536*T*T +
                                T*T*T/24490000.0);
    double Mp = normalizeDegrees(134.9633964 + 477198.8675055*T + 0.0087414*T*T +
                                 T*T*T/69699.0 - T*T*T*T/14712000.0);
    double F = normalizeDegrees(93.2720950 + 483202.0175233*T - 0.0036539*T*T -
                                T*T*T/3526000.0 + T*T*T*T/863310000.0);
    double A1 = normalizeDegrees(119.75 + 131.849*T)*deg;
    double A2 = normalizeDegrees(53.09 + 479264.290*T)*deg;
    double A3 = normalizeDegrees(313.45 + 481266.484*T)*deg;
    double E = 1.0 - 0.002516*T - 0.0000074*T*T;

    double sigmaL = 0.0;
    double sigmaR = 0.0;
    for (const LunarTerm &term : lunarTerms)
    {
        double arg = (term.D*D + term.M*M + term.Mp*Mp + term.F*F)*deg;
        double factor = eccentricityFactor(term.M, E);
        sigmaL += factor*term.L*std::sin(arg);
        sigmaR += factor*term.R*std::cos(arg);
    }

    double sigmaB = 0.0;
    for (const LunarLatTerm &term : lunarLatTerms)
    {
        double arg = (term.D*D + term.M*M + term.Mp*Mp + term.F*F)*deg;
        sigmaB += eccentricityFactor(term.M, E)*term.B*std::sin(arg);
    }

    sigmaL += 3958.0*std::sin(A1) + 1962.0*std::sin((Lp - F)*deg) +
              318.0*std::sin(A2);
    sigmaB += -2235.0*std::sin(Lp*deg) + 382.0*std::sin(A3) +
              175.0*std::sin((A1 - F*deg)) + 175.0*std::sin((A1 + F*deg)) +
              127.0*std::sin((Lp - Mp)*deg) - 115.0*std::sin((Lp + Mp)*deg);

    double lon = normalizeDegrees(Lp + sigmaL/1000000.0)*deg;
    double lat = (sigmaB/1000000.0)*deg;
    double distance = 385000.56 + sigmaR/1000.0;
    return eclipticToEquatorial(lon, lat, distance, obliquity(T));
}

static double siderealTime(double jd, double longitudeDeg)
{
    double T = (jd - 2451545.0)/36525.0;
    double theta = 280.46061837 + 360.98564736629*(jd - 2451545.0) +
                   0.000387933*T*T - T*T*T/38710000.0 + longitudeDeg;
    return normalizeDegrees(theta)*deg;
}

static const char *phaseName(double phase)
{
    if (phase < 0.03 || phase >= 0.97) return "NEW MOON";
    if (phase < 0.22) return "WAXING CRESCENT";
    if (phase < 0.28) return "FIRST QUARTER";
    if (phase < 0.47) return "WAXING GIBBOUS";
    if (phase < 0.53) return "FULL MOON";
    if (phase < 0.72) return "WANING GIBBOUS";
    if (phase < 0.78) return "LAST QUARTER";
    return "WANING CRESCENT";
}

static void moonFillRect(TGraphicCanvas &canvas, int x0, int y0, int w, int h, TGraphicColor color)
{
    int x1 = std::max(0, x0);
    int y1 = std::max(0, y0);
    int x2 = std::min(int(canvas.size.x), x0 + w);
    int y2 = std::min(int(canvas.size.y), y0 + h);
    for (int y = y1; y < y2; ++y)
        for (int x = x1; x < x2; ++x)
            canvas.setPixel(x, y, color);
}

static const char *moonGlyph(char ch)
{
    switch (ch)
    {
    case '0': return "111101101101111";
    case '1': return "010110010010111";
    case '2': return "111001111100111";
    case '3': return "111001111001111";
    case '4': return "101101111001001";
    case '5': return "111100111001111";
    case '6': return "111100111101111";
    case '7': return "111001010010010";
    case '8': return "111101111101111";
    case '9': return "111101111001111";
    case 'A': return "010101111101101";
    case 'B': return "110101110101110";
    case 'C': return "111100100100111";
    case 'D': return "110101101101110";
    case 'E': return "111100111100111";
    case 'F': return "111100111100100";
    case 'G': return "111100101101111";
    case 'H': return "101101111101101";
    case 'I': return "111010010010111";
    case 'J': return "001001001101111";
    case 'K': return "101101110101101";
    case 'L': return "100100100100111";
    case 'M': return "101111111101101";
    case 'N': return "110101101101101";
    case 'O': return "111101101101111";
    case 'P': return "111101111100100";
    case 'Q': return "111101101111001";
    case 'R': return "111101111110101";
    case 'S': return "111100111001111";
    case 'T': return "111010010010010";
    case 'U': return "101101101101111";
    case 'V': return "101101101101010";
    case 'W': return "101101111111101";
    case 'X': return "101101010101101";
    case 'Y': return "101101010010010";
    case 'Z': return "111001010100111";
    case '%': return "101001010100101";
    case '.': return "000000000000010";
    case ':': return "000010000010000";
    case '-': return "000000111000000";
    case '+': return "000010111010000";
    case '/': return "001001010100100";
    case ' ': return "000000000000000";
    default: return "000000000000000";
    }
}

static void drawText(TGraphicCanvas &canvas, int x, int y, const char *text,
                     int scale, TGraphicColor color)
{
    for (size_t i = 0; text[i] != 0; ++i)
    {
        char ch = text[i];
        if ('a' <= ch && ch <= 'z')
            ch = char(ch - 'a' + 'A');
        const char *bits = moonGlyph(ch);
        for (int py = 0; py < 5; ++py)
            for (int px = 0; px < 3; ++px)
                if (bits[py*3 + px] == '1')
                    moonFillRect(canvas, x + px*scale, y + py*scale, scale, scale, color);
        x += 4*scale;
    }
}

static int moonTextWidth(const char *text, int scale)
{
    return int(std::strlen(text))*4*scale;
}

static void drawCenteredText(TGraphicCanvas &canvas, int cx, int y, const char *text,
                             int scale, TGraphicColor color)
{
    drawText(canvas, cx - moonTextWidth(text, scale)/2, y, text, scale, color);
}

static void drawMoon(TGraphicCanvas &canvas, int cx, int cy, int radius,
                     const TMoonState &state)
{
    if (radius <= 0)
        return;

    TGraphicColor dark(18, 22, 31);
    TGraphicColor limb(68, 64, 54);
    TGraphicColor light(238, 218, 150);
    TGraphicColor highlight(255, 240, 178);
    double cosI = 2.0*state.illuminatedFraction - 1.0;
    bool waxing = state.phase < 0.5;
    double side = waxing ? 1.0 : -1.0;
    // Astronomical limb position angles are measured from the local vertical.
    // The raster disk math below uses angles measured from the screen x-axis.
    double screenAngle = state.brightLimbAngleRad - pi/2.0;
    double c = std::cos(screenAngle);
    double s = std::sin(screenAngle);

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            double nx = double(x)/double(radius);
            double ny = -double(y)/double(radius);
            double rr = nx*nx + ny*ny;
            if (rr > 1.0)
                continue;

            double bx = nx*c + ny*s;
            double by = -nx*s + ny*c;
            double edge = std::sqrt(std::max(0.0, 1.0 - by*by));
            bool lit = side*bx >= -cosI*edge;
            TGraphicColor color = lit ? light : dark;
            if (rr > 0.90)
                color = lit ? TGraphicColor(205, 184, 122) : limb;
            else if (lit && bx*side > 0.65 && by < -0.25)
                color = highlight;
            canvas.setPixel(cx + x, cy + y, color);
        }
    }
}

} // namespace

TMoonState calculateMoonState(time_t utcTime, const TMoonObserver &observer)
{
    double jd = julianDay(utcTime);
    double T = (jd - 2451545.0)/36525.0;
    Equatorial sun = sunPosition(T);
    Equatorial moon = moonPosition(T);

    double cosPsi = std::sin(sun.dec)*std::sin(moon.dec) +
                    std::cos(sun.dec)*std::cos(moon.dec)*std::cos(sun.ra - moon.ra);
    cosPsi = std::max(-1.0, std::min(1.0, cosPsi));
    double psi = std::acos(cosPsi);
    double phaseAngle = std::atan2(sun.distanceKm*std::sin(psi),
                                   moon.distanceKm - sun.distanceKm*std::cos(psi));
    if (phaseAngle < 0.0)
        phaseAngle += pi;
    double illuminated = (1.0 + std::cos(phaseAngle))/2.0;

    double phase = normalizeRadians(moon.lon - sun.lon)/twoPi;
    double age = phase*synodicMonth;

    double chi = std::atan2(std::cos(sun.dec)*std::sin(sun.ra - moon.ra),
                            std::sin(sun.dec)*std::cos(moon.dec) -
                            std::cos(sun.dec)*std::sin(moon.dec)*std::cos(sun.ra - moon.ra));

    double lat = observer.latitudeDeg*deg;
    double lst = siderealTime(jd, observer.longitudeDeg);
    double H = normalizeRadians(lst - moon.ra);
    if (H > pi)
        H -= twoPi;
    double altitude = std::asin(std::sin(lat)*std::sin(moon.dec) +
                                std::cos(lat)*std::cos(moon.dec)*std::cos(H));
    double azimuth = std::atan2(-std::sin(H),
                                std::tan(moon.dec)*std::cos(lat) -
                                std::sin(lat)*std::cos(H));
    azimuth = normalizeRadians(azimuth);
    double parallactic = std::atan2(std::sin(H),
                                    std::tan(lat)*std::cos(moon.dec) -
                                    std::sin(moon.dec)*std::cos(H));

    TMoonState state;
    state.illuminatedFraction = std::max(0.0, std::min(1.0, illuminated));
    state.phase = phase;
    state.brightLimbAngleRad = chi - parallactic;
    state.altitudeDeg = altitude*rad;
    state.azimuthDeg = azimuth*rad;
    state.ageDays = age;
    state.phaseName = phaseName(phase);
    return state;
}

void drawMoonIcon(TGraphicCanvas &canvas, int cx, int cy, int radius, const TMoonState &state)
{
    drawMoon(canvas, cx, cy, radius, state);
}

void drawMoonDetailView(TGraphicCanvas &canvas, const TMoonState &state,
                        const char *cityName, time_t cityTime)
{
    canvas.clear(TGraphicColor(0, 0, 0, 0));
    int radius = std::max(12, std::min(int(canvas.size.x), int(canvas.size.y))/4);
    int cx = canvas.size.x/2;
    int cy = std::max(radius + 8, int(canvas.size.y)/3);
    drawMoon(canvas, cx, cy, radius, state);

    int scale = canvas.size.x >= 260 && canvas.size.y >= 180 ? 4 : 2;
    TGraphicColor text(248, 230, 170);
    TGraphicColor accent(250, 210, 95);
    int y = std::min(int(canvas.size.y) - 7*scale, cy + radius + 10);

    char line[96];
    drawCenteredText(canvas, cx, y, cityName, scale, accent);
    y += 7*scale;

    tm *lt = gmtime(&cityTime);
    if (lt != 0)
    {
        std::snprintf(line, sizeof(line), "%04d/%02d/%02d %02d:%02d:%02d",
                      lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                      lt->tm_hour, lt->tm_min, lt->tm_sec);
        drawCenteredText(canvas, cx, y, line, scale, text);
        y += 7*scale;
    }

    drawCenteredText(canvas, cx, y, state.phaseName, scale, text);
    y += 7*scale;

    std::snprintf(line, sizeof(line), "ILLUM %.1f%% AGE %.1fD",
                  state.illuminatedFraction*100.0, state.ageDays);
    drawCenteredText(canvas, cx, y, line, scale, text);
    y += 7*scale;

    std::snprintf(line, sizeof(line), "ALT %.1f AZ %.1f",
                  state.altitudeDeg, state.azimuthDeg);
    drawCenteredText(canvas, cx, y, line, scale, text);
    y += 7*scale;

    std::snprintf(line, sizeof(line), "LIMB %.1f", state.brightLimbAngleRad*rad);
    drawCenteredText(canvas, cx, y, line, scale, text);
}
