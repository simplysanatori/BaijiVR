// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <picojson.h>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Core/ActionReplay.h"
#include "Core/ConfigManager.h"
#include "Core/GameMods/GameMods.h"
#include "Core/GameMods/GameModsInternal.h"
#include "Core/GeckoCodeConfig.h"
#include "Core/PatchEngine.h"

namespace fs = std::filesystem;
namespace Internal = GameMods::Internal;

namespace
{
constexpr const char* GAME = "GFEE01";

std::string Entry(const std::string& id, const std::string& extra, const std::string& game = GAME)
{
  return R"({"id":")" + id + R"(","name":"Name )" + id + R"(","title":"Game","game_ids":[")" + game +
         R"("],"author":"Someone","licence":"Not stated","homepage":"https://example.com/x",)"
         R"("notes":"A note.",)" +
         extra + "}";
}

std::string Catalog(const std::vector<std::string>& mods, const char* key = "mods")
{
  std::string list;
  for (const auto& m : mods)
    list += (list.empty() ? "" : ",") + m;
  return std::string(R"({"schema":1,")") + key + R"(":[)" + list + "]}";
}

// A zip with stored (uncompressed) files, written by hand so that any name can be put in it.
std::string MakeZip(const std::vector<std::pair<std::string, std::string>>& files)
{
  const auto crc = [](const std::string& s) {
    u32 c = 0xFFFFFFFF;
    for (const unsigned char b : s)
    {
      c ^= b;
      for (int i = 0; i < 8; ++i)
        c = (c >> 1) ^ (0xEDB88320 & (0 - (c & 1)));
    }
    return ~c;
  };
  const auto le = [](std::string& out, u32 v, int bytes) {
    for (int i = 0; i < bytes; ++i)
      out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
  };
  std::string data, central;
  for (const auto& [name, content] : files)
  {
    const u32 offset = static_cast<u32>(data.size());
    le(data, 0x04034b50, 4);
    le(data, 10, 2);
    le(data, 0, 2);
    le(data, 0, 2);
    le(data, 0, 4);
    le(data, crc(content), 4);
    le(data, static_cast<u32>(content.size()), 4);
    le(data, static_cast<u32>(content.size()), 4);
    le(data, static_cast<u32>(name.size()), 2);
    le(data, 0, 2);
    data += name + content;
    le(central, 0x02014b50, 4);
    le(central, 20, 2);
    le(central, 10, 2);
    le(central, 0, 2);
    le(central, 0, 2);
    le(central, 0, 4);
    le(central, crc(content), 4);
    le(central, static_cast<u32>(content.size()), 4);
    le(central, static_cast<u32>(content.size()), 4);
    le(central, static_cast<u32>(name.size()), 2);
    le(central, 0, 2);
    le(central, 0, 2);
    le(central, 0, 2);
    le(central, 0, 2);
    le(central, name.back() == '/' ? 0x10 : 0, 4);
    le(central, offset, 4);
    central += name;
  }
  std::string zip = data + central;
  le(zip, 0x06054b50, 4);
  le(zip, 0, 2);
  le(zip, 0, 2);
  le(zip, static_cast<u32>(files.size()), 2);
  le(zip, static_cast<u32>(files.size()), 2);
  le(zip, static_cast<u32>(central.size()), 4);
  le(zip, static_cast<u32>(data.size()), 4);
  le(zip, 0, 2);
  return zip;
}

class GameModsTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    m_old_user = File::GetUserPath(D_USER_IDX);
    m_dir = File::CreateTempDir();
    ASSERT_FALSE(m_dir.empty());
    File::SetUserPath(D_USER_IDX, m_dir + DIR_SEP);
    Internal::ResetCatalogCache();
  }

  void TearDown() override
  {
    Internal::ResetCatalogCache();
    File::SetUserPath(D_USER_IDX, m_old_user);
    File::DeleteDirRecursively(m_dir);
  }

  void SetCatalog(GameMods::List list, const std::string& json)
  {
    const std::string path = Internal::CatalogCachePath(list);
    File::CreateFullPath(path);
    ASSERT_TRUE(File::WriteStringToFile(path, json));
    Internal::ResetCatalogCache();
  }

  static Common::IniFile Ini()
  {
    Common::IniFile ini;
    ini.Load(File::GetUserPath(D_GAMESETTINGS_IDX) + GAME + ".ini");
    return ini;
  }

  static void WriteIni(const std::string& text)
  {
    File::CreateFullPath(File::GetUserPath(D_GAMESETTINGS_IDX));
    ASSERT_TRUE(File::WriteStringToFile(File::GetUserPath(D_GAMESETTINGS_IDX) + GAME + ".ini", text));
  }

  static std::string Value(const char* section, const char* key)
  {
    std::string value = "<none>";
    Ini().GetIfExists(section, key, &value);
    return value;
  }

  static GameMods::State StateOf(GameMods::List list, const std::string& id, u16 revision = 0)
  {
    for (const auto& item : GameMods::GetItems(list, GAME, revision))
    {
      if (item.id == id)
        return item.state;
    }
    ADD_FAILURE() << "no item " << id;
    return GameMods::State::Available;
  }

  std::string m_old_user;
  std::string m_dir;
};
}  // namespace

