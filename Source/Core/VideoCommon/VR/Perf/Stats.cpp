// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoCommon/VR/Perf/Stats.h"

#ifdef ENABLE_VR

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

#include "Common/Config/Config.h"
#include "Common/HookableEvent.h"
#include "Common/Timer.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Core.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VideoEvents.h"

namespace VR::Perf
{
namespace
{
std::mutex s_frames_mutex;
FrameTimes s_frames;

std::mutex s_stats_mutex;
Stats s_stats;
std::atomic<u32> s_serial{0};

// The host thread's, but for reading s_measuring.
std::atomic<bool> s_measuring{false};
Common::EventHook s_frame_hook;
Common::EventHook s_state_hook;
CpuLoad s_cpu;

#ifdef __linux__
// The files with the GPU's clock (Hz; the Frame's, as Azahar for Frame reads it) and its load
// (percent; amdgpu), empty where there is none.
struct GpuFiles
{
  std::string clock;
  std::string load;
};

GpuFiles FindGpuFiles()
{
  namespace fs = std::filesystem;
  GpuFiles files;
  std::error_code error;
  for (const auto& entry : fs::directory_iterator("/sys/class/devfreq", error))
  {
    if (entry.path().filename().string().find("gpu") != std::string::npos)
      files.clock = (entry.path() / "cur_freq").string();
  }
  for (const auto& entry : fs::directory_iterator("/sys/class/drm", error))
  {
    const fs::path path = entry.path() / "device" / "gpu_busy_percent";
    if (files.load.empty() && fs::exists(path, error))
      files.load = path.string();
  }
  return files;
}

template <typename T>
std::optional<T> ReadNumber(const std::string& path)
{
  if (path.empty())
    return std::nullopt;
  std::ifstream file(path);
  T number{};
  if (!(file >> number))
    return std::nullopt;
  return number;
}
#endif

void ReadLoads(Stats* stats)
{
#ifdef __linux__
  std::ifstream stat("/proc/stat");
  stats->cpu = s_cpu.Update(stat);
  static const GpuFiles files = FindGpuFiles();
  stats->gpu = ReadNumber<float>(files.load);
  if (const auto hz = ReadNumber<u64>(files.clock))
    stats->gpu_mhz = static_cast<int>(*hz / 1'000'000);
#endif
}

void BreakFrameTimes()
{
  std::lock_guard lock(s_frames_mutex);
  s_frames.Break();
}
}  // namespace

void SetMeasuring(bool on)
{
  if (s_measuring.exchange(on) == on)
    return;
  if (!on)
  {
    s_frame_hook.reset();
    s_state_hook.reset();
    return;
  }

  {
    std::lock_guard lock(s_frames_mutex);
    s_frames = {};
  }
  s_cpu = {};
  s_frame_hook =
      GetVideoEvents().after_present_event.Register([](const PresentInfo& present_info) {
        if (present_info.reason == PresentInfo::PresentReason::VideoInterfaceDuplicate)
          return;
        const u64 now_us = Common::Timer::NowUs();
        std::lock_guard lock(s_frames_mutex);
        s_frames.Add(now_us);
      });
  s_state_hook = Core::AddOnStateChangedCallback([](Core::State) { BreakFrameTimes(); });
}

bool IsMeasuring()
{
  return s_measuring.load(std::memory_order_relaxed);
}

void Publish(const Second& second)
{
  Stats stats;
  stats.second = second;
  if (s_measuring)
  {
    {
      std::lock_guard lock(s_frames_mutex);
      stats.frames = s_frames.Take();
    }
    if (stats.frames->frames == 0)
      stats.frames.reset();
    ReadLoads(&stats);
  }
  stats.setting = Config::Get(Config::GFX_EFB_SCALE);
  stats.every_refresh = g_ActiveConfig.vr_eager_heartbeat;
  stats.resolution = DynamicResolution::GetStatus();

  std::lock_guard lock(s_stats_mutex);
  stats.serial = s_serial.load(std::memory_order_relaxed) + 1;
  s_stats = std::move(stats);
  s_serial.store(s_stats.serial, std::memory_order_release);
}

Stats GetStats()
{
  std::lock_guard lock(s_stats_mutex);
  return s_stats;
}

u32 GetStatsSerial()
{
  return s_serial.load(std::memory_order_acquire);
}

void ResetStats()
{
  std::lock_guard lock(s_stats_mutex);
  s_stats = {};
  s_serial.store(0, std::memory_order_release);
}
}  // namespace VR::Perf

#endif  // ENABLE_VR
