// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

// The game list with the controller, in Baiji's window before a game starts: the D-pad or the left
// stick move the highlighted game, repeating while held. From Azahar for Frame. Pure arithmetic, no
// Qt: ListNav is the timing, ButtonPress one button, ListNavAllowed the rule for when the window
// takes them.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Common
{
struct ListNavTiming
{
  // A second press of A this soon after the first starts the game (Qt's double-click interval). 0:
  // one press starts it.
  int64_t double_tap_ns = 400'000'000;
  // A direction held: the first step at once, the second after this, then every repeat_ns.
  int64_t repeat_delay_ns = 400'000'000;
  int64_t repeat_ns = 110'000'000;
  // The left stick is a direction past stick_on and lets go below stick_off.
  float stick_on = 0.6f;
  float stick_off = 0.4f;
};

class ListNav
{
public:
  struct Result
  {
    int step = 0;           // -1 one game up, +1 one game down, 0 nothing
    bool activate = false;  // A pressed twice quickly: start the highlighted game
  };

  explicit ListNav(const ListNavTiming& timing = {}) : t(timing) {}

  // Once per frame. `enabled` false (a game runs, a dialog or menu is open): nothing comes out, and
  // a direction or A held as it becomes true does nothing until let go. `stick_y`: the left stick,
  // positive up. Left and right are the same with left as up and -x as the stick.
  Result Update(bool enabled, bool dpad_up, bool dpad_down, float stick_y, bool a, int64_t now_ns)
  {
    Result r;
    // The stick's direction, with hysteresis.
    if (stick_dir == 0)
      stick_dir = stick_y >= t.stick_on ? -1 : stick_y <= -t.stick_on ? 1 : 0;
    else if (stick_dir * -stick_y < t.stick_off)
      stick_dir = 0;
    int dir = dpad_up == dpad_down ? 0 : dpad_up ? -1 : 1;
    if (dir == 0)
      dir = stick_dir;
    const bool a_edge = a && !prev_a;
    prev_a = a;
    if (!enabled)
    {
      held_dir = dir;  // held from before: nothing until let go
      blocked = true;
      last_tap_ns = NONE;
      return r;
    }
    if (dir != held_dir)
    {
      held_dir = dir;
      blocked = false;
      if (dir != 0)
      {
        r.step = dir;
        next_repeat_ns = now_ns + t.repeat_delay_ns;
      }
    }
    else if (dir != 0 && !blocked && now_ns >= next_repeat_ns)
    {
      r.step = dir;
      // Never a burst after a long frame.
      next_repeat_ns = std::max(next_repeat_ns + t.repeat_ns, now_ns + t.repeat_ns / 2);
    }
    if (a_edge)
    {
      if (t.double_tap_ns == 0 || (last_tap_ns != NONE && now_ns - last_tap_ns <= t.double_tap_ns))
      {
        r.activate = true;
        last_tap_ns = NONE;  // a third tap starts a new pair
      }
      else
      {
        last_tap_ns = now_ns;
      }
    }
    return r;
  }

  void Reset() { *this = ListNav{t}; }

private:
  static constexpr int64_t NONE = INT64_MIN;
  ListNavTiming t;
  int stick_dir = 0;
  int held_dir = 0;
  bool blocked = true;  // held_dir was held when enabled turned true
  bool prev_a = true;   // an A held when the window appears is no press
  int64_t next_repeat_ns = 0;
  int64_t last_tap_ns = NONE;
};

// A button whose press is a moment late, so that it can be dropped: a controller whose layout makes
// the same button a mouse click or a key sends those, too, and then they are the press.
class ButtonPress
{
public:
  // How long the press waits, and how long before it a click or key counts as the same press.
  static constexpr int64_t WAIT_NS = 60'000'000;
  static constexpr int64_t BEFORE_NS = 150'000'000;

  // Once per frame. `other_ns`: when the last click or key came from somewhere else. A button held
  // as `enabled` becomes true does nothing until let go.
  bool Update(bool enabled, bool down, int64_t other_ns, int64_t now_ns)
  {
    const bool edge = down && !held;
    held = down;
    if (!enabled)
    {
      pressed_ns = NONE;
      return false;
    }
    if (edge)
      pressed_ns = now_ns;
    if (pressed_ns == NONE || now_ns - pressed_ns < WAIT_NS)
      return false;
    const bool same = other_ns >= pressed_ns - BEFORE_NS;
    pressed_ns = NONE;
    return !same;
  }

private:
  static constexpr int64_t NONE = INT64_MIN;
  bool held = true;
  int64_t pressed_ns = NONE;
};

// A stick's push along one axis, 0 unless it is the stick's main axis: a diagonal is one direction.
inline float MainAxis(float along, float across)
{
  return std::abs(along) >= std::abs(across) ? along : 0.0f;
}

// What the window looks like (GUI thread).
struct ListNavState
{
  bool game_running = false;  // a game runs (also paused, with a menu over it)
  bool list_visible = false;  // the game list (not the empty page) shows
  bool popup_open = false;    // a menu or dropdown is open
  bool dialog_open = false;   // a dialog or another window of Baiji's is shown
  bool main_active = false;   // Baiji's main window has the keys
};

// The D-pad, the left stick and A drive the game list only here: no game, the list shown, nothing
// over it, and the main window has the keys (other apps share the screen).
inline bool ListNavAllowed(const ListNavState& s)
{
  return !s.game_running && s.list_visible && !s.popup_open && !s.dialog_open && s.main_active;
}
}  // namespace Common
