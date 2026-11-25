// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"

#include "Core/Config/MainSettings.h"
#include "Core/Config/WiimoteSettings.h"
#include "Core/ConfigLoaders/BaseConfigLoader.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/GameSettingScope.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/GCPadEmu.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "Core/System.h"
#include "Core/VRWiiController.h"

#include "InputCommon/ControllerEmu/ControlGroup/Attachments.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/InputConfig.h"

#include "UICommon/UICommon.h"

#include "VideoCommon/VR/FrameUI/PadBindings.h"
#include "VideoCommon/VR/FrameUI/PadEdit.h"

using namespace VRWiiController;
using namespace VR::UI;

namespace
{
constexpr std::array WII_CHOICES{Choice::Remote, Choice::RemoteNunchuk, Choice::RemoteSideways,
                                 Choice::Classic};

using Values = std::map<std::string, std::string>;

bool HasSys()
{
  return File::Exists(File::GetSysDirectory() + "WiiControllers.ini");
}

Values LoadProfile(const std::string& path)
{
  Common::IniFile ini;
  EXPECT_TRUE(ini.Load(path)) << path;
  const auto& values = ini.GetOrCreateSection("Profile")->GetValues();
  return {values.begin(), values.end()};
}

Values Save(ControllerEmu::EmulatedController* controller)
{
  Common::IniFile::Section section;
  controller->SaveConfig(&section);
  return {section.GetValues().begin(), section.GetValues().end()};
}

std::string GetName(Choice choice)
{
  return NAMES[static_cast<std::size_t>(choice)];
}

std::string GetStock(const char* folder, const std::string& name)
{
  return File::GetSysDirectory() + "Profiles/" + folder + "/Steam Frame " + name + ".ini";
}

// A Wii game, and the Frame's mapping of every game.
class VRWiiControllerMapping : public testing::Test
{
protected:
  void SetUp() override
  {
    if (!HasSys())
      GTEST_SKIP() << "the Sys folder is not where this binary looks";
    m_folder = File::CreateTempDir();
    UICommon::SetUserDirectory(m_folder);
    UICommon::CreateDirectories();
    Config::Init();
    Config::AddLayer(ConfigLoaders::GenerateBaseConfigLoader());
    SConfig::Init();
    Config::SetBase(Config::WIIMOTE_1_SOURCE, WiimoteSource::OpenXR);
    InputConfig* const config = Wiimote::GetConfig();
    for (int i = 0; i < 5; ++i)
      config->CreateController<WiimoteEmu::Wiimote>(i);
    config->SetOverrides(LoadRemote, SaveRemote);
    for (int i = 0; i < 4; ++i)
      Pad::GetConfig()->CreateController<GCPad>(i);
    Pad::GetConfig()->SetOverrides(LoadPad, SavePad);
    auto& system = Core::System::GetInstance();
    m_was_wii = system.IsWii();
    system.SetIsWii(true);
    Wiimote::LoadConfig();
    config->SaveConfig();
    Pad::LoadConfig();
    Pad::GetConfig()->SaveConfig();
  }

  void TearDown() override
  {
    if (m_folder.empty())
      return;
    EndGame();
    // Without a game they do nothing.
    Core::HostDispatchJobs(Core::System::GetInstance());
    Wiimote::GetConfig()->SetOverrides(nullptr, nullptr);
    Wiimote::GetConfig()->ClearControllers();
    Pad::GetConfig()->SetOverrides(nullptr, nullptr);
    Pad::GetConfig()->ClearControllers();
    Core::System::GetInstance().SetIsWii(m_was_wii);
    SConfig::Shutdown();
    Config::Shutdown();
    File::DeleteDirRecursively(m_folder);
  }

  // As SConfig::OnESTitleChanged and BootManager.
  static void Start(const std::string& id)
  {
    SConfig::GetInstance().SetRunningGameMetadata(id);
    Wiimote::LoadConfig();
    Pad::LoadConfig();
  }
  static void EndGame()
  {
    Config::ClearCurrentRunLayer();
    Config::RemoveLayer(Config::LayerType::GlobalGame);
    Config::RemoveLayer(Config::LayerType::LocalGame);
    SConfig::GetInstance().ResetRunningGameMetadata();
    Wiimote::LoadConfig();
    Pad::LoadConfig();
  }
  static void Choose(Choice choice)
  {
    VRWiiController::Choose(Core::System::GetInstance(), choice);
  }
  static Choice Current() { return GetCurrent(Core::System::GetInstance()); }
  // As a window does it.
  static void SetExtension(u32 extension)
  {
    GetAttachments()->SetSelectedAttachment(extension);
    Wiimote::GetConfig()->SaveConfig();
    Core::HostDispatchJobs(Core::System::GetInstance());
  }
  static ControllerEmu::Attachments* GetAttachments()
  {
    return static_cast<ControllerEmu::Attachments*>(
        Wiimote::GetWiimoteGroup(0, WiimoteEmu::WiimoteGroup::Attachments));
  }
  static std::string GameIni(const std::string& id)
  {
    std::string text;
    File::ReadFileToString(File::GetUserPath(D_GAMESETTINGS_IDX) + id + ".ini", text);
    return text;
  }
  static std::string EveryGame(const char* name = "WiimoteNew.ini")
  {
    std::string text;
    File::ReadFileToString(File::GetUserPath(D_CONFIG_IDX) + name, text);
    return text;
  }
  static std::string GetGameProfile(const char* folder, const std::string& name)
  {
    return File::GetUserPath(D_CONFIG_IDX) + "Profiles/" + folder + "/" + name + ".ini";
  }
  static void SetScope(PadKind kind, PadScope scope)
  {
    SetPadScope(kind, scope);
    EXPECT_EQ(GetPadScope(kind), scope);
  }
  static std::string GetUserProfile(Choice choice)
  {
    return Wiimote::GetConfig()->GetUserProfileDirectoryPath() + "VR " + GetName(choice) + ".ini";
  }
  static void Write(const std::string& path, const std::string& text)
  {
    File::CreateFullPath(path);
    EXPECT_TRUE(File::WriteStringToFile(path, text));
  }
  static PadBinding Get(PadKind kind, PadTarget target)
  {
    return GetPadTable(kind)[static_cast<std::size_t>(target)];
  }
  static std::string Describe(PadKind kind, PadTarget target)
  {
    return DescribeBinding(target, Get(kind, target));
  }

