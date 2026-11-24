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
#include <vector>

#include <openxr/openxr.h>

#include "Common/CommonTypes.h"
#include "VideoCommon/VR/FrameUI/Overlay.h"
#include "VideoCommon/VR/FrameUI/PadFilter.h"
#include "VideoCommon/VR/FrameUI/ScreenPlacement.h"
#include "VideoCommon/VR/FrameUI/VrMenu.h"
#include "VideoCommon/VR/FrameUI/beam.h"
#include "VideoCommon/VR/FrameUI/layer_slots.h"
#include "VideoCommon/VR/FrameUI/toast_fit.h"
#include "VideoCommon/VR/FrameUI/ui_canvas.h"

namespace VR
{
class OpenXRManager;
}

namespace VR::UI
{
// What is drawn over the game in the headset, and the controls that are the same in every game:
// the VR menu, the toasts, the pointer beams, moving the screen with a grip (PadFilter.h has the
// buttons). It lives on the XR pacing thread and keeps going when the game is paused or has drawn
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
    // The game is on a screen in the room (Stereo 3D Screen, or the 2D screen), not around you.
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

  // Any thread: a message in the headset for `ms`. A newer one replaces it.
  static void PostToast(std::string text, u32 ms);
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
  void UpdateGrab(const Frame& frame, const Controllers& controllers, float dt);
  void UpdateMenu(const Frame& frame, const Controllers& controllers);
  void UpdateBeams(const Frame& frame, const Controllers& controllers);
  void UpdateToasts(const Frame& frame);
  void HandleEvents(const PadEvents& events);

  void OpenMenu(const Frame& frame);
  void CloseMenu();
  void RefreshMenuState();
  void Activate(const MenuWidget& widget, int option);
  void SetSlider(const MenuWidget& widget, float t);
  void UpdatePause();
  void SetFastForward(bool on);
  void SwitchMode();
  void LoadState(std::string path, std::string undo_label, s64 time);
  void RequestSaves();
  void ShowToast(std::string text, s64 duration_ns);
  void SaveScreenPlacement();

  FrameXr::Input::PanelQuad GetMenuQuad() const;
  FrameXr::Input::Posef PlaceAhead(float distance, float drop) const;
  float NearerThanScreen(float distance) const;

  // ---- Layers ----
  void DrawToastRow(int row, const std::string& text);
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
  bool m_overlays_failed = false;
  bool m_menu_overlay_failed = false;

  Frame m_frame;
  s64 m_last_time_ns = 0;

  PadFilter m_filter;
  std::array<Beam, 2> m_beams;

  // Moving the screen.
  int m_grab_hand = -1;
  std::array<bool, 2> m_grip_down{};
  int m_nudge = 0;
  s64 m_nudge_at_ns = 0;
  ScreenGrab m_grab;

  // The menu.
  bool m_menu_open = false;
  bool m_menu_armed = false;  // everything let go since it opened
  MenuState m_menu;
  std::vector<MenuWidget> m_widgets;
  FrameXr::Input::Posef m_menu_pose;
  float m_menu_scale = 1.0f;
  int m_menu_hand = 1;
  bool m_press_down = false;
  MenuItem m_press_item = MenuItem::None;
  int m_press_index = 0;
  std::optional<MenuWidget> m_drag;
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
  Hint m_hint = Hint::None;
  Hint m_hint_drawn = Hint::None;
  bool m_toasts_dirty = false;

  // Layers, kept for xrEndFrame.
  FrameXr::LayerSlots::Map<SlotQuad> m_slots;
  std::vector<FrameXr::LayerSlots::Map<SlotQuad>::Out> m_slot_layers;
  std::vector<XrCompositionLayerQuad> m_layers;

  std::thread m_draw_thread;
  std::mutex m_draw_mutex;
  std::condition_variable m_draw_cv;
  bool m_draw_exit = false;
  std::optional<MenuState> m_draw_request;
  bool m_saves_request = false;
  std::optional<MenuSaves> m_saves;
  std::vector<u32> m_drawn;
  bool m_drawn_ready = false;
  std::optional<MenuState> m_last_drawn;
};
}  // namespace VR::UI

#endif  // ENABLE_VR
