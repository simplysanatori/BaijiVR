// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoCommon/VR/FrameUI/PadEdit.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Common/Contains.h"
#include "Common/StringUtil.h"

namespace VR::UI
{
using FrameXr::Menu::Rect;

namespace
{
// Pushed, and back (to be pushed again).
constexpr float STICK_ON = 0.7f;
constexpr float STICK_OFF = 0.4f;

constexpr std::array<const char*, NUM_PAD_INPUTS> INPUT_NAMES{
    "A",          "B",           "X",            "Y",
    "Menu",       "View",        "Left Bumper",  "Right Bumper",
    "Left Trigger", "Right Trigger", "D-Pad Up",   "D-Pad Down",
    "D-Pad Left", "D-Pad Right"};

struct TargetInfo
{
  const char* name;
  PadControl control;
};

constexpr std::array<TargetInfo, NUM_PAD_TARGETS> TARGETS{{
    {"A", PadControl::A},
    {"B", PadControl::B},
    {"X", PadControl::X},
    {"Y", PadControl::Y},
    {"Z", PadControl::Z},
    {"L", PadControl::L},
    {"R", PadControl::R},
    {"Start", PadControl::Start},
    {"D-Pad Up", PadControl::DPad},
    {"D-Pad Down", PadControl::DPad},
    {"D-Pad Left", PadControl::DPad},
    {"D-Pad Right", PadControl::DPad},
    {"Control Stick", PadControl::Stick},
    {"C Stick", PadControl::CStick},
    {"1", PadControl::One},
    {"2", PadControl::Two},
    {"Plus", PadControl::Plus},
    {"Minus", PadControl::Minus},
    {"Home", PadControl::Home},
    {"Shake", PadControl::Shake},
    {"C", PadControl::C},
    {"Z", PadControl::NunchukZ},
    {"Nunchuk Stick", PadControl::NunchukStick},
    {"ZL", PadControl::ZL},
    {"ZR", PadControl::ZR},
    {"Left Stick", PadControl::LeftStick},
    {"Right Stick", PadControl::RightStick},
}};

// The chips on the left (above) and on the right (below), as their controls are on the drawing.
// `over`: above the ones above.
struct Sides
{
  std::vector<PadTarget> first;
  std::vector<PadTarget> second;
  std::vector<PadTarget> over{};
};

Sides GetSides(PadKind kind)
{
  using enum PadTarget;
  switch (kind)
  {
  case PadKind::GameCube:
    return {{L, Start, Stick, DPadUp, DPadLeft, DPadRight, DPadDown}, {R, Z, Y, X, A, B, CStick}};
  case PadKind::WiiRemote:
    return {{DPadUp, DPadLeft, DPadRight, DPadDown, A, Minus, One}, {B, Home, Plus, Two, Shake}};
  case PadKind::WiiNunchuk:
    return {{NunchukStick, C, NunchukZ, DPadUp, DPadLeft, DPadRight, DPadDown},
            {B, A, Home, Plus, Minus, One, Two, Shake}};
  case PadKind::WiiRemoteSideways:
    return {{A, Plus, One},
            {B, Home, Minus, Two, Shake},
            {DPadUp, DPadLeft, DPadRight, DPadDown}};
  case PadKind::WiiClassic:
    return {{ZL, L, Minus, DPadUp, DPadLeft, DPadRight, DPadDown, LeftStick},
            {ZR, R, Home, Plus, X, Y, A, B, RightStick}};
  }
  return {};
}

constexpr float CHIP_W = 160.0f;
constexpr float ROW_CHIP_W = 128.0f;
constexpr float CHIP_GAP = 8.0f;
constexpr float ART_GAP = 20.0f;

// "Left Stick Up" is of "Left Stick".
std::string_view GetStick(std::string_view direction)
{
  return direction.substr(0, direction.find(" Stick ") + (sizeof(" Stick") - 1));
}

bool IsVertical(std::string_view direction)
{
  return direction.ends_with(" Up") || direction.ends_with(" Down");
}

std::string GetOpposite(const std::string& direction)
{
  static constexpr std::array<std::pair<const char*, const char*>, 4> OPPOSITES{
      {{" Up", " Down"}, {" Down", " Up"}, {" Left", " Right"}, {" Right", " Left"}}};
  for (const auto& [end, other] : OPPOSITES)
  {
    if (direction.ends_with(end))
      return direction.substr(0, direction.size() - std::string_view(end).size()) + other;
  }
  return direction;
}
}  // namespace

const char* GetInputName(PadInput input)
{
  return INPUT_NAMES[static_cast<std::size_t>(input)];
}

PadInputs GetInputs(const Controllers& controllers)
{
  const auto& left = controllers[0];
  const auto& right = controllers[1];
  PadInputs inputs;
  const auto set = [&inputs](PadInput input, bool down) {
    inputs.set(static_cast<std::size_t>(input), down);
  };
  set(PadInput::A, right.primary_button);
  set(PadInput::B, right.secondary_button);
  set(PadInput::X, right.frame_x_button);
  set(PadInput::Y, right.frame_y_button);
  set(PadInput::Menu, right.menu_button);
  set(PadInput::View, left.view_button);
  set(PadInput::LeftBumper, left.bumper_button);
  set(PadInput::RightBumper, right.bumper_button);
  set(PadInput::LeftTrigger, left.trigger_value > PadFilter::TRIGGER_ON);
  set(PadInput::RightTrigger, right.trigger_value > PadFilter::TRIGGER_ON);
  set(PadInput::DPadUp, left.dpad_up);
  set(PadInput::DPadDown, left.dpad_down);
  set(PadInput::DPadLeft, left.dpad_left);
  set(PadInput::DPadRight, left.dpad_right);
  return inputs;
}

std::array<std::string, 2> GetPushes(const Controllers& controllers)
{
  std::array<std::string, 2> pushes;
  for (std::size_t hand = 0; hand < 2; ++hand)
  {
    const float x = controllers[hand].thumbstick_x;
    const float y = controllers[hand].thumbstick_y;
    // Clearly along one axis.
    const bool vertical = std::abs(y) >= STICK_ON && std::abs(y) > std::abs(x) * 2.0f;
    const bool horizontal = std::abs(x) >= STICK_ON && std::abs(x) > std::abs(y) * 2.0f;
    if (!vertical && !horizontal)
      continue;
    pushes[hand] = hand == 0 ? "Left Stick " : "Right Stick ";
    pushes[hand] += vertical ? (y > 0.0f ? "Up" : "Down") : (x > 0.0f ? "Right" : "Left");
  }
  return pushes;
}

bool IsStick(PadTarget target)
{
  return target == PadTarget::Stick || target == PadTarget::CStick ||
         target == PadTarget::NunchukStick || target == PadTarget::LeftStick ||
         target == PadTarget::RightStick;
}

const char* GetTargetName(PadTarget target)
{
  return TARGETS[static_cast<std::size_t>(target)].name;
}

PadControl GetTargetControl(PadTarget target)
{
  return TARGETS[static_cast<std::size_t>(target)].control;
}

const std::vector<PadTarget>& GetTargets(PadKind kind)
{
  static const std::array<std::vector<PadTarget>, NUM_PAD_KINDS> targets = [] {
    std::array<std::vector<PadTarget>, NUM_PAD_KINDS> all;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
      const Sides sides = GetSides(static_cast<PadKind>(i));
      all[i] = sides.first;
      all[i].insert(all[i].end(), sides.second.begin(), sides.second.end());
      all[i].insert(all[i].end(), sides.over.begin(), sides.over.end());
    }
    return all;
  }();
  return targets[static_cast<std::size_t>(kind)];
}

