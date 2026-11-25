// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/GameList/GameTracker.h"

#include <algorithm>

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>

#include "Common/Config/Config.h"

#include "Core/Config/UISettings.h"
#include "Core/Core.h"
#include "Core/System.h"

#include "DiscIO/DirectoryBlob.h"

#include "DolphinQt/QtUtils/ModalMessageBox.h"
#include "DolphinQt/QtUtils/QueueOnObject.h"

#include "DolphinQt/Settings.h"

#include "UICommon/GameCovers.h"
#include "UICommon/GameFile.h"

// NOTE: Qt likes to be case-sensitive here even though it shouldn't be thus this ugly regex hack
static const QStringList game_filters{
    QStringLiteral("*.[gG][cC][mM]"),     QStringLiteral("*.[bB][iI][nN]"),
    QStringLiteral("*.[iI][sS][oO]"),     QStringLiteral("*.[tT][gG][cC]"),
    QStringLiteral("*.[cC][iI][sS][oO]"), QStringLiteral("*.[gG][cC][zZ]"),
    QStringLiteral("*.[wW][bB][fF][sS]"), QStringLiteral("*.[wW][iI][aA]"),
    QStringLiteral("*.[rR][vV][zZ]"),     QStringLiteral("hif_000000.nfs"),
    QStringLiteral("*.[wW][aA][dD]"),     QStringLiteral("*.[eE][lL][fF]"),
    QStringLiteral("*.[dD][oO][lL]"),     QStringLiteral("*.[jJ][sS][oO][nN]")};

GameTracker::GameTracker(QObject* parent) : QFileSystemWatcher(parent)
{
  qRegisterMetaType<std::shared_ptr<const UICommon::GameFile>>();
  qRegisterMetaType<std::string>();
  qRegisterMetaType<UICommon::CoverKind>();

  connect(qApp, &QApplication::aboutToQuit, this, [this] {
    m_processing_halted = true;
    m_load_thread.StopAndCancel();
  });
  connect(this, &QFileSystemWatcher::directoryChanged, this, &GameTracker::UpdateDirectory);
  connect(this, &QFileSystemWatcher::fileChanged, this, &GameTracker::UpdateFile);
  connect(&Settings::Instance(), &Settings::AutoRefreshToggled, this, [] {
    const auto paths = Settings::Instance().GetPaths();

    for (const auto& path : paths)
    {
      Settings::Instance().RemovePath(path);
      Settings::Instance().AddPath(path, false);
    }
  });
  connect(&Settings::Instance(), &Settings::PathAddedByPlayer, this,
          &GameTracker::PlayerAddedDirectory);

  connect(&Settings::Instance(), &Settings::MetadataRefreshRequested, this,
          [this] { m_load_thread.EmplaceItem(Command{CommandType::UpdateMetadata, {}}); });

  m_cover_timer = new QTimer(this);
  m_cover_timer->setSingleShot(true);
  m_cover_timer->setInterval(3000);
  connect(m_cover_timer, &QTimer::timeout, this, [this] {
    if (m_cover_scans_pending == 0 && !m_cover_question_open && !m_cover_games.empty())
      AskAboutCovers();
  });

  m_load_thread.Reset("GameList Tracker", [this](Command command) {
    switch (command.type)
    {
    case CommandType::LoadCache:
      LoadCache();
      break;
    case CommandType::Start:
      StartInternal();
      break;
    case CommandType::AddDirectory:
      AddDirectoryInternal(command.path);
      break;
    case CommandType::RemoveDirectory:
      RemoveDirectoryInternal(command.path);
      break;
    case CommandType::UpdateDirectory:
      UpdateDirectoryInternal(command.path);
      break;
    case CommandType::UpdateFile:
      UpdateFileInternal(command.path);
      break;
    case CommandType::UpdateMetadata:
      m_cache.UpdateAdditionalMetadata(
          [this](const std::shared_ptr<const UICommon::GameFile>& game) { emit GameUpdated(game); },
          m_processing_halted);
      QueueOnObject(this, [] { Settings::Instance().NotifyMetadataRefreshComplete(); });
      break;
    case CommandType::ResumeProcessing:
      m_processing_halted = false;
      break;
    case CommandType::PurgeCache:
      m_cache.Clear(UICommon::GameFileCache::DeleteOnDisk::Yes);
      break;
    case CommandType::BeginRefresh:
      m_refresh_in_progress = true;
      QueueOnObject(this, [] { Settings::Instance().NotifyRefreshGameListStarted(); });
      for (auto& file : m_tracked_files.keys())
        emit GameRemoved(file.toStdString());
      m_tracked_files.clear();
      break;
    case CommandType::EndRefresh:
      m_refresh_in_progress = false;
      m_cache.Save();
      QueueOnObject(this, [] { Settings::Instance().NotifyRefreshGameListComplete(); });
      break;
    case CommandType::CountCovers:
      CountCoversInternal(command.path, command.generation);
      break;
    case CommandType::DownloadCovers:
      // Whatever the setting says: the player was asked.
      if (m_cache.DownloadCovers(command.games, [this](const auto& game) { emit GameUpdated(game); },
                                 m_processing_halted))
      {
        m_cache.Save();
      }
      break;
    case CommandType::DownloadAllCovers:
      DownloadAllCoversInternal();
      break;
    case CommandType::DownloadCover:
      DownloadCoverInternal(command.path.toStdString(), command.cover);
      break;
    }
    FlushNewGames();
  });

  m_load_thread.EmplaceItem(Command{CommandType::LoadCache, {}});

  // TODO: When language changes, reload m_title_database and call m_cache.UpdateAdditionalMetadata
}

