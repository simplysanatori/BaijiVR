// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "VideoCommon/VR/Perf/PerfTest.h"

using namespace VR::Perf::Test;

TEST(VRPerfTest, ParsesHeaderAndSteps)
{
  const Plan plan = ParsePlan(
      "# a comment\n"
      "seconds = 10\nsettle = 2\nRounds=2\n\n"
      "baseline:\n"
      "waits: AsyncEFBCopies=true; NonBlockingPerfQueries=1 ; skipoverwrittenclears=on\n"
      "3x: InternalResolution=3\n"
      "uber: ShaderCompilationMode=1\n");
  EXPECT_TRUE(plan.problems.empty());
  EXPECT_EQ(plan.seconds, 10);
  EXPECT_EQ(plan.settle, 2);
  EXPECT_EQ(plan.rounds, 2);
  ASSERT_EQ(plan.steps.size(), 4u);
  EXPECT_EQ(plan.steps[0].name, "baseline");
  EXPECT_TRUE(plan.steps[0].settings.empty());
  ASSERT_EQ(plan.steps[1].settings.size(), 3u);
  EXPECT_EQ(plan.steps[1].settings[2].first, "SkipOverwrittenClears");
  EXPECT_EQ(plan.steps[2].settings[0].second, "3");
}

TEST(VRPerfTest, Defaults)
{
  const Plan plan = ParsePlan("a:\n");
  EXPECT_EQ(plan.seconds, 20);
  EXPECT_EQ(plan.settle, 4);
  EXPECT_EQ(plan.rounds, 3);
}

TEST(VRPerfTest, RefusesWhatItCannotApply)
{
  const Plan plan = ParsePlan(
      "pin: PinEmulationCores=true\n"
      "hint: PerformanceHint=true\n"
      "unknown: Foo=1\n"
      "badvalue: DynamicResolution=maybe\n"
      "noequals: DynamicResolution\n"
      "ok: DynamicResolution=true\n"
      "ok: EagerHeartbeat=false\n"
      "nonsense line\n"
      "seconds = abc\n");
  ASSERT_EQ(plan.steps.size(), 1u);  // the duplicate name is out too
  EXPECT_EQ(plan.steps[0].name, "ok");
  EXPECT_EQ(plan.problems.size(), 8u);
}

TEST(VRPerfTest, SettleBelowSeconds)
{
  const Plan plan = ParsePlan("seconds = 3\nsettle = 5\na:\n");
  EXPECT_EQ(plan.settle, 0);
  EXPECT_FALSE(plan.problems.empty());
}

TEST(VRPerfTest, RoundOrder)
{
  EXPECT_EQ(RoundOrder(3, 0), (std::vector<size_t>{0, 1, 2}));
  EXPECT_EQ(RoundOrder(3, 1), (std::vector<size_t>{2, 1, 0}));
  EXPECT_EQ(RoundOrder(3, 2), (std::vector<size_t>{0, 1, 2}));
  EXPECT_TRUE(RoundOrder(0, 1).empty());
}

TEST(VRPerfTest, Summary)
{
  EXPECT_EQ(Summarize({}).n, 0u);
  const Summary one = Summarize({7.0});
  EXPECT_DOUBLE_EQ(one.median, 7.0);
  EXPECT_DOUBLE_EQ(one.p95, 7.0);

  std::vector<double> v;
  for (int i = 100; i >= 0; --i)  // unsorted input, 0..100
    v.push_back(i);
  const Summary s = Summarize(v);
  EXPECT_EQ(s.n, 101u);
  EXPECT_DOUBLE_EQ(s.median, 50.0);
  EXPECT_DOUBLE_EQ(s.p5, 5.0);
  EXPECT_DOUBLE_EQ(s.p95, 95.0);

  const Summary two = Summarize({10.0, 20.0});
  EXPECT_DOUBLE_EQ(two.median, 15.0);
}

TEST(VRPerfTest, Report)
{
  Plan plan;
  std::vector<StepResult> results(2);
  results[0].name = "base";
  results[0].samples = {{60.0, 1.0, true, 5.0, 0.0, 2.0}, {58.0, 0.97, true, 6.0, 1.0, 3.0}};
  results[1].name = "empty";
  const std::string text = FormatReport(plan, results, "GM4E01", "now");
  EXPECT_NE(text.find("base (2 seconds)"), std::string::npos);
  EXPECT_NE(text.find("empty (0 seconds)"), std::string::npos);
  EXPECT_NE(text.find("59.00"), std::string::npos);
}
