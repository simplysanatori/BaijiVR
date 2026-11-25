// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Core/Autosave.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/ConfigLoaders/BaseConfigLoader.h"
#include "Core/ConfigManager.h"
#include "Core/GameSettingScope.h"
#include "InputCommon/ControllerInterface/OpenXR/FrameControllerNames.h"
#include "VideoCommon/VR/FrameUI/MenuSettings.h"
#include "VideoCommon/VR/FrameUI/PadBindings.h"
#include "VideoCommon/VR/FrameUI/PadFilter.h"
#include "VideoCommon/VR/FrameUI/ScreenPlacement.h"
#include "VideoCommon/VR/FrameUI/VrMenu.h"
#include "UICommon/UICommon.h"

using namespace VR::UI;

namespace
{
constexpr s64 MS = 1'000'000;

// Holds `set` for `ms` at 90 Hz and counts what it did.
struct Pad
{
  PadFilter filter;
  Controllers raw{};
  Controllers seen{};
  s64 now = 1'000 * MS;
  bool blocked = false;
  int grab = -1;
  int menus = 0, stops = 0, saves = 0, loads = 0, resets = 0, fasts = 0, switches = 0;
  bool game_menu = false, game_view = false, game_bumper = false, game_trigger = false;
  bool stopping = false;

  void Run(int ms)
  {
    for (const s64 end = now + ms * MS; now < end; now += 11 * MS)
    {
      seen = raw;
      const PadEvents e = filter.Update(&seen, now, blocked, grab);
      menus += e.open_menu;
      stops += e.stop;
      saves += e.quick_save;
      loads += e.quick_load;
      resets += e.reset_screen;
      fasts += e.fast_forward;
      switches += e.switch_mode;
      stopping |= e.stopping;
      game_menu |= seen[1].menu_button;
      game_view |= seen[0].view_button;
      game_bumper |= seen[0].bumper_button || seen[1].bumper_button;
      game_trigger |= seen[1].trigger_value > 0.0f;
    }
  }
  void ClearSeen() { game_menu = game_view = game_bumper = game_trigger = stopping = false; }
};

// Every page of the menu, the lists at their top, middle and end.
std::vector<MenuState> GetPages(bool immersive)
{
  std::vector<MenuState> pages;
  MenuState base;
  base.settings.immersive = immersive;
  base.settings.has_game = true;
  for (const MenuTab tab : {MenuTab::Main, MenuTab::Controls, MenuTab::Mods})
  {
    for (const int page : {0, 1})
    {
      MenuState s = base;
      s.tab = tab;
      s.controls_page = page;
      s.wii = page == 1;
      s.mods.resize(13);
      s.mods_page = page * 2;
      s.mods_changed = true;
      pages.push_back(s);
    }
  }
  for (const PadKind kind : {PadKind::GameCube, PadKind::WiiRemote, PadKind::WiiRemoteSideways,
                             PadKind::WiiNunchuk})
  {
    for (const PadTarget listening : {PadTarget::None, PadTarget::A})
    {
      MenuState s = base;
      s.tab = MenuTab::Controls;
      s.page = MenuPage::EditControls;
      s.pad = kind;
      s.edit.listening = listening;
      s.edit.countdown = 3;
      pages.push_back(s);
    }
  }
  for (std::size_t i = 0; i < NUM_SECTIONS; ++i)
  {
    MenuState s = base;
    s.section = static_cast<MenuSection>(i);
    s.tab = s.section == MenuSection::Screen ? MenuTab::Screen : MenuTab::Settings;
    const float most = VrMenu::GetList(s)->bar.max_scroll;
    for (const float scroll : {0.0f, most * 0.5f, most})
    {
      s.scroll[i] = scroll;
      pages.push_back(s);
    }
  }
  return pages;
}

FrameXr::Menu::Rect GetVisible(const MenuWidget& w)
{
  FrameXr::Menu::Rect r = w.rect;
  if (w.clipped)
  {
    r = {std::max(r.x0, w.clip.x0), std::max(r.y0, w.clip.y0), std::min(r.x1, w.clip.x1),
         std::min(r.y1, w.clip.y1)};
  }
  return r;
}

bool Overlaps(const FrameXr::Menu::Rect& a, const FrameXr::Menu::Rect& b)
{
  return std::min(a.x1, b.x1) - std::max(a.x0, b.x0) > 0.01f &&
         std::min(a.y1, b.y1) - std::max(a.y0, b.y0) > 0.01f;
}

const MenuWidget* FindSetting(const std::vector<MenuWidget>& widgets, SettingId id)
{
  const auto it = std::ranges::find_if(widgets, [id](const MenuWidget& w) {
    return w.item == MenuItem::Setting && w.index == static_cast<int>(id);
  });
  return it != widgets.end() ? &*it : nullptr;
}
}  // namespace

TEST(VRFrameUI, MenuTapAndHold)
{
  Pad pad;
  pad.raw[1].menu_button = true;
  pad.Run(150);
  EXPECT_FALSE(pad.game_menu);
  pad.raw[1].menu_button = false;
  pad.Run(50);
  EXPECT_TRUE(pad.game_menu);
  pad.Run(300);
  pad.ClearSeen();
  pad.Run(100);
  EXPECT_FALSE(pad.game_menu);
  EXPECT_EQ(pad.menus, 0);

  pad.raw[1].menu_button = true;
  pad.Run(1500);
  EXPECT_EQ(pad.menus, 1);
  pad.raw[1].menu_button = false;
  pad.ClearSeen();
  pad.Run(300);
  EXPECT_FALSE(pad.game_menu);
}

TEST(VRFrameUI, ViewTapAndStop)
{
  Pad pad;
  pad.raw[0].view_button = true;
  pad.Run(200);
  EXPECT_FALSE(pad.game_view);
  pad.raw[0].view_button = false;
  pad.Run(50);
  EXPECT_TRUE(pad.game_view);
  pad.Run(300);

  pad.ClearSeen();
  pad.raw[0].view_button = true;
  pad.Run(1000);
  EXPECT_TRUE(pad.stopping);
  EXPECT_EQ(pad.stops, 0);
  pad.Run(700);
  EXPECT_EQ(pad.stops, 1);
  EXPECT_FALSE(pad.game_view);
}

