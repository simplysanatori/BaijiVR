// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef ENABLE_VR

#include "VideoCommon/VR/FrameUI/VrUi.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <deque>
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
#include "Core/GameSettingScope.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/Wiimote.h"
#include "Core/ModeSwitch.h"
#include "Core/NetPlayProto.h"
#include "Core/State.h"
#include "Core/System.h"
#include "Core/VRWiiController.h"

#include "InputCommon/InputConfig.h"

#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/VR/FrameUI/GamesHost.h"
#include "VideoCommon/VR/FrameUI/GrabHint.h"
#include "VideoCommon/VR/FrameUI/PadBindings.h"
#include "VideoCommon/VR/FrameUI/cover_art.h"
#include "VideoCommon/VR/FrameUI/games_tab.h"
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


std::atomic<bool> s_paused_by_menu{false};
std::mutex s_toast_mutex;
struct PostedToast
{
  std::string text;
  u32 ms = 0;
  bool brief = false;
};
std::optional<PostedToast> s_toast;
// After the next toast that is posted.
std::optional<PostedToast> s_next_toast;
bool s_next_toast_ready = false;

// After the one that is shown.
void PostNextToast(std::string text, u32 ms)
{
  std::lock_guard lock(s_toast_mutex);
  s_next_toast = PostedToast{std::move(text), ms};
  s_next_toast_ready = true;
}

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
  PadScope scope = PadScope::Global;
  std::string scope_lock;
  std::vector<std::string> profiles;
  std::string profile;
};
std::atomic<bool> s_bindings_ready{false};
// A state that was to be loaded is not there.
std::atomic<bool> s_load_failed{false};
// Where the page of folders starts.
std::mutex s_files_mutex;
std::optional<std::string> s_files_start;
// Another game was asked for and is on its way.
std::atomic<bool> s_game_starts{false};
// A download has begun.
std::atomic<bool> s_install_started{false};
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

// `save`: with what it changed of every game's settings on the disk, too.
void QueueConfig(std::function<void()> change, bool save = true)
{
  Core::QueueHostJob([change = std::move(change), save](Core::System&) {
    {
      Config::ConfigChangeCallbackGuard guard;
      change();
    }
    if (save)
      GameSettingScope::SaveGlobal();
  });
}