TEST(GameModsParse, GoodAndBadEntries)
{
  const std::string good = Entry(
      "good", R"("kind":"codes","category":"fix","code_type":"gecko","lines":["04000000 00000001"],)"
              R"("revisions":[0],"verified":true,"settings":[{"section":"Video_Hacks","key":"Anything","value":"True"}])");
  const std::string bad_setting = Entry(
      "bad-setting", R"("kind":"settings","category":"fix","settings":[{"section":"Core","key":"EnableCheats","value":"True"}])");
  const std::string bad_line = Entry("bad-line", R"("kind":"codes","category":"fix","code_type":"gecko","lines":["0400 1"])");
  const std::string bad_section = Entry("bad-section", R"("kind":"settings","category":"fix","settings":[{"section":"Gecko","key":"a","value":"b"}])");
  const std::string unknown = Entry("unknown", R"("kind":"settings","category":"fix","settings":[{"section":"Core","key":"CPUThread","value":"True"}],"extra":1)");
  const std::string widescreen = Entry("wide", R"("kind":"settings","category":"widescreen","settings":[{"section":"Core","key":"MMU","value":"True"}])");
  const std::string not_https = Entry("riiv", R"("kind":"riivolution","category":"mod","xml":"riivolution/a.xml","source":{"type":"https","url":"http://example.com/a.zip"})");
  const std::string upper = Entry("Upper", R"("kind":"settings","category":"fix","settings":[{"section":"Core","key":"MMU","value":"True"}])");
  const std::string bad_game = Entry("bad-game", R"("kind":"settings","category":"fix","settings":[{"section":"Core","key":"MMU","value":"True"}])", "gfee01");
  const std::string onframe = Entry("frame", R"("kind":"codes","category":"fix","code_type":"onframe","lines":["0x80001234:dword:0x00000001"])");

  const auto parsed = Internal::ParseCatalog(
      GameMods::List::Mods,
      Catalog({good, bad_setting, bad_line, bad_section, unknown, widescreen, not_https, upper, bad_game, good, onframe}));
  ASSERT_TRUE(parsed);
  ASSERT_EQ(parsed->size(), 2u);  // good (the second one is a duplicate) and onframe
  EXPECT_EQ((*parsed)[0].id, "good");
  EXPECT_TRUE((*parsed)[0].verified);
  EXPECT_EQ((*parsed)[0].revisions, std::vector<u16>{0});
  EXPECT_EQ((*parsed)[1].id, "frame");

  EXPECT_FALSE(Internal::ParseCatalog(GameMods::List::Mods, "not json"));
  EXPECT_FALSE(Internal::ParseCatalog(GameMods::List::Mods, R"({"schema":2,"mods":[]})"));
  EXPECT_FALSE(Internal::ParseCatalog(GameMods::List::Textures, Catalog({}, "mods")));
  EXPECT_TRUE(Internal::ParseCatalog(GameMods::List::Textures, Catalog({}, "packs")));
}

TEST(GameModsParse, TextureEntries)
{
  const std::string pack = Entry(
      "pack", R"("source":{"type":"github_release","repo":"a/b","tag":"latest","asset_pattern":"*.zip"},)"
              R"("size":100,"format":"zip","layout":"single","textures":"png")");
  const std::string no_format = Entry("no-format", R"("source":{"type":"gamebanana","mod":1},"textures":"png")");
  const std::string link = Entry("link", R"("source":{"type":"link"})");
  const std::string http = Entry("http", R"("source":{"type":"https","url":"http://x.y/z"},"format":"zip","textures":"png")");
  const auto parsed = Internal::ParseCatalog(GameMods::List::Textures, Catalog({pack, no_format, link, http}, "packs"));
  ASSERT_TRUE(parsed);
  ASSERT_EQ(parsed->size(), 2u);
  EXPECT_TRUE((*parsed)[0].Downloads());
  EXPECT_FALSE((*parsed)[0].LinkOnly());
  EXPECT_TRUE((*parsed)[1].LinkOnly());
}

TEST(GameModsParse, Helpers)
{
  EXPECT_TRUE(Internal::GlobMatch("Name-*.zip", "name-1.2.ZIP"));
  EXPECT_FALSE(Internal::GlobMatch("Name-*.zip", "Name-1.7z"));
  EXPECT_TRUE(Internal::GlobMatch("riivolution/*.xml", "riivolution/a.xml"));
  EXPECT_EQ(Internal::SafeRelativePath("a\\b/./c"), "a/b/c");
  EXPECT_FALSE(Internal::SafeRelativePath("../a"));
  EXPECT_FALSE(Internal::SafeRelativePath("a/../../b"));
  EXPECT_FALSE(Internal::SafeRelativePath("/etc/passwd"));
  EXPECT_FALSE(Internal::SafeRelativePath("C:/x"));
  EXPECT_FALSE(Internal::SafeRelativePath(""));
}

TEST_F(GameModsTest, RevisionFilterAndOrder)
{
  const auto base = R"("kind":"settings","category":"fix","settings":[{"section":"Core","key":"MMU","value":"True"}])";
  SetCatalog(GameMods::List::Mods,
             Catalog({Entry("zeta", base), Entry("alpha", base), Entry("rev1", std::string(base) + R"(,"revisions":[1])"),
                      Entry("pick", std::string(base) + R"(,"recommended":true)"), Entry("other", base, "GALE01")}));
  // Without the built-in patches, which are there when Sys is.
  const auto get = [](u16 revision) {
    auto listed = GameMods::GetItems(GameMods::List::Mods, GAME, revision);
    std::erase_if(listed, [](const auto& item) { return item.category == "patch"; });
    return listed;
  };
  const auto items = get(0);
  ASSERT_EQ(items.size(), 3u);
  EXPECT_EQ(items[0].id, "pick");
  EXPECT_EQ(items[1].id, "alpha");
  EXPECT_EQ(items[2].id, "zeta");
  EXPECT_EQ(get(1).size(), 4u);
  EXPECT_TRUE(GameMods::GetItems(GameMods::List::Mods, "bad", 0).empty());
}

TEST_F(GameModsTest, CodesAndSettingsAreUndone)
{
  SetCatalog(GameMods::List::Mods,
             Catalog({Entry("wide", R"("kind":"codes","category":"widescreen","aspect":"16:9","code_type":"gecko",)"
                                    R"("lines":["04000000 00000001","04000004 00000002"],)"
                                    R"("settings":[{"section":"Video_Settings","key":"SuggestedAspectRatio","value":"1"},)"
                                    R"({"section":"Video_Hacks","key":"EFBToTextureEnable","value":"False"}])"),
                      Entry("ar", R"("kind":"codes","category":"fix","code_type":"ar","lines":["04000008 00000003"])"),
                      Entry("frame", R"("kind":"codes","category":"fix","code_type":"onframe","lines":["0x80001234:dword:0x00000001"])")}));
  WriteIni("[Video_Settings]\nSuggestedAspectRatio = 3\n[Video_Hacks]\nOther = 5\n[Gecko]\n$Mine [me]\n04000010 00000000\n[Gecko_Enabled]\n$Mine\n");

  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "wide", true).ok);
  EXPECT_EQ(StateOf(GameMods::List::Mods, "wide"), GameMods::State::On);
  EXPECT_EQ(Value("Video_Settings", "SuggestedAspectRatio"), "1");
  EXPECT_EQ(Value("Video_Hacks", "EFBToTextureEnable"), "False");
  EXPECT_EQ(Value("Core", "EnableCheats"), "True");
  {
    auto ini = Ini();
    std::vector<std::string> lines;
    ini.GetLines("Gecko", &lines);
    ASSERT_GE(lines.size(), 5u);
    EXPECT_EQ(lines[0], "$Mine [me]");
    EXPECT_EQ(lines[2], "$Name wide [Someone]");
    ini.GetLines("Gecko_Enabled", &lines);
    EXPECT_EQ(lines, (std::vector<std::string>{"$Mine", "$Name wide"}));
  }
  // A second mod needs cheats on as well: they stay on until the last one goes.
  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "ar", true).ok);
  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "frame", true).ok);
  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "wide", false).ok);
  EXPECT_EQ(StateOf(GameMods::List::Mods, "wide"), GameMods::State::Off);
  EXPECT_EQ(Value("Core", "EnableCheats"), "True");
  EXPECT_EQ(Value("Video_Settings", "SuggestedAspectRatio"), "3");
  EXPECT_EQ(Value("Video_Hacks", "EFBToTextureEnable"), "<none>");
  EXPECT_EQ(Value("Video_Hacks", "Other"), "5");
  {
    auto ini = Ini();
    std::vector<std::string> lines;
    ini.GetLines("Gecko", &lines);
    EXPECT_EQ(lines, (std::vector<std::string>{"$Mine [me]", "04000010 00000000"}));
    ini.GetLines("ActionReplay_Enabled", &lines);
    EXPECT_EQ(lines, std::vector<std::string>{"$Name ar"});
    ini.GetLines("OnFrame_Enabled", &lines);
    EXPECT_EQ(lines, std::vector<std::string>{"$Name frame"});
  }
  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "ar", false).ok);
  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "frame", false).ok);
  EXPECT_EQ(Value("Core", "EnableCheats"), "<none>");
  EXPECT_EQ(StateOf(GameMods::List::Mods, "frame"), GameMods::State::Off);
  std::vector<std::string> lines;
  Ini().GetLines("ActionReplay", &lines);
  EXPECT_TRUE(lines.empty());
}

