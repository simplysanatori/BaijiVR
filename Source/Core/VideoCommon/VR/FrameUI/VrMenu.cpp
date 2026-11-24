// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/FrameUI/VrMenu.h"

#include <algorithm>
#include <ranges>
#include <utility>

#include <fmt/format.h>

#include "VideoCommon/VR/FrameUI/ScreenPlacement.h"
#include "VideoCommon/VR/FrameUI/md3_fonts.h"

namespace VR::UI
{
namespace
{
using namespace FrameXr::Menu;
using Kind = MenuWidget::Kind;

constexpr float RAIL_W = 104.0f;
constexpr float RAIL_ITEM_H = 72.0f;
constexpr float MARGIN = 16.0f;
constexpr float ROW_H = 64.0f;
constexpr float LABEL_W = 220.0f;
constexpr float SLIDER_VALUE_W = 72.0f;
constexpr float SLIDER_VALUE_GAP = 16.0f;
constexpr Rect PANE{RAIL_W, MARGIN, VrMenu::WIDTH - MARGIN, VrMenu::HEIGHT - MARGIN};
constexpr float LEFT = RAIL_W + 24.0f;
constexpr float RIGHT = VrMenu::WIDTH - MARGIN - 24.0f;

float Fraction(float value, float lowest, float highest)
{
  return std::clamp((value - lowest) / (highest - lowest), 0.0f, 1.0f);
}

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

  MenuWidget& Segmented(MenuItem item, std::string label, std::vector<std::string> options,
                        int selected)
  {
    MenuWidget& w = Row(Kind::Segmented, item, std::move(label));
    w.control = {LEFT + LABEL_W, w.rect.Cy() - 24.0f, RIGHT, w.rect.Cy() + 24.0f};
    w.options = std::move(options);
    w.selected = selected;
    return w;
  }

  MenuWidget& Slider(MenuItem item, std::string label, float t, std::string value)
  {
    MenuWidget& w = Row(Kind::Slider, item, std::move(label));
    w.control = {LEFT + LABEL_W, w.rect.Cy() - 24.0f, RIGHT, w.rect.Cy() + 24.0f};
    w.t = t;
    w.value = std::move(value);
    return w;
  }

  MenuWidget& Switch(MenuItem item, std::string label, bool on)
  {
    MenuWidget& w = Row(Kind::Switch, item, std::move(label));
    w.control = {RIGHT - 60.0f, w.rect.Cy() - 24.0f, RIGHT, w.rect.Cy() + 24.0f};
    w.active = on;
    return w;
  }

  MenuWidget& Add(Kind kind, MenuItem item)
  {
    MenuWidget& w = m_widgets.emplace_back();
    w.kind = kind;
    w.item = item;
    return w;
  }

  float GetY() const { return m_y; }

private:
  static constexpr float TEXT_H = 34.0f;

  MenuWidget& Row(Kind kind, MenuItem item, std::string label)
  {
    MenuWidget& w = Add(kind, item);
    w.rect = {LEFT - 12.0f, m_y, RIGHT + 12.0f, m_y + ROW_H - 4.0f};
    w.label = std::move(label);
    m_y += ROW_H;
    return w;
  }

  std::vector<MenuWidget> m_widgets;
  float m_y = PANE.y0 + 16.0f;
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
      {MenuTab::Screen, MenuItem::TabScreen, Icon::kTv, "Screen"},
      {MenuTab::Controls, MenuItem::TabControls, Icon::kStadiaController, "Controls"},
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

void AddMain(Builder* builder, const MenuState& state)
{
  builder->Headline(state.title);

  MenuWidget& resume = builder->Add(Kind::Button, MenuItem::Resume);
  resume.rect = {LEFT, builder->GetY() - 8.0f, RIGHT, builder->GetY() + 56.0f};
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
      {MenuItem::ResetScreen, Icon::kRecenter, state.immersive ? "Reset View" : "Reset Screen"},
      {MenuItem::FastForward, Icon::kSpeed, "Fast Forward", state.fast_forward},
      state.wii ? TileItem{MenuItem::Home, Icon::kHome, "Wii Home"} :
                  TileItem{MenuItem::Restart, Icon::kRestartAlt, "Restart"},
  }};

