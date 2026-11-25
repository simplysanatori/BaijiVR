// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "Common/TheaterOverlay.h"

using namespace Common;

TEST(TheaterOverlay, Key)
{
  EXPECT_EQ(GetGameOverlayKey("4188957058"), "valve.steam.desktopgame.4188957058");
  EXPECT_EQ(GetGameOverlayKey(""), "");
  EXPECT_EQ(GetGameOverlayKey("0"), "");
  EXPECT_EQ(GetGameOverlayKey("12 --x"), "");
  EXPECT_EQ(GetGameOverlayKey("12345678901"), "");
}

TEST(TheaterOverlay, Line)
{
  const OverlayState s = ParseOverlayLine("'k' x size=64x64 flags=0x8000 visible VROverlayType");
  EXPECT_TRUE(s.picture);
  EXPECT_TRUE(s.in_dashboard);
  EXPECT_TRUE(s.visible);
  EXPECT_TRUE(s.IsShowable());

  const OverlayState hidden =
      ParseOverlayLine("'k' x size=1920x1080 flags=0x18001 not_visible VROverlayType");
  EXPECT_TRUE(hidden.IsShowable());
  EXPECT_FALSE(hidden.visible);

  EXPECT_FALSE(ParseOverlayLine("'k' x size=0x0 flags=0x8000 visible VROverlayType").IsShowable());
  EXPECT_FALSE(ParseOverlayLine("'k' x size=64x0 flags=0x8000 visible VROverlayType").IsShowable());
  EXPECT_FALSE(ParseOverlayLine("'k' x size=64x64 flags=0x7fff visible VROverlayType").IsShowable());
  EXPECT_FALSE(ParseOverlayLine("").IsShowable());
  EXPECT_FALSE(ParseOverlayLine("'k' x size= flags=0x").IsShowable());
}

TEST(TheaterOverlay, Find)
{
  const std::string listing =
      "Overlays:\n"
      "'valve.steam.desktopgame.123' a size=1x1 flags=0x1 visible VROverlayType\r\n"
      "'valve.steam.desktopgame.12' b size=2x2 flags=0x8000 visible VROverlayType";
  const auto line = FindOverlayLine(listing, GetGameOverlayKey("12"));
  ASSERT_TRUE(line.has_value());
  EXPECT_TRUE(ParseOverlayLine(*line).IsShowable());
  const auto other = FindOverlayLine(listing, GetGameOverlayKey("123"));
  ASSERT_TRUE(other.has_value());
  EXPECT_FALSE(ParseOverlayLine(*other).IsShowable());
  EXPECT_FALSE(FindOverlayLine(listing, GetGameOverlayKey("1")).has_value());
  EXPECT_FALSE(FindOverlayLine("", GetGameOverlayKey("1")).has_value());
}
