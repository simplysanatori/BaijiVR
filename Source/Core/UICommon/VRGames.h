// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/IniFile.h"
#include "VideoCommon/VR/FrameUI/games_snapshot.h"

class TimePlayed;

namespace Core
{
class TitleDatabase;
}

// The game list as the VR menu's Games tab has it.
namespace UICommon
{
class GameFile;

// What the player does on the Games tab, by game ID, in a file of its own. Host thread.
class VRGameStore
{
public:
  explicit VRGameStore(std::string path);

  bool IsFavourite(const std::string& key) const;
  void SetFavourite(const std::string& key, bool favourite);
  // Unix seconds; 0: not since this is kept.
  s64 GetLastPlayed(const std::string& key) const;
  void SetLastPlayed(const std::string& key, s64 time);
  // What its cover is, other than by itself.
  enum class Cover
  {
    Default,
    None,
    Box,
    Title,
    Snap,
    Own,
  };
  Cover GetCover(const std::string& key) const;
  void SetCover(const std::string& key, Cover cover);

private:
  std::string m_path;
  mutable Common::IniFile m_ini;
};

// What is kept by: the game ID, or for a file without one its name.
std::string GetVRGameKey(const GameFile& game);
// Where the player's own image for it is kept.
std::string GetOwnCoverPath(const std::string& key);

// The game's cover's files, the first that can be read is it: the player's choice, then the one
// beside the game, then the downloaded one. Empty: No Cover.
std::vector<std::string> GetCoverFiles(const GameFile& game, const VRGameStore& store);

class VRGameList
{
public:
  struct Sources
  {
    const Core::TitleDatabase& titles;
    const VRGameStore& store;
    const TimePlayed& time_played;
    const std::set<std::string>& missing;  // paths
  };

  // All that can be shown, the last played first. A game that is as it was keeps its banner and the
  // version of its cover.
  FrameXr::Menu::GamesSnapshot Make(std::span<const std::shared_ptr<const GameFile>> games,
                                    const Sources& sources);
  // Its cover's files are others now.
  void CoverChanged(const std::string& path);

private:
  struct Known
  {
    std::weak_ptr<const GameFile> file;
    std::shared_ptr<const FrameXr::Menu::GameIcon> icon;
    u32 cover_version = 0;
  };

  std::map<std::string, Known> m_known;
};

// The running game's path in the list: the one it was started from, or else one with its ID.
std::string FindRunningVRGame(const FrameXr::Menu::GamesSnapshot& games, const std::string& path,
                              const std::string& game_id);
}  // namespace UICommon
