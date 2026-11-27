// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

#include <array>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <openxr/openxr.h>

#include "Common/CommonTypes.h"
#include "VideoCommon/VR/FrameUI/Overlay.h"
#include "Common/VR/PadFilter.h"
#include "VideoCommon/VR/FrameUI/ScreenPlacement.h"
#include "VideoCommon/VR/FrameUI/Stats.h"
#include "VideoCommon/VR/FrameUI/VrMenu.h"
#include "VideoCommon/VR/FrameUI/beam.h"
#include "VideoCommon/VR/FrameUI/geometry.h"
#include "VideoCommon/VR/FrameUI/grid_focus.h"
#include "VideoCommon/VR/FrameUI/layer_slots.h"
#include "VideoCommon/VR/FrameUI/list_scroll.h"
#include "VideoCommon/VR/FrameUI/toast_corner.h"
#include "VideoCommon/VR/FrameUI/toast_fit.h"
#include "VideoCommon/VR/FrameUI/ui_canvas.h"

namespace VR
{
class OpenXRManager;
}

namespace VR::UI
{
// Goes on with the game in a flat window (Main's bottom button). Host thread.
void RequestSwitchToFlat();

// What is drawn over the game in the headset, and the controls that are the same in every game:
// the VR menu, the toasts, the stats, the pointer beams, moving the screen with a grip (PadFilter.h
// has the buttons). It lives on the XR pacing thread and keeps going when the game is paused or has drawn
// nothing yet. It never waits for another thread: what the rest of the emulator has to do is
// queued as host jobs.
class VrUi
{
public:
  using CreateOverlay = std::function<std::unique_ptr<Overlay>(u32 width, u32 height)>;

  // `manager` outlives this. Constructed and destroyed while the pacing thread is not running.
  VrUi(OpenXRManager* manager, CreateOverlay create_overlay, u32 max_layers);
  ~VrUi();
  VrUi(const VrUi&) = delete;
  VrUi& operator=(const VrUi&) = delete;

  struct Frame
  {
    s64 time_ns = 0;
    bool head_valid = false;
    FrameXr::Input::Posef head;
    // The game is on a screen in the room (Stereoscopic 3D, or the 2D screen), not around you.
    bool has_screen = false;
  };

  // Once a frame with the controllers as read; leaves them as the game may see them.
  void Update(const Frame& frame, Controllers* controllers);
  // The controllers are the system's now (its menu, another app).
  void OnFocusLost();

  // Uploads, and lays out the layers to go over the game's. Not under the video handoff lock.
  // The pointers are good until the next call.
  void BuildLayers(XrSpace space, std::size_t game_layers,
                   std::vector<XrCompositionLayerBaseHeader*>* layers);

  // Something is moving or can be pointed at: every refresh counts, whatever the game's pacing.
  bool IsActive() const;
  bool IsMenuOpen() const { return m_menu_open; }

  // Any thread: a message in the headset for `ms`. A newer one replaces it.
  static void PostToast(std::string text, u32 ms);
  // The same for the answer to a press, whose effect shows: in Stereoscopic 3D it is short, in the
  // screen's corner (toast_corner.h).
  static void PostBriefToast(std::string text);
  // What a press in the menu came to: also with Messages off.
  static void PostAnswer(std::string text);
  // The same for one of the emulator's own messages: shorter, and without its progress.
  static void PostEmulatorMessage(std::string_view message, u32 ms);

private:
  struct Beam
  {
    FrameXr::Vis::HandActivity activity;
    bool visible = false;
    bool dot = false;
    FrameXr::Input::Vec3f start;
    FrameXr::Input::Vec3f end;
  };

  enum class Hint
  {
    None,
    Grab,
    Stop,
  };

  struct SlotQuad
  {
    XrCompositionLayerQuad layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
  };

  // ---- Update ----
  void UpdateGrips(const Controllers& controllers);
  void UpdateStats(const Frame& frame, const Controllers& controllers, float dt);
  void UpdateGrab(const Frame& frame, const Controllers& controllers, float dt);
  void UpdateMenu(const Frame& frame, const Controllers& controllers, float dt);
  void UpdateBeams(const Frame& frame, const Controllers& controllers);
  void UpdateToasts(const Frame& frame);
  void HandleEvents(const PadEvents& events);