// Once, with the first save state.
void ShowSaveStateNotice()
{
  if (Config::Get(Config::MAIN_SAVE_STATE_NOTICE_SHOWN))
    return;
  Config::SetBaseOrCurrent(Config::MAIN_SAVE_STATE_NOTICE_SHOWN, true);
  std::lock_guard lock(s_toast_mutex);
  s_next_toast = PostedToast{"Save states are NOT a replacement for saving in the game", 5000};
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
  saves.has_quick_save = File::Exists(State::GetQuickSavePath());
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

// The covers of the Games tab, read and composed on the drawing thread as their tiles are drawn.
class Covers
{
public:
  void BeginDraw() { m_wanted.clear(); }

  const FrameXr::Menu::GameIcon* Get(const FrameXr::Menu::GameEntry& game)
  {
    if (game.covers.empty())
      return nullptr;
    const auto it = m_covers.find(game.path);
    if (it != m_covers.end() && it->second.version == game.cover_version &&
        it->second.files == game.covers)
    {
      it->second.drawn = true;
      return it->second.icon.w > 0 ? &it->second.icon : nullptr;
    }
    m_wanted.push_back({game.path, game.covers, game.cover_version});
    // The old one until the new one is there.
    return it != m_covers.end() && it->second.icon.w > 0 ? &it->second.icon : nullptr;
  }

  void EndDraw()
  {
    // What was scrolled away is kept for a while.
    if (m_covers.size() > MAX_COVERS)
      std::erase_if(m_covers, [](const auto& cover) { return !cover.second.drawn; });
    for (auto& [path, cover] : m_covers)
      cover.drawn = false;
    m_since_draw = 0;
  }

  bool IsWaiting() const { return !m_wanted.empty(); }

  // True: time to draw them.
  bool LoadNext()
  {
    Wanted wanted = std::move(m_wanted.front());
    m_wanted.pop_front();
    Cover& cover = m_covers[wanted.path];
    cover = {};
    cover.version = wanted.version;
    for (const std::string& file : wanted.files)
    {
      cover.icon = FrameXr::Menu::ComposeCover(Read(file));
      if (cover.icon.w > 0)
        break;
    }
    cover.files = std::move(wanted.files);
    m_found = m_found || cover.icon.w > 0;
    if (!m_wanted.empty() && ++m_since_draw < A_FEW)
      return false;
    return std::exchange(m_found, false);
  }

private:
  static constexpr std::size_t MAX_COVERS = 64;
  static constexpr int A_FEW = 4;

  struct Cover
  {
    std::vector<std::string> files;
    u32 version = 0;
    FrameXr::Menu::GameIcon icon;
    bool drawn = false;
  };
  struct Wanted
  {
    std::string path;
    std::vector<std::string> files;
    u32 version = 0;
  };

  static FrameXr::Menu::GameIcon Read(const std::string& path)
  {
    FrameXr::Menu::GameIcon icon;
    std::string file;
    Common::UniqueBuffer<u8> data;
    u32 width = 0;
    u32 height = 0;
    if (!File::Exists(path) || !File::ReadFileToString(path, file) ||
        !Common::LoadPNG({reinterpret_cast<const u8*>(file.data()), file.size()}, &data, &width,
                         &height) ||
        width == 0 || height == 0 || width > 4096 || height > 4096)
    {
      return icon;
    }
    icon.w = static_cast<int>(width);
    icon.h = static_cast<int>(height);
    icon.rgba.assign(data.data(), data.data() + std::size_t{width} * height * 4);
    return icon;
  }

  std::map<std::string, Cover> m_covers;
  std::deque<Wanted> m_wanted;
  int m_since_draw = 0;
  bool m_found = false;
};

void QueueFastForward(bool on)
{
  Core::QueueHostJob([on](Core::System&) {
    const float normal = Config::GetBase(Config::MAIN_EMULATION_SPEED);
    Config::SetCurrent(Config::MAIN_EMULATION_SPEED, on ? Config::FAST_FORWARD_SPEED : normal);
  });
}

// What the pacing thread needs of the config, read where the config may be read. Host thread.
std::atomic<bool> s_autosave_on{false};
std::atomic<bool> s_can_switch_mode{true};

void ReadFlags()
{
  s_autosave_on = Config::Get(Config::MAIN_AUTOSAVE_ENABLED);
  const OpenXRPresentationMode mode = Config::Get(Config::GFX_VR_PRESENTATION_MODE);
  s_can_switch_mode =
      mode == OpenXRPresentationMode::Immersive || mode == OpenXRPresentationMode::StereoScreen;
}

// Reads the settings after all that was queued before.
void QueueReadSettings(u32 serial)
{
  Core::QueueHostJob([serial](Core::System&) {
    ReadFlags();
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
    {
      WriteSetting(SettingId::Display, static_cast<float>(mode), *keep, true);
      GameSettingScope::SaveGlobal();
    }
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
  s_game_starts = false;
  m_draw_thread = std::thread(&VrUi::DrawThread, this);
  Core::QueueHostJob([](Core::System& system) {
    ReadFlags();
    VRWiiController::OnSessionStarted(system);
  });
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
  // What the stats measure is for the headset only.
  if (Perf::IsMeasuring())
    Core::QueueHostJob([](Core::System&) { Perf::SetMeasuring(false); });
}

void VrUi::PostToast(std::string text, u32 ms)
{
  std::lock_guard lock(s_toast_mutex);
  s_toast = PostedToast{std::move(text), ms};
  s_next_toast_ready = true;
}

void VrUi::PostBriefToast(std::string text)
{
  std::lock_guard lock(s_toast_mutex);
  s_toast = PostedToast{std::move(text), 2000, true};
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
  else if (message.contains(State::QUICK_SAVE_SUFFIX))
    PostBriefToast(saved ? "Quick Saved" : "Quick Loaded");
  else if (message == "Autosaved")
    PostToast("Autosaved", 2000);
  else if (message.contains(Autosave::FILE_MARK))
    PostBriefToast("Autosave Loaded");
  else if (const int slot = GetSlotOf(message))
    PostBriefToast(fmt::format(fmt::runtime(saved ? "Saved to Slot {}" : "Slot {} Loaded"), slot));
  else
    PostBriefToast(saved ? "State Saved" : "State Loaded");
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
  if (!m_notice_asked && frame.head_valid)
  {
    // The controller the game has been given is told where it is seen.
    m_notice_asked = true;
    Core::QueueHostJob([](Core::System&) {
      if (std::string notice = VRWiiController::TakeNotice(); !notice.empty())
        PostNextToast(std::move(notice), 6000);
    });
  }
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
    QueueSave(State::GetQuickSavePath());
  if (events.quick_load)
    LoadState(State::GetQuickSavePath(), "Quick Save", 0);
  if (events.fast_forward)
    SetFastForward(!m_fast_forward);
  if (events.switch_mode)
    SwitchMode();
  m_stats_to_place |= events.reset_screen;
  if (events.reset_screen && !m_frame.has_screen)
  {
    m_manager->RequestRecenter();
    ShowBriefToast("View Reset");
  }
  else if (events.reset_screen)
  {
    m_manager->ResetScreenPlacement();
    SaveScreenPlacement();
    m_manager->RequestFlatScreenReanchor();
    ShowBriefToast("Screen Reset");
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
  ShowBriefToast(on ? "Fast Forward" : "Normal Speed");
}

void VrUi::SwitchMode()
{
  if (s_can_switch_mode)
    QueueSwitchMode(m_frame.has_screen);
}

void VrUi::LoadState(std::string path, std::string undo_label, s64 time)
{
  m_menu.undo_load = fmt::format("{} Loaded: {}", undo_label,
                                 Autosave::LocalTime(time ? time : std::time(nullptr)));
  Core::QueueHostJob([path = std::move(path)](Core::System& system) {
    if (!File::Exists(path))
    {
      s_load_failed = true;
      VrUi::PostBriefToast(path.ends_with(State::QUICK_SAVE_SUFFIX) ? "No Quick Save" :
                                                                "Save State Not Found");
      return;
    }
    State::LoadAs(system, path);
  });
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
  if (s_load_failed.exchange(false))
    m_menu.undo_load.clear();
  if (s_install_started.exchange(false))
  {
    m_watch_install = true;
    m_last_watch_ns = 0;
  }
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
  m_menu.games.scroll = 0.0f;
  m_menu.games.typing = false;
  ClosePopup();
  m_menu.section = MenuSection::General;
  m_menu.settings.scope = DEFAULT_SCOPE;
  m_menu.scroll.fill(0.0f);
  m_menu.hover = MenuItem::None;
  m_menu.show_tooltip = false;
  m_last_refresh_ns = 0;
  StopScrolling();

  // The autosave is of the moment it opened.
  m_pause_after_ns = s_autosave_on ? frame.time_ns + AUTOSAVE_PICTURE_NS : 0;
  Core::QueueHostJob([](Core::System& system) {
    if (Config::Get(Config::MAIN_AUTOSAVE_ENABLED))
      Autosave::Request(system, Autosave::Reason::MenuOpen);
  });
  RequestBindings();
  RequestSaves();
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
    bindings.scope = GetPadScope(bindings.kind);
    bindings.scope_lock = GetPadScopeLock(bindings.kind);
    bindings.profiles = GetPadProfiles(bindings.kind);
    bindings.profile = GetPadProfile(bindings.kind);
    std::lock_guard lock(s_bindings_mutex);
    s_bindings = std::move(bindings);
    s_bindings_ready = true;
  });
}

void VrUi::ChooseController(int controller)
{
  if (controller == m_menu.controller || controller < 0 ||
      controller >= static_cast<int>(VRWiiController::NAMES.size()))
  {
    return;
  }
  m_menu.controller = controller;
  // The drawing at once; what is on its controls follows.
  m_menu.pad = GetControllerPad(controller);
  m_menu.pad_labels = {};
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

  SettingState& settings = m_menu.settings;
  settings.has_game = !game_id.empty() && game_id != DEFAULT_GAME_ID;
  if (!settings.has_game)
    settings.scope = SettingScope::Global;
  settings.immersive = !m_frame.has_screen;
  settings.can_switch_mode = s_can_switch_mode;
  settings.can_curve = m_manager->CanCurveScreen();
  // A mapping window is another place to change them.
  const u32 mappings = Pad::GetConfig()->GetChanges() + Wiimote::GetConfig()->GetChanges();
  if (std::exchange(m_mappings_seen, mappings) != mappings)
    RequestBindings();
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
  values.game[static_cast<std::size_t>(SettingId::ImmersiveMode)] = m_frame.has_screen ? 0.0f : 1.0f;
}

void VrUi::ChangeSetting(SettingId id, float value, bool save)
{
  m_menu.settings.Set(id, value);
  QueueConfig(
      [id, value, save, scope = m_menu.settings.scope] { WriteSetting(id, value, scope, save); },
      save);
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
  return m_menu.open_setting != SettingId::None || m_menu.open_scope || m_menu.open_pad_scope ||
         m_menu.open_pad_profile || m_menu.games.popup != MenuGames::Popup::None;
}

void VrUi::ClosePopup()
{
  m_menu.open_setting = SettingId::None;
  m_menu.open_scope = false;
  m_menu.open_pad_scope = false;
  m_menu.open_pad_profile = false;
  m_menu.games.popup = MenuGames::Popup::None;
}

void VrUi::ChoosePadProfile(int option)
{
  const std::vector<std::string> options = GetPadProfileOptions(m_menu);
  if (option < 0 || option >= static_cast<int>(options.size()))
    return;
  std::string name = options[static_cast<std::size_t>(option)];
  if (name == MORE_PAD_PROFILES)
  {
    ++m_menu.pad_profile_page;
    m_menu.open_pad_profile = true;
    return;
  }
  if (option == 0 && name == NO_PAD_PROFILE)
    name.clear();
  if (name == m_menu.pad_profile)
    return;
  m_menu.pad_profile = name;
  // It may be of another controller: the drawing follows what comes back.
  Core::QueueHostJob([kind = m_menu.pad, name = std::move(name)](Core::System&) {
    SetPadProfile(kind, name);
  });
  RequestBindings();
}

void VrUi::SetPadScope(SettingScope scope)
{
  if (scope == m_menu.pad_scope || !m_menu.pad_scope_lock.empty())
    return;
  m_menu.pad_scope = scope;
  Core::QueueHostJob([kind = m_menu.pad, scope](Core::System&) {
    UI::SetPadScope(kind, scope == SettingScope::PerGame ? PadScope::Game : PadScope::Global);
  });
  RequestBindings();
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
  if (s_game_starts.exchange(false))
  {
    // The game stays paused until it is stopped.
    CloseMenu();
    return;
  }
  {
    std::optional<std::string> start;
    {
      std::lock_guard lock(s_files_mutex);
      start = std::exchange(s_files_start, std::nullopt);
    }
    if (start && m_menu.page == MenuPage::Files)
      RequestFiles(std::move(*start));
  }
  if (m_menu.tab == MenuTab::Games && GetPublishedGamesSerial() != m_games_seen)
    ReadGames();
  if (s_bindings_ready.exchange(false))
  {
    std::lock_guard lock(s_bindings_mutex);
    m_menu.pad = s_bindings.kind;
    m_menu.pad_labels = s_bindings.labels;
    m_menu.edit_lock = s_bindings.edit_lock;
    m_menu.controller = s_bindings.controller;
    m_menu.can_choose_controller = s_bindings.can_choose;
    m_menu.pad_scope =
        s_bindings.scope == PadScope::Game ? SettingScope::PerGame : SettingScope::Global;
    m_menu.pad_scope_lock = s_bindings.scope_lock;
    m_menu.pad_profiles = s_bindings.profiles;
    m_menu.pad_profile = s_bindings.profile;
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
    if (auto files = std::exchange(m_files, std::nullopt); files && !m_menu.files.wanted.empty())
    {
      m_menu.files.listing = std::move(*files);
      m_menu.files.wanted.clear();
      m_menu.files.scroll = 0.0f;
    }
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
    else if (controllers[1].secondary_button && m_menu.tab == MenuTab::Games && m_menu.games.typing)
    {
      m_menu.games.typing = false;
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
  QuadHit hit = hits[hand];

  m_widgets = VrMenu::Layout(m_menu);
  bool focus_options = false;
  const std::optional<std::pair<float, float>> focus =
      UpdateFocus(controllers, hit, &focus_options);
  m_menu.focus = focus.has_value();
  if (focus)
  {
    hit.on_plane = hit.in_bounds = true;
    hit.u = focus->first / VrMenu::WIDTH;
    hit.v = focus->second / VrMenu::HEIGHT;
  }
  const float x = hit.u * VrMenu::WIDTH;
  const float y = hit.v * VrMenu::HEIGHT;
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
                     ((hand == 1 || focus) && controllers[1].primary_button));
  const bool pressed = down && !m_press_down;
  const bool released = !down && m_press_down;
  m_press_down = down;
  // Not over an open menu.
  m_menu.show_tooltip =
      hovered && !down && !beside && frame.time_ns - m_rest_since_ns >= TOOLTIP_NS;

  if (focus && focus_options && usable && hovered->item == MenuItem::GameTile)
  {
    const auto dots = std::ranges::find_if(m_widgets, [&](const MenuWidget& w) {
      return w.item == MenuItem::GameOptions && w.index == hovered->index;
    });
    if (dots != m_widgets.end())
    {
      const MenuWidget widget = *dots;
      Activate(widget, -1);
      UpdateGames(nullptr, dt);
      return;
    }
  }

  float* const scroll = can_scroll ? &GetScroll(&m_menu, *list) : nullptr;
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
    UpdateGames(nullptr, dt);
    return;
  }
  if (!m_bar_press.Active() && !m_list_drag.Dragging() && !m_drag)
  {
    // On the Games tab it moves the focus.
    const float left = m_menu.tab == MenuTab::Games ? 0.0f : controllers[0].thumbstick_y;
    const float right = controllers[1].thumbstick_y;
    const float stick = std::abs(left) > std::abs(right) ? left : right;
    *scroll = m_stick_scroll.Update(stick, dt, *scroll, list->bar.max_scroll, list->bar.list.H(),
                                    list->row_h);
    if (m_stick_scroll.Active())
      m_list_drag.Stop();
    *scroll = m_list_drag.Coast(*scroll, list->bar.max_scroll, dt);
  }
  *scroll = std::clamp(*scroll, 0.0f, list->bar.max_scroll);
  UpdateGames(&list->bar, dt);
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
    if (id == SettingId::ImmersiveMode)
    {
      QueueSwitchMode(!widget.active, m_menu.settings.scope);
      QueueReadSettings(++m_settings_serial);
      break;
    }
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
    // It looks the mod up in the catalogue's files.
    Core::QueueHostJob([list, game, id = item.id](Core::System&) {
      if (GameMods::StartInstall(list, game, id))
        s_install_started = true;
    });
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

// ---- The Games tab ------------------------------------------------------------------------------

void VrUi::ReadGames()
{
  MenuGames& games = m_menu.games;
  PublishedGames published = GetPublishedGames();
  m_games_seen = published.serial;
  games.has_window = published.serial != 0;
  games.list = std::move(published.list);
  games.running = std::move(published.running);
  if (!games.has_window)
  {
    // The running game is all that is known.
    FrameXr::Menu::GameEntry game;
    game.path = game.file_name = game.game_id = SConfig::GetInstance().GetGameID();
    game.title = m_menu.title;
    game.system = m_menu.wii ? FrameXr::Menu::GameSystem::Wii : FrameXr::Menu::GameSystem::GameCube;
    games.running = game.path;
    games.list = std::make_shared<const FrameXr::Menu::GamesSnapshot>(
        FrameXr::Menu::MakeSnapshot({std::move(game)}, {}));
  }
  games.UpdateShown();
}

void VrUi::OpenGames()
{
  m_menu.tab = MenuTab::Games;
  m_menu.page = MenuPage::Main;
  m_menu.games.typing = false;
  m_menu.games.scroll = 0.0f;
  m_game_pointed.clear();
  ReadGames();
  Core::QueueHostJob([](Core::System&) {
    if (const GamesHost* const host = GetGamesHost())
      host->publish();
  });
}

static bool IsFocusRail(const MenuWidget& w)
{
  return w.item >= MenuItem::TabMain && w.item <= MenuItem::TabSettings && !w.disabled;
}

// The row over the tiles, from the left.
static std::vector<const MenuWidget*> GetFocusTop(const std::vector<MenuWidget>& widgets)
{
  std::vector<const MenuWidget*> top;
  for (const MenuWidget& w : widgets)
  {
    if ((w.item == MenuItem::GamesChip || w.item == MenuItem::GamesSearch ||
         w.item == MenuItem::GamesSearchClear || w.item == MenuItem::GamesSort ||
         w.item == MenuItem::GamesAddFolder) &&
        w.rect.y0 < FrameXr::Menu::kGamesTopY1)
    {
      top.push_back(&w);
    }
  }
  std::ranges::stable_sort(top, {}, [](const MenuWidget* w) { return w->rect.Cx(); });
  return top;
}

static FrameXr::Menu::Focus::Shape GetFocusShape(const std::vector<MenuWidget>& widgets,
                                                 int games, int tile)
{
  namespace Games = FrameXr::Menu;
  Games::Focus::Shape shape;
  shape.columns = Games::kGamesColumns;
  shape.games = games;
  for (const MenuWidget& w : widgets)
  {
    if (!IsFocusRail(w))
      continue;
    if (w.item == MenuItem::TabGames)
      shape.rail_active = shape.rail;
    ++shape.rail;
  }
  const float x = Games::kGamesX0 + Games::kGameTileW * 0.5f +
                  static_cast<float>(tile % Games::kGamesColumns) *
                      (Games::kGameTileW + Games::kGameGap);
  float best = 0.0f;
  for (const MenuWidget* const w : GetFocusTop(widgets))
  {
    const float off = std::max({w->rect.x0 - x, x - w->rect.x1, 0.0f});
    if (shape.top == 0 || off < best)
    {
      best = off;
      shape.above = shape.top;
    }
    ++shape.top;
  }
  return shape;
}

std::optional<std::pair<float, float>>
VrUi::UpdateFocus(const Controllers& controllers, const QuadHit& hit, bool* options)
{
  namespace Games = FrameXr::Menu;
  using Games::Focus::Dir;
  const auto& left = controllers[0];
  const Dir dpad = left.dpad_up    ? Dir::Up :
                   left.dpad_down  ? Dir::Down :
                   left.dpad_left  ? Dir::Left :
                   left.dpad_right ? Dir::Right :
                                     Dir::None;
  const bool enabled = m_menu_armed && m_menu.tab == MenuTab::Games && !m_press_down;
  const Dir dir = m_focus_dir.Update(enabled, dpad, left.thumbstick_x, left.thumbstick_y,
                                     m_frame.time_ns);
  const bool x_down = controllers[1].frame_x_button || left.primary_button;
  *options = enabled && x_down && !m_focus_options_down;
  m_focus_options_down = x_down;
  if (m_menu.tab != MenuTab::Games)
  {
    m_focus = {};
    return std::nullopt;
  }

  MenuGames& games = m_menu.games;
  const bool popup = games.popup != MenuGames::Popup::None;
  const bool grid = m_menu.page == MenuPage::Main && !popup && !games.typing;
  const float x = hit.u * VrMenu::WIDTH;
  const float y = hit.v * VrMenu::HEIGHT;
  const std::optional<MenuList> list = VrMenu::GetList(m_menu);
  const bool was_shown = m_focus.shown;
  if (dir != Dir::None)
  {
    if (grid)
    {
      m_focus.Move(dir, GetFocusShape(m_widgets, static_cast<int>(games.shown.size()),
                                      m_focus.tile));
      if (m_focus.area == Games::Focus::Area::Grid && list)
      {
        StopScrolling();
        const float top = static_cast<float>(m_focus.tile / Games::kGamesColumns) * Games::kGameRow;
        games.scroll =
            Games::Focus::ScrollToShow(games.scroll, top, top + Games::kGameTileH,
                                       list->bar.list.H(), list->bar.max_scroll);
      }
    }
    m_focus.shown = true;
    m_focus_beam.reset();
    if (hit.in_bounds)
      m_focus_beam.emplace(x, y);
    m_widgets = VrMenu::Layout(m_menu);
  }
  else if (m_focus.shown && !m_press_down)
  {
    // The beam points again when it moves (a hand trembles), comes onto the menu or is pulled.
    constexpr float MOVED = 16.0f;
    const bool pulled = controllers[0].trigger_value > TRIGGER_ON ||
                        controllers[1].trigger_value > TRIGGER_ON;
    const bool moved = hit.in_bounds &&
                       (!m_focus_beam || std::hypot(x - m_focus_beam->first,
                                                    y - m_focus_beam->second) > MOVED);
    m_focus.shown = !pulled && !moved;
    if (!hit.in_bounds)
      m_focus_beam.reset();
  }
  if (!m_focus.shown)
    return std::nullopt;

  const MenuWidget* at = nullptr;
  if (grid)
  {
    if (m_focus.area == Games::Focus::Area::Top)
    {
      const std::vector<const MenuWidget*> top = GetFocusTop(m_widgets);
      if (!top.empty())
        at = top[static_cast<std::size_t>(std::clamp(m_focus.top, 0, static_cast<int>(top.size()) - 1))];
    }
    else if (m_focus.area == Games::Focus::Area::Rail)
    {
      int rail = 0;
      for (const MenuWidget& w : m_widgets)
      {
        if (IsFocusRail(w) && rail++ == m_focus.rail)
          at = &w;
      }
    }
    else if (!games.shown.empty())
    {
      m_focus.tile = std::clamp(m_focus.tile, 0, static_cast<int>(games.shown.size()) - 1);
      const int tile = games.shown[static_cast<std::size_t>(m_focus.tile)];
      for (const MenuWidget& w : m_widgets)
      {
        if (w.item == MenuItem::GameTile && w.index == tile)
          at = &w;
      }
    }
  }
  else
  {
    const auto find = [&](MenuItem item, int index) -> const MenuWidget* {
      for (const MenuWidget& w : m_widgets)
      {
        if (w.item == item && w.index == index && VrMenu::CanFocus(w, popup))
          return &w;
      }
      return nullptr;
    };
    at = find(m_focus_on.first, m_focus_on.second);
    if (!at)
    {
      // What it was on is gone: the first row, or else the first there is.
      for (const MenuWidget& w : m_widgets)
      {
        if (VrMenu::CanFocus(w, popup) &&
            (!at || (w.item == MenuItem::FileRow && at->item != MenuItem::FileRow)))
        {
          at = &w;
        }
      }
    }
    else if (dir != Dir::None && was_shown)
    {
      const int dx = dir == Dir::Left ? -1 : dir == Dir::Right ? 1 : 0;
      const int dy = dir == Dir::Up ? -1 : dir == Dir::Down ? 1 : 0;
      const int first = m_menu.files.listing.parent.empty() ? 0 : -1;
      const int index = at->index + dy;
      if (at->item == MenuItem::FileRow && dy != 0 && list && index >= first)
      {
        // Along the list, also where it has to scroll.
        if (index < static_cast<int>(m_menu.files.listing.rows.size()))
        {
          const float row = static_cast<float>(index - first) * list->row_h;
          m_menu.files.scroll =
              Games::Focus::ScrollToShow(m_menu.files.scroll, row, row + list->row_h,
                                         list->bar.list.H(), list->bar.max_scroll);
          m_widgets = VrMenu::Layout(m_menu);
          at = find(MenuItem::FileRow, index);
        }
      }
      else if (const MenuWidget* const next =
                   VrMenu::Next(m_widgets, at->rect.Cx(), at->rect.Cy(), dx, dy))
      {
        at = next;
      }
    }
    if (at)
      m_focus_on = {at->item, at->index};
  }
  if (!at)
    return std::nullopt;
  float at_y = at->rect.Cy();
  if (at->clipped)
    at_y = std::clamp(at_y, at->clip.y0 + 1.0f, at->clip.y1 - 1.0f);
  return std::pair{at->rect.Cx(), at_y};
}

// What is pointed at, and the list at rest.
void VrUi::UpdateGames(const FrameXr::Menu::Scrolling::Bar* bar, float dt)
{
  MenuGames& games = m_menu.games;
  if (m_menu.tab != MenuTab::Games)
    return;
  const bool on_game = games.popup == MenuGames::Popup::None &&
                       (m_menu.hover == MenuItem::GameTile || m_menu.hover == MenuItem::GameOptions);
  const FrameXr::Menu::GameEntry* const game = on_game ? games.Find(m_menu.hover_index) : nullptr;
  const std::string path = game ? game->path : std::string();
  m_game_pointed_s = path == m_game_pointed ? m_game_pointed_s + dt : 0.0f;
  m_game_pointed = path;
  float title_scroll = 0.0f;
  if (game && m_draw_mutex.try_lock())
  {
    for (const auto& [cut, overflow] : m_cut_titles)
    {
      if (cut == path)
        title_scroll = FrameXr::Menu::TitleScroll(m_game_pointed_s, overflow);
    }
    m_draw_mutex.unlock();
    // By the picture's pixels.
    games.title_scroll = std::round(title_scroll * MENU_PX_PER_DP) / MENU_PX_PER_DP;
  }
  else if (!game)
  {
    games.title_scroll = 0.0f;
  }

  if (bar && !m_stick_scroll.Active() && !m_list_drag.Flinging() && !m_list_drag.Dragging() &&
      !m_bar_press.Active() && !m_press_down)
  {
    // Rests on a row's edge.
    const float rest = FrameXr::Menu::GamesRestScroll(games.scroll, bar->max_scroll);
    games.scroll += (rest - games.scroll) * std::min(1.0f, dt * 14.0f);
    if (std::abs(rest - games.scroll) < 0.5f)
      games.scroll = rest;
  }
}

void VrUi::OpenFiles(FilePurpose purpose, std::string game)
{
  m_menu.page = MenuPage::Files;
  m_menu.files = {};
  m_menu.files.purpose = purpose;
  m_menu.files.game = std::move(game);
  m_menu.files.wanted = " ";
  StopScrolling();
  Core::QueueHostJob([purpose](Core::System&) {
    std::vector<std::string> wanted;
    if (purpose == FilePurpose::Folder)
    {
      wanted = Config::GetIsoPaths();
      std::ranges::reverse(wanted);
    }
    else if (const char* const home = std::getenv("HOME"))
    {
      wanted.push_back(std::string(home) + "/Pictures");
    }
    std::string start = GetFileListStart(wanted);
    std::lock_guard lock(s_files_mutex);
    s_files_start = std::move(start);
  });
}

void VrUi::RequestFiles(std::string path)
{
  m_menu.files.wanted = path;
  {
    std::lock_guard lock(m_draw_mutex);
    m_files_request.emplace(std::move(path), m_menu.files.purpose);
    m_files.reset();
  }
  m_draw_cv.notify_one();
}

void VrUi::PressGame(const std::string& path)
{
  MenuGames& games = m_menu.games;
  if (path == games.running)
  {
    CloseMenu();
    return;
  }
  games.game = path;
  games.loses_progress = !s_autosave_on;
  games.popup = MenuGames::Popup::Question;
}

void VrUi::StartGame()
{
  const FrameXr::Menu::GameEntry* const game = m_menu.games.FindGame(m_menu.games.game);
  if (!game)
    return;
  Core::QueueHostJob([path = game->path, title = game->title](Core::System&) {
    const GamesHost* const host = GetGamesHost();
    const std::string refusal = host ? host->play(path) : "Games Start in Baiji VR's Window";
    if (!refusal.empty())
    {
      VrUi::PostToast(refusal, 5000);
      return;
    }
    VrUi::PostToast(fmt::format("Starting {}", title), 5000);
    // It is stopped as it is.
    s_paused_by_menu = false;
    s_game_starts = true;
  });
}

static void QueueCover(const std::string& path, CoverChoice choice, const std::string& image = {})
{
  Core::QueueHostJob([path, choice, image](Core::System&) {
    if (const GamesHost* const host = GetGamesHost())
      host->choose_cover(path, choice, image);
  });
}

void VrUi::ChooseGamesOption(MenuGames::Popup popup, int option)
{
  MenuGames& games = m_menu.games;
  switch (popup)
  {
  case MenuGames::Popup::None:
    break;
  case MenuGames::Popup::Sort:
    games.sort = static_cast<FrameXr::Menu::GamesSort>(option);
    games.scroll = 0.0f;
    games.UpdateShown();
    break;
  case MenuGames::Popup::Options:
    if (option == MenuGames::Play)
    {
      PressGame(std::string(games.game));
    }
    else if (option == MenuGames::Favourite)
    {
      Core::QueueHostJob([path = games.game](Core::System&) {
        if (const GamesHost* const host = GetGamesHost())
          host->toggle_favourite(path);
      });
    }
    else if (option == MenuGames::AnotherCover)
    {
      games.popup = MenuGames::Popup::Covers;
    }
    else if (option == MenuGames::NoCover)
    {
      QueueCover(games.game, CoverChoice::None);
    }
    else if (option == MenuGames::OwnImage)
    {
      OpenFiles(FilePurpose::Image, games.game);
    }
    else if (option == MenuGames::ResetCover)
    {
      QueueCover(games.game, CoverChoice::Default);
    }
    else if (option == MenuGames::Download)
    {
      Core::QueueHostJob([](Core::System&) {
        if (const GamesHost* const host = GetGamesHost())
          host->download_thumbnails();
      });
    }
    break;
  case MenuGames::Popup::Covers:
    QueueCover(games.game, option == 0 ? CoverChoice::Box :
                           option == 1 ? CoverChoice::Title :
                                         CoverChoice::Snap);
    break;
  case MenuGames::Popup::Question:
    if (option == MenuGames::Start)
      StartGame();
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
  case MenuItem::TabGames:
    OpenGames();
    break;
  case MenuItem::GameTile:
  case MenuItem::GameOptions:
    if (const FrameXr::Menu::GameEntry* const game = m_menu.games.Find(widget.index))
    {
      // A copy: the list may be another by then.
      const std::string path = game->path;
      if (widget.item == MenuItem::GameTile)
      {
        PressGame(path);
        break;
      }
      m_menu.games.game = path;
      m_menu.games.popup = MenuGames::Popup::Options;
    }
    break;
  case MenuItem::GamesChip:
    m_menu.games.chip = static_cast<FrameXr::Menu::SystemFilter>(widget.index);
    // A search has All's place: the chosen chip again is All.
    if (widget.active && !m_menu.games.search.empty())
      m_menu.games.chip = FrameXr::Menu::SystemFilter::All;
    m_menu.games.scroll = 0.0f;
    m_menu.games.UpdateShown();
    break;
  case MenuItem::GamesAddFolder:
    OpenFiles(FilePurpose::Folder, {});
    break;
  case MenuItem::FilePlace:
    if (const auto at = static_cast<std::size_t>(widget.index);
        at < m_menu.files.listing.places.size())
    {
      RequestFiles(m_menu.files.listing.places[at].second);
    }
    break;
  case MenuItem::FileRow:
    if (widget.index < 0)
    {
      RequestFiles(m_menu.files.listing.parent);
    }
    else if (const auto at = static_cast<std::size_t>(widget.index);
             at < m_menu.files.listing.rows.size())
    {
      const FileRow& row = m_menu.files.listing.rows[at];
      if (row.is_folder)
      {
        RequestFiles(row.path);
        break;
      }
      QueueCover(m_menu.files.game, CoverChoice::Own, row.path);
      m_menu.page = MenuPage::Main;
    }
    break;
  case MenuItem::FileAdd:
    Core::QueueHostJob([path = m_menu.files.listing.path](Core::System&) {
      if (const GamesHost* const host = GetGamesHost())
        host->add_folder(path);
    });
    m_menu.page = MenuPage::Main;
    break;
  case MenuItem::GamesSearch:
    m_menu.games.typing = true;
    m_menu.games.scroll = 0.0f;
    break;
  case MenuItem::GamesSearchClear:
    m_menu.games.search.clear();
    m_menu.games.scroll = 0.0f;
    m_menu.games.UpdateShown();
    break;
  case MenuItem::GamesKey:
    m_menu.games.Type(static_cast<char>(widget.index));
    break;
  case MenuItem::GamesSort:
    m_menu.games.popup = MenuGames::Popup::Sort;
    break;
  case MenuItem::TabPosition:
    m_menu.tab = MenuTab::Position;
    m_menu.page = MenuPage::Main;
    break;
  case MenuItem::TabStats:
    m_menu.tab = MenuTab::Stats;
    m_menu.page = MenuPage::Main;
    // With Show Stats off: the last second the game ran.
    Perf::PublishLastSecond();
    m_menu.stats = GetStatsGroups(Perf::GetStats());
    break;
  case MenuItem::TabControls:
    m_menu.tab = MenuTab::Controls;
    m_menu.page = MenuPage::Main;
    break;
  case MenuItem::TabMods:
    m_menu.tab = MenuTab::Mods;
    m_menu.page = MenuPage::Main;
    Core::QueueHostJob([](Core::System&) { GameMods::RefreshCatalogAsync(); });
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
      VrUi::PostBriefToast("Load Undone");
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
    ChooseController(widget.index);
    break;
  case MenuItem::ControlsScope:
    // Global is what the menu of the dots has: the game's own controls go with it.
    if (option == 1)
      SetPadScope(SettingScope::PerGame);
    else if (option == 0 && m_menu.pad_scope == SettingScope::PerGame)
      m_menu.open_pad_scope = true;
    break;
  case MenuItem::PadProfile:
    m_menu.open_pad_profile = true;
    m_menu.pad_profile_page = 0;
    break;
  case MenuItem::ControlsMore:
    m_menu.open_pad_scope = true;
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
    else if (std::exchange(m_menu.open_pad_scope, false))
      SetPadScope(SettingScope::Global);
    else if (std::exchange(m_menu.open_pad_profile, false))
      ChoosePadProfile(widget.index);
    else if (const auto popup = std::exchange(m_menu.games.popup, MenuGames::Popup::None);
             popup != MenuGames::Popup::None)
    {
      ChooseGamesOption(popup, widget.index);
    }
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
    bool ready = false;
    {
      std::lock_guard lock(m_draw_mutex);
      if (m_stats_drawn_picture != StatsPicture::None)
      {
        m_stats_upload.swap(m_stats_drawn);
        m_stats_queued = std::exchange(m_stats_drawn_picture, StatsPicture::None);
        m_stats_waiting =
            m_stats_drawn_serial != m_stats_serial || m_stats_queued != m_stats_asked;
        ready = true;
      }
    }
    if (ready)
      m_stats_overlay->Upload(reinterpret_cast<const u8*>(m_stats_upload.data()));
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
  m_toast_corner = false;
}

FrameXr::Vis::Toast::Placed VrUi::GetToastCorner() const
{
  const PanelQuad screen = m_manager->GetScreenQuad();
  const float aspect = static_cast<float>(m_toast_boxes[TOAST_ROW].box_px) / ToastFit::kRowPx;
  return FrameXr::Vis::Toast::OnScreen(screen.pose, screen.width_m, screen.height_m,
                                       screen.curve_radius, aspect);
}

void VrUi::ShowBriefToast(std::string text)
{
  if (!g_ActiveConfig.vr_stereo_screen)
  {
    ShowToast(std::move(text), ToastFit::kShortNs);
    return;
  }
  ShowToast(std::move(text), ToastFit::kBriefNs);
  if (!m_frame.head_valid || m_toast_until_ns == 0 || m_menu_open)
    return;
  const auto eyes = m_manager->LocateEyes();
  m_toast_corner = eyes && FrameXr::Vis::Toast::InView(*eyes, GetToastCorner());
}

void VrUi::UpdateToasts(const Frame& frame)
{
  std::optional<PostedToast> posted;
  {
    std::lock_guard lock(s_toast_mutex);
    posted = std::exchange(s_toast, std::nullopt);
    if (!posted && s_next_toast_ready && m_toast_until_ns == 0 && !m_menu_open && frame.head_valid)
      posted = std::exchange(s_next_toast, std::nullopt);
  }
  if (posted && g_ActiveConfig.vr_toasts)
  {
    const s64 duration = std::clamp<s64>(s64{posted->ms} * 1'000'000, ToastFit::kShortNs,
                                         ToastFit::kLongNs);
    if (posted->brief)
      ShowBriefToast(std::move(posted->text));
    else
      ShowToast(std::move(posted->text), duration);
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
    bool ready = false;
    {
      std::lock_guard lock(m_draw_mutex);
      if (std::exchange(m_drawn_ready, false))
      {
        m_menu_upload.swap(m_drawn);
        ready = true;
      }
    }
    if (ready)
      m_menu_overlay->Upload(reinterpret_cast<const u8*>(m_menu_upload.data()));
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
      // Not on that screen any more, or under the menu: ahead, as it was when it appeared.
      m_toast_corner = m_toast_corner && g_ActiveConfig.vr_stereo_screen && !m_menu_open;
      const FrameXr::Vis::Toast::Placed toast =
          m_toast_corner ? GetToastCorner() :
                           FrameXr::Vis::Toast::Placed{m_toast_pose, box.width_m * m_toast_scale,
                                                       box.height_m * m_toast_scale};
      AddQuad(Group::Toast, *m_toasts, toast.pose, toast.width, toast.height,
              row_rect(TOAST_ROW));
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
  // What is handed over: copied without the lock, swapped under it.
  std::vector<u32> menu_scratch;
  std::vector<u32> stats_scratch;
  std::map<std::string, std::optional<MenuPicture>> pictures;
  Covers covers;
  MenuState last;

  std::unique_lock lock(m_draw_mutex);
  while (true)
  {
    m_draw_cv.wait(lock, [this, &covers] {
      return m_draw_exit || m_saves_request || m_mods_request || m_files_request ||
             m_stats_request || m_draw_request.has_value() || covers.IsWaiting();
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
    if (const auto request = std::exchange(m_files_request, std::nullopt))
    {
      lock.unlock();
      FileListing listing = ReadFileList(request->first, request->second);
      lock.lock();
      // Not when another was asked for meanwhile.
      if (!m_files_request)
        m_files = std::move(listing);
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
      stats_scratch = stats_canvas.px;
      lock.lock();
      m_stats_drawn.swap(stats_scratch);
      m_stats_drawn_picture = picture;
      m_stats_drawn_serial = stats.serial;
      continue;
    }
    if (!m_draw_request)
    {
      // A cover, while there is nothing else to do. They show a few at a time.
      lock.unlock();
      const bool show = covers.LoadNext();
      lock.lock();
      if (show && !m_draw_request && !m_draw_exit)
        m_draw_request = last;
      continue;
    }
    const MenuState state = std::move(*m_draw_request);
    m_draw_request.reset();
    lock.unlock();
    VrMenu::GamesDraw games;
    games.get_cover = [&covers](const FrameXr::Menu::GameEntry& game) { return covers.Get(game); };
    covers.BeginDraw();
    VrMenu::Draw(&kit, &canvas, state, [&pictures](const std::string& path) {
      const auto it = pictures.try_emplace(path).first;
      if (!it->second)
        it->second = ReadPicture(path);
      return it->second->width > 0 ? &*it->second : nullptr;
    }, &games);
    covers.EndDraw();
    last = state;
    menu_scratch = canvas.px;
    std::vector<std::pair<std::string, float>> cut_titles;
    for (const auto& [index, overflow] : games.cut_titles)
    {
      if (const FrameXr::Menu::GameEntry* const game = state.games.Find(index))
        cut_titles.emplace_back(game->path, overflow);
    }
    lock.lock();
    m_cut_titles = std::move(cut_titles);
    m_drawn.swap(menu_scratch);
    m_drawn_ready = true;
  }
}
}  // namespace VR::UI

#endif  // ENABLE_VR
