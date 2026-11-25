// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include "Common/ListNav.h"

using namespace Common;

namespace
{
constexpr int64_t MS = 1'000'000;

struct TestPad
{
  bool up = false, down = false, a = false;
  float stick = 0.0f;
};

// Runs frames 8 ms apart and counts what comes out.
struct Runner
{
  ListNav nav;
  int64_t now = 0;
  int steps_up = 0, steps_down = 0, starts = 0;

  void Run(const TestPad& p, int64_t ms, bool enabled = true)
  {
    for (int64_t t = 0; t < ms; t += 8)
    {
      const ListNav::Result r = nav.Update(enabled, p.up, p.down, p.stick, p.a, now);
      steps_up += r.step < 0;
      steps_down += r.step > 0;
      starts += r.activate;
      now += 8 * MS;
    }
  }
};
}  // namespace

TEST(ListNav, PressStepsOnceHoldRepeats)
{
  Runner r;
  r.Run({}, 50);
  r.Run({.down = true}, 100);
  EXPECT_EQ(r.steps_down, 1);
  r.Run({}, 50);
  r.Run({.up = true}, 50);
  EXPECT_EQ(r.steps_up, 1);
  r.Run({}, 50);
  // Held 1 s: one at once, then after 400 ms every 110 ms: 1 + floor(600 / 110) + 1.
  r.steps_down = 0;
  r.Run({.down = true}, 1000);
  EXPECT_GE(r.steps_down, 6);
  EXPECT_LE(r.steps_down, 7);
  // Both directions at once: nothing.
  r.Run({}, 50);
  r.steps_up = r.steps_down = 0;
  r.Run({.up = true, .down = true}, 500);
  EXPECT_EQ(r.steps_up + r.steps_down, 0);
}

TEST(ListNav, NoBurstAfterALongFrame)
{
  ListNav nav;
  nav.Update(true, false, false, 0.0f, false, 0);
  EXPECT_EQ(nav.Update(true, false, true, 0.0f, false, 10 * MS).step, 1);
  EXPECT_EQ(nav.Update(true, false, true, 0.0f, false, 3000 * MS).step, 1);
  EXPECT_EQ(nav.Update(true, false, true, 0.0f, false, 3016 * MS).step, 0);
  EXPECT_EQ(nav.Update(true, false, true, 0.0f, false, 3032 * MS).step, 0);
}

TEST(ListNav, StickHysteresis)
{
  Runner r;
  r.Run({.stick = 0.5f}, 200);  // under stick_on
  EXPECT_EQ(r.steps_up, 0);
  r.Run({.stick = 0.7f}, 50);
  EXPECT_EQ(r.steps_up, 1);
  r.Run({.stick = 0.45f}, 300);  // above stick_off: still held, no new step
  EXPECT_EQ(r.steps_up, 1);
  r.Run({.stick = 0.3f}, 50);   // let go
  r.Run({.stick = -0.8f}, 50);  // down
  EXPECT_EQ(r.steps_down, 1);
}

TEST(ListNav, MainAxis)
{
  EXPECT_EQ(MainAxis(0.8f, 0.3f), 0.8f);
  EXPECT_EQ(MainAxis(-0.8f, 0.3f), -0.8f);
  EXPECT_EQ(MainAxis(0.65f, 0.7f), 0.0f);
}

TEST(ListNav, DoubleTapStarts)
{
  Runner r;
  r.Run({}, 50);
  // One tap.
  r.Run({.a = true}, 80);
  r.Run({}, 600);
  EXPECT_EQ(r.starts, 0);
  // Two taps, press to press 180 ms.
  r.Run({.a = true}, 80);
  r.Run({}, 100);
  r.Run({.a = true}, 80);
  EXPECT_EQ(r.starts, 1);
  r.Run({}, 600);
  // Two taps 500 ms apart: too slow.
  r.Run({.a = true}, 80);
  r.Run({}, 420);
  r.Run({.a = true}, 80);
  EXPECT_EQ(r.starts, 1);
  r.Run({}, 600);
  // Three quick taps: one start (the third begins a new pair).
  r.starts = 0;
  for (int i = 0; i < 3; i++)
  {
    r.Run({.a = true}, 64);
    r.Run({}, 64);
  }
  EXPECT_EQ(r.starts, 1);
}

TEST(ListNav, SinglePressStartsWithoutADoubleTap)
{
  Runner r;
  r.nav = ListNav{ListNavTiming{.double_tap_ns = 0}};
  r.Run({}, 50);
  r.Run({.a = true}, 500);
  EXPECT_EQ(r.starts, 1);
}

