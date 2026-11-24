// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cmath>
#include <string>

#include <gtest/gtest.h>

#include "Core/Autosave.h"
#include "InputCommon/ControllerInterface/OpenXR/FrameControllerNames.h"
#include "VideoCommon/VR/FrameUI/PadBindings.h"
#include "VideoCommon/VR/FrameUI/PadFilter.h"
#include "VideoCommon/VR/FrameUI/ScreenPlacement.h"
#include "VideoCommon/VR/FrameUI/VrMenu.h"

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
  state.autosave = true;
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

  // Everything is on the panel.
  for (const MenuTab tab :
       {MenuTab::Main, MenuTab::Screen, MenuTab::Controls, MenuTab::Mods, MenuTab::Settings})
  {
    for (const int page : {0, 1})
    {
      MenuState s;
      s.tab = tab;
      s.controls_page = page;
      s.wii = page == 1;
      s.mods.resize(13);
      s.mods_page = page * 2;
      s.mods_changed = true;
      for (const MenuWidget& w : VrMenu::Layout(s))
      {
        EXPECT_GE(w.rect.x0, 0.0f);
        EXPECT_GE(w.rect.y0, 0.0f);
        EXPECT_LE(w.rect.x1, VrMenu::WIDTH);
        EXPECT_LE(w.rect.y1, VrMenu::HEIGHT - 16.0f);
      }
    }
  }
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
