// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoCommon/VR/FrameUI/VrMenu.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

#include <fmt/format.h>

#include "Common/StringUtil.h"
#include "Core/VRWiiController.h"
#include "VideoCommon/VR/FrameUI/games_tab.h"
#include "VideoCommon/VR/FrameUI/md3_fonts.h"
#include "VideoCommon/VR/FrameUI/ui_ttf.h"

namespace VR::UI
{
namespace
{
using namespace FrameXr::Menu;
using Kind = MenuWidget::Kind;

constexpr float RAIL_W = 104.0f;
constexpr float RAIL_ITEM_H = 72.0f;
// Where Kit::RailItem has its icon.
constexpr float RAIL_ICON_Y = RAIL_ITEM_H * 0.5f - (32.0f + 4.0f + 14.0f * 1.4f) * 0.5f + 16.0f;
constexpr float MARGIN = 16.0f;
constexpr float CONTROL_H = 48.0f;
constexpr float ROW_H = 64.0f;
constexpr float LABEL_W = 220.0f;
constexpr float DROPDOWN_W = 300.0f;
constexpr float SWITCH_W = 60.0f;
constexpr float MENU_PADDING = 8.0f;
constexpr float SLIDER_VALUE_W = 72.0f;
constexpr float SLIDER_VALUE_GAP = 16.0f;
constexpr Rect PANE{RAIL_W, MARGIN, VrMenu::WIDTH - MARGIN, VrMenu::HEIGHT - MARGIN};
constexpr float LEFT = RAIL_W + 24.0f;
constexpr float RIGHT = VrMenu::WIDTH - MARGIN - 24.0f;
constexpr float TOP = PANE.y0 + 16.0f;
constexpr float SECTIONS_W = 384.0f;
constexpr float SCOPE_W = 252.0f;
constexpr float MORE_W = 32.0f;
// The dots are over the list's bar.
constexpr float MORE_X0 = RIGHT - 26.0f;
constexpr float SCOPE_MENU_W = 300.0f;

class Builder
{
public:
  std::vector<MenuWidget> Take() { return std::move(m_widgets); }

  void Headline(std::string text)
  {
    MenuWidget& w = Add(Kind::Headline, MenuItem::None);
    w.rect = {LEFT, m_y, RIGHT, m_y + 48.0f};
    w.label = std::move(text);
    m_y += 64.0f;
  }

  void Text(std::string left, std::string right)
  {
    MenuWidget& w = Add(Kind::Text, MenuItem::None);
    w.rect = {LEFT, m_y, RIGHT, m_y + TEXT_H};
    w.label = std::move(left);
    w.value = std::move(right);
    m_y += TEXT_H;
  }

  void Gap(float height) { m_y += height; }

  MenuWidget& Add(Kind kind, MenuItem item)
  {
    MenuWidget& w = m_widgets.emplace_back();
    w.kind = kind;
    w.item = item;
    return w;
  }

  float GetY() const { return m_y; }
  std::size_t GetCount() const { return m_widgets.size(); }

  // All that were added from `first` on.
  void Clip(std::size_t first, const Rect& clip)
  {
    for (std::size_t i = first; i < m_widgets.size(); ++i)
    {
      m_widgets[i].clipped = true;
      m_widgets[i].clip = clip;
    }
  }

  const MenuWidget* FindSetting(SettingId id) const
  {
    const auto it = std::ranges::find_if(m_widgets, [id](const MenuWidget& w) {
      return w.item == MenuItem::Setting && w.index == static_cast<int>(id);
    });
    return it != m_widgets.end() ? &*it : nullptr;
  }

  MenuWidget* Find(Kind kind, int index)
  {
    const auto it = std::ranges::find_if(m_widgets, [&](const MenuWidget& w) {
      return w.kind == kind && w.index == index;
    });
    return it != m_widgets.end() ? &*it : nullptr;
  }

private:
  static constexpr float TEXT_H = 34.0f;

