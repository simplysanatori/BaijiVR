// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <QFileSystemWatcher>
#include <QMap>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

#include "Common/CommonTypes.h"
#include "Common/Event.h"
#include "Common/WorkQueueThread.h"
#include "UICommon/GameFileCache.h"

namespace UICommon
{
class GameFile;
}

// Watches directories and loads GameFiles in a separate thread.
// To use this, just add directories using AddDirectory, and listen for the
// GameLoaded and GameRemoved signals.
class GameTracker final : public QFileSystemWatcher
{
  Q_OBJECT

public:
  explicit GameTracker(QObject* parent = nullptr);

  // A GameTracker won't emit any signals until this function has been called.
  // Before you call this function, make sure to call AddDirectory for all
  // directories you want to track, otherwise games will briefly disappear
  // until you call AddDirectory and the GameTracker finishes scanning the file system.
  void Start();

  void AddDirectory(const QString& dir);
  void RemoveDirectory(const QString& dir);
  void RefreshAll();
  void PurgeCache();

signals:
  void GameLoaded(const std::shared_ptr<const UICommon::GameFile>& game);
  void GameUpdated(const std::shared_ptr<const UICommon::GameFile>& game);
  void GameRemoved(const std::string& path);

private:
  void LoadCache();
  void StartInternal();
  void UpdateDirectory(const QString& dir);
  void UpdateFile(const QString& path);
  void AddDirectoryInternal(const QString& dir);
  void RemoveDirectoryInternal(const QString& dir);
  void UpdateDirectoryInternal(const QString& dir);
  void UpdateFileInternal(const QString& path);
  QSet<QString> FindMissingFiles(const QString& dir);
  void LoadGame(const QString& path);

  // The question about covers for a directory that the player added.
  void PlayerAddedDirectory(const QString& dir);
  void CountCoversInternal(const QString& dir, u64 generation);
  void CoversCounted(u64 generation, std::vector<std::string> games);
  void AskAboutCovers();
  // Games that a scan found and the cache did not know: tracker thread, sent after each command.
  void FlushNewGames();
  void NewGamesFound(std::vector<std::string> games);

  bool AddPath(const QString& path);
  bool RemovePath(const QString& path);

  enum class CommandType
  {
    LoadCache,
    Start,
    AddDirectory,
    RemoveDirectory,
    UpdateDirectory,
    UpdateFile,
    UpdateMetadata,
    ResumeProcessing,
    PurgeCache,
    BeginRefresh,
    EndRefresh,
    CountCovers,
    DownloadCovers,
  };

  struct Command
  {
    CommandType type;
    QString path;
    u64 generation = 0;
    std::vector<std::string> games;
  };

  // game path -> directories that track it
  QMap<QString, QSet<QString>> m_tracked_files;
  QVector<QString> m_tracked_paths;
  Common::WorkQueueThread<Command> m_load_thread;
  UICommon::GameFileCache m_cache;
  Common::Event m_cache_loaded_event;
  Common::Event m_initial_games_emitted_event;
  bool m_initial_games_emitted = false;
  bool m_started = false;
  bool m_needs_purge = false;
  bool m_refresh_in_progress = false;
  std::atomic_bool m_processing_halted = false;

  // Main thread. Directories the player added whose scans have not been counted yet, and the games
  // in them that lack a cover, and what rescans found. One question when the last scan is counted. A
  // refresh drops the counts (the commands waiting for the tracker's thread go with it): the
  // generation tells its answers.
  u64 m_cover_generation = 0;
  int m_cover_scans_pending = 0;
  std::vector<std::string> m_cover_games;
  bool m_cover_question_open = false;
  int m_cover_retries = 0;
  QTimer* m_cover_timer = nullptr;  // gathers what a scan or the directory watcher finds
  std::vector<std::string> m_new_games;  // tracker thread
};

Q_DECLARE_METATYPE(std::shared_ptr<const UICommon::GameFile>)
Q_DECLARE_METATYPE(std::string)