void GameTracker::LoadCache()
{
  m_cache.Load();
  m_cache_loaded_event.Set();
}

void GameTracker::Start()
{
  if (m_initial_games_emitted)
    return;

  m_initial_games_emitted = true;

  m_load_thread.EmplaceItem(Command{CommandType::Start, {}});

  m_cache_loaded_event.Wait();

  m_cache.ForEach(
      [this](const std::shared_ptr<const UICommon::GameFile>& game) { emit GameLoaded(game); });

  m_initial_games_emitted_event.Set();
}

void GameTracker::StartInternal()
{
  if (m_started)
    return;

  m_started = true;

  QueueOnObject(this, [] { Settings::Instance().NotifyRefreshGameListStarted(); });

  std::vector<std::string> paths;
  paths.reserve(m_tracked_files.size());
  for (const QString& path : m_tracked_files.keys())
    paths.push_back(path.toStdString());

  const auto emit_game_loaded = [this](const std::shared_ptr<const UICommon::GameFile>& game) {
    emit GameLoaded(game);
  };
  const auto emit_game_updated = [this](const std::shared_ptr<const UICommon::GameFile>& game) {
    emit GameUpdated(game);
  };
  const auto emit_game_removed = [this](const std::string& path) { emit GameRemoved(path); };

  m_initial_games_emitted_event.Wait();

  const auto emit_new_game = [this, &emit_game_loaded](
                                 const std::shared_ptr<const UICommon::GameFile>& game) {
    m_new_games.push_back(game->GetFilePath());
    emit_game_loaded(game);
  };
  bool cache_updated =
      m_cache.Update(paths, emit_new_game, emit_game_removed, m_processing_halted);
  cache_updated |= m_cache.UpdateAdditionalMetadata(emit_game_updated, m_processing_halted);
  if (cache_updated)
    m_cache.Save();

  QueueOnObject(this, [] { Settings::Instance().NotifyMetadataRefreshComplete(); });
  QueueOnObject(this, [] { Settings::Instance().NotifyRefreshGameListComplete(); });
}

bool GameTracker::AddPath(const QString& dir)
{
  if (Settings::Instance().IsAutoRefreshEnabled())
    QueueOnObject(this, [this, dir] { return addPath(dir); });

  m_tracked_paths.push_back(dir);

  return true;
}

bool GameTracker::RemovePath(const QString& dir)
{
  if (Settings::Instance().IsAutoRefreshEnabled())
    QueueOnObject(this, [this, dir] { return removePath(dir); });

  const auto index = m_tracked_paths.indexOf(dir);

  if (index == -1)
    return false;

  m_tracked_paths.remove(index);

  return true;
}

