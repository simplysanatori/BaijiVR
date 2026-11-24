// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"

namespace Core
{
class System;
}

// Save states made on their own (Azahar for Frame's rules): every few minutes of play, when the VR
// menu opens and when the game is closed. Each is a file of its own in <StateSaves>/autosave/, never
// one of the slots. A game keeps its newest few; the one made when the menu opens has a file of its
// own beside them, so opening the menu to go back loses no older one.
namespace Autosave
{
// In every autosave's file name.
inline constexpr std::string_view FILE_MARK = ".auto.";

enum class Reason
{
  Timer,
  MenuOpen,
  Quit,
};

struct Entry
{
  std::string path;
  std::string picture;  // a PNG, empty when there is none
  s64 time = 0;         // Unix seconds
  bool menu = false;

  bool operator==(const Entry&) const = default;
};

// When one may be made. Times are milliseconds of any steady clock.
class Scheduler
{
public:
  // At most one a minute, and none in the first.
  static constexpr s64 THROTTLE_MS = 60'000;
  static constexpr s64 BOOT_GRACE_MS = 60'000;
  // The game counts as running this long after it last drew.
  static constexpr s64 RECENT_MS = 1'000;

  // The game runs at `now_ms`: called at each of its fields. Starts with the first one.
  void Field(s64 now_ms);
  void Stop();
  bool IsDue(s64 every_ms) const { return m_booted && m_play_ms >= every_ms; }
  bool IsAllowed(Reason reason, s64 now_ms, bool paused) const;
  void Fired(s64 now_ms);

private:
  bool m_booted = false;
  bool m_fired = false;
  s64 m_boot_ms = 0;
  s64 m_last_field_ms = 0;
  s64 m_last_fired_ms = 0;
  s64 m_play_ms = 0;
};

// CPU thread, at each field.
void OnField(Core::System& system);
// Host thread. True when one is being made.
bool Request(Core::System& system, Reason reason);
// Host thread, as the game is about to stop: the autosave of a clean stop, and the end of the timer.
void BeforeStop(Core::System& system);

// A game's autosaves, newest first: the menu's and the newest few.
std::vector<Entry> List(const std::string& game_id);

// "Just Now", "12 Minutes Ago", "3 Hours Ago", "Yesterday", then the date and time.
std::string Ago(s64 then, s64 now, s64 then_day, s64 now_day, const std::string& date_time);
std::string AgoLocal(s64 then, s64 now);
std::string LocalDateTime(s64 unix_seconds);
std::string LocalTime(s64 unix_seconds);
}  // namespace Autosave
