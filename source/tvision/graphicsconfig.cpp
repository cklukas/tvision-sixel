/*
 * Sixel graphics support additions and modifications:
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#define Uses_TGraphicRuntime
#define Uses_TScreen
#include <tvision/tv.h>

#include <internal/graphics.h>
#include <internal/getenv.h>
#include <internal/platform.h>

#include <algorithm>
#include <ctype.h>
#include <errno.h>
#include <fstream>
#include <sstream>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_one(path) _mkdir(path)
#else
#define mkdir_one(path) mkdir(path, 0700)
#endif

namespace tvision
{

namespace
{
bool hasTemporaryProfile;
TGraphicProfile temporaryProfile;

static std::string trim(std::string s)
{
    auto notSpace = [] (int c) { return !isspace((unsigned char) c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

static std::string sanitizeKey(std::string s)
{
    if (s.empty())
        return "default";
    for (char &c : s)
        if (!(isalnum((unsigned char) c) || c == '.' || c == '_' || c == '-'))
            c = '_';
    return s;
}

static int parseInt(const std::string &value, int def)
{
    char *end = nullptr;
    long v = strtol(value.c_str(), &end, 0);
    return end != value.c_str() ? int(v) : def;
}

static std::string envString(const char *name)
{
    const char *value = getenv(name);
    return value ? value : "";
}

static void applyValue(TGraphicProfile &profile, const std::string &key, const std::string &value)
{
    if (key == "enabled")
        profile.enabled = parseInt(value, profile.enabled) != 0;
    else if (key == "cell_width_px")
        profile.cellWidth = parseInt(value, profile.cellWidth);
    else if (key == "cell_height_px")
        profile.cellHeight = parseInt(value, profile.cellHeight);
    else if (key == "fill_width_px")
        profile.fillWidth = parseInt(value, profile.fillWidth);
    else if (key == "fill_height_px")
        profile.fillHeight = parseInt(value, profile.fillHeight);
    else if (key == "max_colors")
        profile.maxColors = parseInt(value, profile.maxColors);
}

static bool loadProfile(const std::string &wanted, TGraphicProfile &profile)
{
    std::ifstream in(SixelConfig::configPath().c_str());
    if (!in)
        return false;

    bool inWanted = false;
    bool found = false;
    std::string line;
    while (std::getline(in, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line.front() == '[' && line.back() == ']')
        {
            inWanted = trim(line.substr(1, line.size() - 2)) == wanted;
            found = found || inWanted;
            continue;
        }
        if (inWanted)
        {
            size_t sep = line.find('=');
            if (sep != std::string::npos)
                applyValue(profile, trim(line.substr(0, sep)), trim(line.substr(sep + 1)));
        }
    }
    return found;
}

static void ensureDir(const std::string &path)
{
    if (!path.empty() && mkdir_one(path.c_str()) != 0 && errno != EEXIST)
        ;
}

static std::string parentDir(const std::string &path)
{
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

} // namespace

TGraphicProfile SixelConfig::activeProfile(TPoint fontSize) noexcept
{
    TGraphicProfile profile;
    if (hasTemporaryProfile)
        profile = temporaryProfile;
    else
        loadProfile(profileKey(), profile);

    if (profile.enabled)
    {
        if (profile.cellWidth <= 0)
            profile.cellWidth = fontSize.x;
        if (profile.cellHeight <= 0)
            profile.cellHeight = fontSize.y;
        if (profile.fillWidth <= 0)
            profile.fillWidth = profile.cellWidth;
        if (profile.fillHeight <= 0)
            profile.fillHeight = profile.cellHeight;
        if (profile.maxColors <= 1)
            profile.maxColors = 256;

        if (profile.cellWidth <= 0 || profile.cellHeight <= 0 ||
            profile.fillWidth <= 0 || profile.fillHeight <= 0)
            profile.enabled = False;
    }
    return profile;
}

TGraphicProfile SixelConfig::detectedProfile() noexcept
{
    SixelDetectionInfo info = detect();
    TGraphicProfile profile;
    profile.enabled = True;
    profile.cellWidth = info.fontWidth;
    profile.cellHeight = info.fontHeight;
    profile.fillWidth = info.fontWidth;
    profile.fillHeight = info.fontHeight;
    profile.maxColors = info.colorCount >= 256 ? 256 : 64;
    if (profile.cellWidth <= 0)
        profile.cellWidth = 8;
    if (profile.cellHeight <= 0)
        profile.cellHeight = 16;
    if (profile.fillWidth <= 0)
        profile.fillWidth = profile.cellWidth;
    if (profile.fillHeight <= 0)
        profile.fillHeight = profile.cellHeight;
    if (profile.maxColors <= 1)
        profile.maxColors = 256;
    return profile;
}

SixelDetectionInfo SixelConfig::detect()
{
    SixelDetectionInfo info;
    info.profileKey = profileKey();
    info.configPath = configPath();
    info.termProgram = envString("TERM_PROGRAM");
    info.termProgramVersion = envString("TERM_PROGRAM_VERSION");
    info.term = envString("TERM");
    info.colorterm = envString("COLORTERM");
    info.xtermVersion = envString("XTERM_VERSION");
    info.vteVersion = envString("VTE_VERSION");
    info.sixelProfileOverride = envString("TVISION_SIXEL_PROFILE");
    info.sixelConfigOverride = envString("TVISION_SIXEL_CONFIG");

    Platform &platform = Platform::getInstance();
    TPoint fontSize = platform.getDisplayFontSize();
    info.fontWidth = fontSize.x;
    info.fontHeight = fontSize.y;
    info.colorCount = platform.getDisplayColorCount();
    info.screenCols = TScreen::screenWidth;
    info.screenRows = TScreen::screenHeight;
    info.screenMode = TScreen::screenMode;
    return info;
}

std::string SixelConfig::configPath()
{
    const char *overridePath = getenv("TVISION_SIXEL_CONFIG");
    if (overridePath && *overridePath)
        return overridePath;

    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
        return std::string(xdg) + "/tvision/sixel.conf";

    const char *home = getenv("HOME");
    if (home && *home)
        return std::string(home) + "/.config/tvision/sixel.conf";

    return "sixel.conf";
}

std::string SixelConfig::profileKey()
{
    const char *explicitProfile = getenv("TVISION_SIXEL_PROFILE");
    if (explicitProfile && *explicitProfile)
        return sanitizeKey(explicitProfile);

    std::string key;
    for (const char *name : {"TERM_PROGRAM", "TERM_PROGRAM_VERSION", "TERM", "COLORTERM"})
    {
        const char *value = getenv(name);
        if (value && *value)
        {
            if (!key.empty())
                key += ':';
            key += value;
        }
    }
    return sanitizeKey(key.empty() ? "default" : key);
}

bool SixelConfig::writeProfile(const std::string &key, const TGraphicProfile &profile, std::string *error)
{
    std::string path = configPath();
    std::string dir = parentDir(path);
    if (!dir.empty())
    {
        std::string accum;
        size_t pos = 0;
        if (dir[0] == '/')
            accum = "/";
        while (pos < dir.size())
        {
            size_t next = dir.find('/', pos);
            std::string part = dir.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
            if (!part.empty())
            {
                if (accum.size() > 1)
                    accum += '/';
                accum += part;
                ensureDir(accum);
            }
            if (next == std::string::npos)
                break;
            pos = next + 1;
        }
    }

    std::ifstream in(path.c_str());
    std::ostringstream preserved;
    bool skipping = false;
    std::string safeKey = sanitizeKey(key);
    std::string line;
    while (std::getline(in, line))
    {
        std::string trimmed = trim(line);
        if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']')
            skipping = trim(trimmed.substr(1, trimmed.size() - 2)) == safeKey;
        if (!skipping)
            preserved << line << '\n';
    }

    std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc);
    if (!out)
    {
        if (error)
            *error = "cannot open " + path;
        return false;
    }

    out << preserved.str();
    out << '[' << safeKey << "]\n";
    out << "enabled=" << (profile.enabled ? 1 : 0) << "\n";
    out << "cell_width_px=" << profile.cellWidth << "\n";
    out << "cell_height_px=" << profile.cellHeight << "\n";
    out << "fill_width_px=" << profile.fillWidth << "\n";
    out << "fill_height_px=" << profile.fillHeight << "\n";
    out << "max_colors=" << profile.maxColors << "\n";
    SixelDetectionInfo info = detect();
    out << "manual_sixel_supported=1\n";
    out << "detected_profile_key=" << info.profileKey << "\n";
    out << "detected_config_path=" << info.configPath << "\n";
    out << "detected_term_program=" << info.termProgram << "\n";
    out << "detected_term_program_version=" << info.termProgramVersion << "\n";
    out << "detected_term=" << info.term << "\n";
    out << "detected_colorterm=" << info.colorterm << "\n";
    out << "detected_xterm_version=" << info.xtermVersion << "\n";
    out << "detected_vte_version=" << info.vteVersion << "\n";
    out << "detected_tvision_sixel_profile=" << info.sixelProfileOverride << "\n";
    out << "detected_tvision_sixel_config=" << info.sixelConfigOverride << "\n";
    out << "detected_screen_cols=" << info.screenCols << "\n";
    out << "detected_screen_rows=" << info.screenRows << "\n";
    out << "detected_font_width_px=" << info.fontWidth << "\n";
    out << "detected_font_height_px=" << info.fontHeight << "\n";
    out << "detected_color_count=" << info.colorCount << "\n";
    out << "detected_screen_mode=" << info.screenMode << "\n";
    out << "detected_sixel_capability=manual-confirmed\n";
    return bool(out);
}

} // namespace tvision

Boolean TGraphicRuntime::isConfigured() noexcept
{
    tvision::Platform &platform = tvision::Platform::getInstance();
    return tvision::SixelConfig::activeProfile(platform.getDisplayFontSize()).enabled;
}

TGraphicProfile TGraphicRuntime::getProfile() noexcept
{
    tvision::Platform &platform = tvision::Platform::getInstance();
    return tvision::SixelConfig::activeProfile(platform.getDisplayFontSize());
}

void TGraphicRuntime::invalidate() noexcept
{
    tvision::Platform &platform = tvision::Platform::getInstance();
    platform.touchGraphics();
    platform.flushScreen();
}

void TGraphicRuntime::invalidate(TGraphicView *view) noexcept
{
    tvision::Platform &platform = tvision::Platform::getInstance();
    platform.touchGraphics(view);
    platform.flushScreen();
}

void TGraphicRuntime::setTemporaryProfile(const TGraphicProfile &profile) noexcept
{
    using namespace tvision;
    temporaryProfile = profile;
    hasTemporaryProfile = true;
    TGraphicRuntime::invalidate();
}

void TGraphicRuntime::clearTemporaryProfile() noexcept
{
    using namespace tvision;
    hasTemporaryProfile = false;
    TGraphicRuntime::invalidate();
}
