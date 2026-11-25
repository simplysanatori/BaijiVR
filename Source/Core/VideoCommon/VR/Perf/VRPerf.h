// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

#include <string>

#include "Common/CommonTypes.h"

// What a second of the game cost, for Dynamic Resolution, the performance log and the performance
// test. The Count* and Add* functions are atomic counters, safe from any thread and free of locks;
// Tick is the XR pacing thread's. With nothing that wants the numbers, a Tick does nothing.
namespace VR::Perf
{
struct Second
{
  double seconds = 1.0;
  // The runtime's refreshes: `fresh` had a new picture of the game.
  u32 refreshes = 0;
  u32 fresh = 0;
  u32 empty = 0;
  double period_ms = 0.0;
  double wait_frame_ms = 0.0;  // average xrWaitFrame
  double end_frame_ms = 0.0;   // average xrEndFrame
  // The eye pairs: from the submit to the GPU's end of it, and pairs left out because the GPU was still on
  // the one before. The time includes any earlier work in the queue. In Immersive a pair left out is only
  // one that was not timed: it was shown, and is in `fresh`.
  u32 gpu_frames = 0;
  double gpu_ms = 0.0;
  double gpu_max_ms = 0.0;
  u32 skipped_pairs = 0;
  // Waits of the CPU or video thread for the GPU (EFB copies to RAM, queries, EFB peeks).
  u32 gpu_waits = 0;
  double gpu_wait_ms = 0.0;  // the sum
  // The emulation. 1.0 is full speed.
  double fps = 0.0;
  double vps = 0.0;
  double speed = 0.0;
  int efb_scale = 0;
  // The game ran (not paused, no VR menu) for all of it.
  bool running = false;
};

// Whether anything wants Immersive's GPU time (Stereoscopic 3D's is always there). Video thread.
bool WantsGpuTime();

// Immersive's GPU time for a frame whose eye images go into the queue one after the other: the work up
// to the first eye's submit was done at `first_done_us`, the rest, submitted at `last_submit_us`, at
// `last_done_us`. The time between the two that the GPU had nothing of this frame's is left out.
constexpr u64 GpuBusyUs(u64 first_submit_us, u64 first_done_us, u64 last_submit_us, u64 last_done_us)
{
  if (last_done_us <= first_submit_us)
    return 0;
  if (first_done_us <= first_submit_us || first_done_us >= last_submit_us ||
      last_submit_us > last_done_us)
  {
    return last_done_us - first_submit_us;
  }
  return (first_done_us - first_submit_us) + (last_done_us - last_submit_us);
}

void AddGpuFrame(u64 us);
void CountSkippedPair();
void AddGpuWait(u64 us);

// Every refresh. About once a second the numbers go to a host job, which does Consume.
void Tick(bool fresh, bool empty, bool running, s64 period_ns, u64 wait_frame_us, u64 end_frame_us);

// On the host thread: the log line, then DynamicResolution::OnSecond, the stats and Test::OnSecond.
void Consume(const Second& second);

// The pacing thread's: the last second the game ran for all of goes to the stats (Stats.h).
void PublishLastSecond();
// While the pacing thread does not run: it has not been in this session.
void ForgetLastSecond();

// The performance switches that are not at their defaults ("DynamicResolution=True"), for the log.
std::string DescribeSwitches();
}  // namespace VR::Perf

#endif  // ENABLE_VR
