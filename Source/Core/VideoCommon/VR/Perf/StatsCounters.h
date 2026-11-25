// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <istream>
#include <optional>
#include <string>

#include "Common/CommonTypes.h"

// What the stats measure themselves, free of Dolphin and threads (unit tested). The CPU's load is
// read as Azahar for Frame reads it.
namespace VR::Perf
{
// How far apart the game's frames come.
class FrameTimes
{
public:
  struct Second
  {
    u32 frames = 0;
    double average_ms = 0.0;
    double peak_ms = 0.0;
    u32 slow = 0;
  };

  // A frame is slow from this many times what is usual for the game, be that 60, 30 or 25 fps.
  static constexpr double SLOW = 1.5;

  void Add(u64 now_us)
  {
    if (m_last_us != 0 && now_us > m_last_us)
    {
      const double ms = static_cast<double>(now_us - m_last_us) * 1e-3;
      ++m_second.frames;
      m_sum_ms += ms;
      m_second.peak_ms = std::max(m_second.peak_ms, ms);
      m_second.slow += m_usual_ms > 0.0 && ms > m_usual_ms * SLOW;
      // One long frame changes little of what is usual.
      m_usual_ms = m_usual_ms > 0.0 ?
                       m_usual_ms + (std::min(ms, m_usual_ms * 2.0) - m_usual_ms) * USUAL_SHARE :
                       ms;
    }
    m_last_us = now_us;
  }

  // The game was paused, or goes on: the time since its last frame is not one frame's.
  void Break()
  {
    m_last_us = 0;
    m_usual_ms = 0.0;
  }

  Second Take()
  {
    Second second = m_second;
    if (second.frames > 0)
      second.average_ms = m_sum_ms / second.frames;
    m_second = {};
    m_sum_ms = 0.0;
    return second;
  }

private:
  static constexpr double USUAL_SHARE = 0.1;

  u64 m_last_us = 0;
  double m_sum_ms = 0.0;
  double m_usual_ms = 0.0;
  Second m_second;
};

// The last change of the resolution in use.
class ScaleWatch
{
public:
  struct Change
  {
    int from = 0;
    int to = 0;
    u64 at_us = 0;
  };

  // For another game, or another display: what it starts at is not a change.
  void Reset() { *this = {}; }

  void Update(int scale, u64 now_us)
  {
    if (scale <= 0 || scale == m_scale)
      return;
    if (m_scale > 0)
      m_change = {m_scale, scale, now_us};
    m_scale = scale;
  }

  const std::optional<Change>& GetChange() const { return m_change; }

private:
  int m_scale = 0;
  std::optional<Change> m_change;
};

// The share of all the CPU's cores that was in use between two reads of /proc/stat, in percent.
class CpuLoad
{
public:
  std::optional<float> Update(std::istream& stat)
  {
    std::string name;
    std::array<u64, 8> times{};
    if (!(stat >> name) || name != "cpu")
      return std::nullopt;
    u64 total = 0;
    for (u64& time : times)
    {
      if (!(stat >> time))
        return std::nullopt;
      total += time;
    }
    const u64 busy = total - times[3] - times[4];  // without idle and iowait
    const u64 total_since = total - m_total;
    const u64 busy_since = busy - m_busy;
    const bool first = m_total == 0;
    m_total = total;
    m_busy = busy;
    if (first || total_since == 0)
      return std::nullopt;
    return 100.0f * static_cast<float>(busy_since) / static_cast<float>(total_since);
  }

private:
  u64 m_total = 0;
  u64 m_busy = 0;
};
}  // namespace VR::Perf
