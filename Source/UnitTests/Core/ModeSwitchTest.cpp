// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Core/ModeSwitch.h"

using ModeSwitch::Answer;
using ModeSwitch::Displays;
using ModeSwitch::MenuHold;
using ModeSwitch::Mode;

namespace
{
constexpr auto FLAT = OpenXRPresentationMode::Vulkan;
constexpr auto IMMERSIVE = OpenXRPresentationMode::Immersive;
constexpr auto SCREEN = OpenXRPresentationMode::StereoScreen;
constexpr auto LEGACY = OpenXRPresentationMode::Legacy;
constexpr s64 MS = 1'000'000;
}  // namespace

TEST(ModeSwitch, RequestNeedsAHandler)
{
  ModeSwitch::SetHandler(nullptr);
  EXPECT_FALSE(ModeSwitch::Request(Mode::Flat));
  std::vector<Mode> asked;
  ModeSwitch::SetHandler([&asked](Mode mode) { asked.push_back(mode); });
  EXPECT_TRUE(ModeSwitch::Request(Mode::Flat));
  EXPECT_TRUE(ModeSwitch::Request(Mode::VR));
  EXPECT_EQ(asked, (std::vector<Mode>{Mode::Flat, Mode::VR}));
  ModeSwitch::SetHandler(nullptr);
}

TEST(ModeSwitch, ThereAndBackIsTheSameDisplay)
{
  for (const auto display : {IMMERSIVE, SCREEN, LEGACY})
  {
    const Displays before{display, FLAT};
    const Displays flat = ModeSwitch::GoFlat(before, display);
    EXPECT_EQ(flat, (Displays{FLAT, display}));
    EXPECT_TRUE(ModeSwitch::IsFlatForAll(flat));
    EXPECT_EQ(ModeSwitch::GoVR(flat), before);
  }
  EXPECT_FALSE(ModeSwitch::IsFlatForAll({IMMERSIVE, FLAT}));
  // Flat chosen in the settings: a game's own display still counts.
  EXPECT_FALSE(ModeSwitch::IsFlatForAll({FLAT, FLAT}));
}

TEST(ModeSwitch, GameOfItsOwnDisplayWithFlatInTheSettings)
{
  const Displays flat = ModeSwitch::GoFlat({FLAT, FLAT}, IMMERSIVE);
  EXPECT_EQ(flat, (Displays{FLAT, IMMERSIVE}));
  EXPECT_EQ(ModeSwitch::GoVR(flat), (Displays{IMMERSIVE, FLAT}));
}

TEST(ModeSwitch, NothingToGoBackTo)
{
  EXPECT_EQ(ModeSwitch::GoVR({FLAT, FLAT}), (Displays{SCREEN, FLAT}));
  EXPECT_EQ(ModeSwitch::GoFlat({FLAT, FLAT}, FLAT), (Displays{FLAT, SCREEN}));
  // Once more is no other one.
  EXPECT_EQ(ModeSwitch::GoFlat({FLAT, IMMERSIVE}, FLAT), (Displays{FLAT, IMMERSIVE}));
  // A display chosen in the settings is kept.
  EXPECT_EQ(ModeSwitch::GoVR({IMMERSIVE, SCREEN}), (Displays{IMMERSIVE, FLAT}));
}

TEST(ModeSwitch, FlatIsHeldOverAGamesOwnDisplay)
{
  const Displays flat{FLAT, SCREEN};
  EXPECT_EQ(ModeSwitch::Correct(Mode::Flat, IMMERSIVE, flat, FLAT), FLAT);
  EXPECT_EQ(ModeSwitch::Correct(Mode::Flat, SCREEN, flat, FLAT), FLAT);
  EXPECT_EQ(ModeSwitch::Correct(Mode::Flat, FLAT, flat, FLAT), std::nullopt);
  // The OpenXR hotkey, and a display the player chooses meanwhile.
  EXPECT_EQ(ModeSwitch::Correct(Mode::Flat, LEGACY, flat, FLAT), std::nullopt);
  EXPECT_EQ(ModeSwitch::Correct(Mode::Flat, IMMERSIVE, {IMMERSIVE, SCREEN}, FLAT), std::nullopt);
}

TEST(ModeSwitch, TheWayBackHasTheGamesDisplay)
{
  const Displays vr{SCREEN, FLAT};
  EXPECT_EQ(ModeSwitch::Correct(Mode::VR, SCREEN, vr, IMMERSIVE), IMMERSIVE);
  EXPECT_EQ(ModeSwitch::Correct(Mode::VR, IMMERSIVE, vr, IMMERSIVE), std::nullopt);
  // Once it runs, it may be changed, but not to Flat.
  EXPECT_EQ(ModeSwitch::Correct(Mode::VR, IMMERSIVE, vr, FLAT), std::nullopt);
  EXPECT_EQ(ModeSwitch::Correct(Mode::VR, SCREEN, vr, FLAT), std::nullopt);
  EXPECT_EQ(ModeSwitch::Correct(Mode::VR, FLAT, vr, FLAT), SCREEN);
}

