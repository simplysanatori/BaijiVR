// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <utility>
#include <vector>

// What the VR menu's Add Folder and Use My Own Image show of a folder.
namespace VR::UI
{
enum class FilePurpose
{
  Folder,  // sub-folders and how many games are in each
  Image,   // sub-folders and images
};

struct FileRow
{
  std::string name;
  std::string path;
  bool is_folder = true;
  int games = 0;

  bool operator==(const FileRow&) const = default;
};

struct FileListing
{
  std::string path;
  std::string parent;  // empty: none
  bool readable = false;
  int games = 0;  // in the folder itself
  std::vector<FileRow> rows;
  // Where else to go: names and paths.
  std::vector<std::pair<std::string, std::string>> places;

  bool operator==(const FileListing&) const = default;
};

bool IsGameFileName(const std::string& name);
bool IsImageFileName(const std::string& name);

// Reads the disk: not on the pacing thread.
FileListing ReadFileList(const std::string& path, FilePurpose purpose);
// The first one that is there, else the player's home.
std::string GetFileListStart(const std::vector<std::string>& wanted);
}  // namespace VR::UI
