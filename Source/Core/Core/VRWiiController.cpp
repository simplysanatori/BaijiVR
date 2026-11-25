// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/VRWiiController.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/HookableEvent.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"

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
#include "InputCommon/InputProfile.h"

#include "VideoCommon/OnScreenDisplay.h"

namespace VRWiiController
{
const Config::Info<Choice> VR_WII_CONTROLLER{{Config::System::GFX, "VR", "WiiController"},
                                             Choice::None};

namespace
{
constexpr std::string_view FRAME_DEVICE = "OpenXR/0/OpenXR Controller";
constexpr const char* BUNDLED_FILE = "WiiControllers.ini";
constexpr const char* PAD_PROFILE = "Steam Frame GameCube.ini";
constexpr auto SI_NONE = SerialInterface::SIDEVICE_NONE;
constexpr auto SI_PAD = SerialInterface::SIDEVICE_GC_CONTROLLER;

// Where the mapping of a controller is from. Changed under the state lock, with the mapping.
struct Store
{
  // The profile it is saved to. Empty: it is the mapping of every game.
  std::string path;
  // The profile is the one the running game's settings name, by this name.
  std::string game_profile;
  // Of a Wii Remote's profile: what it is a mapping of.
  Choice choice = Choice::None;
  // With a profile: the mapping of every game, and the profile's as it was loaded or saved last.
  Common::IniFile::Section every_game;
  Common::IniFile::Section saved;
};
std::array<Store, 2> s_stores;

Store& GetStore(Kind kind)
{
  return s_stores[static_cast<std::size_t>(kind)];
}

InputConfig* GetConfig(Kind kind)
{
  return kind == Kind::GameCube ? Pad::GetConfig() : Wiimote::GetConfig();
}

ControllerEmu::EmulatedController* GetController(Kind kind)
{
  InputConfig* const config = GetConfig(kind);
  return config->ControllersNeedToBeCreated() ? nullptr : config->GetController(0);
}

WiimoteEmu::Wiimote* GetRemote()
{
  return static_cast<WiimoteEmu::Wiimote*>(GetController(Kind::Remote));
}

bool IsEmulated(WiimoteSource source)
{
  return source == WiimoteSource::Emulated || source == WiimoteSource::OpenXR;
}

// One of the Wii Remote.
bool IsRemote(Choice choice)
{
  return choice != Choice::None && choice != Choice::GameCube;
}

const ControllerEmu::Attachments& GetAttachments()
{
  return *static_cast<const ControllerEmu::Attachments*>(
      GetRemote()->GetWiimoteGroup(WiimoteEmu::WiimoteGroup::Attachments));
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
  switch (GetAttachments().GetSelectedAttachment())
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

std::string GetSysProfile(Kind kind, Choice choice)
{
  return GetConfig(kind)->GetSysProfileDirectoryPath() +
         (kind == Kind::GameCube ? PAD_PROFILE : GetSysProfileName(choice));
}

// The player's own of every game, once it was changed.
std::string GetUserProfile(Choice choice)
{
  return Wiimote::GetConfig()->GetUserProfileDirectoryPath() + "VR " + GetName(choice) + ".ini";
}

std::string GetGameProfileKey(Kind kind)
{
  return GetConfig(kind)->GetProfileKey() + "Profile1";
}

// The profile the running game's settings name for the first controller, as Dolphin has it:
// [Controls] PadProfile1 or WiimoteProfile1.
std::string GetGameProfile(Kind kind)
{
  if (!GameSettingScope::IsGameRunning())
    return {};
  std::string name;
  Common::IniFile game_ini = SConfig::GetInstance().LoadGameIni();
  game_ini.GetOrCreateSection("Controls")->Get(GetGameProfileKey(kind), &name);
  return name;
}

// In the player's settings of the running game, through the layer that has them: what it has is
// written with every one of them. Empty: they do not have it. Host thread.
void SetGameString(const Config::Location& location, const std::string& value, bool save)
{
  const auto layer = Config::GetLayer(Config::LayerType::LocalGame);
  if (!layer)
    return;
  // Only what the layer has had is removed from the file.
  if (value.empty())
    layer->Set(location, std::string());
  GameSettingScope::SetLocalGame(
      location, value.empty() ? std::nullopt : std::optional<std::string>(value), save);
}

void SetGameProfile(Kind kind, const std::string& name)
{
  if (!GameSettingScope::IsGameRunning() || GetGameProfile(kind) == name)
    return;
  SetGameString({Config::System::GameSettingsOnly, "Controls", GetGameProfileKey(kind)}, name,
                true);
}

// The profile the running game's settings named while it had this controller.
Config::Location GetKeptProfileLocation(Choice choice)
{
  return {Config::System::GFX, "VR",
          fmt::format("WiiControllerProfile{}", std::to_underlying(choice))};
}

std::string GetKeptProfile(Choice choice)
{
  const auto layer = Config::GetLayer(Config::LayerType::LocalGame);
  return layer ? layer->Get<std::string>(GetKeptProfileLocation(choice)).value_or("") : "";
}

// The player's, or else one of the Sys folder.
std::string FindProfile(Kind kind, const std::string& name)
{
  for (const std::string& folder : {GetConfig(kind)->GetUserProfileDirectoryPath(),
                                    GetConfig(kind)->GetSysProfileDirectoryPath()})
  {
    if (const auto found = InputProfile::GetProfilesFromSetting(name, folder); !found.empty())
      return found.front();
  }
  return {};
}

// The profile "Per-Game" makes for the running game.
std::string GetOwnProfileName(std::string_view controller)
{
  return fmt::format("{} {}", SConfig::GetInstance().GetGameID(), controller);
}

std::string GetOwnProfileName(Kind kind, Choice choice)
{
  return GetOwnProfileName(GetName(kind == Kind::GameCube ? Choice::GameCube : choice));
}

// Of any controller: a profile the player has named after the game is not one of those.
bool IsOwnProfileName(const std::string& name)
{
  if (std::ranges::any_of(NAMES, [&](const char* n) { return name == GetOwnProfileName(n); }))
    return true;
  if (!GetRemote())
    return false;
  const auto& attachments = GetAttachments().GetAttachmentList();
  return std::ranges::any_of(
      attachments, [&](const auto& a) { return name == GetOwnProfileName(a->GetName()); });
}

std::string GetUserProfilePath(Kind kind, const std::string& name)
{
  return GetConfig(kind)->GetUserProfileDirectoryPath() + name + ".ini";
}

void SetChoice(Common::IniFile::Section* section, Choice choice)
{
  // As strings: Set() makes "True" of a string literal.
  const std::string extension = choice == Choice::RemoteNunchuk ? "Nunchuk" :
                                choice == Choice::Classic       ? "Classic" :
                                                                  "None";
  section->Set("Extension", extension);
  section->Set("Options/Sideways Wiimote", choice == Choice::RemoteSideways);
}

// With a choice, a Wii Remote's is of it, whatever the file says about the extension.
bool LoadProfile(Kind kind, const std::string& path, Choice choice = Choice::None)
{
  Common::IniFile ini;
  if (!ini.Load(path))
    return false;
  auto* const section = ini.GetOrCreateSection("Profile");
  if (section->GetValues().empty())
    return false;
  if (choice != Choice::None)
    SetChoice(section, choice);
  GetController(kind)->LoadConfig(section);
  return true;
}

Common::IniFile::Section GetMapping(Kind kind)
{
  Common::IniFile::Section section;
  GetController(kind)->SaveConfig(&section);
  return section;
}

void SetStore(Kind kind, std::string path, std::string game_profile, Choice choice,
              const Common::IniFile::Section& every_game)
{
  GetStore(kind) = {std::move(path), std::move(game_profile), choice, every_game,
                    GetMapping(kind)};
}

// The profile the game's settings name is the mapping, whatever it is of. False without one.
bool LoadGameProfile(Kind kind, const Common::IniFile::Section& every_game)
{
  const std::string name = GetGameProfile(kind);
  if (name.empty())
    return false;
  if (const std::string path = FindProfile(kind, name); !path.empty() && LoadProfile(kind, path))
  {
    // One of the Sys folder is saved as the player's of that name.
    const bool own = path.starts_with(GetConfig(kind)->GetUserProfileDirectoryPath());
    SetStore(kind, own ? path : GetUserProfilePath(kind, name), name,
             kind == Kind::Remote ? DescribeMapping() : Choice::None, every_game);
  }
  return true;
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

// What the running game is to have. With the mapping of every game loaded.
Rules GetRules()
{
  using GameSettingScope::Store;
  auto& system = Core::System::GetInstance();
  Rules rules;
  if (!system.IsWii() || !GameSettingScope::IsGameRunning())
    return rules;
  rules.own = ToChoice(static_cast<int>(Config::Get(VR_WII_CONTROLLER)));
  rules.has_profile = !GetGameProfile(Kind::Remote).empty();
  rules.has_port =
      GameSettingScope::HasGameValue(Config::GetInfoForSIDevice(0).GetLocation(),
                                     Store::LocalGame) ||
      GameSettingScope::HasGameValue(Config::WIIMOTE_1_SOURCE.GetLocation(), Store::LocalGame);
  rules.as_set_up =
      rules.own == Choice::None &&
      GameSettingScope::HasGameValue(VR_WII_CONTROLLER.GetLocation(), Store::LocalGame);
  rules.bundled = GetBundled(system);
  return rules;
}

void Reload(Core::System& system, Kind kind)
{
  if (!GetController(kind))
    return;
  const Core::CPUThreadGuard guard(system);
  if (kind == Kind::Remote)
    Wiimote::LoadConfig();
  else
    Pad::LoadConfig();
}

// The next game starts with the mapping of every game.
void RestoreWhenGameEnds()
{
  static std::once_flag once;
  static Common::EventHook hook;
  std::call_once(once, [] {
    hook = Core::AddOnStateChangedCallback([](Core::State state) {
      if (state != Core::State::Uninitialized)
        return;
      // The next game may have started already, and have its own.
      Core::QueueHostJob(
          [](Core::System& system) {
            for (const Kind kind : {Kind::GameCube, Kind::Remote})
            {
              if (GetController(kind) && !GetStore(kind).path.empty())
                Reload(system, kind);
            }
          },
          true);
    });
  });
}

// With the game it is of. Under the state lock.
std::pair<std::string, std::string> s_notice;

// The table chose: the player is told once. A game that starts has no session yet, which takes the
// notice when it has (TakeNotice).
void TellBundled(Choice choice)
{
  static std::string s_told;
  const std::string id = SConfig::GetInstance().GetGameID();
  if (std::exchange(s_told, id) == id)
    return;
  s_notice = {id,
              fmt::format("This game has the {}. The VR menu's Controls tab has the other "
                          "controllers.",
                          GetName(choice))};
  Core::DisplayMessage(s_notice.second, 6000);
}

// The player's choice, without anything else: the mapping is that controller's already. None: the
// mapping of every game, with whatever it has.
void SetChoiceOnly(Choice choice)
{
  GameSettingScope::SetForGame(VR_WII_CONTROLLER, choice, GameSettingScope::Store::LocalGame);
}

void KeepProfile(Choice choice, const std::string& name)
{
  if (choice != Choice::None)
    SetGameString(GetKeptProfileLocation(choice), name, false);
  else if (!name.empty())
    OSD::AddMessage(fmt::format("Profile \"{}\" is kept. Edit Controls has it.", name), 6000);
}

// Under the state lock: into the profile if the mapping is one, and the section stays what it is.
void Save(Kind kind, Common::IniFile::Section* section)
{
  Store& store = GetStore(kind);
  if (store.path.empty())
  {
    GetController(kind)->SaveConfig(section);
    return;
  }
  Common::IniFile ini;
  auto* const profile = ini.GetOrCreateSection("Profile");
  GetController(kind)->SaveConfig(profile);
  if (profile->GetValues() != store.saved.GetValues())
  {
    File::CreateFullPath(store.path);
    ini.Save(store.path);
    store.saved = *profile;
  }
  // The file of every game was not there yet.
  if (section->GetValues().empty())
  {
    for (const auto& [key, value] : store.every_game.GetValues())
      section->Set(key, value);
  }
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
  return rules.has_port || rules.as_set_up ? Choice::None : rules.bundled;
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
  return system.IsWii() && !IsFixed(system) && GameSettingScope::IsGameRunning();
}

void Choose(Core::System& system, Choice choice)
{
  if (choice == Choice::None || !CanChoose(system))
    return;
  using GameSettingScope::Store;
  const auto& port = Config::GetInfoForSIDevice(0);
  const auto& source = Config::WIIMOTE_1_SOURCE;
  const bool pad = choice == Choice::GameCube;
  const Choice before = DescribeMapping();
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
    const bool has_profile = HasGameMapping(Kind::Remote);
    if (!pad && (before != choice || !has_profile))
    {
      // A profile is of one controller: it is kept for that one.
      if (before != choice)
        KeepProfile(before, has_profile ? GetGameProfile(Kind::Remote) : std::string{});
      const std::string kept = GetKeptProfile(choice);
      SetGameProfile(Kind::Remote, FindProfile(Kind::Remote, kept).empty() ? std::string{} : kept);
    }
    if (const auto layer = Config::GetLayer(Config::LayerType::LocalGame))
      layer->Save();
  }
  Reload(system, Kind::Remote);
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

std::string TakeNotice()
{
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  const auto [id, text] = std::exchange(s_notice, {});
  return id == SConfig::GetInstance().GetGameID() ? text : std::string{};
}

void LoadMapping()
{
  Wiimote::GetConfig()->LoadConfig();

  auto& system = Core::System::GetInstance();
  if (!system.IsWii() || !GameSettingScope::IsGameRunning())
    return;
  const SerialInterface::SIDevices device = Config::Get(Config::GetInfoForSIDevice(0));
  // The port is set up when the game starts, and not when one title starts another.
  if (!IsFixed(system))
    system.GetSerialInterface().ChangeDevice(device, 0);
  if (GetRemote())
  {
    INFO_LOG_FMT(WIIMOTE, "Controller: {} is wanted, {} is mapped ({}). Port 1: {}, source: {}",
                 GetName(Resolve(GetRules())), GetName(DescribeMapping()),
                 DescribeMappingStore(Kind::Remote, 0), std::to_underlying(device),
                 std::to_underlying(Config::Get(Config::WIIMOTE_1_SOURCE)));
  }
}

bool LoadPad(int index, Common::IniFile::Section* section)
{
  constexpr Kind KIND = Kind::GameCube;
  if (index != 0 || !GetController(KIND))
    return false;
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  GetStore(KIND) = {};
  GetController(KIND)->LoadConfig(section);
  LoadGameProfile(KIND, *section);
  if (!GetStore(KIND).path.empty())
    RestoreWhenGameEnds();
  return true;
}

bool SavePad(int index, Common::IniFile::Section* section)
{
  if (index != 0 || !GetController(Kind::GameCube))
    return false;
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  Save(Kind::GameCube, section);
  return true;
}

bool LoadRemote(int index, Common::IniFile::Section* section)
{
  constexpr Kind KIND = Kind::Remote;
  if (index != 0 || !GetRemote())
    return false;
  // All of it at once: the game does not see the mapping of every game in between.
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  GetStore(KIND) = {};
  GetRemote()->LoadConfig(section);

  if (!LoadGameProfile(KIND, *section))
  {
    const Rules rules = GetRules();
    const Choice wanted = Resolve(rules);
    if (IsRemote(wanted) && wanted != DescribeMapping() &&
        (LoadProfile(KIND, GetUserProfile(wanted), wanted) ||
         LoadProfile(KIND, GetSysProfile(KIND, wanted), wanted)))
    {
      SetStore(KIND, GetUserProfile(wanted), {}, wanted, *section);
      if (rules.own == Choice::None)
        TellBundled(wanted);
    }
  }
  if (!GetStore(KIND).path.empty())
    RestoreWhenGameEnds();
  return true;
}

bool SaveRemote(int index, Common::IniFile::Section* section)
{
  constexpr Kind KIND = Kind::Remote;
  if (index != 0 || !GetRemote())
    return false;
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  Store& store = GetStore(KIND);
  const Choice mapping = DescribeMapping();
  const Rules rules = GetRules();
  // A profile of the game is the mapping, whatever it is of.
  if (!store.game_profile.empty())
  {
    Save(KIND, section);
    if (IsRemote(mapping) && IsRemote(rules.own) && mapping != rules.own)
      Core::QueueHostJob([mapping](Core::System&) { SetChoiceOnly(mapping); });
    return true;
  }
  const Choice wanted = Resolve(rules);
  const Choice choice = !store.path.empty() ? store.choice : IsRemote(wanted) ? wanted : mapping;
  if (mapping == choice)
  {
    Save(KIND, section);
    return true;
  }

  // Another extension was chosen in a window. The mapping of every game has it if that is what the
  // window shows; a profile of one controller stays that.
  if (store.path.empty())
    GetRemote()->SaveConfig(section);
  if (mapping != Choice::None)
  {
    // The game has that one now, with the mapping that is for it.
    Core::QueueHostJob([mapping](Core::System& system) { Choose(system, mapping); }, true);
  }
  else if (store.path.empty())
  {
    // One that is not a choice, for every game.
    Core::QueueHostJob([](Core::System&) { SetChoiceOnly(Choice::None); }, true);
  }
  else
  {
    // One that is not a choice, where the mapping is not that of every game: for this game.
    const auto& attachments = GetAttachments();
    const std::string name = GetOwnProfileName(
        attachments.GetAttachmentList()[attachments.GetSelectedAttachment()]->GetName());
    store.path = GetUserProfilePath(KIND, name);
    store.game_profile = name;
    store.choice = Choice::None;
    Save(KIND, section);
    Core::QueueHostJob([name](Core::System&) { SetGameProfile(KIND, name); }, true);
  }
  return true;
}

bool HasGameMapping(Kind kind)
{
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  return !GetStore(kind).game_profile.empty();
}

std::string GetGameMappingLock(Kind kind)
{
  if (!GetController(kind) || !GameSettingScope::IsGameRunning())
    return "No game is running.";
  if (kind == Kind::Remote && DescribeMapping() == Choice::None && !HasGameMapping(kind))
    return "The game has another controller.";
  return {};
}

void MakeGameMapping(Core::System& system, Kind kind)
{
  if (!GetGameMappingLock(kind).empty() || HasGameMapping(kind))
    return;
  std::string name;
  {
    const auto lock = ControllerEmu::EmulatedController::GetStateLock();
    name = GetOwnProfileName(kind, DescribeMapping());
    const std::string path = GetUserProfilePath(kind, name);
    Common::IniFile ini;
    GetController(kind)->SaveConfig(ini.GetOrCreateSection("Profile"));
    File::CreateFullPath(path);
    ini.Save(path);
  }
  SetGameProfile(kind, name);
  Reload(system, kind);
}

void RemoveGameMapping(Core::System& system, Kind kind)
{
  const std::string name = GetGameProfile(kind);
  if (name.empty())
    return;
  if (kind == Kind::Remote)
    SetGameString(GetKeptProfileLocation(DescribeMapping()), {}, false);
  // The one "Per-Game" made goes with it; another profile is only not this game's any more.
  if (IsOwnProfileName(name))
    File::Delete(GetUserProfilePath(kind, name), File::IfAbsentBehavior::NoConsoleWarning);
  SetGameProfile(kind, {});
  Reload(system, kind);
}

// The controller a saved profile is of. None: another extension, or one that is chosen by a formula.
static Choice DescribeProfile(const std::string& path)
{
  Common::IniFile ini;
  if (!ini.Load(path))
    return Choice::None;
  const Common::IniFile::Section* const section = ini.GetSection("Profile");
  if (!section)
    return Choice::None;
  std::string extension;
  section->Get("Extension", &extension, "None");
  if (extension == "Nunchuk")
    return Choice::RemoteNunchuk;
  if (extension == "Classic")
    return Choice::Classic;
  if (!extension.empty() && extension != "None")
    return Choice::None;
  bool sideways = false;
  section->Get(std::string("Options/") + WiimoteEmu::Wiimote::SIDEWAYS_OPTION, &sideways, false);
  return sideways ? Choice::RemoteSideways : Choice::Remote;
}

std::vector<std::string> GetProfiles(Kind kind, Choice choice)
{
  std::vector<std::string> names;
  std::set<std::string> seen;
  const std::string user = GetConfig(kind)->GetUserProfileDirectoryPath();
  for (const std::string& folder : {user, GetConfig(kind)->GetSysProfileDirectoryPath()})
  {
    // The player's is not there before the first one is saved.
    if (!File::IsDirectory(folder))
      continue;
    for (const std::string& path : Common::DoFileSearch(folder, ".ini"))
    {
      std::string name;
      SplitPath(path, nullptr, &name, nullptr);
      // Of those that come with the program, the Frame controllers', as in the mapping window. The
      // player's are in front of them.
      if (name.empty() || (folder != user && !name.starts_with("Steam Frame")) ||
          !seen.insert(name).second)
      {
        continue;
      }
      // The mapping of every game is no profile to choose (GetUserProfile).
      const bool every_game = std::ranges::any_of(
          NAMES, [&name](const char* controller) { return name == std::string("VR ") + controller; });
      if (kind == Kind::Remote && (every_game || DescribeProfile(path) != choice))
        continue;
      names.push_back(std::move(name));
    }
  }
  return names;
}

std::string GetProfile(Kind kind)
{
  return GetGameProfile(kind);
}

void SetProfile(Core::System& system, Kind kind, const std::string& name)
{
  if (!GetController(kind) || !GameSettingScope::IsGameRunning())
    return;
  const Choice before = DescribeMapping();
  const std::string kept = HasGameMapping(kind) ? GetGameProfile(kind) : "";
  SetGameProfile(kind, name);
  Reload(system, kind);
  if (kind != Kind::Remote)
    return;
  const Choice mapping = DescribeMapping();
  if (before != Choice::None && (before != mapping || name.empty()))
    SetGameString(GetKeptProfileLocation(before), name.empty() ? std::string{} : kept, true);
  // The controller the game has is the profile's.
  if (!name.empty() && IsRemote(mapping) &&
      IsRemote(ToChoice(static_cast<int>(Config::Get(VR_WII_CONTROLLER)))))
  {
    SetChoiceOnly(mapping);
  }
}

std::string DescribeMappingStore(Kind kind, int index)
{
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  const Store& store = GetStore(kind);
  if (index != 0 || store.path.empty())
    return {};
  if (!store.game_profile.empty())
  {
    return fmt::format("Profile \"{}\" of {}", store.game_profile,
                       SConfig::GetInstance().GetGameID());
  }
  return fmt::format("{} of Every Game", GetName(store.choice));
}

Choice GetOwnMapping()
{
  const auto lock = ControllerEmu::EmulatedController::GetStateLock();
  const Store& store = GetStore(Kind::Remote);
  return store.game_profile.empty() ? store.choice : Choice::None;
}

void KeepChoice(int index, Common::IniFile::Section* section)
{
  const Choice own = GetOwnMapping();
  if (index == 0 && own != Choice::None)
    SetChoice(section, own);
}

std::string GetDefaultProfile(int index)
{
  const Choice own = index == 0 ? GetOwnMapping() : Choice::None;
  return GetSysProfileName(own != Choice::None ? own : Choice::RemoteNunchuk);
}

void ResetMapping(Kind kind)
{
  if (!GetController(kind))
    return;
  bool every_game_profile;
  {
    const auto lock = ControllerEmu::EmulatedController::GetStateLock();
    Store& store = GetStore(kind);
    const Choice choice = kind == Kind::GameCube ? Choice::None : DescribeMapping();
    if ((kind == Kind::Remote && choice == Choice::None) ||
        !LoadProfile(kind, GetSysProfile(kind, choice), choice))
    {
      return;
    }
    // That one is the Steam Frame profile without a file.
    every_game_profile = !store.path.empty() && store.game_profile.empty();
    if (every_game_profile)
    {
      store.saved = GetMapping(kind);
      File::Delete(store.path, File::IfAbsentBehavior::NoConsoleWarning);
    }
  }
  GetController(kind)->UpdateReferences(g_controller_interface);
  if (!every_game_profile)
    GetConfig(kind)->SaveConfig();
  GetConfig(kind)->OnMappingChanged();
}
}  // namespace VRWiiController