TEST(VRFrameUI, ViewCombinations)
{
  Pad pad;
  pad.raw[0].view_button = true;
  pad.Run(100);
  pad.raw[1].bumper_button = true;
  pad.Run(200);
  EXPECT_EQ(pad.saves, 0);
  pad.raw[1].bumper_button = false;
  pad.Run(100);
  EXPECT_EQ(pad.saves, 1);

  // A brush of the left bumper loads nothing.
  pad.raw[0].bumper_button = true;
  pad.Run(200);
  pad.raw[0].bumper_button = false;
  pad.Run(100);
  EXPECT_EQ(pad.loads, 0);
  pad.raw[0].bumper_button = true;
  pad.Run(1200);
  EXPECT_EQ(pad.loads, 1);
  pad.raw[0].bumper_button = false;

  pad.raw[1].trigger_value = 1.0f;
  pad.Run(200);
  EXPECT_EQ(pad.fasts, 0);
  pad.raw[1].trigger_value = 0.0f;
  pad.Run(100);
  EXPECT_EQ(pad.fasts, 1);

  pad.raw[0].view_button = false;
  pad.Run(500);
  EXPECT_EQ(pad.stops, 0);
  EXPECT_FALSE(pad.game_view);
  EXPECT_FALSE(pad.game_bumper);
  EXPECT_FALSE(pad.game_trigger);
}

TEST(VRFrameUI, WhatWasDownBeforeViewIsTheGames)
{
  Pad pad;
  pad.raw[1].trigger_value = 1.0f;
  pad.raw[1].bumper_button = true;
  pad.Run(200);
  pad.raw[0].view_button = true;
  pad.Run(300);
  pad.raw[1].trigger_value = 0.0f;
  pad.raw[1].bumper_button = false;
  pad.Run(100);
  EXPECT_EQ(pad.saves, 0);
  EXPECT_EQ(pad.fasts, 0);
  EXPECT_TRUE(pad.game_bumper);
  EXPECT_TRUE(pad.game_trigger);
}

TEST(VRFrameUI, SwitchMode)
{
  Pad pad;
  pad.raw[0].bumper_button = pad.raw[1].bumper_button = true;
  pad.Run(100);
  pad.raw[0].view_button = true;
  pad.Run(50);
  pad.raw[1].menu_button = true;
  pad.Run(3000);
  EXPECT_EQ(pad.switches, 1);
  EXPECT_EQ(pad.stops, 0);
  EXPECT_EQ(pad.menus, 0);
  EXPECT_EQ(pad.loads, 0);
  EXPECT_EQ(pad.saves, 0);
}

TEST(VRFrameUI, MenuGivesBackOnlyWhatIsLetGo)
{
  Pad pad;
  pad.blocked = true;
  pad.raw[1].primary_button = true;
  pad.Run(100);
  EXPECT_FALSE(pad.seen[1].primary_button);
  pad.blocked = false;
  pad.Run(100);
  EXPECT_FALSE(pad.seen[1].primary_button);
  pad.raw[1].primary_button = false;
  pad.Run(50);
  pad.raw[1].primary_button = true;
  pad.Run(50);
  EXPECT_TRUE(pad.seen[1].primary_button);
}

TEST(VRFrameUI, ViewStopsFromTheMenu)
{
  Pad pad;
  pad.blocked = true;
  pad.raw[0].view_button = true;
  pad.Run(1000);
  EXPECT_TRUE(pad.stopping);
  EXPECT_EQ(pad.stops, 0);
  pad.Run(1500);
  EXPECT_EQ(pad.stops, 1);
  EXPECT_FALSE(pad.game_view);

  // Let go half way, or closed the menu half way: nothing.
  Pad other;
  other.blocked = true;
  other.raw[0].view_button = true;
  other.Run(1000);
  other.raw[0].view_button = false;
  other.Run(100);
  other.raw[0].view_button = true;
  other.Run(1000);
  other.blocked = false;
  other.Run(2000);
  EXPECT_EQ(other.stops, 0);
  EXPECT_FALSE(other.game_view);
}

TEST(VRFrameUI, GrabKeepsTheStickFromTheGame)
{
  Pad pad;
  pad.grab = 1;
  pad.raw[1].squeeze_value = 1.0f;
  pad.raw[1].thumbstick_y = 1.0f;
  pad.raw[0].thumbstick_y = 1.0f;
  pad.Run(100);
  EXPECT_EQ(pad.seen[1].thumbstick_y, 0.0f);
  EXPECT_EQ(pad.seen[1].squeeze_value, 0.0f);
  EXPECT_EQ(pad.seen[0].thumbstick_y, 1.0f);
  pad.grab = -1;
  pad.raw[1].squeeze_value = 0.0f;
  pad.Run(50);
  EXPECT_EQ(pad.seen[1].thumbstick_y, 0.0f);
  pad.raw[1].thumbstick_y = 0.0f;
  pad.Run(50);
  pad.raw[1].thumbstick_y = 1.0f;
  pad.Run(50);
  EXPECT_EQ(pad.seen[1].thumbstick_y, 1.0f);
}

TEST(VRFrameUI, ResetIsAHold)
{
  Pad pad;
  pad.raw[1].thumbstick_button = true;
  pad.Run(500);
  EXPECT_TRUE(pad.seen[1].thumbstick_button);
  pad.Run(800);
  EXPECT_EQ(pad.resets, 1);
  pad.raw[1].thumbstick_button = false;
  pad.Run(50);
  // Pushed in while turning: not a reset.
  pad.raw[1].thumbstick_x = 1.0f;
  pad.raw[1].thumbstick_button = true;
  pad.Run(1500);
  EXPECT_EQ(pad.resets, 1);
}

TEST(VRFrameUI, GrabCarriesTheScreenUnderTheBeam)
{
  const ScreenAnchor anchor{{0.1f, 1.6f, 0.2f}, 0.3f};
  ScreenPlacement placement;
  FrameXr::Input::Ray ray{{0.3f, 1.2f, 0.0f}, {0.0f, 0.0f, -1.0f}};
  ScreenGrab grab;
  grab.Begin(anchor, placement, ray);
  const ScreenPlacement same = grab.Update(anchor, placement, ray);
  EXPECT_NEAR(same.yaw_deg, 0.0f, 1e-3f);
  EXPECT_NEAR(same.pitch_deg, 0.0f, 1e-3f);

  const float angle = 0.2f;
  ray.direction = {-std::sin(angle), 0.0f, -std::cos(angle)};
  const ScreenPlacement moved = grab.Update(anchor, placement, ray);
  EXPECT_GT(moved.yaw_deg, 5.0f);
  EXPECT_EQ(moved.distance, placement.distance);

  const ScreenPlacement nudged = grab.Nudge(moved, 0.5f);
  EXPECT_NEAR(nudged.yaw_deg, moved.yaw_deg + 0.5f, 1e-3f);
  EXPECT_NEAR(grab.Update(anchor, nudged, ray).yaw_deg, nudged.yaw_deg, 1e-2f);
}

