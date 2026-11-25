// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Autosave.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <system_error>

#include <fmt/format.h>

#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"

#include "Core/AchievementManager.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/State.h"
#include "Core/System.h"

#include "VideoCommon/FrameDumper.h"

namespace Autosave
{
namespace
{
std::mutex s_mutex;
std::atomic<bool> s_queued{false};
Scheduler s_scheduler;

s64 NowMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::tm LocalTm(s64 unix_seconds)
{
  const std::time_t time = unix_seconds;
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif
  return tm;
}

s64 LocalDay(s64 unix_seconds)
{
  const std::tm tm = LocalTm(unix_seconds);
  const std::chrono::year_month_day date{std::chrono::year{tm.tm_year + 1900},
                                         std::chrono::month{static_cast<unsigned>(tm.tm_mon + 1)},
                                         std::chrono::day{static_cast<unsigned>(tm.tm_mday)}};
  return std::chrono::sys_days{date}.time_since_epoch().count();
}

std::string GetDirectory()
{
  return File::GetUserPath(D_STATESAVES_IDX) + "autosave/";
}

u32 GetKeep()
{
  return std::clamp(Config::Get(Config::MAIN_AUTOSAVE_KEEP), 1u, 10u);
}

std::vector<Entry> ListAll(const std::string& game_id)
{
  std::vector<Entry> entries;
  const std::string prefix = fmt::format("{}{}", game_id, FILE_MARK);
  std::error_code error;
  for (const auto& file :
       std::filesystem::directory_iterator(StringToPath(GetDirectory()), error))
  {
    const std::string name = PathToString(file.path().filename());
    if (!name.starts_with(prefix) || !name.ends_with(".sav"))
      continue;
    const auto written = file.last_write_time(error);
    if (error)
      continue;

    Entry& entry = entries.emplace_back();
    entry.path = GetDirectory() + name;
    entry.menu = name == prefix + "menu.sav";
    entry.time = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::file_clock::to_sys(written).time_since_epoch())
                     .count();
    const std::string picture = entry.path.substr(0, entry.path.size() - 4) + ".png";
    if (File::Exists(picture))
      entry.picture = picture;
  }
  std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
    return a.time != b.time ? a.time > b.time : a.path > b.path;
  });
  return entries;
}

void Prune(const std::string& game_id)
{
  u32 kept = 0;
  for (const Entry& entry : ListAll(game_id))
  {
    if (entry.menu || ++kept <= GetKeep())
      continue;
    File::Delete(entry.path);
    File::Delete(entry.path + ".dtm", File::IfAbsentBehavior::NoConsoleWarning);
    if (!entry.picture.empty())
      File::Delete(entry.picture);
  }
}

bool CanSave(Core::System& system)
{
  return Config::Get(Config::MAIN_AUTOSAVE_ENABLED) && Core::IsRunning(system) &&
         !NetPlay::IsNetPlayRunning() && !system.GetMovie().IsMovieActive() &&
         !AchievementManager::GetInstance().IsHardcoreModeActive();
}
}  // namespace

void Scheduler::Field(s64 now_ms)
{
  if (!m_booted)
  {
    *this = {};
    m_booted = true;
    m_boot_ms = now_ms;
  }
  else
  {
    // A pause or a hitch is no play.
    m_play_ms += std::clamp<s64>(now_ms - m_last_field_ms, 0, 500);
  }
  m_last_field_ms = now_ms;
}

void Scheduler::Stop()
{
  *this = {};
}

bool Scheduler::IsAllowed(Reason reason, s64 now_ms, bool paused) const
{
  if (!m_booted || now_ms - m_boot_ms < BOOT_GRACE_MS ||
      (m_fired && now_ms - m_last_fired_ms < THROTTLE_MS))
  {
    return false;
  }
  // A paused game is a sound state to close; one that hangs is not.
  const bool recent = now_ms - m_last_field_ms <= RECENT_MS;
  return reason == Reason::Quit ? paused || recent : !paused && recent;
}

