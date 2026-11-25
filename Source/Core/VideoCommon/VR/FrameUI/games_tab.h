// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.
// SPDX-License-Identifier: GPL-3.0-or-later

// The Games tab's tiles (Azahar for Frame's, its drawing GAMES-TAB-LAYOUT-3): where they are and
// their drawing. The tab's layout and its presses are the menu's (VrMenu, VrUi).

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "VideoCommon/VR/FrameUI/games_snapshot.h"
#include "VideoCommon/VR/FrameUI/md3.h"

namespace FrameXr::Menu {

// Layout (dp; tile-relative where it says so).
constexpr int kGamesColumns = 4;
constexpr float kGameTileW = 160.0f, kGameTileH = 200.0f, kGameGap = 12.0f;
constexpr float kGamesX0 = 128.0f, kGamesX1 = 804.0f, kGamesY0 = 112.0f, kGamesY1 = 524.0f;
constexpr float kGamesTopY0 = 36.0f, kGamesTopY1 = 84.0f; ///< the chips and buttons
constexpr float kGamesCountCy = 97.0f;
constexpr float kGamesChipGap = 8.0f, kGamesChipMinW = 64.0f, kGamesChipRoom = 44.0f,
                kGamesChipRadius = 12.0f;
constexpr float kGamesRoundR = 24.0f;
constexpr float kGamesSortMenuW = 220.0f, kGamesOptionsMenuW = 264.0f;
constexpr float kGamesBarRight = 822.0f; ///< what the scrollbar hangs off
constexpr float kGamesClipRoom = 6.0f;   ///< the grid's clip is this much wider
constexpr float kGamesSearchChipRoom = 64.0f, kGamesSearchChipMaxW = 137.0f,
                kGamesSearchChipClose = 48.0f;
/// The keyboard of Search: under one row of tiles.
constexpr int kGamesKeyColumns = 10, kGamesKeyRows = 4;
constexpr float kGamesKeysY0 = 324.0f, kGamesKeyH = 48.0f, kGamesKeyGap = 5.0f;
constexpr float kGamesTypingY1 = kGamesY0 + 200.0f;
constexpr std::size_t kGamesSearchMax = 60;
constexpr float kGameOptionsTarget = 48.0f;
/// A row of tiles and its gap: what a stick's tap scrolls.
constexpr float kGameRow = kGameTileH + kGameGap;
constexpr int kGameTitleLines = 3;
constexpr float kGameTitlePitch = 18.0f;
constexpr float kGameTextW = kGameTileW - 24.0f;

/// "42 Games", "1 Game", "2 Matches"
std::string GameCount(std::size_t n, bool searching);
/// A tile's tooltip: Continue on the running game, its marks' words.
std::string GameTooltip(const GameEntry& game, bool running);
/// The whole title in lines of `width`, broken at spaces; a word too long for a line goes on in the
/// next.
std::vector<std::string> GameTitleWrapped(const Kit& kit, const std::string& title, float width);
/// Its first lines, as many as a tile has, the last ending with dots after a whole word when there
/// are more.
std::vector<std::string> GameTitleLines(const Kit& kit, const std::string& title, float width,
                                        bool* cut = nullptr);
/// Where a list at rest at `scroll` goes on to: the nearest row's edge.
float GamesRestScroll(float scroll, float max_scroll);
/// How far a title `overflow` dp too high has moved after `t` seconds: it waits, moves up slowly,
/// waits, starts again.
float TitleScroll(float t, float overflow);
/// The game of `path` as an index of the list, -1 not there.
int GameAt(const GamesList& list, const std::string& path);

/// The tab's icon on the rail (four tiles), 24 dp.
void DrawGamesIcon(Kit& kit, float cx, float cy, bool active);
void DrawSearchGlyph(Kit& kit, float cx, float cy, uint32_t rgb, float size = 24.0f);
void DrawSortGlyph(Kit& kit, float cx, float cy, uint32_t rgb);
void DrawAddFolderGlyph(Kit& kit, float cx, float cy, uint32_t rgb);

struct GameTileLook {
    bool running = false;
    bool pointed = false;
    State tile = State::Rest;
    State options = State::Rest;
    float title_scroll = 0.0f; ///< how far a cut title has moved up (dp)
};

/// A tile at `r`, within `clip`, with its composed cover (null: none). Returns how much too high its
/// title is (dp, 0: it is all there).
float DrawGameTile(Kit& kit, const GameEntry& game, const GameIcon* cover, const GameTileLook& look,
                   const Rect& r, const Rect& clip);
/// A chip of the tab, with its check when it is on.
void DrawGamesChip(Kit& kit, const Rect& r, const std::string& name, bool on, State state);

} // namespace FrameXr::Menu
