// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Common/Buffer.h"
#include "Common/FileUtil.h"
#include "Common/Image.h"
#include "UICommon/GameCovers.h"

using namespace UICommon;

namespace
{
CoverQuery Query(std::string file, std::string title = {})
{
  return {.game_id = "RSBE01",
          .region_code = "US",
          .is_wii = true,
          .region = DiscIO::Region::NTSC_U,
          .file_name = std::move(file),
          .title = std::move(title)};
}
}  // namespace

TEST(GameCovers, NamesFromFile)
{
  const auto names = LibretroNames(Query("Kirby's Epic Yarn (USA) (En,Fr,Es) (Rev 2).nkit.iso"));
  const std::vector<std::string> expected{"Kirby's Epic Yarn (USA) (En,Fr,Es) (Rev 2)",
                                          "Kirby's Epic Yarn (USA) (En,Fr,Es)",
                                          "Kirby's Epic Yarn (USA)"};
  EXPECT_EQ(names, expected);
}

TEST(GameCovers, NamesFromTitle)
{
  const auto names = LibretroNames(Query("brawl.rvz", "Fire Emblem: Radiant Dawn"));
  ASSERT_EQ(names.size(), 3u);
  EXPECT_EQ(names[0], "Fire Emblem - Radiant Dawn (USA)");
  EXPECT_EQ(LibretroNames(Query("x.iso", "Who? / What"))[0], "Who_ _ What (USA)");
  EXPECT_TRUE(LibretroNames(Query("x.iso")).empty());
}

TEST(GameCovers, Urls)
{
  const auto urls = CoverUrls(Query("A & B (USA).rvz"));
  EXPECT_EQ(urls[0], "https://art.gametdb.com/wii/cover/US/RSBE01.png");
  EXPECT_EQ(std::ranges::count_if(urls, [](const auto& u) { return u.contains("gametdb"); }), 2);
  EXPECT_EQ(urls.back(), "https://raw.githubusercontent.com/libretro-thumbnails/Nintendo_-_Wii/"
                         "master/Named_Boxarts/A%20_%20B%20%28USA%29.png");
}

TEST(GameCovers, Links)
{
  EXPECT_EQ(*LinkTarget("https://h/a/b.png", "C D.png"), "https://h/a/C%20D.png");
  EXPECT_FALSE(LinkTarget("https://h/a/b.png", "../C.png"));
  EXPECT_FALSE(LinkTarget("https://h/a/b.png", "404: Not Found"));
}

TEST(GameCovers, Store)
{
  const std::string dir = File::CreateTempDir();
  const std::string source = dir + "/in.png";
  const std::vector<u8> pixels(512 * 719 * 4, 200);
  ASSERT_TRUE(Common::SavePNG(source, pixels.data(), Common::ImageByteFormat::RGBA, 512, 719,
                              512 * 4));
  std::string png;
  ASSERT_TRUE(File::ReadFileToString(source, png));
  const std::span<const u8> bytes{reinterpret_cast<const u8*>(png.data()), png.size()};
  ASSERT_TRUE(StoreCover(bytes, dir + "/out.png"));

  std::string out;
  ASSERT_TRUE(File::ReadFileToString(dir + "/out.png", out));
  Common::UniqueBuffer<u8> image;
  u32 width = 0;
  u32 height = 0;
  ASSERT_TRUE(Common::LoadPNG({reinterpret_cast<const u8*>(out.data()), out.size()}, &image,
                              &width, &height));
  EXPECT_EQ(width, 256u);
  EXPECT_EQ(height, 359u);
  EXPECT_EQ(image[0], 200);

  EXPECT_FALSE(StoreCover(bytes.first(40), dir + "/bad.png"));
  EXPECT_FALSE(StoreCover({}, dir + "/bad.png"));
  EXPECT_FALSE(File::Exists(dir + "/bad.png"));
  File::DeleteDirRecursively(dir);
}