  std::vector<MenuWidget> m_widgets;
  float m_y = TOP;
};

void AddRail(Builder* builder, const MenuState& state)
{
  struct RailItem
  {
    MenuTab tab;
    MenuItem item;
    char32_t icon;
    const char* label;
  };
  static constexpr RailItem ITEMS[] = {
      {MenuTab::Main, MenuItem::TabMain, Icon::kHome, "Main"},
      // Its icon is drawn (DrawGamesIcon).
      {MenuTab::Games, MenuItem::TabGames, 0, "Games"},
      {MenuTab::Position, MenuItem::TabPosition, Icon::kOpenWith, "Position"},
      {MenuTab::Stats, MenuItem::TabStats, Icon::kMonitoring, "Stats"},
      {MenuTab::Controls, MenuItem::TabControls, Icon::kStadiaController, "Controls"},
      {MenuTab::Mods, MenuItem::TabMods, Icon::kExtension, "Mods"},
      {MenuTab::Settings, MenuItem::TabSettings, Icon::kSettings, "Settings"},
  };
  float y = (VrMenu::HEIGHT - RAIL_ITEM_H * std::size(ITEMS)) * 0.5f;
  for (const RailItem& item : ITEMS)
  {
    MenuWidget& w = builder->Add(Kind::Rail, item.item);
    w.rect = {0.0f, y, RAIL_W, y + RAIL_ITEM_H};
    w.icon = item.icon;
    w.label = item.label;
    w.active = state.tab == item.tab;
    y += RAIL_ITEM_H;
  }
}

// Azahar for Frame's page: eight tiles between two buttons as wide as the page.
void AddMain(Builder* builder, const MenuState& state)
{
  constexpr float GAP = 12.0f;
  constexpr int COLUMNS = 4;
  constexpr float TILE_W = (RIGHT - LEFT - GAP * (COLUMNS - 1)) / COLUMNS;
  constexpr float BOTTOM = PANE.y1 - 24.0f;
  constexpr float RESUME_Y0 = PANE.y0 + 24.0f + 52.0f;
  constexpr float TILES_Y0 = RESUME_Y0 + 64.0f + 20.0f;
  constexpr float TILES_Y1 = BOTTOM - 56.0f - GAP;
  constexpr float TILE_H = (TILES_Y1 - TILES_Y0 - GAP) * 0.5f;

  MenuWidget& headline = builder->Add(Kind::Headline, MenuItem::None);
  headline.rect = {LEFT, PANE.y0 + 24.0f, RIGHT, RESUME_Y0 - 8.0f};
  headline.label = state.title;

  MenuWidget& resume = builder->Add(Kind::Button, MenuItem::Resume);
  resume.rect = {LEFT, RESUME_Y0, RIGHT, RESUME_Y0 + 64.0f};
  resume.icon = Icon::kPlayArrow;
  resume.label = "Continue";
  resume.active = true;

  struct TileItem
  {
    MenuItem item;
    char32_t icon;
    const char* label;
    bool active = false;
    bool disabled = false;
  };
  const std::array<TileItem, 8> tiles{{
      {MenuItem::QuickSave, Icon::kSave, "Quick Save"},
      {MenuItem::QuickLoad, Icon::kHistory, "Quick Load", false, !state.saves.has_quick_save},
      {MenuItem::SaveState, Icon::kSaveAs, "Save State"},
      {MenuItem::LoadState, Icon::kFolderOpen, "Load State"},
      {MenuItem::Screenshot, Icon::kPhotoCamera, "Screenshot"},
      {MenuItem::Restart, Icon::kRestartAlt, "Restart"},
      state.wii ? TileItem{MenuItem::Home, Icon::kHome, "Wii Home"} :
                  TileItem{MenuItem::FastForward, Icon::kSpeed, "Fast Forward", state.fast_forward},
      {MenuItem::Stop, Icon::kClose, "Close Game"},
  }};
  for (std::size_t i = 0; i < tiles.size(); ++i)
  {
    const float x = LEFT + static_cast<float>(i % COLUMNS) * (TILE_W + GAP);
    const float y = TILES_Y0 + static_cast<float>(i / COLUMNS) * (TILE_H + GAP);
    MenuWidget& w = builder->Add(Kind::Tile, tiles[i].item);
    w.rect = {x, y, x + TILE_W, y + TILE_H};
    w.icon = tiles[i].icon;
    w.label = tiles[i].label;
    w.active = tiles[i].active;
    w.disabled = tiles[i].disabled;
    w.danger = tiles[i].item == MenuItem::Stop;
  }

  MenuWidget& flat = builder->Add(Kind::Button, MenuItem::SwitchToFlat);
  flat.rect = {LEFT, BOTTOM - 56.0f, RIGHT, BOTTOM};
  flat.icon = Icon::kTv;
  flat.label = "Switch to Flat";
}

// Save State and Load State: the slots in two columns, over them in Load State the autosaves' card
// and under them Undo Load (Azahar for Frame's page).
void AddSlots(Builder* builder, const MenuState& state)
{
  const bool saving = state.page == MenuPage::Save;
  constexpr float GAP = 12.0f;
  MenuWidget& back = builder->Add(Kind::Back, MenuItem::Back);
  back.rect = {LEFT - 4.0f, TOP, LEFT + 44.0f, TOP + 48.0f};
  MenuWidget& headline = builder->Add(Kind::Headline, MenuItem::None);
  headline.rect = {LEFT + 56.0f, TOP, RIGHT, TOP + 48.0f};
  headline.label = saving ? "Save State" : "Load State";

  float top = TOP + 64.0f;
  float bottom = PANE.y1 - 24.0f;
  constexpr float CARD_H = 72.0f;
  const Rect card_rect{LEFT, top, RIGHT, top + CARD_H};
  if (!saving)
  {
    MenuWidget& card = builder->Add(Kind::AutosaveCard, MenuItem::AutosaveCard);
    card.rect = card_rect;
    card.active = state.settings.values.game[static_cast<std::size_t>(SettingId::Autosave)] != 0.0f;
    card.disabled = state.saves.autosaves.empty();
    top = card_rect.y1 + GAP;
    if (card.disabled && !card.active)
    {
      MenuWidget& settings = builder->Add(Kind::Button, MenuItem::AutosaveSettings);
      settings.rect = {RIGHT - 124.0f, card_rect.Cy() - 24.0f, RIGHT - 12.0f,
                       card_rect.Cy() + 24.0f};
      settings.label = "Settings";
    }
    if (!state.undo_load.empty())
    {
      MenuWidget& undo = builder->Add(Kind::Snackbar, MenuItem::UndoLoad);
      undo.rect = {LEFT, bottom - 48.0f, RIGHT, bottom};
      undo.label = state.undo_load;
      undo.value = "Undo Load";
      bottom -= 48.0f + GAP;
    }
  }

  constexpr int ROWS = NUM_SLOTS / 2;
  constexpr float WIDTH = (RIGHT - LEFT - GAP) * 0.5f;
  const float row_gap = state.undo_load.empty() || saving ? GAP : 10.0f;
  const float height = (bottom - top - row_gap * (ROWS - 1)) / ROWS;
  for (int i = 0; i < NUM_SLOTS; ++i)
  {
    const float x = LEFT + static_cast<float>(i / ROWS) * (WIDTH + GAP);
    const float y = top + static_cast<float>(i % ROWS) * (height + row_gap);
    MenuWidget& w = builder->Add(Kind::Slot, MenuItem::Slot);
    w.index = i + 1;
    w.rect = {x, y, x + WIDTH, y + height};
    w.label = fmt::format("Slot {}", i + 1);
    const std::string& time = state.saves.slots[static_cast<std::size_t>(i)];
    w.value = time.empty() ? "Empty" : time;
    w.disabled = !saving && time.empty();
  }

  if (saving || !state.autosave_list)
    return;
  // Up to six in one column, more in two.
  const int count = static_cast<int>(state.saves.autosaves.size());
  const int columns = count > 6 ? 2 : 1;
  const int rows = (count + columns - 1) / columns;
  constexpr float ITEM_H = 56.0f;
  constexpr float PAD = 8.0f;
  const float list_w = columns == 1 ? 420.0f : card_rect.W();
  MenuWidget& popup = builder->Add(Kind::Popup, MenuItem::AutosaveList);
  popup.rect = {RIGHT - list_w, card_rect.y1 + 4.0f, RIGHT,
                card_rect.y1 + 4.0f + PAD * 2.0f + ITEM_H * static_cast<float>(rows)};
  const Rect box = popup.rect;
  const float item_w = (box.W() - 8.0f) / static_cast<float>(columns);
  for (int i = 0; i < count; ++i)
  {
    const MenuAutosave& autosave = state.saves.autosaves[static_cast<std::size_t>(i)];
    const float x = box.x0 + 4.0f + item_w * static_cast<float>(i / rows);
    const float y = box.y0 + PAD + ITEM_H * static_cast<float>(i % rows);
    MenuWidget& w = builder->Add(Kind::AutosaveEntry, MenuItem::AutosaveEntry);
    w.index = i;
    w.rect = {x, y, x + item_w, y + ITEM_H};
    w.label = autosave.ago;
    w.value = autosave.clock;
  }
}

// ---- Rows of settings ----

// Where a section's list is, under the line of controls.
constexpr Rect LIST{PANE.x0, TOP + CONTROL_H + 12.0f, PANE.x1, PANE.y1 - 8.0f};

std::optional<MenuSection> GetSection(const MenuState& state)
{
  if (state.tab == MenuTab::Position)
    return MenuSection::Screen;
  if (state.tab == MenuTab::Settings)
    return state.section;
  return std::nullopt;
}

// The switches at the end of other settings' rows have none of their own.
std::vector<const SettingRow*> GetRows(MenuSection section)
{
  std::vector<const SettingRow*> rows;
  for (const SettingRow& row : GetSettingRows())
  {
    if (row.section == section && !row.in_next_row)
      rows.push_back(&row);
  }
  return rows;
}

std::string GetNote(Applies applies)
{
  switch (applies)
  {
  case Applies::NextGame:
    return "From the Next Game";
  case Applies::NextStart:
    return "From the Next Start";
  case Applies::Now:
    break;
  }
  return {};
}

void SetLock(MenuWidget* widget, const SettingRow& row, const SettingState& state)
{
  const Lock lock = GetLock(row, state);
  widget->disabled = lock != Lock::None;
  widget->tooltip = widget->disabled ? ExplainLock(lock, row) : row.tooltip;
}

void AddRow(Builder* builder, const MenuState& state, const SettingRow& row, float y)
{
  const SettingState& settings = state.settings;
  if (row.kind == RowKind::Header)
  {
    MenuWidget& w = builder->Add(Kind::Header, MenuItem::None);
    w.rect = {LEFT, y, RIGHT, y + ROW_H};
    w.label = row.label;
    return;
  }

  static constexpr std::array KINDS{Kind::Header, Kind::Switch, Kind::Segmented, Kind::Dropdown,
                                    Kind::Slider};
  MenuWidget& w = builder->Add(KINDS[static_cast<std::size_t>(row.kind)], MenuItem::Setting);
  w.index = static_cast<int>(row.id);
  w.rect = {LEFT - 12.0f, y, RIGHT, y + ROW_H - 4.0f};
  w.label = row.label;
  w.note = GetNote(row.applies);
  SetLock(&w, row, settings);
  const float value = settings.Get(row.id);
  const float y0 = w.rect.Cy() - CONTROL_H * 0.5f;
  switch (row.kind)
  {
  case RowKind::Switch:
    w.control = {RIGHT - SWITCH_W, y0, RIGHT, y0 + CONTROL_H};
    w.active = value != 0.0f;
    break;
  case RowKind::Segmented:
  {
    w.control = {LEFT + LABEL_W, y0, RIGHT, y0 + CONTROL_H};
    const std::vector<SettingOption> options = GetOptions(row, settings);
    for (const SettingOption& option : options)
      w.options.push_back(option.label);
    w.selected = FindOption(options, value);
    break;
  }
  case RowKind::Dropdown:
    w.control = {RIGHT - DROPDOWN_W, y0, RIGHT, y0 + CONTROL_H};
    w.value = FormatValue(row, value);
    w.active = state.open_setting == row.id;
    break;
  case RowKind::Slider:
    w.control = {LEFT + LABEL_W, y0, RIGHT, y0 + CONTROL_H};
    w.t = SliderFraction(row, value);
    w.value = FormatValue(row, value);
    if (row.turned_on_by != SettingId::None)
    {
      w.control.x1 = RIGHT - SWITCH_W - 16.0f;
      w.rect.x1 = RIGHT - SWITCH_W - 8.0f;
      const SettingRow& enable = GetSettingRow(row.turned_on_by);
      MenuWidget& on = builder->Add(Kind::Switch, MenuItem::Setting);
      on.index = static_cast<int>(enable.id);
      on.rect = {RIGHT - SWITCH_W, y0, RIGHT, y0 + CONTROL_H};
      on.control = on.rect;
      on.active = settings.IsOn(enable.id);
      SetLock(&on, enable, settings);
    }
    break;
  case RowKind::Header:
    break;
  }
}

// The menu of a drop-down control, under it or over it.
void AddMenu(Builder* builder, const Rect& anchor, float width, std::vector<std::string> options,
             int selected)
{
  const float item_h = Kit::MenuItemHeight();
  const float height = item_h * static_cast<float>(options.size()) + MENU_PADDING * 2.0f;
  float y0 = anchor.y1 + 4.0f;
  if (y0 + height > PANE.y1)
    y0 = std::max(PANE.y0, std::min(anchor.y0, PANE.y1) - 4.0f - height);
  const Rect box{anchor.x1 - width, y0, anchor.x1, y0 + height};
  const auto count = static_cast<int>(options.size());
  MenuWidget& menu = builder->Add(Kind::Menu, MenuItem::Popup);
  menu.rect = box;
  menu.options = std::move(options);
  menu.selected = selected;
  for (int i = 0; i < count; ++i)
  {
    const float y = box.y0 + MENU_PADDING + item_h * static_cast<float>(i);
    MenuWidget& w = builder->Add(Kind::Option, MenuItem::PopupOption);
    w.index = i;
    w.rect = {box.x0, y, box.x1, y + item_h};
  }
}

void AddList(Builder* builder, const MenuState& state, MenuSection section)
{
  const std::size_t first = builder->GetCount();
  float y = LIST.y0 - state.scroll[static_cast<std::size_t>(section)];
  for (const SettingRow* const row : GetRows(section))
  {
    if (y + ROW_H > LIST.y0 && y < LIST.y1)
      AddRow(builder, state, *row, y);
    y += ROW_H;
  }
  builder->Clip(first, LIST);

  const std::optional<MenuList> list = VrMenu::GetList(state);
  if (list && list->bar.Valid())
  {
    MenuWidget& bar = builder->Add(Kind::Scrollbar, MenuItem::Scrollbar);
    bar.rect = Scrolling::HitRect(list->bar);
    bar.rect.x0 = RIGHT;
  }

  const MenuWidget* const open = builder->FindSetting(state.open_setting);
  if (!open)
    return;
  const Rect anchor = open->control;
  const SettingRow& row = GetSettingRow(state.open_setting);
  const std::vector<SettingOption> options = GetOptions(row, state.settings);
  std::vector<std::string> labels;
  for (const SettingOption& option : options)
    labels.push_back(option.label);
  AddMenu(builder, anchor, DROPDOWN_W, std::move(labels),
          FindOption(options, state.settings.Get(row.id)));
}

// Global or Per-Game, and the menu of the dots (Azahar for Frame's), over a section's list.
void AddScope(Builder* builder, const MenuState& state, MenuSection section)
{
  const SettingState& settings = state.settings;
  const int own = CountOwnValues(section, settings);
  MenuWidget& scope = builder->Add(Kind::Segmented, MenuItem::Scope);
  scope.rect = {MORE_X0 - 2.0f - SCOPE_W, TOP, MORE_X0 - 2.0f, TOP + CONTROL_H};
  scope.control = scope.rect;
  scope.options = {"Global", "Per-Game"};
  scope.selected = static_cast<int>(settings.scope);
  scope.disabled = !settings.has_game;
  scope.tooltip =
      settings.has_game ? DescribeOwnValues(own) : "Per-Game values need a running game.";

  MenuWidget& more = builder->Add(Kind::More, MenuItem::ScopeMore);
  more.rect = {MORE_X0, TOP, MORE_X0 + MORE_W, TOP + CONTROL_H};
  more.active = state.open_scope;
  more.disabled = !settings.has_game || own == 0;
  if (more.disabled)
  {
    more.tooltip = settings.has_game ? "Nothing on this tab is set for this game." :
                                       "Per-Game values need a running game.";
  }
}

void AddScopeMenu(Builder* builder, const MenuState& state)
{
  if (state.open_scope)
  {
    AddMenu(builder, {MORE_X0, TOP, MORE_X0 + MORE_W, TOP + CONTROL_H}, SCOPE_MENU_W,
            {"Use Global Values for All"}, -1);
  }
}

void AddScreen(Builder* builder, const MenuState& state)
{
  MenuWidget& reset = builder->Add(Kind::Button, MenuItem::ResetScreen);
  reset.rect = {LEFT, TOP, LEFT + 200.0f, TOP + CONTROL_H};
  reset.icon = Icon::kRecenter;
  reset.label = state.settings.immersive ? "Reset View" : "Reset Screen";
  AddScope(builder, state, MenuSection::Screen);
  AddList(builder, state, MenuSection::Screen);
  AddScopeMenu(builder, state);
}

// Edit Controls: the drawing with a chip for each control that can be given something else.
void AddEditControls(Builder* builder, const MenuState& state)
{
  const bool listening = state.edit.listening != PadTarget::None;
  MenuWidget& headline = builder->Add(Kind::Headline, MenuItem::None);
  headline.rect = {LEFT, TOP, LEFT + 260.0f, TOP + CONTROL_H};
  headline.label = "Edit Controls";
  MenuWidget& done = builder->Add(Kind::Button, MenuItem::EditDone);
  constexpr float DONE_X0 = RIGHT - 96.0f;
  done.rect = {DONE_X0, TOP, RIGHT, TOP + CONTROL_H};
  done.label = "Done";
  done.active = true;
  done.disabled = listening;
  MenuWidget& reset = builder->Add(Kind::Button, MenuItem::EditReset);
  reset.rect = {DONE_X0 - 12.0f - 196.0f, TOP, DONE_X0 - 12.0f, TOP + CONTROL_H};
  reset.icon = Icon::kRestartAlt;
  reset.label = "Reset to Default";
  reset.disabled = listening;

  constexpr float BAR_H = 48.0f;
  constexpr Rect BAR{LEFT, PANE.y1 - 12.0f - BAR_H, RIGHT, PANE.y1 - 12.0f};
  constexpr Rect AREA{LEFT, TOP + CONTROL_H + 8.0f, RIGHT, BAR.y0 - 8.0f};
  MenuWidget& pad = builder->Add(Kind::EditPad, MenuItem::None);
  pad.rect = GetPadEditArt(state.pad, AREA);
  for (const PadChip& chip : GetPadChips(state.pad, AREA))
  {
    MenuWidget& w = builder->Add(Kind::Chip, MenuItem::EditChip);
    w.index = static_cast<int>(chip.target);
    w.rect = chip.rect;
    // Where its line ends.
    w.control = {chip.x, chip.y, chip.x, chip.y};
    w.label = GetTargetName(chip.target);
    w.active = chip.target == state.edit.listening;
    w.value = DescribeBinding(chip.target, state.edit.table[static_cast<std::size_t>(w.index)]);
    // All of it, where the chip is too small.
    if (w.value.size() > 18)
      w.tooltip = w.value;
    w.disabled = listening && !w.active;
  }

  // What to do, and under it whose controls these are.
  constexpr float PROFILE_W = 300.0f;
  MenuWidget& guide = builder->Add(Kind::Guide, MenuItem::None);
  guide.rect = {BAR.x0, BAR.y0, BAR.x1 - (listening ? 112.0f : PROFILE_W + 12.0f), BAR.y1};
  guide.active = listening;
  if (listening)
  {
    guide.label = fmt::format("{}: {}", GetTargetName(state.edit.listening), GetPrompt(state.edit));
    guide.note = "On your controllers. The grips and the stick clicks keep their own jobs.";
  }
  else
  {
    guide.label = "Choose a Control to Change";
    guide.note = fmt::format("{}, {}.", GetPadName(state.pad),
                             state.pad_scope == SettingScope::PerGame ? "for this game only" :
                                                                         "for every game");
  }
  if (listening)
  {
    MenuWidget& cancel = builder->Add(Kind::Button, MenuItem::EditCancel);
    cancel.rect = {BAR.x1 - 100.0f, BAR.y0, BAR.x1, BAR.y1};
    cancel.label = "Cancel";
    return;
  }
  // The saved profile the game has, as the mapping window's Profile.
  const Rect profile_rect{BAR.x1 - PROFILE_W, BAR.y0, BAR.x1, BAR.y1};
  MenuWidget& profile = builder->Add(Kind::Dropdown, MenuItem::PadProfile);
  profile.rect = profile_rect;
  profile.control = profile_rect;
  profile.value =
      fmt::format("Profile: {}", state.pad_profile.empty() ? NO_PAD_PROFILE : state.pad_profile);
  profile.active = state.open_pad_profile;
  profile.disabled = !state.pad_scope_lock.empty();
  profile.tooltip = profile.disabled ? state.pad_scope_lock :
                                       "The saved profile this game uses, as in the mapping window.";
  if (state.open_pad_profile)
  {
    std::vector<std::string> options = GetPadProfileOptions(state);
    // Default is the first of all; a profile may be called that, too.
    const bool has_default = options.front() == NO_PAD_PROFILE;
    int selected = state.pad_profile.empty() && has_default ? 0 : -1;
    const auto first = options.begin() + (has_default ? 1 : 0);
    if (const auto it = std::find(first, options.end(), state.pad_profile);
        !state.pad_profile.empty() && it != options.end())
    {
      selected = static_cast<int>(it - options.begin());
    }
    AddMenu(builder, profile_rect, 400.0f, std::move(options), selected);
  }
}

constexpr float CONTROLS_PAGE_W = 248.0f;
constexpr float EDIT_W = 152.0f;
constexpr float CHOICE_H = 36.0f;
constexpr float CHOICE_GAP = 8.0f;

// A Wii game's controllers as a row of chips, as wide as their names, from `x0` on.
void AddControllers(Builder* builder, const MenuState& state, float x0, float y)
{
  std::vector<std::string> labels;
  std::vector<float> widths;
  float total = -CHOICE_GAP;
  for (const char* const name : VRWiiController::NAMES)
  {
    labels.push_back(GetControllerChipLabel(name));
    widths.push_back(28.0f + 7.6f * static_cast<float>(labels.back().size()));
    total += widths.back() + CHOICE_GAP;
  }
  const float scale = std::min(1.0f, (RIGHT - x0) / total);
  float x = x0;
  for (std::size_t i = 0; i < labels.size(); ++i)
  {
    MenuWidget& w = builder->Add(Kind::Choice, MenuItem::Controller);
    w.index = static_cast<int>(i);
    w.rect = {x, y, x + widths[i] * scale, y + CHOICE_H};
    w.label = std::move(labels[i]);
    w.active = state.controller == w.index;
    w.disabled = !state.can_choose_controller;
    w.tooltip = w.disabled ? "NetPlay, a movie or the game's own settings have chosen it." :
                             VRWiiController::NAMES[i];
    x = w.rect.x1 + CHOICE_GAP * scale;
  }
}

void AddControls(Builder* builder, const MenuState& state)
{
  if (state.page == MenuPage::EditControls)
  {
    AddEditControls(builder, state);
    return;
  }
  MenuWidget& page = builder->Add(Kind::Segmented, MenuItem::ControlsPage);
  page.rect = {LEFT, TOP, LEFT + CONTROLS_PAGE_W, TOP + CONTROL_H};
  page.control = page.rect;
  page.options = {"Game", "Shortcuts"};
  page.selected = state.controls_page;
  builder->Gap(64.0f);

  if (state.controls_page == 0)
  {
    // Under them: Edit Controls, and beside it a Wii game's controllers.
    const float row_y = builder->GetY() - 4.0f;
    MenuWidget& edit = builder->Add(Kind::Button, MenuItem::EditControls);
    edit.rect = {LEFT, row_y, LEFT + EDIT_W, row_y + CHOICE_H};
    edit.label = "Edit Controls";
    edit.disabled = !state.edit_lock.empty();
    edit.tooltip = state.edit_lock;

    // Whose mapping it is, where Settings has it.
    const bool own = state.pad_scope == SettingScope::PerGame;
    const bool locked = !state.pad_scope_lock.empty();
    MenuWidget& scope = builder->Add(Kind::Segmented, MenuItem::ControlsScope);
    scope.rect = {MORE_X0 - 2.0f - SCOPE_W, TOP, MORE_X0 - 2.0f, TOP + CONTROL_H};
    scope.control = scope.rect;
    scope.options = {"Global", "Per-Game"};
    scope.selected = own ? 1 : 0;
    scope.disabled = locked;
    scope.tooltip = locked ? state.pad_scope_lock :
                    own    ? "This game has its own controls." :
                             "These controls are for every game.";
    MenuWidget& more = builder->Add(Kind::More, MenuItem::ControlsMore);
    more.rect = {MORE_X0, TOP, MORE_X0 + MORE_W, TOP + CONTROL_H};
    more.active = state.open_pad_scope;
    more.disabled = locked || !own;
    if (more.disabled)
      more.tooltip = locked ? state.pad_scope_lock : "This game has no controls of its own.";

    if (state.wii)
      AddControllers(builder, state, LEFT + EDIT_W + 16.0f, row_y);
    MenuWidget& pad = builder->Add(Kind::Pad, MenuItem::None);
    pad.rect = {LEFT, row_y + CHOICE_H + 8.0f, RIGHT, PANE.y1 - 8.0f};
    if (state.open_pad_scope)
    {
      AddMenu(builder, {MORE_X0, TOP, MORE_X0 + MORE_W, TOP + CONTROL_H}, SCOPE_MENU_W,
              {"Use Global Values"}, -1);
    }
    return;
  }
  builder->Text("VR Menu", "Hold Menu for 1 Second");
  builder->Text("Close Game", "Hold View for 1.5 Seconds");
  builder->Text("Quick Save", "View, then Right Bumper");
  builder->Text("Quick Load", "View, then Hold Left Bumper");
  builder->Text("Fast Forward", "View, then Right Trigger");
  builder->Text("Reset Screen or View", "Hold Right Stick Click");
  builder->Text("Stereoscopic 3D or Immersive", "Both Bumpers, then View and Menu");
  builder->Gap(12.0f);
  builder->Text("Move Screen", "Grip");
  builder->Text("Screen Distance and Size", "Grip and Stick");
  builder->Text("Nudge Screen", "Grip and D-Pad Left or Right");
  builder->Text("Screen Curve", "Grip and D-Pad Up or Down");
}

std::string FormatSize(u64 bytes)
{
  if (bytes >= 1000'000'000)
    return fmt::format("{:.1f} GB", static_cast<double>(bytes) / 1e9);
  return bytes > 0 ? fmt::format("{:.0f} MB", std::ceil(static_cast<double>(bytes) / 1e6)) : "";
}

bool IsInstalling(const GameMods::Progress& progress)
{
  return progress.phase == GameMods::Progress::Phase::Downloading ||
         progress.phase == GameMods::Progress::Phase::Unpacking;
}

std::string FormatProgress(const GameMods::Progress& progress)
{
  if (progress.phase == GameMods::Progress::Phase::Unpacking)
    return "Unpacking";
  if (progress.bytes_total == 0)
    return progress.bytes_done > 0 ? FormatSize(progress.bytes_done) : "Starting";
  return fmt::format("{}%", progress.bytes_done * 100 / progress.bytes_total);
}

void AddMods(Builder* builder, const MenuState& state)
{
  MenuWidget& list = builder->Add(Kind::Segmented, MenuItem::ModsList);
  list.rect = {LEFT + 136.0f, PANE.y0 + 16.0f, RIGHT - 136.0f, PANE.y0 + 64.0f};
  list.control = list.rect;
  list.options = {"Mods and Cheats", "Textures"};
  list.selected = state.mods_list;
  builder->Gap(64.0f);

  const float top = builder->GetY();
  const float footer = PANE.y1 - 16.0f - 48.0f;
  if (state.mods.empty())
  {
    if (state.mods_ready)
      builder->Text("Nothing for this game yet.", "");
    return;
  }

  constexpr float GAP = 8.0f;
  constexpr float PITCH = 75.0f;
  constexpr float CARD_H = PITCH - GAP;
  const int count = static_cast<int>(state.mods.size());
  const int pages = (count + MODS_PER_PAGE - 1) / MODS_PER_PAGE;
  const int page = std::clamp(state.mods_page, 0, pages - 1);
  const bool busy = IsInstalling(state.mods_progress);
  for (int row = 0; row < MODS_PER_PAGE; ++row)
  {
    const int index = page * MODS_PER_PAGE + row;
    if (index >= count)
      break;
    const GameMods::Item& item = state.mods[static_cast<std::size_t>(index)];
    const float y = top + PITCH * static_cast<float>(row);
    MenuWidget& card = builder->Add(Kind::Mod, MenuItem::None);
    card.index = index;
    card.rect = {LEFT, y, RIGHT, y + CARD_H};
    const Rect rect = card.rect;

    float right = rect.x1 - 16.0f;
    const auto control = [&](Kind kind, MenuItem menu_item, float width) -> MenuWidget& {
      MenuWidget& w = builder->Add(kind, menu_item);
      w.index = index;
      w.rect = {right - width, rect.Cy() - 24.0f, right, rect.Cy() + 24.0f};
      w.control = w.rect;
      right -= width + 8.0f;
      return w;
    };
    if (busy && state.mods_progress.id == item.id)
    {
      control(Kind::IconButton, MenuItem::ModCancel, 48.0f).icon = Icon::kClose;
      MenuWidget& text = control(Kind::Text, MenuItem::None, 104.0f);
      text.value = FormatProgress(state.mods_progress);
    }
    else if (item.state == GameMods::State::LinkOnly)
    {
      control(Kind::Text, MenuItem::None, 120.0f).value = "On the Web";
    }
    else if (item.state == GameMods::State::Available && item.downloads)
    {
      MenuWidget& get = control(Kind::Button, MenuItem::ModGet, 144.0f);
      get.icon = Icon::kDownload;
      get.label = item.size > 0 ? FormatSize(item.size) : "Get";
      get.disabled = busy;
    }
    else
    {
      MenuWidget& on = control(Kind::Switch, MenuItem::ModSwitch, 60.0f);
      on.active = item.state == GameMods::State::On;
      if (item.downloads)
        control(Kind::IconButton, MenuItem::ModRemove, 48.0f).icon = Icon::kRemove;
    }
    // What is left for the text (the references above do not outlive the next Add).
    builder->Find(Kind::Mod, index)->control = {rect.x0 + 20.0f, rect.y0, right, rect.y1};
  }

  if (pages > 1)
  {
    MenuWidget& next = builder->Add(Kind::IconButton, MenuItem::ModsNext);
    next.rect = {RIGHT - 48.0f, footer, RIGHT, footer + 48.0f};
    next.icon = Icon::kChevronRight;
    next.disabled = page + 1 >= pages;
    MenuWidget& text = builder->Add(Kind::Text, MenuItem::None);
    text.rect = {RIGHT - 120.0f, footer, RIGHT - 60.0f, footer + 48.0f};
    text.value = fmt::format("{} / {}", page + 1, pages);
    MenuWidget& previous = builder->Add(Kind::IconButton, MenuItem::ModsPrevious);
    previous.rect = {RIGHT - 176.0f, footer, RIGHT - 128.0f, footer + 48.0f};
    previous.icon = Icon::kArrowBack;
    previous.disabled = page == 0;
  }
  if (state.mods_changed)
  {
    MenuWidget& text = builder->Add(Kind::Text, MenuItem::None);
    text.rect = {LEFT, footer, RIGHT - 200.0f, footer + 48.0f};
    text.label = "Changes are for the game's next start.";
  }
}

// Azahar for Frame's page: the settings, and under them the numbers in two columns.
void AddStats(Builder* builder, const MenuState& state)
{
  constexpr float COLUMN_GAP = 40.0f;
  constexpr float COLUMN_W = (RIGHT - LEFT - COLUMN_GAP) * 0.5f;
  constexpr float TITLE_H = 30.0f;
  constexpr float STAT_H = 28.0f;
  constexpr float GROUP_GAP = 12.0f;
  float top = TOP - 8.0f;
  for (const SettingRow* const row : GetRows(MenuSection::Stats))
  {
    AddRow(builder, state, *row, top);
    top += ROW_H;
  }
  top += 12.0f;
  if (state.stats.empty())
  {
    MenuWidget& none = builder->Add(Kind::Text, MenuItem::None);
    none.rect = {LEFT, top, RIGHT, top + 34.0f};
    none.label = "No Stats Yet";
    return;
  }
  // The first group is alone on the left.
  std::array<float, 2> y{top, top};
  for (std::size_t i = 0; i < state.stats.size(); ++i)
  {
    const std::size_t column = i == 0 ? 0 : 1;
    const float x = LEFT + (COLUMN_W + COLUMN_GAP) * static_cast<float>(column);
    MenuWidget& title = builder->Add(Kind::StatsTitle, MenuItem::None);
    title.rect = {x, y[column], x + COLUMN_W, y[column] + TITLE_H};
    title.label = state.stats[i].title;
    y[column] += TITLE_H;
    for (const StatsRow& row : state.stats[i].rows)
    {
      MenuWidget& w = builder->Add(Kind::Stat, MenuItem::None);
      w.rect = {x, y[column], x + COLUMN_W, y[column] + STAT_H};
      w.label = row.label;
      w.value = row.value;
      w.active = row.changed;
      y[column] += STAT_H;
    }
    y[column] += GROUP_GAP;
  }
}

constexpr float QUESTION_W = 480.0f;
constexpr float QUESTION_EDGE = 24.0f;
constexpr float QUESTION_TITLE_H = 34.0f;
constexpr float QUESTION_TEXT_H = 26.0f;
constexpr float QUESTION_GAP = 12.0f;

// In lines of about `max` letters; the last of `max_lines` has the rest.
std::vector<std::string> WrapWords(std::string_view text, std::size_t max, std::size_t max_lines)
{
  std::vector<std::string> lines;
  while (text.size() > max && lines.size() + 1 < max_lines)
  {
    std::size_t cut = text.rfind(' ', max);
    if (cut == 0 || cut == std::string_view::npos)
      cut = text.find(' ');
    if (cut == std::string_view::npos)
      break;
    lines.emplace_back(text.substr(0, cut));
    text.remove_prefix(cut + 1);
  }
  lines.emplace_back(text);
  return lines;
}

// A box with a title, lines of text and buttons, all in its middle; the last button is the filled
// one. Its height follows the text.
void AddQuestion(Builder* builder, const std::string& title, const std::vector<std::string>& text,
                 const std::vector<std::pair<std::string, int>>& buttons)
{
  const std::vector<std::string> title_lines = WrapWords(title, 34, 3);
  std::vector<std::string> text_lines;
  for (const std::string& line : text)
  {
    for (std::string& part : WrapWords(line, 44, 3))
      text_lines.push_back(std::move(part));
  }
  const float height = QUESTION_EDGE * 2.0f + QUESTION_GAP * 2.0f + 48.0f +
                       QUESTION_TITLE_H * static_cast<float>(title_lines.size()) +
                       QUESTION_TEXT_H * static_cast<float>(text_lines.size());
  // In the middle of the pane.
  const float cx = PANE.Cx();
  const float cy = PANE.Cy();
  MenuWidget& box = builder->Add(Kind::Question, MenuItem::Popup);
  box.rect = {cx - QUESTION_W * 0.5f, cy - height * 0.5f, cx + QUESTION_W * 0.5f,
              cy + height * 0.5f};
  box.label = title;
  box.options = title_lines;
  box.selected = static_cast<int>(title_lines.size());
  box.options.insert(box.options.end(), text_lines.begin(), text_lines.end());
  const float bottom = box.rect.y1 - QUESTION_EDGE;
  constexpr float WIDTH = 112.0f;
  constexpr float GAP = 12.0f;
  const float count = static_cast<float>(buttons.size());
  float x = cx - (WIDTH * count + GAP * (count - 1.0f)) * 0.5f;
  for (const auto& [label, index] : buttons)
  {
    MenuWidget& w = builder->Add(Kind::Button, MenuItem::PopupOption);
    w.index = index;
    w.rect = {x, bottom - 48.0f, x + WIDTH, bottom};
    w.label = label;
    w.active = &label == &buttons.back().first;
    x = w.rect.x1 + GAP;
  }
}

Rect GetGamesGrid(const MenuGames& games)
{
  return {kGamesX0, kGamesY0, kGamesX1, games.typing ? kGamesTypingY1 : kGamesY1};
}

void AddKeyboard(Builder* builder)
{
  static constexpr std::array<std::string_view, kGamesKeyRows> ROWS{"1234567890", "qwertyuiop",
                                                                    "asdfghjkl'", "zxcvbnm \b\n"};
  const float pitch = (kGamesX1 - kGamesX0 + kGamesKeyGap) / kGamesKeyColumns;
  for (int row = 0; row < kGamesKeyRows; ++row)
  {
    const float y = kGamesKeysY0 + static_cast<float>(row) * (kGamesKeyH + kGamesKeyGap);
    const std::string_view keys = ROWS[static_cast<std::size_t>(row)];
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
      const float x = kGamesX0 + static_cast<float>(i) * pitch;
      MenuWidget& w = builder->Add(Kind::Key, MenuItem::GamesKey);
      w.index = keys[i];
      w.rect = {x, y, x + pitch - kGamesKeyGap, y + kGamesKeyH};
      if (keys[i] == '\b')
        w.icon = Icon::kBackspace;
      else if (keys[i] == '\n')
        w.label = "Done";
      else if (keys[i] == ' ')
        w.label = "Space";
      else
        w.label = std::string(1, static_cast<char>(Common::ToUpper(keys[i])));
      w.active = keys[i] == '\n';
      if (keys[i] == '\b')
        w.tooltip = "Backspace";
    }
  }
}

// Azahar for Frame's Games tab: chips and buttons, the count, the tiles.
void AddGames(Builder* builder, const MenuState& state)
{
  const MenuGames& games = state.games;
  const std::vector<SystemFilter> chips = GameChips(games.list);
  const SystemFilter chosen = GameChip(games.list, games.chip);
  float x = kGamesX0;
  if (!games.search.empty() && !games.typing)
  {
    // In All's place.
    const float width =
        std::min(kGamesSearchChipMaxW,
                 std::ceil(10.0f * static_cast<float>(games.search.size()) + kGamesSearchChipRoom));
    MenuWidget& w = builder->Add(Kind::SearchChip, MenuItem::GamesSearch);
    w.rect = {x, kGamesTopY0, x + width, kGamesTopY1};
    w.label = games.search;
    MenuWidget& clear = builder->Add(Kind::IconButton, MenuItem::GamesSearchClear);
    clear.rect = {x + width - kGamesSearchChipClose, kGamesTopY0, x + width, kGamesTopY1};
    clear.icon = Icon::kClose;
    clear.tooltip = "Clear Search";
    x += width + kGamesChipGap;
  }
  for (const SystemFilter chip : chips)
  {
    if (games.typing || (chip == SystemFilter::All && !games.search.empty()))
      continue;
    MenuWidget& w = builder->Add(Kind::GamesChip, MenuItem::GamesChip);
    w.index = static_cast<int>(chip);
    w.label = ChipName(chip);
    const float width = std::max(
        kGamesChipMinW, std::ceil(10.7f * static_cast<float>(w.label.size()) + kGamesChipRoom));
    w.rect = {x, kGamesTopY0, x + width, kGamesTopY1};
    w.active = chip == chosen;
    x = w.rect.x1 + kGamesChipGap;
  }

  const Rect add_rect{kGamesX1 - kGamesRoundR * 2.0f, kGamesTopY0, kGamesX1, kGamesTopY1};
  const Rect sort_rect{add_rect.x0 - 56.0f, kGamesTopY0, add_rect.x1 - 56.0f, kGamesTopY1};
  if (games.typing)
  {
    MenuWidget& field = builder->Add(Kind::SearchField, MenuItem::GamesSearch);
    field.rect = {kGamesX0, kGamesTopY0, kGamesX1, kGamesTopY1};
    field.label = games.search;
    if (!games.search.empty())
    {
      MenuWidget& clear = builder->Add(Kind::IconButton, MenuItem::GamesSearchClear);
      clear.rect = {kGamesX1 - 48.0f, kGamesTopY0, kGamesX1, kGamesTopY1};
      clear.icon = Icon::kClose;
      clear.tooltip = "Clear Search";
    }
    AddKeyboard(builder);
  }
  else if (games.list && games.has_window && games.search.empty())
  {
    MenuWidget& search = builder->Add(Kind::RoundButton, MenuItem::GamesSearch);
    search.rect = {sort_rect.x0 - 56.0f, kGamesTopY0, sort_rect.x1 - 56.0f, kGamesTopY1};
    search.label = "Search";
    search.tooltip = "Search";
  }
  if (games.list && games.has_window && !games.typing)
  {
    MenuWidget& sort = builder->Add(Kind::RoundButton, MenuItem::GamesSort);
    sort.rect = sort_rect;
    sort.label = "Sort";
    sort.tooltip = "Sort";
    sort.active = games.popup == MenuGames::Popup::Sort;
    MenuWidget& add = builder->Add(Kind::RoundButton, MenuItem::GamesAddFolder);
    add.rect = add_rect;
    add.label = "Add Folder";
    add.tooltip = "Add Folder";
  }

  MenuWidget& count = builder->Add(Kind::GamesCount, MenuItem::None);
  const float cy = chips.empty() ? (kGamesTopY0 + kGamesTopY1) * 0.5f : kGamesCountCy;
  count.rect = {kGamesX0, cy - 10.0f, kGamesX1, cy + 10.0f};
  if (!games.has_window)
    count.label = "The list of games is in Baiji VR's window.";
  else if (games.list)
    count.label = GameCount(games.shown.size(), !games.search.empty()) + " \u00b7 " + SortName(games.sort);

  const std::size_t first = builder->GetCount();
  const Rect GRID = GetGamesGrid(games);
  const Rect CLIP{GRID.x0 - kGamesClipRoom, GRID.y0 - kGamesClipRoom, GRID.x1 + kGamesClipRoom,
                      GRID.y1 + kGamesClipRoom};
  Rect options_rect{};
  for (std::size_t i = 0; i < games.shown.size(); ++i)
  {
    const float tile_x = GRID.x0 + static_cast<float>(i % kGamesColumns) * (kGameTileW + kGameGap);
    const float tile_y =
        GRID.y0 + static_cast<float>(i / kGamesColumns) * kGameRow - games.scroll;
    const GameEntry* const game = games.Find(games.shown[i]);
    if (!game || tile_y + kGameTileH <= CLIP.y0 || tile_y >= CLIP.y1)
      continue;
    const Rect rect{tile_x, tile_y, tile_x + kGameTileW, tile_y + kGameTileH};
    const bool running = !games.running.empty() && game->path == games.running;
    MenuWidget& tile = builder->Add(Kind::GameTile, MenuItem::GameTile);
    tile.index = games.shown[i];
    tile.rect = rect;
    tile.label = game->title;
    tile.active = running;
    tile.tooltip = GameTooltip(*game, running);
    if (!games.has_window)
      continue;
    MenuWidget& options = builder->Add(Kind::GameOptions, MenuItem::GameOptions);
    options.index = games.shown[i];
    options.rect = {rect.x1 - kGameOptionsTarget, rect.y0, rect.x1, rect.y0 + kGameOptionsTarget};
    options.label = "Options: " + game->title;
    options.tooltip = "Options";
    options.active = (games.popup == MenuGames::Popup::Options ||
                      games.popup == MenuGames::Popup::Covers) &&
                     games.game == game->path;
    if (options.active)
      options_rect = options.rect;
  }
  builder->Clip(first, CLIP);

  if (games.shown.empty() && !games.search.empty())
  {
    MenuWidget& none = builder->Add(Kind::NoMatches, MenuItem::None);
    none.rect = GRID;
    none.label = "No Matches";
    MenuWidget& clear = builder->Add(Kind::Button, MenuItem::GamesSearchClear);
    clear.label = "Clear Search";
    clear.rect = {GRID.Cx() - 76.0f, GRID.Cy() + 28.0f, GRID.Cx() + 76.0f, GRID.Cy() + 76.0f};
  }
  if (const std::optional<MenuList> list = VrMenu::GetList(state); list && list->bar.Valid())
  {
    MenuWidget& bar = builder->Add(Kind::Scrollbar, MenuItem::Scrollbar);
    bar.rect = Scrolling::HitRect(list->bar);
    bar.rect.x0 = kGamesX1;
  }

  switch (games.popup)
  {
  case MenuGames::Popup::None:
    break;
  case MenuGames::Popup::Sort:
  {
    std::vector<std::string> options;
    for (int i = 0; i < static_cast<int>(GamesSort::Count); ++i)
      options.emplace_back(SortName(static_cast<GamesSort>(i)));
    AddMenu(builder, sort_rect, kGamesSortMenuW, std::move(options),
            static_cast<int>(games.sort));
    break;
  }
  case MenuGames::Popup::Options:
  case MenuGames::Popup::Covers:
  {
    const GameEntry* const game = games.FindGame(games.game);
    if (!game || options_rect.W() <= 0.0f)
      break;
    Rect anchor = options_rect;
    anchor.y0 = std::max(anchor.y0, CLIP.y0);
    anchor.x1 = std::max(anchor.x1, kGamesX0 + kGamesOptionsMenuW);
    if (games.popup == MenuGames::Popup::Covers)
    {
      AddMenu(builder, anchor, kGamesOptionsMenuW, {"Box Art", "Title Screen", "Screenshot"}, -1);
      break;
    }
    AddMenu(builder, anchor, kGamesOptionsMenuW,
            {game->path == games.running ? "Continue" : "Play", "Favourite", "Choose Another Cover",
             "No Cover", "Use My Own Image", "Reset Cover", "Download Thumbnails"},
            game->favorite ? MenuGames::Favourite : -1);
    break;
  }
  case MenuGames::Popup::Question:
  {
    const GameEntry* const game = games.FindGame(games.game);
    if (!game)
      break;
    const GameEntry* const running = games.FindGame(games.running);
    std::vector<std::string> text{
        fmt::format("{} will close.", running ? running->title : state.title)};
    if (games.loses_progress)
      text.emplace_back("Progress not saved in the game is lost.");
    AddQuestion(builder, fmt::format("Start {}?", game->title), text,
                {{"Cancel", MenuGames::Cancel}, {"Start", MenuGames::Start}});
    break;
  }
  }
}

constexpr float FILE_ROW_H = 56.0f;
constexpr float FILE_PLACE_H = 36.0f;
constexpr Rect FILE_LIST{PANE.x0, TOP + 48.0f + 34.0f + FILE_PLACE_H + 12.0f, PANE.x1,
                         PANE.y1 - 8.0f};

int CountFileRows(const FileListing& listing)
{
  return static_cast<int>(listing.rows.size()) + (listing.parent.empty() ? 0 : 1);
}

// Add Folder and Use My Own Image: where it is, other places to go, what is in it.
void AddFiles(Builder* builder, const MenuState& state)
{
  const MenuFiles& files = state.files;
  const FileListing& listing = files.listing;
  const bool folder = files.purpose == FilePurpose::Folder;
  const bool reading = !files.wanted.empty();
  MenuWidget& back = builder->Add(Kind::Back, MenuItem::Back);
  back.rect = {LEFT - 4.0f, TOP, LEFT + 44.0f, TOP + 48.0f};
  MenuWidget& headline = builder->Add(Kind::Headline, MenuItem::None);
  headline.rect = {LEFT + 56.0f, TOP, RIGHT - 200.0f, TOP + 48.0f};
  headline.label = folder ? "Add Folder" : "Use My Own Image";
  if (folder)
  {
    MenuWidget& add = builder->Add(Kind::Button, MenuItem::FileAdd);
    add.rect = {RIGHT - 172.0f, TOP, RIGHT, TOP + 48.0f};
    add.label = "Add This Folder";
    add.active = true;
    add.disabled = reading || !listing.readable;
  }
  MenuWidget& path = builder->Add(Kind::FilePath, MenuItem::None);
  path.rect = {LEFT, TOP + 52.0f, RIGHT, TOP + 76.0f};
  path.label = reading ? files.wanted : listing.path;
  if (reading)
    path.value = "Reading";
  else if (!listing.readable)
    path.value = "Cannot Be Read";
  else if (folder)
    path.value = GameCount(static_cast<std::size_t>(listing.games), false);

  float x = LEFT;
  const float place_y = TOP + 82.0f;
  for (std::size_t i = 0; i < listing.places.size(); ++i)
  {
    const float width =
        std::min(220.0f, 28.0f + 8.0f * static_cast<float>(listing.places[i].first.size()));
    if (x + width > RIGHT)
      break;
    MenuWidget& w = builder->Add(Kind::Choice, MenuItem::FilePlace);
    w.index = static_cast<int>(i);
    w.rect = {x, place_y, x + width, place_y + FILE_PLACE_H};
    w.label = listing.places[i].first;
    w.active = listing.places[i].second == listing.path;
    x = w.rect.x1 + 8.0f;
  }

  const std::size_t first = builder->GetCount();
  const int up = listing.parent.empty() ? 0 : 1;
  for (int i = 0; i < CountFileRows(listing); ++i)
  {
    const float y = FILE_LIST.y0 + static_cast<float>(i) * FILE_ROW_H - files.scroll;
    if (y + FILE_ROW_H <= FILE_LIST.y0 || y >= FILE_LIST.y1)
      continue;
    MenuWidget& w = builder->Add(Kind::FileRow, MenuItem::FileRow);
    w.index = i - up;
    w.rect = {LEFT - 12.0f, y, RIGHT, y + FILE_ROW_H};
    w.disabled = reading;
    if (w.index < 0)
    {
      w.label = "Up";
      continue;
    }
    const FileRow& row = listing.rows[static_cast<std::size_t>(w.index)];
    w.label = row.name;
    w.icon = row.is_folder ? Icon::kFolderOpen : Icon::kPhotoCamera;
    if (row.games > 0)
      w.value = GameCount(static_cast<std::size_t>(row.games), false);
  }
  builder->Clip(first, FILE_LIST);
  if (const std::optional<MenuList> list = VrMenu::GetList(state); list && list->bar.Valid())
  {
    MenuWidget& bar = builder->Add(Kind::Scrollbar, MenuItem::Scrollbar);
    bar.rect = Scrolling::HitRect(list->bar);
    bar.rect.x0 = RIGHT;
  }
}

void AddSettings(Builder* builder, const MenuState& state)
{
  MenuWidget& section = builder->Add(Kind::Segmented, MenuItem::Section);
  section.rect = {LEFT, TOP, LEFT + SECTIONS_W, TOP + CONTROL_H};
  section.control = section.rect;
  section.options = {"General", "Graphics", "Immersive"};
  section.selected = static_cast<int>(state.section) - static_cast<int>(MenuSection::General);
  AddScope(builder, state, state.section);
  AddList(builder, state, state.section);
  AddScopeMenu(builder, state);
}
}  // namespace