  std::string m_folder;
  bool m_was_wii = false;
};
}  // namespace

TEST(VRWiiController, Names)
{
  EXPECT_EQ(ToChoice(-1), Choice::None);
  EXPECT_EQ(ToChoice(99), Choice::None);
  EXPECT_EQ(ToChoice(3), Choice::RemoteSideways);
  EXPECT_EQ(static_cast<std::size_t>(Choice::Classic) + 1, NAMES.size());
  for (std::size_t i = 0; i < NAMES.size(); ++i)
    EXPECT_EQ(FindChoice(NAMES[i]), ToChoice(static_cast<int>(i)));
  EXPECT_EQ(FindChoice(""), Choice::None);
}

TEST(VRWiiController, ThePlayersChoiceAndTheGamesSettingsAreFirst)
{
  Rules rules;
  EXPECT_EQ(Resolve(rules), Choice::None);
  rules.bundled = Choice::RemoteSideways;
  EXPECT_EQ(Resolve(rules), Choice::RemoteSideways);
  rules.as_set_up = true;
  EXPECT_EQ(Resolve(rules), Choice::None);
  rules.as_set_up = false;
  rules.has_port = true;
  EXPECT_EQ(Resolve(rules), Choice::None);
  rules.own = Choice::GameCube;
  EXPECT_EQ(Resolve(rules), Choice::GameCube);
  rules.has_profile = true;
  EXPECT_EQ(Resolve(rules), Choice::None);
}

TEST(VRWiiController, BundledIsByTheWholeID)
{
  Common::IniFile ini;
  ini.GetOrCreateSection("Controllers")->Set("RMGE01", std::string("Wii Remote + Nunchuk"));
  EXPECT_EQ(FindBundled(ini, "RMGE01"), Choice::RemoteNunchuk);
  EXPECT_EQ(FindBundled(ini, "RMGE52"), Choice::None);
  EXPECT_EQ(FindBundled(ini, "RMG"), Choice::None);
  EXPECT_EQ(FindBundled(Common::IniFile{}, "RMGE01"), Choice::None);
}

TEST(VRWiiControllerSys, BundledGamesAreKnown)
{
  if (!HasSys())
    GTEST_SKIP() << "the Sys folder is not where this binary looks";
  Values titles;
  std::ifstream db(File::GetSysDirectory() + "wiitdb-en.txt");
  for (std::string line; std::getline(db, line);)
  {
    if (const auto at = line.find(" = "); at != std::string::npos)
      titles[line.substr(0, at)] = line.substr(at + 3);
  }
  ASSERT_FALSE(titles.empty());

  Common::IniFile ini;
  ASSERT_TRUE(ini.Load(File::GetSysDirectory() + "WiiControllers.ini"));
  const auto& values = ini.GetOrCreateSection("Controllers")->GetValues();
  EXPECT_FALSE(values.empty());
  for (const auto& [id, name] : values)
  {
    EXPECT_EQ(id.size(), 6u) << id;
    EXPECT_TRUE(titles.contains(id)) << id;
    const Choice choice = FindChoice(name);
    EXPECT_TRUE(choice != Choice::None && choice != Choice::GameCube) << id << ' ' << name;
  }
}

TEST(VRWiiControllerSys, EveryLineOfAProfileIsUsed)
{
  if (!HasSys())
    GTEST_SKIP() << "the Sys folder is not where this binary looks";
  for (const Choice choice : WII_CHOICES)
  {
    const std::string path = GetStock("Wiimote", GetName(choice));
    Common::IniFile ini;
    ASSERT_TRUE(ini.Load(path));
    WiimoteEmu::Wiimote remote(0);
    remote.LoadConfig(ini.GetOrCreateSection("Profile"));
    const Values saved = Save(&remote);
    for (const auto& [key, value] : LoadProfile(path))
    {
      // Those are what there is without it, which is not written.
      if (value == "None" || key == "IR/Relative Input")
        continue;
      const auto it = saved.find(key);
      if (it == saved.end())
        ADD_FAILURE() << GetName(choice) << ": " << key;
      else if (!key.ends_with("Calibration"))
        EXPECT_EQ(it->second, value) << GetName(choice) << ": " << key;
    }
  }
}

