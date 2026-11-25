// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "UICommon/VRGames.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

#include "Common/FileUtil.h"
#include "Common/NandPaths.h"
#include "Common/StringUtil.h"
#include "Core/TimePlayed.h"
#include "Core/TitleDatabase.h"
#include "DiscIO/Enums.h"
#include "UICommon/GameCovers.h"
#include "UICommon/GameFile.h"

namespace UICommon
{
using FrameXr::Menu::GameEntry;
using FrameXr::Menu::GameIcon;
using FrameXr::Menu::GamesSnapshot;
using FrameXr::Menu::GameSystem;

namespace
{
constexpr char FAVOURITES[] = "Favourites";
constexpr char LAST_PLAYED[] = "LastPlayed";
constexpr char COVERS[] = "Covers";
// In the order of VRGameStore::Cover.
constexpr std::array COVER_NAMES{"", "None", "Box", "Title", "Snap", "Own"};

GameSystem GetSystem(DiscIO::Platform platform)
{
  switch (platform)
  {
  case DiscIO::Platform::GameCubeDisc:
  case DiscIO::Platform::Triforce:
    return GameSystem::GameCube;
  case DiscIO::Platform::WiiDisc:
  case DiscIO::Platform::WiiWAD:
    return GameSystem::Wii;
  default:
    return GameSystem::Other;
  }
}

std::string GetRegion(DiscIO::Region region)
{
  switch (region)
  {
  case DiscIO::Region::NTSC_J:
    return "Japan";
  case DiscIO::Region::NTSC_U:
    return "USA";  // also beside "GameCube" it has room
  case DiscIO::Region::PAL:
    return "Europe";
  case DiscIO::Region::NTSC_K:
    return "Korea";
  default:
    return {};
  }
}

std::shared_ptr<const GameIcon> MakeIcon(const GameBanner& banner)
{
  const std::size_t pixels = std::size_t{banner.width} * banner.height;
  if (pixels == 0 || banner.buffer.size() < pixels)
    return nullptr;
  auto icon = std::make_shared<GameIcon>();
  icon->w = static_cast<int>(banner.width);
  icon->h = static_cast<int>(banner.height);
  icon->rgba.resize(pixels * 4);
  for (std::size_t i = 0; i < pixels; ++i)
  {
    // As DolphinQt's ImageConverter: blue first.
    const u32 p = banner.buffer[i];
    icon->rgba[i * 4] = static_cast<u8>(p >> 16);
    icon->rgba[i * 4 + 1] = static_cast<u8>(p >> 8);
    icon->rgba[i * 4 + 2] = static_cast<u8>(p);
    icon->rgba[i * 4 + 3] = 255;
  }
  return icon;
}
}  // namespace

VRGameStore::VRGameStore(std::string path) : m_path(std::move(path))
{
  m_ini.Load(m_path);
}

bool VRGameStore::IsFavourite(const std::string& key) const
{
  bool favourite = false;
  m_ini.GetIfExists(FAVOURITES, key, &favourite);
  return favourite;
}

void VRGameStore::SetFavourite(const std::string& key, bool favourite)
{
  if (favourite)
    m_ini.GetOrCreateSection(FAVOURITES)->Set(key, true);
  else
    m_ini.DeleteKey(FAVOURITES, key);
  m_ini.Save(m_path);
}

s64 VRGameStore::GetLastPlayed(const std::string& key) const
{
  s64 time = 0;
  m_ini.GetIfExists(LAST_PLAYED, key, &time);
  return time;
}

void VRGameStore::SetLastPlayed(const std::string& key, s64 time)
{
  m_ini.GetOrCreateSection(LAST_PLAYED)->Set(key, time);
  m_ini.Save(m_path);
}

VRGameStore::Cover VRGameStore::GetCover(const std::string& key) const
{
  std::string name;
  m_ini.GetIfExists(COVERS, key, &name);
  const auto it = std::ranges::find(COVER_NAMES, name);
  return it != COVER_NAMES.end() ? static_cast<Cover>(it - COVER_NAMES.begin()) : Cover::Default;
}

void VRGameStore::SetCover(const std::string& key, Cover cover)
{
  if (cover == Cover::Default)
    m_ini.DeleteKey(COVERS, key);
  else
    m_ini.GetOrCreateSection(COVERS)->Set(key, std::string(COVER_NAMES[static_cast<size_t>(cover)]));
  m_ini.Save(m_path);
}

std::string GetOwnCoverPath(const std::string& key)
{
  return File::GetUserPath(D_CONFIG_IDX) + "VRCovers/" + Common::EscapeFileName(key) + ".png";
}

std::string GetVRGameKey(const GameFile& game)
{
  if (!game.GetGameID().empty())
    return game.GetGameID();
  // What an INI's key cannot have.
  std::string name = game.GetFileName();
  std::ranges::replace_if(name, [](char c) { return c == '=' || c == '[' || c == '#'; }, '_');
  return name;
}

GamesSnapshot VRGameList::Make(std::span<const std::shared_ptr<const GameFile>> games,
                               const Sources& sources)
{
  std::map<std::string, Known> known;
  std::vector<GameEntry> rows;
  std::vector<std::pair<s64, std::string>> played;
  for (const std::shared_ptr<const GameFile>& file : games)
  {
    if (!file)
      continue;
    const std::string& path = file->GetFilePath();
    Known& k = known[path];
    if (const auto it = m_known.find(path); it != m_known.end())
      k = it->second;
    if (k.file.lock() != file)
    {
      k.file = file;
      k.icon = MakeIcon(file->GetBannerImage());
      ++k.cover_version;
    }

    const std::string key = GetVRGameKey(*file);
    std::string directory, name;
    SplitPath(path, &directory, &name, nullptr);

    GameEntry& entry = rows.emplace_back();
    entry.path = path;
    entry.title = file->GetName(sources.titles);
    // Without a name of its own it is the file's, with the extension.
    if (entry.title == file->GetFileName())
      entry.title = name;
    entry.file_name = name;
    entry.region = GetRegion(file->GetRegion());
    entry.game_id = file->GetGameID();
    entry.system = GetSystem(file->GetPlatform());
    entry.favorite = sources.store.IsFavourite(key);
    entry.missing = sources.missing.contains(path);
    using Cover = VRGameStore::Cover;
    const Cover cover = sources.store.GetCover(key);
    const std::string& id = file->GetGameTDBID();
    if (cover == Cover::Own)
      entry.covers.push_back(GetOwnCoverPath(key));
    else if (!id.empty() && cover == Cover::Title)
      entry.covers.push_back(GetCoverPath(id, CoverKind::Title));
    else if (!id.empty() && cover == Cover::Snap)
      entry.covers.push_back(GetCoverPath(id, CoverKind::Snap));
    else if (!id.empty() && cover == Cover::Box)
      entry.covers.push_back(GetCoverPath(id));
    if (cover != Cover::None)
    {
      entry.covers.push_back(directory + name + ".cover.png");
      entry.covers.push_back(directory + "cover.png");
      if (!id.empty() && cover != Cover::Box)
        entry.covers.push_back(GetCoverPath(id));
    }
    entry.cover_version = k.cover_version;
    entry.played_s = static_cast<u64>(std::chrono::duration_cast<std::chrono::seconds>(
                                          sources.time_played.GetTimePlayed(file->GetGameID()))
                                          .count());
    entry.icon = k.icon;
    if (const s64 time = sources.store.GetLastPlayed(key); time != 0)
      played.emplace_back(time, path);
  }
  m_known = std::move(known);

  std::ranges::stable_sort(played, std::ranges::greater{}, &std::pair<s64, std::string>::first);
  std::vector<std::string> recent;
  for (auto& [time, path] : played)
    recent.push_back(std::move(path));
  return FrameXr::Menu::MakeSnapshot(std::move(rows), recent);
}

void VRGameList::CoverChanged(const std::string& path)
{
  if (const auto it = m_known.find(path); it != m_known.end())
    ++it->second.cover_version;
}

std::string FindRunningVRGame(const GamesSnapshot& games, const std::string& path,
                              const std::string& game_id)
{
  const auto by_path = std::ranges::find(games.games, path, &GameEntry::path);
  if (!path.empty() && by_path != games.games.end())
    return path;
  const auto by_id = std::ranges::find(games.games, game_id, &GameEntry::game_id);
  return !game_id.empty() && by_id != games.games.end() ? by_id->path : std::string();
}
}  // namespace UICommon
