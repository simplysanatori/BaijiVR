// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <set>
#include <string>

#include <QObject>

#include "UICommon/GameCovers.h"
#include "UICommon/VRGames.h"
#include "VideoCommon/VR/FrameUI/GamesHost.h"

class GameListModel;
class QTimer;

// The window's side of the VR menu's Games tab (VideoCommon/VR/FrameUI/GamesHost.h): publishes the
// game list, gives the window's list the same covers, and keeps the favourites and when a game was played last.
class VRGames final : public QObject
{
public:
  // Stops the running game and starts another. Empty when it is on its way, or else why not.
  using Play = std::function<std::string(const std::string& path)>;

  VRGames(GameListModel& model, Play play, QObject* parent);
  ~VRGames() override;

  // The file a game is started from, before its boot; empty when there is none.
  void SetRunning(const std::string& path);

private:
  void Publish();
  void ToggleFavourite(const std::string& path);
  std::string PlayGame(const std::string& path);
  void OnStarted();
  void AddFolder(const std::string& path);
  void ChooseCover(const std::string& path, VR::UI::CoverChoice choice, const std::string& image);
  void SetCover(const std::string& path, UICommon::VRGameStore::Cover cover);
  void OnCoversDownloaded(int asked, int found, int unanswered);
  void OnCoverDownloaded(const std::string& path, UICommon::CoverKind kind, bool found,
                         bool unanswered);

  GameListModel& m_model;
  Play m_play;
  UICommon::VRGameStore m_store;
  UICommon::VRGameList m_list;
  std::set<std::string> m_missing;
  std::string m_running;
  bool m_started = false;
  QTimer* m_timer;
};
