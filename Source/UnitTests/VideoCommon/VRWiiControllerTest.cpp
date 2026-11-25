// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

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
    config->SetSaveOverride(SaveOwnMapping);
    auto& system = Core::System::GetInstance();
    m_was_wii = system.IsWii();
    system.SetIsWii(true);
    Wiimote::LoadConfig();
    config->SaveConfig();
  }

  void TearDown() override
  {
    if (m_folder.empty())
      return;
    EndGame();
    Wiimote::GetConfig()->SetSaveOverride(nullptr);
    Wiimote::GetConfig()->ClearControllers();
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
  }
  static void EndGame()
  {
    Config::ClearCurrentRunLayer();
    Config::RemoveLayer(Config::LayerType::GlobalGame);
    Config::RemoveLayer(Config::LayerType::LocalGame);
    SConfig::GetInstance().ResetRunningGameMetadata();
    Wiimote::LoadConfig();
  }
  static void Choose(Choice choice)
  {
    VRWiiController::Choose(Core::System::GetInstance(), choice);
  }
  static Choice Current() { return GetCurrent(Core::System::GetInstance()); }
  static std::string EveryGame()
  {
    std::string text;
    File::ReadFileToString(File::GetUserPath(D_CONFIG_IDX) + "WiimoteNew.ini", text);
    return text;
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
  ini.GetOrCreateSection("Controllers")->Set("RMGE01", "Wii Remote + Nunchuk");
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
  EXPECT_FALSE(CanChoose(Core::System::GetInstance()));
  EXPECT_EQ(Current(), Choice::None);
  EXPECT_EQ(GetOwnMapping(), Choice::None);
  EXPECT_NE(GetPadEditLock(PadKind::WiiRemote), "");
}