void Scheduler::Fired(s64 now_ms)
{
  m_fired = true;
  m_last_fired_ms = now_ms;
  m_play_ms = 0;
}

void OnField(Core::System& system)
{
  const s64 now = NowMs();
  {
    std::lock_guard lock(s_mutex);
    s_scheduler.Field(now);
    const s64 every = s64{std::max(Config::Get(Config::MAIN_AUTOSAVE_MINUTES), 1u)} * 60'000;
    if (!s_scheduler.IsDue(every) || !s_scheduler.IsAllowed(Reason::Timer, now, false))
      return;
  }
  if (!CanSave(system) || s_queued.exchange(true))
    return;
  Core::QueueHostJob([](Core::System& host_system) {
    Request(host_system, Reason::Timer);
    s_queued = false;
  });
}

bool Request(Core::System& system, Reason reason)
{
  if (!CanSave(system))
    return false;
  const s64 now = NowMs();
  {
    std::lock_guard lock(s_mutex);
    const bool paused = Core::GetState(system) == Core::State::Paused;
    if (!s_scheduler.IsAllowed(reason, now, paused))
      return false;
    s_scheduler.Fired(now);
  }

  const std::string game_id = SConfig::GetInstance().GetGameID();
  std::string name = "menu";
  if (reason != Reason::MenuOpen)
  {
    const std::tm tm = LocalTm(std::time(nullptr));
    name = fmt::format("{:04}{:02}{:02}-{:02}{:02}{:02}", tm.tm_year + 1900, tm.tm_mon + 1,
                       tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  }
  const std::string base = fmt::format("{}{}{}{}", GetDirectory(), game_id, FILE_MARK, name);
  INFO_LOG_FMT(CORE, "Autosave: {}", base);
  File::CreateFullPath(base);
  Prune(game_id);

  // Not moved to the "undo save state" file.
  File::Delete(base + ".sav", File::IfAbsentBehavior::NoConsoleWarning);
  File::Delete(base + ".png", File::IfAbsentBehavior::NoConsoleWarning);
  if (reason != Reason::Quit && g_frame_dumper)
    g_frame_dumper->SaveScreenshot(base + ".png");

  State::SaveOptions options;
  options.wait_for_completion = reason == Reason::Quit;
  State::SaveAs(system, base + ".sav", options);
  return true;
}

void BeforeStop(Core::System& system)
{
  Request(system, Reason::Quit);
  End();
}

void End()
{
  std::lock_guard lock(s_mutex);
  s_scheduler.Stop();
}

std::vector<Entry> List(const std::string& game_id)
{
  std::vector<Entry> entries = ListAll(game_id);
  u32 kept = 0;
  std::erase_if(entries, [&kept](const Entry& e) { return !e.menu && ++kept > GetKeep(); });
  return entries;
}

std::string Ago(s64 then, s64 now, s64 then_day, s64 now_day, const std::string& date_time)
{
  const s64 seconds = now - then;
  if (seconds < 60)
    return "Just Now";
  if (seconds < 3600)
  {
    const s64 minutes = seconds / 60;
    return minutes == 1 ? "1 Minute Ago" : fmt::format("{} Minutes Ago", minutes);
  }
  if (then_day == now_day)
  {
    const s64 hours = seconds / 3600;
    return hours == 1 ? "1 Hour Ago" : fmt::format("{} Hours Ago", hours);
  }
  return then_day + 1 == now_day ? "Yesterday" : date_time;
}

std::string AgoLocal(s64 then, s64 now)
{
  return Ago(then, now, LocalDay(then), LocalDay(now), LocalDateTime(then));
}

std::string LocalDateTime(s64 unix_seconds)
{
  const std::tm tm = LocalTm(unix_seconds);
  return fmt::format("{:04}-{:02}-{:02} {:02}:{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min);
}

std::string LocalTime(s64 unix_seconds)
{
  const std::tm tm = LocalTm(unix_seconds);
  return fmt::format("{:02}:{:02}", tm.tm_hour, tm.tm_min);
}
}  // namespace Autosave