std::string GetControllerChipLabel(std::string_view name)
{
  constexpr std::string_view REMOTE = "Wii Remote ";
  constexpr std::string_view CONTROLLER = " Controller";
  if (name.ends_with(CONTROLLER) && name.size() > CONTROLLER.size())
    return std::string(name.substr(0, name.size() - CONTROLLER.size()));
  if (!name.starts_with(REMOTE) || name.size() == REMOTE.size())
    return std::string(name);
  const std::string_view rest = name.substr(REMOTE.size());
  if (rest.size() > 2 && rest.front() == '(' && rest.back() == ')')
    return std::string(rest.substr(1, rest.size() - 2));
  return std::string(rest);
}

std::vector<std::string> GetPadProfileOptions(const MenuState& state)
{
  std::vector<std::string> options{NO_PAD_PROFILE};
  // The game's is always there.
  if (!state.pad_profile.empty() &&
      std::ranges::find(state.pad_profiles, state.pad_profile) == state.pad_profiles.end())
  {
    options.push_back(state.pad_profile);
  }
  options.insert(options.end(), state.pad_profiles.begin(), state.pad_profiles.end());
  if (options.size() <= MAX_PAD_PROFILES)
    return options;
  constexpr std::size_t PAGE = MAX_PAD_PROFILES - 1;
  const std::size_t pages = (options.size() + PAGE - 1) / PAGE;
  const std::size_t first = static_cast<std::size_t>(state.pad_profile_page) % pages * PAGE;
  const std::size_t last = std::min(first + PAGE, options.size());
  std::vector<std::string> page(options.begin() + static_cast<std::ptrdiff_t>(first),
                                options.begin() + static_cast<std::ptrdiff_t>(last));
  page.push_back(MORE_PAD_PROFILES);
  return page;
}