TEST(VRWiiControllerSys, DefaultsAreTheProfiles)
{
  if (!HasSys())
    GTEST_SKIP() << "the Sys folder is not where this binary looks";
  const auto compare = [](ControllerEmu::EmulatedController* controller, const std::string& path) {
    controller->LoadDefaults(g_controller_interface);
    const Values defaults = Save(controller);
    Common::IniFile ini;
    ASSERT_TRUE(ini.Load(path));
    controller->LoadConfig(ini.GetOrCreateSection("Profile"));
    EXPECT_EQ(defaults, Save(controller));
  };
  GCPad pad(0);
  compare(&pad, GetStock("GCPad", "GameCube"));
  WiimoteEmu::Wiimote remote(0);
  compare(&remote, GetStock("Wiimote", "Wii Remote + Nunchuk"));
}

TEST_F(VRWiiControllerMapping, EveryControlOfEveryKindIsMapped)
{
  constexpr std::array KINDS{PadKind::WiiRemote, PadKind::WiiNunchuk, PadKind::WiiRemoteSideways,
                             PadKind::WiiClassic};
  Start("GTST01");
  for (std::size_t i = 0; i < KINDS.size(); ++i)
  {
    Choose(WII_CHOICES[i]);
    EXPECT_EQ(Current(), WII_CHOICES[i]);
    const auto [kind, labels] = GetPadBindings(true);
    EXPECT_EQ(kind, KINDS[i]);
    EXPECT_EQ(GetPadEditLock(kind), "");
    for (const PadTarget target : GetTargets(kind))
    {
      const PadBinding binding = Get(kind, target);
      EXPECT_EQ(binding.custom, "") << GetTargetName(target);
      EXPECT_EQ(binding.names.empty(), target == PadTarget::Home) << GetTargetName(target);
      const auto control = static_cast<std::size_t>(GetTargetControl(target));
      EXPECT_FALSE(labels.text[control].empty()) << GetTargetName(target);
    }
  }
}

TEST_F(VRWiiControllerMapping, ClassicControllerIsByPosition)
{
  Start("GTST01");
  Choose(Choice::Classic);
  constexpr PadKind KIND = PadKind::WiiClassic;
  EXPECT_EQ(Describe(KIND, PadTarget::A), "B");
  EXPECT_EQ(Describe(KIND, PadTarget::B), "A");
  EXPECT_EQ(Describe(KIND, PadTarget::X), "Y");
  EXPECT_EQ(Describe(KIND, PadTarget::Y), "X");
  EXPECT_EQ(Describe(KIND, PadTarget::L), "Left Trigger");
  EXPECT_EQ(Describe(KIND, PadTarget::R), "Right Trigger");
  EXPECT_EQ(Describe(KIND, PadTarget::ZL), "Left Bumper");
  EXPECT_EQ(Describe(KIND, PadTarget::ZR), "Right Bumper");
  EXPECT_EQ(Describe(KIND, PadTarget::LeftStick), "Left Stick");
  EXPECT_EQ(Describe(KIND, PadTarget::RightStick), "Right Stick");
}

TEST_F(VRWiiControllerMapping, AChoiceLastsAndTheMappingOfEveryGameStays)
{
  const std::string before = EveryGame();
  ASSERT_FALSE(before.empty());
  Start("GTST01");
  EXPECT_EQ(Current(), Choice::RemoteNunchuk);
  for (const Choice choice : WII_CHOICES)
  {
    Choose(choice);
    EXPECT_EQ(Current(), choice);
    EXPECT_EQ(GetOwnMapping(), choice == Choice::RemoteNunchuk ? Choice::None : choice);
    // As a window that is opened and closed.
    Wiimote::LoadConfig();
    Wiimote::GetConfig()->SaveConfig();
    EXPECT_EQ(Current(), choice);
    EXPECT_EQ(EveryGame(), before);
    EXPECT_FALSE(File::Exists(GetUserProfile(choice)));
    EndGame();
    EXPECT_EQ(GetOwnMapping(), Choice::None);
    Wiimote::GetConfig()->SaveConfig();
    EXPECT_EQ(EveryGame(), before);

    Start("GTST01");
    EXPECT_EQ(Current(), choice);
    // One title starts another.
    Start("GTST02");
    EXPECT_EQ(Current(), Choice::RemoteNunchuk);
    EXPECT_EQ(GetOwnMapping(), Choice::None);
    Start("GTST01");
  }
  Choose(Choice::GameCube);
  EXPECT_EQ(Current(), Choice::GameCube);
  EXPECT_EQ(Config::Get(Config::WIIMOTE_1_SOURCE), WiimoteSource::None);
  EXPECT_EQ(Config::Get(Config::GetInfoForSIDevice(0)), SerialInterface::SIDEVICE_GC_CONTROLLER);
  Choose(Choice::Classic);
  EXPECT_EQ(Current(), Choice::Classic);
  EXPECT_EQ(Config::Get(Config::WIIMOTE_1_SOURCE), WiimoteSource::OpenXR);
  EXPECT_EQ(Config::Get(Config::GetInfoForSIDevice(0)), SerialInterface::SIDEVICE_NONE);
  EXPECT_EQ(EveryGame(), before);
}

