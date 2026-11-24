// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/FrameUI/PadBindings.h"

#include <algorithm>
#include <string_view>

#include "Common/StringUtil.h"

#include "Core/HW/GCPad.h"
#include "Core/HW/GCPadEmu.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/Extension/Nunchuk.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"

#include "InputCommon/ControlReference/ControlReference.h"
#include "InputCommon/ControllerEmu/Control/Control.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/InputConfig.h"

namespace VR::UI
{
namespace
{
std::string GetExpression(const ControllerEmu::ControlGroup* group, std::string_view control)
{
  if (!group)
    return {};
  const auto it = std::ranges::find(group->controls, control, &ControllerEmu::Control::name);
  return it == group->controls.end() ? std::string{} : (*it)->control_ref->GetExpression();
}

class Labels
{
public:
  PadLabels Take() { return std::move(m_labels); }

  void Button(PadControl control, const ControllerEmu::ControlGroup* group, std::string_view name)
  {
    Set(control, DescribeExpression(GetExpression(group, name)));
  }
  void Direction(PadControl control, const ControllerEmu::ControlGroup* group)
  {
    Set(control, DescribeUp(GetExpression(group, "Up")));
  }
  void Set(PadControl control, std::string text)
  {
    m_labels.text[static_cast<std::size_t>(control)] = std::move(text);
  }

private:
  PadLabels m_labels;
};
}  // namespace

std::string DescribeExpression(std::string expression)
{
  std::erase(expression, '`');
  std::string text = ReplaceAll(std::string(StripWhitespace(expression)), "|", " or ");
  while (text.contains("  "))
    text = ReplaceAll(text, "  ", " ");
  return text;
}

std::string DescribeUp(std::string expression)
{
  std::string text = DescribeExpression(std::move(expression));
  if (text.ends_with(" Up"))
    text.resize(text.size() - 3);
  return text;
}

std::pair<PadKind, PadLabels> GetPadBindings(bool wii)
{
  Labels labels;
  if (!wii)
  {
    const auto lock = ControllerEmu::EmulatedController::GetStateLock();
    const auto* const buttons = Pad::GetGroup(0, PadGroup::Buttons);
    labels.Button(PadControl::A, buttons, "A");
    labels.Button(PadControl::B, buttons, "B");
    labels.Button(PadControl::X, buttons, "X");
    labels.Button(PadControl::Y, buttons, "Y");
    labels.Button(PadControl::Z, buttons, "Z");
    labels.Button(PadControl::Start, buttons, "Start");
    const auto* const triggers = Pad::GetGroup(0, PadGroup::Triggers);
    labels.Button(PadControl::L, triggers, "L");
    labels.Button(PadControl::R, triggers, "R");
    labels.Direction(PadControl::Stick, Pad::GetGroup(0, PadGroup::MainStick));
    labels.Direction(PadControl::CStick, Pad::GetGroup(0, PadGroup::CStick));
    labels.Direction(PadControl::DPad, Pad::GetGroup(0, PadGroup::DPad));
    return {PadKind::GameCube, labels.Take()};
  }

  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  auto* const wiimote = static_cast<WiimoteEmu::Wiimote*>(Wiimote::GetConfig()->GetController(0));
  const bool nunchuk =
      wiimote->GetActiveExtensionNumber() == WiimoteEmu::ExtensionNumber::NUNCHUK;
  const PadKind kind = nunchuk                ? PadKind::WiiNunchuk :
                       wiimote->IsSideways() ? PadKind::WiiRemoteSideways :
                                               PadKind::WiiRemote;

  const auto* const buttons = wiimote->GetWiimoteGroup(WiimoteEmu::WiimoteGroup::Buttons);
  labels.Button(PadControl::A, buttons, "A");
  labels.Button(PadControl::B, buttons, "B");
  labels.Button(PadControl::One, buttons, "1");
  labels.Button(PadControl::Two, buttons, "2");
  labels.Button(PadControl::Plus, buttons, "+");
  labels.Button(PadControl::Minus, buttons, "-");
  const std::string home = DescribeExpression(GetExpression(buttons, "Home"));
  labels.Set(PadControl::Home, home.empty() ? "VR Menu" : home);
  // The sticks are its D-Pad, too (CreateOpenXRInputOverrideFunction).
  labels.Set(PadControl::DPad, nunchuk ? "Right Stick or D-Pad" : "D-Pad or Left Stick");
  labels.Button(PadControl::Shake, wiimote->GetWiimoteGroup(WiimoteEmu::WiimoteGroup::Shake), "X");
  if (kind != PadKind::WiiRemoteSideways)
    labels.Set(PadControl::Pointer, "Right Controller");
  if (nunchuk)
  {
    const auto* const nunchuk_buttons =
        wiimote->GetNunchukGroup(WiimoteEmu::NunchukGroup::Buttons);
    labels.Button(PadControl::C, nunchuk_buttons, "C");
    labels.Button(PadControl::NunchukZ, nunchuk_buttons, "Z");
    labels.Direction(PadControl::NunchukStick,
                     wiimote->GetNunchukGroup(WiimoteEmu::NunchukGroup::Stick));
  }
  return {kind, labels.Take()};
}
}  // namespace VR::UI