TEST(VRFrameUI, NaturalShiftIsTheEyesApartAtInfinity)
{
  // 1 in NDC is half the screen. Right minus left eye, in metres on the screen.
  constexpr float WIDTH = 2.67f, EYES = 0.063f, DISTANCE = 2.0f, CONVERGENCE = 20.0f;
  const auto apart = [&](float w) {
    return 2.0f * NaturalShift(EYES, WIDTH) * (1.0f - CONVERGENCE / w) * WIDTH * 0.5f;
  };
  EXPECT_NEAR(apart(1e9f), EYES, 1e-5f);
  EXPECT_NEAR(apart(CONVERGENCE), 0.0f, 1e-6f);
  for (const float w : {10.0f, 40.0f, 200.0f})
    EXPECT_NEAR(DISTANCE / (1.0f - apart(w) / EYES), DISTANCE * w / CONVERGENCE, 1e-2f);
}

TEST(VRFrameUI, MenuFindsWhatIsOnTop)
{
  MenuState state;
  state.page = MenuPage::Load;
  state.settings.Set(SettingId::Autosave, 1.0f);
  state.autosave_list = true;
  state.saves.autosaves.resize(3);
  state.saves.slots.fill("2026-11-24 10:00");
  const auto widgets = VrMenu::Layout(state);
  const MenuWidget* const entry = VrMenu::At(widgets, 590.0f, 208.0f);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->item, MenuItem::AutosaveEntry);
  EXPECT_EQ(entry->index, 0);
  const MenuWidget* const slot = VrMenu::At(widgets, 200.0f, 208.0f);
  ASSERT_NE(slot, nullptr);
  EXPECT_EQ(slot->item, MenuItem::Slot);
  EXPECT_EQ(slot->index, 1);
}

TEST(VRFrameUI, MainPage)
{
  MenuState s;
  s.title = "Game";
  for (const bool wii : {false, true})
  {
    s.wii = wii;
    const auto widgets = VrMenu::Layout(s);
    std::vector<MenuItem> tiles;
    for (const MenuWidget& w : widgets)
    {
      if (w.kind == MenuWidget::Kind::Tile)
        tiles.push_back(w.item);
    }
    EXPECT_EQ(tiles, (std::vector{MenuItem::QuickSave, MenuItem::QuickLoad, MenuItem::SaveState,
                                  MenuItem::LoadState, MenuItem::Screenshot, MenuItem::Restart,
                                  wii ? MenuItem::Home : MenuItem::FastForward, MenuItem::Stop}));
    const auto flat = std::ranges::find(widgets, MenuItem::SwitchToFlat, &MenuWidget::item);
    const auto resume = std::ranges::find(widgets, MenuItem::Resume, &MenuWidget::item);
    ASSERT_TRUE(flat != widgets.end() && resume != widgets.end());
    EXPECT_EQ(flat->rect.W(), resume->rect.W());
  }
}

TEST(VRFrameUI, MenuIsOnThePanelWithoutOverlaps)
{
  for (const bool immersive : {false, true})
  {
    for (const MenuState& state : GetPages(immersive))
    {
      const auto widgets = VrMenu::Layout(state);
      for (const MenuWidget& w : widgets)
      {
        const auto r = GetVisible(w);
        EXPECT_GE(r.x0, 0.0f);
        EXPECT_GE(r.y0, 0.0f);
        EXPECT_LE(r.x1, VrMenu::WIDTH);
        EXPECT_LE(r.y1, VrMenu::HEIGHT - 16.0f);
      }
      for (std::size_t i = 0; i < widgets.size(); ++i)
      {
        for (std::size_t k = i + 1; k < widgets.size(); ++k)
        {
          const MenuWidget& a = widgets[i];
          const MenuWidget& b = widgets[k];
          // A mod's controls are on its card.
          if (a.kind == MenuWidget::Kind::Mod || b.kind == MenuWidget::Kind::Mod)
            continue;
          EXPECT_FALSE(Overlaps(GetVisible(a), GetVisible(b)))
              << static_cast<int>(state.tab) << ": " << a.label << " and " << b.label;
        }
      }
    }
  }
}

TEST(VRFrameUI, MenuTextFits)
{
  // What is hit is laid out without the fonts: it has to agree with what is drawn.
  FrameXr::Menu::Kit kit(1.5f);
  for (const MenuState& state : GetPages(false))
  {
    for (const MenuWidget& w : VrMenu::Layout(state))
    {
      using Kind = MenuWidget::Kind;
      if (w.kind == Kind::Segmented)
      {
        EXPECT_LE(kit.SegmentedWidth(w.options), w.control.W() + 0.5f) << w.options.front();
        const auto edges = kit.SegmentEdges(w.control, w.options);
        for (std::size_t i = 0; i < w.options.size(); ++i)
        {
          EXPECT_EQ(VrMenu::OptionAt(w, (edges[i] + edges[i + 1]) * 0.5f), static_cast<int>(i));
        }
      }
      if (w.item == MenuItem::Setting)
      {
        EXPECT_LE(128.0f + kit.TextWidth(FrameXr::Menu::Type::Body, w.label),
                  w.control.x0 - 8.0f)
            << w.label;
      }
      if (w.kind == Kind::Dropdown)
      {
        EXPECT_LE(kit.TextWidth(FrameXr::Menu::Type::Label, w.value), w.control.W() - 64.0f)
            << w.value;
      }
      if (w.kind == Kind::Button || w.kind == Kind::Tile)
      {
        EXPECT_LE(kit.TextWidth(FrameXr::Menu::Type::Label, w.label), w.rect.W() - 16.0f)
            << w.label;
      }
    }
  }
}

TEST(VRFrameUI, EveryRowCanBeScrolledTo)
{
  for (std::size_t i = 0; i < NUM_SECTIONS; ++i)
  {
    MenuState s;
    s.section = static_cast<MenuSection>(i);
    s.tab = s.section == MenuSection::Screen ? MenuTab::Screen : MenuTab::Settings;
    std::set<int> found;
    for (float scroll = 0.0f;; scroll += 8.0f)
    {
      const MenuList list = *VrMenu::GetList(s);
      s.scroll[i] = std::min(scroll, list.bar.max_scroll);
      for (const MenuWidget& w : VrMenu::Layout(s))
      {
        if (w.item == MenuItem::Setting && w.rect.y0 >= list.bar.list.y0 &&
            w.rect.y1 <= list.bar.list.y1)
        {
          found.insert(w.index);
        }
      }
      if (scroll >= list.bar.max_scroll)
        break;
    }
    for (const SettingRow& row : GetSettingRows())
    {
      if (row.section == s.section && row.id != SettingId::None)
      {
        EXPECT_TRUE(found.contains(static_cast<int>(row.id))) << row.label;
      }
    }
  }
}

