// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "Common/WindowFit.h"

using namespace Common::WindowFit;

TEST(WindowFit, Area)
{
  EXPECT_EQ(Area16x9({1280, 800}).width, 1280);
  EXPECT_EQ(Area16x9({1280, 800}).height, 720);
  EXPECT_EQ(Area16x9({1920, 1080}).width, 1920);
  EXPECT_EQ(Area16x9({1920, 1080}).height, 1080);
  EXPECT_EQ(Area16x9({1000, 1000}).height, 563);
  EXPECT_EQ(Area16x9({0, 100}).width, 0);
}

TEST(WindowFit, SmallDialogTakesHalf)
{
  const Fit fit = FitWindow({1920, 1080}, {300, 200}, {0, 0}, {0, 0});
  EXPECT_EQ(fit.size.width, 960);
  EXPECT_EQ(fit.size.height, 540);
  EXPECT_FALSE(fit.too_small);
}

TEST(WindowFit, GrowsToNeed)
{
  const Fit fit = FitWindow({1920, 1080}, {1344, 400}, {0, 0}, {0, 0});
  EXPECT_EQ(fit.size.width, 1344);
  EXPECT_EQ(fit.size.height, 756);
}

TEST(WindowFit, NeverLargerThanArea)
{
  const Fit fit = FitWindow({1280, 800}, {3000, 2000}, {0, 0}, {0, 0});
  EXPECT_EQ(fit.size.width, 1280);
  EXPECT_EQ(fit.size.height, 720);
}

TEST(WindowFit, FixedSizeIsKept)
{
  const Fit fit = FitWindow({1920, 1080}, {400, 150}, {400, 150}, {400, 150});
  EXPECT_EQ(fit.size.width, 400);
  EXPECT_EQ(fit.size.height, 150);
  EXPECT_FALSE(fit.too_small);
}

TEST(WindowFit, MinimumLargerThanArea)
{
  const Fit fit = FitWindow({1280, 800}, {500, 500}, {900, 1100}, {0, 0});
  EXPECT_TRUE(fit.too_small);
  EXPECT_EQ(fit.size.width, 1280);
  EXPECT_EQ(fit.size.height, 1100);
}

TEST(WindowFit, NoArea)
{
  const Fit fit = FitWindow({0, 0}, {300, 200}, {0, 0}, {0, 0});
  EXPECT_EQ(fit.size.width, 300);
  EXPECT_EQ(fit.size.height, 200);
}
