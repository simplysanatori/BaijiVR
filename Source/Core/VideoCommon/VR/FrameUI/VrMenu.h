// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/GameMods/GameMods.h"
#include "VideoCommon/VR/FrameUI/MenuSettings.h"
#include "VideoCommon/VR/FrameUI/PadArt.h"
#include "VideoCommon/VR/FrameUI/PadEdit.h"
#include "VideoCommon/VR/FrameUI/Stats.h"
#include "VideoCommon/VR/FrameUI/list_scroll.h"
#include "VideoCommon/VR/FrameUI/md3.h"

// The VR menu: what is on it for a state, and its drawing, in Azahar for Frame's menu style (its
// size, rail and Material 3 parts). Laying out and drawing are apart so the pointer's thread can
// find what it is on while another draws. No OpenXR here (unit tested).
namespace VR::UI
{
enum class MenuTab
{
  Main,
  Position,
  Stats,
  Controls,
  Mods,
  Settings,
};

enum class MenuItem
{
  None,
  TabMain,
  TabPosition,
  TabStats,
  TabControls,
  TabMods,
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
  Controller,  // a Wii game's: the index is its VRWiiController::Choice
  ControlsScope,
  ControlsMore,
  PadProfile,
  EditControls,
  EditChip,  // the index is its PadTarget
  EditCancel,
  EditReset,
  EditDone,
  ModsList,
  ModSwitch,
  ModGet,
  ModCancel,
  ModRemove,
  ModsPrevious,
  ModsNext,
  ResetScreen,
  QuickSave,
  QuickLoad,
  FastForward,
  Stop,
  SwitchToFlat,
  // A row's setting: the index is its SettingId.
  Setting,
  Section,
  Scope,
  ScopeMore,
  Scrollbar,
  Popup,
  PopupOption,
};

// Main's pages.
enum class MenuPage
{
  Main,
  Save,
  Load,
  EditControls,  // on the Controls tab
};

constexpr int NUM_SLOTS = 10;
constexpr int MODS_PER_PAGE = 5;

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
  std::string edit_lock;  // why they cannot be changed
  int controller = -1;    // a Wii game's, in VRWiiController::NAMES; -1: another one
  bool can_choose_controller = true;
  // The mapping in use is this game's own.
  SettingScope pad_scope = SettingScope::Global;
  std::string pad_scope_lock;  // why it cannot be
  // The saved profiles of this controller, as the mapping window lists them, and the game's.
  std::vector<std::string> pad_profiles;
  std::string pad_profile;  // empty: the mapping of every game
  PadEditView edit;

  int mods_list = 0;  // Mods, Textures
  int mods_page = 0;
  bool mods_ready = false;
  bool mods_changed = false;
  std::vector<GameMods::Item> mods;
  GameMods::Progress mods_progress;

  std::vector<StatsGroup> stats;

  std::string title;
  bool wii = false;
  bool fast_forward = false;

  SettingState settings;
  MenuSection section = MenuSection::General;  // of the Settings tab
  std::array<float, NUM_SECTIONS> scroll{};
  // The row with its drop-down menu open.
  SettingId open_setting = SettingId::None;
  bool open_scope = false;  // the menu of the dots
  bool open_pad_scope = false;  // the same on the Controls tab
  bool open_pad_profile = false;

  MenuItem hover = MenuItem::None;
  MenuItem pressed = MenuItem::None;
  int hover_index = 0;
  int hover_option = -1;
  bool show_tooltip = false;  // of what is under the pointer

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
    Mod,
    IconButton,
    Header,
    Dropdown,
    Menu,    // an open drop-down menu
    Option,  // of it; drawn with it
    Scrollbar,
    More,  // three dots
    EditPad,
    Chip,
    Choice,  // one of a row of chips: the chosen one is filled
    Guide,   // what to do, in two lines
    StatsTitle,
    Stat,
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
  std::string note;     // under the label
  std::string tooltip;  // what it does, or why it cannot be changed
  // In a list: nothing of it is past this.
  bool clipped = false;
  FrameXr::Menu::Rect clip;
};

// A Wii game's controller on its chip: "Wii Remote + Nunchuk" is "+ Nunchuk" beside "Wii Remote",
// and "Classic Controller" is "Classic".
std::string GetControllerChipLabel(std::string_view name);
// The drawing for a choice of VRWiiController::NAMES, and its name.
PadKind GetControllerPad(int controller);
const char* GetPadName(PadKind kind);
// What the Profile list offers: no profile first, then the saved ones (the game's is among them).
constexpr const char* NO_PAD_PROFILE = "Every Game";
constexpr std::size_t MAX_PAD_PROFILES = 8;
std::vector<std::string> GetPadProfileOptions(const MenuState& state);

// The list of a page that scrolls.
struct MenuList
{
  FrameXr::Menu::Scrolling::Bar bar;
  MenuSection section = MenuSection::General;
  float row_h = 0.0f;
};

// Whether the game goes on behind the menu: a page whose settings show on it.
bool ShowsGame(MenuTab tab, MenuSection section);

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
  // The page's list, if it has one (its bar is not valid when everything fits).
  static std::optional<MenuList> GetList(const MenuState& state);

  // The widget at a point that does something, or null. `greyed`: also one that cannot be used now
  // but says why.
  static const MenuWidget* At(const std::vector<MenuWidget>& widgets, float x, float y,
                              bool greyed = false);
  // The option of a segmented button at `x`.
  static int OptionAt(const MenuWidget& widget, float x);
  // 0..1 along a slider at `x`.
  static float SliderAt(const MenuWidget& widget, float x);

  static void Draw(FrameXr::Menu::Kit* kit, FrameXr::Menu::Canvas* canvas,
                   const MenuState& state, const GetPicture& get_picture);
};
}  // namespace VR::UI
