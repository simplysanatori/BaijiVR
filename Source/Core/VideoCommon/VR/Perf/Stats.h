// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef ENABLE_VR

#include <optional>

#include "VideoCommon/VR/Perf/DynamicResolution.h"
#include "VideoCommon/VR/Perf/StatsCounters.h"
#include "VideoCommon/VR/Perf/VRPerf.h"

// What the stats in the headset show (GFX_VR_SHOW_STATS): the performance log's numbers of a
// second, and what is measured only while they are on.
namespace VR::Perf
{
struct Stats
{
  // Counts the seconds published. 0: none yet.
  u32 serial = 0;
  Second second;
  // Only while the stats are on.
  std::optional<FrameTimes::Second> frames;
  std::optional<float> cpu;  // percent of all cores
  std::optional<float> gpu;  // percent
  std::optional<int> gpu_mhz;
  // Internal Resolution: the setting (0 is Auto), not what is in use (second.efb_scale).
  int setting = 0;
  bool every_refresh = true;
  DynamicResolution::Status resolution;
};

// Host thread. What is measured only for the stats: off, nothing of it runs.
void SetMeasuring(bool on);
// Any thread.
bool IsMeasuring();
// Host thread: a second that the game ran for all of.
void Publish(const Second& second);
// Any thread. The last one published in this session.
Stats GetStats();
// Its serial, without a lock.
u32 GetStatsSerial();
// A new session.
void ResetStats();
}  // namespace VR::Perf

#endif  // ENABLE_VR