  void OpenMenu(const Frame& frame);
  void CloseMenu();
  void RefreshMenuState();
  // What the game shows now, whatever the config has.
  void SetLiveSettings();
  void ChangeSetting(SettingId id, float value, bool save = true);
  // The slider is let go: what it is at is kept.
  void EndDrag();
  bool HasPopup() const;
  void ClosePopup();
  void UseGlobalValues();
  void ActivateSetting(const MenuWidget& widget, int option);
  void ChooseOption(const SettingRow& row, int option);
  void StopScrolling();
  // What is on the controller's controls.
  void RequestBindings();
  void OpenEditControls();
  void ChooseController(int controller);
  // The game's own controls, or the ones of every game again.
  void SetPadScope(SettingScope scope);
  // An option of the Profile list.
  void ChoosePadProfile(int option);
  // Edit Controls waits for a press: the controllers are its own.
  void UpdateListening(const Controllers& controllers, float dt, bool on_cancel);
  void ChangeMod(const MenuWidget& widget);
  void Activate(const MenuWidget& widget, int option);
  void SetSlider(const MenuWidget& widget, float t);
  void UpdatePause();
  void SetFastForward(bool on);
  void SwitchMode();
  void LoadState(std::string path, std::string undo_label, s64 time);
  void RequestSaves();
  void RequestMods();
  // ---- The Games tab ----
  void UpdateGames(const FrameXr::Menu::Scrolling::Bar* bar, float dt);
  void ReadGames();
  void OpenGames();
  void PressGame(const std::string& path);
  void StartGame();
  // Where the D-Pad and the left stick are on the Games tab, instead of the beam.
  std::optional<std::pair<float, float>> UpdateFocus(const Controllers& controllers,
                                                     const FrameXr::Input::QuadHit& hit,
                                                     bool* options);
  void OpenFiles(FilePurpose purpose, std::string game);
  void RequestFiles(std::string path);
  void ChooseGamesOption(MenuGames::Popup popup, int option);
  // Says when a download is done, the menu open or not.
  void WatchInstall();
  void ShowToast(std::string text, s64 duration_ns);
  void ShowBriefToast(std::string text);
  FrameXr::Vis::Toast::Placed GetToastCorner() const;
  void SaveScreenPlacement();

  FrameXr::Input::PanelQuad GetMenuQuad() const;
  FrameXr::Input::Posef PlaceAhead(float distance, float drop) const;
  float NearerThanScreen(float distance) const;

  // ---- Layers ----
  void DrawToastRow(int row, const std::string& text);
  void DrawGrabHint();
  void UploadStats();
  void AddQuad(FrameXr::LayerSlots::Group group, const Overlay& overlay,
               const FrameXr::Input::Posef& pose, float width, float height, XrRect2Di rect);

  // ---- The menu's drawing thread ----
  void DrawThread();
  void RequestMenuDraw();

  OpenXRManager* const m_manager;
  const CreateOverlay m_create_overlay;
  const u32 m_max_layers;

  std::unique_ptr<Overlay> m_art;
  std::unique_ptr<Overlay> m_toasts;
  std::unique_ptr<Overlay> m_menu_overlay;
  std::unique_ptr<Overlay> m_stats_overlay;  // from when they are first shown
  bool m_overlays_failed = false;
  bool m_menu_overlay_failed = false;
  bool m_stats_overlay_failed = false;

  Frame m_frame;
  s64 m_last_time_ns = 0;

  PadFilter m_filter;
  std::array<Beam, 2> m_beams;

  // Moving the screen.
  int m_grab_hand = -1;
  std::array<bool, 2> m_grip_down{};
  std::array<bool, 2> m_grip_pressed{};  // since the last frame
  int m_nudge = 0;
  int m_curve_step = 0;
  float m_curve = 0.0f;
  s64 m_nudge_at_ns = 0;
  ScreenGrab m_grab;

  // The stats.
  enum class StatsPicture
  {
    None,
    Front,
    Wrist,
  };
  bool m_stats_on = false;
  bool m_stats_on_wrist = false;
  bool m_stats_to_place = true;
  bool m_stats_front = false;  // In Front, to be shown now
  bool m_stats_wrist = false;  // the wrist is turned to the face
  bool m_stats_shown = false;  // In Front, as drawn last
  int m_stats_hand = -1;
  s64 m_recenter_seen = 0;
  StatsWindow m_stats_window;
  FrameXr::Input::Posef m_wrist_pose;
  // What was asked to be drawn, what is on its way to the overlay, and what that shows.
  u32 m_stats_serial = 0;
  StatsPicture m_stats_asked = StatsPicture::None;
  StatsPicture m_stats_queued = StatsPicture::None;
  StatsPicture m_stats_image = StatsPicture::None;
  bool m_stats_waiting = false;

