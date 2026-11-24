// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerInterface/OpenXR/FrameControllerNames.h"

#include <utility>

#include "Common/StringUtil.h"

namespace ciface::OpenXR
{
namespace
{
constexpr std::pair<std::string_view, std::string_view> NAMES[] = {
    {"Right Button A", "A"},
    {"Right Button B", "B"},
    {"Right Button X", "X"},
    {"Right Button Y", "Y"},
    // The Frame has X and Y on the right controller.
    {"Left Button X", "X"},
    {"Left Button Y", "Y"},
    {"Right Button Menu", "Menu"},
    {"Left Button Menu", "View"},
    {"Left Button View", "View"},
    {"Left Button System", "Left System"},
    {"Right Button System", "Right System"},
    {"Left Button Bumper", "Left Bumper"},
    {"Right Button Bumper", "Right Bumper"},
    {"Left Button Trigger", "Left Trigger Click"},
    {"Right Button Trigger", "Right Trigger Click"},
    {"Left Button Squeeze", "Left Grip"},
    {"Right Button Squeeze", "Right Grip"},
    {"Left Squeeze", "Left Grip Pressure"},
    {"Right Squeeze", "Right Grip Pressure"},
    {"Left Button Thumbstick", "Left Stick Click"},
    {"Right Button Thumbstick", "Right Stick Click"},
    {"Left D-Pad Up", "D-Pad Up"},
    {"Left D-Pad Down", "D-Pad Down"},
    {"Left D-Pad Left", "D-Pad Left"},
    {"Left D-Pad Right", "D-Pad Right"},
    {"Left Thumbstick X-", "Left Stick Left"},
    {"Left Thumbstick X+", "Left Stick Right"},
    {"Left Thumbstick Y-", "Left Stick Down"},
    {"Left Thumbstick Y+", "Left Stick Up"},
    {"Right Thumbstick X-", "Right Stick Left"},
    {"Right Thumbstick X+", "Right Stick Right"},
    {"Right Thumbstick Y-", "Right Stick Down"},
    {"Right Thumbstick Y+", "Right Stick Up"},
};

struct Moved
{
  std::string_view control;
  std::string_view before;
  std::string_view after;
};

// Old defaults that are somewhere else now. They match whole expressions only, so nothing anyone
// chose themselves is touched.
constexpr Moved MOVED[] = {
    {"Buttons/Z", "`Right Button Squeeze`", "`Right Bumper`"},
    {"Shake/X", "`Right Button Squeeze`", "`Right Bumper`"},
    {"Shake/Y", "`Right Button Squeeze`", "`Right Bumper`"},
    {"Shake/Z", "`Right Button Squeeze`", "`Right Bumper`"},
    {"Nunchuk/Buttons/C", "`Left Button Squeeze`", "`Left Bumper`"},
    {"Buttons/-", "`Left Button Bumper`", "`View`"},
    {"Buttons/+", "`Right Button Bumper`", "`Menu`"},
    {"Buttons/Home", "`Left Button System`", ""},
    {"General/Stop", "`Left Button View`", "hold(`View`, 1.5)"},
    {"General/Stop", "`Left Button Menu`", "hold(`View`, 1.5)"},
};
}  // namespace

std::string_view GetFrameInputName(std::string_view name)
{
  for (const auto& [before, after] : NAMES)
  {
    if (name == before)
      return after;
  }
  return name;
}

void UpdateFrameExpression(std::string_view control, std::string* expression)
{
  if (expression->empty())
    return;

  for (const Moved& moved : MOVED)
  {
    if (control == moved.control && *expression == moved.before)
    {
      *expression = moved.after;
      return;
    }
  }

  for (const auto& [before, after] : NAMES)
  {
    if (expression->find(before) == std::string::npos)
      continue;

    *expression = ReplaceAll(std::move(*expression), "`" + std::string(before) + "`",
                             "`" + std::string(after) + "`");
    // "`OpenXR/0/OpenXR Controller:Right Button A`"
    *expression =
        ReplaceAll(std::move(*expression), "OpenXR Controller:" + std::string(before) + "`",
                   "OpenXR Controller:" + std::string(after) + "`");
  }
}
}  // namespace ciface::OpenXR