TEST_F(GameModsTest, PlayersOwnCodeAndSettingStay)
{
  SetCatalog(GameMods::List::Mods,
             Catalog({Entry("code", R"("kind":"codes","category":"fix","code_type":"gecko","lines":["04000000 00000001"],)"
                                    R"("settings":[{"section":"Core","key":"MMU","value":"True"}])")}));
  // The player has the same code, switched off, and EnableCheats on already.
  WriteIni("[Core]\nEnableCheats = True\n[Gecko]\n$Name code [Someone]\n04000000 00000001\n");
  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "code", true).ok);
  EXPECT_EQ(StateOf(GameMods::List::Mods, "code"), GameMods::State::On);
  // The player changes a setting while the mod is on: it is theirs now.
  {
    auto ini = Ini();
    ini.GetOrCreateSection("Core")->Set("MMU", std::string("False"));
    ini.Save(File::GetUserPath(D_GAMESETTINGS_IDX) + GAME + ".ini");
  }
  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "code", false).ok);
  EXPECT_EQ(Value("Core", "MMU"), "False");
  EXPECT_EQ(Value("Core", "EnableCheats"), "True");
  auto ini = Ini();
  std::vector<std::string> lines;
  ini.GetLines("Gecko", &lines);
  EXPECT_EQ(lines.size(), 2u);  // the player's code is still there
  EXPECT_FALSE(ini.Exists("Gecko_Enabled", "$Name code"));
  EXPECT_EQ(StateOf(GameMods::List::Mods, "code"), GameMods::State::Off);

  // A different code under the same name is refused.
  SetCatalog(GameMods::List::Mods,
             Catalog({Entry("code", R"("kind":"codes","category":"fix","code_type":"gecko","lines":["04000000 00000009"])")}));
  const auto result = GameMods::SetEnabled(GameMods::List::Mods, GAME, "code", true);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.error.empty());
}