void GameTracker::AddDirectory(const QString& dir)
{
  m_load_thread.EmplaceItem(Command{CommandType::AddDirectory, dir});
}

void GameTracker::RemoveDirectory(const QString& dir)
{
  m_load_thread.EmplaceItem(Command{CommandType::RemoveDirectory, dir});
}

void GameTracker::RefreshAll()
{
  ++m_cover_generation;
  m_cover_scans_pending = 0;
  m_processing_halted = true;
  m_load_thread.Cancel();
  m_load_thread.EmplaceItem(Command{CommandType::ResumeProcessing, {}});

  if (m_needs_purge)
  {
    m_load_thread.EmplaceItem(Command{CommandType::PurgeCache, {}});
    m_needs_purge = false;
  }

  m_load_thread.EmplaceItem(Command{CommandType::BeginRefresh});

  for (const QString& dir : Settings::Instance().GetPaths())
  {
    m_load_thread.EmplaceItem(Command{CommandType::RemoveDirectory, dir});
    m_load_thread.EmplaceItem(Command{CommandType::AddDirectory, dir});
  }

  m_load_thread.EmplaceItem(Command{CommandType::EndRefresh});
}

void GameTracker::UpdateDirectory(const QString& dir)
{
  m_load_thread.EmplaceItem(Command{CommandType::UpdateDirectory, dir});
}

void GameTracker::UpdateFile(const QString& dir)
{
  m_load_thread.EmplaceItem(Command{CommandType::UpdateFile, dir});
}

void GameTracker::AddDirectoryInternal(const QString& dir)
{
  if (!QFileInfo(dir).exists())
    return;
  AddPath(dir);
  UpdateDirectoryInternal(dir);
}

static std::unique_ptr<QDirIterator> GetIterator(const QString& dir)
{
  return std::make_unique<QDirIterator>(dir, game_filters, QDir::NoFilter,
                                        Config::Get(Config::MAIN_RECURSIVE_ISO_PATHS) ?
                                            QDirIterator::Subdirectories :
                                            QDirIterator::NoIteratorFlags);
}

void GameTracker::RemoveDirectoryInternal(const QString& dir)
{
  RemovePath(dir);
  const auto dir_it = GetIterator(dir);
  while (dir_it->hasNext())
  {
    QString path = QFileInfo(dir_it->next()).canonicalFilePath();
    if (const auto it = m_tracked_files.find(path); it != m_tracked_files.end())
    {
      auto& set = *it;
      set.remove(dir);
      if (set.isEmpty())
      {
        RemovePath(path);
        m_tracked_files.erase(it);
        if (m_started)
          emit GameRemoved(path.toStdString());
      }
    }
  }
}

void GameTracker::UpdateDirectoryInternal(const QString& dir)
{
  const auto dir_it = GetIterator(dir);
  while (dir_it->hasNext() && !m_processing_halted)
  {
    QString path = QFileInfo(dir_it->next()).canonicalFilePath();

    if (const auto it = m_tracked_files.find(path); it != m_tracked_files.end())
    {
      it->insert(dir);
    }
    else
    {
      AddPath(path);
      m_tracked_files[path] = QSet<QString>{dir};
      LoadGame(path);
    }
  }

  for (const auto& missing : FindMissingFiles(dir))
  {
    if (m_processing_halted)
      break;

    auto& tracked_file = m_tracked_files[missing];

    tracked_file.remove(dir);
    if (tracked_file.empty())
    {
      m_tracked_files.remove(missing);
      if (m_started)
        GameRemoved(missing.toStdString());
    }
  }
}

void GameTracker::UpdateFileInternal(const QString& file)
{
  if (QFileInfo(file).exists())
  {
    if (m_started)
      GameRemoved(file.toStdString());
    AddPath(file);
    LoadGame(file);
  }
  else if (RemovePath(file))
  {
    m_tracked_files.remove(file);
    if (m_started)
      emit GameRemoved(file.toStdString());
  }
}

QSet<QString> GameTracker::FindMissingFiles(const QString& dir)
{
  auto it = GetIterator(dir);

  QSet<QString> missing_files;

  for (const auto& key : m_tracked_files.keys())
  {
    if (m_tracked_files[key].contains(dir))
      missing_files.insert(key);
  }

  while (it->hasNext())
  {
    QString path = QFileInfo(it->next()).canonicalFilePath();
    if (m_tracked_files.contains(path))
      missing_files.remove(path);
  }

  return missing_files;
}