TEST_F(VRWiiControllerMapping, AnEditIsOfItsKind)
{
  constexpr PadKind KIND = PadKind::WiiClassic;
  const std::string before = EveryGame();
  Start("GTST01");
  Choose(Choice::Classic);
  PadTable table = GetPadTable(KIND);
  const auto at = [&table](PadTarget target) -> PadBinding& {
    return table[static_cast<std::size_t>(target)];
  };
  std::swap(at(PadTarget::A), at(PadTarget::B));
  at(PadTarget::L) = {{"Left Grip"}, {}};
  at(PadTarget::R) = {{"Left Trigger"}, {}};
  std::swap(at(PadTarget::LeftStick), at(PadTarget::RightStick));
  std::vector<std::pair<PadTarget, PadBinding>> bindings;
  for (const PadTarget target : {PadTarget::A, PadTarget::B, PadTarget::L, PadTarget::R,
                                 PadTarget::LeftStick, PadTarget::RightStick})
  {
    bindings.emplace_back(target, at(target));
  }
  SetPadBindings(KIND, bindings);
  EXPECT_EQ(EveryGame(), before);

  // As Qt's window has them.
  const Values profile = LoadProfile(GetUserProfile(Choice::Classic));
  EXPECT_EQ(profile.at("Classic/Buttons/A"), "`A`");
  EXPECT_EQ(profile.at("Classic/Triggers/L"), "`Left Grip`");
  EXPECT_EQ(profile.at("Classic/Triggers/L-Analog"), "`Left Grip`");
  EXPECT_EQ(profile.at("Classic/Triggers/R"), "`Left Trigger Click`");
  EXPECT_EQ(profile.at("Classic/Triggers/R-Analog"), "`Left Trigger`");
  EXPECT_EQ(profile.at("Classic/Left Stick/Up"), "`Right Stick Up`");

  // Another game with this controller, and then another controller.
  EndGame();
  Start("GTST02");
  Choose(Choice::Classic);
  EXPECT_EQ(GetPadTable(KIND), table);
  Choose(Choice::RemoteSideways);
  EXPECT_EQ(Describe(PadKind::WiiRemoteSideways, PadTarget::Two), "A");
  Choose(Choice::Classic);
  EXPECT_EQ(GetPadTable(KIND), table);

  ResetPadBindings(KIND);
  EXPECT_FALSE(File::Exists(GetUserProfile(Choice::Classic)));
  EXPECT_EQ(Describe(KIND, PadTarget::A), "B");
  EXPECT_EQ(EveryGame(), before);
}

TEST_F(VRWiiControllerMapping, AnEditOfTheMappingOfEveryGameIsOfEveryGame)
{
  constexpr PadKind KIND = PadKind::WiiNunchuk;
  const std::string before = EveryGame();
  Start("GTST01");
  SetPadBindings(KIND, {{PadTarget::C, PadBinding{{"Left Grip"}, {}}}});
  EXPECT_TRUE(EveryGame().contains("Nunchuk/Buttons/C = `Left Grip`"));
  EXPECT_FALSE(File::Exists(GetUserProfile(Choice::RemoteNunchuk)));
  ResetPadBindings(KIND);
  EXPECT_EQ(EveryGame(), before);
}

TEST_F(VRWiiControllerMapping, AnotherExtensionInAWindowDoesNotReachOtherMappings)
{
  const std::string before = EveryGame();
  Start("GTST01");
  Choose(Choice::RemoteSideways);
  static_cast<ControllerEmu::Attachments*>(
      Wiimote::GetWiimoteGroup(0, WiimoteEmu::WiimoteGroup::Attachments))
      ->SetSelectedAttachment(WiimoteEmu::ExtensionNumber::CLASSIC);
  Wiimote::GetConfig()->SaveConfig();
  EXPECT_EQ(EveryGame(), before);
  EXPECT_FALSE(File::Exists(GetUserProfile(Choice::RemoteSideways)));
  EXPECT_FALSE(File::Exists(GetUserProfile(Choice::Classic)));
  EXPECT_EQ(Current(), Choice::Classic);
  // What the host job does.
  Choose(Choice::Classic);
  EXPECT_EQ(GetOwnMapping(), Choice::Classic);
  EXPECT_EQ(Describe(PadKind::WiiClassic, PadTarget::A), "B");
  EXPECT_EQ(EveryGame(), before);
}

TEST_F(VRWiiControllerMapping, AFileOfAnotherKindDoesNotChangeTheController)
{
  Start("GTST01");
  Write(GetUserProfile(Choice::Classic),
        "[Profile]\nDevice = OpenXR/0/OpenXR Controller\nExtension = Nunchuk\n"
        "Options/Sideways Wiimote = True\nClassic/Buttons/A = `Left Grip`\n");
  Write(GetUserProfile(Choice::Remote), "");
  Choose(Choice::Classic);
  EXPECT_EQ(Current(), Choice::Classic);
  EXPECT_EQ(Describe(PadKind::WiiClassic, PadTarget::A), "Left Grip");
  Choose(Choice::Remote);
  EXPECT_EQ(Current(), Choice::Remote);
  EXPECT_EQ(Describe(PadKind::WiiRemote, PadTarget::A), "B");
}

TEST_F(VRWiiControllerMapping, BundledDefault)
{
  Start("SUKE01");
  EXPECT_EQ(Current(), Choice::RemoteSideways);
  EXPECT_EQ(Config::Get(VR_WII_CONTROLLER), Choice::None);
  EXPECT_FALSE(File::Exists(File::GetUserPath(D_GAMESETTINGS_IDX) + "SUKE01.ini"));
  Choose(Choice::Remote);
  EXPECT_EQ(Current(), Choice::Remote);
  EndGame();
  Start("SUKE01");
  EXPECT_EQ(Current(), Choice::Remote);
  EndGame();
  Start("RMGE52");
  EXPECT_EQ(Current(), Choice::RemoteNunchuk);
  EXPECT_EQ(GetOwnMapping(), Choice::None);
  EndGame();

  // With another device the mapping is the player's.
  Wiimote::GetConfig()->GetController(0)->SetDefaultDevice("SDL/0/Gamepad");
  Wiimote::GetConfig()->SaveConfig();
  Start("SF8E01");
  EXPECT_EQ(Current(), Choice::RemoteNunchuk);
  EXPECT_EQ(GetOwnMapping(), Choice::None);
}