PadKind GetControllerPad(int controller)
{
  // In the order of VRWiiController::Choice.
  static constexpr std::array PADS{PadKind::GameCube, PadKind::WiiRemote, PadKind::WiiNunchuk,
                                   PadKind::WiiRemoteSideways, PadKind::WiiClassic};
  static_assert(PADS.size() == VRWiiController::NAMES.size());
  return controller >= 0 && controller < static_cast<int>(PADS.size()) ?
             PADS[static_cast<std::size_t>(controller)] :
             PadKind::WiiRemote;
}

const char* GetPadName(PadKind kind)
{
  for (int i = 0; i < static_cast<int>(VRWiiController::NAMES.size()); ++i)
  {
    if (GetControllerPad(i) == kind)
      return VRWiiController::NAMES[static_cast<std::size_t>(i)];
  }
  return VRWiiController::NAMES[0];
}

bool ShowsGame(MenuTab tab, MenuSection section)
{
  switch (tab)
  {
  case MenuTab::Position:
    return true;
  case MenuTab::Settings:
    return section != MenuSection::General;
  case MenuTab::Main:
  case MenuTab::Games:
  case MenuTab::Stats:
  case MenuTab::Controls:
  case MenuTab::Mods:
    break;
  }
  return false;
}

const GameEntry* MenuGames::Find(int index) const
{
  return list && index >= 0 && index < static_cast<int>(list->games.size()) ?
             &list->games[static_cast<std::size_t>(index)] :
             nullptr;
}