TEST_F(GameModsTest, BuiltinCodeNeedsTheCode)
{
  SetCatalog(GameMods::List::Mods,
             Catalog({Entry("builtin", R"("kind":"builtin","category":"fix","code_type":"gecko","code_name":"No Such Code")")}));
  EXPECT_FALSE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "builtin", true).ok);
  EXPECT_FALSE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "missing", true).ok);
}

TEST_F(GameModsTest, ZipRefusesUnsafeNames)
{
  const std::string zip = m_dir + "/ok.zip";
  ASSERT_TRUE(File::WriteStringToFile(zip, MakeZip({{"a/", ""}, {"a/b.txt", "hello"}, {"c.txt", "x"}})));
  std::atomic_bool cancel{false};
  ASSERT_EQ(Internal::ExtractZip(zip, m_dir + "/out", cancel), "");
  std::string content;
  ASSERT_TRUE(File::ReadFileToString(m_dir + "/out/a/b.txt", content));
  EXPECT_EQ(content, "hello");

  for (const char* name : {"../evil.txt", "a/../../evil.txt", "/abs/evil.txt", "..\\evil.txt"})
  {
    const std::string bad = m_dir + "/bad.zip";
    ASSERT_TRUE(File::WriteStringToFile(bad, MakeZip({{"fine.txt", "x"}, {name, "evil"}})));
    EXPECT_NE(Internal::ExtractZip(bad, m_dir + "/bad-out", cancel), "") << name;
    EXPECT_FALSE(File::Exists(m_dir + "/evil.txt")) << name;
    EXPECT_FALSE(File::Exists(m_dir + "/bad-out/fine.txt")) << "nothing is written from a bad archive";
  }

  ASSERT_TRUE(File::WriteStringToFile(m_dir + "/junk.bin", "not an archive at all"));
  EXPECT_NE(Internal::ExtractArchive(m_dir + "/junk.bin", m_dir + "/junk-out", cancel), "");
}