  constexpr int COLUMNS = 4;
  constexpr float GAP = 12.0f;
  constexpr float TILE_W = (RIGHT - LEFT - GAP * (COLUMNS - 1)) / COLUMNS;
  constexpr float STOP_TOP = PANE.y1 - 24.0f - 56.0f;
  const float top = resume.rect.y1 + 20.0f;
  const float tile_h = (STOP_TOP - GAP - top - GAP) * 0.5f;
  for (std::size_t i = 0; i < tiles.size(); ++i)
  {
    const float x = LEFT + static_cast<float>(i % COLUMNS) * (TILE_W + GAP);
    const float y = top + static_cast<float>(i / COLUMNS) * (tile_h + GAP);
    MenuWidget& w = builder->Add(Kind::Tile, tiles[i].item);
    w.rect = {x, y, x + TILE_W, y + tile_h};
    w.icon = tiles[i].icon;
    w.label = tiles[i].label;
    w.active = tiles[i].active;
    w.disabled = tiles[i].disabled;
  }

  MenuWidget& stop = builder->Add(Kind::Button, MenuItem::Stop);
  stop.rect = {LEFT, STOP_TOP, RIGHT, PANE.y1 - 24.0f};
  stop.icon = Icon::kClose;
  stop.label = "Close Game";
  stop.danger = true;
}

// Save State and Load State: the slots in two columns, over them in Load State the autosaves' card
// and under them Undo Load (Azahar for Frame's page).
void AddSlots(Builder* builder, const MenuState& state)
{
  const bool saving = state.page == MenuPage::Save;
  constexpr float GAP = 12.0f;
  constexpr float TOP = PANE.y0 + 16.0f;

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
    card.active = state.autosave;
    card.disabled = state.saves.autosaves.empty();
    top = card_rect.y1 + GAP;
    if (card.disabled && !state.autosave)
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

void AddScreen(Builder* builder, const MenuState& state)
{
  MenuWidget& mode = builder->Segmented(MenuItem::Mode, "Display", {"3D Screen", "Immersive"},
                                        state.immersive ? 1 : 0);
  mode.disabled = !state.can_switch_mode;

  builder->Slider(MenuItem::Distance, "Distance",
                  Fraction(state.distance, ScreenPlacement::MIN_DISTANCE,
                           ScreenPlacement::MAX_DISTANCE),
                  fmt::format("{:.1f} m", state.distance));
  builder->Slider(MenuItem::Size, "Size",
                  Fraction(state.size, ScreenPlacement::MIN_HEIGHT, ScreenPlacement::MAX_HEIGHT),
                  fmt::format("{:.1f} m", state.size));
  if (state.immersive)
  {
    builder->Text("Distance and Size are for the game's 2D parts.", "");
    return;
  }
  builder->Slider(MenuItem::Depth, "3D Depth", Fraction(state.depth, 0.0f, 100.0f),
                  state.depth < 0.5f ? "Off" : fmt::format("{:.0f}", state.depth));
  builder->Slider(MenuItem::Convergence, "Convergence", Fraction(state.convergence, 0.0f, 200.0f),
                  fmt::format("{:.0f}", state.convergence));
  builder->Segmented(MenuItem::Aspect, "Aspect Ratio", {"Auto", "16:9", "4:3"}, state.aspect);
  builder->Switch(MenuItem::LockScreen, "Lock Screen", state.lock_screen);

  MenuWidget& reset = builder->Add(Kind::Button, MenuItem::ResetScreen);
  reset.rect = {LEFT, builder->GetY() + 2.0f, LEFT + 200.0f, builder->GetY() + 50.0f};
  reset.icon = Icon::kRecenter;
  reset.label = "Reset Screen";
}

void AddControls(Builder* builder, const MenuState& state)
{
  MenuWidget& page = builder->Add(Kind::Segmented, MenuItem::ControlsPage);
  page.rect = {LEFT + 136.0f, PANE.y0 + 16.0f, RIGHT - 136.0f, PANE.y0 + 64.0f};
  page.control = page.rect;
  page.options = {"Game", "Shortcuts"};
  page.selected = state.controls_page;
  builder->Gap(64.0f);

  if (state.controls_page == 0)
  {
    MenuWidget& pad = builder->Add(Kind::Pad, MenuItem::None);
    pad.rect = {LEFT, builder->GetY(), RIGHT, PANE.y1 - 8.0f};
    return;
  }
  builder->Text("VR Menu", "Hold Menu for 1 Second");
  builder->Text("Close Game", "Hold View for 1.5 Seconds");
  builder->Text("Quick Save", "View, then Right Bumper");
  builder->Text("Quick Load", "View, then Hold Left Bumper");
  builder->Text("Fast Forward", "View, then Right Trigger");
  builder->Text("Reset Screen or View", "Hold Right Stick Click");
  builder->Text("3D Screen or Immersive", "Both Bumpers, then View and Menu");
  builder->Gap(12.0f);
  builder->Text("Move Screen", "Grip");
  builder->Text("Screen Distance and Size", "Grip and Stick");
  builder->Text("Nudge Screen", "Grip and D-Pad Left or Right");
}

void AddSettings(Builder* builder, const MenuState& state)
{
  const int resolution = state.resolution >= 1 && state.resolution <= 4 ? state.resolution - 1 : -1;
  builder->Segmented(MenuItem::Resolution, "Internal Resolution", {"1x", "2x", "3x", "4x"},
                     resolution);
  builder->Segmented(MenuItem::Pacing, "Frame Pacing", {"Every Refresh", "Match Game"},
                     state.eager ? 0 : 1);
  builder->Switch(MenuItem::Beams, "Pointer Beams", state.beams);
  builder->Switch(MenuItem::Toasts, "Messages", state.toasts);
  builder->Switch(MenuItem::Autosave, "Autosave", state.autosave);
  std::vector<std::string> minutes;
  for (const int m : AUTOSAVE_MINUTES)
    minutes.push_back(fmt::format("{} min", m));
  MenuWidget& every = builder->Segmented(MenuItem::AutosaveEvery, "Autosave Every",
                                         std::move(minutes), state.autosave_every);
  every.disabled = !state.autosave;
  MenuWidget& keep = builder->Slider(MenuItem::AutosaveKeep, "Autosaves to Keep",
                                     Fraction(static_cast<float>(state.autosave_keep), 1.0f, 10.0f),
                                     fmt::format("{}", state.autosave_keep));
  keep.disabled = !state.autosave;
}
}  // namespace

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
  case MenuTab::Screen:
    AddScreen(&builder, state);
    break;
  case MenuTab::Controls:
    AddControls(&builder, state);
    break;
  case MenuTab::Settings:
    AddSettings(&builder, state);
    break;
  }
  return builder.Take();
}

