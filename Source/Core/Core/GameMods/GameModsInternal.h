// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Private to GameMods and its tests.

#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/GameMods/GameMods.h"
#include "DiscIO/RiivolutionParser.h"

namespace GameMods::Internal
{
struct Source
{
  std::string type;  // github_release, gamebanana, gdrive, gdrive_folder, https, ...
  std::string repo, tag, asset_pattern;
  std::string file_pattern, folder_id, file_id, url;
  u64 mod = 0;

  // Can it be downloaded here (and not only opened in a browser)?
  bool Downloadable() const;
};

struct Setting
{
  std::string section, key, value;
};

struct Entry
{
  List list = List::Mods;
  std::string id, name, title, author, homepage, notes, category, version, updated;
  std::vector<std::string> game_ids;
  std::vector<u16> revisions;
  bool verified = false;
  bool recommended = false;
  // mods
  std::string kind, code_type, code_name, xml;
  std::vector<std::string> lines;
  std::vector<Setting> settings;
  // riivolution and textures
  std::vector<Source> parts;  // one for riivolution
  u64 size = 0;
  std::string sha256, layout;

  bool Downloads() const;
  bool LinkOnly() const;
};

// Bad entries are dropped with a warning. nullopt: not a catalogue of schema 1.
std::optional<std::vector<Entry>> ParseCatalog(List list, std::string_view json);
// The shipped catalogue, or the cached one when it parses and is not older.
std::vector<Entry> LoadEntries(List list);
void ResetCatalogCache();
std::string CatalogCachePath(List list);

bool GlobMatch(std::string_view pattern, std::string_view text);
// "a/b" from "a\\b/./b"; nullopt for "..", absolute and empty names.
std::optional<std::string> SafeRelativePath(std::string_view name);

// Everything below returns "" or a sentence for the player.
// Unpacks a zip into the empty folder dir. Refuses unsafe names and links.
std::string ExtractZip(const std::string& zip, const std::string& dir, const std::atomic_bool& cancel);
// zip itself, 7z and rar with bsdtar.
std::string ExtractArchive(const std::string& archive, const std::string& dir,
                           const std::atomic_bool& cancel);
// Turns an unpacked texture pack into Load/Textures/<id>: wrapper folders dropped, every gameids
// file off. dir is on the same disk as the target.
// Unpacked separate archives into one folder, later files winning.
std::string MergeParts(const std::vector<std::string>& dirs, const std::string& out, bool drop_wrappers);
std::string FinishTexturePack(const std::string& unpacked, const Entry& entry);
std::string FinishRiivolution(const std::string& unpacked, const Entry& entry);

std::string TextureDir(const std::string& id);
std::string RiivolutionDir(const std::string& id);
// Where an install on this disk may be unpacked, next to its target.
std::string WorkDir(List list, const std::string& id);

// The one XML of an unpacked mod that the entry's path pattern names, relative; "" if none.
std::string FindXml(const std::string& dir, const std::string& pattern);
// Guards the game INIs and state files; the worker takes it to switch an install on.
std::mutex& StateMutex();

// Called by the worker once the files are in place.
Result SetEnabledLocked(List list, const std::string& game_id, const Entry& entry, bool enabled);

// Riivolution patches of the enabled mods, for the boot.
std::vector<DiscIO::Riivolution::Patch> GetRiivolutionPatches(const std::string& game_id,
                                                              std::optional<u16> revision,
                                                              std::optional<u8> disc_number);

// Where the file of a source is and how big (a network call).
std::string ResolveSource(const Source& source, std::string* url, u64* size);

// The worker (Install.cpp).
bool StartWorker(const Entry& entry, const std::string& game_id);
void StopWorkers();
}  // namespace GameMods::Internal
