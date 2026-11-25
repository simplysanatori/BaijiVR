// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/FrameUI/FileList.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <system_error>

#include "Common/StringUtil.h"

namespace VR::UI
{
namespace
{
namespace fs = std::filesystem;

// A folder with more than this is not a folder of games.
constexpr int MAX_COUNTED = 2000;
constexpr std::size_t MAX_ROWS = 1000;
// A slow disk: the rest of the folders are shown without a count.
constexpr std::chrono::milliseconds MAX_COUNT_TIME{700};

template <std::size_t N>
bool HasExtension(const std::string& name, const std::array<std::string_view, N>& extensions)
{
  const std::size_t dot = name.rfind('.');
  if (dot == std::string::npos)
    return false;
  std::string extension = name.substr(dot);
  Common::ToLower(&extension);
  return std::ranges::find(extensions, extension) != extensions.end();
}

int CountGames(const fs::path& folder)
{
  int games = 0;
  int seen = 0;
  std::error_code error;
  for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
  {
    if (++seen > MAX_COUNTED)
      break;
    std::error_code type_error;
    if (IsGameFileName(PathToString(it->path().filename())) && it->is_regular_file(type_error))
      ++games;
  }
  return games;
}

std::string GetHome()
{
  const char* const home = std::getenv("HOME");
  return home && *home ? home : "/";
}

// Where drives are: /run/media/<drive>, or /run/media/<user>/<drive>.
void AddDrives(std::vector<std::pair<std::string, std::string>>* places, const fs::path& folder,
               bool users)
{
  std::error_code error;
  for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
  {
    std::error_code type_error;
    if (!it->is_directory(type_error))
      continue;
    const std::string name = PathToString(it->path().filename());
    const char* const user = std::getenv("USER");
    if (users && user && name == user)
      AddDrives(places, it->path(), false);
    else
      places->emplace_back(name, PathToString(it->path()));
  }
}
}  // namespace

bool IsGameFileName(const std::string& name)
{
  // As the game list's (DolphinQt/GameList/GameTracker.cpp).
  static constexpr std::array<std::string_view, 14> EXTENSIONS{
      ".gcm", ".bin", ".iso", ".tgc", ".wbfs", ".ciso", ".gcz",
      ".wia", ".rvz", ".nfs", ".wad", ".dol",  ".elf",  ".json"};
  return HasExtension(name, EXTENSIONS);
}

bool IsImageFileName(const std::string& name)
{
  static constexpr std::array<std::string_view, 3> EXTENSIONS{".png", ".jpg", ".jpeg"};
  return HasExtension(name, EXTENSIONS);
}

FileListing ReadFileList(const std::string& path, FilePurpose purpose)
{
  FileListing listing;
  const fs::path folder = StringToPath(path).lexically_normal();
  listing.path = PathToString(folder);
  if (folder.has_parent_path() && folder.parent_path() != folder)
    listing.parent = PathToString(folder.parent_path());
  listing.places.emplace_back("Home", GetHome());
  AddDrives(&listing.places, "/run/media", true);

  std::error_code error;
  fs::directory_iterator it(folder, error);
  listing.readable = !error;
  for (const fs::directory_iterator end; !error && it != end; it.increment(error))
  {
    const std::string name = PathToString(it->path().filename());
    if (name.starts_with('.') || listing.rows.size() >= MAX_ROWS)
      continue;
    std::error_code type_error;
    if (it->is_directory(type_error))
      listing.rows.push_back({name, PathToString(it->path()), true, 0});
    else if (purpose == FilePurpose::Folder && IsGameFileName(name))
      ++listing.games;
    else if (purpose == FilePurpose::Image && IsImageFileName(name))
      listing.rows.push_back({name, PathToString(it->path()), false, 0});
  }
  std::ranges::stable_sort(listing.rows, [](const FileRow& a, const FileRow& b) {
    return a.is_folder != b.is_folder ? a.is_folder :
                                        Common::CaseInsensitiveLess{}(a.name, b.name);
  });
  if (purpose == FilePurpose::Folder)
  {
    const auto until = std::chrono::steady_clock::now() + MAX_COUNT_TIME;
    for (FileRow& row : listing.rows)
    {
      row.games = CountGames(StringToPath(row.path));
      if (std::chrono::steady_clock::now() > until)
        break;
    }
  }
  return listing;
}

std::string GetFileListStart(const std::vector<std::string>& wanted)
{
  for (const std::string& path : wanted)
  {
    std::error_code error;
    if (!path.empty() && fs::is_directory(StringToPath(path), error))
      return path;
  }
  return GetHome();
}
}  // namespace VR::UI