TEST_F(GameModsTest, TexturePackOnAndOff)
{
  SetCatalog(GameMods::List::Textures,
             Catalog({Entry("pack", R"("source":{"type":"https","url":"https://example.com/a.zip"},"format":"zip","textures":"png")")},
                     "packs"));
  EXPECT_EQ(StateOf(GameMods::List::Textures, "pack"), GameMods::State::Available);
  EXPECT_FALSE(GameMods::SetEnabled(GameMods::List::Textures, GAME, "pack", true).ok);

  const std::string zip = m_dir + "/pack.zip";
  ASSERT_TRUE(File::WriteStringToFile(
      zip, MakeZip({{"Wrapper/", ""}, {"Wrapper/gameids/", ""}, {"Wrapper/gameids/GFEE01.txt", ""},
                    {"Wrapper/gameids/GFEP01.txt", ""}, {"Wrapper/tex1_64x64_abcd_9.png", "png"}})));
  std::atomic_bool cancel{false};
  const std::string work = Internal::WorkDir(GameMods::List::Textures, "pack");
  ASSERT_EQ(Internal::ExtractZip(zip, work, cancel), "");
  const auto entries = Internal::LoadEntries(GameMods::List::Textures);
  ASSERT_EQ(entries.size(), 1u);
  ASSERT_EQ(Internal::FinishTexturePack(work, entries[0]), "");
  EXPECT_FALSE(File::Exists(work));

  const std::string dir = Internal::TextureDir("pack");
  EXPECT_TRUE(File::Exists(dir + "/tex1_64x64_abcd_9.png"));  // the wrapper is gone
  EXPECT_FALSE(File::Exists(dir + "/gameids/GFEE01.txt"));    // nothing is on yet
  EXPECT_TRUE(File::Exists(dir + "/gameids/GFEE01.off"));
  EXPECT_EQ(StateOf(GameMods::List::Textures, "pack"), GameMods::State::Off);

  WriteIni("[Video_Settings]\nHiresTextures = False\n");
  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Textures, GAME, "pack", true).ok);
  EXPECT_TRUE(File::Exists(dir + "/gameids/GFEE01.txt"));
  EXPECT_FALSE(File::Exists(dir + "/gameids/GFEP01.txt"));
  EXPECT_EQ(Value("Video_Settings", "HiresTextures"), "True");
  EXPECT_EQ(StateOf(GameMods::List::Textures, "pack"), GameMods::State::On);

  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Textures, GAME, "pack", false).ok);
  EXPECT_FALSE(File::Exists(dir + "/gameids/GFEE01.txt"));
  EXPECT_EQ(Value("Video_Settings", "HiresTextures"), "False");  // the player's own value is back
  EXPECT_EQ(StateOf(GameMods::List::Textures, "pack"), GameMods::State::Off);

  ASSERT_TRUE(GameMods::Remove(GameMods::List::Textures, GAME, "pack").ok);
  EXPECT_FALSE(File::Exists(dir));
  EXPECT_EQ(StateOf(GameMods::List::Textures, "pack"), GameMods::State::Available);
}

