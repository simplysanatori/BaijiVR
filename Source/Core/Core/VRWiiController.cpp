// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/VRWiiController.h"

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/HookableEvent.h"
#include "Common/IniFile.h"

#include "Core/Config/GraphicsSettings.h"
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

#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/InputConfig.h"

#include "VideoCommon/VideoConfig.h"

namespace VRWiiController
{
const Config::Info<Choice> VR_WII_CONTROLLER{{Config::System::GFX, "VR", "WiiController"},
                                             Choice::None};

namespace
{
constexpr std::string_view FRAME_DEVICE = "OpenXR/0/OpenXR Controller";
constexpr auto SI_NONE = SerialInterface::SIDEVICE_NONE;
constexpr auto SI_PAD = SerialInterface::SIDEVICE_GC_CONTROLLER;

// The mapping in use is a profile's.
std::atomic<Choice> s_profile{Choice::None};

WiimoteEmu::Wiimote* GetRemote()
{
  if (Wiimote::GetConfig()->ControllersNeedToBeCreated())
    return nullptr;
  return static_cast<WiimoteEmu::Wiimote*>(Wiimote::GetConfig()->GetController(0));
}

Choice DescribeMapping()
{
  WiimoteEmu::Wiimote* const remote = GetRemote();
  if (!remote)
    return Choice::None;
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  switch (remote->GetActiveExtensionNumber())
  {
  case WiimoteEmu::ExtensionNumber::NUNCHUK:
    return Choice::RemoteNunchuk;
  case WiimoteEmu::ExtensionNumber::NONE:
    return remote->IsSideways() ? Choice::RemoteSideways : Choice::Remote;
  default:
    return Choice::None;
  }
}

std::string GetName(Choice choice)
{
  switch (choice)
  {
  case Choice::RemoteNunchuk:
    return "Wii Remote + Nunchuk";
  case Choice::RemoteSideways:
    return "Wii Remote (Sideways)";
  default:
    return "Wii Remote";
  }
}

std::string GetSysProfile(Choice choice)
{
  return Wiimote::GetConfig()->GetSysProfileDirectoryPath() + "Steam Frame " + GetName(choice) +
         ".ini";
}

// The player's own, once Edit Controls has changed it.
std::string GetUserProfile(Choice choice)
{
  return Wiimote::GetConfig()->GetUserProfileDirectoryPath() + "VR " + GetName(choice) + ".ini";
}

bool LoadProfile(const std::string& path)
{
  Common::IniFile ini;
  if (!ini.Load(path))
    return false;
  GetRemote()->LoadConfig(ini.GetOrCreateSection("Profile"));
  GetRemote()->UpdateReferences(g_controller_interface);
  return true;
}

bool IsVR()
{
  switch (Config::Get(Config::GFX_VR_PRESENTATION_MODE))
  {
  case OpenXRPresentationMode::Immersive:
  case OpenXRPresentationMode::StereoScreen:
    return true;
  case OpenXRPresentationMode::Vulkan:
    return false;
  case OpenXRPresentationMode::Legacy:
    break;
  }
  return Config::Get(Config::GFX_VR_ENABLE_OPENXR);
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

// The next game starts with the mapping of every game.
void RestoreWhenGameEnds()
{
  static std::once_flag once;
  static Common::EventHook hook;
  std::call_once(once, [] {
    hook = Core::AddOnStateChangedCallback([](Core::State state) {
      if (state != Core::State::Uninitialized || s_profile.exchange(Choice::None) == Choice::None)
        return;
      Core::QueueHostJob(
          [](Core::System&) {
            if (GetRemote())
              Wiimote::GetConfig()->LoadConfig();
          },
          true);
    });
  });
}
}  // namespace

Choice GetCurrent(Core::System& system)
{
  if (!system.IsWii())
    return Choice::None;
  if (Config::Get(Config::WIIMOTE_1_SOURCE) == WiimoteSource::None &&
      Config::Get(Config::GetInfoForSIDevice(0)) == SI_PAD)
  {
    return Choice::GameCube;
  }
  return DescribeMapping();
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
    {
      GameSettingScope::SetForGame(source, WiimoteSource::None, Store::LocalGame, false);
    }
    else if (Config::GetBase(source) == WiimoteSource::None)
    {
      GameSettingScope::SetForGame(source, WiimoteSource::OpenXR, Store::LocalGame, false);
    }
    else
    {
      GameSettingScope::UseGlobal(source, Store::LocalGame);
    }
    if (const auto layer = Config::GetLayer(Config::LayerType::LocalGame))
      layer->Save();
  }
  system.GetSerialInterface().ChangeDevice(pad ? SI_PAD : SI_NONE, 0);
  if (!pad)
    Wiimote::LoadConfig();
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

void OnMappingLoaded()
{
  s_profile = Choice::None;
  const Choice choice = Config::Get(VR_WII_CONTROLLER);
  if (choice == Choice::None || choice == Choice::GameCube || !GetRemote() || !IsVR() ||
      choice == DescribeMapping())
  {
    return;
  }
  if (HasGameProfile())
    return;
  if (!LoadProfile(GetUserProfile(choice)) && !LoadProfile(GetSysProfile(choice)))
    return;
  s_profile = choice;
  RestoreWhenGameEnds();
}

void SaveMapping()
{
  const Choice profile = s_profile;
  if (profile == Choice::None)
  {
    Wiimote::GetConfig()->SaveConfig();
    return;
  }
  const std::string path = GetUserProfile(profile);
  Common::IniFile ini;
  GetRemote()->SaveConfig(ini.GetOrCreateSection("Profile"));
  File::CreateFullPath(path);
  ini.Save(path);
}

void ResetMapping()
{
  const Choice profile = s_profile;
  if (profile != Choice::None)
  {
    if (LoadProfile(GetSysProfile(profile)))
      File::Delete(GetUserProfile(profile), File::IfAbsentBehavior::NoConsoleWarning);
  }
  else if (const Choice mapping = DescribeMapping(); mapping != Choice::None)
  {
    if (LoadProfile(GetSysProfile(mapping)))
      Wiimote::GetConfig()->SaveConfig();
  }
}
}  // namespace VRWiiController
