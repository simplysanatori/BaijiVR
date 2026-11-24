// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/Perf/VRPerf.h"

#ifdef ENABLE_VR

#include <atomic>
#include <ctime>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/Timer.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/PerformanceMetrics.h"
#include "VideoCommon/VR/Perf/DynamicResolution.h"
#include "VideoCommon/VR/Perf/PerfTest.h"
#include "VideoCommon/VideoConfig.h"

namespace VR::Perf
{
namespace
{
std::atomic<u32> s_gpu_frames{0};
std::atomic<u64> s_gpu_us{0};
std::atomic<u64> s_gpu_max_us{0};
std::atomic<u32> s_skipped_pairs{0};
std::atomic<u32> s_gpu_waits{0};
std::atomic<u64> s_gpu_wait_us{0};

constexpr u64 SECOND_US = 1'000'000;

// The pacing thread's.
struct Window
{
  u64 start_us = 0;
  u32 refreshes = 0;
  u32 fresh = 0;
  u32 empty = 0;
  u64 wait_frame_us = 0;
  u64 end_frame_us = 0;
  bool running = true;
};
Window s_window;

void WriteLine(const Second& s)
{
  const std::string path = File::GetUserPath(D_LOGS_IDX) + "perf.csv";
  const bool is_new = !File::Exists(path);
  File::IOFile file(path, "ab");
  if (!file)
    return;
  if (is_new)
  {
    file.WriteString("time,game,seconds,refreshes,fresh,empty,period_ms,wait_frame_ms,end_frame_ms,"
                     "gpu_frames,gpu_ms,gpu_max_ms,skipped_pairs,gpu_waits,gpu_wait_ms,fps,vps,speed,"
                     "efb_scale,running,step,switches\n");
  }
  file.WriteString(fmt::format(
      "{:%H:%M:%S},{},{:.3f},{},{},{},{:.2f},{:.2f},{:.2f},{},{:.2f},{:.2f},{},{},{:.2f},{:.1f},{:.1f},"
      "{:.3f},{},{},{},{}\n",
      fmt::localtime(std::time(nullptr)), SConfig::GetInstance().GetGameID(), s.seconds,
      s.refreshes, s.fresh, s.empty, s.period_ms, s.wait_frame_ms, s.end_frame_ms, s.gpu_frames,
      s.gpu_ms, s.gpu_max_ms, s.skipped_pairs, s.gpu_waits, s.gpu_wait_ms, s.fps, s.vps, s.speed,
      s.efb_scale, s.running ? 1 : 0, Test::GetStepName(), DescribeSwitches()));
}
}  // namespace

void AddGpuFrame(u64 us)
{
  s_gpu_frames.fetch_add(1, std::memory_order_relaxed);
  s_gpu_us.fetch_add(us, std::memory_order_relaxed);
  u64 max = s_gpu_max_us.load(std::memory_order_relaxed);
  while (us > max && !s_gpu_max_us.compare_exchange_weak(max, us, std::memory_order_relaxed))
  {
  }
}

void CountSkippedPair()
{
  s_skipped_pairs.fetch_add(1, std::memory_order_relaxed);
}

void AddGpuWait(u64 us)
{
  s_gpu_waits.fetch_add(1, std::memory_order_relaxed);
  s_gpu_wait_us.fetch_add(us, std::memory_order_relaxed);
}

void Tick(bool fresh, bool empty, bool running, s64 period_ns, u64 wait_frame_us, u64 end_frame_us)
{
  const u64 now = Common::Timer::NowUs();
  if (s_window.start_us == 0)
    s_window.start_us = now;
  ++s_window.refreshes;
  s_window.fresh += fresh;
  s_window.empty += empty;
  s_window.wait_frame_us += wait_frame_us;
  s_window.end_frame_us += end_frame_us;
  s_window.running &= running;
  if (now - s_window.start_us < SECOND_US)
    return;

  const Window w = s_window;
  s_window = {.start_us = now};

  Second s;
  s.seconds = static_cast<double>(now - w.start_us) / SECOND_US;
  s.refreshes = w.refreshes;
  s.fresh = w.fresh;
  s.empty = w.empty;
  s.period_ms = static_cast<double>(period_ns) * 1e-6;
  s.wait_frame_ms = w.wait_frame_us * 1e-3 / w.refreshes;
  s.end_frame_ms = w.end_frame_us * 1e-3 / w.refreshes;
  s.gpu_frames = s_gpu_frames.exchange(0, std::memory_order_relaxed);
  const u64 gpu_us = s_gpu_us.exchange(0, std::memory_order_relaxed);
  s.gpu_ms = s.gpu_frames ? gpu_us * 1e-3 / s.gpu_frames : 0.0;
  s.gpu_max_ms = s_gpu_max_us.exchange(0, std::memory_order_relaxed) * 1e-3;
  s.skipped_pairs = s_skipped_pairs.exchange(0, std::memory_order_relaxed);
  s.gpu_waits = s_gpu_waits.exchange(0, std::memory_order_relaxed);
  s.gpu_wait_ms = s_gpu_wait_us.exchange(0, std::memory_order_relaxed) * 1e-3;
  s.running = w.running;

  if (!g_ActiveConfig.vr_perf_log && !g_ActiveConfig.vr_dynamic_resolution && !Test::IsRequested())
    return;

  Core::QueueHostJob([s](Core::System&) mutable {
    s.fps = g_perf_metrics.GetFPS();
    s.vps = g_perf_metrics.GetVPS();
    s.speed = g_perf_metrics.GetSpeed();
    s.efb_scale = g_framebuffer_manager ? static_cast<int>(g_framebuffer_manager->GetEFBScale()) : 0;
    Consume(s);
  });
}

void Consume(const Second& second)
{
  if (g_ActiveConfig.vr_perf_log || Test::IsRequested())
    WriteLine(second);
  DynamicResolution::OnSecond(second);
  Test::OnSecond(second);
}

std::string DescribeSwitches()
{
  std::string out;
  const auto add = [&](std::string_view name, const auto& info) {
    const auto value = Config::Get(info);
    if (value != info.GetDefaultValue())
      out += fmt::format("{}{}={}", out.empty() ? "" : " ", name, value);
  };
  add("DynamicResolution", Config::GFX_VR_DYNAMIC_RESOLUTION);
  add("PinEmulationCores", Config::GFX_VR_PIN_EMULATION_CORES);
  add("PerformanceHint", Config::GFX_VR_PERFORMANCE_HINT);
  add("AsyncEFBCopies", Config::GFX_VR_ASYNC_EFB_COPIES);
  add("NonBlockingPerfQueries", Config::GFX_VR_NON_BLOCKING_PERF_QUERIES);
  add("SkipOverwrittenClears", Config::GFX_VR_SKIP_OVERWRITTEN_CLEARS);
  add("InternalResolution", Config::GFX_EFB_SCALE);
  const int mode = static_cast<int>(Config::Get(Config::GFX_SHADER_COMPILATION_MODE));
  if (mode != 0)
    out += fmt::format("{}ShaderCompilationMode={}", out.empty() ? "" : " ", mode);
  return out;
}
}  // namespace VR::Perf

#endif  // ENABLE_VR
