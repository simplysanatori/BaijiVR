// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

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
  bool as_set_up = false;     // the player's is an extension that is not a choice
  Choice bundled = Choice::None;
};
// None: as it is set up for every game.
Choice Resolve(const Rules& rules);

// What the running game has. Host thread.
Choice GetCurrent(Core::System& system);

// Not with NetPlay or a movie.
bool CanChoose(Core::System& system);

// Changes the emulated hardware of the running game, now and when it is started again. A profile its
// settings name is of one controller: it is kept for that one, and is the game's again with it. Host
// thread.
void Choose(Core::System& system, Choice choice);

// Without a choice, a Wii game played with a Wii Remote has no GameCube Controller on the same
// buttons, until the game ends. Host thread.
void OnSessionStarted(Core::System& system);

// What the player is to be told of the running game's controller, once: Data/Sys/WiiControllers.ini
// chose it. Or empty. Any thread.
std::string TakeNotice();

// Loads the mapping of every game, or the one the running game has. Any thread.
void LoadMapping();

// The first Wii Remote and the first GameCube Controller have one of these mappings:
// - the one of every game (WiimoteNew.ini, GCPadNew.ini),
// - for a Wii Remote that the one of every game is not for (another extension, sideways): the
//   profile "VR <controller>.ini" of every game, which is the Steam Frame profile until it is
//   changed,
// - the running game's own: the profile its settings name, as Dolphin has it ([Controls]
//   PadProfile1 or WiimoteProfile1 in the game's INI; a profile of the User or the Sys folder).
//   "Per-Game" makes one, "<game ID> <controller>.ini", from the mapping the game has, and so does
//   an extension that is not a choice, if it is chosen in a window while the game has a profile.
// Whoever saves the mapping (the VR menu, a mapping window) saves it where it is from: the file of
// every game is only written with the mapping of every game.
enum class Kind
{
  GameCube,
  Remote,
};

// InputConfig's overrides for these two.
bool LoadPad(int index, Common::IniFile::Section* section);
bool SavePad(int index, Common::IniFile::Section* section);
bool LoadRemote(int index, Common::IniFile::Section* section);
bool SaveRemote(int index, Common::IniFile::Section* section);

// The running game's settings name a profile, and that is its mapping.
bool HasGameMapping(Kind kind);
// Why it cannot have one, or empty. Host thread.
std::string GetGameMappingLock(Kind kind);
// Its own copy of the mapping it has now, and without it again. Host thread.
void MakeGameMapping(Core::System& system, Kind kind);
void RemoveGameMapping(Core::System& system, Kind kind);

// The profiles a mapping window lists, the one the running game's settings name (or empty), and
// another one for it (empty: none). Host thread.
std::vector<std::string> GetProfiles(Kind kind);
std::string GetProfile(Kind kind);
void SetProfile(Core::System& system, Kind kind, const std::string& name);

// What a mapping window says after the controller's name, or empty with the mapping of every game.
std::string DescribeMappingStore(Kind kind, int index);

// The choice whose own mapping the Wii Remote has, or None: it has the one of every game.
Choice GetOwnMapping();
// Extension and Sideways as that choice has them, in a mapping that is about to be loaded.
void KeepChoice(int index, Common::IniFile::Section* section);
// The Steam Frame profile for a Wii Remote: a file name in the Sys profiles.
std::string GetDefaultProfile(int index);

// The mapping is to be the Steam Frame profile. Host thread.
void ResetMapping(Kind kind);
}  // namespace VRWiiController
