// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/GameMods/GameMods.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <mutex>
#include <regex>
#include <set>

#include <picojson.h>

#include "Common/CommonPaths.h"
#include "Common/Contains.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/IniFile.h"
#include "Common/JsonUtil.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"

#include "Core/ActionReplay.h"
#include "Core/ConfigLoaders/GameConfigLoader.h"
#include "Core/GameMods/GameModsInternal.h"
#include "Core/GeckoCodeConfig.h"
#include "Core/PatchEngine.h"
#include "DiscIO/GameModDescriptor.h"

namespace fs = std::filesystem;

namespace GameMods::Internal
{
std::mutex& StateMutex()
{
  static std::mutex mutex;
  return mutex;
}

namespace
{
bool IdChars(std::string_view text, std::string_view extra = "")
{
  return !text.empty() && std::ranges::all_of(text, [&](char c) {
           return std::isalnum(static_cast<unsigned char>(c)) || extra.find(c) != std::string_view::npos;
         });
}

bool EntryIdOk(std::string_view id)
{
  return IdChars(id, "-") && !std::ranges::any_of(id, [](char c) { return std::isupper(static_cast<unsigned char>(c)); });
}

bool GameIdOk(std::string_view id)
{
  return id.size() == 6 && std::ranges::all_of(id, [](char c) {
           return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
         });
}

// --- Catalogue ------------------------------------------------------------------------------

const char* ListName(List list)
{
  return list == List::Mods ? "mods" : "textures";
}

std::string Str(const picojson::object& o, const char* key)
{
  const auto it = o.find(key);
  return it != o.end() && it->second.is<std::string>() ? it->second.get<std::string>() : "";
}

bool IsHex8(std::string_view s)
{
  return s.size() == 8 && std::ranges::all_of(s, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

bool CodeLineOk(std::string_view code_type, const std::string& line)
{
  if (code_type == "onframe")
  {
    static const std::regex re(
        "^0x[0-9A-Fa-f]{8}:(byte|word|dword):0x[0-9A-Fa-f]{1,8}(:0x[0-9A-Fa-f]{1,8})?$");
    return std::regex_match(line, re);
  }
  return line.size() == 17 && line[8] == ' ' && IsHex8(line.substr(0, 8)) && IsHex8(line.substr(9));
}

bool SettingAllowed(const Setting& s)
{
  static const std::map<std::string, std::set<std::string>> allowed = {
      {"Video_Settings",
       {"wideScreenHack", "SuggestedAspectRatio", "AspectRatio", "SafeTextureCacheColorSamples"}},
      {"Video_Enhancements", {"ForceTextureFiltering", "DisableCopyFilter", "ArbitraryMipmapDetection"}},
      {"Core", {"CPUThread", "SyncGPU", "FPRF", "MMU", "Overclock", "OverclockEnable"}},
  };
  if (s.key.empty() || s.key.find_first_of("=\r\n[]") != std::string::npos)
    return false;
  if (s.value.find_first_of("\r\n") != std::string::npos)
    return false;
  if (s.section == "Video_Hacks")
    return true;
  const auto it = allowed.find(s.section);
  return it != allowed.end() && it->second.contains(s.key);
}

bool Https(const std::string& url)
{
  return url.starts_with("https://") && url.find_first_of(" \t\r\n") == std::string::npos;
}

std::optional<Source> ParseSource(const picojson::value& value, std::string* why)
{
  if (!value.is<picojson::object>())
  {
    *why = "no source";
    return std::nullopt;
  }
  const auto& o = value.get<picojson::object>();
  Source s;
  s.type = Str(o, "type");
  s.repo = Str(o, "repo");
  s.tag = Str(o, "tag");
  s.asset_pattern = Str(o, "asset_pattern");
  s.file_pattern = Str(o, "file_pattern");
  s.folder_id = Str(o, "folder_id");
  s.file_id = Str(o, "file_id");
  s.url = Str(o, "url");
  if (const auto it = o.find("mod"); it != o.end() && it->second.is<double>())
    s.mod = static_cast<u64>(it->second.get<double>());

  const auto repo_ok = [&] {
    const auto slash = s.repo.find('/');
    return slash != std::string::npos && slash > 0 && slash + 1 < s.repo.size() &&
           s.repo.find('/', slash + 1) == std::string::npos && IdChars(s.repo, "/-_.");
  };
  const auto drive_ok = [](const std::string& id) { return id.size() >= 10 && id.size() <= 80 && IdChars(id, "-_"); };
  const std::string& t = s.type;
  if (t == "github_release")
  {
    if (!repo_ok() || s.tag.empty() || s.asset_pattern.empty() || !IdChars(s.tag, "-_.+"))
      *why = "github_release source";
  }
  else if (t == "gamebanana")
  {
    if (s.mod == 0)
      *why = "gamebanana source";
    if (s.file_pattern.empty())
      s.file_pattern = "*";
  }
  else if (t == "gdrive")
  {
    if (!drive_ok(s.file_id))
      *why = "gdrive source";
  }
  else if (t == "gdrive_folder")
  {
    if (!drive_ok(s.folder_id) || s.file_pattern.empty())
      *why = "gdrive_folder source";
  }
  else if (t == "https")
  {
    if (!Https(s.url))
      *why = "source url must be https";
  }
  else if (t == "mega")
  {
    if (!Https(Str(o, "link")))
      *why = "source link must be https";
  }
  else if (t == "github_repo_path")
  {
    if (!repo_ok() || Str(o, "path").find("..") != std::string::npos)
      *why = "github_repo_path source";
  }
  else if (t != "link")
  {
    *why = "source type " + t;
  }
  if (!why->empty())
    return std::nullopt;
  return s;
}

std::optional<Entry> ParseEntry(List list, const picojson::value& value, std::string* why)
{
  if (!value.is<picojson::object>())
  {
    *why = "not an object";
    return std::nullopt;
  }
  const auto& o = value.get<picojson::object>();
  static const std::set<std::string> common = {"id",       "name",   "title",  "game_ids",
                                               "author",   "licence", "homepage", "notes"};
  static const std::set<std::string> optional = {"revisions", "author_url", "updated", "verified",
                                                 "recommended", "version"};
  static const std::set<std::string> mod = {"kind",  "category",  "aspect",    "settings", "code_type",
                                            "lines", "code_name", "source",    "xml"};
  static const std::set<std::string> tex = {"source", "parts",  "size",   "sha256",
                                            "format", "layout", "textures", "downloads", "folder"};
  for (const auto& [key, _] : o)
  {
    if (!common.contains(key) && !optional.contains(key) &&
        !(list == List::Mods ? mod : tex).contains(key))
    {
      *why = "unknown field " + key;
      return std::nullopt;
    }
  }
  for (const char* key : {"id", "name", "title", "author", "licence", "homepage", "notes"})
  {
    if (Str(o, key).empty())
    {
      *why = std::string(key) + " is missing";
      return std::nullopt;
    }
  }

  Entry e;
  e.list = list;
  e.id = Str(o, "id");
  e.name = Str(o, "name");
  e.title = Str(o, "title");
  e.author = Str(o, "author");
  e.homepage = Str(o, "homepage");
  e.notes = Str(o, "notes");
  e.version = Str(o, "version");
  e.updated = Str(o, "updated");
  if (!EntryIdOk(e.id))
    *why = "id";
  if (!(e.homepage.starts_with("https://") || e.homepage.starts_with("http://")) ||
      e.homepage.find_first_of(" \t\r\n") != std::string::npos)
    *why = "homepage";
  if (!e.updated.empty())
  {
    static const std::regex re("^\\d{4}-\\d{2}(-\\d{2})?$");
    if (!std::regex_match(e.updated, re))
      *why = "updated";
  }
  if (e.notes.size() > 300 || e.name.size() > 48)
    *why = "notes or name too long";

  const auto games = o.find("game_ids");
  if (games == o.end() || !games->second.is<picojson::array>() || games->second.get<picojson::array>().empty())
    *why = "game_ids";
  else
  {
    for (const auto& g : games->second.get<picojson::array>())
    {
      if (!g.is<std::string>() || !GameIdOk(g.get<std::string>()))
        *why = "game ID";
      else
        e.game_ids.push_back(g.get<std::string>());
    }
  }
  if (const auto it = o.find("revisions"); it != o.end())
  {
    if (!it->second.is<picojson::array>())
      *why = "revisions";
    else
    {
      for (const auto& r : it->second.get<picojson::array>())
      {
        if (r.is<double>() && r.get<double>() >= 0 && r.get<double>() < 65536)
          e.revisions.push_back(static_cast<u16>(r.get<double>()));
        else
          *why = "revisions";
      }
    }
  }
  e.verified = ReadBoolFromJson(o, "verified").value_or(false);
  e.recommended = ReadBoolFromJson(o, "recommended").value_or(false);
  if (!why->empty())
    return std::nullopt;

  const auto source_it = o.find("source");
  const bool has_source = source_it != o.end();
  if (list == List::Textures)
  {
    e.category = "textures";
    e.size = ReadNumericFromJson<u64>(o, "size").value_or(0);
    e.sha256 = Str(o, "sha256");
    e.layout = Str(o, "layout").empty() ? "single" : Str(o, "layout");
    if (e.layout != "single" && e.layout != "variants")
      *why = "layout";
    if (!e.sha256.empty() && !(e.sha256.size() == 64 && IdChars(e.sha256)))
      *why = "sha256";
    if (const auto it = o.find("parts"); it != o.end())
    {
      if (!it->second.is<picojson::array>() || it->second.get<picojson::array>().empty())
        *why = "parts";
      else
      {
        for (const auto& p : it->second.get<picojson::array>())
        {
          if (auto s = ParseSource(p, why))
            e.parts.push_back(std::move(*s));
        }
      }
    }
    else if (has_source)
    {
      if (auto s = ParseSource(source_it->second, why))
        e.parts.push_back(std::move(*s));
    }
    else
    {
      *why = "no source";
    }
    const bool linked = e.parts.size() == 1 && e.parts[0].type == "link";
    const std::string format = Str(o, "format");
    if (!linked && format != "zip" && format != "7z" && format != "rar")
      *why = "format";
    if (!linked && Str(o, "textures") != "png" && Str(o, "textures") != "dds")
      *why = "textures";
    return why->empty() ? std::optional<Entry>(std::move(e)) : std::nullopt;
  }

  e.kind = Str(o, "kind");
  e.category = Str(o, "category");
  e.code_type = Str(o, "code_type");
  e.code_name = Str(o, "code_name");
  e.xml = Str(o, "xml");
  static const std::set<std::string> kinds = {"codes", "builtin", "settings", "riivolution"};
  static const std::set<std::string> categories = {"widescreen", "framerate", "fix",    "graphics",
                                                   "controls",   "gameplay",  "cheat",  "translation",
                                                   "mod"};
  if (!kinds.contains(e.kind))
    *why = "kind";
  if (!categories.contains(e.category))
    *why = "category";
  if (e.category == "widescreen" && Str(o, "aspect") != "16:9")
    *why = "a widescreen entry needs aspect 16:9";
  if (const auto it = o.find("settings"); it != o.end())
  {
    if (!it->second.is<picojson::array>())
      *why = "settings";
    else
    {
      for (const auto& v : it->second.get<picojson::array>())
      {
        if (!v.is<picojson::object>())
        {
          *why = "setting";
          continue;
        }
        const auto& so = v.get<picojson::object>();
        const Setting s{Str(so, "section"), Str(so, "key"), Str(so, "value")};
        const auto value_it = so.find("value");
        if (value_it == so.end() || !value_it->second.is<std::string>() || !SettingAllowed(s))
          *why = "setting " + s.section + "/" + s.key + " is not allowed";
        e.settings.push_back(s);
      }
    }
  }
  if (e.kind == "settings" && e.settings.empty())
    *why = "no settings";
  if (e.kind == "codes" || e.kind == "builtin")
  {
    if (e.code_type != "gecko" && e.code_type != "ar" && e.code_type != "onframe")
      *why = "code_type";
  }
  if (e.kind == "codes")
  {
    if (const auto it = o.find("lines"); it != o.end() && it->second.is<picojson::array>())
    {
      for (const auto& l : it->second.get<picojson::array>())
      {
        if (l.is<std::string>() && CodeLineOk(e.code_type, l.get<std::string>()))
          e.lines.push_back(l.get<std::string>());
        else
          *why = "code line";
      }
    }
    if (e.lines.empty())
      *why = "no lines";
  }
  if (e.kind == "builtin" && (e.code_name.empty() || e.code_name.find_first_of("[]\r\n") != std::string::npos))
    *why = "code_name";
  if (e.kind == "riivolution")
  {
    if (!has_source)
      *why = "no source";
    else if (auto s = ParseSource(source_it->second, why))
      e.parts.push_back(std::move(*s));
    const bool linked = e.parts.size() == 1 && e.parts[0].type == "link";
    if (!linked && (e.xml.empty() || e.xml.find("..") != std::string::npos || e.xml.front() == '/'))
      *why = "xml";
  }
  return why->empty() ? std::optional<Entry>(std::move(e)) : std::nullopt;
}

struct CachedCatalog
{
  std::string path;
  fs::file_time_type stamp{};
  bool exists = false;
  std::vector<Entry> entries;
  bool parsed = false;
};

std::mutex s_catalog_mutex;
CachedCatalog s_catalog[2][2];  // [list][sys, cache]

std::string SysCatalogPath(List list)
{
  return File::GetSysDirectory() + "GameMods/" + ListName(list) + ".json";
}

// Entries of one file, kept until the file changes.
const CachedCatalog& ReadFile(List list, int which, const std::string& path)
{
  CachedCatalog& c = s_catalog[static_cast<int>(list)][which];
  std::error_code ec;
  const bool exists = fs::is_regular_file(path, ec);
  const auto stamp = exists ? fs::last_write_time(path, ec) : fs::file_time_type{};
  if (c.path == path && c.exists == exists && c.stamp == stamp)
    return c;
  c = {};
  c.path = path;
  c.exists = exists;
  c.stamp = stamp;
  if (exists)
  {
    std::string text;
    if (File::ReadFileToString(path, text))
    {
      if (auto entries = ParseCatalog(list, text))
      {
        c.entries = std::move(*entries);
        c.parsed = true;
      }
    }
  }
  return c;
}

// --- Per-game state -------------------------------------------------------------------------

struct CodeRec
{
  std::string type, name;
  bool added = false;  // it was not in the INI
  bool prev = false;   // enabled before
};

struct EntryRec
{
  std::vector<CodeRec> codes;
  std::string xml;
};

struct SettingRec
{
  bool had = false;
  std::string old, value;
  std::vector<std::string> owners;
};

struct GameState
{
  std::map<std::string, EntryRec> entries;
  std::map<std::string, SettingRec> settings;  // "Section/Key"
};

std::string StatePath(const std::string& game_id)
{
  return File::GetUserPath(D_USER_IDX) + "GameMods/" + game_id + ".json";
}

std::string IniPath(const std::string& game_id)
{
  return File::GetUserPath(D_GAMESETTINGS_IDX) + game_id + ".ini";
}

GameState LoadState(const std::string& game_id)
{
  GameState state;
  picojson::value root;
  std::string error;
  if (!JsonFromFile(StatePath(game_id), &root, &error) || !root.is<picojson::object>())
    return state;
  const auto& o = root.get<picojson::object>();
  if (const auto it = o.find("entries"); it != o.end() && it->second.is<picojson::object>())
  {
    for (const auto& [id, v] : it->second.get<picojson::object>())
    {
      if (!EntryIdOk(id) || !v.is<picojson::object>())
        continue;
      const auto& eo = v.get<picojson::object>();
      EntryRec rec;
      rec.xml = Str(eo, "xml");
      if (const auto c = eo.find("codes"); c != eo.end() && c->second.is<picojson::array>())
      {
        for (const auto& cv : c->second.get<picojson::array>())
        {
          if (!cv.is<picojson::object>())
            continue;
          const auto& co = cv.get<picojson::object>();
          rec.codes.push_back({Str(co, "type"), Str(co, "name"),
                               ReadBoolFromJson(co, "added").value_or(false),
                               ReadBoolFromJson(co, "prev").value_or(false)});
        }
      }
      state.entries[id] = std::move(rec);
    }
  }
  if (const auto it = o.find("settings"); it != o.end() && it->second.is<picojson::object>())
  {
    for (const auto& [key, v] : it->second.get<picojson::object>())
    {
      if (!v.is<picojson::object>())
        continue;
      const auto& so = v.get<picojson::object>();
      SettingRec rec{ReadBoolFromJson(so, "had").value_or(false), Str(so, "old"), Str(so, "value"), {}};
      if (const auto ow = so.find("owners"); ow != so.end() && ow->second.is<picojson::array>())
      {
        for (const auto& owner : ow->second.get<picojson::array>())
        {
          if (owner.is<std::string>())
            rec.owners.push_back(owner.get<std::string>());
        }
      }
      state.settings[key] = std::move(rec);
    }
  }
  return state;
}

bool SaveState(const std::string& game_id, const GameState& state)
{
  picojson::object entries;
  for (const auto& [id, rec] : state.entries)
  {
    picojson::object eo;
    picojson::array codes;
    for (const auto& c : rec.codes)
    {
      picojson::object co;
      co["type"] = picojson::value(c.type);
      co["name"] = picojson::value(c.name);
      co["added"] = picojson::value(c.added);
      co["prev"] = picojson::value(c.prev);
      codes.emplace_back(std::move(co));
    }
    eo["codes"] = picojson::value(std::move(codes));
    if (!rec.xml.empty())
      eo["xml"] = picojson::value(rec.xml);
    entries[id] = picojson::value(std::move(eo));
  }
  picojson::object settings;
  for (const auto& [key, rec] : state.settings)
  {
    picojson::object so;
    so["had"] = picojson::value(rec.had);
    so["old"] = picojson::value(rec.old);
    so["value"] = picojson::value(rec.value);
    picojson::array owners;
    for (const auto& owner : rec.owners)
      owners.emplace_back(owner);
    so["owners"] = picojson::value(std::move(owners));
    settings[key] = picojson::value(std::move(so));
  }
  picojson::object root;
  root["entries"] = picojson::value(std::move(entries));
  root["settings"] = picojson::value(std::move(settings));
  File::CreateFullPath(StatePath(game_id));
  return JsonToFile(StatePath(game_id), picojson::value(std::move(root)), true);
}

// One value of the game INI, with what was there before kept for the last owner to put back.
void TakeSetting(Common::IniFile& ini, GameState& state, const std::string& id, const Setting& s)
{
  const std::string key = s.section + "/" + s.key;
  auto it = state.settings.find(key);
  if (it == state.settings.end())
  {
    SettingRec rec;
    const auto* section = ini.GetSection(s.section);
    rec.had = section && section->Get(s.key, &rec.old);
    it = state.settings.emplace(key, std::move(rec)).first;
  }
  auto& rec = it->second;
  if (!Common::Contains(rec.owners, id))
    rec.owners.push_back(id);
  rec.value = s.value;
  ini.GetOrCreateSection(s.section)->Set(s.key, s.value);
}

void ReleaseSettings(Common::IniFile& ini, GameState& state, const std::string& id)
{
  for (auto it = state.settings.begin(); it != state.settings.end();)
  {
    auto& [key, rec] = *it;
    if (std::erase(rec.owners, id) == 0 || !rec.owners.empty())
    {
      ++it;
      continue;
    }
    const auto slash = key.find('/');
    const std::string section = key.substr(0, slash), name = key.substr(slash + 1);
    std::string now;
    const auto* sec = ini.GetSection(section);
    // Only what is still ours: the player may have changed it since.
    if (sec && sec->Get(name, &now) && now == rec.value)
    {
      if (rec.had)
        ini.GetOrCreateSection(section)->Set(name, rec.old);
      else
        ini.DeleteKey(section, name);
    }
    it = state.settings.erase(it);
  }
}

// --- Codes ----------------------------------------------------------------------------------

std::string CodeName(const Entry& e)
{
  std::string name = e.kind == "builtin" ? e.code_name : e.name;
  std::ranges::replace(name, '[', '(');
  std::ranges::replace(name, ']', ')');
  std::ranges::replace(name, '$', ' ');
  return std::string(StripWhitespace(name));
}

struct GeckoOps
{
  using Code = Gecko::GeckoCode;
  static constexpr const char* TYPE = "gecko";
  static std::vector<Code> Load(const Common::IniFile& g, const Common::IniFile& l)
  {
    return Gecko::LoadCodes(g, l);
  }
  static void Save(Common::IniFile& l, std::vector<Code>& c) { Gecko::SaveCodes(l, c); }
  static std::optional<Code> Make(const Entry& e, const std::string& name)
  {
    Code code;
    code.name = name;
    code.creator = e.author;
    code.user_defined = true;
    for (const auto& line : e.lines)
    {
      auto parsed = Gecko::DeserializeLine(line);
      if (!parsed)
        return std::nullopt;
      code.codes.push_back(*parsed);
    }
    return code;
  }
  static std::vector<std::string> Lines(const Code& c)
  {
    std::vector<std::string> lines;
    for (const auto& line : c.codes)
      lines.push_back(line.original_line);
    return lines;
  }
};

struct ArOps
{
  using Code = ActionReplay::ARCode;
  static constexpr const char* TYPE = "ar";
  static std::vector<Code> Load(const Common::IniFile& g, const Common::IniFile& l)
  {
    return ActionReplay::LoadCodes(g, l);
  }
  static void Save(Common::IniFile& l, std::vector<Code>& c) { ActionReplay::SaveCodes(&l, c); }
  static std::optional<Code> Make(const Entry& e, const std::string& name)
  {
    Code code;
    code.name = name;
    code.user_defined = true;
    for (const auto& line : e.lines)
    {
      const auto parsed = ActionReplay::DeserializeLine(line);
      if (!std::holds_alternative<ActionReplay::AREntry>(parsed))
        return std::nullopt;
      code.ops.push_back(std::get<ActionReplay::AREntry>(parsed));
    }
    return code;
  }
  static std::vector<std::string> Lines(const Code& c)
  {
    std::vector<std::string> lines;
    for (const auto& op : c.ops)
      lines.push_back(ActionReplay::SerializeLine(op));
    return lines;
  }
};

struct FrameOps
{
  using Code = PatchEngine::Patch;
  static constexpr const char* TYPE = "onframe";
  static std::vector<Code> Load(const Common::IniFile& g, const Common::IniFile& l)
  {
    std::vector<Code> patches;
    PatchEngine::LoadPatchSection("OnFrame", &patches, g, l);
    return patches;
  }
  static void Save(Common::IniFile& l, std::vector<Code>& c) { PatchEngine::SavePatchSection(&l, c); }
  static std::optional<Code> Make(const Entry& e, const std::string& name)
  {
    Code code;
    code.name = name;
    code.user_defined = true;
    for (const auto& line : e.lines)
    {
      const auto parsed = PatchEngine::DeserializeLine(line);
      if (!parsed)
        return std::nullopt;
      code.entries.push_back(*parsed);
    }
    return code;
  }
  static std::vector<std::string> Lines(const Code& c)
  {
    std::vector<std::string> lines;
    for (const auto& line : c.entries)
      lines.push_back(PatchEngine::SerializeLine(line));
    return lines;
  }
};

Common::IniFile LoadSysIni(const std::string& game_id, const Entry& e)
{
  Common::IniFile ini;
  std::optional<u16> revision;
  if (!e.revisions.empty())
    revision = e.revisions.front();
  const std::string dir = File::GetSysDirectory() + GAMESETTINGS_DIR DIR_SEP;
  for (const std::string& name : ConfigLoaders::GetGameIniFilenames(game_id, revision))
    ini.Load(dir + name, true);
  // The disc's revision is not known here: a code that only one revision's INI has is found too.
  std::vector<std::string> revisions;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
  {
    const std::string name = it->path().filename().string();
    if (name.starts_with(game_id + "r") && name.ends_with(".ini"))
      revisions.push_back(name);
  }
  std::ranges::sort(revisions);
  for (const std::string& name : revisions)
    ini.Load(dir + name, true);
  return ini;
}

// The name of the code in the INI: a built-in AR or OnFrame code can have "[author]" on its name.
template <class Ops>
std::string ResolveName(const std::vector<typename Ops::Code>& codes, const Entry& e)
{
  const std::string name = CodeName(e);
  if (e.kind != "builtin" || std::ranges::any_of(codes, [&](const auto& c) { return c.name == name; }))
    return name;
  for (const auto& c : codes)
  {
    if (c.name.starts_with(name + " ["))
      return c.name;
  }
  return name;
}

template <class Ops>
Result ToggleCode(Common::IniFile& local, const Common::IniFile& global, GameState& state,
                  const Entry& e, bool on)
{
  auto codes = Ops::Load(global, local);
  const std::string name = ResolveName<Ops>(codes, e);
  const auto it = std::ranges::find(codes, name, &Ops::Code::name);
  auto& rec = state.entries[e.id];
  const auto rec_it = std::ranges::find_if(rec.codes, [&](const CodeRec& c) { return c.name == name && c.type == Ops::TYPE; });
  if (on)
  {
    CodeRec cr{Ops::TYPE, name, false, false};
    if (it != codes.end())
    {
      if (e.kind == "codes" && Ops::Lines(*it) != e.lines)
        return {false, "A different code with this name is already in the game's list."};
      cr.prev = it->enabled;
    }
    else
    {
      if (e.kind == "builtin")
        return {false, "This game has no such built-in code."};
      auto code = Ops::Make(e, name);
      if (!code)
        return {false, "The code in the list is not readable."};
      codes.push_back(std::move(*code));
      cr.added = true;
    }
    // The INIs of several revisions can each have the code.
    for (auto& code : codes)
    {
      if (code.name == name)
        code.enabled = true;
    }
    if (rec_it != rec.codes.end())
    {
      // Switched on twice: what was there before the first time stays.
      cr.added = rec_it->added;
      cr.prev = rec_it->prev;
      *rec_it = cr;
    }
    else
    {
      rec.codes.push_back(cr);
    }
  }
  else if (it != codes.end())
  {
    if (rec_it != rec.codes.end() && rec_it->added && Ops::Lines(*it) == e.lines)
    {
      std::erase_if(codes, [&](const auto& code) { return code.name == name; });
    }
    else
    {
      for (auto& code : codes)
      {
        if (code.name == name)
          code.enabled = false;
      }
    }
    if (rec_it != rec.codes.end())
      rec.codes.erase(rec_it);
  }
  Ops::Save(local, codes);
  return {true, {}};
}

template <class Ops>
bool CodeIsOn(const Common::IniFile& local, const Common::IniFile& global, const Entry& e)
{
  const auto codes = Ops::Load(global, local);
  const auto it = std::ranges::find(codes, ResolveName<Ops>(codes, e), &Ops::Code::name);
  return it != codes.end() && it->enabled;
}

bool CodeEntryIsOn(const std::string& game_id, const Entry& e)
{
  Common::IniFile local;
  local.Load(IniPath(game_id));
  const Common::IniFile global = LoadSysIni(game_id, e);
  if (e.code_type == "gecko")
    return CodeIsOn<GeckoOps>(local, global, e);
  if (e.code_type == "ar")
    return CodeIsOn<ArOps>(local, global, e);
  return CodeIsOn<FrameOps>(local, global, e);
}

// --- Textures and Riivolution ---------------------------------------------------------------

std::string GameIdsFile(const std::string& id, const std::string& game_id, const char* ext)
{
  return TextureDir(id) + "/gameids/" + game_id + ext;
}

Result SetTextures(Common::IniFile& ini, GameState& state, const std::string& game_id, const Entry& e, bool on)
{
  std::error_code ec;
  if (!fs::is_directory(TextureDir(e.id), ec))
    return {false, "This pack is not downloaded yet."};
  const std::string on_file = GameIdsFile(e.id, game_id, ".txt");
  const std::string off_file = GameIdsFile(e.id, game_id, ".off");
  if (on)
  {
    fs::create_directories(fs::path(on_file).parent_path(), ec);
    if (fs::exists(off_file, ec))
      fs::rename(off_file, on_file, ec);
    else if (!fs::exists(on_file, ec))
      File::IOFile(on_file, "wb");
    if (!fs::exists(on_file, ec))
      return {false, "The pack could not be switched on."};
    TakeSetting(ini, state, e.id, {"Video_Settings", "HiresTextures", "True"});
  }
  else
  {
    if (fs::exists(on_file, ec))
      fs::rename(on_file, off_file, ec);
    if (ec)
      return {false, "The pack could not be switched off."};
    ReleaseSettings(ini, state, e.id);
  }
  return {true, {}};
}

}  // namespace

std::string FindXml(const std::string& dir, const std::string& pattern)
{
  std::vector<std::string> found;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
  {
    if (!it->is_regular_file(ec))
      continue;
    const std::string rel = fs::relative(it->path(), dir, ec).generic_string();
    if (GlobMatch(pattern, rel))
      found.push_back(rel);
  }
  std::ranges::sort(found);
  return found.empty() ? "" : found.front();
}

bool Source::Downloadable() const
{
  return type == "github_release" || type == "gamebanana" || type == "gdrive" ||
         type == "gdrive_folder" || type == "https";
}

bool Entry::Downloads() const
{
  return list == List::Textures || kind == "riivolution";
}

bool Entry::LinkOnly() const
{
  return Downloads() &&
         !std::ranges::all_of(parts, [](const Source& s) { return s.Downloadable(); });
}

std::string TextureDir(const std::string& id)
{
  return File::GetUserPath(D_HIRESTEXTURES_IDX) + id;
}

std::string RiivolutionDir(const std::string& id)
{
  return File::GetUserPath(D_LOAD_IDX) + "Riivolution/" + id;
}

std::string WorkDir(List, const std::string& id)
{
  return File::GetUserPath(D_LOAD_IDX) + ".gamemods-" + id;
}

bool GlobMatch(std::string_view pattern, std::string_view text)
{
  const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
  size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
  while (t < text.size())
  {
    if (p < pattern.size() && (pattern[p] == '?' || lower(pattern[p]) == lower(text[t])) && pattern[p] != '*')
    {
      ++p;
      ++t;
    }
    else if (p < pattern.size() && pattern[p] == '*')
    {
      star = p++;
      mark = t;
    }
    else if (star != std::string_view::npos)
    {
      p = star + 1;
      t = ++mark;
    }
    else
    {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*')
    ++p;
  return p == pattern.size();
}

std::optional<std::string> SafeRelativePath(std::string_view name)
{
  std::string path(name);
  std::ranges::replace(path, '\\', '/');
  if (path.empty() || path.front() == '/' || path.find('\0') != std::string::npos ||
      (path.size() >= 2 && path[1] == ':'))
    return std::nullopt;
  std::string out;
  for (const std::string& part : SplitString(path, '/'))
  {
    if (part == "..")
      return std::nullopt;
    if (part.empty() || part == ".")
      continue;
    out += (out.empty() ? "" : "/") + part;
  }
  if (out.empty())
    return std::nullopt;
  return out;
}

std::optional<std::vector<Entry>> ParseCatalog(List list, std::string_view json)
{
  picojson::value root;
  const char* begin = json.data();
  const std::string err = picojson::parse(root, begin, begin + json.size());
  if (!err.empty() || !root.is<picojson::object>())
    return std::nullopt;
  const auto& o = root.get<picojson::object>();
  const auto schema = o.find("schema");
  if (schema == o.end() || !schema->second.is<double>() || schema->second.get<double>() != 1)
    return std::nullopt;
  const auto list_it = o.find(list == List::Mods ? "mods" : "packs");
  if (list_it == o.end() || !list_it->second.is<picojson::array>())
    return std::nullopt;

  std::vector<Entry> entries;
  std::set<std::string> ids;
  for (const auto& v : list_it->second.get<picojson::array>())
  {
    std::string why;
    auto entry = ParseEntry(list, v, &why);
    if (entry && !ids.insert(entry->id).second)
    {
      why = "id used twice";
      entry.reset();
    }
    if (!entry)
    {
      WARN_LOG_FMT(COMMON, "GameMods: entry '{}' dropped: {}", v.is<picojson::object>() ? Str(v.get<picojson::object>(), "id") : "?", why);
      continue;
    }
    entries.push_back(std::move(*entry));
  }
  return entries;
}

std::string CatalogCachePath(List list)
{
  return File::GetUserPath(D_CACHE_IDX) + "GameMods/" + ListName(list) + ".json";
}

void ResetCatalogCache()
{
  std::lock_guard lock{s_catalog_mutex};
  for (auto& row : s_catalog)
  {
    for (auto& c : row)
      c = {};
  }
}

std::vector<Entry> LoadEntries(List list)
{
  std::lock_guard lock{s_catalog_mutex};
  const CachedCatalog& sys = ReadFile(list, 0, SysCatalogPath(list));
  const CachedCatalog& cache = ReadFile(list, 1, CatalogCachePath(list));
  if (cache.parsed && (!sys.parsed || cache.stamp >= sys.stamp))
    return cache.entries;
  return sys.parsed ? sys.entries : std::vector<Entry>{};
}

Result SetEnabledLocked(List list, const std::string& game_id, const Entry& e, bool enabled)
{
  GameState state = LoadState(game_id);
  Common::IniFile ini;
  ini.Load(IniPath(game_id));
  Result result{true, {}};

  if (list == List::Textures)
  {
    result = SetTextures(ini, state, game_id, e, enabled);
  }
  else if (e.kind == "riivolution")
  {
    if (enabled)
    {
      const std::string xml = FindXml(RiivolutionDir(e.id), e.xml);
      if (xml.empty())
        return {false, "The mod is not downloaded yet."};
      state.entries[e.id].xml = xml;
    }
    else
    {
      state.entries.erase(e.id);
    }
  }
  else
  {
    if (e.kind == "codes" || e.kind == "builtin")
    {
      const Common::IniFile global = LoadSysIni(game_id, e);
      if (e.code_type == "gecko")
        result = ToggleCode<GeckoOps>(ini, global, state, e, enabled);
      else if (e.code_type == "ar")
        result = ToggleCode<ArOps>(ini, global, state, e, enabled);
      else
        result = ToggleCode<FrameOps>(ini, global, state, e, enabled);
      // Gecko and AR codes only run with cheats on for the game.
      if (result.ok && enabled && e.code_type != "onframe")
        TakeSetting(ini, state, e.id, {"Core", "EnableCheats", "True"});
    }
    if (result.ok && enabled)
    {
      state.entries[e.id];
      for (const Setting& s : e.settings)
        TakeSetting(ini, state, e.id, s);
    }
  }
  if (!result.ok)
    return result;

  if (!enabled)
  {
    ReleaseSettings(ini, state, e.id);
    if (e.kind != "codes" && e.kind != "builtin")
      state.entries.erase(e.id);
    else if (const auto it = state.entries.find(e.id); it != state.entries.end() && it->second.codes.empty())
      state.entries.erase(it);
  }
  File::CreateFullPath(IniPath(game_id));
  if (!ini.Save(IniPath(game_id)) || !SaveState(game_id, state))
    return {false, "The game's settings could not be saved."};
  return {true, {}};
}

std::vector<DiscIO::Riivolution::Patch> GetRiivolutionPatches(const std::string& game_id,
                                                              std::optional<u16> revision,
                                                              std::optional<u8> disc_number)
{
  DiscIO::GameModDescriptorRiivolution descriptor;
  for (const std::string& xml : GameMods::GetEnabledRiivolutionXmls(game_id))
  {
    // The XML sits in <root>/riivolution/.
    descriptor.patches.push_back({xml, fs::path(xml).parent_path().parent_path().string(), {}});
  }
  if (descriptor.patches.empty())
    return {};
  return DiscIO::Riivolution::GenerateRiivolutionPatchesFromGameModDescriptor(descriptor, game_id,
                                                                              revision, disc_number);
}
}  // namespace GameMods::Internal

namespace GameMods
{
using namespace Internal;

std::vector<Item> GetItems(List list, const std::string& game_id, u16 revision)
{
  std::vector<Item> items;
  if (!GameIdOk(game_id))
    return items;
  std::lock_guard lock{StateMutex()};
  const GameState state = LoadState(game_id);
  for (const Entry& e : LoadEntries(list))
  {
    if (!Common::Contains(e.game_ids, game_id) ||
        (!e.revisions.empty() && !Common::Contains(e.revisions, revision)))
      continue;
    Item item;
    item.id = e.id;
    item.name = e.name;
    item.author = e.author;
    item.notes = e.notes;
    item.homepage = e.homepage;
    item.category = e.category;
    item.version = e.version;
    item.updated = e.updated;
    item.size = e.size;
    item.verified = e.verified;
    item.recommended = e.recommended;
    item.downloads = e.Downloads() && !e.LinkOnly();

    std::error_code ec;
    if (list == List::Textures)
    {
      if (!fs::is_directory(TextureDir(e.id), ec))
        item.state = e.LinkOnly() ? State::LinkOnly : State::Available;
      else
        item.state = fs::exists(GameIdsFile(e.id, game_id, ".txt"), ec) ? State::On : State::Off;
    }
    else if (e.kind == "riivolution")
    {
      if (!fs::is_directory(RiivolutionDir(e.id), ec))
        item.state = e.LinkOnly() ? State::LinkOnly : State::Available;
      else
        item.state = state.entries.contains(e.id) ? State::On : State::Off;
    }
    else if (e.kind == "settings")
    {
      item.state = state.entries.contains(e.id) ? State::On : State::Off;
    }
    else
    {
      item.state = CodeEntryIsOn(game_id, e) ? State::On : State::Off;
    }
    items.push_back(std::move(item));
  }
  std::ranges::stable_sort(items, [](const Item& a, const Item& b) {
    if (a.recommended != b.recommended)
      return a.recommended;
    if (a.verified != b.verified)
      return a.verified;
    return Common::CaseInsensitiveLess{}(a.name, b.name);
  });
  return items;
}

static std::optional<Entry> FindEntry(List list, const std::string& game_id, const std::string& id)
{
  if (!GameIdOk(game_id))
    return std::nullopt;
  for (Entry& e : LoadEntries(list))
  {
    if (e.id == id && Common::Contains(e.game_ids, game_id))
      return std::move(e);
  }
  return std::nullopt;
}

Result SetEnabled(List list, const std::string& game_id, const std::string& id, bool enabled)
{
  const auto entry = FindEntry(list, game_id, id);
  if (!entry)
    return {false, "That item is not in the list for this game."};
  std::lock_guard lock{StateMutex()};
  return SetEnabledLocked(list, game_id, *entry, enabled);
}

Result Remove(List list, const std::string& game_id, const std::string& id)
{
  const auto entry = FindEntry(list, game_id, id);
  if (!entry)
    return {false, "That item is not in the list for this game."};
  if (const Progress p = GetProgress(); p.id == id && (p.phase == Progress::Phase::Downloading || p.phase == Progress::Phase::Unpacking))
    return {false, "Wait for the download to finish first."};
  std::lock_guard lock{StateMutex()};
  Result result = SetEnabledLocked(list, game_id, *entry, false);
  if (!entry->Downloads())
    return result;
  // A missing download is already "removed".
  std::error_code ec;
  fs::remove_all(list == List::Textures ? TextureDir(id) : RiivolutionDir(id), ec);
  if (ec)
    return {false, "The files could not be removed."};
  return {true, {}};
}

bool StartInstall(List list, const std::string& game_id, const std::string& id)
{
  const auto entry = FindEntry(list, game_id, id);
  if (!entry || !entry->Downloads() || entry->LinkOnly())
    return false;
  return StartWorker(*entry, game_id);
}

std::vector<std::string> GetEnabledRiivolutionXmls(const std::string& game_id)
{
  std::vector<std::string> xmls;
  if (!GameIdOk(game_id))
    return xmls;
  std::lock_guard lock{StateMutex()};
  for (const auto& [id, rec] : LoadState(game_id).entries)
  {
    if (rec.xml.empty() || !SafeRelativePath(rec.xml))
      continue;
    const std::string path = RiivolutionDir(id) + "/" + rec.xml;
    if (File::Exists(path))
      xmls.push_back(path);
  }
  return xmls;
}

void Shutdown()
{
  StopWorkers();
}
}  // namespace GameMods
