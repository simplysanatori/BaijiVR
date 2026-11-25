// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/FrameUI/PadBindings.h"

#include <algorithm>
#include <string_view>

#include <fmt/format.h>

#include "Common/Config/Config.h"
#include "Common/IniFile.h"
#include "Common/StringUtil.h"

#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/GCPadEmu.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/Extension/Classic.h"
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

  if (kind == PadKind::WiiClassic)
  {
    using WiimoteEmu::Classic;
    using WiimoteEmu::ClassicGroup;
    const auto button = [](const char* name) {
      return Place{Wiimote::GetClassicGroup(0, ClassicGroup::Buttons), {name}};
    };
    const auto trigger = [](const char* name, const char* analog) {
      return Place{Wiimote::GetClassicGroup(0, ClassicGroup::Triggers), {name}, analog};
    };
    const auto dpad = [](const char* name) {
      return Place{Wiimote::GetClassicGroup(0, ClassicGroup::DPad), {name}};
    };
    const auto stick = [](ClassicGroup group) {
      return Place{Wiimote::GetClassicGroup(0, group), {"Up", "Down", "Left", "Right"}};
    };
    switch (target)
    {
    case PadTarget::A:
      return button(Classic::A_BUTTON);
    case PadTarget::B:
      return button(Classic::B_BUTTON);
    case PadTarget::X:
      return button(Classic::X_BUTTON);
    case PadTarget::Y:
      return button(Classic::Y_BUTTON);
    case PadTarget::ZL:
      return button(Classic::ZL_BUTTON);
    case PadTarget::ZR:
      return button(Classic::ZR_BUTTON);
    case PadTarget::Plus:
      return button(Classic::PLUS_BUTTON);
    case PadTarget::Minus:
      return button(Classic::MINUS_BUTTON);
    case PadTarget::Home:
      return button(Classic::HOME_BUTTON);
    case PadTarget::L:
      return trigger(Classic::L_DIGITAL, Classic::L_ANALOG);
    case PadTarget::R:
      return trigger(Classic::R_DIGITAL, Classic::R_ANALOG);
    case PadTarget::DPadUp:
      return dpad("Up");
    case PadTarget::DPadDown:
      return dpad("Down");
    case PadTarget::DPadLeft:
      return dpad("Left");
    case PadTarget::DPadRight:
      return dpad("Right");
    case PadTarget::LeftStick:
      return stick(ClassicGroup::LeftStick);
    case PadTarget::RightStick:
      return stick(ClassicGroup::RightStick);
    default:
      return {};
    }
  }

  const auto button = [](const char* name) {
    return Place{Wiimote::GetWiimoteGroup(0, WiimoteEmu::WiimoteGroup::Buttons), {name}};
  };
  const auto dpad = [](const char* name) {
    return Place{Wiimote::GetWiimoteGroup(0, WiimoteEmu::WiimoteGroup::DPad), {name}};
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
  case PadTarget::DPadUp:
    return dpad("Up");
  case PadTarget::DPadDown:
    return dpad("Down");
  case PadTarget::DPadLeft:
    return dpad("Left");
  case PadTarget::DPadRight:
    return dpad("Right");
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
  config->SaveConfig();
  config->OnMappingChanged();
  config->GenerateControllerTextures();
}

// What is on a trigger, if its way in and its click are the same thing.
PadBinding GetTrigger(const Place& place)
{
  const std::string click = GetExpression(place.group, place.names.front());
  const std::string analog = GetExpression(place.group, place.analog);
  PadBinding binding = WithoutClicks(ParseBinding(click));
  if (binding.custom.empty() && binding != WithoutClicks(ParseBinding(analog)))
    binding = {{}, click.empty() ? analog : click};
  return binding;
}

PadKind ToKind(VRWiiController::Choice choice)
{
  switch (choice)
  {
  case VRWiiController::Choice::GameCube:
    return PadKind::GameCube;
  case VRWiiController::Choice::RemoteNunchuk:
    return PadKind::WiiNunchuk;
  case VRWiiController::Choice::RemoteSideways:
    return PadKind::WiiRemoteSideways;
  case VRWiiController::Choice::Classic:
    return PadKind::WiiClassic;
  case VRWiiController::Choice::None:
  case VRWiiController::Choice::Remote:
    break;
  }
  return PadKind::WiiRemote;
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
  PadBinding binding = ParseBinding(expression);
  if (!binding.custom.empty())
    return DescribeExpression(std::move(expression));
  std::string text;
  for (std::string& name : binding.names)
  {
    if (name.ends_with(" Up"))
      name.resize(name.size() - 3);
    if (!text.empty())
      text += " or ";
    text += name;
  }
  return text;
}

