// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/FrameUI/PadBindings.h"

#include <algorithm>
#include <string_view>

#include <fmt/format.h>

#include "Common/IniFile.h"

#include "Common/StringUtil.h"

#include "Core/ConfigManager.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/GCPadEmu.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/Extension/Nunchuk.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "Core/System.h"
#include "Core/VRWiiController.h"

#include "InputCommon/ControlReference/ControlReference.h"
#include "InputCommon/ControllerEmu/Control/Control.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
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

constexpr std::string_view FRAME_DEVICE = "OpenXR/0/OpenXR Controller";
constexpr std::string_view CLICK = " Click";

InputConfig* GetConfig(PadKind kind)
{
  return kind == PadKind::GameCube ? Pad::GetConfig() : Wiimote::GetConfig();
}

// Where a control is in the mapping.
struct Place
{
  const ControllerEmu::ControlGroup* group = nullptr;
  std::vector<const char*> names;
  // The same for a trigger's way in.
  const char* analog = nullptr;
};

Place GetPlace(PadKind kind, PadTarget target)
{
  if (kind == PadKind::GameCube)
  {
    const auto button = [](PadGroup group, const char* name, const char* analog = nullptr) {
      return Place{Pad::GetGroup(0, group), {name}, analog};
    };
    const auto stick = [](PadGroup group) {
      return Place{Pad::GetGroup(0, group), {"Up", "Down", "Left", "Right"}};
    };
    switch (target)
    {
    case PadTarget::A:
    case PadTarget::B:
    case PadTarget::X:
    case PadTarget::Y:
    case PadTarget::Z:
    case PadTarget::Start:
      return button(PadGroup::Buttons, GetTargetName(target));
    case PadTarget::L:
      return button(PadGroup::Triggers, "L", "L-Analog");
    case PadTarget::R:
      return button(PadGroup::Triggers, "R", "R-Analog");
    case PadTarget::DPadUp:
      return button(PadGroup::DPad, "Up");
    case PadTarget::DPadDown:
      return button(PadGroup::DPad, "Down");
    case PadTarget::DPadLeft:
      return button(PadGroup::DPad, "Left");
    case PadTarget::DPadRight:
      return button(PadGroup::DPad, "Right");
    case PadTarget::Stick:
      return stick(PadGroup::MainStick);
    case PadTarget::CStick:
      return stick(PadGroup::CStick);
    default:
      return {};
    }
  }

  const auto button = [](const char* name) {
    return Place{Wiimote::GetWiimoteGroup(0, WiimoteEmu::WiimoteGroup::Buttons), {name}};
  };
  const auto nunchuk = [](const char* name) {
    return Place{Wiimote::GetNunchukGroup(0, WiimoteEmu::NunchukGroup::Buttons), {name}};
  };
  switch (target)
  {
  case PadTarget::A:
    return button("A");
  case PadTarget::B:
    return button("B");
  case PadTarget::One:
    return button("1");
  case PadTarget::Two:
    return button("2");
  case PadTarget::Plus:
    return button("+");
  case PadTarget::Minus:
    return button("-");
  case PadTarget::Home:
    return button("Home");
  case PadTarget::Shake:
    return {Wiimote::GetWiimoteGroup(0, WiimoteEmu::WiimoteGroup::Shake), {"X", "Y", "Z"}};
  case PadTarget::C:
    return nunchuk("C");
  case PadTarget::NunchukZ:
    return nunchuk("Z");
  case PadTarget::NunchukStick:
    return {Wiimote::GetNunchukGroup(0, WiimoteEmu::NunchukGroup::Stick),
            {"Up", "Down", "Left", "Right"}};
  default:
    return {};
  }
}

// A trigger is one thing to press, however far it has to be pulled.
PadBinding WithoutClicks(PadBinding binding)
{
  for (std::string& name : binding.names)
  {
    if (name.ends_with(" Trigger Click"))
      name.resize(name.size() - CLICK.size());
  }
  return binding;
}

