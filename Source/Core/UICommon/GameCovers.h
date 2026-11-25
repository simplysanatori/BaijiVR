// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"
#include "DiscIO/Enums.h"

// Covers for the VR menu and the window, and banners for the window: one PNG of each per game ID in
// the cover cache, from libretro-thumbnails on GitHub (by name). EnsureCover blocks on the network,
// so it is called from the game list's own thread.
namespace UICommon
{
// While false there is no question, no automatic download whatever UseGameCovers says, and no
// check box: nothing shows covers yet.
constexpr bool COVERS_ARE_SHOWN = true;

// Which of a game's pictures. The box is the one that is its cover by itself, the logo the banner
// of a game without one.
enum class CoverKind
{
  Box,
  Title,
  Snap,
  Logo,
};

struct CoverQuery
{
  std::string game_id;  // the cache file's name
  bool is_wii = false;
  DiscIO::Region region = DiscIO::Region::Unknown;
  std::string file_name;  // with its extension
  std::string title;      // the database's name for the game
  CoverKind kind = CoverKind::Box;
};

std::string GetCoverPath(const std::string& game_id, CoverKind kind = CoverKind::Box);

// What decides whether a game still lacks a cover: none of its own (beside the game), none in the
// cache, and the cache does not know it as missing. The same for a banner.
struct CoverState
{
  std::string game_id;
  bool has_custom = false;
  bool has_cached = false;
  bool known_missing = false;
  bool needs_banner = false;
};
bool NeedsCover(const CoverState& state);
// The cache's side of a CoverState, from the files on disk.
void FillCoverCacheState(CoverState* state, CoverKind kind = CoverKind::Box);

// True when the cover is in the cache (it was, or it is now).
bool EnsureCover(const CoverQuery& query);

// Names to try in libretro-thumbnails, best first.
std::vector<std::string> LibretroNames(const CoverQuery& query);
// Their addresses, all on raw.githubusercontent.com. The known name comes first.
std::vector<std::string> CoverUrls(const CoverQuery& query, const std::string& known_name = {});
// The names that Sys/covers.txt has, by game ID (Tools/make-cover-names.py writes it).
using CoverNames = std::map<std::string, std::array<std::string, 4>, std::less<>>;  // by CoverKind
CoverNames ParseCoverNames(std::string_view text);
// The picture that a link in libretro-thumbnails (a small text file) stands for.
std::optional<std::string> LinkTarget(const std::string& url, std::string_view body);

// Halves a picture until it is at most COVER_MAX_HEIGHT high, and writes it as PNG. False for
// anything that is not a sane PNG.
constexpr u32 COVER_MAX_HEIGHT = 400;
bool StoreCover(std::span<const u8> png, const std::string& path);
}  // namespace UICommon
