// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/FrameUI/VrMenu.h"

#include <algorithm>
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

  struct TileItem
  {
    MenuItem item;
    char32_t icon;
    const char* label;
    bool active = false;
    bool disabled = false;
  };
  std::vector<TileItem> tiles{
      {MenuItem::Resume, Icon::kPlayArrow, "Resume"},
      {MenuItem::QuickSave, Icon::kSave, "Quick Save"},
      {MenuItem::QuickLoad, Icon::kHistory, "Quick Load", false, !state.has_quick_save},
      {MenuItem::ResetScreen, Icon::kRecenter, "Reset Screen", false, state.immersive},
      {MenuItem::FastForward, Icon::kSpeed, "Fast Forward", state.fast_forward},
  };
  if (state.wii)
    tiles.push_back({MenuItem::Home, Icon::kHome, "Wii Home Button"});

  constexpr int COLUMNS = 3;
  constexpr float GAP = 12.0f;
  constexpr float TILE_W = (RIGHT - LEFT - GAP * (COLUMNS - 1)) / COLUMNS;
  constexpr float TILE_H = 128.0f;
  const float top = builder->GetY();
  for (std::size_t i = 0; i < tiles.size(); ++i)
  {
    const float x = LEFT + static_cast<float>(i % COLUMNS) * (TILE_W + GAP);
    const float y = top + static_cast<float>(i / COLUMNS) * (TILE_H + GAP);
    MenuWidget& w = builder->Add(Kind::Tile, tiles[i].item);
    w.rect = {x, y, x + TILE_W, y + TILE_H};
    w.icon = tiles[i].icon;
    w.label = tiles[i].label;
    w.active = tiles[i].active;
    w.disabled = tiles[i].disabled;
  }

  MenuWidget& stop = builder->Add(Kind::Button, MenuItem::Stop);
  stop.rect = {LEFT, PANE.y1 - 24.0f - 56.0f, RIGHT, PANE.y1 - 24.0f};
  stop.icon = Icon::kClose;
  stop.label = "Stop Game";
  stop.danger = true;
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
  if (state.wii)
  {
    builder->Text("Pointer", "Right Controller");
    builder->Text("A, B", "A, Right Trigger");
    builder->Text("1, 2", "X, Y");
    builder->Text("+, -", "Menu, View");
    builder->Text("D-Pad", "Right Stick or D-Pad");
    builder->Text("Nunchuk", "Left Stick");
    builder->Text("C, Z", "Left Bumper, Left Trigger");
    builder->Text("Shake", "Right Bumper");
  }
  else
  {
    builder->Text("A, B, X, Y", "A, B, X, Y");
    builder->Text("Control Stick, C Stick", "Left Stick, Right Stick");
    builder->Text("L, R", "Left Trigger, Right Trigger");
    builder->Text("Z", "Right Bumper");
    builder->Text("Start", "Menu");
    builder->Text("D-Pad", "D-Pad");
  }
  builder->Gap(12.0f);
  builder->Text("VR Menu", "Hold Menu");
  builder->Text("Stop Game", "Hold View");
  builder->Text("Quick Save, Quick Load", "View + Right Bumper, + Left Bumper (hold)");
  builder->Text("Fast Forward", "View + Right Trigger");
  builder->Text("Move Screen", "Grip. Stick: Distance, Size");
  builder->Text("Reset Screen", "Hold Right Stick Click");
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
}
}  // namespace

std::vector<MenuWidget> VrMenu::Layout(const MenuState& state)
{
  Builder builder;
  AddRail(&builder, state);
  switch (state.tab)
  {
  case MenuTab::Main:
    AddMain(&builder, state);
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
  for (const MenuWidget& widget : widgets)
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

void VrMenu::Draw(Kit* kit, Canvas* canvas, const MenuState& state)
{
  std::fill(canvas->px.begin(), canvas->px.end(), 0u);
  kit->Begin(*canvas);
  kit->Fill({0.0f, 0.0f, WIDTH, HEIGHT}, 28.0f, Tok::kSurfaceContainer);
  kit->Fill(PANE, 24.0f, Tok::kSurface);

  for (const MenuWidget& w : Layout(state))
  {
    State look = State::Rest;
    if (w.disabled)
      look = State::Disabled;
    else if (w.item != MenuItem::None && w.item == state.pressed)
      look = State::Pressed;
    else if (w.item != MenuItem::None && w.item == state.hover)
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
      kit->Tile(w.rect, w.item == MenuItem::Resume || w.active ? ButtonStyle::Filled :
                                                                 ButtonStyle::Tonal,
                w.icon, w.label, look);
      break;
    case Kind::Button:
      kit->Button(w.rect, w.danger ? ButtonStyle::Danger : ButtonStyle::Tonal, w.icon, w.label,
                  look);
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
    }
  }
}
}  // namespace VR::UI
