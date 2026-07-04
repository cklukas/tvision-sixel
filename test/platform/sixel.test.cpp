/*
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#include <test.h>

#include <internal/graphics.h>
#include <internal/sixel.h>

#include <stdlib.h>
#include <string>

#ifdef _WIN32
#include <process.h>
static void setEnvVar(const char *name, const char *value)
{
    _putenv_s(name, value ? value : "");
}
#else
#include <unistd.h>
static void setEnvVar(const char *name, const char *value)
{
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
}
#endif

TEST(SixelEncoder, EmitsDcsWrappedRaster)
{
    uint32_t pixels[] = {
        0xFFFF0000, 0xFF00FF00,
        0xFF0000FF, 0xFFFFFFFF,
    };

    std::string sixel = tvision::encodeSixel(pixels, {2, 2}, 16);

    ASSERT_FALSE(sixel.empty());
    EXPECT_EQ("\x1BPq", sixel.substr(0, 3));
    EXPECT_NE(std::string::npos, sixel.find("\"1;1;2;2"));
    EXPECT_NE(std::string::npos, sixel.find("#0;2;"));
    EXPECT_EQ("\x1B\\", sixel.substr(sixel.size() - 2));
}

TEST(SixelConfig, WritesAndLoadsSelectedProfile)
{
    std::string path =
#ifdef _WIN32
        "sixel-test.conf";
#else
        std::string("/tmp/tvision-sixel-test-") + std::to_string((long long) getpid()) + ".conf";
#endif

    setEnvVar("TVISION_SIXEL_CONFIG", path.c_str());
    setEnvVar("TVISION_SIXEL_PROFILE", "unit-test-terminal");
    TGraphicRuntime::clearTemporaryProfile();

    TGraphicProfile profile;
    profile.enabled = True;
    profile.cellWidth = 7;
    profile.cellHeight = 15;
    profile.fillWidth = 7;
    profile.fillHeight = 16;
    profile.maxColors = 64;

    ASSERT_TRUE(tvision::SixelConfig::writeProfile(tvision::SixelConfig::profileKey(), profile));

    TGraphicProfile loaded = tvision::SixelConfig::activeProfile({0, 0});
    EXPECT_TRUE(loaded.enabled);
    EXPECT_EQ(7, loaded.cellWidth);
    EXPECT_EQ(15, loaded.cellHeight);
    EXPECT_EQ(7, loaded.fillWidth);
    EXPECT_EQ(16, loaded.fillHeight);
    EXPECT_EQ(64, loaded.maxColors);

    remove(path.c_str());
    setEnvVar("TVISION_SIXEL_CONFIG", nullptr);
    setEnvVar("TVISION_SIXEL_PROFILE", nullptr);
}
