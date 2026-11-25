// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Config
{
template <typename T>
class Info;
}

namespace Core
{
class System;
}

// The controller a Wii game has in VR, kept per game. A Wii Remote has a mapping for each way it is
// held: the one of every game (WiimoteNew.ini) if that is for it, and else one in a profile of its
// own, so that the mapping of every game stays what it is.
namespace VRWiiController
{
enum class Choice : int
{
  None = -1,  // as it is set up
  GameCube,
  Remote,
  RemoteNunchuk,
  RemoteSideways,
};

extern const Config::Info<Choice> VR_WII_CONTROLLER;

// What the running game has. Host thread.
Choice GetCurrent(Core::System& system);

// Not with NetPlay, a movie or a controller profile named by the game's settings.
bool CanChoose(Core::System& system);

// Changes the emulated hardware of the running game, now and when it is started again. Host thread.
void Choose(Core::System& system, Choice choice);

// Without a choice, a Wii game played with a Wii Remote has no GameCube Controller on the same
// buttons, until the game ends. Host thread.
void OnSessionStarted(Core::System& system);

// After the mapping of every game was loaded. Any thread.
void OnMappingLoaded();

// The Wii Remote's mapping was changed, or is to be the Steam Frame profile. Host thread.
void SaveMapping();
void ResetMapping();
}  // namespace VRWiiController
