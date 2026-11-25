// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/Perf/DynamicResolution.h"

#ifdef ENABLE_VR

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <string>

#include <fmt/format.h>

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
#include "VideoCommon/VR/Perf/StatsCounters.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

namespace VR::DynamicResolution
{
namespace
{
// Immersive is two steps up with Dynamic Resolution off too.
constexpr bool IMMERSIVE_ALWAYS_UP = true;

// Immersive's answer before its first second: the top.
constexpr int UNDECIDED = -1;

static_assert(EFB_PIXELS_W == EFB_WIDTH && EFB_PIXELS_H == EFB_HEIGHT);

// A display mode's: a game can go from one to the other and back.
struct Mode
{
  explicit Mode(bool immersive_) : immersive(immersive_), up(immersive_ ? UNDECIDED : 0) {}

  const bool immersive;
  // The host thread's answer: the steps over the floor.
  std::atomic<int> up;
  // The video thread's: the floor (the setting, 0 before the first frame).
  std::atomic<int> floor{0};

  // The host thread's.
  Controller controller;
  std::string game;
  int game_floor = 0;
  u64 last_second_us = 0;
};
Mode s_screen{false};
Mode s_immersive{true};

// The video thread's: what the screen can show, and Immersive's eye image.
std::atomic<double> s_coverage{0.0};
std::atomic<int> s_eye_width{0};
std::atomic<int> s_eye_height{0};
// The game has ended.
std::atomic<bool> s_stopped{false};

// The host thread's.
CeilingHold s_ceiling;
ScaleStore s_store;
bool s_store_loaded = false;
Perf::ScaleWatch s_watch;
std::string s_watched_game;
const Mode* s_watched_mode = nullptr;
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
  if (game.empty() || RememberedScale(game) == scale)
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

// The mode that is drawn, null for one this has nothing to do with.
Mode* GetMode(const VideoConfig& config)
{
  if (config.stereo_mode == StereoMode::OpenXR)
    return &s_immersive;
  return config.vr_stereo_screen ? &s_screen : nullptr;
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

int GetFloor(const VideoConfig& config)
{
  if (config.iEFBScale != EFB_SCALE_AUTO_INTEGRAL)
    return config.iEFBScale;
  return g_presenter ? static_cast<int>(g_presenter->AutoIntegralScale()) : 0;
}

// The same for the mode that is drawn. Host thread.
int GetMaxUp(const Mode& mode, const VideoConfig& config, int floor)
{
  const int max_up = GetMaxUp(config, floor);
  return mode.immersive ?
             ImmersiveUp(floor,
                         EyeCeiling(s_eye_width.load(std::memory_order_relaxed),
                                    s_eye_height.load(std::memory_order_relaxed)),
                         max_up) :
             max_up;
}

void Watch(const Mode* mode, const std::string& game, const Perf::Second& second)
{
  if (mode != s_watched_mode || game != s_watched_game)
    s_watch.Reset();
  s_watched_mode = mode;
  s_watched_game = game;
  if (second.running)
    s_watch.Update(second.efb_scale, Common::Timer::NowUs());
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

// Once per session, and again when the setting changes.
void LogImmersive(int eye_w, int eye_h, int floor, int max_up)
{
  static std::array<int, 4> s_logged{};
  const std::array<int, 4> now{eye_w, eye_h, floor, max_up};
  if (s_logged == now)
    return;
  s_logged = now;
  if (floor > 0)
  {
    INFO_LOG_FMT(VIDEO,
                 "Immersive: the eye image is {}x{}, the EFB covers it at {}x; Internal Resolution {}x, "
                 "drawn at up to {}x",
                 eye_w, eye_h, EyeCeiling(eye_w, eye_h), floor, floor + max_up);
  }
}

int GetImmersiveScale(const VideoConfig& config)
{
  // No eye image before and after a session.
  const IOpenXRSwapchain* swapchain = g_openxr ? g_openxr->GetSwapchain() : nullptr;
  const int eye_w = swapchain ? static_cast<int>(swapchain->GetEyeWidth()) : 0;
  const int eye_h = swapchain ? static_cast<int>(swapchain->GetEyeHeight()) : 0;
  const int floor = swapchain ? GetFloor(config) : 0;
  const int max_up =
      floor > 0 ? ImmersiveUp(floor, EyeCeiling(eye_w, eye_h), GetMaxUp(config, floor)) : 0;
  s_eye_width.store(eye_w, std::memory_order_relaxed);
  s_eye_height.store(eye_h, std::memory_order_relaxed);
  s_immersive.floor.store(floor, std::memory_order_relaxed);
  LogImmersive(eye_w, eye_h, floor, max_up);

  int up = IMMERSIVE_ALWAYS_UP ? max_up : 0;
  if (config.vr_dynamic_resolution)
  {
    up = s_immersive.up.load(std::memory_order_relaxed);
    up = up == UNDECIDED ? max_up : std::min(up, max_up);
  }
  return up > 0 ? floor + up : config.iEFBScale;
}
}  // namespace

int GetEFBScale(const VideoConfig& config)
{
  if (config.stereo_mode == StereoMode::OpenXR)
    return GetImmersiveScale(config);

  const int setting = config.iEFBScale;
  if (!config.vr_dynamic_resolution || !config.vr_stereo_screen)
    return setting;

  const int floor = GetFloor(config);
  s_screen.floor.store(floor, std::memory_order_relaxed);
  s_coverage.store(GetScreenCoverage(), std::memory_order_relaxed);

  const int up = s_screen.up.load(std::memory_order_relaxed);
  return floor > 0 && up > 0 ? floor + up : setting;
}

void OnSecond(const Perf::Second& second)
{
  if (!s_state_hook)
  {
    s_state_hook = Core::AddOnStateChangedCallback([](Core::State state) {
      if (state != Core::State::Uninitialized)
        return;
      s_screen.up.store(0, std::memory_order_relaxed);
      s_screen.floor.store(0, std::memory_order_relaxed);
      s_immersive.up.store(UNDECIDED, std::memory_order_relaxed);
      s_immersive.floor.store(0, std::memory_order_relaxed);
      s_stopped.store(true, std::memory_order_relaxed);
    });
  }
  if (s_stopped.exchange(false, std::memory_order_relaxed))
  {
    s_screen.game_floor = s_immersive.game_floor = 0;
    s_watch.Reset();
  }

  const VideoConfig& config = g_ActiveConfig;
  Mode* const mode = GetMode(config);
  Watch(mode, SConfig::GetInstance().GetGameID(), second);
  if (!config.vr_dynamic_resolution || !mode)
    return;
  const bool immersive = mode->immersive;
  const int floor = mode->floor.load(std::memory_order_relaxed);
  if (floor <= 0)
    return;

  const u64 now_us = Common::Timer::NowUs();
  const bool gap = now_us - mode->last_second_us > GAP_US;
  mode->last_second_us = now_us;

  const int eye_w = s_eye_width.load(std::memory_order_relaxed);
  const int eye_h = s_eye_height.load(std::memory_order_relaxed);
  const int max_up = GetMaxUp(*mode, config, floor);
  const std::string& game = SConfig::GetInstance().GetGameID();
  const std::string key = StoreKey(game, immersive);
  const int last_up = mode->up.load(std::memory_order_relaxed);
  const int before = last_up == UNDECIDED ? max_up : last_up;
  if (mode->game_floor == 0 || game != mode->game)
  {
    mode->game = game;
    mode->game_floor = floor;
    if (!immersive)
      s_ceiling = {};
    const int remembered = RememberedScale(key);
    mode->controller.Start(immersive ? ImmersiveStartUp(floor, remembered, max_up) :
                                       StartUp(floor, remembered, max_up));
  }
  else if (floor != mode->game_floor)
  {
    // A new Internal Resolution wins over what was remembered.
    mode->game_floor = floor;
    mode->controller.Start(immersive ? max_up : 0);
  }
  else if (gap)
  {
    mode->controller.Resume();
  }

  Sample sample;
  sample.seconds = second.seconds;
  sample.running = second.running;
  sample.speed = second.speed;
  sample.period_ms = second.period_ms;
  sample.refreshes = second.refreshes;
  sample.every_refresh = config.vr_eager_heartbeat;
  // Every pair of Immersive is shown, the ones that were not timed too: they count once.
  sample.fresh = immersive ? second.gpu_frames : second.fresh;
  sample.gpu_frames = second.gpu_frames;
  sample.gpu_ms = second.gpu_ms;
  sample.skipped = second.skipped_pairs;
  sample.gpu_wait_ms = second.gpu_wait_ms;
  sample.scale = second.efb_scale;

  const int ceiling =
      immersive ? floor + max_up : s_ceiling.Next(s_coverage.load(std::memory_order_relaxed));
  const int ceiling_up = ceiling > 0 ? std::max(0, ceiling - floor) : max_up;
  const double predicted = mode->controller.PredictUp(sample);
  const int up = mode->controller.Judge(sample, floor, max_up, ceiling_up);
  mode->up.store(up, std::memory_order_relaxed);

  if (up != before)
  {
    const Controller::Step step = mode->controller.LastStep();
    const std::string limit =
        immersive ? fmt::format("the eye image is {}x{}, covered at {}x", eye_w, eye_h,
                                EyeCeiling(eye_w, eye_h)) :
                    fmt::format("the screen shows {}x", ceiling);
    INFO_LOG_FMT(VIDEO,
                 "Dynamic Resolution: {}x (was {}x, {}; GPU {:.1f} ms of {:.1f}, {} pairs and {} left "
                 "out, waits {:.1f} ms, speed {:.2f}, {:.1f} ms predicted a scale up, {})",
                 floor + up, floor + before,
                 step == Controller::Step::Up   ? "room" :
                 step == Controller::Step::Down ? "the GPU is the limit" :
                                                  "the game, the setting or the screen",
                 sample.gpu_ms, Controller::Interval(sample), sample.gpu_frames, sample.skipped,
                 sample.gpu_wait_ms, sample.speed, predicted, limit);
  }
  if (max_up > 0 && mode->controller.IsSettled())
    Remember(key, floor + mode->controller.Level());
}

Status GetStatus()
{
  const VideoConfig& config = g_ActiveConfig;
  const Mode* const mode = GetMode(config);
  Status status;
  status.on = config.vr_dynamic_resolution && mode;
  const int floor = status.on ? mode->floor.load(std::memory_order_relaxed) : 0;
  if (floor > 0)
  {
    status.floor = floor;
    status.top = floor + GetMaxUp(*mode, config, floor);
  }
  if (const auto& change = s_watch.GetChange())
  {
    status.change = {change->from, change->to,
                     static_cast<double>(Common::Timer::NowUs() - change->at_us) * 1e-6};
  }
  return status;
}
}  // namespace VR::DynamicResolution

#endif  // ENABLE_VR