TEST_F(VRWiiControllerMapping, AProfileOfTheGameIsInFront)
{
  Write(Wiimote::GetConfig()->GetUserProfileDirectoryPath() + "Mine.ini",
        "[Profile]\nDevice = OpenXR/0/OpenXR Controller\nExtension = Guitar\n");
  Write(File::GetUserPath(D_GAMESETTINGS_IDX) + "SUKE01.ini",
        "[Controls]\nWiimoteProfile1 = Mine\n");
  Start("SUKE01");
  EXPECT_EQ(Current(), Choice::None);
  EXPECT_EQ(GetOwnMapping(), Choice::None);
  EXPECT_NE(GetPadEditLock(PadKind::WiiRemote), "");
}

TEST_F(VRWiiControllerMapping, AGameHasItsOwnMappingOfAController)
{
  constexpr PadKind KIND = PadKind::WiiRemoteSideways;
  const std::string before = EveryGame();
  EXPECT_NE(GetPadScopeLock(KIND), "");
  SetPadScope(KIND, PadScope::Game);
  EXPECT_EQ(GetPadScope(KIND), PadScope::Global);

  Start("GTST01");
  Choose(Choice::RemoteSideways);
  EXPECT_EQ(GetPadScopeLock(KIND), "");
  EXPECT_EQ(GetPadScope(KIND), PadScope::Global);
  const PadTable global = GetPadTable(KIND);
  SetScope(KIND, PadScope::Game);
  const std::string path = GetGameProfile("Wiimote", "GTST01 Wii Remote (Sideways)");
  EXPECT_TRUE(File::Exists(path));
  // It starts as the one of every game.
  EXPECT_EQ(GetPadTable(KIND), global);
  EXPECT_EQ(Current(), Choice::RemoteSideways);

  SetPadBindings(KIND, {{PadTarget::One, PadBinding{{"Left Bumper"}, {}}}});
  EXPECT_EQ(LoadProfile(path).at("Buttons/1"), "`Left Bumper`");
  EXPECT_FALSE(File::Exists(GetUserProfile(Choice::RemoteSideways)));
  EXPECT_EQ(EveryGame(), before);

  // Another controller of this game has the mapping of every game, and this one keeps its own.
  Choose(Choice::Classic);
  EXPECT_EQ(GetPadScope(PadKind::WiiClassic), PadScope::Global);
  Choose(Choice::RemoteSideways);
  EXPECT_EQ(GetPadScope(KIND), PadScope::Game);
  EXPECT_EQ(Describe(KIND, PadTarget::One), "Left Bumper");

  // Another game has not.
  EndGame();
  Start("GTST02");
  Choose(Choice::RemoteSideways);
  EXPECT_EQ(GetPadScope(KIND), PadScope::Global);
  EXPECT_EQ(GetPadTable(KIND), global);
  EndGame();
  EXPECT_EQ(GetPadScope(KIND), PadScope::Global);

  // The Steam Frame profile is for the game's own, and "Use Global Values" removes it.
  Start("GTST01");
  EXPECT_EQ(GetPadScope(KIND), PadScope::Game);
  EXPECT_EQ(Describe(KIND, PadTarget::One), "Left Bumper");
  ResetPadBindings(KIND);
  EXPECT_EQ(GetPadTable(KIND), global);
  EXPECT_TRUE(File::Exists(path));
  SetPadBindings(KIND, {{PadTarget::One, PadBinding{{"Left Bumper"}, {}}}});
  SetScope(KIND, PadScope::Global);
  EXPECT_FALSE(File::Exists(path));
  EXPECT_EQ(GetPadTable(KIND), global);
  EXPECT_EQ(EveryGame(), before);
}

TEST_F(VRWiiControllerMapping, AGameHasItsOwnMappingOfTheOneOfEveryGame)
{
  constexpr PadKind KIND = PadKind::WiiNunchuk;
  const std::string before = EveryGame();
  Start("GTST01");
  EXPECT_EQ(Current(), Choice::RemoteNunchuk);
  SetScope(KIND, PadScope::Game);
  SetPadBindings(KIND, {{PadTarget::C, PadBinding{{"Left Grip"}, {}}}});
  EXPECT_EQ(EveryGame(), before);
  // As a window that is opened and closed.
  Wiimote::LoadConfig();
  Wiimote::GetConfig()->SaveConfig();
  EXPECT_EQ(Describe(KIND, PadTarget::C), "Left Grip");
  EXPECT_EQ(EveryGame(), before);
  EndGame();
  EXPECT_EQ(Describe(KIND, PadTarget::C), "Left Bumper");
  Wiimote::GetConfig()->SaveConfig();
  EXPECT_EQ(EveryGame(), before);
  Start("GTST01");
  EXPECT_EQ(Describe(KIND, PadTarget::C), "Left Grip");
  SetScope(KIND, PadScope::Global);
  EXPECT_EQ(Describe(KIND, PadTarget::C), "Left Bumper");
  EXPECT_EQ(EveryGame(), before);
}

