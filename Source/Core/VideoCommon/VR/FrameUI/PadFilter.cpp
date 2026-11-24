// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef ENABLE_VR

#include "VideoCommon/VR/FrameUI/PadFilter.h"

#include <algorithm>
#include <cmath>

namespace VR::UI
{
namespace
{
using State = Common::VR::OpenXRControllerState;

constexpr std::size_t LEFT = 0;
constexpr std::size_t RIGHT = 1;

enum ButtonIndex : std::size_t
{
  A,
  B,
  X,
  Y,
  MENU,
  SYSTEM,
  VIEW,
  BUMPER,
  DPAD_UP,
  DPAD_DOWN,
  DPAD_LEFT,
  DPAD_RIGHT,
  TRIGGER,
  GRIP,
  STICK,
};

constexpr std::array<bool State::*, 15> BUTTONS{
    &State::primary_button,   &State::secondary_button, &State::frame_x_button,
    &State::frame_y_button,   &State::menu_button,      &State::system_button,
    &State::view_button,      &State::bumper_button,    &State::dpad_up,
    &State::dpad_down,        &State::dpad_left,        &State::dpad_right,
    &State::trigger_button,   &State::squeeze_button,   &State::thumbstick_button,
};

// The trigger and the grip also count as down by their value: an analog binding sees that.
bool IsDown(const State& state, std::size_t button)
{
  if (button == TRIGGER)
    return state.trigger_button || state.trigger_value > 0.1f;
  if (button == GRIP)
    return state.squeeze_button || state.squeeze_value > 0.1f;
  return state.*BUTTONS[button];
}

void Clear(State* state, std::size_t button)
{
  state->*BUTTONS[button] = false;
  if (button == TRIGGER)
    state->trigger_value = 0.0f;
  if (button == GRIP)
  {
    state->squeeze_value = 0.0f;
    state->squeeze_force = 0.0f;
  }
}

float Deflection(const State& state)
{
  return std::max(std::abs(state.thumbstick_x), std::abs(state.thumbstick_y));
}

void ClearStick(State* state)
{
  state->thumbstick_x = 0.0f;
  state->thumbstick_y = 0.0f;
}
}  // namespace

static_assert(BUTTONS.size() == 15);

void PadFilter::Reset()
{
  *this = PadFilter{};
}

void PadFilter::SwallowHeld(const Controllers& controllers)
{
  for (std::size_t hand = 0; hand < controllers.size(); ++hand)
  {
    for (std::size_t button = 0; button < NUM_BUTTONS; ++button)
      m_swallowed[hand][button] |= IsDown(controllers[hand], button);
    m_stick_swallowed[hand] |= Deflection(controllers[hand]) > 0.3f;
  }
}

// Let go or not is as read (`raw`): this may have taken it out of `controllers` already.
void PadFilter::Swallow(Controllers* controllers, const Controllers& raw)
{
  for (std::size_t hand = 0; hand < controllers->size(); ++hand)
  {
    State& state = (*controllers)[hand];
    for (std::size_t button = 0; button < NUM_BUTTONS; ++button)
    {
      bool& swallowed = m_swallowed[hand][button];
      swallowed = swallowed && IsDown(raw[hand], button);
      if (swallowed)
        Clear(&state, button);
    }
    m_stick_swallowed[hand] = m_stick_swallowed[hand] && Deflection(raw[hand]) > 0.3f;
    if (m_stick_swallowed[hand])
      ClearStick(&state);
  }
}

PadEvents PadFilter::Update(Controllers* controllers, s64 now_ns, bool blocked, int grab_hand)
{
  PadEvents events;
  State& left = (*controllers)[LEFT];
  State& right = (*controllers)[RIGHT];

  if (blocked)
  {
    // What is down when the menu lets go of the controllers was meant for the menu.
    const Controllers raw = *controllers;
    Reset();
    m_blocked = true;
    SwallowHeld(raw);
    for (State& state : *controllers)
    {
      for (std::size_t button = 0; button < NUM_BUTTONS; ++button)
        Clear(&state, button);
      ClearStick(&state);
      state.screen_hit = {};
    }
    return events;
  }
  m_blocked = false;

  // Swallowed controls look let go to everything below, but for the View combinations, which
  // swallow their own.
  const Controllers raw = *controllers;
  Swallow(controllers, raw);

  // ---- Both bumpers, View and Menu ----
  const bool all = raw[LEFT].bumper_button && raw[RIGHT].bumper_button &&
                   (raw[LEFT].view_button || raw[RIGHT].view_button) &&
                   (raw[LEFT].menu_button || raw[RIGHT].menu_button);
  if (all && !m_switch_down)
  {
    events.switch_mode = true;
    SwallowHeld(raw);
    Swallow(controllers, raw);
    m_menu = MenuState::Idle;
    m_view_used = true;
  }
  m_switch_down = all;

  // ---- Menu ----
  const bool menu = right.menu_button || left.menu_button;
  switch (m_menu)
  {
  case MenuState::Idle:
    if (menu)
    {
      m_menu = MenuState::Waiting;
      m_menu_since = now_ns;
    }
    break;
  case MenuState::Waiting:
    if (!menu)
    {
      m_menu = MenuState::Idle;
      m_menu_pulse_until = now_ns + PULSE_NS;
    }
    else if (now_ns - m_menu_since >= MENU_TAP_NS)
    {
      m_menu = MenuState::Passing;
    }
    break;
  case MenuState::Passing:
    if (!menu)
    {
      m_menu = MenuState::Idle;
    }
    else if (now_ns - m_menu_since >= MENU_HOLD_NS)
    {
      m_menu = MenuState::Done;
      events.open_menu = true;
    }
    break;
  case MenuState::Done:
    if (!menu)
      m_menu = MenuState::Idle;
    break;
  }
  left.menu_button = false;
  right.menu_button = m_menu == MenuState::Passing || now_ns < m_menu_pulse_until;

  // ---- View ----
  const bool view = left.view_button || right.view_button;
  const bool right_bumper_raw = raw[RIGHT].bumper_button;
  const bool left_bumper_raw = raw[LEFT].bumper_button;
  const bool right_trigger_raw = IsDown(raw[RIGHT], TRIGGER);
  if (view && !m_view_down)
  {
    m_view_since = now_ns;
    m_view_used = false;
    m_left_bumper_done = false;
    m_right_bumper_down = false;
    m_left_bumper_down = false;
    m_right_trigger_down = false;
    // What was down before View is the game's.
    m_right_bumper_ignored = true;
    m_left_bumper_ignored = true;
    m_right_trigger_ignored = true;
  }
  m_right_bumper_ignored = m_right_bumper_ignored && right_bumper_raw;
  m_left_bumper_ignored = m_left_bumper_ignored && left_bumper_raw;
  m_right_trigger_ignored = m_right_trigger_ignored && right_trigger_raw;
  if (view)
  {
    const bool right_bumper = right_bumper_raw && !m_right_bumper_ignored;
    const bool left_bumper = left_bumper_raw && !m_left_bumper_ignored;
    const bool right_trigger = right_trigger_raw && !m_right_trigger_ignored;

    if (m_right_bumper_down && !right_bumper)
      events.quick_save = true;
    m_view_used |= right_bumper;
    m_right_bumper_down = right_bumper;

    if (left_bumper && !m_left_bumper_down)
    {
      m_left_bumper_since = now_ns;
      m_left_bumper_done = false;
    }
    m_left_bumper_down = left_bumper;
    if (left_bumper)
    {
      m_view_used = true;
      if (!m_left_bumper_done && now_ns - m_left_bumper_since >= QUICK_LOAD_NS)
      {
        m_left_bumper_done = true;
        events.quick_load = true;
      }
    }

    const float trigger = raw[RIGHT].trigger_value;
    const bool pulled =
        right_trigger && (trigger > (m_right_trigger_down ? TRIGGER_OFF : TRIGGER_ON) ||
                          (raw[RIGHT].trigger_button && trigger <= 0.0f));
    if (m_right_trigger_down && !pulled)
      events.fast_forward = true;
    m_view_used |= pulled;
    m_right_trigger_down = pulled;

    if (!m_view_used)
    {
      const s64 held = now_ns - m_view_since;
      events.stopping = held >= STOP_NS / 2;
      if (held >= STOP_NS)
      {
        events.stop = true;
        events.stopping = false;
        m_view_used = true;
      }
    }

    // Their own presses are for these, not the game.
    m_swallowed[RIGHT][BUMPER] |= right_bumper;
    m_swallowed[LEFT][BUMPER] |= left_bumper;
    m_swallowed[RIGHT][TRIGGER] |= right_trigger;
  }
  else
  {
    if (m_view_down && !m_view_used && now_ns - m_view_since < VIEW_TAP_NS)
      m_view_pulse_until = now_ns + PULSE_NS;
    m_right_trigger_down = false;
  }
  m_view_down = view;
  right.view_button = false;
  left.view_button = now_ns < m_view_pulse_until;

  // ---- Right stick click: the game sees it as it is, until it becomes the reset ----
  if (right.thumbstick_button && !m_reset_down)
  {
    m_reset_since = now_ns;
    m_reset_done = false;
    // Pushed in while turning a camera: not this.
    m_reset_ignored = Deflection(right) > 0.5f;
  }
  m_reset_down = right.thumbstick_button;
  if (m_reset_down && !m_reset_ignored && !m_reset_done && now_ns - m_reset_since >= RESET_NS)
  {
    m_reset_done = true;
    events.reset_screen = true;
    m_swallowed[RIGHT][STICK] = true;
  }

  // ---- A hand holding the screen ----
  if (grab_hand >= 0)
  {
    const auto hand = static_cast<std::size_t>(grab_hand);
    m_swallowed[hand][GRIP] = true;
    m_stick_swallowed[hand] = true;
    for (const std::size_t button : {DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT})
      m_swallowed[LEFT][button] |= IsDown(left, button);
    for (State& state : *controllers)
      state.screen_hit = {};
    ClearStick(&(*controllers)[hand]);
  }

  Swallow(controllers, raw);
  return events;
}
}  // namespace VR::UI

#endif  // ENABLE_VR