PadBinding ParseBinding(const std::string& expression)
{
  PadBinding binding;
  const std::string trimmed(StripWhitespace(expression));
  if (trimmed.empty())
    return binding;
  for (const std::string& part : SplitString(trimmed, '|'))
  {
    const std::string_view name = StripWhitespace(part);
    if (name.size() < 3 || name.front() != '`' || name.back() != '`' ||
        name.substr(1, name.size() - 2).contains('`'))
    {
      return {{}, trimmed};
    }
    binding.names.emplace_back(name.substr(1, name.size() - 2));
  }
  return binding;
}

std::string MakeExpression(const PadBinding& binding)
{
  if (!binding.custom.empty())
    return binding.custom;
  std::string expression;
  for (const std::string& name : binding.names)
  {
    if (!expression.empty())
      expression += " | ";
    expression += '`' + name + '`';
  }
  return expression;
}

std::string DescribeBinding(PadTarget target, const PadBinding& binding)
{
  if (!binding.custom.empty())
    return "Custom";
  if (binding.names.empty())
    return "None";
  if (IsStick(target))
  {
    const std::string_view stick = GetStick(binding.names.front());
    const bool one = std::ranges::all_of(
        binding.names, [stick](const std::string& name) { return GetStick(name) == stick; });
    return one && binding.names.size() == 4 ? std::string(stick) : "Custom";
  }
  std::string text;
  for (const std::string& name : binding.names)
  {
    if (!text.empty())
      text += " or ";
    text += name;
  }
  return text;
}

