// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/GameSettingScope.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "Common/Config/Config.h"
#include "Common/IniFile.h"
#include "Common/StringUtil.h"

#include "Core/ConfigManager.h"

namespace GameSettingScope
{
namespace
{
constexpr std::string_view VR_SECTION_NAME = "Graphics.VR";
constexpr std::string_view LEGACY_VR_SECTION_NAME = "GFX.VR";

bool IsComment(std::string_view line)
{
  return line.starts_with('#') || line.starts_with(';') || line.starts_with('$') ||
         line.starts_with('*');
}

// The setting a line has, by any of the names it had.
bool IsKey(std::string_view line, std::string_view key)
{
  if (line.empty() || IsComment(line) || line.front() == '[')
    return false;
  std::string name;
  std::string value;
  Common::IniFile::ParseLine(line, &name, &value);
  return Common::CaseInsensitiveEquals(name, key) ||
         (key == "Passthrough" && Common::CaseInsensitiveEquals(name, "ARMode"));
}
}  // namespace

bool IsVRSectionHeader(std::string_view line)
{
  if (line.size() < 3 || line.front() != '[' || line.back() != ']')
    return false;

  const std::string_view section_name = line.substr(1, line.size() - 2);
  return Common::CaseInsensitiveEquals(section_name, VR_SECTION_NAME) ||
         Common::CaseInsensitiveEquals(section_name, LEGACY_VR_SECTION_NAME);
}

std::string EditVRSettings(std::string_view text, std::string_view key,
                           const std::optional<std::string>& value)
{
  std::vector<std::string> lines;
  bool in_section = false;
  std::optional<std::size_t> last_in_first_section;
  bool past_first_section = false;
  for (std::string& line : SplitString(std::string(text), '\n'))
  {
    const std::string_view trimmed = StripWhitespace(line);
    if (trimmed.starts_with('['))
    {
      past_first_section = past_first_section || (in_section && !IsVRSectionHeader(trimmed));
      in_section = IsVRSectionHeader(trimmed);
    }
    // Also where it was given a second time.
    if (in_section && IsKey(trimmed, key))
      continue;
    lines.push_back(std::move(line));
    // The first one only is read. After its last line with something in it.
    if (in_section && !past_first_section && !StripWhitespace(lines.back()).empty())
      last_in_first_section = lines.size() - 1;
  }
  while (!lines.empty() && StripWhitespace(lines.back()).empty())
    lines.pop_back();

  if (value)
  {
    std::string line = fmt::format("{} = {}", key, *value);
    if (last_in_first_section)
    {
      lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*last_in_first_section) + 1,
                   std::move(line));
    }
    else
    {
      if (!lines.empty())
        lines.emplace_back();
      lines.push_back(fmt::format("[{}]", VR_SECTION_NAME));
      lines.push_back(std::move(line));
    }
  }
  else if (last_in_first_section && *last_in_first_section < lines.size() &&
           IsVRSectionHeader(StripWhitespace(lines[*last_in_first_section])))
  {
    // Nothing is left in it.
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*last_in_first_section));
    while (!lines.empty() && StripWhitespace(lines.back()).empty())
      lines.pop_back();
  }

  std::string result;
  for (const std::string& line : lines)
    result.append(line).push_back('\n');
  return result;
}

bool IsGameRunning()
{
  return Config::GetLayer(Config::LayerType::LocalGame) != nullptr &&
         SConfig::GetInstance().GetGameID() != DEFAULT_GAME_ID;
}

bool HasGameValue(const Config::Location& location, Store store)
{
  if (store == Store::VRFile)
    return SConfig::GetInstance().HasGameVRSetting(location.key);
  if (store != Store::LocalGame)
    return false;
  for (const Config::LayerType type : {Config::LayerType::LocalGame, Config::LayerType::GlobalGame})
  {
    const auto layer = Config::GetLayer(type);
    if (layer && layer->Exists(location))
      return true;
  }
  return false;
}

bool HasOwnValue(const Config::Location& location, Store store)
{
  if (store == Store::VRFile)
    return SConfig::GetInstance().HasOwnGameVRSetting(location.key);
  const auto layer = Config::GetLayer(Config::LayerType::LocalGame);
  return store == Store::LocalGame && layer && layer->Exists(location);
}

void SaveGlobal()
{
  if (const auto layer = Config::GetLayer(Config::LayerType::Base))
    layer->Save();
}

void SetLocalGame(const Config::Location& location, std::optional<std::string> value, bool save)
{
  const auto layer = Config::GetLayer(Config::LayerType::LocalGame);
  if (!layer)
    return;
  if (value ? layer->Set(location, std::move(*value)) : layer->DeleteKey(location))
    Config::OnConfigChanged();
  if (save)
    layer->Save();
}

void SetVRFile(const Config::Location& location, const std::string& value, bool save)
{
  SConfig& config = SConfig::GetInstance();
  config.MarkGameVRSetting(location.key);
  if (save)
    config.SaveGameVRSetting(location.key, value);
}

std::optional<std::string> RemoveFromVRFile(const Config::Location& location)
{
  return SConfig::GetInstance().RemoveGameVRSetting(location.key);
}
}  // namespace GameSettingScope
