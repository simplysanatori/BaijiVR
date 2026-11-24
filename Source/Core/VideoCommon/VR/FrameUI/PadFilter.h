// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

#include <array>
#include <cstdint>

#include "Common/CommonTypes.h"
#include "Common/VR/OpenXRInputState.h"

// The VR controls that are the same in every game (Azahar for Frame's), taken out of the
// controllers before the game sees them:
//
//   Menu tapped: the game's button. Held 1 s: the VR menu.
//   View tapped: the game's button. Held 1.5 s: stop the game.
//   View + Right Bumper: Quick Save. View + Left Bumper held half a second: Quick Load.
//   View + Right Trigger: fast forward while held.
//   Right Stick clicked and held 1 s: reset the screen.
//
// A control that did one of these, or was down when the menu closed, stays away from the game
// until it is let go. No OpenXR here (unit tested).
namespace VR::UI
{
using Controllers = std::array<Common::VR::OpenXRControllerState, 2>;

struct PadEvents
{
  bool open_menu = false;
  bool stop = false;
  bool quick_save = false;
  bool quick_load = false;
  bool reset_screen = false;
  // While View is held towards the stop: 0 (not yet) to 1.
  float stop_progress = 0.0f;
};

class PadFilter
{
public:
  static constexpr s64 MENU_TAP_NS = 300'000'000;
  static constexpr s64 MENU_HOLD_NS = 1'000'000'000;
  static constexpr s64 VIEW_TAP_NS = 500'000'000;
  static constexpr s64 STOP_NS = 1'500'000'000;
  static constexpr s64 QUICK_LOAD_NS = 500'000'000;
  static constexpr s64 RESET_NS = 1'000'000'000;
  // A tap reaches the game as a press this long: three frames of a 30 fps game.
  static constexpr s64 PULSE_NS = 100'000'000;
  static constexpr float TRIGGER_ON = 0.55f;
  static constexpr float TRIGGER_OFF = 0.40f;

  // One frame. `controllers` comes in as read and goes out as the game may see it. `blocked`: the
  // VR menu has the controllers and the game gets nothing. `grab_hand`: that hand holds the
  // screen (-1: neither), and its grip and stick, and the D-Pad, stay away from the game.
  PadEvents Update(Controllers* controllers, s64 now_ns, bool blocked, int grab_hand);

  // The session lost focus: nothing held counts any more.
  void Reset();

  bool IsFastForward() const { return m_fast_forward; }

private:
  static constexpr std::size_t NUM_BUTTONS = 15;

  enum class MenuState
  {
    Idle,
    Waiting,
    Passing,
    Done,
  };

  void Swallow(Controllers* controllers);
  void SwallowHeld(const Controllers& controllers);

  MenuState m_menu = MenuState::Idle;
  s64 m_menu_since = 0;
  s64 m_menu_pulse_until = 0;

  bool m_view_down = false;
  bool m_view_used = false;
  s64 m_view_since = 0;
  s64 m_view_pulse_until = 0;
  bool m_right_bumper_down = false;
  bool m_left_bumper_down = false;
  bool m_left_bumper_done = false;
  bool m_right_bumper_ignored = false;
  bool m_left_bumper_ignored = false;
  bool m_right_trigger_ignored = false;
  s64 m_left_bumper_since = 0;
  bool m_fast_forward = false;

  bool m_reset_down = false;
  bool m_reset_done = false;
  bool m_reset_ignored = false;
  s64 m_reset_since = 0;

  bool m_blocked = false;

  // Away from the game until let go.
  std::array<std::array<bool, NUM_BUTTONS>, 2> m_swallowed{};
  std::array<bool, 2> m_stick_swallowed{};
};
}  // namespace VR::UI

#endif  // ENABLE_VR