TEST(ModeSwitch, WholeState)
{
  const std::string path = File::CreateTempDir() + "/a.sav";
  const auto write = [&path](u64 says, size_t has, u16 compression, u32 lzo = 0) {
    State::StateHeaderLegacy legacy{};
    legacy.lzo_size = lzo;
    const std::string name = "a version";
    const State::StateHeaderVersion version{0xBAADBABE, static_cast<u32>(name.size())};
    const State::StateExtendedBaseHeader base{1, compression, 0, says};
    File::IOFile file(path, "wb");
    file.WriteArray(&legacy, 1);
    file.WriteArray(&version, 1);
    file.WriteString(name);
    file.WriteArray(&base, 1);
    file.WriteString(std::string(has, 'x'));
  };
  EXPECT_FALSE(ModeSwitch::IsWholeState(path));
  write(1000, 1000, State::CompressionType::Uncompressed);
  EXPECT_TRUE(ModeSwitch::IsWholeState(path));
  write(1000, 999, State::CompressionType::Uncompressed);
  EXPECT_FALSE(ModeSwitch::IsWholeState(path));
  write(0, 0, State::CompressionType::Uncompressed);
  EXPECT_FALSE(ModeSwitch::IsWholeState(path));
  write(1000, 1000, State::CompressionType::LZ4);
  EXPECT_FALSE(ModeSwitch::IsWholeState(path));
  write(1000, 1000, State::CompressionType::Uncompressed, 8);
  EXPECT_FALSE(ModeSwitch::IsWholeState(path));
  File::CreateEmptyFile(path);
  EXPECT_FALSE(ModeSwitch::IsWholeState(path));
  File::Delete(path);
  File::DeleteDir(path.substr(0, path.size() - 6));
}

TEST(ModeSwitch, MenuHeldForThreeSeconds)
{
  MenuHold hold;
  EXPECT_FALSE(hold.Update(false, false, 0));
  EXPECT_FALSE(hold.Update(true, false, 100 * MS));
  EXPECT_FALSE(hold.Update(true, false, 3099 * MS));
  EXPECT_TRUE(hold.Update(true, false, 3100 * MS));
  // Once for a press.
  EXPECT_FALSE(hold.Update(true, false, 9000 * MS));
  EXPECT_FALSE(hold.Update(false, false, 9100 * MS));
  EXPECT_FALSE(hold.Update(true, false, 9200 * MS));
  EXPECT_TRUE(hold.Update(true, false, 12200 * MS));
}

TEST(ModeSwitch, MenuLetGoOfStartsAnew)
{
  MenuHold hold;
  hold.Update(false, false, 0);
  EXPECT_FALSE(hold.Update(true, false, 0));
  EXPECT_FALSE(hold.Update(true, false, 2900 * MS));
  EXPECT_FALSE(hold.Update(false, false, 2950 * MS));
  EXPECT_FALSE(hold.Update(true, false, 3000 * MS));
  EXPECT_FALSE(hold.Update(true, false, 5900 * MS));
  EXPECT_TRUE(hold.Update(true, false, 6000 * MS));
}

TEST(ModeSwitch, MenuHeldOverFromBeforeOrBlockedDoesNotCount)
{
  MenuHold hold;
  EXPECT_FALSE(hold.Update(true, false, 0));
  EXPECT_FALSE(hold.Update(true, false, 5000 * MS));
  hold.Update(false, false, 5100 * MS);

  EXPECT_FALSE(hold.Update(true, true, 6000 * MS));
  EXPECT_FALSE(hold.Update(true, false, 9500 * MS));
  hold.Update(false, false, 9600 * MS);
  EXPECT_FALSE(hold.Update(true, false, 10000 * MS));
  EXPECT_FALSE(hold.Update(true, true, 11000 * MS));
  EXPECT_FALSE(hold.Update(true, false, 14000 * MS));

  hold.Reset();
  EXPECT_FALSE(hold.Update(true, false, 20000 * MS));
  EXPECT_FALSE(hold.Update(true, false, 24000 * MS));
}

TEST(ModeSwitch, AnsweredAsTheButtonIsLetGoOf)
{
  Answer answer;
  EXPECT_EQ(answer.Update(false, false), Answer::Result::None);
  EXPECT_EQ(answer.Update(true, false), Answer::Result::None);
  EXPECT_EQ(answer.Update(true, false), Answer::Result::None);
  EXPECT_EQ(answer.Update(false, false), Answer::Result::Yes);
  EXPECT_EQ(answer.Update(false, false), Answer::Result::None);
  EXPECT_EQ(answer.Update(false, true), Answer::Result::None);
  EXPECT_EQ(answer.Update(false, false), Answer::Result::No);
  // The first one counts, and both together are no Yes.
  EXPECT_EQ(answer.Update(false, true), Answer::Result::None);
  EXPECT_EQ(answer.Update(true, false), Answer::Result::None);
  EXPECT_EQ(answer.Update(false, false), Answer::Result::No);
  EXPECT_EQ(answer.Update(true, true), Answer::Result::None);
  EXPECT_EQ(answer.Update(false, false), Answer::Result::No);
}

TEST(ModeSwitch, WhatIsHeldAsTheQuestionOpensIsNoAnswer)
{
  Answer answer;
  EXPECT_EQ(answer.Update(true, false), Answer::Result::None);
  EXPECT_EQ(answer.Update(false, false), Answer::Result::None);
  EXPECT_EQ(answer.Update(true, false), Answer::Result::None);
  EXPECT_EQ(answer.Update(false, false), Answer::Result::Yes);
}
