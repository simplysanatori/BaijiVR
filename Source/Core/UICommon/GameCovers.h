// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"
#include "DiscIO/Enums.h"

// Box art for the game list and the VR UI: one PNG per game ID in the cover cache, from
// libretro-thumbnails on GitHub (by name). EnsureCover blocks on the network, so it is called from
// the game list's own thread.
namespace UICommon
{
struct CoverQuery
{
  std::string game_id;  // the cache file's name
  bool is_wii = false;
  DiscIO::Region region = DiscIO::Region::Unknown;
  std::string file_name;  // with its extension
  std::string title;      // the database's name for the game
};

std::string GetCoverPath(const std::string& game_id);

// What decides whether a game still lacks a cover: none of its own (beside the game), none in the
// cache, and the cache does not know it as missing.
struct CoverState
{
  std::string game_id;
  bool has_custom = false;
  bool has_cached = false;
  bool known_missing = false;
};
bool NeedsCover(const CoverState& state);
// The cache's side of a CoverState, from the files on disk.
void FillCoverCacheState(CoverState* state);

// True when the cover is in the cache (it was, or it is now).
bool EnsureCover(const CoverQuery& query);

// Names to try in libretro-thumbnails, best first.
std::vector<std::string> LibretroNames(const CoverQuery& query);
// Their addresses, all on raw.githubusercontent.com.
std::vector<std::string> CoverUrls(const CoverQuery& query);
// The picture that a link in libretro-thumbnails (a small text file) stands for.
std::optional<std::string> LinkTarget(const std::string& url, std::string_view body);

// Halves a picture until it is at most COVER_MAX_HEIGHT high, and writes it as PNG. False for
// anything that is not a sane PNG.
constexpr u32 COVER_MAX_HEIGHT = 400;
bool StoreCover(std::span<const u8> png, const std::string& path);
}  // namespace UICommon
