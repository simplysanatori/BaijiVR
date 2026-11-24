// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"

// Mods, patches, cheats and texture packs from a catalogue
// (github.com/simplysanatori/gc-wii-game-fixes; its AGENTS.md has the format). The catalogue ships
// in Sys/GameMods/{mods,textures}.json; a newer one is fetched into the cache only when asked for.
// Nothing is rehosted: packs and mods come from where their authors keep them.
//
// Every function is safe from any thread. Nothing blocks on the network: downloads run on one worker
// thread and are watched with GetProgress(). Reading and switching do touch the disk, so the VR
// pacing thread must not call them directly (VrUi takes a snapshot from a host job).
namespace GameMods
{
enum class List
{
  Mods,
  Textures,
};

enum class State
{
  Available,  // not installed (codes and settings are never downloaded: they are on or off)
  Off,        // here, not used
  On,         // used from the game's next start
  LinkOnly,   // only a page to open in a browser
};

struct Item
{
  std::string id;
  std::string name;
  std::string author;
  std::string notes;
  std::string homepage;
  std::string category;  // "widescreen", "fix"...; "textures"
  std::string version;
  std::string updated;  // "2026-11" or "2026-11-24"
  u64 size = 0;         // bytes to download, 0 unknown or none
  bool verified = false;
  bool recommended = false;
  bool downloads = false;  // switching it on first needs a download
  State state = State::Available;

  bool operator==(const Item&) const = default;
};

// What the list holds for a game, best first: recommended, verified, then by name. Entries for other
// revisions of the disc are left out.
std::vector<Item> GetItems(List list, const std::string& game_id, u16 revision);

struct Result
{
  bool ok = false;
  std::string error;  // one sentence for the player
};

// Codes and settings go to the user's game INI (codes switch the game's cheats on as well), packs to
// Load/Textures, Riivolution mods to Load/Riivolution. Switching off undoes exactly that: anything
// the player set themselves is put back as it was. All of it is for the game's next start.
Result SetEnabled(List list, const std::string& game_id, const std::string& id, bool enabled);
// Off, and its files gone.
Result Remove(List list, const std::string& game_id, const std::string& id);

// Downloads, unpacks and switches on. False while another download runs.
bool StartInstall(List list, const std::string& game_id, const std::string& id);
void CancelInstall();

struct Progress
{
  enum class Phase
  {
    Idle,
    Downloading,
    Unpacking,
    Done,
    Failed,
    Cancelled,
  };

  Phase phase = Phase::Idle;
  std::string id;
  std::string name;
  u64 bytes_done = 0;
  u64 bytes_total = 0;  // 0 unknown
  std::string error;    // Failed: one sentence for the player

  bool operator==(const Progress&) const = default;
};
Progress GetProgress();

// Fetches the catalogue into the cache in the background (one GET per list). Progress is not shown;
// a later GetItems has the new one. Does nothing within 10 minutes of the last one.
void RefreshCatalogAsync();

// The Riivolution XML files switched on for a game, for the boot (BootCore).
std::vector<std::string> GetEnabledRiivolutionXmls(const std::string& game_id);

// Joins the worker thread (UICommon::Shutdown).
void Shutdown();
}  // namespace GameMods