TEST(VRFrameUI, ARowPastTheListIsNotHit)
{
  MenuState s;
  s.tab = MenuTab::Settings;
  const MenuList list = *VrMenu::GetList(s);
  ASSERT_TRUE(list.bar.Valid());
  s.scroll[static_cast<std::size_t>(MenuSection::General)] = 40.0f;
  const auto widgets = VrMenu::Layout(s);
  const MenuWidget& row = *std::ranges::find(widgets, MenuItem::Setting, &MenuWidget::item);
  EXPECT_LT(row.rect.y0, list.bar.list.y0);
  const MenuWidget* const over = VrMenu::At(widgets, 400.0f, list.bar.list.y0 - 4.0f);
  EXPECT_TRUE(!over || over->item != MenuItem::Setting);
  const MenuWidget* const in = VrMenu::At(widgets, 400.0f, list.bar.list.y0 + 4.0f);
  ASSERT_NE(in, nullptr);
  EXPECT_EQ(in->index, row.index);
}

TEST(VRFrameUI, EveryRowHasItsPlaceAndWords)
{
  std::set<SettingId> seen;
  for (const SettingRow& row : GetSettingRows())
  {
    EXPECT_FALSE(row.label.empty());
    if (row.kind == RowKind::Header)
      continue;
    EXPECT_FALSE(row.tooltip.empty()) << row.label;
    EXPECT_TRUE(seen.insert(row.id).second) << row.label;
    if (row.kind == RowKind::Segmented || row.kind == RowKind::Dropdown)
    {
      EXPECT_GE(row.options.size(), 2u) << row.label;
    }
    if (row.kind == RowKind::Slider && row.options.empty())
    {
      EXPECT_NE(row.format, nullptr) << row.label;
      EXPECT_LT(row.minimum, row.maximum) << row.label;
      for (const float t : {0.0f, 0.3f, 0.5f, 1.0f})
      {
        const float value = SliderValue(row, t);
        EXPECT_NEAR(SliderFraction(row, value), t, row.exponential ? 0.02f : 0.06f) << row.label;
        EXPECT_NEAR(SliderValue(row, SliderFraction(row, value)), value,
                    std::abs(value) * 1e-3f + 1e-4f)
            << row.label;
      }
    }
  }
  EXPECT_EQ(seen.size(), NUM_SETTINGS);
}

TEST(VRFrameUI, AntiAliasingIsWhatTheGPUCanDo)
{
  SettingState s;
  s.values.aa_modes = {1, 2, 4};
  std::vector<std::string> labels;
  for (const SettingOption& option : GetOptions(GetSettingRow(SettingId::AntiAliasing), s))
    labels.push_back(option.label);
  EXPECT_EQ(labels, (std::vector<std::string>{"None", "2x", "4x"}));
}

TEST(VRFrameUI, SliderWithASwitch)
{
  MenuState s;
  s.tab = MenuTab::Settings;
  s.section = MenuSection::Immersive;
  s.settings.immersive = true;
  for (const bool on : {false, true})
  {
    s.settings.Set(SettingId::LeanBackOn, on ? 1.0f : 0.0f);
    const auto widgets = VrMenu::Layout(s);
    const MenuWidget* const slider = FindSetting(widgets, SettingId::LeanBack);
    const MenuWidget* const toggle = FindSetting(widgets, SettingId::LeanBackOn);
    ASSERT_TRUE(slider && toggle);
    EXPECT_EQ(slider->disabled, !on);
    EXPECT_FALSE(toggle->disabled);
    EXPECT_EQ(toggle->active, on);
    EXPECT_EQ(slider->rect.Cy(), toggle->rect.Cy());
    if (!on)
    {
      EXPECT_EQ(slider->tooltip, "Turn on Lean Back to change it.");
    }
  }
}

TEST(VRFrameUI, WorldScaleSlider)
{
  const SettingRow& row = GetSettingRow(SettingId::WorldScale);
  EXPECT_NEAR(SliderValue(row, 0.0f), 0.1f, 1e-4f);
  EXPECT_NEAR(SliderValue(row, 1.0f), 2000.0f, 1.0f);
  EXPECT_NEAR(SliderValue(row, SliderFraction(row, 100.0f)), 100.0f, 0.1f);
  EXPECT_EQ(FormatValue(row, 120.0f), "120");
  EXPECT_EQ(FormatValue(row, 1.3f), "1.3");
}

TEST(VRFrameUI, SliderWithStops)
{
  const SettingRow& every = GetSettingRow(SettingId::AutosaveEvery);
  EXPECT_EQ(SliderValue(every, 0.0f), 1.0f);
  EXPECT_EQ(SliderValue(every, 0.5f), 5.0f);
  EXPECT_EQ(SliderValue(every, 1.0f), 15.0f);
  EXPECT_EQ(SliderFraction(every, 10.0f), 0.75f);
  EXPECT_EQ(SliderFraction(every, 7.0f), 0.5f);
}

TEST(VRFrameUI, GreyedRowsSayWhy)
{
  MenuState s;
  s.tab = MenuTab::Screen;
  s.settings.immersive = true;
  const auto widgets = VrMenu::Layout(s);
  const MenuWidget* const lock = FindSetting(widgets, SettingId::LockScreen);
  ASSERT_NE(lock, nullptr);
  EXPECT_TRUE(lock->disabled);
  EXPECT_EQ(lock->tooltip, "Only in Stereoscopic 3D.");
  EXPECT_EQ(VrMenu::At(widgets, 400.0f, lock->rect.Cy()), nullptr);
  EXPECT_EQ(VrMenu::At(widgets, 400.0f, lock->rect.Cy(), true), lock);

  s.settings.Set(SettingId::Autosave, 0.0f);
  EXPECT_EQ(GetLock(GetSettingRow(SettingId::AutosaveKeep), s.settings), Lock::AutosaveOff);
  s.settings.Set(SettingId::Autosave, 1.0f);
  EXPECT_EQ(GetLock(GetSettingRow(SettingId::AutosaveKeep), s.settings), Lock::None);
}

TEST(VRFrameUI, DropDownMenu)
{
  MenuState s;
  s.tab = MenuTab::Settings;
  s.section = MenuSection::Graphics;
  s.open_setting = SettingId::InternalResolution;
  s.settings.Set(SettingId::InternalResolution, 2.0f);
  const auto widgets = VrMenu::Layout(s);
  const auto menu = std::ranges::find(widgets, MenuWidget::Kind::Menu, &MenuWidget::kind);
  ASSERT_NE(menu, widgets.end());
  EXPECT_EQ(menu->selected, 2);
  EXPECT_GE(menu->rect.y0, 16.0f);
  EXPECT_LE(menu->rect.y1, VrMenu::HEIGHT - 16.0f);
  const MenuWidget* const option =
      VrMenu::At(widgets, menu->rect.Cx(), menu->rect.y0 + 8.0f + 48.0f * 3.5f);
  ASSERT_NE(option, nullptr);
  EXPECT_EQ(option->item, MenuItem::PopupOption);
  EXPECT_EQ(option->index, 3);
  // Its edge does nothing, and keeps the rows under it from being pressed.
  const MenuWidget* const edge = VrMenu::At(widgets, menu->rect.Cx(), menu->rect.y0 + 2.0f);
  ASSERT_NE(edge, nullptr);
  EXPECT_EQ(edge->item, MenuItem::Popup);

  // Out of the list (6x from the settings page), it still says what it is.
  EXPECT_EQ(FormatValue(GetSettingRow(SettingId::InternalResolution), 6.0f), "6x");
}