TEST(ListNav, DoubleTapLimit)
{
  ListNav nav;
  const int64_t limit = ListNavTiming{}.double_tap_ns;
  nav.Update(true, false, false, 0.0f, false, 0);
  nav.Update(true, false, false, 0.0f, true, 1 * MS);
  nav.Update(true, false, false, 0.0f, false, 2 * MS);
  EXPECT_TRUE(nav.Update(true, false, false, 0.0f, true, 1 * MS + limit).activate);

  ListNav late;
  late.Update(true, false, false, 0.0f, false, 0);
  late.Update(true, false, false, 0.0f, true, 1 * MS);
  late.Update(true, false, false, 0.0f, false, 2 * MS);
  EXPECT_FALSE(late.Update(true, false, false, 0.0f, true, 2 * MS + limit).activate);
}

TEST(ListNav, HeldFromBeforeDoesNothing)
{
  Runner r;
  r.Run({.down = true}, 500, false);
  r.Run({.a = true}, 50, false);
  r.Run({}, 50, false);
  r.Run({.a = true}, 50, false);
  EXPECT_EQ(r.steps_down + r.starts, 0);
  // A direction held while the list becomes allowed (a dialog closes): no step until let go.
  r.Run({.down = true}, 100, false);
  r.Run({.down = true}, 800, true);
  EXPECT_EQ(r.steps_down, 0);
  r.Run({}, 50);
  r.Run({.down = true}, 50);
  EXPECT_EQ(r.steps_down, 1);
  // A first tap before a dialog closed and a second one after it: no start.
  r.Run({}, 50);
  r.Run({.a = true}, 50, false);
  r.Run({}, 50, false);
  r.Run({.a = true}, 50, true);
  EXPECT_EQ(r.starts, 0);
  // An A held when the window appears is no tap.
  ListNav fresh;
  EXPECT_FALSE(fresh.Update(true, false, false, 0.0f, true, 0).activate);
  EXPECT_FALSE(fresh.Update(true, false, false, 0.0f, false, 10 * MS).activate);
  EXPECT_FALSE(fresh.Update(true, false, false, 0.0f, true, 20 * MS).activate);
  EXPECT_EQ(fresh.Update(true, false, false, 0.0f, false, 30 * MS).step, 0);
}

TEST(ListNav, ButtonPressOnce)
{
  constexpr int64_t NEVER = INT64_MIN;
  ButtonPress button;
  int presses = 0;
  // Held from before: nothing until let go.
  for (int64_t t = 0; t < 500; t += 16)
    presses += button.Update(true, true, NEVER, t * MS);
  EXPECT_EQ(presses, 0);
  button.Update(true, false, NEVER, 500 * MS);
  // A short press comes out once, after the wait, also when the button is up again by then.
  EXPECT_FALSE(button.Update(true, true, NEVER, 516 * MS));
  for (int64_t t = 532; t < 1000; t += 16)
    presses += button.Update(true, false, NEVER, t * MS);
  EXPECT_EQ(presses, 1);
}

TEST(ListNav, ButtonPressThatIsAlsoAClick)
{
  ButtonPress button;
  button.Update(true, false, INT64_MIN, 0);
  // The click comes just after the button.
  EXPECT_FALSE(button.Update(true, true, INT64_MIN, 1000 * MS));
  EXPECT_FALSE(button.Update(true, true, 1010 * MS, 1070 * MS));
  EXPECT_FALSE(button.Update(true, false, 1010 * MS, 1100 * MS));
  // Or just before it.
  EXPECT_FALSE(button.Update(true, true, 1900 * MS, 2000 * MS));
  EXPECT_FALSE(button.Update(true, true, 1900 * MS, 2070 * MS));
  button.Update(true, false, 1900 * MS, 2100 * MS);
  // An older click is none of its business.
  EXPECT_FALSE(button.Update(true, true, 1900 * MS, 3000 * MS));
  EXPECT_TRUE(button.Update(true, true, 1900 * MS, 3070 * MS));
}

TEST(ListNav, ButtonPressDroppedWhenNotAllowed)
{
  ButtonPress button;
  button.Update(true, false, INT64_MIN, 0);
  EXPECT_FALSE(button.Update(true, true, INT64_MIN, 100 * MS));
  EXPECT_FALSE(button.Update(false, true, INT64_MIN, 116 * MS));
  EXPECT_FALSE(button.Update(true, true, INT64_MIN, 500 * MS));
}

TEST(ListNav, AllowedOnlyWithNothingOverTheList)
{
  ListNavState s;
  s.list_visible = true;
  s.main_active = true;
  EXPECT_TRUE(ListNavAllowed(s));

  const auto with = [&](auto change) {
    ListNavState t = s;
    change(t);
    return ListNavAllowed(t);
  };
  EXPECT_FALSE(with([](ListNavState& t) { t.game_running = true; }));
  EXPECT_FALSE(with([](ListNavState& t) { t.list_visible = false; }));
  EXPECT_FALSE(with([](ListNavState& t) { t.popup_open = true; }));
  EXPECT_FALSE(with([](ListNavState& t) { t.dialog_open = true; }));
  EXPECT_FALSE(with([](ListNavState& t) { t.main_active = false; }));
}