const GameEntry* MenuGames::FindGame(const std::string& path) const
{
  return Find(path.empty() ? -1 : GameAt(list, path));
}

void MenuGames::UpdateShown()
{
  chip = GameChip(list, chip);
  shown = GamesShown(list, chip, sort, search);
  if (popup != Popup::Sort && !FindGame(game))
    popup = Popup::None;
}

void MenuGames::Type(char key)
{
  if (key == '\n')
  {
    typing = false;
    return;
  }
  if (key == '\b')
  {
    if (!search.empty())
      search.pop_back();
  }
  else if (search.size() < kGamesSearchMax && !(key == ' ' && search.empty()))
  {
    search += key;
  }
  scroll = 0.0f;
  UpdateShown();
}

float& GetScroll(MenuState* state, const MenuList& list)
{
  if (list.files)
    return state->files.scroll;
  return list.games ? state->games.scroll : state->scroll[static_cast<std::size_t>(list.section)];
}

float GetScroll(const MenuState& state, const MenuList& list)
{
  if (list.files)
    return state.files.scroll;
  return list.games ? state.games.scroll : state.scroll[static_cast<std::size_t>(list.section)];
}

std::vector<MenuWidget> VrMenu::Layout(const MenuState& state)
{
  Builder builder;
  AddRail(&builder, state);
  switch (state.tab)
  {
  case MenuTab::Main:
    if (state.page == MenuPage::Main)
      AddMain(&builder, state);
    else
      AddSlots(&builder, state);
    break;
  case MenuTab::Games:
    if (state.page == MenuPage::Files)
      AddFiles(&builder, state);
    else
      AddGames(&builder, state);
    break;
  case MenuTab::Position:
    AddScreen(&builder, state);
    break;
  case MenuTab::Stats:
    AddStats(&builder, state);
    break;
  case MenuTab::Controls:
    AddControls(&builder, state);
    break;
  case MenuTab::Mods:
    AddMods(&builder, state);
    break;
  case MenuTab::Settings:
    AddSettings(&builder, state);
    break;
  }
  return builder.Take();
}