TEST_F(GameModsTest, RiivolutionList)
{
  SetCatalog(GameMods::List::Mods,
             Catalog({Entry("riiv", R"("kind":"riivolution","category":"mod","xml":"riivolution/*.xml",)"
                                    R"("source":{"type":"https","url":"https://example.com/a.zip"})")}));
  EXPECT_TRUE(GameMods::GetEnabledRiivolutionXmls(GAME).empty());
  EXPECT_EQ(StateOf(GameMods::List::Mods, "riiv"), GameMods::State::Available);
  EXPECT_FALSE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "riiv", true).ok);

  const std::string zip = m_dir + "/mod.zip";
  ASSERT_TRUE(File::WriteStringToFile(zip, MakeZip({{"riivolution/mod.xml", "<wiidisc/>"}, {"files/a.bin", "x"}})));
  std::atomic_bool cancel{false};
  const std::string work = Internal::WorkDir(GameMods::List::Mods, "riiv");
  ASSERT_EQ(Internal::ExtractZip(zip, work, cancel), "");
  ASSERT_EQ(Internal::FinishRiivolution(work, Internal::LoadEntries(GameMods::List::Mods)[0]), "");
  EXPECT_EQ(StateOf(GameMods::List::Mods, "riiv"), GameMods::State::Off);
  EXPECT_TRUE(GameMods::GetEnabledRiivolutionXmls(GAME).empty());

  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "riiv", true).ok);
  EXPECT_EQ(StateOf(GameMods::List::Mods, "riiv"), GameMods::State::On);
  EXPECT_EQ(GameMods::GetEnabledRiivolutionXmls(GAME),
            std::vector<std::string>{Internal::RiivolutionDir("riiv") + "/riivolution/mod.xml"});
  EXPECT_TRUE(GameMods::GetEnabledRiivolutionXmls("GALE01").empty());

  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "riiv", false).ok);
  EXPECT_TRUE(GameMods::GetEnabledRiivolutionXmls(GAME).empty());
  // A bad XML must not stop a boot: the patches list is simply empty.
  ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, GAME, "riiv", true).ok);
  EXPECT_TRUE(Internal::GetRiivolutionPatches(GAME, 0, 0).empty());
}

TEST_F(GameModsTest, NothingRunsWithoutStart)
{
  EXPECT_EQ(GameMods::GetProgress().phase, GameMods::Progress::Phase::Idle);
  EXPECT_FALSE(GameMods::StartInstall(GameMods::List::Mods, GAME, "missing"));
  GameMods::CancelInstall();
  GameMods::Shutdown();
  GameMods::Shutdown();
}

TEST_F(GameModsTest, SeparateArchivesAreMerged)
{
  std::atomic_bool cancel{false};
  const auto unpack = [&](const std::string& name, const std::vector<std::pair<std::string, std::string>>& files) {
    ASSERT_TRUE(File::WriteStringToFile(m_dir + "/" + name + ".zip", MakeZip(files)));
    ASSERT_EQ(Internal::ExtractZip(m_dir + "/" + name + ".zip", m_dir + "/w/" + name, cancel), "");
  };
  // Different wrappers: each loses its own.
  unpack("p0", {{"Main/", ""}, {"Main/a/", ""}, {"Main/a/1.dds", "1"}, {"Main/a/same.dds", "old"}});
  unpack("p1", {{"Extra Part/", ""}, {"Extra Part/a/", ""}, {"Extra Part/a/2.dds", "2"}, {"Extra Part/a/same.dds", "new"}});
  ASSERT_EQ(Internal::MergeParts({m_dir + "/w/p0", m_dir + "/w/p1"}, m_dir + "/m1", true), "");
  std::string content;
  EXPECT_TRUE(File::Exists(m_dir + "/m1/1.dds"));  // the chain of wrapper folders is gone
  EXPECT_TRUE(File::Exists(m_dir + "/m1/2.dds"));
  ASSERT_TRUE(File::ReadFileToString(m_dir + "/m1/same.dds", content));
  EXPECT_EQ(content, "new");  // later files win

  // Paths that already line up are left alone.
  unpack("q0", {{"x/", ""}, {"x/1.dds", "1"}});
  unpack("q1", {{"x/", ""}, {"x/2.dds", "2"}});
  ASSERT_EQ(Internal::MergeParts({m_dir + "/w/q0", m_dir + "/w/q1"}, m_dir + "/m2", true), "");
  EXPECT_TRUE(File::Exists(m_dir + "/m2/x/1.dds"));
  EXPECT_TRUE(File::Exists(m_dir + "/m2/x/2.dds"));
}

TEST(GameModsSys, ShippedCatalogueHasNoBadEntries)
{
  if (!File::Exists(File::GetSysDirectory() + "GameMods/mods.json"))
    GTEST_SKIP() << "the Sys folder is not where this binary looks";
  for (const auto list : {GameMods::List::Mods, GameMods::List::Textures})
  {
    const bool mods = list == GameMods::List::Mods;
    std::string text;
    ASSERT_TRUE(File::ReadFileToString(File::GetSysDirectory() + "GameMods/" + (mods ? "mods.json" : "textures.json"), text));
    picojson::value root;
    ASSERT_TRUE(picojson::parse(root, text).empty());
    const size_t expected = root.get<picojson::object>().at(mods ? "mods" : "packs").get<picojson::array>().size();
    const auto parsed = Internal::ParseCatalog(list, text);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->size(), expected);
  }
}

// A patch that Sys has on by default, switched as the Mods tab does and read as a boot does.
TEST_F(GameModsTest, BuiltInPatchesAreListed)
{
  if (!File::Exists(File::GetSysDirectory() + "GameMods/mods.json"))
    GTEST_SKIP() << "the Sys folder is not where this binary looks";
  const std::string game = "GFEE01";
  const std::string id = "patch-gpu-hang-watchdog-off";
  const auto state = [&] {
    for (const auto& item : GameMods::GetItems(GameMods::List::Mods, game, 0))
    {
      if (item.id == id)
        return item.state;
    }
    return GameMods::State::Available;
  };
  const auto boots_with_it = [&] {
    std::vector<PatchEngine::Patch> patches;
    PatchEngine::LoadPatchSection("OnFrame", &patches, SConfig::LoadDefaultGameIni(game, 0),
                                  SConfig::LoadLocalGameIni(game, 0));
    return std::ranges::any_of(patches, [](const auto& p) { return p.enabled; });
  };
  EXPECT_EQ(state(), GameMods::State::On);
  EXPECT_TRUE(boots_with_it());
  EXPECT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, game, id, false).ok);
  EXPECT_EQ(state(), GameMods::State::Off);
  EXPECT_FALSE(boots_with_it());
  EXPECT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, game, id, true).ok);
  EXPECT_EQ(state(), GameMods::State::On);
  EXPECT_TRUE(boots_with_it());
  EXPECT_FALSE(GameMods::SetEnabled(GameMods::List::Mods, game, "patch-none", true).ok);
}

// Built-in codes of the shipped catalogue against the real Sys INIs, read back the way a boot does.
TEST_F(GameModsTest, BuiltinCodesOfTheShippedCatalogue)
{
  if (!File::Exists(File::GetSysDirectory() + "GameMods/mods.json"))
    GTEST_SKIP() << "the Sys folder is not where this binary looks";
  size_t checked = 0;
  for (const auto& e : Internal::LoadEntries(GameMods::List::Mods))
  {
    if (e.kind != "builtin")
      continue;
    for (const auto& game : e.game_ids)
    {
      std::vector<std::optional<u16>> revisions;
      for (const u16 r : e.revisions)
        revisions.push_back(r);
      if (revisions.empty())
        revisions.push_back(std::nullopt);
      const auto enabled = [&](const std::optional<u16>& revision) {
        const Common::IniFile global = SConfig::LoadDefaultGameIni(game, revision);
        const Common::IniFile local = SConfig::LoadLocalGameIni(game, revision);
        const std::string name = e.code_name;
        if (e.code_type == "gecko")
        {
          for (const auto& c : Gecko::LoadCodes(global, local))
          {
            if ((c.name == name || c.name.starts_with(name + " [")) && c.enabled)
              return true;
          }
        }
        else if (e.code_type == "ar")
        {
          for (const auto& c : ActionReplay::LoadCodes(global, local))
          {
            if ((c.name == name || c.name.starts_with(name + " [")) && c.enabled)
              return true;
          }
        }
        else
        {
          std::vector<PatchEngine::Patch> patches;
          PatchEngine::LoadPatchSection("OnFrame", &patches, global, local);
          for (const auto& c : patches)
          {
            if ((c.name == name || c.name.starts_with(name + " [")) && c.enabled)
              return true;
          }
        }
        return false;
      };
      const auto result = GameMods::SetEnabled(GameMods::List::Mods, game, e.id, true);
      ASSERT_TRUE(result.ok) << e.id << " " << game << ": " << result.error;
      for (const auto& revision : revisions)
        EXPECT_TRUE(enabled(revision)) << e.id << " " << game;
      ASSERT_TRUE(GameMods::SetEnabled(GameMods::List::Mods, game, e.id, false).ok);
      for (const auto& revision : revisions)
        EXPECT_FALSE(enabled(revision)) << e.id << " " << game << " off";
      ++checked;
    }
  }
  EXPECT_GT(checked, 0u);
}