std::string GetPrompt(const PadEditView& view)
{
  if (view.listening == PadTarget::None)
    return {};
  std::string text = !IsStick(view.listening) ? "Press a Button…" :
                     view.step == 0           ? "Push the Stick Up…" :
                                                "Push the Stick Right…";
  if (view.countdown > 0)
    text += " " + std::to_string(view.countdown);
  return text;
}

void PadEdit::Begin(PadKind kind, PadTable table)
{
  m_kind = kind;
  m_view = {};
  m_view.table = std::move(table);
  m_changes.clear();
}

void PadEdit::Choose(PadTarget target)
{
  const PadTarget before = m_view.listening;
  Stop(Event::None);
  if (target == before || !Common::Contains(GetTargets(m_kind), target))
    return;
  m_view.listening = target;
}

PadEdit::Event PadEdit::Stop(Event event)
{
  m_view.listening = PadTarget::None;
  m_view.step = 0;
  m_view.countdown = 0;
  m_up.clear();
  m_seconds = 0.0f;
  m_started = false;
  return event;
}

PadEdit::Event PadEdit::Cancel()
{
  return IsListening() ? Stop(Event::Cancelled) : Event::None;
}

PadEdit::Event PadEdit::Update(const Controllers& controllers, float seconds, bool on_cancel)
{
  if (!IsListening())
    return Event::None;

  const PadInputs inputs = GetInputs(controllers);
  std::array<std::string, 2> pushes = GetPushes(controllers);
  for (std::size_t hand = 0; hand < 2; ++hand)
  {
    // It has to come back before it is pushed again.
    const auto& c = controllers[hand];
    if (pushes[hand].empty() && std::hypot(c.thumbstick_x, c.thumbstick_y) > STICK_OFF)
      pushes[hand] = m_pushed[hand];
  }
  if (!std::exchange(m_started, true))
  {
    m_held = inputs;
    m_pushed = pushes;
    return Event::None;
  }

  const PadInputs pressed = inputs & ~m_held;
  m_held = inputs;
  const std::array<std::string, 2> pushed_before = std::exchange(m_pushed, pushes);
  const auto is_pressed = [&pressed](PadInput input) {
    return pressed[static_cast<std::size_t>(input)];
  };
  if (on_cancel && (is_pressed(PadInput::LeftTrigger) || is_pressed(PadInput::RightTrigger) ||
                    is_pressed(PadInput::A)))
  {
    return Cancel();
  }
  for (std::size_t i = 0; i < NUM_PAD_INPUTS; ++i)
  {
    if (!pressed[i])
      continue;
    if (const Event event = Press(static_cast<PadInput>(i)); event != Event::None)
      return event;
  }
  for (std::size_t hand = 0; hand < 2; ++hand)
  {
    if (pushes[hand].empty() || pushes[hand] == pushed_before[hand])
      continue;
    if (const Event event = Push(pushes[hand]); event != Event::None)
      return event;
  }

  m_seconds += seconds;
  if (m_seconds >= LISTEN_SECONDS)
    return Cancel();
  m_view.countdown =
      m_seconds >= QUIET_SECONDS ? static_cast<int>(std::ceil(LISTEN_SECONDS - m_seconds)) : 0;
  return Event::None;
}

std::vector<PadTarget> PadEdit::TakeChanges()
{
  return std::exchange(m_changes, {});
}

void PadEdit::Change(PadTarget target, PadBinding binding)
{
  m_view.table[static_cast<std::size_t>(target)] = std::move(binding);
  m_changes.push_back(target);
}