TEST_F(VRWiiControllerMapping, AGameHasItsOwnMappingOfTheGameCubeController)
{
  constexpr PadKind KIND = PadKind::GameCube;
  const std::string before = EveryGame("GCPadNew.ini");
  ASSERT_FALSE(before.empty());
  Core::System::GetInstance().SetIsWii(false);
  EXPECT_NE(GetPadScopeLock(KIND), "");
  Start("GTST01");
  EXPECT_EQ(GetPadScopeLock(KIND), "");
  const PadTable global = GetPadTable(KIND);
  SetScope(KIND, PadScope::Game);
  const std::string path = GetGameProfile("GCPad", "GTST01 GameCube Controller");
  EXPECT_TRUE(File::Exists(path));
  EXPECT_EQ(GetPadTable(KIND), global);
  SetPadBindings(KIND, {{PadTarget::Z, PadBinding{{"Left Grip"}, {}}}});
  EXPECT_EQ(LoadProfile(path).at("Buttons/Z"), "`Left Grip`");
  EXPECT_EQ(EveryGame("GCPadNew.ini"), before);
  Pad::LoadConfig();
  Pad::GetConfig()->SaveConfig();
  EXPECT_EQ(EveryGame("GCPadNew.ini"), before);
  EXPECT_EQ(Describe(KIND, PadTarget::Z), "Left Grip");

  EndGame();
  EXPECT_EQ(GetPadTable(KIND), global);
  Start("GTST02");
  EXPECT_EQ(GetPadScope(KIND), PadScope::Global);
  EXPECT_EQ(GetPadTable(KIND), global);
  // An edit of the one of every game does not reach the game's own.
  SetPadBindings(KIND, {{PadTarget::Z, PadBinding{{"Left Trigger"}, {}}}});
  EXPECT_NE(EveryGame("GCPadNew.ini"), before);
  EndGame();
  Start("GTST01");
  EXPECT_EQ(Describe(KIND, PadTarget::Z), "Left Grip");
  ResetPadBindings(KIND);
  EXPECT_EQ(GetPadTable(KIND), global);
  EXPECT_TRUE(File::Exists(path));
  SetScope(KIND, PadScope::Global);
  EXPECT_FALSE(File::Exists(path));
  EXPECT_EQ(Describe(KIND, PadTarget::Z), "Left Trigger");
}

// Dolphin's own: the game's INI names a profile of the mapping windows.
TEST_F(VRWiiControllerMapping, AProfileOfTheGameIsItsOwnMapping)
{
  constexpr PadKind KIND = PadKind::WiiNunchuk;
  const std::string before = EveryGame();
  const std::string mine = GetGameProfile("Wiimote", "Mine");
  Write(mine, "[Profile]\nDevice = OpenXR/0/OpenXR Controller\nExtension = Nunchuk\n"
              "Nunchuk/Buttons/C = `Left Grip`\n");
  Write(File::GetUserPath(D_GAMESETTINGS_IDX) + "GTST01.ini",
        "[Controls]\nWiimoteProfile1 = Mine\n");
  Start("GTST01");
  EXPECT_EQ(GetPadScope(KIND), PadScope::Game);
  EXPECT_EQ(GetPadProfile(KIND), "Mine");
  EXPECT_EQ(GetPadEditLock(KIND), "");
  EXPECT_EQ(Describe(KIND, PadTarget::C), "Left Grip");

  // An edit, and a window that saves, are the profile's.
  SetPadBindings(KIND, {{PadTarget::NunchukZ, PadBinding{{"Left Bumper"}, {}}}});
  Wiimote::GetConfig()->SaveConfig();
  EXPECT_EQ(LoadProfile(mine).at("Nunchuk/Buttons/Z"), "`Left Bumper`");
  EXPECT_EQ(LoadProfile(mine).at("Nunchuk/Buttons/C"), "`Left Grip`");
  EXPECT_EQ(EveryGame(), before);

  // Another extension in a window is the profile's, too.
  static_cast<ControllerEmu::Attachments*>(
      Wiimote::GetWiimoteGroup(0, WiimoteEmu::WiimoteGroup::Attachments))
      ->SetSelectedAttachment(WiimoteEmu::ExtensionNumber::CLASSIC);
  Wiimote::GetConfig()->SaveConfig();
  EXPECT_EQ(LoadProfile(mine).at("Extension"), "Classic");
  EXPECT_EQ(Current(), Choice::Classic);
  EXPECT_EQ(EveryGame(), before);

  // "Use Global Values": the profile is not this game's any more, and stays.
  SetScope(PadKind::WiiClassic, PadScope::Global);
  EXPECT_EQ(GetPadProfile(KIND), "");
  EXPECT_TRUE(File::Exists(mine));
  EXPECT_EQ(Current(), Choice::RemoteNunchuk);
  EXPECT_EQ(Describe(KIND, PadTarget::C), "Left Bumper");
  EXPECT_EQ(EveryGame(), before);
}

