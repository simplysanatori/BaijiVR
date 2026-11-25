// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "VideoCommon/VR/FrameUI/PadArt.h"
#include "VideoCommon/VR/FrameUI/PadFilter.h"

// Edit Controls: what is on each control of the emulated controller, the one that waits for a press
// and what a press does. After Azahar for Frame's remap.*. No OpenXR and no config here (unit
// tested).
namespace VR::UI
{
// What can be given something else. The Wii Remote's pointer and motion are not in its mapping
// (CreateOpenXRInputOverrideFunction).
enum class PadTarget : int
{
  None = -1,
  A,
  B,
  X,
  Y,
  Z,
  L,
  R,
  Start,
  DPadUp,
  DPadDown,
  DPadLeft,
  DPadRight,
  Stick,
  CStick,
  One,
  Two,
  Plus,
  Minus,
  Home,
  Shake,
  C,
  NunchukZ,
  NunchukStick,
  ZL,
  ZR,
  LeftStick,
  RightStick,
  Count,
};

constexpr std::size_t NUM_PAD_TARGETS = static_cast<std::size_t>(PadTarget::Count);

// What can be pressed on the Frame's controllers. The grips and the stick clicks have jobs of their
// own.
enum class PadInput : int
{
  A,
  B,
  X,
  Y,
  Menu,
  View,
  LeftBumper,
  RightBumper,
  LeftTrigger,
  RightTrigger,
  DPadUp,
  DPadDown,
  DPadLeft,
  DPadRight,
  Count,
};

constexpr std::size_t NUM_PAD_INPUTS = static_cast<std::size_t>(PadInput::Count);
using PadInputs = std::bitset<NUM_PAD_INPUTS>;

// As in the mappings.
const char* GetInputName(PadInput input);
PadInputs GetInputs(const Controllers& controllers);
// "Left Stick Up": where a stick is pushed (left, right), or empty.
std::array<std::string, 2> GetPushes(const Controllers& controllers);

bool IsStick(PadTarget target);
const char* GetTargetName(PadTarget target);
PadControl GetTargetControl(PadTarget target);
const std::vector<PadTarget>& GetTargets(PadKind kind);

// What is on a control.
struct PadBinding
{
  // Inputs by name: any of them, or a stick's up, down, left and right.
  std::vector<std::string> names;
  // An expression that is more than that. It is kept until the control is given something else.
  std::string custom;

  bool operator==(const PadBinding&) const = default;
};

PadBinding ParseBinding(const std::string& expression);
std::string MakeExpression(const PadBinding& binding);
std::string DescribeBinding(PadTarget target, const PadBinding& binding);

using PadTable = std::array<PadBinding, NUM_PAD_TARGETS>;

// What the menu shows of it.
struct PadEditView
{
  PadTable table;
  PadTarget listening = PadTarget::None;
  int step = 0;       // of a stick: 0 up, 1 right
  int countdown = 0;  // 3, 2, 1 before it stops listening

  bool operator==(const PadEditView&) const = default;
};

// What to do, while a control waits for it: "Press a Button…", "Push the Stick Up…".
std::string GetPrompt(const PadEditView& view);
// The line above the drawing.
inline constexpr const char* PAD_EDIT_GUIDE = "Select a control, then press the button for it.";

class PadEdit
{
public:
  // It listens this long, the last three seconds of it with a countdown.
  static constexpr float QUIET_SECONDS = 3.0f;
  static constexpr float LISTEN_SECONDS = 6.0f;

  enum class Event
  {
    None,
    Next,       // a stick's up is known, it waits for right
    Changed,    // TakeChanges() has them
    Cancelled,  // everything is as it was
  };

  void Begin(PadKind kind, PadTable table);
  // It listens, or stops listening when it did already.
  void Choose(PadTarget target);
  Event Cancel();
  // One frame. `on_cancel`: the pointer is on Cancel.
  Event Update(const Controllers& controllers, float seconds, bool on_cancel);

  bool IsListening() const { return m_view.listening != PadTarget::None; }
  PadKind GetKind() const { return m_kind; }
  const PadEditView& GetView() const { return m_view; }
  std::vector<PadTarget> TakeChanges();

  Event Press(PadInput input);
  Event Push(const std::string& direction);

private:
  void Change(PadTarget target, PadBinding binding);
  Event Stop(Event event);

  PadKind m_kind = PadKind::GameCube;
  PadEditView m_view;
  std::string m_up;
  float m_seconds = 0.0f;
  // Down when it started: not an answer.
  bool m_started = false;
  PadInputs m_held;
  std::array<std::string, 2> m_pushed;
  std::vector<PadTarget> m_changes;
};

// Where the control's chip is.
struct PadChip
{
  PadTarget target = PadTarget::None;
  FrameXr::Menu::Rect rect;
  // The end of the line from the control.
  float x = 0.0f;
  float y = 0.0f;
};

constexpr float PAD_CHIP_H = 42.0f;

std::vector<PadChip> GetPadChips(PadKind kind, const FrameXr::Menu::Rect& area);
// Between the chips.
FrameXr::Menu::Rect GetPadEditArt(PadKind kind, const FrameXr::Menu::Rect& area);
}  // namespace VR::UI
