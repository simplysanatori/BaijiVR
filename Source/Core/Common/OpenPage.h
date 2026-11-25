// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

// Opens a web page in another program (Moonlight XR's way): in Steam's Game Mode Steam's own browser,
// which SteamVR shows in its dashboard, else what the desktop opens pages with. Linux only.
namespace Common::OpenPage
{
enum class Opener
{
  Nothing,
  Steam,  // steam steam://openurl/<url>
  Xdg,    // xdg-open <url>
};

// An https address without anything odd in it: nothing else is ever opened.
bool IsPage(std::string_view url);
// For this PATH: `steam` only in Game Mode.
Opener ChooseOpener(std::string_view path, bool game_mode);
// Empty: not with this opener, or not this address.
std::vector<std::string> MakeArguments(Opener opener, std::string_view url);
// Without what baiji-vr sets for itself: another program has its own libraries and Qt plugins.
std::vector<std::string> CleanEnvironment(const char* const* environment);

// Steam's Game Mode runs under gamescope.
bool IsGameMode();
// Any thread but the XR pacing thread. Does not wait for the program. Nothing: nothing was started.
Opener Open(const std::string& url);
}  // namespace Common::OpenPage