TEST_F(VRWiiControllerMapping, AProfileIsChosenForTheGame)
{
  const std::string before = EveryGame();
  Write(GetGameProfile("Wiimote", "Mine"),
        "[Profile]\nDevice = OpenXR/0/OpenXR Controller\nExtension = Classic\n"
        "Classic/Buttons/A = `Left Grip`\n");
  Start("GTST01");
  // Each page lists its own controller's.
  Write(GetGameProfile("Wiimote", "Guitar"),
        "[Profile]\nDevice = OpenXR/0/OpenXR Controller\nExtension = Guitar\n");
  Write(GetGameProfile("Wiimote", "On Its Side"),
        "[Profile]\nOptions/Sideways Wiimote = True\n");
  Write(GetGameProfile("Wiimote", "VR Wii Remote"), "[Profile]\nExtension = None\n");
  Write(GetGameProfile("Wiimote", "Plain"), "[Profile]\nButtons/A = `A`\n");
  Write(GetGameProfile("Wiimote", "Formula"), "[Profile]\nExtension = `Attached Extension`\n");
  using Names = std::vector<std::string>;
  EXPECT_EQ(GetPadProfiles(PadKind::WiiNunchuk), Names{"Steam Frame Wii Remote + Nunchuk"});
  EXPECT_EQ(GetPadProfiles(PadKind::WiiClassic),
            (Names{"Mine", "Steam Frame Classic Controller"}));
  EXPECT_EQ(GetPadProfiles(PadKind::WiiRemoteSideways),
            (Names{"On Its Side", "Steam Frame Wii Remote (Sideways)"}));
  EXPECT_EQ(GetPadProfiles(PadKind::WiiRemote), (Names{"Plain", "Steam Frame Wii Remote"}));
  EXPECT_TRUE(std::ranges::contains(GetPadProfiles(PadKind::GameCube), "Steam Frame GameCube"));
  EXPECT_EQ(GetPadProfile(PadKind::WiiNunchuk), "");

  SetPadProfile(PadKind::WiiNunchuk, "Mine");
  EXPECT_EQ(GetPadProfile(PadKind::WiiClassic), "Mine");
  EXPECT_EQ(GetPadScope(PadKind::WiiClassic), PadScope::Game);
  EXPECT_EQ(Current(), Choice::Classic);
  EXPECT_EQ(Describe(PadKind::WiiClassic, PadTarget::A), "Left Grip");
  std::string text;
  File::ReadFileToString(File::GetUserPath(D_GAMESETTINGS_IDX) + "GTST01.ini", text);
  EXPECT_TRUE(text.contains("WiimoteProfile1 = Mine"));

  // One that comes with the program: an edit makes the player's of that name.
  SetPadProfile(PadKind::WiiClassic, "Steam Frame Wii Remote (Sideways)");
  EXPECT_EQ(Current(), Choice::RemoteSideways);
  const std::string own = GetGameProfile("Wiimote", "Steam Frame Wii Remote (Sideways)");
  Wiimote::GetConfig()->SaveConfig();
  EXPECT_FALSE(File::Exists(own));
  SetPadBindings(PadKind::WiiRemoteSideways, {{PadTarget::One, PadBinding{{"Left Bumper"}, {}}}});
  EXPECT_EQ(LoadProfile(own).at("Buttons/1"), "`Left Bumper`");

  // The game is started again.
  EndGame();
  EXPECT_EQ(Current(), Choice::RemoteNunchuk);
  Start("GTST01");
  EXPECT_EQ(GetPadProfile(PadKind::WiiRemoteSideways), "Steam Frame Wii Remote (Sideways)");
  EXPECT_EQ(Describe(PadKind::WiiRemoteSideways, PadTarget::One), "Left Bumper");

  // Another controller is chosen: not this profile, and the one the game had with that controller.
  Choose(Choice::Classic);
  EXPECT_EQ(GetPadProfile(PadKind::WiiClassic), "Mine");
  EXPECT_EQ(Describe(PadKind::WiiClassic, PadTarget::A), "Left Grip");
  Choose(Choice::RemoteSideways);
  EXPECT_EQ(GetPadProfile(PadKind::WiiRemoteSideways), "Steam Frame Wii Remote (Sideways)");
  EXPECT_EQ(Describe(PadKind::WiiRemoteSideways, PadTarget::One), "Left Bumper");
  Choose(Choice::GameCube);
  Choose(Choice::RemoteNunchuk);
  EXPECT_EQ(GetPadProfile(PadKind::WiiNunchuk), "");
  EndGame();
  Start("GTST01");
  EXPECT_EQ(Current(), Choice::RemoteNunchuk);
  Choose(Choice::Classic);
  EXPECT_EQ(GetPadProfile(PadKind::WiiClassic), "Mine");
  SetPadProfile(PadKind::WiiClassic, "");
  EXPECT_EQ(GetPadScope(PadKind::WiiClassic), PadScope::Global);
  // None, and it stays none.
  Choose(Choice::RemoteSideways);
  Choose(Choice::Classic);
  EXPECT_EQ(GetPadProfile(PadKind::WiiClassic), "");
  EXPECT_EQ(EveryGame(), before);
}

// What the layer of the game's settings has is written with every one of them.
TEST_F(VRWiiControllerMapping, AProfileThatWasRemovedDoesNotComeBack)
{
  Write(GetGameProfile("Wiimote", "Mine"),
        "[Profile]\nDevice = OpenXR/0/OpenXR Controller\nExtension = Nunchuk\n");
  Write(File::GetUserPath(D_GAMESETTINGS_IDX) + "GTST01.ini",
        "[Controls]\nWiimoteProfile1 = Mine\n");
  Start("GTST01");
  EXPECT_EQ(GetPadProfile(PadKind::WiiNunchuk), "Mine");
  SetScope(PadKind::WiiNunchuk, PadScope::Global);
  GameSettingScope::SetForGame(Config::MAIN_AUTOSAVE_ENABLED, true,
                               GameSettingScope::Store::LocalGame);
  EXPECT_FALSE(GameIni("GTST01").contains("Mine"));
  SetPadProfile(PadKind::WiiNunchuk, "Mine");
  GameSettingScope::SetForGame(Config::MAIN_AUTOSAVE_ENABLED, false,
                               GameSettingScope::Store::LocalGame);
  EXPECT_TRUE(GameIni("GTST01").contains("WiimoteProfile1 = Mine"));
}