void GameTracker::LoadGame(const QString& path)
{
  if (!m_started)
    return;

  const std::string converted_path = path.toStdString();
  if (!DiscIO::ShouldHideFromGameList(converted_path))
  {
    bool cache_changed = false;
    const bool known = m_cache.Find(converted_path) != nullptr;
    auto game = m_cache.AddOrGet(converted_path, &cache_changed);
    if (game && !known)
      m_new_games.push_back(converted_path);
    if (game)
      emit GameLoaded(std::move(game));
    if (cache_changed && !m_refresh_in_progress)
      m_cache.Save();
  }
}

void GameTracker::PurgeCache()
{
  m_needs_purge = true;
  Settings::Instance().RefreshGameList();
}

// Games without a cover are looked for after the scan of the directory the player added, which is
// the command before it, and counted on the tracker's thread, which has the games.
void GameTracker::PlayerAddedDirectory(const QString& dir)
{
  // This runs after the connection that tells the tracker to scan the directory.
  if (!UICommon::COVERS_ARE_SHOWN || Config::Get(Config::MAIN_USE_GAME_COVERS) ||
      Settings::Instance().IsBatchModeEnabled())
    return;
  ++m_cover_scans_pending;
  m_load_thread.EmplaceItem(Command{CommandType::CountCovers, dir, m_cover_generation});
}

void GameTracker::CountCoversInternal(const QString& dir, u64 generation)
{
  std::vector<std::string> games;
  for (auto it = m_tracked_files.cbegin(); it != m_tracked_files.cend(); ++it)
  {
    if (!it->contains(dir))
      continue;
    const auto game = m_cache.Find(it.key().toStdString());
    if (!game)
      continue;
    if (UICommon::NeedsCover(game->GetCoverState()))
      games.push_back(game->GetFilePath());
  }
  QueueOnObject(this, [this, generation, games = std::move(games)]() mutable {
    CoversCounted(generation, std::move(games));
  });
}

void GameTracker::CoversCounted(u64 generation, std::vector<std::string> games)
{
  if (generation != m_cover_generation)
    return;
  --m_cover_scans_pending;
  for (std::string& game : games)
  {
    if (std::ranges::find(m_cover_games, game) == m_cover_games.end())
      m_cover_games.push_back(std::move(game));
  }
  if (m_cover_scans_pending > 0)
    return;
  m_cover_scans_pending = 0;
  if (!m_cover_games.empty() && !m_cover_question_open)
    AskAboutCovers();
}

void GameTracker::AskAboutCovers()
{
  // Never over a running game or in batch mode; the question is skipped then.
  if (!UICommon::COVERS_ARE_SHOWN || Config::Get(Config::MAIN_USE_GAME_COVERS) || Settings::Instance().IsBatchModeEnabled() ||
      Core::GetState(Core::System::GetInstance()) != Core::State::Uninitialized)
  {
    m_cover_games.clear();
    return;
  }

  // Over the window the player is in (the Settings dialog while its Paths page is used). Not over
  // a menu or another message box or file dialog that goes away by itself: the question is asked
  // again a little later.
  QWidget* parent = QApplication::activeModalWidget();
  if (!parent)
    parent = QApplication::activeWindow();
  if (QApplication::activePopupWidget() || qobject_cast<QMessageBox*>(parent) ||
      qobject_cast<QFileDialog*>(parent))
  {
    if (++m_cover_retries <= 600)
      QTimer::singleShot(1000, this, [this] {
        if (m_cover_scans_pending == 0 && !m_cover_question_open && !m_cover_games.empty())
          AskAboutCovers();
      });
    else
      m_cover_games.clear();
    return;
  }
  m_cover_retries = 0;
  std::vector<std::string> games = std::move(m_cover_games);
  m_cover_games.clear();

  // It does not block: the tracker's thread goes on, and so does Qt.
  auto* box = new ModalMessageBox(parent);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setIcon(QMessageBox::Question);
  box->setWindowTitle(tr("Download Thumbnails?"));
  box->setText(games.size() == 1 ?
                   tr("A cover for the game found can be downloaded from libretro-thumbnails on "
                      "GitHub.") :
                   tr("Covers for the %1 games found can be downloaded from libretro-thumbnails on "
                      "GitHub.")
                       .arg(games.size()));
  QPushButton* const download = box->addButton(tr("Download"), QMessageBox::AcceptRole);
  QPushButton* const not_now = box->addButton(tr("Not Now"), QMessageBox::RejectRole);
  box->setDefaultButton(not_now);
  box->setEscapeButton(not_now);

  m_cover_question_open = true;
  const u64 generation = m_cover_generation;
  connect(box, &QMessageBox::finished, this,
          [this, box, download, generation, games = std::move(games)]() mutable {
            if (box->clickedButton() == download)
            {
              m_load_thread.EmplaceItem(
                  Command{CommandType::DownloadCovers, {}, generation, std::move(games)});
            }
          });
  // Also when the window it is over goes away: that is Not Now.
  connect(box, &QObject::destroyed, this, [this] {
    m_cover_question_open = false;
    if (m_cover_scans_pending == 0 && !m_cover_games.empty())
      QueueOnObject(this, [this] { AskAboutCovers(); });
  });
  box->open();
}

