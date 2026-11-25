// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <set>
#include <string>

#include <QObject>

#include "UICommon/VRGames.h"

class GameListModel;
class QTimer;

// The window's side of the VR menu's Games tab (VideoCommon/VR/FrameUI/GamesHost.h): publishes the
// game list, and keeps the favourites and when a game was played last.
class VRGames final : public QObject
{
public:
  // Stops the running game and starts another. Empty when it is on its way, or else why not.
  using Play = std::function<std::string(const std::string& path)>;

  VRGames(const GameListModel& model, Play play, QObject* parent);
  ~VRGames() override;

  // The file a game is started from, before its boot; empty when there is none.
  void SetRunning(const std::string& path);

private:
  void Publish();
  void ToggleFavourite(const std::string& path);
  std::string PlayGame(const std::string& path);
  void OnStarted();

  const GameListModel& m_model;
  Play m_play;
  UICommon::VRGameStore m_store;
  UICommon::VRGameList m_list;
  std::set<std::string> m_missing;
  std::string m_running;
  bool m_started = false;
  QTimer* m_timer;
};
