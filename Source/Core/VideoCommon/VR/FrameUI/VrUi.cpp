// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef ENABLE_VR

#include "VideoCommon/VR/FrameUI/VrUi.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <map>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/Image.h"
#include "Common/Logging/Log.h"
#include "Common/VR/OpenXRInputState.h"

#include "Core/Autosave.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/GameMods/GameMods.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/ModeSwitch.h"
#include "Core/NetPlayProto.h"
#include "Core/State.h"
#include "Core/System.h"
#include "Core/VRWiiController.h"

#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/VR/FrameUI/GrabHint.h"
#include "VideoCommon/VR/FrameUI/PadBindings.h"
#include "VideoCommon/VR/OpenXRManager.h"
#include "VideoCommon/VR/Perf/VRPerf.h"
#include "VideoCommon/VideoConfig.h"

namespace VR::UI
{
namespace
{
using namespace FrameXr::Input;
using FrameXr::LayerSlots::Group;
namespace Vis = FrameXr::Vis;
namespace ToastFit = FrameXr::ToastFit;
namespace ui = frame_xr::ui;

constexpr float MENU_DISTANCE = 1.2f;
constexpr float MENU_DROP = 0.08f;
constexpr float MENU_PX_PER_DP = 1.5f;
constexpr u32 MENU_TEX_WIDTH = static_cast<u32>(VrMenu::WIDTH * MENU_PX_PER_DP);
constexpr u32 MENU_TEX_HEIGHT = static_cast<u32>(VrMenu::HEIGHT * MENU_PX_PER_DP);

constexpr float TOAST_DISTANCE = 0.9f;
constexpr float TOAST_DROP = 0.25f;
constexpr u32 TOAST_TEX_WIDTH = ToastFit::kAtlasW;
constexpr u32 TOAST_TEX_HEIGHT = ToastFit::kRowPx * 2;
constexpr int TOAST_ROW = 0;
constexpr int HINT_ROW = 1;

// The beams' strengths, and the dot.
constexpr u32 ART_SIZE = 256;
constexpr int DOT_PIXELS = 64;
constexpr int DOT_X = 160;
constexpr float BEAM_WIDTH = 0.008f;
constexpr float STUB_LENGTH = 0.5f;

constexpr float GRIP_ON = 0.60f;
constexpr float GRIP_OFF = 0.40f;
constexpr float TRIGGER_ON = 0.55f;
constexpr float TRIGGER_OFF = 0.40f;

constexpr s64 REFRESH_NS = 250'000'000;
constexpr u32 STATS_TEX_WIDTH = static_cast<u32>(STATS_WIDTH * MENU_PX_PER_DP);
constexpr u32 STATS_TEX_HEIGHT = static_cast<u32>(STATS_HEIGHT * MENU_PX_PER_DP);
constexpr s32 STATS_TEX_WRIST_HEIGHT = static_cast<s32>(STATS_WRIST_HEIGHT * MENU_PX_PER_DP);
// What the menu opens on while a game runs.
constexpr SettingScope DEFAULT_SCOPE = SettingScope::PerGame;
// How long the pointer rests before it says what it is on.
constexpr s64 TOOLTIP_NS = 600'000'000;
// The autosave's picture is of the running game.
constexpr s64 AUTOSAVE_PICTURE_NS = 250'000'000;

constexpr float NUDGE_DEG = 0.5f;
constexpr s64 NUDGE_FIRST_NS = 400'000'000;
constexpr s64 NUDGE_REPEAT_NS = 100'000'000;

// Of what Azahar calls turbo speed.
constexpr float FAST_FORWARD_SPEED = 2.0f;

std::atomic<bool> s_paused_by_menu{false};
std::mutex s_toast_mutex;
std::optional<std::pair<std::string, u32>> s_toast;
// After the next toast that is posted.
std::optional<std::pair<std::string, u32>> s_next_toast;
bool s_next_toast_ready = false;
// The save states on disk changed.
std::atomic<u32> s_saves_changed{0};
std::atomic<u32> s_mods_changed{0};
// What the config has, and for which of the menu's changes (m_settings_serial).
std::mutex s_settings_mutex;
std::optional<std::pair<u32, SettingValues>> s_settings;
struct Bindings
{
  PadKind kind = PadKind::GameCube;
  PadLabels labels;
  PadTable table;
  std::string edit_lock;
  int controller = -1;
  bool can_choose = true;
};
std::atomic<bool> s_bindings_ready{false};
std::mutex s_bindings_mutex;
Bindings s_bindings;

Vec3f ToVec(const std::array<float, 3>& v)
{
  return {v[0], v[1], v[2]};
}

Posef ToPose(const Common::VR::OpenXRPoseState& pose)
{
  return {{pose.orientation[0], pose.orientation[1], pose.orientation[2], pose.orientation[3]},
          ToVec(pose.position)};
}

XrPosef ToXr(const Posef& pose)
{
  return {{pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w},
          {pose.position.x, pose.position.y, pose.position.z}};
}

bool IsUsed(const Common::VR::OpenXRControllerState& c)
{
  return c.primary_button || c.secondary_button || c.frame_x_button || c.frame_y_button ||
         c.menu_button || c.view_button || c.bumper_button || c.dpad_up || c.dpad_down ||
         c.dpad_left || c.dpad_right || c.thumbstick_button || c.trigger_value > 0.1f ||
         c.squeeze_value > 0.1f || std::abs(c.thumbstick_x) > 0.2f ||
         std::abs(c.thumbstick_y) > 0.2f;
}

// A white dot with a dark rim (Azahar for Frame's).
void DrawDot(std::vector<u8>* pixels)
{
  constexpr float c = (DOT_PIXELS - 1) / 2.0f;
  constexpr float r = DOT_PIXELS / 2.0f - 1.0f;
  for (int y = 0; y < DOT_PIXELS; ++y)
  {
    for (int x = 0; x < DOT_PIXELS; ++x)
    {
      const float d = std::hypot(static_cast<float>(x) - c, static_cast<float>(y) - c);
      const float alpha = std::clamp(r - d, 0.0f, 1.0f);
      const float white = std::clamp(r * 0.72f - d, 0.0f, 1.0f);
      const float value = white * 255.0f + (1.0f - white) * 40.0f;
      u8* const p = pixels->data() + (static_cast<std::size_t>(y) * ART_SIZE + DOT_X + x) * 4;
      p[0] = p[1] = p[2] = static_cast<u8>(value * alpha);
      p[3] = static_cast<u8>(alpha * 255.0f);
    }
  }
}

std::vector<u8> MakeArt()
{
  std::vector<u8> pixels(std::size_t{ART_SIZE} * ART_SIZE * 4, 0);
  const std::vector<u8> beams = Vis::BeamArt();
  constexpr std::size_t row = std::size_t{Vis::kBeamLevels} * Vis::kBeamTexWidth * 4;
  static_assert(Vis::kBeamTexHeight == ART_SIZE);
  for (u32 y = 0; y < ART_SIZE; ++y)
    std::memcpy(pixels.data() + std::size_t{y} * ART_SIZE * 4, beams.data() + y * row, row);
  DrawDot(&pixels);
  return pixels;
}

constexpr std::string_view QUICK_SAVE_SUFFIX = ".quick.sav";

std::string GetQuickSavePath()
{
  return fmt::format("{}{}{}", File::GetUserPath(D_STATESAVES_IDX),
                     SConfig::GetInstance().GetGameID(), QUICK_SAVE_SUFFIX);
}

void QueueConfig(std::function<void()> change)
{
  Core::QueueHostJob([change = std::move(change)](Core::System&) {
    Config::ConfigChangeCallbackGuard guard;
    change();
  });
}

// Once, with the first save state.
void ShowSaveStateNotice()
{
  if (Config::Get(Config::MAIN_SAVE_STATE_NOTICE_SHOWN))
    return;
  Config::SetBaseOrCurrent(Config::MAIN_SAVE_STATE_NOTICE_SHOWN, true);
  std::lock_guard lock(s_toast_mutex);
  s_next_toast.emplace("Save states are NOT a replacement for saving in the game", 5000);
  s_next_toast_ready = false;
}

void QueueSave(std::string path)
{
  Core::QueueHostJob([path = std::move(path)](Core::System& system) {
    // Says it is saved (PostEmulatorMessage) once it is.
    File::CreateFullPath(path);
    ShowSaveStateNotice();
    State::SaveAs(system, path);
  });
}

std::string GetSlotPath(int slot)
{
  return fmt::format("{}{}.s{:02d}", File::GetUserPath(D_STATESAVES_IDX),
                     SConfig::GetInstance().GetGameID(), slot);
}

// "GFEE01.s03": 3; 0 for another name.
int GetSlotOf(std::string_view message)
{
  const std::size_t at = message.rfind(".s");
  if (at == std::string_view::npos || message.size() != at + 4 ||
      !std::isdigit(static_cast<unsigned char>(message[at + 2])) ||
      !std::isdigit(static_cast<unsigned char>(message[at + 3])))
  {
    return 0;
  }
  return (message[at + 2] - '0') * 10 + (message[at + 3] - '0');
}

// The drawing thread: this waits for a save that is being written.
MenuSaves ReadSaves()
{
  MenuSaves saves;
  saves.has_quick_save = File::Exists(GetQuickSavePath());
  for (int i = 0; i < NUM_SLOTS; ++i)
  {
    if (!File::Exists(GetSlotPath(i + 1)))
      continue;
    if (const u64 ms = State::GetUnixTimeOfSlot(i + 1))
    {
      saves.slots[static_cast<std::size_t>(i)] =
          Autosave::LocalDateTime(static_cast<s64>(ms / 1000));
    }
  }
  const s64 now = std::time(nullptr);
  for (const Autosave::Entry& entry : Autosave::List(SConfig::GetInstance().GetGameID()))
  {
    saves.autosaves.push_back({entry.path, entry.picture, Autosave::AgoLocal(entry.time, now),
                               Autosave::LocalDateTime(entry.time),
                               Autosave::LocalTime(entry.time)});
  }
  return saves;
}

// A picture scaled down to what the menu shows.
MenuPicture ReadPicture(const std::string& path)
{
  MenuPicture picture;
  std::string file;
  Common::UniqueBuffer<u8> data;
  u32 width = 0;
  u32 height = 0;
  if (!File::ReadFileToString(path, file) ||
      !Common::LoadPNG({reinterpret_cast<const u8*>(file.data()), file.size()}, &data, &width,
                       &height) ||
      width == 0 || height == 0)
  {
    return picture;
  }
  const u32 step = std::max(1u, width / 160);
  picture.width = static_cast<int>(width / step);
  picture.height = static_cast<int>(height / step);
  picture.pixels.reserve(static_cast<std::size_t>(picture.width) * picture.height * 4);
  for (u32 y = 0; y < static_cast<u32>(picture.height); ++y)
  {
    for (u32 x = 0; x < static_cast<u32>(picture.width); ++x)
    {
      std::array<u32, 3> sum{};
      for (u32 j = 0; j < step; ++j)
      {
        for (u32 i = 0; i < step; ++i)
        {
          const u8* const p =
              data.data() + (std::size_t{y * step + j} * width + x * step + i) * 4;
          for (std::size_t c = 0; c < sum.size(); ++c)
            sum[c] += p[c];
        }
      }
      for (const u32 c : sum)
        picture.pixels.push_back(static_cast<u8>(c / (step * step)));
      picture.pixels.push_back(255);
    }
  }
  return picture;
}

void QueueFastForward(bool on)
{
  Core::QueueHostJob([on](Core::System&) {
    const float normal = Config::GetBase(Config::MAIN_EMULATION_SPEED);
    Config::SetCurrent(Config::MAIN_EMULATION_SPEED, on ? FAST_FORWARD_SPEED : normal);
  });
}

// Reads the settings after all that was queued before.
void QueueReadSettings(u32 serial)
{
  Core::QueueHostJob([serial](Core::System&) {
    SettingValues values = ReadSettings();
    std::lock_guard lock(s_settings_mutex);
    s_settings.emplace(serial, std::move(values));
  });
}

void ResumeFromMenu(Core::System& system)
{
  if (s_paused_by_menu.exchange(false) && Core::GetState(system) == Core::State::Paused)
    Core::SetState(system, Core::State::Running);
}

// `keep`: it is the display from now on, too, for every game or for this one.
void QueueSwitchMode(bool to_immersive, std::optional<SettingScope> keep = std::nullopt)
{
  Core::QueueHostJob([to_immersive, keep](Core::System&) {
    if (!g_openxr || !g_openxr->GetSwapchain())
      return;
    const OpenXRPresentationMode mode = to_immersive ? OpenXRPresentationMode::Immersive :
                                                       OpenXRPresentationMode::StereoScreen;
    if (Config::Get(Config::GFX_VR_PRESENTATION_MODE) != mode)
    {
      if (g_openxr->GetSwapchain()->HasFoveatedFramebuffers() ||
          (g_framebuffer_manager && g_framebuffer_manager->IsEFBFoveated()))
      {
        VrUi::PostToast("Turn Foveated Rendering Off to Switch", 5000);
        return;
      }
      if (!to_immersive)
        g_openxr->RequestFlatScreenReanchor();
      Config::SetCurrent(Config::GFX_VR_PRESENTATION_MODE, mode);
      VrUi::PostToast(to_immersive ? "Immersive" : "Stereoscopic 3D", 2000);
    }
    if (keep)
      WriteSetting(SettingId::Display, static_cast<float>(mode), *keep, true);
  });
}
}  // namespace

void RequestSwitchToFlat()
{
  if (!ModeSwitch::Request(ModeSwitch::Mode::Flat))
    VrUi::PostToast("Switch to Flat Needs Baiji VR's Window", 5000);
}

VrUi::VrUi(OpenXRManager* manager, CreateOverlay create_overlay, u32 max_layers)
    : m_manager(manager), m_create_overlay(std::move(create_overlay)), m_max_layers(max_layers)
{
  ui::InitFonts();
  m_toast_canvas.Init(static_cast<int>(TOAST_TEX_WIDTH), static_cast<int>(TOAST_TEX_HEIGHT));
  std::fill(m_toast_canvas.px.begin(), m_toast_canvas.px.end(), 0u);
  Perf::ForgetLastSecond();
  m_draw_thread = std::thread(&VrUi::DrawThread, this);
  Core::QueueHostJob([](Core::System& system) { VRWiiController::OnSessionStarted(system); });
}

VrUi::~VrUi()
{
  {
    std::lock_guard lock(m_draw_mutex);
    m_draw_exit = true;
  }
  m_draw_cv.notify_one();
  m_draw_thread.join();

  // The game must not be left paused, or fast, without a menu to change that.
  Core::QueueHostJob(ResumeFromMenu);
  if (m_fast_forward)
    QueueFastForward(false);
}

void VrUi::PostToast(std::string text, u32 ms)
{
  std::lock_guard lock(s_toast_mutex);
  s_toast.emplace(std::move(text), ms);
  s_next_toast_ready = true;
}

void VrUi::PostEmulatorMessage(std::string_view message, u32 ms)
{
  if (message.starts_with("Saving State") || message.starts_with("Decompressing State"))
    return;
  const bool saved = message.starts_with("Saved State") || message == "Autosaved";
  const bool loaded = message.starts_with("Loaded State");
  if (saved)
    ++s_saves_changed;
  if (!saved && !loaded)
    PostToast(std::string(message.substr(0, message.find('\n'))), ms);
  else if (message.contains(QUICK_SAVE_SUFFIX))
    PostToast(saved ? "Quick Saved" : "Quick Loaded", 2000);
  else if (message == "Autosaved")
    PostToast("Autosaved", 1500);
  else if (message.contains(Autosave::FILE_MARK))
    PostToast("Autosave Loaded", 2000);
  else if (const int slot = GetSlotOf(message))
    PostToast(fmt::format(fmt::runtime(saved ? "Saved to Slot {}" : "Slot {} Loaded"), slot), 2000);
  else
    PostToast(saved ? "State Saved" : "State Loaded", 2000);
}

bool VrUi::IsActive() const
{
  return m_menu_open || m_grab_hand >= 0 || m_stats_hand >= 0 || m_stats_wrist || m_hint != Hint::None || m_toast_until_ns != 0 ||
         m_beams[0].visible || m_beams[1].visible;
}

// ---- Update -------------------------------------------------------------------------------------

void VrUi::Update(const Frame& frame, Controllers* controllers)
{
  const float dt =
      m_last_time_ns ? static_cast<float>(frame.time_ns - m_last_time_ns) * 1e-9f : 0.0f;
  m_last_time_ns = frame.time_ns;
  m_frame = frame;

  const Controllers raw = *controllers;
  WatchInstall();
  UpdateMenu(frame, raw, dt);
  UpdateGrips(raw);
  UpdateStats(frame, raw, dt);
  UpdateGrab(frame, raw, dt);

  // With the menu on its Position tab the game runs, but it does not get the controllers.
  const PadEvents events =
      m_filter.Update(controllers, frame.time_ns, m_menu_open, m_grab_hand, m_stats_hand);
  HandleEvents(events);

  UpdateBeams(frame, raw);
  UpdateToasts(frame);
}

void VrUi::OnFocusLost()
{
  m_filter.Reset();
  m_edit.Cancel();
  if (m_grab_hand >= 0)
  {
    m_grab_hand = -1;
    SaveScreenPlacement();
  }
  m_grip_down = {};
  m_stats_hand = -1;
  m_stats_wrist = false;
  EndDrag();
  m_press_down = false;
  m_press_item = MenuItem::None;
  m_menu_armed = false;
  StopScrolling();
  if (m_hint != Hint::None)
    m_hint = Hint::None;
  for (Beam& beam : m_beams)
    beam.visible = false;
}

void VrUi::HandleEvents(const PadEvents& events)
{
  if (events.open_menu)
    OpenMenu(m_frame);

  if (events.quick_save)
    QueueSave(GetQuickSavePath());
  if (events.quick_load)
    LoadState(GetQuickSavePath(), "Quick Save", 0);
  if (events.fast_forward)
    SetFastForward(!m_fast_forward);
  if (events.switch_mode)
    SwitchMode();
  m_stats_to_place |= events.reset_screen;
  if (events.reset_screen && !m_frame.has_screen)
  {
    m_manager->RequestRecenter();
    ShowToast("View Reset", ToastFit::kShortNs);
  }
  else if (events.reset_screen)
  {
    m_manager->ResetScreenPlacement();
    SaveScreenPlacement();
    m_manager->RequestFlatScreenReanchor();
    ShowToast("Screen Reset", ToastFit::kShortNs);
  }
  if (events.stop)
  {
    ShowToast("Stopping...", ToastFit::kLongNs);
    Core::QueueHostJob([](Core::System& system) { Core::Stop(system); });
  }

  if (m_grab_hand >= 0)
    m_hint = Hint::Grab;
  else if (events.stopping)
    m_hint = Hint::Stop;
  else
    m_hint = Hint::None;
}

void VrUi::SetFastForward(bool on)
{
  if (on == m_fast_forward)
    return;
  m_fast_forward = on;
  m_menu.fast_forward = on;
  QueueFastForward(on);
  ShowToast(on ? "Fast Forward" : "Normal Speed", ToastFit::kShortNs);
}

void VrUi::SwitchMode()
{
  if (m_menu.settings.can_switch_mode)
    QueueSwitchMode(m_frame.has_screen);
}

void VrUi::LoadState(std::string path, std::string undo_label, s64 time)
{
  if (!File::Exists(path))
  {
    ShowToast("No Quick Save", ToastFit::kShortNs);
    return;
  }
  m_menu.undo_load = fmt::format("{} Loaded: {}", undo_label,
                                 Autosave::LocalTime(time ? time : std::time(nullptr)));
  Core::QueueHostJob(
      [path = std::move(path)](Core::System& system) { State::LoadAs(system, path); });
}

void VrUi::RequestSaves()
{
  {
    std::lock_guard lock(m_draw_mutex);
    m_saves_request = true;
  }
  m_draw_cv.notify_one();
}

void VrUi::RequestMods()
{
  {
    std::lock_guard lock(m_draw_mutex);
    m_mods_request = m_menu.mods_list == 0 ? GameMods::List::Mods : GameMods::List::Textures;
  }
  m_draw_cv.notify_one();
}

void VrUi::WatchInstall()
{
  if (!m_watch_install || m_frame.time_ns - m_last_watch_ns < REFRESH_NS)
    return;
  m_last_watch_ns = m_frame.time_ns;
  GameMods::Progress progress = GameMods::GetProgress();
  using Phase = GameMods::Progress::Phase;
  if (progress.phase != Phase::Downloading && progress.phase != Phase::Unpacking)
  {
    m_watch_install = false;
    if (progress.phase == Phase::Done)
      ShowToast(fmt::format("{} Installed", progress.name), ToastFit::kShortNs);
    else if (progress.phase == Phase::Failed)
      ShowToast(progress.error, ToastFit::kLongNs);
    m_menu.mods_changed |= progress.phase == Phase::Done;
    ++s_mods_changed;
  }
  m_menu.mods_progress = std::move(progress);
}

void VrUi::SaveScreenPlacement()
{
  const ScreenPlacement placement = m_manager->GetScreenPlacement();
  QueueConfig([placement] {
    using namespace GameSettingScope;
    SetWhereItIs(Config::GFX_VR_SCREEN_DISTANCE, placement.distance, Store::VRFile);
    SetWhereItIs(Config::GFX_VR_SCREEN_SIZE, placement.height, Store::VRFile);
    Config::SetBaseOrCurrent(Config::GFX_VR_SCREEN_YAW, placement.yaw_deg);
    Config::SetBaseOrCurrent(Config::GFX_VR_SCREEN_PITCH, placement.pitch_deg);
  });
}

// ---- Moving the screen --------------------------------------------------------------------------

void VrUi::UpdateGrips(const Controllers& controllers)
{
  for (std::size_t hand = 0; hand < 2; ++hand)
  {
    const auto& c = controllers[hand];
    const bool was_down = m_grip_down[hand];
    m_grip_down[hand] = c.connected && (c.squeeze_value > (was_down ? GRIP_OFF : GRIP_ON) ||
                                        (c.squeeze_button && c.squeeze_value <= 0.0f));
    m_grip_pressed[hand] = m_grip_down[hand] && !was_down;
  }
}

void VrUi::UpdateGrab(const Frame& frame, const Controllers& controllers, float dt)
{
  for (int hand = 0; hand < 2; ++hand)
  {
    const auto& c = controllers[static_cast<std::size_t>(hand)];
    const bool down = m_grip_down[static_cast<std::size_t>(hand)];
    const bool pressed = m_grip_pressed[static_cast<std::size_t>(hand)];

    if (m_grab_hand == hand && (!down || !frame.has_screen || !c.aim_pose.valid || m_menu_open))
    {
      m_grab_hand = -1;
      SaveScreenPlacement();
    }
    if (m_grab_hand < 0 && pressed && m_stats_hand != hand && frame.has_screen &&
        c.aim_pose.valid && !m_menu_open && !g_ActiveConfig.vr_lock_screen)
    {
      const PanelQuad screen = m_manager->GetScreenQuad();
      const Ray ray = RayFromPose(ToPose(c.aim_pose));
      if (screen.visible && IntersectQuad(ray, screen).in_bounds)
      {
        m_grab_hand = hand;
        m_nudge = 0;
        m_curve_step = 0;
        m_curve = static_cast<float>(g_ActiveConfig.vr_screen_curve);
        m_grab.Begin(m_manager->GetScreenAnchor(), m_manager->GetScreenPlacement(), ray);
        m_manager->PulseHaptics(static_cast<std::size_t>(hand));
      }
    }
  }

  if (m_grab_hand < 0)
    return;

  const auto& c = controllers[static_cast<std::size_t>(m_grab_hand)];
  ScreenPlacement placement = m_manager->GetScreenPlacement();
  placement = MoveWithStick(placement, c.thumbstick_x, c.thumbstick_y, dt);
  placement = m_grab.Update(m_manager->GetScreenAnchor(), placement,
                            RayFromPose(ToPose(c.aim_pose)));

  // The D-Pad moves it half a degree at a time, for an edge just out of view.
  const int nudge = controllers[0].dpad_left ? 1 : controllers[0].dpad_right ? -1 : 0;
  if (nudge != m_nudge || (nudge && frame.time_ns >= m_nudge_at_ns))
  {
    m_nudge_at_ns = frame.time_ns + (nudge != m_nudge ? NUDGE_FIRST_NS : NUDGE_REPEAT_NS);
    if (nudge)
    {
      if (nudge != m_nudge)
        ShowToast(nudge > 0 ? "Moved Left" : "Moved Right", ToastFit::kShortNs);
      placement = m_grab.Nudge(placement, static_cast<float>(nudge) * NUDGE_DEG);
    }
    m_nudge = nudge;
  }
  m_manager->SetScreenPlacement(placement);

  // Up or down bends it, a step a press.
  const int step = controllers[0].dpad_up ? 1 : controllers[0].dpad_down ? -1 : 0;
  if (step != std::exchange(m_curve_step, step) && step)
  {
    if (!m_manager->CanCurveScreen())
    {
      ShowToast("This VR Runtime Cannot Curve the Screen", ToastFit::kShortNs);
      return;
    }
    const float before = std::exchange(m_curve, StepScreenCurve(m_curve, step));
    // At the ends, too, so that the press shows.
    const SettingRow& row = GetSettingRow(SettingId::ScreenCurve);
    ShowToast(fmt::format("{}: {}", row.label, FormatValue(row, m_curve)), ToastFit::kShortNs);
    if (m_curve != before)
    {
      QueueConfig([curve = static_cast<int>(m_curve)] {
        GameSettingScope::SetWhereItIs(Config::GFX_VR_SCREEN_CURVE, curve,
                                       GameSettingScope::Store::VRFile);
      });
    }
  }
}

// ---- The stats ----------------------------------------------------------------------------------

void VrUi::UpdateStats(const Frame& frame, const Controllers& controllers, float dt)
{
  const bool on = g_ActiveConfig.vr_show_stats;
  const bool on_wrist = g_ActiveConfig.vr_stats_position == 1;
  if (!on && !m_stats_on)
    return;
  if (on != std::exchange(m_stats_on, on) || on_wrist != std::exchange(m_stats_on_wrist, on_wrist))
    m_stats_to_place = true;
  if (const s64 time = m_manager->GetRecenterTime();
      time != m_recenter_seen && frame.time_ns >= time)
  {
    m_recenter_seen = time;
    m_stats_to_place = true;
  }

  const bool live = on && frame.head_valid && !m_menu_open;
  const auto& left = controllers[0];
  m_stats_front = live && !on_wrist;
  if (live && on_wrist && left.connected && left.aim_pose.valid)
  {
    m_wrist_pose = Vis::WristStatsPose(ToPose(left.aim_pose));
    m_stats_wrist = Vis::WristShown(Vis::WristFacingDeg(m_wrist_pose, frame.head.position),
                                    m_stats_wrist);
  }
  else
  {
    m_stats_wrist = false;
  }
  if (!m_stats_front)
  {
    m_stats_hand = -1;
    return;
  }

  std::array<PanelQuad, 1> screens;
  if (frame.has_screen)
    screens[0] = m_manager->GetScreenQuad();
  const Vec3f eye = frame.head.position;
  if (std::exchange(m_stats_to_place, false))
  {
    m_stats_hand = -1;
    m_stats_window.Place(frame.head, screens);
  }
  for (int hand = 0; hand < 2; ++hand)
  {
    const auto i = static_cast<std::size_t>(hand);
    const auto& c = controllers[i];
    if (m_stats_hand == hand && (!m_grip_down[i] || !c.aim_pose.valid))
      m_stats_hand = -1;
    if (m_stats_hand < 0 && m_stats_shown && m_grip_pressed[i] && c.aim_pose.valid)
    {
      const QuadHit hit = IntersectQuad(RayFromPose(ToPose(c.aim_pose)), m_stats_window.GetQuad());
      if (hit.in_bounds)
      {
        m_stats_hand = hand;
        m_stats_window.BeginGrab(eye, hit.point_world);
        m_manager->PulseHaptics(i);
      }
    }
  }
  if (m_stats_hand < 0)
  {
    m_stats_window.Keep(eye, screens);
    return;
  }
  const auto& c = controllers[static_cast<std::size_t>(m_stats_hand)];
  m_stats_window.Move(eye, RayFromPose(ToPose(c.aim_pose)), c.thumbstick_x, c.thumbstick_y, dt,
                      screens);
}

// ---- The menu -----------------------------------------------------------------------------------

Posef VrUi::PlaceAhead(float distance, float drop) const
{
  const Vec3f ahead = Rotate(m_frame.head.orientation, {0.0f, 0.0f, -1.0f});
  const float length = std::hypot(ahead.x, ahead.z);
  // Looking straight up or down: along the head's up instead.
  Vec3f flat = length > 0.2f ? Vec3f{ahead.x / length, 0.0f, ahead.z / length} :
                               Rotate(m_frame.head.orientation, {0.0f, 1.0f, 0.0f});
  if (length <= 0.2f)
  {
    flat.y = 0.0f;
    flat = Normalized(flat);
  }
  const Vec3f centre = m_frame.head.position + flat * distance - Vec3f{0.0f, drop, 0.0f};
  return {FaceViewer(centre, m_frame.head.position, {0.0f, 1.0f, 0.0f}), centre};
}

float VrUi::NearerThanScreen(float distance) const
{
  // Something drawn over a nearer screen cannot be read: the eyes meet on the screen.
  if (!m_frame.has_screen)
    return distance;
  const float screen = Length(m_manager->GetScreenQuad().pose.position - m_frame.head.position);
  return std::clamp(screen * 0.85f, 0.35f, distance);
}

PanelQuad VrUi::GetMenuQuad() const
{
  PanelQuad quad;
  quad.pose = m_menu_pose;
  quad.width_m = VrMenu::WIDTH * VrMenu::METRES_PER_DP * m_menu_scale;
  quad.height_m = VrMenu::HEIGHT * VrMenu::METRES_PER_DP * m_menu_scale;
  quad.visible = m_menu_open;
  return quad;
}

void VrUi::OpenMenu(const Frame& frame)
{
  if (m_menu_open || !frame.head_valid)
    return;
  const float distance = NearerThanScreen(MENU_DISTANCE);
  m_menu_scale = distance / MENU_DISTANCE;
  m_menu_pose = PlaceAhead(distance, MENU_DROP * m_menu_scale);
  m_menu_open = true;
  m_menu_armed = false;
  m_press_down = false;
  m_press_item = MenuItem::None;
  EndDrag();
  m_menu.tab = MenuTab::Main;
  m_menu.page = MenuPage::Main;
  m_menu.autosave_list = false;
  ClosePopup();
  m_menu.section = MenuSection::General;
  m_menu.settings.scope = DEFAULT_SCOPE;
  m_menu.scroll.fill(0.0f);
  m_menu.hover = MenuItem::None;
  m_menu.show_tooltip = false;
  m_last_refresh_ns = 0;
  StopScrolling();

  // The autosave is of the moment it opened.
  m_pause_after_ns = 0;
  if (Config::Get(Config::MAIN_AUTOSAVE_ENABLED))
  {
    m_pause_after_ns = frame.time_ns + AUTOSAVE_PICTURE_NS;
    Core::QueueHostJob(
        [](Core::System& system) { Autosave::Request(system, Autosave::Reason::MenuOpen); });
  }
  RequestBindings();
  RequestSaves();
  Perf::PublishLastSecond();
  RefreshMenuState();
  UpdatePause();
}

void VrUi::RequestBindings()
{
  Core::QueueHostJob([](Core::System& system) {
    Bindings bindings;
    std::tie(bindings.kind, bindings.labels) = GetPadBindings(system.IsWii());
    bindings.edit_lock = GetPadEditLock(bindings.kind);
    bindings.controller = static_cast<int>(VRWiiController::GetCurrent(system));
    bindings.can_choose = VRWiiController::CanChoose(system);
    bindings.table = GetPadTable(bindings.kind);
    std::lock_guard lock(s_bindings_mutex);
    s_bindings = std::move(bindings);
    s_bindings_ready = true;
  });
}

void VrUi::ChooseController(int controller)
{
  if (controller == m_menu.controller)
    return;
  m_menu.controller = controller;
  Core::QueueHostJob([choice = static_cast<VRWiiController::Choice>(controller)](
                         Core::System& system) { VRWiiController::Choose(system, choice); });
  RequestBindings();
}

void VrUi::OpenEditControls()
{
  m_menu.page = MenuPage::EditControls;
  m_edit.Begin(m_menu.pad, m_bindings_table);
  m_menu.edit = m_edit.GetView();
}

void VrUi::UpdateListening(const Controllers& controllers, float dt, bool on_cancel)
{
  const PadEdit::Event event = m_edit.Update(controllers, dt, on_cancel);
  if (event == PadEdit::Event::Changed)
  {
    std::vector<std::pair<PadTarget, PadBinding>> bindings;
    const PadTable& table = m_edit.GetView().table;
    for (const PadTarget target : m_edit.TakeChanges())
      bindings.emplace_back(target, table[static_cast<std::size_t>(target)]);
    Core::QueueHostJob([kind = m_edit.GetKind(), bindings = std::move(bindings)](Core::System&) {
      SetPadBindings(kind, bindings);
    });
    RequestBindings();
  }
  if (event != PadEdit::Event::None)
    m_manager->PulseHaptics(static_cast<std::size_t>(m_menu_hand));
  m_menu.edit = m_edit.GetView();
  // The press was the answer, and is not for the menu.
  m_menu_armed = false;
  m_press_down = false;
  m_press_item = MenuItem::None;
  m_menu.pressed = MenuItem::None;
}

void VrUi::StopScrolling()
{
  m_stick_scroll.Reset();
  m_bar_press.End();
  m_list_drag.Cancel();
}

void VrUi::CloseMenu()
{
  if (!m_menu_open)
    return;
  m_edit.Cancel();
  m_menu_open = false;
  EndDrag();
  m_press_item = MenuItem::None;
  UpdatePause();
}

void VrUi::UpdatePause()
{
  const bool wanted = m_menu_open && !ShowsGame(m_menu.tab, m_menu.section) &&
                      m_frame.time_ns >= m_pause_after_ns;
  if (wanted == m_pause_wanted)
    return;
  m_pause_wanted = wanted;
  if (wanted)
  {
    Core::QueueHostJob([](Core::System& system) {
      if (Core::GetState(system) != Core::State::Running || NetPlay::IsNetPlayRunning())
        return;
      s_paused_by_menu = true;
      Core::SetState(system, Core::State::Paused);
    });
  }
  else
  {
    Core::QueueHostJob(ResumeFromMenu);
  }
}

void VrUi::RefreshMenuState()
{
  m_menu.title = SConfig::GetInstance().GetTitleName();
  const std::string game_id = SConfig::GetInstance().GetGameID();
  if (m_menu.title.empty())
    m_menu.title = game_id;
  m_menu.wii = Core::System::GetInstance().IsWii();
  m_menu.fast_forward = m_fast_forward;

  const OpenXRPresentationMode mode = Config::Get(Config::GFX_VR_PRESENTATION_MODE);
  SettingState& settings = m_menu.settings;
  settings.has_game = !game_id.empty() && game_id != DEFAULT_GAME_ID;
  if (!settings.has_game)
    settings.scope = SettingScope::Global;
  settings.immersive = !m_frame.has_screen;
  settings.can_switch_mode =
      mode == OpenXRPresentationMode::Immersive || mode == OpenXRPresentationMode::StereoScreen;
  settings.can_curve = m_manager->CanCurveScreen();
  if (m_menu.tab == MenuTab::Stats)
    m_menu.stats = GetStatsGroups(Perf::GetStats());
  SetLiveSettings();
  QueueReadSettings(m_settings_serial);
}

void VrUi::SetLiveSettings()
{
  // Global, while the game has its own, is not what is shown.
  SettingValues& values = m_menu.settings.values;
  const auto set = [&values](SettingId id, float value) {
    const auto i = static_cast<std::size_t>(id);
    values.game[i] = value;
    if (!values.game_value[i])
      values.global[i] = value;
  };
  const ScreenPlacement placement = m_manager->GetScreenPlacement();
  set(SettingId::Distance, placement.distance);
  set(SettingId::Size, placement.height);
  values.game[static_cast<std::size_t>(SettingId::Display)] =
      static_cast<float>(m_frame.has_screen ? OpenXRPresentationMode::StereoScreen :
                                              OpenXRPresentationMode::Immersive);
}

void VrUi::ChangeSetting(SettingId id, float value, bool save)
{
  m_menu.settings.Set(id, value);
  QueueConfig([id, value, save, scope = m_menu.settings.scope] {
    WriteSetting(id, value, scope, save);
  });
  QueueReadSettings(++m_settings_serial);
}

void VrUi::EndDrag()
{
  if (const auto slider = std::exchange(m_drag, std::nullopt); slider && m_drag_changed)
  {
    const auto id = static_cast<SettingId>(slider->index);
    ChangeSetting(id, m_menu.settings.Get(id));
  }
}

void VrUi::UseGlobalValues()
{
  const MenuSection section =
      m_menu.tab == MenuTab::Position ? MenuSection::Screen : m_menu.section;
  QueueConfig([section] { UI::UseGlobalValues(section); });
  if (section == MenuSection::Screen)
  {
    Core::QueueHostJob([](Core::System&) {
      const OpenXRPresentationMode mode = Config::GetBase(Config::GFX_VR_PRESENTATION_MODE);
      if (mode == OpenXRPresentationMode::Immersive || mode == OpenXRPresentationMode::StereoScreen)
        QueueSwitchMode(mode == OpenXRPresentationMode::Immersive);
    });
  }
  QueueReadSettings(++m_settings_serial);
}

bool VrUi::HasPopup() const
{
  return m_menu.open_setting != SettingId::None || m_menu.open_scope || m_menu.open_controller;
}

void VrUi::ClosePopup()
{
  m_menu.open_setting = SettingId::None;
  m_menu.open_scope = false;
  m_menu.open_controller = false;
}

void VrUi::UpdateMenu(const Frame& frame, const Controllers& controllers, float dt)
{
  if (!m_menu_open)
    return;

  if (frame.time_ns - m_last_refresh_ns >= REFRESH_NS)
  {
    m_last_refresh_ns = frame.time_ns;
    RefreshMenuState();
  }
  UpdatePause();
  if (s_saves_changed.exchange(0) != 0)
    RequestSaves();
  if (s_mods_changed.exchange(0) != 0)
    RequestMods();
  if (s_bindings_ready.exchange(false))
  {
    std::lock_guard lock(s_bindings_mutex);
    m_menu.pad = s_bindings.kind;
    m_menu.pad_labels = s_bindings.labels;
    m_menu.edit_lock = s_bindings.edit_lock;
    m_menu.controller = s_bindings.controller;
    m_menu.can_choose_controller = s_bindings.can_choose;
    m_bindings_table = s_bindings.table;
    if (m_menu.page == MenuPage::EditControls && !m_edit.IsListening())
      OpenEditControls();
  }
  {
    std::lock_guard lock(s_settings_mutex);
    if (s_settings && s_settings->first == m_settings_serial)
    {
      // What was changed since is on its way to the config.
      m_menu.settings.values = std::move(s_settings->second);
      SetLiveSettings();
    }
    s_settings.reset();
  }
  if (m_draw_mutex.try_lock())
  {
    if (m_saves)
      m_menu.saves = std::move(*std::exchange(m_saves, std::nullopt));
    if (auto mods = std::exchange(m_mods, std::nullopt);
        mods && (mods->first == GameMods::List::Mods) == (m_menu.mods_list == 0))
    {
      m_menu.mods = std::move(mods->second);
      m_menu.mods_ready = true;
    }
    m_draw_mutex.unlock();
  }
  m_menu.autosave_list &= m_menu.saves.autosaves.size() > 1;

  const bool back = controllers[1].secondary_button || controllers[1].menu_button ||
                    controllers[0].menu_button;
  const bool back_pressed = back && !m_back_down;
  m_back_down = back;
  if (m_edit.IsListening())
  {
    // Any of them can be the answer.
  }
  else if (!m_menu_armed)
  {
    // The press that opened it is not for it.
    m_menu_armed = !back && controllers[0].trigger_value < TRIGGER_OFF &&
                   controllers[1].trigger_value < TRIGGER_OFF && !controllers[1].primary_button;
  }
  else if (back_pressed)
  {
    // Menu closes it; B goes back first.
    if (controllers[1].secondary_button && HasPopup())
    {
      ClosePopup();
    }
    else if (controllers[1].secondary_button && m_menu.autosave_list)
    {
      m_menu.autosave_list = false;
    }
    else if (controllers[1].secondary_button && m_menu.page != MenuPage::Main)
    {
      m_menu.page = MenuPage::Main;
    }
    else
    {
      CloseMenu();
      return;
    }
  }

  // The hand that points: the one that pressed last, or the one that is on the menu.
  const PanelQuad quad = GetMenuQuad();
  std::array<QuadHit, 2> hits;
  for (std::size_t hand = 0; hand < 2; ++hand)
  {
    if (controllers[hand].aim_pose.valid)
      hits[hand] = IntersectQuad(RayFromPose(ToPose(controllers[hand].aim_pose)), quad);
  }
  if (!m_press_down)
  {
    for (const std::size_t hand : {std::size_t{0}, std::size_t{1}})
    {
      const std::size_t other = 1 - hand;
      const bool pulled = controllers[hand].trigger_value > TRIGGER_ON;
      if (hits[hand].in_bounds && (pulled || !hits[other].in_bounds))
        m_menu_hand = static_cast<int>(hand);
    }
  }

  const auto hand = static_cast<std::size_t>(m_menu_hand);
  const auto& c = controllers[hand];
  const QuadHit& hit = hits[hand];
  const float x = hit.u * VrMenu::WIDTH;
  const float y = hit.v * VrMenu::HEIGHT;

  m_widgets = VrMenu::Layout(m_menu);
  const std::optional<MenuList> list = VrMenu::GetList(m_menu);
  const bool can_scroll = list && list->bar.Valid() && !HasPopup();
  const bool scrolling = m_bar_press.Active() || m_list_drag.Dragging();
  const MenuWidget* const hovered =
      hit.in_bounds && !m_drag && !scrolling ? VrMenu::At(m_widgets, x, y, true) : nullptr;
  MenuItem hover = hovered ? hovered->item : MenuItem::None;
  int index = hovered ? hovered->index : 0;
  if (m_drag)
  {
    hover = m_drag->item;
    index = m_drag->index;
  }
  else if (m_bar_press.Active())
  {
    hover = MenuItem::Scrollbar;
  }
  // With a menu open, what is beside it only closes it.
  const bool beside =
      HasPopup() && hover != MenuItem::Popup && hover != MenuItem::PopupOption;
  if (m_edit.IsListening())
  {
    const bool on_cancel = hovered && hovered->item == MenuItem::EditCancel;
    m_menu.hover = on_cancel ? MenuItem::EditCancel : MenuItem::None;
    m_menu.show_tooltip = false;
    UpdateListening(controllers, dt, on_cancel);
    return;
  }
  const bool usable = hovered && !hovered->disabled && !beside;
  const int option = usable ? VrMenu::OptionAt(*hovered, x) : -1;
  if (hover != m_menu.hover || index != m_menu.hover_index)
  {
    if (usable && !scrolling)
      m_manager->PulseHaptics(hand);
    m_rest_since_ns = frame.time_ns;
  }
  m_menu.hover = hover;
  m_menu.hover_index = index;
  m_menu.hover_option = option;

  const bool down = m_menu_armed &&
                    (c.trigger_value > (m_press_down ? TRIGGER_OFF : TRIGGER_ON) ||
                     (c.trigger_button && c.trigger_value <= 0.0f) ||
                     (hand == 1 && c.primary_button));
  const bool pressed = down && !m_press_down;
  const bool released = !down && m_press_down;
  m_press_down = down;
  m_menu.show_tooltip =
      hovered && !down && frame.time_ns - m_rest_since_ns >= TOOLTIP_NS;

  float* const scroll =
      can_scroll ? &m_menu.scroll[static_cast<std::size_t>(list->section)] : nullptr;
  const float seconds = static_cast<float>(frame.time_ns % 1'000'000'000'000) * 1e-9f;
  if (pressed)
  {
    m_list_drag.Stop();
    m_press_item = usable ? hover : MenuItem::None;
    m_press_index = index;
    if (scroll && hover == MenuItem::Scrollbar)
    {
      *scroll = m_bar_press.Begin(list->bar, *scroll, y);
    }
    else if (usable && hovered->kind == MenuWidget::Kind::Slider &&
             x >= hovered->control.x0 - 12.0f && x < hovered->control.x1)
    {
      m_drag = *hovered;
      m_drag_changed = false;
    }
    else if (scroll && hit.in_bounds && list->bar.list.Contains(x, y))
    {
      // A press that moves scrolls the list.
      m_list_drag.Press(y, *scroll, seconds);
    }
  }
  if (m_drag && down && hit.on_plane)
    SetSlider(*m_drag, VrMenu::SliderAt(*m_drag, x));
  if (scroll && down && !pressed && hit.on_plane)
  {
    if (m_bar_press.Active())
      *scroll = m_bar_press.Update(list->bar, *scroll, y, dt);
    else if (!m_drag && m_list_drag.Move(y, seconds, *scroll, list->bar.max_scroll))
      m_press_item = MenuItem::None;
  }
  if (released)
  {
    const MenuItem item = std::exchange(m_press_item, MenuItem::None);
    const bool dragged = m_list_drag.Dragging();
    m_list_drag.Release(seconds);
    if (m_bar_press.Active())
    {
      m_bar_press.End();
    }
    else if (m_drag)
    {
      EndDrag();
    }
    else if (dragged)
    {
      // It coasts on.
    }
    else if (HasPopup() && item != MenuItem::PopupOption && item != MenuItem::Popup)
    {
      // Beside the menu: it closes.
      ClosePopup();
    }
    else if (m_menu.autosave_list && item != MenuItem::AutosaveEntry &&
             item != MenuItem::AutosaveList)
    {
      m_menu.autosave_list = false;
    }
    else if (usable && item == hovered->item && m_press_index == index)
    {
      // A copy: the menu changes under it.
      const MenuWidget widget = *hovered;
      Activate(widget, option);
    }
  }
  m_menu.pressed = down ? m_press_item : MenuItem::None;

  if (!scroll)
  {
    m_stick_scroll.Reset();
    m_list_drag.Stop();
    return;
  }
  if (!m_bar_press.Active() && !m_list_drag.Dragging() && !m_drag)
  {
    const float left = controllers[0].thumbstick_y;
    const float right = controllers[1].thumbstick_y;
    const float stick = std::abs(left) > std::abs(right) ? left : right;
    *scroll = m_stick_scroll.Update(stick, dt, *scroll, list->bar.max_scroll, list->bar.list.H(),
                                    list->row_h);
    if (m_stick_scroll.Active())
      m_list_drag.Stop();
    *scroll = m_list_drag.Coast(*scroll, list->bar.max_scroll, dt);
  }
  *scroll = std::clamp(*scroll, 0.0f, list->bar.max_scroll);
}

void VrUi::SetSlider(const MenuWidget& widget, float t)
{
  const auto id = static_cast<SettingId>(widget.index);
  const float value = SliderValue(GetSettingRow(id), t);
  if (value == m_menu.settings.Get(id))
    return;
  m_drag_changed = true;
  if (id == SettingId::Distance || id == SettingId::Size)
  {
    ScreenPlacement placement = m_manager->GetScreenPlacement();
    (id == SettingId::Distance ? placement.distance : placement.height) = value;
    m_manager->SetScreenPlacement(placement);
    SetLiveSettings();
    return;
  }
  ChangeSetting(id, value, false);
}

void VrUi::ActivateSetting(const MenuWidget& widget, int option)
{
  const auto id = static_cast<SettingId>(widget.index);
  const SettingRow& row = GetSettingRow(id);
  switch (row.kind)
  {
  case RowKind::Switch:
    ChangeSetting(id, widget.active ? 0.0f : 1.0f);
    break;
  case RowKind::Segmented:
    if (option >= 0 && option != widget.selected)
      ChooseOption(row, option);
    break;
  case RowKind::Dropdown:
    m_menu.open_setting = id;
    break;
  case RowKind::Slider:
  case RowKind::Header:
    break;
  }
}

void VrUi::ChooseOption(const SettingRow& row, int option)
{
  const std::vector<SettingOption> options = GetOptions(row, m_menu.settings);
  if (option < 0 || option >= static_cast<int>(options.size()))
    return;
  const float value = options[static_cast<std::size_t>(option)].value;
  if (row.id == SettingId::Display)
  {
    QueueSwitchMode(value == static_cast<float>(OpenXRPresentationMode::Immersive),
                    m_menu.settings.scope);
    QueueReadSettings(++m_settings_serial);
  }
  else
  {
    ChangeSetting(row.id, value);
  }
}

void VrUi::ChangeMod(const MenuWidget& widget)
{
  const auto index = static_cast<std::size_t>(widget.index);
  if (index >= m_menu.mods.size())
    return;
  GameMods::Item& item = m_menu.mods[index];
  const GameMods::List list =
      m_menu.mods_list == 0 ? GameMods::List::Mods : GameMods::List::Textures;
  const std::string& game = SConfig::GetInstance().GetGameID();
  if (widget.item == MenuItem::ModGet)
  {
    m_watch_install = GameMods::StartInstall(list, game, item.id);
    m_menu.mods_progress = GameMods::GetProgress();
    return;
  }
  const bool remove = widget.item == MenuItem::ModRemove;
  const bool on = !remove && item.state != GameMods::State::On;
  item.state = remove ? GameMods::State::Available :
               on     ? GameMods::State::On :
                        GameMods::State::Off;
  m_menu.mods_changed = true;
  // What was asked for is what the list shows, then what there is.
  Core::QueueHostJob([list, game, id = item.id, remove, on](Core::System&) {
    const GameMods::Result result =
        remove ? GameMods::Remove(list, game, id) : GameMods::SetEnabled(list, game, id, on);
    if (!result.ok)
      VrUi::PostToast(result.error, 5000);
    ++s_mods_changed;
  });
}

void VrUi::Activate(const MenuWidget& widget, int option)
{
  PadEvents events;
  switch (widget.item)
  {
  case MenuItem::TabMain:
    m_menu.tab = MenuTab::Main;
    m_menu.page = MenuPage::Main;
    break;
  case MenuItem::TabPosition:
    m_menu.tab = MenuTab::Position;
    m_menu.page = MenuPage::Main;
    break;
  case MenuItem::TabStats:
    m_menu.tab = MenuTab::Stats;
    m_menu.page = MenuPage::Main;
    m_menu.stats = GetStatsGroups(Perf::GetStats());
    break;
  case MenuItem::TabControls:
    m_menu.tab = MenuTab::Controls;
    m_menu.page = MenuPage::Main;
    break;
  case MenuItem::TabMods:
    m_menu.tab = MenuTab::Mods;
    m_menu.page = MenuPage::Main;
    GameMods::RefreshCatalogAsync();
    RequestMods();
    break;
  case MenuItem::TabSettings:
    m_menu.tab = MenuTab::Settings;
    m_menu.page = MenuPage::Main;
    break;
  case MenuItem::ModsList:
    if (option >= 0 && option != m_menu.mods_list)
    {
      m_menu.mods_list = option;
      m_menu.mods_page = 0;
      m_menu.mods_ready = false;
      m_menu.mods.clear();
      RequestMods();
    }
    break;
  case MenuItem::ModsPrevious:
    m_menu.mods_page = std::max(m_menu.mods_page - 1, 0);
    break;
  case MenuItem::ModsNext:
    ++m_menu.mods_page;
    break;
  case MenuItem::ModCancel:
    GameMods::CancelInstall();
    break;
  case MenuItem::ModSwitch:
  case MenuItem::ModGet:
  case MenuItem::ModRemove:
    ChangeMod(widget);
    break;
  case MenuItem::Resume:
    CloseMenu();
    break;
  case MenuItem::Home:
    CloseMenu();
    // After the game runs again.
    Core::QueueHostJob([](Core::System&) { Common::VR::OpenXRInputState::PressHome(); });
    break;
  case MenuItem::ResetScreen:
    events.reset_screen = true;
    HandleEvents(events);
    break;
  case MenuItem::QuickSave:
    events.quick_save = true;
    HandleEvents(events);
    CloseMenu();
    break;
  case MenuItem::QuickLoad:
    events.quick_load = true;
    HandleEvents(events);
    CloseMenu();
    break;
  case MenuItem::FastForward:
    SetFastForward(!m_fast_forward);
    break;
  case MenuItem::SaveState:
    m_menu.page = MenuPage::Save;
    RequestSaves();
    break;
  case MenuItem::LoadState:
    m_menu.page = MenuPage::Load;
    RequestSaves();
    break;
  case MenuItem::Back:
    m_menu.page = MenuPage::Main;
    break;
  case MenuItem::Slot:
    if (m_menu.page == MenuPage::Save)
      QueueSave(GetSlotPath(widget.index));
    else
      LoadState(GetSlotPath(widget.index), widget.label, 0);
    CloseMenu();
    break;
  case MenuItem::AutosaveCard:
  case MenuItem::AutosaveEntry:
    if (widget.item == MenuItem::AutosaveCard && m_menu.saves.autosaves.size() > 1)
    {
      m_menu.autosave_list = true;
      break;
    }
    if (const auto at = static_cast<std::size_t>(widget.index); at < m_menu.saves.autosaves.size())
    {
      LoadState(m_menu.saves.autosaves[at].path, "Autosave", 0);
      CloseMenu();
    }
    break;
  case MenuItem::AutosaveSettings:
    m_menu.tab = MenuTab::Settings;
    m_menu.page = MenuPage::Main;
    m_menu.section = MenuSection::General;
    break;
  case MenuItem::UndoLoad:
    m_menu.undo_load.clear();
    Core::QueueHostJob([](Core::System& system) {
      State::UndoLoadState(system);
      VrUi::PostToast("Load Undone", 2000);
    });
    CloseMenu();
    break;
  case MenuItem::Restart:
    CloseMenu();
    Core::QueueHostJob(
        [](Core::System& system) { system.GetProcessorInterface().ResetButton_Tap(); });
    break;
  case MenuItem::Screenshot:
    CloseMenu();
    Core::QueueHostJob([](Core::System&) { Core::SaveScreenShot(); });
    break;
  case MenuItem::ControlsPage:
    if (option >= 0)
      m_menu.controls_page = option;
    break;
  case MenuItem::Controller:
    m_menu.open_controller = true;
    break;
  case MenuItem::EditControls:
    OpenEditControls();
    break;
  case MenuItem::EditChip:
    m_edit.Choose(static_cast<PadTarget>(widget.index));
    m_menu.edit = m_edit.GetView();
    break;
  case MenuItem::EditReset:
    Core::QueueHostJob([kind = m_edit.GetKind()](Core::System&) { ResetPadBindings(kind); });
    RequestBindings();
    break;
  case MenuItem::EditDone:
    m_menu.page = MenuPage::Main;
    break;
  case MenuItem::EditCancel:
    break;
  case MenuItem::Stop:
    events.stop = true;
    HandleEvents(events);
    CloseMenu();
    break;
  case MenuItem::SwitchToFlat:
    CloseMenu();
    Core::QueueHostJob([](Core::System&) { RequestSwitchToFlat(); });
    break;
  case MenuItem::Section:
    if (option >= 0)
      m_menu.section = static_cast<MenuSection>(static_cast<int>(MenuSection::General) + option);
    break;
  case MenuItem::Setting:
    ActivateSetting(widget, option);
    break;
  case MenuItem::Scope:
    if (option >= 0)
      m_menu.settings.scope = static_cast<SettingScope>(option);
    break;
  case MenuItem::ScopeMore:
    m_menu.open_scope = true;
    break;
  case MenuItem::PopupOption:
    if (std::exchange(m_menu.open_scope, false))
      UseGlobalValues();
    else if (std::exchange(m_menu.open_controller, false))
      ChooseController(widget.index);
    else if (const SettingId id = std::exchange(m_menu.open_setting, SettingId::None);
        id != SettingId::None)
    {
      ChooseOption(GetSettingRow(id), widget.index);
    }
    break;
  case MenuItem::None:
  case MenuItem::Scrollbar:
  case MenuItem::Popup:
  case MenuItem::AutosaveList:
    break;
  }
  UpdatePause();
}

// ---- Beams --------------------------------------------------------------------------------------

void VrUi::UpdateBeams(const Frame& frame, const Controllers& controllers)
{
  const PanelQuad menu = GetMenuQuad();
  const PanelQuad screen = frame.has_screen ? m_manager->GetScreenQuad() : PanelQuad{};
  const bool wii = Core::System::GetInstance().IsWii();
  for (std::size_t hand = 0; hand < 2; ++hand)
  {
    const auto& c = controllers[hand];
    Beam& beam = m_beams[hand];
    const Vis::HandInput input{c.aim_pose.valid, ToPose(c.aim_pose)};
    beam.activity.Update(frame.time_ns, input, IsUsed(c));
    beam.visible = false;
    beam.dot = false;
    if (!beam.activity.Tracked() || !frame.head_valid)
      continue;

    const Posef aim = beam.activity.Pose();
    const Ray ray = RayFromPose(aim);
    beam.start = aim.position;
    if (m_menu_open)
    {
      const QuadHit hit = IntersectQuad(ray, menu);
      beam.visible = true;
      beam.dot = hit.in_bounds;
      beam.end = hit.in_bounds ? hit.point_world : ray.origin + ray.direction * STUB_LENGTH;
      continue;
    }

    // The stats are in front of the screen.
    if (m_stats_shown && (g_ActiveConfig.vr_pointer_beams || m_stats_hand == static_cast<int>(hand)))
    {
      const QuadHit hit = IntersectQuad(ray, m_stats_window.GetQuad());
      if (hit.in_bounds)
      {
        beam.visible = true;
        beam.dot = true;
        beam.end = hit.point_world;
        continue;
      }
    }

    const bool grabbing = m_grab_hand == static_cast<int>(hand);
    if (!screen.visible || (!g_ActiveConfig.vr_pointer_beams && !grabbing))
      continue;
    const QuadHit hit = IntersectQuad(ray, screen);
    if (!hit.in_bounds && !grabbing)
      continue;
    beam.visible = true;
    beam.end = hit.on_plane ? hit.point_world : ray.origin + ray.direction * STUB_LENGTH;
    // A Wii game draws its own pointer where the right controller points.
    beam.dot = hit.in_bounds && !(wii && hand == 1 && !grabbing);
  }
}

// ---- Toasts -------------------------------------------------------------------------------------

void VrUi::DrawToastRow(int row, const std::string& text)
{
  constexpr float width = static_cast<float>(TOAST_TEX_WIDTH);
  std::vector<std::string> lines;
  const ToastFit::Box box = ToastFit::Fit(ui::TextWidth(ui::kFontLarge, text.c_str()), [&] {
    lines = ui::WrapText(ui::kFontSmall, text, width - 2.0f * ToastFit::kPadPx, 2);
    float widest = 0.0f;
    for (const std::string& line : lines)
      widest = std::max(widest, ui::TextWidth(ui::kFontSmall, line.c_str()));
    return widest;
  });

  const std::size_t stride = TOAST_TEX_WIDTH;
  const std::size_t first = static_cast<std::size_t>(row) * ToastFit::kRowPx * stride;
  std::fill_n(m_toast_canvas.px.begin() + static_cast<std::ptrdiff_t>(first),
              ToastFit::kRowPx * stride, 0u);
  const float top = static_cast<float>(row * ToastFit::kRowPx);
  const float right = static_cast<float>(box.box_px);
  m_toast_canvas.RoundRect(2, top + 2, right - 2, top + ToastFit::kRowPx - 2, 36,
                           ui::rgb(28, 28, 34, 0.92f));
  const ui::Color white = ui::rgb(240, 240, 245);
  if (!box.two_lines)
  {
    m_toast_canvas.Text(right / 2.0f, top + 62, ui::kFontLarge, text.c_str(), white, 1);
  }
  else if (lines.size() == 1)
  {
    m_toast_canvas.Text(right / 2.0f, top + 58, ui::kFontSmall, lines[0].c_str(), white, 1);
  }
  else if (lines.size() >= 2)
  {
    m_toast_canvas.Text(right / 2.0f, top + 40, ui::kFontSmall, lines[0].c_str(), white, 1);
    m_toast_canvas.Text(right / 2.0f, top + 78, ui::kFontSmall, lines[1].c_str(), white, 1);
  }
  m_toast_boxes[static_cast<std::size_t>(row)] = box;
  m_toasts_dirty = true;
}

void VrUi::DrawGrabHint()
{
  m_toast_boxes[HINT_ROW] = UI::DrawGrabHint(&m_toast_canvas, HINT_ROW);
  m_toasts_dirty = true;
}

void VrUi::UploadStats()
{
  const StatsPicture picture = m_stats_front ? StatsPicture::Front :
                               m_stats_wrist ? StatsPicture::Wrist :
                                               StatsPicture::None;
  const u32 serial = picture == StatsPicture::None ? 0 : Perf::GetStatsSerial();
  if (serial != 0 && !m_stats_overlay && !m_stats_overlay_failed)
  {
    m_stats_overlay = m_create_overlay(STATS_TEX_WIDTH, STATS_TEX_HEIGHT);
    m_stats_overlay_failed = !m_stats_overlay;
  }
  if (!m_stats_overlay)
    return;
  if (serial != 0 && (serial != m_stats_serial || picture != m_stats_asked))
  {
    Perf::Stats stats = Perf::GetStats();
    m_stats_serial = stats.serial;
    m_stats_asked = picture;
    m_stats_waiting = true;
    {
      std::lock_guard lock(m_draw_mutex);
      m_stats_request.emplace(std::move(stats), picture);
    }
    m_draw_cv.notify_one();
  }
  if (m_stats_waiting)
  {
    std::lock_guard lock(m_draw_mutex);
    if (m_stats_drawn_picture != StatsPicture::None)
    {
      m_stats_overlay->Upload(reinterpret_cast<const u8*>(m_stats_drawn.data()));
      m_stats_queued = std::exchange(m_stats_drawn_picture, StatsPicture::None);
      m_stats_waiting =
          m_stats_drawn_serial != m_stats_serial || m_stats_queued != m_stats_asked;
    }
  }
  m_stats_overlay->Poll();
  if (m_stats_overlay->HasImage() && !m_stats_overlay->IsBusy())
    m_stats_image = m_stats_queued;
}

void VrUi::ShowToast(std::string text, s64 duration_ns)
{
  if (!m_frame.head_valid || text.empty())
    return;
  DrawToastRow(TOAST_ROW, text);
  const float distance = NearerThanScreen(TOAST_DISTANCE);
  m_toast_scale = distance / TOAST_DISTANCE;
  m_toast_pose = PlaceAhead(distance, TOAST_DROP * m_toast_scale);
  m_toast_until_ns = m_frame.time_ns + duration_ns;
}

void VrUi::UpdateToasts(const Frame& frame)
{
  std::optional<std::pair<std::string, u32>> posted;
  {
    std::lock_guard lock(s_toast_mutex);
    posted = std::exchange(s_toast, std::nullopt);
    if (!posted && s_next_toast_ready && m_toast_until_ns == 0 && !m_menu_open)
      posted = std::exchange(s_next_toast, std::nullopt);
  }
  if (posted && g_ActiveConfig.vr_toasts)
  {
    const s64 duration = std::clamp<s64>(s64{posted->second} * 1'000'000, ToastFit::kShortNs,
                                         ToastFit::kLongNs);
    ShowToast(std::move(posted->first), duration);
  }
  if (frame.time_ns >= m_toast_until_ns)
    m_toast_until_ns = 0;

  if (m_hint != m_hint_drawn && m_hint != Hint::None)
  {
    m_hint_drawn = m_hint;
    if (m_hint == Hint::Grab)
      DrawGrabHint();
    else
      DrawToastRow(HINT_ROW, "Stopping...");
  }
}

// ---- Layers -------------------------------------------------------------------------------------

void VrUi::AddQuad(Group group, const Overlay& overlay, const Posef& pose, float width,
                   float height, XrRect2Di rect)
{
  SlotQuad quad;
  quad.layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
  quad.layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
  quad.layer.subImage.swapchain = overlay.GetSwapchain();
  quad.layer.subImage.imageRect = rect;
  quad.layer.pose = ToXr(pose);
  quad.layer.size = {width, height};
  m_slots.Add(group, reinterpret_cast<u64>(overlay.GetSwapchain()), rect.extent.width,
              rect.extent.height, quad);
}

void VrUi::BuildLayers(XrSpace space, std::size_t game_layers,
                       std::vector<XrCompositionLayerBaseHeader*>* layers)
{
  if (!m_art && !m_overlays_failed)
  {
    m_art = m_create_overlay(ART_SIZE, ART_SIZE);
    m_toasts = m_create_overlay(TOAST_TEX_WIDTH, TOAST_TEX_HEIGHT);
    if (m_art && m_toasts)
      m_art->Upload(MakeArt().data());
    else
      m_overlays_failed = true;
  }
  if (m_overlays_failed)
    return;

  // ---- Uploads ----
  if (m_toasts_dirty)
  {
    m_toasts->Upload(reinterpret_cast<const u8*>(m_toast_canvas.px.data()));
    m_toasts_dirty = false;
  }
  if (m_menu_open && !m_menu_overlay && !m_menu_overlay_failed)
  {
    m_menu_overlay = m_create_overlay(MENU_TEX_WIDTH, MENU_TEX_HEIGHT);
    m_menu_overlay_failed = !m_menu_overlay;
  }
  if (m_menu_overlay)
  {
    if (m_menu_open)
      RequestMenuDraw();
    std::lock_guard lock(m_draw_mutex);
    if (m_drawn_ready)
    {
      m_menu_overlay->Upload(reinterpret_cast<const u8*>(m_drawn.data()));
      m_drawn_ready = false;
    }
  }
  m_art->Poll();
  m_toasts->Poll();
  if (m_menu_overlay)
    m_menu_overlay->Poll();
  UploadStats();

  // ---- Quads, in drawing order ----
  m_slots.Begin(static_cast<u32>(game_layers) * 2 + (m_menu_open ? 1 : 0));

  if (m_menu_open && m_menu_overlay && m_menu_overlay->HasImage())
  {
    const PanelQuad quad = GetMenuQuad();
    AddQuad(Group::Ui, *m_menu_overlay, quad.pose, quad.width_m, quad.height_m,
            {{0, 0}, {static_cast<s32>(MENU_TEX_WIDTH), static_cast<s32>(MENU_TEX_HEIGHT)}});
  }

  m_stats_shown = m_stats_front && m_stats_image == StatsPicture::Front;
  if (m_stats_shown)
  {
    const PanelQuad quad = m_stats_window.GetQuad();
    AddQuad(Group::Ui, *m_stats_overlay, quad.pose, quad.width_m, quad.height_m,
            {{0, 0}, {static_cast<s32>(STATS_TEX_WIDTH), static_cast<s32>(STATS_TEX_HEIGHT)}});
  }
  else if (m_stats_wrist && m_stats_image == StatsPicture::Wrist)
  {
    constexpr float height = Vis::kWristWidth * STATS_WRIST_HEIGHT / STATS_WIDTH;
    AddQuad(Group::Ui, *m_stats_overlay, m_wrist_pose, Vis::kWristWidth, height,
            {{0, 0}, {static_cast<s32>(STATS_TEX_WIDTH), STATS_TEX_WRIST_HEIGHT}});
  }

  if (m_toasts->HasImage() && !m_toasts->IsBusy())
  {
    const auto row_rect = [this](int row) -> XrRect2Di {
      return {{0, row * ToastFit::kRowPx},
              {m_toast_boxes[static_cast<std::size_t>(row)].box_px, ToastFit::kRowPx}};
    };
    if (m_toast_until_ns != 0)
    {
      const ToastFit::Box& box = m_toast_boxes[TOAST_ROW];
      AddQuad(Group::Toast, *m_toasts, m_toast_pose, box.width_m * m_toast_scale,
              box.height_m * m_toast_scale, row_rect(TOAST_ROW));
    }
    if (m_hint != Hint::None && m_hint == m_hint_drawn && m_frame.head_valid)
    {
      const ToastFit::Box& box = m_toast_boxes[HINT_ROW];
      Posef pose;
      float scale;
      if (m_hint == Hint::Grab)
      {
        // Just under the screen, the same size to the eye at any distance.
        const PanelQuad screen = m_manager->GetScreenQuad();
        scale = Length(screen.pose.position - m_frame.head.position) / TOAST_DISTANCE;
        const float below = screen.height_m * 0.5f + box.height_m * scale;
        pose = {screen.pose.orientation,
                screen.pose.position + Rotate(screen.pose.orientation, {0.0f, -below, 0.0f})};
      }
      else
      {
        const float distance = NearerThanScreen(TOAST_DISTANCE);
        scale = distance / TOAST_DISTANCE;
        pose = PlaceAhead(distance, TOAST_DROP * scale);
      }
      AddQuad(Group::Hint, *m_toasts, pose, box.width_m * scale, box.height_m * scale,
              row_rect(HINT_ROW));
    }
  }

  if (m_art->HasImage())
  {
    for (const Beam& beam : m_beams)
    {
      const std::optional<int> level = Vis::BeamLevel(beam.activity.Beam());
      if (!beam.visible || !level)
        continue;
      if (const auto quad = Vis::PlaceBeam(beam.start, beam.end, m_frame.head, BEAM_WIDTH))
      {
        AddQuad(Group::Top, *m_art, quad->pose, quad->width, quad->length,
                {{*level * Vis::kBeamTexWidth, 0}, {Vis::kBeamTexWidth, Vis::kBeamTexHeight}});
      }
    }
    for (const Beam& beam : m_beams)
    {
      if (!beam.visible || !beam.dot || !Vis::BeamLevel(beam.activity.Beam()))
        continue;
      const float distance = Length(beam.end - m_frame.head.position);
      const float size = 0.01f + 0.003f * distance;
      const Posef pose{FaceViewer(beam.end, m_frame.head.position, {0.0f, 1.0f, 0.0f}), beam.end};
      AddQuad(Group::Top, *m_art, pose, size, size, {{DOT_X, 0}, {DOT_PIXELS, DOT_PIXELS}});
    }
  }

  const std::size_t room = m_max_layers > game_layers + 1 ? m_max_layers - game_layers - 1 : 0;
  m_slots.End(room, m_slot_layers, [](SlotQuad& quad) {
    quad.layer.size.width *= FrameXr::LayerSlots::kCollapse;
    quad.layer.size.height *= FrameXr::LayerSlots::kCollapse;
  });

  m_layers.clear();
  m_layers.reserve(m_slot_layers.size());
  for (const auto& out : m_slot_layers)
  {
    if (m_layers.size() >= room)
      break;
    m_layers.push_back(out.quad.layer);
    m_layers.back().space = space;
  }
  for (XrCompositionLayerQuad& layer : m_layers)
    layers->push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
}

// ---- The menu's drawing thread ------------------------------------------------------------------

void VrUi::RequestMenuDraw()
{
  if (m_last_drawn == m_menu)
    return;
  m_last_drawn = m_menu;
  {
    std::lock_guard lock(m_draw_mutex);
    m_draw_request = m_menu;
  }
  m_draw_cv.notify_one();
}

void VrUi::DrawThread()
{
  FrameXr::Menu::Kit kit(MENU_PX_PER_DP);
  FrameXr::Menu::Canvas canvas;
  canvas.Init(static_cast<int>(MENU_TEX_WIDTH), static_cast<int>(MENU_TEX_HEIGHT));
  FrameXr::Menu::Canvas stats_canvas;
  std::map<std::string, std::optional<MenuPicture>> pictures;

  std::unique_lock lock(m_draw_mutex);
  while (true)
  {
    m_draw_cv.wait(lock, [this] {
      return m_draw_exit || m_saves_request || m_mods_request || m_stats_request ||
             m_draw_request.has_value();
    });
    if (m_draw_exit)
      return;
    if (m_saves_request)
    {
      m_saves_request = false;
      lock.unlock();
      MenuSaves saves = ReadSaves();
      pictures.clear();
      lock.lock();
      m_saves = std::move(saves);
      continue;
    }
    if (m_mods_request)
    {
      const GameMods::List list = *std::exchange(m_mods_request, std::nullopt);
      lock.unlock();
      const SConfig& config = SConfig::GetInstance();
      std::vector<GameMods::Item> items =
          GameMods::GetItems(list, config.GetGameID(), config.GetRevision());
      lock.lock();
      m_mods.emplace(list, std::move(items));
      continue;
    }
    if (const auto request = std::exchange(m_stats_request, std::nullopt))
    {
      lock.unlock();
      const auto& [stats, picture] = *request;
      if (stats_canvas.px.empty())
        stats_canvas.Init(static_cast<int>(STATS_TEX_WIDTH), static_cast<int>(STATS_TEX_HEIGHT));
      if (picture == StatsPicture::Front)
        DrawStats(&kit, &stats_canvas, stats);
      else
        DrawWristStats(&kit, &stats_canvas, stats);
      lock.lock();
      m_stats_drawn = stats_canvas.px;
      m_stats_drawn_picture = picture;
      m_stats_drawn_serial = stats.serial;
      continue;
    }
    const MenuState state = std::move(*m_draw_request);
    m_draw_request.reset();
    lock.unlock();
    VrMenu::Draw(&kit, &canvas, state, [&pictures](const std::string& path) {
      const auto it = pictures.try_emplace(path).first;
      if (!it->second)
        it->second = ReadPicture(path);
      return it->second->width > 0 ? &*it->second : nullptr;
    });
    lock.lock();
    m_drawn = canvas.px;
    m_drawn_ready = true;
  }
}
}  // namespace VR::UI

#endif  // ENABLE_VR
