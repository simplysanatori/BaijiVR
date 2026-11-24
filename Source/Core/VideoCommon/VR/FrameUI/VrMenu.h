// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "VideoCommon/VR/FrameUI/PadArt.h"
#include "VideoCommon/VR/FrameUI/md3.h"

// The VR menu: what is on it for a state, and its drawing, in Azahar for Frame's menu style (its
// size, rail and Material 3 parts). Laying out and drawing are apart so the pointer's thread can
// find what it is on while another draws. No OpenXR here (unit tested).
namespace VR::UI
{
enum class MenuTab
{
  Main,
  Screen,
  Controls,
  Settings,
};

enum class MenuItem
{
  None,
  TabMain,
  TabScreen,
  TabControls,
  TabSettings,
  Resume,
  SaveState,
  LoadState,
  Restart,
  Screenshot,
  Home,
  Back,
  Slot,
  AutosaveCard,
  AutosaveEntry,
  AutosaveList,
  AutosaveSettings,
  UndoLoad,
  ControlsPage,
  Autosave,
  AutosaveEvery,
  AutosaveKeep,
  ResetScreen,
  QuickSave,
  QuickLoad,
  FastForward,
  Stop,
  Mode,
  Distance,
  Size,
  Depth,
  Convergence,
  Aspect,
  LockScreen,
  Resolution,
  Beams,
  Toasts,
  Pacing,
};

// Main's pages.
enum class MenuPage
{
  Main,
  Save,
  Load,
};

constexpr int NUM_SLOTS = 10;

struct MenuAutosave
{
  std::string path;
  std::string picture;  // a PNG, or empty
  std::string ago;      // "3 Minutes Ago"
  std::string time;     // "2026-11-24 10:14"
  std::string clock;    // "10:14"

  bool operator==(const MenuAutosave&) const = default;
};

// A game's save states.
struct MenuSaves
{
  std::array<std::string, NUM_SLOTS> slots;  // when, or empty
  std::vector<MenuAutosave> autosaves;       // newest first
  bool has_quick_save = false;

  bool operator==(const MenuSaves&) const = default;
};

struct MenuState
{
  MenuTab tab = MenuTab::Main;
  MenuPage page = MenuPage::Main;
  bool autosave_list = false;
  MenuSaves saves;
  std::string undo_load;  // "Slot 3 Loaded: 10:22"
  int controls_page = 0;
  PadKind pad = PadKind::GameCube;
  PadLabels pad_labels;

  std::string title;
  bool wii = false;
  bool immersive = false;
  bool can_switch_mode = true;
  bool fast_forward = false;

  float distance = 2.0f;
  float size = 1.5f;
  float depth = 20.0f;
  float convergence = 20.0f;
  int aspect = 0;  // Auto, 16:9, 4:3, -1 something else
  bool lock_screen = false;

  int resolution = 2;  // 1x.. ; 0 is automatic
  bool beams = true;
  bool toasts = true;
  bool eager = true;
  bool autosave = false;
  int autosave_every = 2;  // in AUTOSAVE_MINUTES
  int autosave_keep = 3;

  MenuItem hover = MenuItem::None;
  MenuItem pressed = MenuItem::None;
  int hover_index = 0;
  int hover_option = -1;

  bool operator==(const MenuState&) const = default;
};

struct MenuWidget
{
  enum class Kind
  {
    Headline,
    Text,
    Rail,
    Tile,
    Button,
    Segmented,
    Switch,
    Slider,
    Back,
    Slot,
    AutosaveCard,
    AutosaveEntry,
    Popup,
    Snackbar,
    Pad,
  };

  Kind kind = Kind::Text;
  MenuItem item = MenuItem::None;
  int index = 0;
  FrameXr::Menu::Rect rect;     // what the pointer hits
  FrameXr::Menu::Rect control;  // the control within a row
  std::string label;
  std::string value;
  char32_t icon = 0;
  bool active = false;
  bool disabled = false;
  bool danger = false;
  float t = 0.0f;
  int selected = -1;
  std::vector<std::string> options;
};

constexpr std::array AUTOSAVE_MINUTES{1, 2, 5, 10, 15};

// A menu picture: RGBA.
struct MenuPicture
{
  int width = 0;
  int height = 0;
  std::vector<u8> pixels;
};

class VrMenu
{
public:
  // A picture's pixels, or null.
  using GetPicture = std::function<const MenuPicture*(const std::string& path)>;

  // dp; 1 dp is 1.25 mm at 1.2 m, as Azahar's.
  static constexpr float WIDTH = 840.0f;
  static constexpr float HEIGHT = 560.0f;
  static constexpr float METRES_PER_DP = 0.00125f;

  static std::vector<MenuWidget> Layout(const MenuState& state);

  // The widget at a point that does something, or null.
  static const MenuWidget* At(const std::vector<MenuWidget>& widgets, float x, float y);
  // The option of a segmented button at `x`.
  static int OptionAt(const MenuWidget& widget, float x);
  // 0..1 along a slider at `x`.
  static float SliderAt(const MenuWidget& widget, float x);

  static void Draw(FrameXr::Menu::Kit* kit, FrameXr::Menu::Canvas* canvas,
                   const MenuState& state, const GetPicture& get_picture);
};
}  // namespace VR::UI
