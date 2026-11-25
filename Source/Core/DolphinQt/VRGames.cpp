// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/VRGames.h"

#include <ctime>
#include <memory>
#include <utility>
#include <vector>

#include <QTimer>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/TimePlayed.h"
#include "DolphinQt/GameList/GameListModel.h"
#include "DolphinQt/Settings.h"
#include "UICommon/GameFile.h"
#include "VideoCommon/VR/FrameUI/GamesHost.h"

namespace
{
// A scan brings many games one after another.
constexpr int GATHER_MS = 300;
}  // namespace

VRGames::VRGames(const GameListModel& model, Play play, QObject* parent)
    : QObject(parent), m_model(model), m_play(std::move(play)),
      m_store(File::GetUserPath(D_CONFIG_IDX) + "VRGames.ini"), m_timer(new QTimer(this))
{
  m_timer->setSingleShot(true);
  m_timer->setInterval(GATHER_MS);
  connect(m_timer, &QTimer::timeout, this, &VRGames::Publish);
  const auto schedule = [this] {
    if (!m_timer->isActive())
      m_timer->start();
  };
  connect(&m_model, &QAbstractItemModel::rowsInserted, this, schedule);
  connect(&m_model, &QAbstractItemModel::rowsRemoved, this, schedule);
  connect(&m_model, &QAbstractItemModel::dataChanged, this, schedule);
  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this, [this](Core::State state) {
    if (state == Core::State::Running && !std::exchange(m_started, true))
      OnStarted();
    else if (state == Core::State::Uninitialized)
      m_started = false;
  });

  VR::UI::SetGamesHost(
      {.play = [this](const std::string& path) { return PlayGame(path); },
       .toggle_favourite = [this](const std::string& path) { ToggleFavourite(path); },
       .publish = [this] { Publish(); }});
  schedule();
}

VRGames::~VRGames()
{
  VR::UI::SetGamesHost({});
}

void VRGames::SetRunning(const std::string& path)
{
  m_running = path;
}

void VRGames::OnStarted()
{
  const std::shared_ptr<const UICommon::GameFile> game = m_model.FindGame(m_running);
  const std::string key =
      game ? UICommon::GetVRGameKey(*game) : SConfig::GetInstance().GetGameID();
  if (!key.empty())
    m_store.SetLastPlayed(key, std::time(nullptr));
  Publish();
}

void VRGames::Publish()
{
  m_timer->stop();
  std::vector<std::shared_ptr<const UICommon::GameFile>> games;
  const int count = m_model.rowCount(QModelIndex());
  for (int i = 0; i < count; ++i)
    games.push_back(m_model.GetGameFile(i));
  const TimePlayed time_played;
  auto list = std::make_shared<const FrameXr::Menu::GamesSnapshot>(
      m_list.Make(games, {m_model.GetTitleDatabase(), m_store, time_played, m_missing}));
  std::string running;
  if (!Core::IsUninitialized(Core::System::GetInstance()))
    running = UICommon::FindRunningVRGame(*list, m_running, SConfig::GetInstance().GetGameID());
  VR::UI::PublishGames(std::move(list), std::move(running));
}

void VRGames::ToggleFavourite(const std::string& path)
{
  const std::shared_ptr<const UICommon::GameFile> game = m_model.FindGame(path);
  if (!game)
    return;
  const std::string key = UICommon::GetVRGameKey(*game);
  m_store.SetFavourite(key, !m_store.IsFavourite(key));
  Publish();
}

std::string VRGames::PlayGame(const std::string& path)
{
  if (!File::Exists(path))
  {
    m_missing.insert(path);
    Publish();
    return "File Not Found";
  }
  m_missing.erase(path);
  return m_play(path);
}
