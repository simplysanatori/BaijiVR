// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.
// SPDX-License-Identifier: GPL-3.0-or-later

// The games the VR menu's Games tab lists: what the game list found, published as one shared list that
// nobody changes after (a new list replaces it). No Qt and no emulator types (from Azahar for Frame,
// with Dolphin's systems).

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace FrameXr::Menu {

enum class GameSystem : int { GameCube, Wii, Other };
/// The chips.
enum class SystemFilter : int { All, GameCube, Wii };
constexpr int kSystemFilters = 3;

inline bool FilterMatches(SystemFilter filter, GameSystem system) {
    return filter == SystemFilter::All ||
           (filter == SystemFilter::GameCube && system == GameSystem::GameCube) ||
           (filter == SystemFilter::Wii && system == GameSystem::Wii);
}

/// A picture: straight RGBA, rows top down (a game's banner, or a composed cover).
struct GameIcon {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool operator==(const GameIcon&) const = default;
};

struct GameEntry {
    std::string path;      ///< what starts it
    std::string title;
    std::string file_name; ///< without its folder and extension
    std::string region;    ///< "" unknown
    std::string game_id;
    GameSystem system = GameSystem::GameCube;
    bool favorite = false;
    bool missing = false; ///< its file was not there when it was pressed
    /// Where its cover can be, the first that is there (read as its tile shows, never before).
    std::vector<std::string> covers;
    uint32_t cover_version = 0; ///< another: the files may be others
    uint64_t played_s = 0;      ///< the time it was played, in seconds
    std::shared_ptr<const GameIcon> icon; ///< its banner, null none
    bool operator==(const GameEntry&) const = default;
};

struct GamesSnapshot {
    std::vector<GameEntry> games; ///< in the order the tab shows them
    bool operator==(const GamesSnapshot&) const = default;
};
using GamesList = std::shared_ptr<const GamesSnapshot>;

/// The list as the tab shows it, from the game list's rows in any order: each file once, the
/// recently played games first (`recent`: their paths, newest first), then the rest by title. A
/// game whose title the menu's font cannot draw, or that shares its title with another game of its
/// system, has its file's name.
GamesSnapshot MakeSnapshot(std::vector<GameEntry> rows, const std::vector<std::string>& recent);

/// Whether the menu's font has every letter of a name (Latin only).
bool NameDrawable(const std::string& utf8);

/// The chips that show, in order: none with one system, else All and each system that has games.
std::vector<SystemFilter> GameChips(const GamesList& list);
/// The chip in effect: `wanted` while it shows, else All.
SystemFilter GameChip(const GamesList& list, SystemFilter wanted);

enum class GamesSort : int { RecentlyPlayed, FavouritesFirst, Title, PlayTime, Count };

/// The games under a chip whose title or file name has `search` in it (any case, accents aside),
/// sorted, as indices of `list->games`.
std::vector<int> GamesShown(const GamesList& list, SystemFilter chip, GamesSort sort,
                            const std::string& search);

const char* ChipName(SystemFilter chip);
const char* SystemName(GameSystem system);
const char* SortName(GamesSort sort);
/// A tile's small line, the longest first: the one that fits shows.
std::vector<std::string> GameLines(const GameEntry& game, bool running);

} // namespace FrameXr::Menu
