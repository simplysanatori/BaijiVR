// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

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
#include "Core/HW/ProcessorInterface.h"
#include "Core/NetPlayProto.h"
#include "Core/State.h"
#include "Core/System.h"

#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/VR/FrameUI/PadBindings.h"
#include "VideoCommon/VR/OpenXRManager.h"
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
// The autosave's picture is of the running game.
constexpr s64 AUTOSAVE_PICTURE_NS = 250'000'000;

constexpr float NUDGE_DEG = 0.5f;
constexpr s64 NUDGE_FIRST_NS = 400'000'000;
constexpr s64 NUDGE_REPEAT_NS = 80'000'000;

// Of what Azahar calls turbo speed.
constexpr float FAST_FORWARD_SPEED = 2.0f;

std::atomic<bool> s_paused_by_menu{false};
std::mutex s_toast_mutex;
std::optional<std::pair<std::string, u32>> s_toast;
// After the toast there is.
std::optional<std::pair<std::string, u32>> s_next_toast;
// The save states on disk changed.
std::atomic<u32> s_saves_changed{0};
std::atomic<bool> s_bindings_ready{false};
std::mutex s_bindings_mutex;
std::pair<PadKind, PadLabels> s_bindings;

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
}

void QueueSave(std::string path)
{
  Core::QueueHostJob([path = std::move(path)](Core::System& system) {
    // Says it is saved (PostEmulatorMessage) once it is.
    File::CreateFullPath(path);
    State::SaveAs(system, path);
    ShowSaveStateNotice();
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

template <typename T>
void QueueSet(const Config::Info<T>& info, T value)
{
  QueueConfig([&info, value] { Config::SetBaseOrCurrent(info, value); });
}

void ResumeFromMenu(Core::System& system)
{
  if (s_paused_by_menu.exchange(false) && Core::GetState(system) == Core::State::Paused)
    Core::SetState(system, Core::State::Running);
}

void QueueSwitchMode(bool to_immersive)
{
  Core::QueueHostJob([to_immersive](Core::System&) {
    if (!g_openxr || !g_openxr->GetSwapchain())
      return;
    if (g_openxr->GetSwapchain()->HasFoveatedFramebuffers() ||
        (g_framebuffer_manager && g_framebuffer_manager->IsEFBFoveated()))
    {
      VrUi::PostToast("Turn Foveated Rendering Off to Switch", 5000);
      return;
    }
    if (!to_immersive)
      g_openxr->RequestFlatScreenReanchor();
    Config::SetCurrent(Config::GFX_VR_PRESENTATION_MODE,
                       to_immersive ? OpenXRPresentationMode::Immersive :
                                      OpenXRPresentationMode::StereoScreen);
    VrUi::PostToast(to_immersive ? "Immersive" : "3D Screen", 2000);
  });
}
}  // namespace

VrUi::VrUi(OpenXRManager* manager, CreateOverlay create_overlay, u32 max_layers)
    : m_manager(manager), m_create_overlay(std::move(create_overlay)), m_max_layers(max_layers)
{
  ui::InitFonts();
  m_toast_canvas.Init(static_cast<int>(TOAST_TEX_WIDTH), static_cast<int>(TOAST_TEX_HEIGHT));
  std::fill(m_toast_canvas.px.begin(), m_toast_canvas.px.end(), 0u);
  m_draw_thread = std::thread(&VrUi::DrawThread, this);
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
  return m_menu_open || m_grab_hand >= 0 || m_hint != Hint::None || m_toast_until_ns != 0 ||
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
  UpdateMenu(frame, raw);
  UpdateGrab(frame, raw, dt);

  // With the menu on its Screen tab the game runs, but it does not get the controllers.
  const PadEvents events =
      m_filter.Update(controllers, frame.time_ns, m_menu_open, m_grab_hand);
  HandleEvents(events);

  UpdateBeams(frame, raw);
  UpdateToasts(frame);
}

void VrUi::OnFocusLost()
{
  m_filter.Reset();
  if (m_grab_hand >= 0)
  {
    m_grab_hand = -1;
    SaveScreenPlacement();
  }
  m_grip_down = {};
  m_drag.reset();
  m_press_down = false;
  m_press_item = MenuItem::None;
  m_menu_armed = false;
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
  if (m_menu.can_switch_mode)
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

void VrUi::SaveScreenPlacement()
{
  const ScreenPlacement placement = m_manager->GetScreenPlacement();
  QueueConfig([placement] {
    Config::SetBaseOrCurrent(Config::GFX_VR_SCREEN_DISTANCE, placement.distance);
    Config::SetBaseOrCurrent(Config::GFX_VR_SCREEN_SIZE, placement.height);
    Config::SetBaseOrCurrent(Config::GFX_VR_SCREEN_YAW, placement.yaw_deg);
    Config::SetBaseOrCurrent(Config::GFX_VR_SCREEN_PITCH, placement.pitch_deg);
  });
}

// ---- Moving the screen --------------------------------------------------------------------------

void VrUi::UpdateGrab(const Frame& frame, const Controllers& controllers, float dt)
{
  for (int hand = 0; hand < 2; ++hand)
  {
    const auto& c = controllers[static_cast<std::size_t>(hand)];
    bool& down = m_grip_down[static_cast<std::size_t>(hand)];
    const bool was_down = down;
    down = c.connected && (c.squeeze_value > (was_down ? GRIP_OFF : GRIP_ON) ||
                            (c.squeeze_button && c.squeeze_value <= 0.0f));

    if (m_grab_hand == hand && (!down || !frame.has_screen || !c.aim_pose.valid || m_menu_open))
    {
      m_grab_hand = -1;
      SaveScreenPlacement();
    }
    if (m_grab_hand < 0 && down && !was_down && frame.has_screen && c.aim_pose.valid &&
        !m_menu_open && !g_ActiveConfig.vr_lock_screen)
    {
      const PanelQuad screen = m_manager->GetScreenQuad();
      const Ray ray = RayFromPose(ToPose(c.aim_pose));
      if (screen.visible && IntersectQuad(ray, screen).in_bounds)
      {
        m_grab_hand = hand;
        m_nudge = 0;
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
  m_drag.reset();
  m_menu.tab = MenuTab::Main;
  m_menu.page = MenuPage::Main;
  m_menu.autosave_list = false;
  m_menu.hover = MenuItem::None;
  m_last_refresh_ns = 0;

  // The autosave is of the moment it opened.
  m_pause_after_ns = 0;
  if (Config::Get(Config::MAIN_AUTOSAVE_ENABLED))
  {
    m_pause_after_ns = frame.time_ns + AUTOSAVE_PICTURE_NS;
    Core::QueueHostJob(
        [](Core::System& system) { Autosave::Request(system, Autosave::Reason::MenuOpen); });
  }
  Core::QueueHostJob([](Core::System& system) {
    auto bindings = GetPadBindings(system.IsWii());
    std::lock_guard lock(s_bindings_mutex);
    s_bindings = std::move(bindings);
    s_bindings_ready = true;
  });
  RequestSaves();
  RefreshMenuState();
  UpdatePause();
}

void VrUi::CloseMenu()
{
  if (!m_menu_open)
    return;
  m_menu_open = false;
  m_drag.reset();
  m_press_item = MenuItem::None;
  UpdatePause();
}

void VrUi::UpdatePause()
{
  // The Screen tab's settings show on the running game.
  const bool wanted = m_menu_open && m_menu.tab != MenuTab::Screen &&
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
  const ScreenPlacement placement = m_manager->GetScreenPlacement();
  m_menu.title = SConfig::GetInstance().GetTitleName();
  if (m_menu.title.empty())
    m_menu.title = SConfig::GetInstance().GetGameID();
  m_menu.wii = Core::System::GetInstance().IsWii();
  const OpenXRPresentationMode mode = Config::Get(Config::GFX_VR_PRESENTATION_MODE);
  m_menu.immersive = !m_frame.has_screen;
  m_menu.can_switch_mode =
      mode == OpenXRPresentationMode::Immersive || mode == OpenXRPresentationMode::StereoScreen;
  m_menu.fast_forward = m_fast_forward;
  m_menu.distance = placement.distance;
  m_menu.size = placement.height;
  if (!m_drag || m_drag->item != MenuItem::Depth)
    m_menu.depth = Config::Get(Config::GFX_STEREO_DEPTH);
  if (!m_drag || m_drag->item != MenuItem::Convergence)
    m_menu.convergence = Config::Get(Config::GFX_STEREO_CONVERGENCE);
  switch (Config::Get(Config::GFX_ASPECT_RATIO))
  {
  case AspectMode::Auto:
  case AspectMode::Stretch:
    m_menu.aspect = 0;
    break;
  case AspectMode::ForceWide:
    m_menu.aspect = 1;
    break;
  case AspectMode::ForceStandard:
    m_menu.aspect = 2;
    break;
  default:
    m_menu.aspect = -1;
    break;
  }
  m_menu.lock_screen = Config::Get(Config::GFX_VR_LOCK_SCREEN);
  m_menu.resolution = Config::Get(Config::GFX_EFB_SCALE);
  m_menu.beams = Config::Get(Config::GFX_VR_POINTER_BEAMS);
  m_menu.toasts = Config::Get(Config::GFX_VR_TOASTS);
  m_menu.eager = Config::Get(Config::GFX_VR_EAGER_HEARTBEAT);
  m_menu.autosave = Config::Get(Config::MAIN_AUTOSAVE_ENABLED);
  const auto minutes = static_cast<int>(Config::Get(Config::MAIN_AUTOSAVE_MINUTES));
  m_menu.autosave_every = 0;
  while (m_menu.autosave_every + 1 < static_cast<int>(AUTOSAVE_MINUTES.size()) &&
         AUTOSAVE_MINUTES[static_cast<std::size_t>(m_menu.autosave_every) + 1] <= minutes)
  {
    ++m_menu.autosave_every;
  }
  if (!m_drag || m_drag->item != MenuItem::AutosaveKeep)
  {
    m_menu.autosave_keep =
        std::clamp(static_cast<int>(Config::Get(Config::MAIN_AUTOSAVE_KEEP)), 1, 10);
  }
}

void VrUi::UpdateMenu(const Frame& frame, const Controllers& controllers)
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
  if (s_bindings_ready.exchange(false))
  {
    std::lock_guard lock(s_bindings_mutex);
    std::tie(m_menu.pad, m_menu.pad_labels) = s_bindings;
  }
  if (m_draw_mutex.try_lock())
  {
    if (m_saves)
      m_menu.saves = std::move(*std::exchange(m_saves, std::nullopt));
    m_draw_mutex.unlock();
  }
  m_menu.autosave_list &= m_menu.saves.autosaves.size() > 1;

  const bool back = controllers[1].secondary_button || controllers[1].menu_button ||
                    controllers[0].menu_button;
  const bool back_pressed = back && !m_back_down;
  m_back_down = back;
  if (!m_menu_armed)
  {
    // The press that opened it is not for it.
    m_menu_armed = !back && controllers[0].trigger_value < TRIGGER_OFF &&
                   controllers[1].trigger_value < TRIGGER_OFF && !controllers[1].primary_button;
  }
  else if (back_pressed)
  {
    // Menu closes it; B goes back first.
    if (controllers[1].secondary_button && m_menu.autosave_list)
    {
      m_menu.autosave_list = false;
    }
    else if (controllers[1].secondary_button && m_menu.tab == MenuTab::Main &&
             m_menu.page != MenuPage::Main)
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
  const MenuWidget* const hovered =
      hit.in_bounds && !m_drag ? VrMenu::At(m_widgets, x, y) : nullptr;
  const MenuItem hover = m_drag ? m_drag->item : hovered ? hovered->item : MenuItem::None;
  const int index = m_drag ? m_drag->index : hovered ? hovered->index : 0;
  const int option = hovered ? VrMenu::OptionAt(*hovered, x) : -1;
  if (hover != MenuItem::None && (hover != m_menu.hover || index != m_menu.hover_index))
    m_manager->PulseHaptics(hand);
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

  if (pressed)
  {
    m_press_item = hover;
    m_press_index = index;
    if (hovered && hovered->kind == MenuWidget::Kind::Slider)
      m_drag = *hovered;
  }
  if (m_drag && down && hit.on_plane)
    SetSlider(*m_drag, VrMenu::SliderAt(*m_drag, x));
  if (released)
  {
    const MenuItem item = std::exchange(m_press_item, MenuItem::None);
    if (m_drag)
    {
      if (m_drag->item == MenuItem::Distance || m_drag->item == MenuItem::Size)
        SaveScreenPlacement();
      m_drag.reset();
    }
    else if (m_menu.autosave_list && item != MenuItem::AutosaveEntry &&
             item != MenuItem::AutosaveList)
    {
      // Beside the list: it closes.
      m_menu.autosave_list = false;
    }
    else if (hovered && item == hovered->item && m_press_index == index)
    {
      // A copy: the menu changes under it.
      const MenuWidget widget = *hovered;
      Activate(widget, option);
    }
  }
  m_menu.pressed = down ? m_press_item : MenuItem::None;
}

void VrUi::SetSlider(const MenuWidget& widget, float t)
{
  ScreenPlacement placement = m_manager->GetScreenPlacement();
  switch (widget.item)
  {
  case MenuItem::Distance:
    placement.distance =
        ScreenPlacement::MIN_DISTANCE +
        t * (ScreenPlacement::MAX_DISTANCE - ScreenPlacement::MIN_DISTANCE);
    placement.distance = std::round(placement.distance * 10.0f) / 10.0f;
    m_manager->SetScreenPlacement(placement);
    m_menu.distance = placement.distance;
    break;
  case MenuItem::Size:
    placement.height = ScreenPlacement::MIN_HEIGHT +
                       t * (ScreenPlacement::MAX_HEIGHT - ScreenPlacement::MIN_HEIGHT);
    placement.height = std::round(placement.height * 10.0f) / 10.0f;
    m_manager->SetScreenPlacement(placement);
    m_menu.size = placement.height;
    break;
  case MenuItem::Depth:
  {
    const float value = std::round(t * Config::GFX_STEREO_DEPTH_MAXIMUM);
    if (value != m_menu.depth)
      QueueSet(Config::GFX_STEREO_DEPTH, value);
    m_menu.depth = value;
    break;
  }
  case MenuItem::AutosaveKeep:
  {
    const int value = 1 + static_cast<int>(std::lround(t * 9.0f));
    if (value != m_menu.autosave_keep)
      QueueSet(Config::MAIN_AUTOSAVE_KEEP, static_cast<u32>(value));
    m_menu.autosave_keep = value;
    break;
  }
  case MenuItem::Convergence:
  {
    const float value = std::round(t * Config::GFX_STEREO_CONVERGENCE_MAXIMUM);
    if (value != m_menu.convergence)
      QueueSet(Config::GFX_STEREO_CONVERGENCE, value);
    m_menu.convergence = value;
    break;
  }
  default:
    break;
  }
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
  case MenuItem::TabScreen:
    m_menu.tab = MenuTab::Screen;
    m_menu.page = MenuPage::Main;
    break;
  case MenuItem::TabControls:
    m_menu.tab = MenuTab::Controls;
    m_menu.page = MenuPage::Main;
    break;
  case MenuItem::TabSettings:
    m_menu.tab = MenuTab::Settings;
    m_menu.page = MenuPage::Main;
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
  case MenuItem::Autosave:
    m_menu.autosave = !m_menu.autosave;
    QueueSet(Config::MAIN_AUTOSAVE_ENABLED, m_menu.autosave);
    break;
  case MenuItem::AutosaveEvery:
    if (option >= 0)
    {
      m_menu.autosave_every = option;
      QueueSet(Config::MAIN_AUTOSAVE_MINUTES,
               static_cast<u32>(AUTOSAVE_MINUTES[static_cast<std::size_t>(option)]));
    }
    break;
  case MenuItem::Stop:
    events.stop = true;
    HandleEvents(events);
    CloseMenu();
    break;
  case MenuItem::Mode:
    if (option >= 0 && option != widget.selected)
      QueueSwitchMode(option == 1);
    break;
  case MenuItem::Aspect:
    if (option >= 0)
    {
      static constexpr std::array MODES{AspectMode::Auto, AspectMode::ForceWide,
                                        AspectMode::ForceStandard};
      QueueSet(Config::GFX_ASPECT_RATIO, MODES[static_cast<std::size_t>(option)]);
      m_menu.aspect = option;
    }
    break;
  case MenuItem::LockScreen:
    m_menu.lock_screen = !m_menu.lock_screen;
    QueueSet(Config::GFX_VR_LOCK_SCREEN, m_menu.lock_screen);
    break;
  case MenuItem::Resolution:
    if (option >= 0)
    {
      QueueSet(Config::GFX_EFB_SCALE, option + 1);
      m_menu.resolution = option + 1;
    }
    break;
  case MenuItem::Beams:
    m_menu.beams = !m_menu.beams;
    QueueSet(Config::GFX_VR_POINTER_BEAMS, m_menu.beams);
    break;
  case MenuItem::Toasts:
    m_menu.toasts = !m_menu.toasts;
    QueueSet(Config::GFX_VR_TOASTS, m_menu.toasts);
    break;
  case MenuItem::Pacing:
    if (option >= 0)
    {
      m_menu.eager = option == 0;
      QueueSet(Config::GFX_VR_EAGER_HEARTBEAT, m_menu.eager);
    }
    break;
  case MenuItem::None:
  case MenuItem::Distance:
  case MenuItem::Size:
  case MenuItem::Depth:
  case MenuItem::Convergence:
  case MenuItem::AutosaveKeep:
  case MenuItem::AutosaveList:
    break;
  }
  // What was just set is on its way to the config: do not read the old value back over it.
  m_last_refresh_ns = m_frame.time_ns;
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
    if (!posted && m_toast_until_ns == 0 && !m_menu_open)
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
    DrawToastRow(HINT_ROW,
                 m_hint == Hint::Grab ? "Stick: Distance, Size   D-Pad: Move" : "Stopping...");
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

  // ---- Quads, in drawing order ----
  m_slots.Begin(static_cast<u32>(game_layers) * 2 + (m_menu_open ? 1 : 0));

  if (m_menu_open && m_menu_overlay && m_menu_overlay->HasImage())
  {
    const PanelQuad quad = GetMenuQuad();
    AddQuad(Group::Ui, *m_menu_overlay, quad.pose, quad.width_m, quad.height_m,
            {{0, 0}, {static_cast<s32>(MENU_TEX_WIDTH), static_cast<s32>(MENU_TEX_HEIGHT)}});
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
  std::map<std::string, std::optional<MenuPicture>> pictures;

  std::unique_lock lock(m_draw_mutex);
  while (true)
  {
    m_draw_cv.wait(lock, [this] {
      return m_draw_exit || m_saves_request || m_draw_request.has_value();
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