void GameTracker::DownloadCovers()
{
  m_load_thread.EmplaceItem(Command{CommandType::DownloadAllCovers, {}});
}

void GameTracker::DownloadCover(const std::string& path, UICommon::CoverKind kind)
{
  m_load_thread.EmplaceItem(
      Command{CommandType::DownloadCover, QString::fromStdString(path), 0, {}, kind});
}

void GameTracker::DownloadAllCoversInternal()
{
  std::vector<std::string> games;
  for (auto it = m_tracked_files.cbegin(); it != m_tracked_files.cend(); ++it)
  {
    const auto game = m_cache.Find(it.key().toStdString());
    if (game && UICommon::NeedsCover(game->GetCoverState()))
      games.push_back(game->GetFilePath());
  }
  if (m_cache.DownloadCovers(games, [this](const auto& game) { emit GameUpdated(game); },
                             m_processing_halted))
  {
    m_cache.Save();
  }
  int found = 0;
  int unanswered = 0;
  for (const std::string& path : games)
  {
    const auto game = m_cache.Find(path);
    const UICommon::CoverState state = game ? game->GetCoverState() : UICommon::CoverState{};
    found += state.has_cached ? 1 : 0;
    unanswered += UICommon::NeedsCover(state) ? 1 : 0;
  }
  emit CoversDownloaded(static_cast<int>(games.size()), found, unanswered);
}

void GameTracker::DownloadCoverInternal(const std::string& path, UICommon::CoverKind kind)
{
  const auto game = m_cache.Find(path);
  const bool found = game && game->DownloadCover(kind);
  UICommon::CoverState state{.game_id = game ? game->GetGameTDBID() : std::string()};
  UICommon::FillCoverCacheState(&state, kind);
  emit CoverDownloaded(path, kind, found, !found && UICommon::NeedsCover(state));
}

void GameTracker::FlushNewGames()
{
  if (m_new_games.empty())
    return;
  std::vector<std::string> games;
  for (std::string& path : m_new_games)
  {
    const auto game = m_cache.Find(path);
    if (game && UICommon::NeedsCover(game->GetCoverState()))
      games.push_back(std::move(path));
  }
  m_new_games.clear();
  if (!games.empty())
    QueueOnObject(this, [this, games = std::move(games)]() mutable { NewGamesFound(std::move(games)); });
}

// A rescan found games the list did not have: the same question, a few seconds after the last find.
void GameTracker::NewGamesFound(std::vector<std::string> games)
{
  if (!UICommon::COVERS_ARE_SHOWN || Config::Get(Config::MAIN_USE_GAME_COVERS) || Settings::Instance().IsBatchModeEnabled())
    return;
  for (std::string& game : games)
  {
    if (std::ranges::find(m_cover_games, game) == m_cover_games.end())
      m_cover_games.push_back(std::move(game));
  }
  m_cover_timer->start();
}