  // The menu.
  bool m_menu_open = false;
  bool m_menu_armed = false;  // everything let go since it opened
  MenuState m_menu;
  std::vector<MenuWidget> m_widgets;
  FrameXr::Input::Posef m_menu_pose;
  float m_menu_scale = 1.0f;
  u32 m_games_seen = 0;  // of what is published
  std::string m_game_pointed;
  float m_game_pointed_s = 0.0f;
  int m_menu_hand = 1;
  bool m_press_down = false;
  FrameXr::Menu::Focus::GridFocus m_focus;
  FrameXr::Menu::Focus::DirRepeat m_focus_dir;
  // Off the grid: what it is on.
  std::pair<MenuItem, int> m_focus_on{MenuItem::None, 0};
  // Where the beam was.
  std::optional<std::pair<float, float>> m_focus_beam;
  bool m_focus_options_down = false;
  MenuItem m_press_item = MenuItem::None;
  int m_press_index = 0;
  std::optional<MenuWidget> m_drag;
  bool m_drag_changed = false;
  PadEdit m_edit;
  PadTable m_bindings_table;
  // Of the mappings they are of.
  u32 m_mappings_seen = 0;
  bool m_notice_asked = false;
  s64 m_rest_since_ns = 0;
  // Counts the menu's changes of settings.
  u32 m_settings_serial = 0;
  FrameXr::Menu::Scrolling::StickScroll m_stick_scroll;
  FrameXr::Menu::Scrolling::BarPress m_bar_press;
  FrameXr::Menu::Scrolling::DragScroll m_list_drag;
  bool m_back_down = false;
  s64 m_last_refresh_ns = 0;
  s64 m_pause_after_ns = 0;
  bool m_pause_wanted = false;
  bool m_fast_forward = false;

  // Toasts: row 0 the message, row 1 the hint.
  frame_xr::ui::Canvas m_toast_canvas;
  std::array<FrameXr::ToastFit::Box, 2> m_toast_boxes;
  s64 m_toast_until_ns = 0;
  FrameXr::Input::Posef m_toast_pose;
  float m_toast_scale = 1.0f;
  bool m_toast_corner = false;
  Hint m_hint = Hint::None;
  Hint m_hint_drawn = Hint::None;
  bool m_toasts_dirty = false;

  // Layers, kept for xrEndFrame.
  FrameXr::LayerSlots::Map<SlotQuad> m_slots;
  std::vector<FrameXr::LayerSlots::Map<SlotQuad>::Out> m_slot_layers;
  std::vector<XrCompositionLayerQuad> m_layers;

  bool m_watch_install = false;
  s64 m_last_watch_ns = 0;

  std::thread m_draw_thread;
  std::mutex m_draw_mutex;
  std::condition_variable m_draw_cv;
  bool m_draw_exit = false;
  std::optional<MenuState> m_draw_request;
  bool m_saves_request = false;
  std::optional<int> m_mods_request;
  std::optional<std::pair<std::string, FilePurpose>> m_files_request;
  std::optional<FileListing> m_files;
  std::optional<std::pair<int, std::vector<GameMods::Item>>> m_mods;
  std::optional<MenuSaves> m_saves;
  std::optional<std::pair<Perf::Stats, StatsPicture>> m_stats_request;
  std::vector<u32> m_stats_drawn;
  StatsPicture m_stats_drawn_picture = StatsPicture::None;  // None: taken
  u32 m_stats_drawn_serial = 0;
  std::vector<u32> m_drawn;
  // The titles that are cut, of the list as drawn.
  std::vector<std::pair<std::string, float>> m_cut_titles;
  bool m_drawn_ready = false;
  // The pacing thread's: what it uploads.
  std::vector<u32> m_menu_upload;
  std::vector<u32> m_stats_upload;
  std::optional<MenuState> m_last_drawn;
};
}  // namespace VR::UI

#endif  // ENABLE_VR