TEST(VRFrameUI, ModRows)
{
  MenuState s;
  s.tab = MenuTab::Mods;
  s.mods.resize(4);
  s.mods[0].state = GameMods::State::On;
  s.mods[1].downloads = true;
  s.mods[2].downloads = true;
  s.mods[2].state = GameMods::State::Off;
  s.mods[3].state = GameMods::State::LinkOnly;
  const auto widgets = VrMenu::Layout(s);
  const auto count = [&](MenuItem item, int index) {
    return std::ranges::count_if(
        widgets, [&](const MenuWidget& w) { return w.item == item && w.index == index; });
  };
  EXPECT_EQ(count(MenuItem::ModSwitch, 0), 1);
  EXPECT_EQ(count(MenuItem::ModGet, 1), 1);
  EXPECT_EQ(count(MenuItem::ModSwitch, 2), 1);
  EXPECT_EQ(count(MenuItem::ModRemove, 2), 1);
  EXPECT_EQ(count(MenuItem::ModSwitch, 3), 0);
  for (const MenuWidget& w : widgets)
  {
    if (w.kind == MenuWidget::Kind::Mod)
      EXPECT_GT(w.control.W(), 250.0f);
  }
}

TEST(VRFrameUI, FrameNames)
{
  using namespace ciface::OpenXR;
  EXPECT_EQ(GetFrameInputName("Right Button A"), "A");
  EXPECT_EQ(GetFrameInputName("Left Button View"), "View");

  std::string expression = "`Right Button A` | `Left Thumbstick Y+`";
  UpdateFrameExpression("Buttons/A", &expression);
  EXPECT_EQ(expression, "`A` | `Left Stick Up`");
  std::string own = "`Return`";
  UpdateFrameExpression("Buttons/A", &own);
  EXPECT_EQ(own, "`Return`");

  EXPECT_EQ(DescribeExpression("`A` | `Right Trigger`"), "A or Right Trigger");
  EXPECT_EQ(DescribeUp("`Left Stick Up`"), "Left Stick");
  EXPECT_EQ(DescribeUp("`D-Pad Up`"), "D-Pad");
}

