// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/VRWiiController.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/HookableEvent.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"

#include "Core/Config/MainSettings.h"
#include "Core/Config/WiimoteSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/GameSettingScope.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/System.h"

#include "InputCommon/ControllerEmu/ControlGroup/Attachments.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerEmu/Setting/NumericSetting.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/InputConfig.h"

namespace VRWiiController
{
const Config::Info<Choice> VR_WII_CONTROLLER{{Config::System::GFX, "VR", "WiiController"},
                                             Choice::None};

namespace
{
constexpr std::string_view FRAME_DEVICE = "OpenXR/0/OpenXR Controller";
constexpr const char* BUNDLED_FILE = "WiiControllers.ini";
constexpr auto SI_NONE = SerialInterface::SIDEVICE_NONE;
constexpr auto SI_PAD = SerialInterface::SIDEVICE_GC_CONTROLLER;

std::mutex s_load_mutex;

// Changed under the state lock, with the mapping:
std::atomic<Choice> s_own{Choice::None};
// With one: the mapping of every game, and the own one as it was loaded or saved last.
Common::IniFile::Section s_every_game;
Common::IniFile::Section s_saved;
// The mapping may be the one of every game without being the one to use.
bool s_loading = false;

WiimoteEmu::Wiimote* GetRemote()
{
  if (Wiimote::GetConfig()->ControllersNeedToBeCreated())
    return nullptr;
  return static_cast<WiimoteEmu::Wiimote*>(Wiimote::GetConfig()->GetController(0));
}

bool IsEmulated(WiimoteSource source)
{
  return source == WiimoteSource::Emulated || source == WiimoteSource::OpenXR;
}

bool IsSideways(const WiimoteEmu::Wiimote& remote)
{
  // Not what the hot keys have made of it.
  const auto* const options = remote.GetWiimoteGroup(WiimoteEmu::WiimoteGroup::Options);
  for (const auto& setting : options->numeric_settings)
  {
    if (setting->GetType() == ControllerEmu::SettingType::Bool &&
        std::string_view(setting->GetININame()) == WiimoteEmu::Wiimote::SIDEWAYS_OPTION)
    {
      return static_cast<const ControllerEmu::NumericSetting<bool>*>(setting.get())->GetValue();
    }
  }
  return false;
}

// Of the mapping: the emulated hardware follows it at its next update, which is after the menu.
Choice DescribeMapping()
{
  const WiimoteEmu::Wiimote* const remote = GetRemote();
  if (!remote)
    return Choice::None;
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  const auto* const attachments = static_cast<const ControllerEmu::Attachments*>(
      remote->GetWiimoteGroup(WiimoteEmu::WiimoteGroup::Attachments));
  switch (attachments->GetSelectedAttachment())
  {
  case WiimoteEmu::ExtensionNumber::NUNCHUK:
    return Choice::RemoteNunchuk;
  case WiimoteEmu::ExtensionNumber::CLASSIC:
    return Choice::Classic;
  case WiimoteEmu::ExtensionNumber::NONE:
    return IsSideways(*remote) ? Choice::RemoteSideways : Choice::Remote;
  default:
    return Choice::None;
  }
}

std::string GetName(Choice choice)
{
  return choice == Choice::None ? "None" : NAMES[static_cast<std::size_t>(choice)];
}

std::string GetSysProfileName(Choice choice)
{
  return "Steam Frame " + GetName(choice) + ".ini";
}

std::string GetSysProfile(Choice choice)
{
  return Wiimote::GetConfig()->GetSysProfileDirectoryPath() + GetSysProfileName(choice);
}

// The player's own, once it was changed.
std::string GetUserProfile(Choice choice)
{
  return Wiimote::GetConfig()->GetUserProfileDirectoryPath() + "VR " + GetName(choice) + ".ini";
}

void SetChoice(Common::IniFile::Section* section, Choice choice)
{
  section->Set("Extension", choice == Choice::RemoteNunchuk ? "Nunchuk" :
                            choice == Choice::Classic       ? "Classic" :
                                                              "None");
  section->Set("Options/Sideways Wiimote", choice == Choice::RemoteSideways ? "True" : "False");
}

// What the file says about the extension does not matter.
bool LoadProfile(const std::string& path, Choice choice)
{
  Common::IniFile ini;
  if (!ini.Load(path))
    return false;
  auto* const section = ini.GetOrCreateSection("Profile");
  if (section->GetValues().empty())
    return false;
  SetChoice(section, choice);
  GetRemote()->LoadConfig(section);
  return true;
}

Common::IniFile::Section GetMapping()
{
  Common::IniFile::Section section;
  GetRemote()->SaveConfig(&section);
  return section;
}

// The game's settings name one.
bool HasGameProfile()
{
  Common::IniFile game_ini = SConfig::GetInstance().LoadGameIni();
  return game_ini.GetOrCreateSection("Controls")->Exists("WiimoteProfile1");
}

// The same inputs have to be there for everyone.
bool IsFixed(Core::System& system)
{
  return NetPlay::IsNetPlayRunning() || system.GetMovie().IsMovieActive();
}

Choice GetBundled(Core::System& system)
{
  if (IsFixed(system) || !IsEmulated(Config::Get(Config::WIIMOTE_1_SOURCE)) ||
      GetRemote()->GetDefaultDevice().ToString() != FRAME_DEVICE)
  {
    return Choice::None;
  }
  Common::IniFile bundled;
  bundled.Load(File::GetSysDirectory() + BUNDLED_FILE);
  return FindBundled(bundled, SConfig::GetInstance().GetGameID());
}

// What the running game is to have. After GetRemote().
Choice GetWanted()
{
  using GameSettingScope::Store;
  auto& system = Core::System::GetInstance();
  if (!system.IsWii() || !GameSettingScope::IsGameRunning())
    return Choice::None;
  Rules rules;
  rules.own = ToChoice(static_cast<int>(Config::Get(VR_WII_CONTROLLER)));
  rules.has_profile = HasGameProfile();
  rules.has_port =
      GameSettingScope::HasGameValue(Config::GetInfoForSIDevice(0).GetLocation(),
                                     Store::LocalGame) ||
      GameSettingScope::HasGameValue(Config::WIIMOTE_1_SOURCE.GetLocation(), Store::LocalGame);
  rules.bundled = GetBundled(system);
  return Resolve(rules);
}

// The mapping is one of the Wii Remote that the choice is not for.
bool IsFor(Choice choice, Choice mapping)
{
  return choice == Choice::None || choice == Choice::GameCube || choice == mapping;
}

void Reload(Core::System& system)
{
  if (!GetRemote())
    return;
  const Core::CPUThreadGuard guard(system);
  Wiimote::LoadConfig();
}

// The next game starts with the mapping of every game.
void RestoreWhenGameEnds()
{
  static std::once_flag once;
  static Common::EventHook hook;
  std::call_once(once, [] {
    hook = Core::AddOnStateChangedCallback([](Core::State state) {
      if (state != Core::State::Uninitialized || s_own == Choice::None)
        return;
      // The next game may have started already, and have its own.
      Core::QueueHostJob([](Core::System& system) { Reload(system); }, true);
    });
  });
}
}  // namespace

Choice ToChoice(int number)
{
  return number >= 0 && number < static_cast<int>(NAMES.size()) ? static_cast<Choice>(number) :
                                                                  Choice::None;
}

Choice FindChoice(std::string_view name)
{
  const auto it = std::ranges::find(NAMES, name);
  return it == NAMES.end() ? Choice::None : static_cast<Choice>(it - NAMES.begin());
}

Choice FindBundled(const Common::IniFile& bundled, std::string_view game_id)
{
  std::string name;
  if (const auto* const section = bundled.GetSection("Controllers"))
    section->Get(game_id, &name);
  return FindChoice(name);
}

Choice Resolve(const Rules& rules)
{
  if (rules.has_profile)
    return Choice::None;
  if (rules.own != Choice::None)
    return rules.own;
  return rules.has_port ? Choice::None : rules.bundled;
}

Choice GetCurrent(Core::System& system)
{
  if (!system.IsWii())
    return Choice::None;
  const WiimoteSource source = Config::Get(Config::WIIMOTE_1_SOURCE);
  if (source == WiimoteSource::None)
  {
    return Config::Get(Config::GetInfoForSIDevice(0)) == SI_PAD ? Choice::GameCube : Choice::None;
  }
  return IsEmulated(source) ? DescribeMapping() : Choice::None;
}

bool CanChoose(Core::System& system)
{
  return system.IsWii() && !IsFixed(system) && GameSettingScope::IsGameRunning() &&
         !HasGameProfile();
}

void Choose(Core::System& system, Choice choice)
{
  if (choice == Choice::None || !CanChoose(system))
    return;
  using GameSettingScope::Store;
  const auto& port = Config::GetInfoForSIDevice(0);
  const auto& source = Config::WIIMOTE_1_SOURCE;
  const bool pad = choice == Choice::GameCube;
  {
    // All at once: the Wii Remote's callback reads them.
    const Config::ConfigChangeCallbackGuard guard;
    GameSettingScope::SetForGame(VR_WII_CONTROLLER, choice, Store::LocalGame, false);
    GameSettingScope::SetForGame(port, pad ? SI_PAD : SI_NONE, Store::LocalGame, false);
    if (pad)
      GameSettingScope::SetForGame(source, WiimoteSource::None, Store::LocalGame, false);
    else if (!IsEmulated(Config::GetBase(source)))
      GameSettingScope::SetForGame(source, WiimoteSource::OpenXR, Store::LocalGame, false);
    else
      GameSettingScope::UseGlobal(source, Store::LocalGame);
    if (const auto layer = Config::GetLayer(Config::LayerType::LocalGame))
      layer->Save();
  }
  Reload(system);
}

void OnSessionStarted(Core::System& system)
{
  const auto& port = Config::GetInfoForSIDevice(0);
  if (!system.IsWii() || IsFixed(system) || Config::Get(VR_WII_CONTROLLER) != Choice::None ||
      Config::Get(Config::WIIMOTE_1_SOURCE) == WiimoteSource::None ||
      Config::Get(port) != SI_PAD ||
      GameSettingScope::HasGameValue(port.GetLocation(), GameSettingScope::Store::LocalGame) ||
      Pad::GetConfig()->ControllersNeedToBeCreated() ||
      Pad::GetConfig()->GetController(0)->GetDefaultDevice().ToString() != FRAME_DEVICE)
  {
    return;
  }
  Config::SetCurrent(port, SI_NONE);
  system.GetSerialInterface().ChangeDevice(SI_NONE, 0);
}

void LoadMapping()
{
  const std::lock_guard load_lock(s_load_mutex);
  {
    const auto lock = ControllerEmu::EmulatedController::GetStateLock();
    s_own = Choice::None;
    s_loading = true;
  }
  Wiimote::GetConfig()->LoadConfig();

  auto& system = Core::System::GetInstance();
  const bool running = system.IsWii() && GameSettingScope::IsGameRunning();
  const SerialInterface::SIDevices device = Config::Get(Config::GetInfoForSIDevice(0));
  // The port is set up when the game starts, and not when one title starts another.
  if (running && !IsFixed(system))
    system.GetSerialInterface().ChangeDevice(device, 0);

  auto* const remote = GetRemote();
  const Choice wanted = remote ? GetWanted() : Choice::None;
  {
    const auto lock = ControllerEmu::EmulatedController::GetStateLock();
    if (remote && !IsFor(wanted, DescribeMapping()))
    {
      s_every_game = GetMapping();
      if (LoadProfile(GetUserProfile(wanted), wanted) ||
          LoadProfile(GetSysProfile(wanted), wanted))
      {
        s_saved = GetMapping();
        s_own = wanted;
      }
    }
    s_loading = false;
  }
  if (s_own != Choice::None)
  {
    remote->UpdateReferences(g_controller_interface);
    RestoreWhenGameEnds();
    Wiimote::GetConfig()->OnMappingChanged();
  }
  if (running)
  {
    INFO_LOG_FMT(WIIMOTE, "Controller: {} is wanted, {} is mapped{}. Port 1: {}, source: {}",
                 GetName(wanted), GetName(DescribeMapping()),
                 s_own == Choice::None ? "" : " (its own)", std::to_underlying(device),
                 std::to_underlying(Config::Get(Config::WIIMOTE_1_SOURCE)));
  }
}

bool SaveOwnMapping(int index, Common::IniFile::Section* section)
{
  if (index != 0 || !GetRemote())
    return false;
  const Choice wanted = GetWanted();
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  if (s_loading)
    return true;
  const Choice own = s_own;
  const Choice mapping = DescribeMapping();
  if (own == Choice::None)
  {
    GetRemote()->SaveConfig(section);
    // It is not for this game any more.
    if (!IsFor(wanted, mapping))
      Core::QueueHostJob([](Core::System& system) { Reload(system); });
    return true;
  }

  if (mapping == own)
  {
    Common::IniFile ini;
    auto* const profile = ini.GetOrCreateSection("Profile");
    GetRemote()->SaveConfig(profile);
    if (profile->GetValues() != s_saved.GetValues())
    {
      const std::string path = GetUserProfile(own);
      File::CreateFullPath(path);
      ini.Save(path);
      s_saved = *profile;
    }
  }
  else if (mapping != Choice::None)
  {
    // Another extension was chosen in a window: the game has that one now, with its own mapping.
    Core::QueueHostJob([mapping](Core::System& system) { Choose(system, mapping); });
  }
  if (section->GetValues().empty())
  {
    for (const auto& [key, value] : s_every_game.GetValues())
      section->Set(key, value);
  }
  return true;
}

Choice GetOwnMapping()
{
  return s_own;
}

void KeepChoice(int index, Common::IniFile::Section* section)
{
  const Choice own = s_own;
  if (index == 0 && own != Choice::None)
    SetChoice(section, own);
}

std::string GetDefaultProfile(int index)
{
  const Choice own = index == 0 ? s_own.load() : Choice::None;
  return GetSysProfileName(own != Choice::None ? own : Choice::RemoteNunchuk);
}

void ResetMapping()
{
  Choice own;
  {
    const auto lock = ControllerEmu::EmulatedController::GetStateLock();
    own = s_own;
    const Choice choice = own != Choice::None ? own : DescribeMapping();
    if (choice == Choice::None || !LoadProfile(GetSysProfile(choice), choice))
      return;
    s_saved = GetMapping();
  }
  GetRemote()->UpdateReferences(g_controller_interface);
  if (own != Choice::None)
    File::Delete(GetUserProfile(own), File::IfAbsentBehavior::NoConsoleWarning);
  else
    Wiimote::GetConfig()->SaveConfig();
  Wiimote::GetConfig()->OnMappingChanged();
}
}  // namespace VRWiiController
