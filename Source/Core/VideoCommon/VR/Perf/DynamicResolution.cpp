// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/Perf/DynamicResolution.h"

#ifdef ENABLE_VR

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/HookableEvent.h"
#include "Common/Logging/Log.h"
#include "Common/Timer.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VR/OpenXRManager.h"
#include "VideoCommon/VR/Perf/DynamicResolutionController.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

namespace VR::DynamicResolution
{
namespace
{
// The host thread's answer: the steps over the floor.
std::atomic<int> s_up{0};
// The video thread's: the floor (the setting, 0 before the first frame) and what the screen can show.
std::atomic<int> s_floor{0};
std::atomic<double> s_coverage{0.0};
// The game has ended.
std::atomic<bool> s_stopped{false};

// The host thread's.
Controller s_controller;
CeilingHold s_ceiling;
std::string s_game;
int s_game_floor = 0;
u64 s_last_second_us = 0;
ScaleStore s_store;
bool s_store_loaded = false;
Common::EventHook s_state_hook;

constexpr u64 GAP_US = 3'000'000;

std::string StorePath()
{
  return File::GetUserPath(D_CONFIG_IDX) + "dynamic-resolution.txt";
}

int RememberedScale(const std::string& game)
{
  if (!s_store_loaded)
  {
    s_store_loaded = true;
    std::string text;
    if (File::ReadFileToString(StorePath(), text))
      s_store = ParseStore(text);
  }
  const auto it = s_store.find(game);
  return it != s_store.end() ? it->second : 0;
}

void Remember(const std::string& game, int scale)
{
  if (!IsStoredGame(game) || RememberedScale(game) == scale)
    return;
  s_store[game] = scale;
  INFO_LOG_FMT(VIDEO, "Dynamic Resolution: {}x remembered for {}", scale, game);
  const std::string path = StorePath();
  if (!File::WriteStringToFile(path + ".tmp", FormatStore(s_store)) ||
      !File::Rename(path + ".tmp", path))
  {
    WARN_LOG_FMT(VIDEO, "Dynamic Resolution: could not save {}", path);
  }
}

bool InEffect(const VideoConfig& config)
{
  return config.vr_dynamic_resolution && config.vr_stereo_screen;
}

// How far over the floor the GPU and the shaders allow.
int GetMaxUp(const VideoConfig& config, int floor)
{
  // Vertex rounding is on from 2x: a step from 1x would build every shader again.
  if (floor == 1 && config.bVertexRounding)
    return 0;
  const int most = static_cast<int>(g_backend_info.MaxTextureSize / EFB_WIDTH);
  return std::clamp(std::min(most, Controller::MAX_SCALE - 1) - floor, 0, Controller::MAX_UP);
}

double GetScreenCoverage()
{
  if (!g_openxr || !g_openxr->GetSwapchain() || !g_openxr->IsSessionRunning())
    return 0.0;
  const FrameXr::Input::PanelQuad quad = g_openxr->GetScreenQuad();
  const float distance = g_openxr->GetScreenPlacement().distance;
  if (distance <= 0.0f)
    return 0.0;

  ScreenCover cover;
  cover.tan_w = quad.width_m / distance;
  cover.tan_h = quad.height_m / distance;
  const XrFovf& fov = g_openxr->GetEyeViews()[0].fov;
  cover.eye_tan_w = std::tan(fov.angleRight) - std::tan(fov.angleLeft);
  cover.eye_tan_h = std::tan(fov.angleUp) - std::tan(fov.angleDown);
  // The recommended size, without the setting's factor.
  const double scale = g_ActiveConfig.vr_resolution_scale > 0.0f ? g_ActiveConfig.vr_resolution_scale :
                                                                   1.0;
  cover.eye_px_w = g_openxr->GetSwapchain()->GetEyeWidth() / scale;
  cover.eye_px_h = g_openxr->GetSwapchain()->GetEyeHeight() / scale;
  return ScreenCoverage(cover);
}
}  // namespace

int GetEFBScale(const VideoConfig& config)
{
  const int setting = config.iEFBScale;
  if (!InEffect(config))
    return setting;

  int floor = setting;
  if (setting == EFB_SCALE_AUTO_INTEGRAL)
    floor = g_presenter ? static_cast<int>(g_presenter->AutoIntegralScale()) : 0;
  s_floor.store(floor, std::memory_order_relaxed);
  s_coverage.store(GetScreenCoverage(), std::memory_order_relaxed);

  const int up = s_up.load(std::memory_order_relaxed);
  return floor > 0 && up > 0 ? floor + up : setting;
}

void OnSecond(const Perf::Second& second)
{
  if (!s_state_hook)
  {
    s_state_hook = Core::AddOnStateChangedCallback([](Core::State state) {
      if (state != Core::State::Uninitialized)
        return;
      s_up.store(0, std::memory_order_relaxed);
      s_floor.store(0, std::memory_order_relaxed);
      s_stopped.store(true, std::memory_order_relaxed);
    });
  }
  if (s_stopped.exchange(false, std::memory_order_relaxed))
    s_game_floor = 0;

  // In Immersive the GPU's time is not known (only eye pairs are timed): the setting is kept.
  const VideoConfig& config = g_ActiveConfig;
  const int floor = s_floor.load(std::memory_order_relaxed);
  if (!InEffect(config) || floor <= 0)
    return;

  const u64 now_us = Common::Timer::NowUs();
  const bool gap = now_us - s_last_second_us > GAP_US;
  s_last_second_us = now_us;

  const int max_up = GetMaxUp(config, floor);
  const std::string& game = SConfig::GetInstance().GetGameID();
  const int before = s_up.load(std::memory_order_relaxed);
  if (s_game_floor == 0 || game != s_game)
  {
    s_game = game;
    s_game_floor = floor;
    s_ceiling = {};
    s_controller.Start(StartUp(floor, RememberedScale(game), max_up));
  }
  else if (floor != s_game_floor)
  {
    // A new Internal Resolution wins over what was remembered.
    s_game_floor = floor;
    s_controller.Start(0);
  }
  else if (gap)
  {
    s_controller.Resume();
  }

  Sample sample;
  sample.seconds = second.seconds;
  sample.running = second.running;
  sample.speed = second.speed;
  sample.period_ms = second.period_ms;
  sample.refreshes = second.refreshes;
  sample.every_refresh = config.vr_eager_heartbeat;
  sample.fresh = second.fresh;
  sample.gpu_frames = second.gpu_frames;
  sample.gpu_ms = second.gpu_ms;
  sample.skipped = second.skipped_pairs;
  sample.gpu_wait_ms = second.gpu_wait_ms;
  sample.scale = second.efb_scale;

  const int ceiling = s_ceiling.Next(s_coverage.load(std::memory_order_relaxed));
  const int ceiling_up = ceiling > 0 ? std::max(0, ceiling - floor) : max_up;
  const double predicted = s_controller.PredictUp(sample);
  const int up = s_controller.Judge(sample, floor, max_up, ceiling_up);
  s_up.store(up, std::memory_order_relaxed);

  if (up != before)
  {
    const Controller::Step step = s_controller.LastStep();
    INFO_LOG_FMT(VIDEO,
                 "Dynamic Resolution: {}x (was {}x, {}; GPU {:.1f} ms of {:.1f}, {} pairs and {} left "
                 "out, waits {:.1f} ms, speed {:.2f}, {:.1f} ms predicted a scale up, the screen shows {}x)",
                 floor + up, floor + before,
                 step == Controller::Step::Up   ? "room" :
                 step == Controller::Step::Down ? "the GPU is the limit" :
                                                  "the game, the setting or the screen",
                 sample.gpu_ms, Controller::Interval(sample), sample.gpu_frames, sample.skipped,
                 sample.gpu_wait_ms, sample.speed, predicted, ceiling);
  }
  if (max_up > 0 && s_controller.IsSettled())
    Remember(game, floor + s_controller.Level());
}
}  // namespace VR::DynamicResolution

#endif  // ENABLE_VR