// A profile of the player's is not the one Per-Game made.
TEST_F(VRWiiControllerMapping, AProfileNamedAfterTheGameIsNotDeleted)
{
  const std::string mine = GetGameProfile("Wiimote", "GTST01 mine");
  Write(mine, "[Profile]\nDevice = OpenXR/0/OpenXR Controller\nExtension = Nunchuk\n");
  Start("GTST01");
  SetPadProfile(PadKind::WiiNunchuk, "GTST01 mine");
  SetScope(PadKind::WiiNunchuk, PadScope::Global);
  EXPECT_TRUE(File::Exists(mine));
}

// The folder of the games' settings is not there at first.
TEST_F(VRWiiControllerMapping, TheSettingsFolderIsMade)
{
  File::DeleteDirRecursively(File::GetUserPath(D_GAMESETTINGS_IDX));
  Start("GTST01");
  Choose(Choice::Classic);
  EXPECT_TRUE(GameIni("GTST01").contains("WiiController = 4"));
}

// An extension that is none of the menu's, chosen in a window: as Dolphin keeps it.
TEST_F(VRWiiControllerMapping, AnotherExtensionOfTheMappingOfEveryGameIsKept)
{
  Start("GTST01");
  SetExtension(WiimoteEmu::ExtensionNumber::GUITAR);
  EXPECT_TRUE(EveryGame().contains("Extension = Guitar"));
  EXPECT_EQ(Current(), Choice::None);
  EndGame();
  Start("GTST01");
  EXPECT_EQ(GetAttachments()->GetSelectedAttachment(), u32(WiimoteEmu::ExtensionNumber::GUITAR));

  // Also where the player has chosen a controller, and it is the one of every game.
  SetExtension(WiimoteEmu::ExtensionNumber::NUNCHUK);
  Choose(Choice::RemoteNunchuk);
  EXPECT_EQ(GetOwnMapping(), Choice::None);
  SetExtension(WiimoteEmu::ExtensionNumber::GUITAR);
  Wiimote::LoadConfig();
  EXPECT_EQ(GetAttachments()->GetSelectedAttachment(), u32(WiimoteEmu::ExtensionNumber::GUITAR));
  EndGame();
  Start("GTST01");
  EXPECT_EQ(GetAttachments()->GetSelectedAttachment(), u32(WiimoteEmu::ExtensionNumber::GUITAR));

  // And with one of the menu's again.
  Choose(Choice::Classic);
  EXPECT_EQ(Current(), Choice::Classic);
  EXPECT_TRUE(EveryGame().contains("Extension = Guitar"));
}

TEST_F(VRWiiControllerMapping, AnotherExtensionOfAProfileIsTheGames)
{
  const std::string before = EveryGame();
  for (const bool bundled : {false, true})
  {
    const std::string id = bundled ? "SUKE01" : "GTST01";
    Start(id);
    if (!bundled)
      Choose(Choice::RemoteSideways);
    EXPECT_EQ(Current(), Choice::RemoteSideways);
    SetExtension(WiimoteEmu::ExtensionNumber::GUITAR);
    EXPECT_EQ(GetPadProfile(PadKind::WiiRemote), id + " Guitar");
    EXPECT_FALSE(File::Exists(GetUserProfile(Choice::RemoteSideways)));
    EXPECT_EQ(EveryGame(), before);
    EndGame();
    Start(id);
    EXPECT_EQ(Current(), Choice::None);
    EXPECT_EQ(GetAttachments()->GetSelectedAttachment(), u32(WiimoteEmu::ExtensionNumber::GUITAR));
    // An edit of it is its own.
    SetExtension(WiimoteEmu::ExtensionNumber::DRUMS);
    EXPECT_EQ(LoadProfile(GetGameProfile("Wiimote", id + " Guitar")).at("Extension"), "Drums");
    EXPECT_EQ(EveryGame(), before);
    Choose(Choice::RemoteSideways);
    EXPECT_EQ(Current(), Choice::RemoteSideways);
    EXPECT_EQ(GetPadProfile(PadKind::WiiRemote), "");
    EndGame();
  }
}

TEST_F(VRWiiControllerMapping, TheBundledControllerIsTold)
{
  Start("RK5E01");
  EXPECT_TRUE(TakeNotice().contains("Wii Remote (Sideways)"));
  Wiimote::LoadConfig();
  EXPECT_EQ(TakeNotice(), "");
  EndGame();
  Start("SUKE01");
  EndGame();
  // Of another game than the one that runs.
  Start("GTST01");
  EXPECT_EQ(TakeNotice(), "");
}

TEST_F(VRWiiControllerMapping, AProfileThatIsNotThere)
{
  const std::string before = EveryGame();
  Write(File::GetUserPath(D_GAMESETTINGS_IDX) + "GTST01.ini",
        "[Controls]\nWiimoteProfile1 = Gone\n");
  Start("GTST01");
  EXPECT_EQ(Current(), Choice::RemoteNunchuk);
  EXPECT_EQ(GetPadScope(PadKind::WiiNunchuk), PadScope::Global);
  Wiimote::GetConfig()->SaveConfig();
  EXPECT_EQ(EveryGame(), before);
}
