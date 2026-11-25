// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <string>

#include "Common/CommonTypes.h"
#include "VideoCommon/VR/FrameUI/games_snapshot.h"

// Between the VR menu's Games tab and the window that has the game list: the window publishes the
// list, the menu asks for what its tiles do. Without a window there is no host.
namespace VR::UI
{
// What a tile's cover is to be.
enum class CoverChoice
{
  Default,
  None,
  Box,
  Title,
  Snap,
  Own,
};

struct GamesHost
{
  // Stops the running game and starts another. Empty when it is on its way, or else why not.
  std::function<std::string(const std::string& path)> play;
  std::function<void(const std::string& path)> toggle_favourite;
  // Box, Title and Snap are downloaded when they are not there. `image`: the file of Own.
  std::function<void(const std::string& path, CoverChoice choice, const std::string& image)>
      choose_cover;
  // To the directories of games.
  std::function<void(const std::string& path)> add_folder;
  // For all games that lack a cover. A message says what came of it.
  std::function<void()> download_thumbnails;
  // The list again, where something of it may have changed.
  std::function<void()> publish;
};

// Host thread. A host without `play` is none.
void SetGamesHost(GamesHost host);
// Host thread. Null: none.
const GamesHost* GetGamesHost();

// Host thread. Report a Problem (UICommon): saves a report and says what came of it.
void SetReportSaver(std::function<std::string()> save);
// Empty: there is no saver.
std::string SaveReport();

struct PublishedGames
{
  FrameXr::Menu::GamesList list;  // null: none yet
  std::string running;            // the running game's path in it, or empty
  u32 serial = 0;                 // counts what was published
};

// Any thread.
void PublishGames(FrameXr::Menu::GamesList list, std::string running);
PublishedGames GetPublishedGames();
u32 GetPublishedGamesSerial();
}  // namespace VR::UI