std::pair<PadKind, PadLabels> GetPadBindings(bool wii)
{
  const auto choice = VRWiiController::GetCurrent(Core::System::GetInstance());
  const PadKind kind = wii ? ToKind(choice) : PadKind::GameCube;
  Labels labels;
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  if (kind == PadKind::GameCube)
  {
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
    return {kind, labels.Take()};
  }

  auto* const wiimote = static_cast<WiimoteEmu::Wiimote*>(Wiimote::GetConfig()->GetController(0));
  const auto* const buttons = wiimote->GetWiimoteGroup(WiimoteEmu::WiimoteGroup::Buttons);
  // The VR menu has the Wii Remote's, which every controller on it can do without its own.
  std::string home = DescribeExpression(GetExpression(buttons, "Home"));
  if (kind == PadKind::WiiClassic)
  {
    using WiimoteEmu::Classic;
    using WiimoteEmu::ClassicGroup;
    const auto* const classic = wiimote->GetClassicGroup(ClassicGroup::Buttons);
    labels.Button(PadControl::A, classic, Classic::A_BUTTON);
    labels.Button(PadControl::B, classic, Classic::B_BUTTON);
    labels.Button(PadControl::X, classic, Classic::X_BUTTON);
    labels.Button(PadControl::Y, classic, Classic::Y_BUTTON);
    labels.Button(PadControl::ZL, classic, Classic::ZL_BUTTON);
    labels.Button(PadControl::ZR, classic, Classic::ZR_BUTTON);
    labels.Button(PadControl::Plus, classic, Classic::PLUS_BUTTON);
    labels.Button(PadControl::Minus, classic, Classic::MINUS_BUTTON);
    home = DescribeExpression(GetExpression(classic, Classic::HOME_BUTTON));
    labels.Set(PadControl::Home, home.empty() ? "VR Menu" : home);
    const auto* const triggers = wiimote->GetClassicGroup(ClassicGroup::Triggers);
    labels.Button(PadControl::L, triggers, Classic::L_DIGITAL);
    labels.Button(PadControl::R, triggers, Classic::R_DIGITAL);
    labels.Direction(PadControl::DPad, wiimote->GetClassicGroup(ClassicGroup::DPad));
    labels.Direction(PadControl::LeftStick, wiimote->GetClassicGroup(ClassicGroup::LeftStick));
    labels.Direction(PadControl::RightStick, wiimote->GetClassicGroup(ClassicGroup::RightStick));
    return {kind, labels.Take()};
  }

  const bool nunchuk = kind == PadKind::WiiNunchuk;
  labels.Button(PadControl::A, buttons, "A");
  labels.Button(PadControl::B, buttons, "B");
  labels.Button(PadControl::One, buttons, "1");
  labels.Button(PadControl::Two, buttons, "2");
  labels.Button(PadControl::Plus, buttons, "+");
  labels.Button(PadControl::Minus, buttons, "-");
  labels.Set(PadControl::Home, home.empty() ? "VR Menu" : home);
  // A stick is its D-Pad, too (CreateOpenXRInputOverrideFunction).
  std::string dpad = DescribeUp(
      GetExpression(wiimote->GetWiimoteGroup(WiimoteEmu::WiimoteGroup::DPad), "Up"));
  const std::string stick = nunchuk ? "Right Stick" : "Left Stick";
  if (choice != VRWiiController::Choice::None && !dpad.contains(stick))
    dpad += dpad.empty() ? stick : " or " + stick;
  labels.Set(PadControl::DPad, std::move(dpad));
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
  // Others have none of these controls, or not from here.
  auto& system = Core::System::GetInstance();
  const bool known =
      system.IsWii() ? VRWiiController::GetCurrent(system) != VRWiiController::Choice::None :
                       Config::Get(Config::GetInfoForSIDevice(0)) ==
                           SerialInterface::SIDEVICE_GC_CONTROLLER;
  if (!known)
    return "The game has another controller.";
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
      if (place.analog)
        binding = GetTrigger(place);
      else if (place.group)
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
      {
        SetExpression(place.group, name,
                      place.analog ? MakeExpression(WithClicks(binding)) : expression);
      }
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