PadEdit::Event PadEdit::Press(PadInput input)
{
  if (!IsListening() || IsStick(m_view.listening))
    return Event::None;

  const PadTarget target = m_view.listening;
  const std::string name = GetInputName(input);
  const PadBinding chosen{{name}, {}};
  const PadBinding old = m_view.table[static_cast<std::size_t>(target)];
  if (old == chosen)
    return Stop(Event::Cancelled);

  // Who had it takes what this one had.
  for (const PadTarget other : GetTargets(m_kind))
  {
    if (other == target || IsStick(other))
      continue;
    PadBinding binding = m_view.table[static_cast<std::size_t>(other)];
    const auto it = std::ranges::find(binding.names, name);
    if (it == binding.names.end())
      continue;
    if (binding.names.size() == 1)
    {
      binding = old;
    }
    else if (old.names.size() == 1 && !Common::Contains(binding.names, old.names.front()))
    {
      *it = old.names.front();
    }
    else
    {
      binding.names.erase(it);
    }
    Change(other, std::move(binding));
  }
  Change(target, chosen);
  return Stop(Event::Changed);
}

PadEdit::Event PadEdit::Push(const std::string& direction)
{
  if (!IsListening() || !IsStick(m_view.listening))
    return Event::None;
  // With the Nunchuk, the right stick is the Wii Remote's D-Pad.
  if (m_kind == PadKind::WiiNunchuk && !direction.starts_with("Left"))
    return Event::None;
  if (m_view.step == 0)
  {
    m_up = direction;
    m_view.step = 1;
    m_view.countdown = 0;
    m_seconds = 0.0f;
    return Event::Next;
  }
  if (GetStick(direction) != GetStick(m_up) || IsVertical(direction) == IsVertical(m_up))
    return Event::None;

  const PadTarget target = m_view.listening;
  const PadBinding chosen{{m_up, GetOpposite(m_up), GetOpposite(direction), direction}, {}};
  const PadBinding old = m_view.table[static_cast<std::size_t>(target)];
  if (old == chosen)
    return Stop(Event::Cancelled);
  for (const PadTarget other : GetTargets(m_kind))
  {
    if (other == target || !IsStick(other))
      continue;
    const PadBinding& binding = m_view.table[static_cast<std::size_t>(other)];
    if (!binding.names.empty() && GetStick(binding.names.front()) == GetStick(m_up))
      Change(other, old);
  }
  Change(target, chosen);
  return Stop(Event::Changed);
}

std::vector<PadChip> GetPadChips(PadKind kind, const Rect& area)
{
  const Sides sides = GetSides(kind);
  std::vector<PadChip> chips;
  if (kind == PadKind::WiiRemoteSideways)
  {
    const auto add = [&](const std::vector<PadTarget>& targets, float y, bool above) {
      const auto count = static_cast<float>(targets.size());
      float x = area.Cx() - (count * ROW_CHIP_W + (count - 1.0f) * CHIP_GAP) * 0.5f;
      for (const PadTarget target : targets)
      {
        const Rect rect{x, y, x + ROW_CHIP_W, y + PAD_CHIP_H};
        chips.push_back({target, rect, rect.Cx(), above ? rect.y1 : rect.y0});
        x += ROW_CHIP_W + CHIP_GAP;
      }
    };
    add(sides.over, area.y0, true);
    add(sides.first, area.y0 + PAD_CHIP_H + CHIP_GAP, true);
    add(sides.second, area.y1 - PAD_CHIP_H, false);
    return chips;
  }
  for (const bool left : {true, false})
  {
    const std::vector<PadTarget>& targets = left ? sides.first : sides.second;
    const auto count = static_cast<float>(targets.size());
    const float pitch = std::min(PAD_CHIP_H + CHIP_GAP, area.H() / count);
    const float x = left ? area.x0 : area.x1 - CHIP_W;
    float y = area.Cy() - (count * pitch - (pitch - PAD_CHIP_H)) * 0.5f;
    for (const PadTarget target : targets)
    {
      const Rect rect{x, y, x + CHIP_W, y + PAD_CHIP_H};
      chips.push_back({target, rect, left ? rect.x1 : rect.x0, rect.Cy()});
      y += pitch;
    }
  }
  return chips;
}

Rect GetPadEditArt(PadKind kind, const Rect& area)
{
  if (kind == PadKind::WiiRemoteSideways)
  {
    const float inset = PAD_CHIP_H + ART_GAP * 2.0f;
    return {area.x0, area.y0 + PAD_CHIP_H + CHIP_GAP + inset, area.x1, area.y1 - inset};
  }
  return {area.x0 + CHIP_W + ART_GAP, area.y0, area.x1 - CHIP_W - ART_GAP, area.y1};
}
}  // namespace VR::UI
