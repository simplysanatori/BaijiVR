// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include <vector>

#include "Common/Thread.h"

using Common::CoreSelection;
using Common::CpuCoreInfo;

namespace
{
// A Snapdragon-like layout: 2 slow, 3 mid, 2 fast, 1 prime.
std::vector<CpuCoreInfo> Layout()
{
  return {{0, 300, 1800000}, {1, 300, 1800000}, {2, 700, 2800000}, {3, 700, 2800000},
          {4, 700, 2800000}, {5, 900, 3100000}, {6, 900, 3100000}, {7, 1024, 3400000}};
}
std::vector<int> All(int n)
{
  std::vector<int> cores;
  for (int i = 0; i < n; ++i)
    cores.push_back(i);
  return cores;
}
}  // namespace

TEST(CorePinning, FastestSecondThird)
{
  const CoreSelection s = Common::SelectCoresForRoles(Layout(), All(8));
  EXPECT_EQ(s.emu_cpu, 7);
  EXPECT_EQ(s.emu_video, 6);
  EXPECT_EQ(s.light, 5);
}

TEST(CorePinning, AllowedMaskIsRespected)
{
  const std::vector<int> allowed{0, 1, 2, 3};
  const CoreSelection s = Common::SelectCoresForRoles(Layout(), allowed);
  EXPECT_EQ(s.emu_cpu, 3);  // ties go to the higher index
  EXPECT_EQ(s.emu_video, 2);
  EXPECT_EQ(s.light, -1);  // 0 and 1 are the slowest tier

  const std::vector<int> some{1, 4, 6};
  const CoreSelection t = Common::SelectCoresForRoles(Layout(), some);
  EXPECT_EQ(t.emu_cpu, 6);
  EXPECT_EQ(t.emu_video, 4);
  EXPECT_EQ(t.light, -1);
}

TEST(CorePinning, EqualCoresPinNothing)
{
  std::vector<CpuCoreInfo> cores;
  for (int i = 0; i < 16; ++i)
    cores.push_back({i, 0, 4500000});
  const CoreSelection s = Common::SelectCoresForRoles(cores, All(16));
  EXPECT_EQ(s.emu_cpu, -1);
  EXPECT_EQ(s.emu_video, -1);
  EXPECT_EQ(s.light, -1);

  const std::vector<int> mids{2, 3, 4};
  EXPECT_EQ(Common::SelectCoresForRoles(Layout(), mids).emu_cpu, -1);
}

TEST(CorePinning, FrequencyWhenNoCapacity)
{
  const std::vector<CpuCoreInfo> cores{
      {0, 0, 1000000}, {1, 0, 1000000}, {2, 0, 2000000}, {3, 0, 3000000}};
  const CoreSelection s = Common::SelectCoresForRoles(cores, All(4));
  EXPECT_EQ(s.emu_cpu, 3);
  EXPECT_EQ(s.emu_video, 2);
  EXPECT_EQ(s.light, -1);
}

TEST(CorePinning, NoneOrOneCore)
{
  EXPECT_EQ(Common::SelectCoresForRoles({}, {}).emu_cpu, -1);
  const std::vector<int> one{7};
  EXPECT_EQ(Common::SelectCoresForRoles(Layout(), one).emu_cpu, -1);
  const std::vector<int> two{0, 1};
  const std::vector<CpuCoreInfo> unknown{{0, 0, 0}, {1, 0, 0}};
  EXPECT_EQ(Common::SelectCoresForRoles(unknown, two).emu_cpu, -1);
}