PadBinding WithClicks(PadBinding binding)
{
  for (std::string& name : binding.names)
  {
    if (name.ends_with(" Trigger"))
      name += CLICK;
  }
  return binding;
}

void SetExpression(const ControllerEmu::ControlGroup* group, std::string_view control,
                   const std::string& expression)
{
  const auto it = std::ranges::find(group->controls, control, &ControllerEmu::Control::name);
  if (it != group->controls.end())
    (*it)->control_ref->SetExpression(expression);
}

// Not under the state lock.
void ApplyAndSave(PadKind kind)
{
  InputConfig* const config = GetConfig(kind);
  config->GetController(0)->UpdateReferences(g_controller_interface);
  if (kind == PadKind::GameCube)
    config->SaveConfig();
  else
    VRWiiController::SaveMapping();
  config->GenerateControllerTextures();
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
  if (!wii || VRWiiController::GetCurrent(Core::System::GetInstance()) ==
                  VRWiiController::Choice::GameCube)
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

std::string GetPadEditLock(PadKind kind)
{
  if (GetConfig(kind)->GetController(0)->GetDefaultDevice().ToString() != FRAME_DEVICE)
    return "Only for the Frame controllers.";
  // Saving would put the game's profile into the mapping of every game.
  Common::IniFile game_ini = SConfig::GetInstance().LoadGameIni();
  const std::string profile = fmt::format("{}Profile1", GetConfig(kind)->GetProfileKey());
  if (game_ini.GetOrCreateSection("Controls")->Exists(profile))
    return "This game has a controller profile of its own.";
  return {};
}

PadTable GetPadTable(PadKind kind)
{
  PadTable table;
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  for (const PadTarget target : GetTargets(kind))
  {
    const Place place = GetPlace(kind, target);
    PadBinding& binding = table[static_cast<std::size_t>(target)];
    if (!IsStick(target))
    {
      if (place.group)
        binding = WithoutClicks(ParseBinding(GetExpression(place.group, place.names.front())));
      continue;
    }
    std::string all;
    for (const char* const name : place.names)
    {
      const std::string expression = GetExpression(place.group, name);
      PadBinding direction = ParseBinding(expression);
      if (direction.names.size() == 1)
        binding.names.push_back(std::move(direction.names.front()));
      all += expression;
    }
    if (binding.names.size() != place.names.size())
      binding = {{}, all};
  }
  return table;
}

void SetPadBindings(PadKind kind, const std::vector<std::pair<PadTarget, PadBinding>>& bindings)
{
  {
    const auto lock = ControllerEmu::EmulatedController::GetStateLock();
    for (const auto& [target, binding] : bindings)
    {
      const Place place = GetPlace(kind, target);
      if (!place.group)
        continue;
      if (IsStick(target))
      {
        // One that is more than that is as it was.
        for (std::size_t i = 0; i < place.names.size() && i < binding.names.size(); ++i)
          SetExpression(place.group, place.names[i], MakeExpression({{binding.names[i]}, {}}));
        continue;
      }
      const std::string expression = MakeExpression(binding);
      for (const char* const name : place.names)
        SetExpression(place.group, name, place.analog ? MakeExpression(WithClicks(binding)) : expression);
      if (place.analog)
        SetExpression(place.group, place.analog, expression);
    }
  }
  ApplyAndSave(kind);
}

void ResetPadBindings(PadKind kind)
{
  InputConfig* const config = GetConfig(kind);
  if (kind != PadKind::GameCube)
  {
    VRWiiController::ResetMapping();
    config->GenerateControllerTextures();
    return;
  }
  Common::IniFile ini;
  if (!ini.Load(config->GetSysProfileDirectoryPath() + "Steam Frame GameCube.ini"))
    return;
  config->GetController(0)->LoadConfig(ini.GetOrCreateSection("Profile"));
  ApplyAndSave(kind);
}
}  // namespace VR::UI