const MenuWidget* VrMenu::At(const std::vector<MenuWidget>& widgets, float x, float y)
{
  // What is drawn last is on top.
  for (const MenuWidget& widget : std::views::reverse(widgets))
  {
    if (widget.item != MenuItem::None && !widget.disabled && widget.rect.Contains(x, y))
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

void VrMenu::Draw(Kit* kit, Canvas* canvas, const MenuState& state, const GetPicture& get_picture)
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

  for (const MenuWidget& w : Layout(state))
  {
    State look = State::Rest;
    if (w.disabled)
      look = State::Disabled;
    else if (w.item != MenuItem::None && w.index == state.hover_index && w.item == state.pressed)
      look = State::Pressed;
    else if (w.item != MenuItem::None && w.index == state.hover_index && w.item == state.hover)
      look = State::Hover;

    const float opacity = w.disabled ? kDisabledContent : 1.0f;
    const bool row =
        w.kind == Kind::Segmented || w.kind == Kind::Switch || w.kind == Kind::Slider;
    if (row)
      kit->TextMid(LEFT, w.rect.Cy(), Type::Body, Tok::kOnSurface, w.label, 0, opacity);

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
      break;
    case Kind::Tile:
      kit->Tile(w.rect, w.active ? ButtonStyle::Filled : ButtonStyle::Tonal, w.icon, w.label, look);
      break;
    case Kind::Button:
      kit->Button(w.rect,
                  w.danger ? ButtonStyle::Danger :
                  w.active ? ButtonStyle::Filled :
                  w.item == MenuItem::AutosaveSettings ? ButtonStyle::Text :
                                                         ButtonStyle::Tonal,
                  w.icon, w.label, look);
      break;
    case Kind::Segmented:
    {
      const bool hovered = w.item == state.hover && !w.disabled;
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
    case Kind::Pad:
      DrawPad(kit, canvas, w.rect, state.pad, state.pad_labels);
      break;
    }
  }
}
}  // namespace VR::UI
