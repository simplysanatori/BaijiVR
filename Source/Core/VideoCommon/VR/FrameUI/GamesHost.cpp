// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/FrameUI/GamesHost.h"

#include <atomic>
#include <mutex>
#include <utility>

namespace VR::UI
{
namespace
{
GamesHost s_host;
std::function<std::string()> s_save_report;

std::mutex s_mutex;
PublishedGames s_published;
std::atomic<u32> s_serial{0};
}  // namespace

void SetGamesHost(GamesHost host)
{
  s_host = std::move(host);
}

const GamesHost* GetGamesHost()
{
  return s_host.play ? &s_host : nullptr;
}

void SetReportSaver(std::function<std::string()> save)
{
  s_save_report = std::move(save);
}

std::string SaveReport()
{
  return s_save_report ? s_save_report() : std::string();
}

void PublishGames(FrameXr::Menu::GamesList list, std::string running)
{
  std::lock_guard lock(s_mutex);
  // The same list again stays the one that was drawn.
  if (!s_published.list || !list || *s_published.list != *list)
    s_published.list = std::move(list);
  s_published.running = std::move(running);
  s_published.serial = ++s_serial;
}

PublishedGames GetPublishedGames()
{
  std::lock_guard lock(s_mutex);
  return s_published;
}

u32 GetPublishedGamesSerial()
{
  return s_serial;
}
}  // namespace VR::UI
