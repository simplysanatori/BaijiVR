// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "VideoCommon/VR/Perf/VRPerf.h"

// The performance test: Config/perf-test.txt in the user folder has steps (a name and settings),
// and a running game goes through them in turns. perf.csv has a line per second.
//
//   # comment
//   seconds = 20      (running seconds per step)
//   settle = 4        (the first seconds of a step, left out of the numbers)
//   rounds = 3        (round 1 goes A B C, round 2 C B A, round 3 A B C, to even out the heat)
//   name: Key=Value; Key=Value
//
// Keys: DynamicResolution, AsyncEFBCopies, NonBlockingPerfQueries, SkipOverwrittenClears,
// InternalResolution, ShaderCompilationMode, EagerHeartbeat. PinEmulationCores and
// PerformanceHint only apply at a game start, so a step with them is refused.
// The test starts 15 running seconds into the game. Its steps are set on the run's config layer and
// all put back at the end. At the end Logs/perf-test-<date-time>.txt has the numbers per step, and
// the file becomes perf-test.done.txt so that it does not run again.
namespace VR::Perf::Test
{
struct Step
{
  std::string name;
  std::vector<std::pair<std::string, std::string>> settings;  // canonical key, value
};

struct Plan
{
  int seconds = 20;
  int settle = 4;
  int rounds = 3;
  std::vector<Step> steps;
  std::vector<std::string> problems;  // "line N: ..."; the line is left out
};

// Pure: the file's text to a plan.
Plan ParsePlan(std::string_view text);

// Pure: the order of the steps in a round (0 is the first): forward, then reversed, and so on.
std::vector<size_t> RoundOrder(size_t step_count, int round);

struct Summary
{
  size_t n = 0;
  double median = 0.0;
  double p5 = 0.0;
  double p95 = 0.0;
};
// Pure: median and the 5th/95th percentile (linear between the neighbours). Empty gives zeros.
Summary Summarize(std::vector<double> values);

// What one counted second of a step gave.
struct Sample
{
  double fps = 0.0;
  double speed = 0.0;
  bool has_gpu = false;  // the eye pairs of Stereoscopic 3D were timed
  double gpu_ms = 0.0;
  double skipped_pairs_per_s = 0.0;
  double gpu_wait_ms = 0.0;
};
struct StepResult
{
  std::string name;
  std::vector<Sample> samples;  // all rounds, without the settling seconds
};
// Pure: the text of the result file.
std::string FormatReport(const Plan& plan, const std::vector<StepResult>& results,
                         std::string_view game, std::string_view when);

// Any thread; cheap.
bool IsRequested();
// The host thread's.
void OnSecond(const Second& second);
// "name#round", with a "*" while the seconds are still settling.
std::string GetStepName();
}  // namespace VR::Perf::Test

#endif  // ENABLE_VR