TEST(VRFrameUI, AutosaveRules)
{
  using namespace Autosave;
  Scheduler s;
  s64 now = 5'000;
  EXPECT_FALSE(s.IsAllowed(Reason::Timer, now, false));
  const auto play = [&](s64 ms) {
    for (const s64 end = now + ms; now < end; now += 16)
      s.Field(now);
  };
  play(30'000);
  EXPECT_FALSE(s.IsAllowed(Reason::MenuOpen, now, false));
  play(31'000);
  EXPECT_TRUE(s.IsAllowed(Reason::MenuOpen, now, false));
  EXPECT_TRUE(s.IsDue(60'000));
  EXPECT_FALSE(s.IsDue(300'000));
  s.Fired(now);
  EXPECT_FALSE(s.IsAllowed(Reason::Quit, now, false));
  // A pause is no play.
  now += 600'000;
  s.Field(now);
  EXPECT_FALSE(s.IsDue(60'000));
  EXPECT_TRUE(s.IsAllowed(Reason::Timer, now, false));
  EXPECT_FALSE(s.IsAllowed(Reason::Timer, now, true));
  // Closed while paused, or hung.
  now += 10'000;
  EXPECT_TRUE(s.IsAllowed(Reason::Quit, now, true));
  EXPECT_FALSE(s.IsAllowed(Reason::Quit, now, false));
  s.Stop();
  EXPECT_FALSE(s.IsAllowed(Reason::Quit, now, true));

  EXPECT_EQ(Ago(1000, 1030, 1, 1, "x"), "Just Now");
  EXPECT_EQ(Ago(1000, 1090, 1, 1, "x"), "1 Minute Ago");
  EXPECT_EQ(Ago(1000, 8300, 1, 1, "x"), "2 Hours Ago");
  EXPECT_EQ(Ago(1000, 90000, 1, 2, "x"), "Yesterday");
  EXPECT_EQ(Ago(1000, 900000, 1, 11, "x"), "x");
}

TEST(VRFrameUI, ScopeControls)
{
  MenuState s;
  s.tab = MenuTab::Screen;
  const auto find = [&s](MenuItem item) {
    const auto widgets = VrMenu::Layout(s);
    return *std::ranges::find(widgets, item, &MenuWidget::item);
  };
  EXPECT_TRUE(find(MenuItem::Scope).disabled);
  EXPECT_EQ(find(MenuItem::ScopeMore).tooltip, "Per-Game values need a running game.");

  s.settings.has_game = true;
  EXPECT_EQ(find(MenuItem::Scope).selected, 1);
  EXPECT_EQ(find(MenuItem::Scope).tooltip, "No Per-Game Values Set");
  EXPECT_EQ(find(MenuItem::ScopeMore).tooltip, "Nothing on this tab is set for this game.");
  s.settings.values.own_value.set(static_cast<std::size_t>(SettingId::Size));
  s.settings.values.game_value.set(static_cast<std::size_t>(SettingId::Size));
  EXPECT_EQ(find(MenuItem::Scope).tooltip, "1 Per-Game Value Set");
  EXPECT_FALSE(find(MenuItem::ScopeMore).disabled);
  // Another tab has none.
  s.tab = MenuTab::Settings;
  EXPECT_TRUE(find(MenuItem::ScopeMore).disabled);

  s.tab = MenuTab::Screen;
  EXPECT_EQ(GetLock(GetSettingRow(SettingId::Size), s.settings), Lock::None);
  s.settings.scope = SettingScope::Global;
  EXPECT_EQ(GetLock(GetSettingRow(SettingId::Size), s.settings), Lock::GameValue);
  EXPECT_EQ(GetLock(GetSettingRow(SettingId::Distance), s.settings), Lock::None);

  s.settings.values.global[static_cast<std::size_t>(SettingId::Size)] = 1.5f;
  s.settings.values.game[static_cast<std::size_t>(SettingId::Size)] = 2.5f;
  EXPECT_EQ(s.settings.Get(SettingId::Size), 1.5f);
  s.settings.scope = SettingScope::PerGame;
  EXPECT_EQ(s.settings.Get(SettingId::Size), 2.5f);

  s.open_scope = true;
  const MenuWidget option = find(MenuItem::PopupOption);
  EXPECT_LE(option.rect.x1, VrMenu::WIDTH - 16.0f);
}

TEST(VRFrameUI, EditVRSettingsKeepsTheRest)
{
  using GameSettingScope::EditVRSettings;
  EXPECT_EQ(EditVRSettings("", "Gamma", "1.2"), "[Graphics.VR]\nGamma = 1.2\n");
  EXPECT_EQ(EditVRSettings("[Graphics.VR]\nGamma = 1.2\n", "Gamma", std::nullopt), "");
  const std::string file = "# Mine\n[Graphics.VR]\nUnitsPerMeter = 10\ngamma = 2\n\n"
                           "[Hidden]\n$One\n";
  EXPECT_EQ(EditVRSettings(file, "Gamma", "1.5"), "# Mine\n[Graphics.VR]\nUnitsPerMeter = 10\n"
                                                  "Gamma = 1.5\n\n[Hidden]\n$One\n");
  EXPECT_EQ(EditVRSettings(file, "Gamma", std::nullopt),
            "# Mine\n[Graphics.VR]\nUnitsPerMeter = 10\n\n[Hidden]\n$One\n");
  EXPECT_EQ(EditVRSettings("[GFX.VR]\nARMode = True\n", "Passthrough", "False"),
            "[GFX.VR]\nPassthrough = False\n");
  EXPECT_EQ(EditVRSettings("[Hidden]\n$One\n", "Gamma", "1.5"),
            "[Hidden]\n$One\n\n[Graphics.VR]\nGamma = 1.5\n");
}

namespace
{
constexpr auto GLOBAL = SettingScope::Global;
constexpr auto PER_GAME = SettingScope::PerGame;

// A running game, with settings of its own.
class VRFrameUISettings : public testing::Test
{
protected:
  void SetUp() override
  {
    m_folder = File::CreateTempDir();
    UICommon::SetUserDirectory(m_folder);
    UICommon::CreateDirectories();
    Config::Init();
    Config::AddLayer(ConfigLoaders::GenerateBaseConfigLoader());
    SConfig::Init();
    SConfig::GetInstance().SetRunningGameMetadata("GTST01");
  }
  void TearDown() override
  {
    EndGame();
    SConfig::Shutdown();
    Config::Shutdown();
    File::DeleteDirRecursively(m_folder);
  }

  static std::string Read(const std::string& path)
  {
    std::string text;
    File::ReadFileToString(path, text);
    return text;
  }
  // As BootManager.
  static void EndGame()
  {
    Config::ClearCurrentRunLayer();
    Config::RemoveLayer(Config::LayerType::GlobalGame);
    Config::RemoveLayer(Config::LayerType::LocalGame);
    SConfig::GetInstance().ResetRunningGameMetadata();
  }
  static std::string VRFile()
  {
    return File::GetUserPath(D_GAMESETTINGSVR_IDX) + "GTST01.ini";
  }
  static std::string GameFile() { return File::GetUserPath(D_GAMESETTINGS_IDX) + "GTST01.ini"; }
  static bool Has(const std::bitset<NUM_SETTINGS>& bits, SettingId id)
  {
    return bits[static_cast<std::size_t>(id)];
  }

  std::string m_folder;
};
}  // namespace

TEST_F(VRFrameUISettings, EverySettingCanBeReadAndWritten)
{
  for (const SettingRow& row : GetSettingRows())
  {
    if (row.id == SettingId::None)
      continue;
    const auto i = static_cast<std::size_t>(row.id);
    for (const SettingScope scope : {GLOBAL, PER_GAME})
    {
      const SettingValues before = ReadSettings();
      float value = before.game[i] != 0.0f ? 0.0f : 1.0f;
      if (row.kind == RowKind::Slider)
        value = SliderValue(row, 0.25f) != before.game[i] ? SliderValue(row, 0.25f) :
                                                            SliderValue(row, 0.75f);
      else if (!row.options.empty())
        value = row.options[0].value != before.game[i] ? row.options[0].value :
                                                         row.options[1].value;
      WriteSetting(row.id, value, scope, true);
      const SettingValues after = ReadSettings();
      EXPECT_NEAR(after.game[i], value, 1e-3f) << row.label;
      const bool own = scope == PER_GAME && row.store != SettingStore::Global;
      EXPECT_EQ(Has(after.own_value, row.id), own) << row.label;
      if (own)
      {
        EXPECT_EQ(after.global[i], before.global[i]) << row.label;
      }
      else
      {
        EXPECT_NEAR(after.global[i], value, 1e-3f) << row.label;
      }
    }
  }
  SettingState state;
  state.values = ReadSettings();
  for (const MenuSection section : {MenuSection::Screen, MenuSection::General,
                                    MenuSection::Graphics, MenuSection::Immersive})
  {
    EXPECT_GT(CountOwnValues(section, state), 0);
    UseGlobalValues(section);
    state.values = ReadSettings();
    EXPECT_EQ(CountOwnValues(section, state), 0);
  }
  EXPECT_FALSE(File::Exists(VRFile()));
}

TEST_F(VRFrameUISettings, PerGameInTheVRFile)
{
  constexpr auto ID = SettingId::ScreenDepth;
  constexpr auto AT = static_cast<std::size_t>(ID);
  const float base = Config::GetBase(Config::GFX_VR_SCREEN_DEPTH);
  // A slider that is being moved.
  WriteSetting(ID, 40.0f, PER_GAME, false);
  EXPECT_EQ(Config::Get(Config::GFX_VR_SCREEN_DEPTH), 40.0f);
  EXPECT_FALSE(File::Exists(VRFile()));
  WriteSetting(ID, 45.0f, PER_GAME, true);
  EXPECT_NE(Read(VRFile()).find("ScreenDepth = 45"), std::string::npos);
  EXPECT_EQ(Config::GetBase(Config::GFX_VR_SCREEN_DEPTH), base);
  SettingValues values = ReadSettings();
  EXPECT_EQ(values.game[AT], 45.0f);
  EXPECT_EQ(values.global[AT], base);
  EXPECT_TRUE(Has(values.own_value, ID));
  EXPECT_TRUE(Has(values.game_value, ID));

  // The game starts again.
  EndGame();
  EXPECT_EQ(Config::Get(Config::GFX_VR_SCREEN_DEPTH), base);
  SConfig::GetInstance().SetRunningGameMetadata("GTST01");
  EXPECT_EQ(Config::Get(Config::GFX_VR_SCREEN_DEPTH), 45.0f);
  EXPECT_TRUE(Has(ReadSettings().own_value, ID));

  WriteSetting(ID, 70.0f, GLOBAL, true);
  EXPECT_EQ(Config::Get(Config::GFX_VR_SCREEN_DEPTH), 45.0f);
  UseGlobalValues(MenuSection::Graphics);
  EXPECT_EQ(Config::Get(Config::GFX_VR_SCREEN_DEPTH), 70.0f);
  EXPECT_FALSE(File::Exists(VRFile()));
  values = ReadSettings();
  EXPECT_FALSE(Has(values.own_value, ID));
  EXPECT_FALSE(Has(values.game_value, ID));
}

TEST_F(VRFrameUISettings, ValuesForSeveralGamesStay)
{
  // "GTS.ini" is for every region.
  const std::string path = File::GetUserPath(D_GAMESETTINGSVR_IDX) + "GTS.ini";
  File::CreateFullPath(path);
  File::WriteStringToFile(path, "[Graphics.VR]\nScreenDepth = 30\n");
  SConfig::GetInstance().ReloadGameVRConfigOverrides();
  SettingValues values = ReadSettings();
  EXPECT_TRUE(Has(values.game_value, SettingId::ScreenDepth));
  EXPECT_FALSE(Has(values.own_value, SettingId::ScreenDepth));
  WriteSetting(SettingId::ScreenDepth, 60.0f, PER_GAME, true);
  EXPECT_EQ(Config::Get(Config::GFX_VR_SCREEN_DEPTH), 60.0f);
  UseGlobalValues(MenuSection::Graphics);
  EXPECT_EQ(Config::Get(Config::GFX_VR_SCREEN_DEPTH), 30.0f);
  EXPECT_TRUE(Has(ReadSettings().game_value, SettingId::ScreenDepth));
  EXPECT_NE(Read(path).find("ScreenDepth = 30"), std::string::npos);
}

TEST_F(VRFrameUISettings, PerGameInTheGameFile)
{
  const int base = Config::GetBase(Config::GFX_EFB_SCALE);
  WriteSetting(SettingId::InternalResolution, 3.0f, PER_GAME, true);
  EXPECT_EQ(Config::Get(Config::GFX_EFB_SCALE), 3);
  EXPECT_EQ(Config::GetBase(Config::GFX_EFB_SCALE), base);
  EXPECT_NE(Read(GameFile()).find("InternalResolution = 3"), std::string::npos);
  EXPECT_TRUE(Has(ReadSettings().own_value, SettingId::InternalResolution));
  // A hot key changes it until the game ends; the menu's value is from now on.
  Config::SetCurrent(Config::GFX_EFB_SCALE, 1);
  WriteSetting(SettingId::InternalResolution, 4.0f, PER_GAME, true);
  EXPECT_EQ(Config::Get(Config::GFX_EFB_SCALE), 4);
  UseGlobalValues(MenuSection::Graphics);
  EXPECT_EQ(Config::Get(Config::GFX_EFB_SCALE), base);
  EXPECT_EQ(Read(GameFile()).find("InternalResolution"), std::string::npos);
}

TEST_F(VRFrameUISettings, WhereItIs)
{
  using namespace GameSettingScope;
  SetWhereItIs(Config::GFX_VR_SCREEN_SIZE, 2.0f, Store::VRFile);
  EXPECT_EQ(Config::GetBase(Config::GFX_VR_SCREEN_SIZE), 2.0f);
  EXPECT_FALSE(File::Exists(VRFile()));
  SetForGame(Config::GFX_VR_SCREEN_SIZE, 3.0f, Store::VRFile);
  SetWhereItIs(Config::GFX_VR_SCREEN_SIZE, 3.5f, Store::VRFile);
  EXPECT_EQ(Config::GetBase(Config::GFX_VR_SCREEN_SIZE), 2.0f);
  EXPECT_EQ(Config::Get(Config::GFX_VR_SCREEN_SIZE), 3.5f);
  EXPECT_NE(Read(VRFile()).find("ScreenSize = 3.5"), std::string::npos);
}

namespace
{
PadBinding On(std::string name)
{
  return {{std::move(name)}, {}};
}

struct Editor
{
  PadEdit edit;
  Controllers raw{};

  const PadBinding& Get(PadTarget target) const
  {
    return edit.GetView().table[static_cast<std::size_t>(target)];
  }
  PadEdit::Event Frame(float seconds = 0.011f, bool on_cancel = false)
  {
    return edit.Update(raw, seconds, on_cancel);
  }
};

PadTable GameCubeTable()
{
  PadTable table;
  const auto set = [&table](PadTarget target, PadBinding binding) {
    table[static_cast<std::size_t>(target)] = std::move(binding);
  };
  set(PadTarget::A, On("B"));
  set(PadTarget::B, On("A"));
  set(PadTarget::L, On("Left Trigger"));
  set(PadTarget::Z, {{}, "`Right Bumper` & `X`"});
  set(PadTarget::X, {{"Y", "Left Bumper"}, {}});
  set(PadTarget::Stick,
      {{"Left Stick Up", "Left Stick Down", "Left Stick Left", "Left Stick Right"}, {}});
  set(PadTarget::CStick,
      {{"Right Stick Up", "Right Stick Down", "Right Stick Left", "Right Stick Right"}, {}});
  return table;
}
}  // namespace

TEST(VRFrameUI, BindingsAreExpressions)
{
  EXPECT_EQ(ParseBinding("`A`"), On("A"));
  EXPECT_EQ(ParseBinding(" `Right Trigger` | `A` "), (PadBinding{{"Right Trigger", "A"}, {}}));
  EXPECT_EQ(ParseBinding("").names.size(), 0u);
  EXPECT_EQ(ParseBinding("`A` & `B`").custom, "`A` & `B`");
  EXPECT_EQ(ParseBinding("if(`A`|`B`,1,0)").custom, "if(`A`|`B`,1,0)");
  for (const char* const text : {"`A`", "`Right Trigger` | `A`", "", "`A` & !`B`"})
    EXPECT_EQ(MakeExpression(ParseBinding(text)), text);
  EXPECT_EQ(DescribeBinding(PadTarget::A, ParseBinding("`X` | `Y`")), "X or Y");
  EXPECT_EQ(DescribeBinding(PadTarget::A, ParseBinding("")), "None");
  EXPECT_EQ(DescribeBinding(PadTarget::A, ParseBinding("`X` & `Y`")), "Custom");
  const PadTable table = GameCubeTable();
  EXPECT_EQ(DescribeBinding(PadTarget::Stick, table[static_cast<std::size_t>(PadTarget::Stick)]),
            "Left Stick");
}

TEST(VRFrameUI, EditControlsSwaps)
{
  Editor e;
  e.edit.Begin(PadKind::GameCube, GameCubeTable());
  // The trigger that chose the chip is still down.
  e.raw[1].trigger_value = 1.0f;
  e.edit.Choose(PadTarget::A);
  EXPECT_TRUE(e.edit.IsListening());
  EXPECT_EQ(GetPrompt(e.edit.GetView()), "Press a Button\u2026");
  EXPECT_EQ(e.Frame(), PadEdit::Event::None);
  e.raw[1].trigger_value = 0.0f;
  EXPECT_EQ(e.Frame(), PadEdit::Event::None);
  e.raw[1].primary_button = true;
  EXPECT_EQ(e.Frame(), PadEdit::Event::Changed);
  EXPECT_FALSE(e.edit.IsListening());
  EXPECT_EQ(e.Get(PadTarget::A), On("A"));
  EXPECT_EQ(e.Get(PadTarget::B), On("B"));
  auto changes = e.edit.TakeChanges();
  std::ranges::sort(changes);
  EXPECT_EQ(changes, (std::vector{PadTarget::A, PadTarget::B}));
  EXPECT_TRUE(e.edit.TakeChanges().empty());

  // The same again changes nothing.
  e.raw = {};
  e.edit.Choose(PadTarget::A);
  e.Frame();
  e.raw[1].primary_button = true;
  EXPECT_EQ(e.Frame(), PadEdit::Event::Cancelled);
  EXPECT_TRUE(e.edit.TakeChanges().empty());

  // One of two inputs is exchanged.
  e.raw = {};
  e.edit.Choose(PadTarget::L);
  e.Frame();
  e.raw[0].bumper_button = true;
  EXPECT_EQ(e.Frame(), PadEdit::Event::Changed);
  EXPECT_EQ(e.Get(PadTarget::L), On("Left Bumper"));
  EXPECT_EQ(e.Get(PadTarget::X), (PadBinding{{"Y", "Left Trigger"}, {}}));

  // A formula is kept until it is replaced.
  e.raw = {};
  e.edit.Choose(PadTarget::Y);
  e.Frame();
  e.raw[1].bumper_button = true;
  EXPECT_EQ(e.Frame(), PadEdit::Event::Changed);
  EXPECT_EQ(e.Get(PadTarget::Z).custom, "`Right Bumper` & `X`");
}

TEST(VRFrameUI, EditControlsCancels)
{
  Editor e;
  const PadTable table = GameCubeTable();
  e.edit.Begin(PadKind::GameCube, table);
  // Choosing it again.
  e.edit.Choose(PadTarget::A);
  e.edit.Choose(PadTarget::A);
  EXPECT_FALSE(e.edit.IsListening());
  // Not a control of this controller.
  e.edit.Choose(PadTarget::One);
  EXPECT_FALSE(e.edit.IsListening());
  // The trigger on Cancel.
  e.edit.Choose(PadTarget::A);
  e.Frame(0.011f, true);
  e.raw[1].trigger_value = 1.0f;
  EXPECT_EQ(e.Frame(0.011f, true), PadEdit::Event::Cancelled);
  // Nobody presses anything.
  e.raw = {};
  e.edit.Choose(PadTarget::B);
  e.Frame();
  for (int i = 0; i < 59; ++i)
  {
    EXPECT_EQ(e.Frame(0.1f), PadEdit::Event::None);
    if (i == 20)
    {
      EXPECT_EQ(e.edit.GetView().countdown, 0);
    }
    if (i == 45)
    {
      EXPECT_EQ(e.edit.GetView().countdown, 2);
    }
  }
  EXPECT_EQ(GetPrompt(e.edit.GetView()), "Press a Button\u2026 1");
  PadEdit::Event event = PadEdit::Event::None;
  for (int i = 0; i < 3 && event == PadEdit::Event::None; ++i)
    event = e.Frame(0.1f);
  EXPECT_EQ(event, PadEdit::Event::Cancelled);
  EXPECT_EQ(e.edit.GetView().table, table);
  EXPECT_TRUE(e.edit.TakeChanges().empty());
}

TEST(VRFrameUI, EditControlsSticks)
{
  Editor e;
  e.edit.Begin(PadKind::GameCube, GameCubeTable());
  e.edit.Choose(PadTarget::Stick);
  e.Frame();
  // A button is no answer.
  e.raw[1].primary_button = true;
  EXPECT_EQ(e.Frame(), PadEdit::Event::None);
  e.raw = {};
  e.raw[1].thumbstick_y = 0.9f;
  EXPECT_EQ(e.Frame(), PadEdit::Event::Next);
  EXPECT_EQ(GetPrompt(e.edit.GetView()), "Push the Stick Right\u2026");
  EXPECT_EQ(e.Frame(), PadEdit::Event::None);
  // The other stick is not it.
  e.raw[0].thumbstick_x = 0.9f;
  EXPECT_EQ(e.Frame(), PadEdit::Event::None);
  e.raw = {};
  e.Frame();
  e.raw[1].thumbstick_x = 0.9f;
  EXPECT_EQ(e.Frame(), PadEdit::Event::Changed);
  EXPECT_EQ(DescribeBinding(PadTarget::Stick, e.Get(PadTarget::Stick)), "Right Stick");
  EXPECT_EQ(DescribeBinding(PadTarget::CStick, e.Get(PadTarget::CStick)), "Left Stick");
  EXPECT_EQ(e.Get(PadTarget::Stick).names,
            (std::vector<std::string>{"Right Stick Up", "Right Stick Down", "Right Stick Left",
                                      "Right Stick Right"}));
}

TEST(VRFrameUI, EditControlsOfTheWiiRemote)
{
  Editor e;
  e.edit.Begin(PadKind::WiiNunchuk, {});
  // Its D-Pad is always there.
  e.edit.Choose(PadTarget::One);
  e.Frame();
  e.raw[0].dpad_up = true;
  EXPECT_EQ(e.Frame(), PadEdit::Event::None);
  e.raw = {};
  e.raw[0].view_button = true;
  EXPECT_EQ(e.Frame(), PadEdit::Event::Changed);
  EXPECT_EQ(e.Get(PadTarget::One), On("View"));
  // The right stick is its D-Pad.
  e.raw = {};
  e.edit.Choose(PadTarget::NunchukStick);
  e.Frame();
  e.raw[1].thumbstick_y = 1.0f;
  EXPECT_EQ(e.Frame(), PadEdit::Event::None);
}

TEST(VRFrameUI, EditControlsHasAChipForEveryControl)
{
  for (const PadKind kind : {PadKind::GameCube, PadKind::WiiRemote, PadKind::WiiRemoteSideways,
                             PadKind::WiiNunchuk})
  {
    MenuState s;
    s.tab = MenuTab::Controls;
    s.page = MenuPage::EditControls;
    s.pad = kind;
    std::set<int> chips;
    for (const MenuWidget& w : VrMenu::Layout(s))
    {
      if (w.item == MenuItem::EditChip)
        chips.insert(w.index);
    }
    EXPECT_EQ(chips.size(), GetTargets(kind).size());
    for (const PadTarget target : GetTargets(kind))
      EXPECT_TRUE(chips.contains(static_cast<int>(target)));
  }
}
