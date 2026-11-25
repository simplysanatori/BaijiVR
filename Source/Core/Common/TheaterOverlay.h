// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

// A game's window in Steam's Game Mode as one of SteamVR's overlays, as `vrcmd --overlays` lists it:
// a line is 'KEY' ... size=WxH ... flags=0x... visible VROverlayType_... From Azahar for Frame.

#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Common
{
struct OverlayState
{
  bool picture = false;
  bool in_dashboard = false;
  bool visible = false;

  // The dashboard has a frame for it. Any earlier, a dock request moves the active frame instead,
  // which is Steam's library.
  bool IsShowable() const { return picture && in_dashboard; }
};

// Empty when Steam's app ID is not one.
inline std::string GetGameOverlayKey(std::string_view app_id)
{
  if (app_id.empty() || app_id.size() > 10 || app_id == "0" ||
      !std::ranges::all_of(app_id, [](char c) { return c >= '0' && c <= '9'; }))
  {
    return {};
  }
  return "valve.steam.desktopgame." + std::string(app_id);
}

inline std::optional<std::string_view> FindOverlayLine(std::string_view listing,
                                                       std::string_view key)
{
  const std::string prefix = "'" + std::string(key) + "' ";
  while (!listing.empty())
  {
    const size_t end = listing.find('\n');
    const std::string_view line = listing.substr(0, end);
    if (line.starts_with(prefix))
      return line;
    if (end == std::string_view::npos)
      break;
    listing.remove_prefix(end + 1);
  }
  return std::nullopt;
}

inline OverlayState ParseOverlayLine(std::string_view line)
{
  constexpr uint64_t VISIBLE_IN_DASHBOARD = 0x8000;
  // The number after `name`, which is taken off `text` with it. 0: there is none.
  const auto take_number = [](std::string_view* text, std::string_view name, int base) -> uint64_t {
    const size_t at = text->find(name);
    if (at == std::string_view::npos)
      return 0;
    text->remove_prefix(at + name.size());
    uint64_t value = 0;
    const char* const end = text->data() + text->size();
    text->remove_prefix(std::from_chars(text->data(), end, value, base).ptr - text->data());
    return value;
  };

  OverlayState state;
  std::string_view size = line;
  const uint64_t width = take_number(&size, " size=", 10);
  state.picture = width > 0 && size.starts_with('x') && take_number(&size, "x", 10) > 0;
  std::string_view flags = line;
  state.in_dashboard = (take_number(&flags, " flags=0x", 16) & VISIBLE_IN_DASHBOARD) != 0;
  state.visible = line.find(" visible VROverlayType") != std::string_view::npos;
  return state;
}
}  // namespace Common
