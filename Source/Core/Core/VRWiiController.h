// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <string>
#include <string_view>

#include "Common/IniFile.h"

namespace Config
{
template <typename T>
class Info;
}

namespace Core
{
class System;
}

// The controller a Wii game has, kept per game. A Wii Remote has a mapping for each way it is held
// and each extension: the one of every game (WiimoteNew.ini) if that is for it, and else one in a
// profile of its own, so that the mapping of every game stays what it is.
namespace VRWiiController
{
// Kept as numbers in the games' settings: new ones go to the end.
enum class Choice : int
{
  None = -1,  // as it is set up
  GameCube,
  Remote,
  RemoteNunchuk,
  RemoteSideways,
  Classic,
};

// In the order of Choice, as the menu and Data/Sys/WiiControllers.ini have them.
constexpr std::array NAMES{"GameCube Controller", "Wii Remote", "Wii Remote + Nunchuk",
                           "Wii Remote (Sideways)", "Classic Controller"};

extern const Config::Info<Choice> VR_WII_CONTROLLER;

// None for a number that is not a choice.
Choice ToChoice(int number);
// By its name, or None.
Choice FindChoice(std::string_view name);
// What Data/Sys/WiiControllers.ini has for a game.
Choice FindBundled(const Common::IniFile& bundled, std::string_view game_id);

// What decides which controller a Wii game has.
struct Rules
{
  Choice own = Choice::None;  // the player's, for this game
  bool has_profile = false;   // its settings name a profile of the Wii Remote
  bool has_port = false;      // its settings name the first port's device or the Wii Remote's source
  Choice bundled = Choice::None;
};
// None: as it is set up for every game.
Choice Resolve(const Rules& rules);

// What the running game has. Host thread.
Choice GetCurrent(Core::System& system);

// Not with NetPlay, a movie or a controller profile named by the game's settings.
bool CanChoose(Core::System& system);

// Changes the emulated hardware of the running game, now and when it is started again. Host thread.
void Choose(Core::System& system, Choice choice);

// Without a choice, a Wii game played with a Wii Remote has no GameCube Controller on the same
// buttons, until the game ends. Host thread.
void OnSessionStarted(Core::System& system);

// Loads the mapping of every game, or the one the running game has. Any thread.
void LoadMapping();

// An InputConfig::SaveOverride: saves the mapping of one choice into its profile.
bool SaveOwnMapping(int index, Common::IniFile::Section* section);

// The choice whose own mapping the Wii Remote has, or None: it has the one of every game.
Choice GetOwnMapping();
// Extension and Sideways as that choice has them, in a mapping that is about to be loaded.
void KeepChoice(int index, Common::IniFile::Section* section);
// The Steam Frame profile for a Wii Remote: a file name in the Sys profiles.
std::string GetDefaultProfile(int index);

// The Wii Remote's mapping is to be the Steam Frame profile. Host thread.
void ResetMapping();
}  // namespace VRWiiController