bool VrMenu::CanFocus(const MenuWidget& w, bool popup)
{
  if (w.disabled)
    return false;
  if (popup)
    return w.item == MenuItem::PopupOption;
  switch (w.item)
  {
  case MenuItem::Back:
  case MenuItem::GamesSearchClear:
  case MenuItem::GamesKey:
  case MenuItem::FilePlace:
  case MenuItem::FileRow:
  case MenuItem::FileAdd:
    return true;
  default:
    return false;
  }
}

const MenuWidget* VrMenu::Next(const std::vector<MenuWidget>& widgets, float x, float y, int dx,
                               int dy)
{
  const bool popup = std::ranges::any_of(
      widgets, [](const MenuWidget& w) { return w.item == MenuItem::PopupOption; });
  const MenuWidget* best = nullptr;
  float best_cost = 0.0f;
  for (const MenuWidget& w : widgets)
  {
    if (!CanFocus(w, popup))
      continue;
    const float along = dx != 0 ? (w.rect.Cx() - x) * static_cast<float>(dx) :
                                  (w.rect.Cy() - y) * static_cast<float>(dy);
    if (along < 4.0f)
      continue;
    const float aside = dx != 0 ? std::max({w.rect.y0 - y, y - w.rect.y1, 0.0f}) :
                                  std::max({w.rect.x0 - x, x - w.rect.x1, 0.0f});
    const float cost = along + aside * 4.0f;
    if (!best || cost < best_cost)
    {
      best = &w;
      best_cost = cost;
    }
  }
  return best;
}

std::optional<MenuList> VrMenu::GetList(const MenuState& state)
{
  if (state.tab == MenuTab::Games && state.page == MenuPage::Files)
  {
    const float height = FILE_ROW_H * static_cast<float>(CountFileRows(state.files.listing));
    return MenuList{{FILE_LIST, PANE.x1, height, std::max(0.0f, height - FILE_LIST.H())},
                    MenuSection::General, FILE_ROW_H, false, true};
  }
  if (state.tab == MenuTab::Games)
  {
    const Rect grid = GetGamesGrid(state.games);
    const int rows = (static_cast<int>(state.games.shown.size()) + kGamesColumns - 1) / kGamesColumns;
    const float content = std::max(0.0f, static_cast<float>(rows) * kGameRow - kGameGap);
    return MenuList{{grid, kGamesBarRight, content, std::max(0.0f, content - grid.H())},
                    MenuSection::General, kGameRow, true};
  }
  const std::optional<MenuSection> section = GetSection(state);
  if (!section)
    return std::nullopt;
  const float height = ROW_H * static_cast<float>(GetRows(*section).size());
  return MenuList{{LIST, PANE.x1, height, std::max(0.0f, height - LIST.H())}, *section, ROW_H};
}

const MenuWidget* VrMenu::At(const std::vector<MenuWidget>& widgets, float x, float y, bool greyed)
{
  // What is drawn last is on top.
  for (const MenuWidget& widget : std::views::reverse(widgets))
  {
    if (widget.item == MenuItem::None || !widget.rect.Contains(x, y) ||
        (widget.clipped && !widget.clip.Contains(x, y)))
    {
      continue;
    }
    if (!widget.disabled || (greyed && !widget.tooltip.empty()))
      return &widget;
  }
  return nullptr;
}

int VrMenu::OptionAt(const MenuWidget& widget, float x)
{
  const Rect& r = widget.control;
  if (widget.options.empty() || x < r.x0 || x >= r.x1)
    return -1;
  const auto count = static_cast<float>(widget.options.size());
  return std::min(static_cast<int>((x - r.x0) / r.W() * count),
                  static_cast<int>(widget.options.size()) - 1);
}

float VrMenu::SliderAt(const MenuWidget& widget, float x)
{
  const float x0 = widget.control.x0;
  const float x1 = widget.control.x1 - SLIDER_VALUE_W - SLIDER_VALUE_GAP;
  return std::clamp((x - x0) / (x1 - x0), 0.0f, 1.0f);
}

