// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "Common/Config/Config.h"
#include "Common/StringUtil.h"

// Settings for every game and for the running game only. Host thread.
namespace GameSettingScope
{
// Where a game's own value is kept.
enum class Store
{
  Global,     // it has none
  VRFile,     // GameSettingsVR, under the setting's key
  LocalGame,  // GameSettings
};

// The running game has a value, so that the global one is not used.
bool HasGameValue(const Config::Location& location, Store store);
// The player's, for this game. The others are from a bundled profile or a file for several games.
bool HasOwnValue(const Config::Location& location, Store store);

// Detail of the templates below.
bool IsGameRunning();
void SetLocalGame(const Config::Location& location, std::optional<std::string> value, bool save);
void SetVRFile(const Config::Location& location, const std::string& value, bool save);
// The value there still is.
std::optional<std::string> RemoveFromVRFile(const Config::Location& location);

// A GameSettingsVR file's text with a key set, or removed.
std::string EditVRSettings(std::string_view text, std::string_view key,
                           const std::optional<std::string>& value);
bool IsVRSectionHeader(std::string_view line);

// A change that was only for this run (hot keys) does not stay in front of it.
template <typename T>
void SetGlobal(const Config::Info<T>& info, const T& value, Store store)
{
  Config::SetBase(info, value);
  if (!HasGameValue(info.GetLocation(), store))
    Config::DeleteKey(Config::LayerType::CurrentRun, info);
}

// Without `save`, until the game ends (a slider that is being moved).
template <typename T>
void SetForGame(const Config::Info<T>& info, const T& value, Store store, bool save = true)
{
  if (!IsGameRunning())
    return;
  if (store == Store::LocalGame)
  {
    SetLocalGame(info.GetLocation(), ValueToString(value), save);
    Config::DeleteKey(Config::LayerType::CurrentRun, info);
  }
  else if (store == Store::VRFile)
  {
    Config::SetCurrent(info, value);
    SetVRFile(info.GetLocation(), ValueToString(value), save);
  }
}

// Where the value in use is: a setting that is changed outside of the menu's Global and Per-Game.
template <typename T>
void SetWhereItIs(const Config::Info<T>& info, const T& value, Store store)
{
  if (HasGameValue(info.GetLocation(), store))
    SetForGame(info, value, store);
  else
    SetGlobal(info, value, store);
}

template <typename T>
void UseGlobal(const Config::Info<T>& info, Store store)
{
  if (!IsGameRunning() || !HasOwnValue(info.GetLocation(), store))
    return;
  std::optional<T> left;
  if (store == Store::LocalGame)
    SetLocalGame(info.GetLocation(), std::nullopt, true);
  else if (const auto text = RemoveFromVRFile(info.GetLocation()))
    left = Config::detail::TryParse<T>(*text);
  if (left)
    Config::SetCurrent(info, *left);
  else
    Config::DeleteKey(Config::LayerType::CurrentRun, info);
}
}  // namespace GameSettingScope