void VrMenu::Draw(Kit* kit, Canvas* canvas, const MenuState& state, const GetPicture& get_picture,
                  GamesDraw* games)
{
  const auto draw_picture = [&](const Rect& r, const std::string& path, u32 icon_rgb) {
    const MenuPicture* const picture = path.empty() ? nullptr : get_picture(path);
    if (picture)
    {
      kit->Image(picture->pixels.data(), picture->width, picture->height, r);
      return;
    }
    kit->Fill(r, 8.0f, Tok::kSurfaceContainerHighest);
    kit->Icon(r.Cx(), r.Cy(), 28.0f, Icon::kHistory, false, icon_rgb);
  };

  std::fill(canvas->px.begin(), canvas->px.end(), 0u);
  kit->Begin(*canvas);
  kit->Fill({0.0f, 0.0f, WIDTH, HEIGHT}, 28.0f, Tok::kSurfaceContainer);
  kit->Fill(PANE, 24.0f, Tok::kSurface);

  const std::vector<MenuWidget> widgets = Layout(state);
  const MenuWidget* pointed = nullptr;
  for (const MenuWidget& w : widgets)
  {
    if (w.clipped)
      kit->Clip(w.clip);
    else
      kit->ClearClip();
    if (w.item != MenuItem::None && w.item == state.hover && w.index == state.hover_index)
      pointed = &w;

    State look = State::Rest;
    if (w.disabled)
      look = State::Disabled;
    else if (w.item != MenuItem::None && w.index == state.hover_index && w.item == state.pressed)
      look = State::Pressed;
    else if (w.item != MenuItem::None && w.index == state.hover_index && w.item == state.hover)
      look = State::Hover;

    const float opacity = w.disabled ? kDisabledContent : 1.0f;
    const bool row = w.kind == Kind::Segmented || w.kind == Kind::Switch ||
                     w.kind == Kind::Slider || w.kind == Kind::Dropdown;
    if (row && !w.label.empty())
    {
      const float y = w.rect.Cy() - (w.note.empty() ? 0.0f : 10.0f);
      kit->TextMid(LEFT, y, Type::Body, Tok::kOnSurface, w.label, 0, opacity);
      if (!w.note.empty())
      {
        kit->TextMid(LEFT, w.rect.Cy() + 14.0f, Type::Tooltip, Tok::kOnSurfaceVariant, w.note, 0,
                     opacity);
      }
    }

    switch (w.kind)
    {
    case Kind::Headline:
      kit->TextMid(w.rect.x0, w.rect.Cy(), Type::Headline, Tok::kOnSurface,
                   kit->Fit(Type::Headline, w.label, w.rect.W()));
      break;
    case Kind::Text:
      kit->TextMid(w.rect.x0, w.rect.Cy(), Type::Body, Tok::kOnSurface, w.label);
      if (!w.value.empty())
      {
        kit->TextMid(w.rect.x1, w.rect.Cy(), Type::Body, Tok::kOnSurfaceVariant, w.value, 2);
      }
      break;
    case Kind::Rail:
      kit->RailItem(w.rect, w.icon, w.label, w.active, look);
      if (w.item == MenuItem::TabGames)
        DrawGamesIcon(*kit, w.rect.Cx(), w.rect.y0 + RAIL_ICON_Y, w.active);
      break;
    case Kind::Tile:
      kit->Tile(w.rect,
                w.danger ? ButtonStyle::Danger :
                w.active ? ButtonStyle::Filled :
                           ButtonStyle::Tonal,
                w.icon, w.label, look);
      break;
    case Kind::Button:
      kit->Button(w.rect,
                  w.danger ? ButtonStyle::Danger :
                  w.active ? ButtonStyle::Filled :
                  w.item == MenuItem::AutosaveSettings ? ButtonStyle::Text :
                                                         ButtonStyle::Tonal,
                  w.icon, w.label, look, w.rect.H() >= 56.0f ? Type::Title : Type::Label);
      break;
    case Kind::Segmented:
    {
      const bool hovered =
          w.item == state.hover && w.index == state.hover_index && !w.disabled;
      kit->Segmented(w.control, w.options, w.selected, hovered ? state.hover_option : -1,
                     look == State::Pressed ? state.hover_option : -1, w.disabled);
      break;
    }
    case Kind::Switch:
      kit->Switch(w.control, w.active, look);
      break;
    case Kind::Slider:
      kit->Slider(w.control, w.t, w.value, look, 0, SLIDER_VALUE_W);
      break;
    case Kind::Back:
      kit->StateLayer(w.rect, 24.0f, Tok::kOnSurfaceVariant, look);
      kit->Icon(w.rect.Cx(), w.rect.Cy(), 24.0f, Icon::kArrowBack, false, Tok::kOnSurfaceVariant);
      break;
    case Kind::Slot:
      kit->Fill(w.rect, 16.0f, Tok::kSurfaceContainerHigh, w.disabled ? 0.5f : 1.0f);
      kit->StateLayer(w.rect, 16.0f, Tok::kOnSurface, look);
      kit->TextMid(w.rect.x0 + 20.0f, w.rect.Cy(), Type::Label, Tok::kOnSurface, w.label, 0,
                   opacity);
      kit->TextMid(w.rect.x1 - 20.0f, w.rect.Cy(), Type::Body, Tok::kOnSurfaceVariant, w.value, 2,
                   opacity);
      break;
    case Kind::AutosaveCard:
    {
      const u32 color = Tok::kOnSecondaryContainer;
      const Rect& c = w.rect;
      const Rect picture{c.x0 + 12.0f, c.y0 + 8.0f, c.x0 + 12.0f + (c.H() - 16.0f) * 4.0f / 3.0f,
                         c.y1 - 8.0f};
      kit->Fill(c, 16.0f, Tok::kSecondaryContainer, w.active && w.disabled ? 0.5f : 1.0f);
      if (w.disabled)
      {
        const float shown = w.active ? kDisabledContent : 1.0f;
        kit->Icon(picture.Cx(), picture.Cy(), 32.0f, Icon::kHistory, false, color, shown);
        kit->TextMid(picture.x1 + 16.0f, c.Cy(), Type::Label, color,
                     w.active ? "Autosave \u00b7 None Yet" : "Autosave: Off", 0, shown);
        break;
      }
      const MenuAutosave& newest = state.saves.autosaves.front();
      kit->StateLayer(c, 16.0f, color, state.autosave_list ? State::Pressed : look);
      draw_picture(picture, newest.picture, color);
      float right = c.x1 - 20.0f;
      if (state.saves.autosaves.size() > 1)
      {
        kit->Icon(c.x1 - 28.0f, c.Cy(), 24.0f, Icon::kArrowDropDown, false, color);
        right = c.x1 - 52.0f;
      }
      kit->TextMid(right, c.Cy(), Type::Body, color, newest.time, 2, 0.8f);
      const float room =
          right - kit->TextWidth(Type::Body, newest.time) - 16.0f - picture.x1 - 16.0f;
      kit->TextMid(picture.x1 + 16.0f, c.Cy(), Type::Label, color,
                   kit->Fit(Type::Label, "Autosave \u00b7 " + newest.ago, room));
      break;
    }
    case Kind::Popup:
      kit->Fill(w.rect, 16.0f, Tok::kSurfaceContainerHigh);
      kit->Stroke(w.rect, 16.0f, 1.0f, Tok::kOutlineVariant);
      break;
    case Kind::AutosaveEntry:
    {
      const MenuAutosave& autosave = state.saves.autosaves[static_cast<std::size_t>(w.index)];
      const Rect& c = w.rect;
      kit->StateLayer(c, 12.0f, Tok::kOnSurface, look);
      const Rect picture{c.x0 + 12.0f, c.y0 + 6.0f, c.x0 + 12.0f + (c.H() - 12.0f) * 4.0f / 3.0f,
                         c.y1 - 6.0f};
      draw_picture(picture, autosave.picture, Tok::kOnSurfaceVariant);
      kit->TextMid(c.x1 - 16.0f, c.Cy(), Type::Body, Tok::kOnSurfaceVariant, w.value, 2);
      const float room =
          c.x1 - 28.0f - kit->TextWidth(Type::Body, w.value) - picture.x1 - 16.0f;
      kit->TextMid(picture.x1 + 16.0f, c.Cy(), Type::Label, Tok::kOnSurface,
                   kit->Fit(Type::Label, w.label, room));
      break;
    }
    case Kind::Snackbar:
      kit->Fill(w.rect, 8.0f, Tok::kInverseSurface);
      kit->StateLayer(w.rect, 8.0f, Tok::kInverseOnSurface, look);
      kit->TextMid(w.rect.x0 + 20.0f, w.rect.Cy(), Type::Body, Tok::kInverseOnSurface, w.label);
      kit->TextMid(w.rect.x1 - 20.0f, w.rect.Cy(), Type::Label, Tok::kInversePrimary, w.value, 2);
      break;
    case Kind::Mod:
    {
      const GameMods::Item& item = state.mods[static_cast<std::size_t>(w.index)];
      const Rect& t = w.control;
      kit->Fill(w.rect, 16.0f, Tok::kSurfaceContainerHigh);
      // Recommended has the seal, what is only verified says so.
      constexpr std::string_view VERIFIED = "Verified";
      const float chip_w = kit->TextWidth(Type::Tooltip, std::string(VERIFIED)) + 16.0f;
      const bool chip = item.verified && !item.recommended;
      const float extra = item.recommended ? 28.0f : chip ? chip_w + 10.0f : 0.0f;
      const std::string name = kit->Fit(Type::Label, item.name, t.W() - extra);
      const float y = w.rect.y0 + 23.0f;
      kit->TextMid(t.x0, y, Type::Label, Tok::kOnSurface, name);
      const float after = t.x0 + kit->TextWidth(Type::Label, name) + 10.0f;
      if (item.recommended)
      {
        kit->Icon(after + 8.0f, y, 20.0f, Icon::kVerified, true, Tok::kPrimary);
      }
      else if (chip)
      {
        kit->Fill({after, y - 11.0f, after + chip_w, y + 11.0f}, 8.0f, Tok::kSecondaryContainer);
        kit->TextMid(after + 8.0f, y, Type::Tooltip, Tok::kOnSecondaryContainer,
                     std::string(VERIFIED));
      }
      const std::string about = item.notes.empty() ? item.author : item.notes;
      kit->TextMid(t.x0, w.rect.y1 - 20.0f, Type::Tooltip, Tok::kOnSurfaceVariant,
                   kit->Fit(Type::Tooltip, about, t.W()));
      if (state.mods_progress.id == item.id && IsInstalling(state.mods_progress) &&
          state.mods_progress.bytes_total > 0)
      {
        const float done = static_cast<float>(state.mods_progress.bytes_done) /
                           static_cast<float>(state.mods_progress.bytes_total);
        const Rect bar{w.rect.x0 + 16.0f, w.rect.y1 - 5.0f, w.rect.x1 - 16.0f, w.rect.y1 - 2.0f};
        kit->Fill(bar, 1.5f, Tok::kSurfaceContainerHighest);
        kit->Fill({bar.x0, bar.y0, bar.x0 + bar.W() * std::min(done, 1.0f), bar.y1}, 1.5f,
                  Tok::kPrimary);
      }
      break;
    }
    case Kind::IconButton:
      kit->StateLayer(w.rect, 24.0f, Tok::kOnSurfaceVariant, look);
      kit->Icon(w.rect.Cx(), w.rect.Cy(), 24.0f, w.icon, false, Tok::kOnSurfaceVariant, opacity);
      break;
    case Kind::Pad:
      DrawPad(kit, canvas, w.rect, state.pad, state.pad_labels);
      break;
    case Kind::EditPad:
    {
      std::vector<PadLink> links;
      for (const MenuWidget& chip : widgets)
      {
        const bool lit = state.edit.listening != PadTarget::None ?
                             chip.active :
                             chip.item == state.hover && chip.index == state.hover_index;
        if (chip.kind != Kind::Chip || !lit)
          continue;
        links.push_back(GetLink(static_cast<PadTarget>(chip.index), chip.control.x0,
                                chip.control.y0));
      }
      DrawPadArt(kit, canvas, w.rect, state.pad, links);
      break;
    }
    case Kind::Chip:
    {
      const u32 text = w.active ? Tok::kOnPrimaryContainer : Tok::kOnSurface;
      const float shown = w.disabled ? kDisabledContent : 1.0f;
      const float room = w.rect.W() - 24.0f;
      kit->Fill(w.rect, 12.0f, w.active ? Tok::kPrimaryContainer : Tok::kSurfaceContainerHigh);
      kit->StateLayer(w.rect, 12.0f, Tok::kOnSurface, look);
      kit->TextMid(w.rect.x0 + 12.0f, w.rect.y0 + 13.0f, Type::Tooltip,
                   w.active ? text : Tok::kOnSurfaceVariant, w.label, 0, shown);
      kit->TextMid(w.rect.x0 + 12.0f, w.rect.y0 + 29.0f, Type::RailLabel, text,
                   kit->Fit(Type::RailLabel, w.value, room), 0, shown);
      break;
    }
    case Kind::Choice:
    {
      const float shown = w.disabled ? kDisabledContent : 1.0f;
      if (w.active)
        kit->Fill(w.rect, 8.0f, Tok::kSecondaryContainer, shown);
      else
        kit->Stroke(w.rect, 8.0f, 1.0f, Tok::kOutline, shown);
      kit->StateLayer(w.rect, 8.0f, Tok::kOnSurface, look);
      kit->TextMid(w.rect.Cx(), w.rect.Cy(), Type::RailLabel,
                   w.active ? Tok::kOnSecondaryContainer : Tok::kOnSurfaceVariant,
                   kit->Fit(Type::RailLabel, w.label, w.rect.W() - 12.0f), 1, shown);
      break;
    }
    case Kind::Guide:
      kit->TextMid(w.rect.x0, w.rect.y0 + 14.0f, Type::Label,
                   w.active ? Tok::kPrimary : Tok::kOnSurface,
                   kit->Fit(Type::Label, w.label, w.rect.W()));
      kit->TextMid(w.rect.x0, w.rect.y0 + 36.0f, Type::Tooltip, Tok::kOnSurfaceVariant,
                   kit->Fit(Type::Tooltip, w.note, w.rect.W()));
      break;
    case Kind::Header:
      kit->Line(w.rect.x0, w.rect.y0 + 18.0f, w.rect.x1, w.rect.y0 + 18.0f, 1.0f,
                Tok::kOutlineVariant);
      kit->TextMid(w.rect.x0, w.rect.y0 + 44.0f, Type::RailLabel, Tok::kPrimary, w.label);
      break;
    case Kind::StatsTitle:
      kit->TextMid(w.rect.x0, w.rect.Cy(), Type::RailLabel, Tok::kPrimary, w.label);
      break;
    case Kind::Stat:
    {
      const float room = w.rect.W() - kit->TextWidth(Type::Body, w.label) - 12.0f;
      kit->TextMid(w.rect.x0, w.rect.Cy(), Type::Body, Tok::kOnSurfaceVariant, w.label);
      kit->TextMid(w.rect.x1, w.rect.Cy(), Type::Value, w.active ? Tok::kPrimary : Tok::kOnSurface,
                   kit->Fit(Type::Value, w.value, room), 2);
      break;
    }
    case Kind::Dropdown:
      kit->MenuButton(w.control, kit->Fit(Type::Label, w.value, w.control.W() - 64.0f), w.active,
                      look);
      break;
    case Kind::Menu:
      kit->Menu(w.rect, w.options, w.selected,
                state.hover == MenuItem::PopupOption ? state.hover_index : -1);
      kit->Stroke(w.rect, 16.0f, 1.0f, Tok::kOutlineVariant);
      break;
    case Kind::Option:
      break;
    case Kind::More:
    {
      kit->StateLayer({w.rect.Cx() - 16.0f, w.rect.Cy() - 16.0f, w.rect.Cx() + 16.0f,
                       w.rect.Cy() + 16.0f},
                      16.0f, Tok::kOnSurfaceVariant, look);
      for (const float dy : {-6.0f, 0.0f, 6.0f})
        kit->Dot(w.rect.Cx(), w.rect.Cy() + dy, 2.0f, Tok::kOnSurfaceVariant, opacity);
      break;
    }
    case Kind::GameTile:
    {
      const GameEntry* const game = state.games.Find(w.index);
      if (!game)
        break;
      GameTileLook tile;
      tile.running = w.active;
      const bool on_tile = w.index == state.hover_index && (state.hover == MenuItem::GameTile ||
                                                            state.hover == MenuItem::GameOptions);
      const bool open = (state.games.popup == MenuGames::Popup::Options ||
                         state.games.popup == MenuGames::Popup::Covers) &&
                        state.games.game == game->path;
      tile.pointed = open || (on_tile && state.games.popup == MenuGames::Popup::None);
      tile.tile = on_tile && look == State::Rest ? State::Hover : look;
      if (open || (on_tile && state.pressed == MenuItem::GameOptions))
        tile.options = State::Pressed;
      else if (on_tile && state.hover == MenuItem::GameOptions)
        tile.options = State::Hover;
      tile.title_scroll = on_tile ? state.games.title_scroll : 0.0f;
      const GameIcon* const cover = games && games->get_cover ? games->get_cover(*game) : nullptr;
      const float overflow = DrawGameTile(*kit, *game, cover, tile, w.rect, w.clip);
      if (games && overflow > 0.0f)
        games->cut_titles.emplace_back(w.index, overflow);
      break;
    }
    case Kind::GameOptions:
      break;
    case Kind::GamesChip:
      DrawGamesChip(*kit, w.rect, w.label, w.active, look);
      break;
    case Kind::GamesCount:
      kit->TextMid(w.rect.x0, w.rect.Cy(), Type::Tooltip, Tok::kOnSurfaceVariant, w.label);
      break;
    case Kind::RoundButton:
      kit->Fill(w.rect, kGamesRoundR, Tok::kSurfaceContainerHigh);
      kit->StateLayer(w.rect, kGamesRoundR, Tok::kOnSurfaceVariant,
                      w.active ? State::Pressed : look);
      if (w.item == MenuItem::GamesSearch)
        DrawSearchGlyph(*kit, w.rect.Cx(), w.rect.Cy(), Tok::kOnSurfaceVariant);
      else if (w.item == MenuItem::GamesAddFolder)
        DrawAddFolderGlyph(*kit, w.rect.Cx(), w.rect.Cy(), Tok::kOnSurfaceVariant);
      else
        DrawSortGlyph(*kit, w.rect.Cx(), w.rect.Cy(), Tok::kOnSurfaceVariant);
      break;
    case Kind::FilePath:
    {
      const float value_w = kit->TextWidth(Type::Tooltip, w.value) + 16.0f;
      // Its end, when it is longer than the line.
      std::string text = w.label;
      const float room = w.rect.W() - value_w;
      bool cut = false;
      while (!text.empty() && kit->TextWidth(Type::Tooltip, (cut ? "\u2026" : "") + text) > room)
      {
        std::size_t end = 0;
        frame_xr::ui::NextUtf8(text, end);
        text.erase(0, end);
        cut = true;
      }
      kit->TextMid(w.rect.x0, w.rect.Cy(), Type::Tooltip, Tok::kOnSurfaceVariant,
                   (cut ? "\u2026" : "") + text);
      kit->TextMid(w.rect.x1, w.rect.Cy(), Type::Tooltip, Tok::kOnSurfaceVariant, w.value, 2);
      break;
    }
    case Kind::FileRow:
    {
      const float shown = w.disabled ? kDisabledContent : 1.0f;
      kit->StateLayer(w.rect, 12.0f, Tok::kOnSurface, look);
      if (w.icon != 0)
        kit->Icon(LEFT + 12.0f, w.rect.Cy(), 24.0f, w.icon, false, Tok::kOnSurfaceVariant, shown);
      else
        kit->Icon(LEFT + 12.0f, w.rect.Cy(), 24.0f, Icon::kArrowBack, false, Tok::kOnSurfaceVariant,
                  shown);
      const float value_w = w.value.empty() ? 0.0f : kit->TextWidth(Type::Body, w.value) + 16.0f;
      kit->TextMid(LEFT + 40.0f, w.rect.Cy(), Type::Body, Tok::kOnSurface,
                   kit->Fit(Type::Body, w.label, RIGHT - 16.0f - LEFT - 40.0f - value_w), 0, shown);
      kit->TextMid(RIGHT - 16.0f, w.rect.Cy(), Type::Body, Tok::kOnSurfaceVariant, w.value, 2, shown);
      break;
    }
    case Kind::SearchField:
    {
      kit->Fill(w.rect, 24.0f, Tok::kSurfaceContainerHigh);
      DrawSearchGlyph(*kit, w.rect.x0 + 26.0f, w.rect.Cy(), Tok::kOnSurfaceVariant, 20.0f);
      const float text_x = w.rect.x0 + 48.0f;
      // Its end, when it is longer than the field.
      std::string text = w.label;
      const float room = w.rect.W() - 48.0f - 60.0f;
      while (!text.empty() && kit->TextWidth(Type::Body, text) > room)
        text.erase(0, 1);
      if (text.empty())
        kit->TextMid(text_x + 6.0f, w.rect.Cy(), Type::Body, Tok::kOnSurfaceVariant, "Search");
      else
        kit->TextMid(text_x, w.rect.Cy(), Type::Body, Tok::kOnSurface, text);
      const float caret = text_x + kit->TextWidth(Type::Body, text) + 2.0f;
      kit->Line(caret, w.rect.Cy() - 11.0f, caret, w.rect.Cy() + 11.0f, 2.0f, Tok::kPrimary);
      break;
    }
    case Kind::SearchChip:
      kit->Fill(w.rect, kGamesChipRadius, Tok::kSecondaryContainer);
      kit->StateLayer(w.rect, kGamesChipRadius, Tok::kOnSecondaryContainer, look);
      kit->TextMid(w.rect.x0 + 16.0f, w.rect.Cy(), Type::Label, Tok::kOnSecondaryContainer,
                   kit->Fit(Type::Label, w.label, w.rect.W() - kGamesSearchChipRoom));
      break;
    case Kind::NoMatches:
      DrawSearchGlyph(*kit, w.rect.Cx(), w.rect.Cy() - 58.0f, Tok::kOnSurfaceVariant, 48.0f);
      kit->TextMid(w.rect.Cx(), w.rect.Cy() - 4.0f, Type::Title, Tok::kOnSurface, w.label, 1);
      break;
    case Kind::Key:
      kit->Fill(w.rect, 10.0f, w.active ? Tok::kPrimary : Tok::kSurfaceContainerHigh);
      kit->StateLayer(w.rect, 10.0f, w.active ? Tok::kOnPrimary : Tok::kOnSurface, look);
      if (w.icon != 0)
      {
        kit->Icon(w.rect.Cx(), w.rect.Cy(), 22.0f, w.icon, false, Tok::kOnSurface);
      }
      else
      {
        kit->TextMid(w.rect.Cx(), w.rect.Cy(), w.label.size() > 1 ? Type::RailLabel : Type::Value,
                     w.active ? Tok::kOnPrimary : Tok::kOnSurface, w.label, 1);
      }
      break;
    case Kind::Question:
    {
      kit->Fill({0.0f, 0.0f, WIDTH, HEIGHT}, 28.0f, 0x000000, 0.5f);
      kit->Fill(w.rect, 28.0f, Tok::kSurfaceContainerHigh);
      const float room = w.rect.W() - QUESTION_EDGE * 2.0f;
      // Smaller rather than cut.
      const auto line = [&](const std::string& text, Type type, float size, uint32_t color, float y) {
        if (kit->TextWidth(type, text) <= room)
        {
          kit->TextMid(w.rect.Cx(), y, type, color, text, 1);
          return;
        }
        while (size > 9.0f && kit->TextWidthSized(size, false, text) > room)
          size -= 0.5f;
        kit->TextSized(w.rect.Cx(), y + kit->CapHeightSized(size, false) * 0.5f, size, false, color,
                       text, 1);
      };
      float y = w.rect.y0 + QUESTION_EDGE;
      for (std::size_t i = 0; i < w.options.size(); ++i)
      {
        const bool title = static_cast<int>(i) < w.selected;
        if (static_cast<int>(i) == w.selected)
          y += QUESTION_GAP;
        const float h = title ? QUESTION_TITLE_H : QUESTION_TEXT_H;
        line(w.options[i], title ? Type::Headline : Type::Body, title ? 26.0f : 18.0f,
             title ? Tok::kOnSurface : Tok::kOnSurfaceVariant, y + h * 0.5f);
        y += h;
      }
      break;
    }
    case Kind::Scrollbar:
      if (const std::optional<MenuList> list = GetList(state))
      {
        const bool held = look == State::Pressed;
        const bool wide = held || look == State::Hover;
        const float scroll = GetScroll(state, *list);
        if (wide)
        {
          kit->Fill(Scrolling::TrackRect(list->bar, true), Scrolling::kBarHoverW * 0.5f,
                    Tok::kOnSurface, 0.08f);
        }
        const Rect thumb = Scrolling::ThumbRect(list->bar, scroll, wide);
        kit->Fill(thumb, thumb.W() * 0.5f, held ? Tok::kPrimary : Tok::kOnSurfaceVariant,
                  held ? 1.0f : wide ? 0.72f : 0.38f);
      }
      break;
    }
    if (state.focus && pointed == &w)
    {
      const float radius = w.kind == Kind::GameTile    ? 16.0f :
                           w.kind == Kind::RoundButton ? kGamesRoundR :
                           w.kind == Kind::Key         ? 10.0f :
                                                         12.0f;
      kit->Stroke(w.rect, radius, 3.0f, Tok::kPrimary);
    }
  }
  kit->ClearClip();

  if (!state.show_tooltip || !pointed || pointed->tooltip.empty())
    return;
  // Over the row's label; under the row when it is the first one shown (Azahar for Frame's).
  const std::string& text = pointed->tooltip;
  const float wrap_w = PANE.W() * 0.8f;
  const bool wrap = kit->TextWidth(Type::Tooltip, text) + 20.0f > wrap_w;
  const float half = wrap ? wrap_w * 0.5f : kit->TextWidth(Type::Tooltip, text) * 0.5f + 10.0f;
  const bool row = pointed->item == MenuItem::Setting && !pointed->label.empty();
  const float at =
      row ? LEFT + kit->TextWidth(Type::Body, pointed->label) * 0.5f : pointed->rect.Cx();
  const float cx = std::clamp(at, PANE.x0 + half + 4.0f, PANE.x1 - half - 4.0f);
  float top = pointed->rect.y0;
  if (pointed->clipped)
    top = std::max(top, pointed->clip.y0);
  // A controller's chip is under Global and Per-Game.
  const bool above = top - 40.0f > PANE.y0 && pointed->item != MenuItem::Controller &&
                     pointed->item != MenuItem::GamesSort &&
                     pointed->item != MenuItem::GamesSearch &&
                     pointed->item != MenuItem::GamesAddFolder;
  const float y = above ? top + (row ? 6.0f : 0.0f) : pointed->rect.y1;
  if (wrap)
    kit->TooltipWrapped(cx, y, text, above, wrap_w);
  else
    kit->Tooltip(cx, y, text, above);
}
}  // namespace VR::UI
