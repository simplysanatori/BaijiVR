// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QAbstractTableModel>
#include <QMap>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "Core/Core.h"
#include "Core/TimePlayed.h"
#include "Core/TitleDatabase.h"

#include "DolphinQt/GameList/GameTracker.h"

namespace UICommon
{
class GameFile;
}

class GameListModel final : public QAbstractTableModel
{
  Q_OBJECT

public:
  explicit GameListModel(QObject* parent = nullptr);

  // Qt's Model/View stuff uses these overrides.
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;
  int rowCount(const QModelIndex& parent) const override;
  int columnCount(const QModelIndex& parent) const override;

  std::shared_ptr<const UICommon::GameFile> GetGameFile(int index) const;
  std::string GetNetPlayName(const UICommon::GameFile& game) const;
  bool ShouldDisplayGameListItem(int index) const;
  void SetSearchTerm(const QString& term);

  // Using a custom sort role as it sometimes differs slightly from the default Qt::DisplayRole.
  static constexpr int SORT_ROLE = Qt::UserRole;

  enum class Column
  {
    Platform = 0,
    Banner,
    Title,
    Description,
    Maker,
    ID,
    Country,
    Size,
    FileName,
    FilePath,
    FileFormat,
    BlockSize,
    Compression,
    TimePlayed,
    Tags,
    Count,
  };

  void AddGame(const std::shared_ptr<const UICommon::GameFile>& game);
  void UpdateGame(const std::shared_ptr<const UICommon::GameFile>& game);
  void RemoveGame(const std::string& path);

  std::shared_ptr<const UICommon::GameFile> FindGame(const std::string& path) const;
  std::shared_ptr<const UICommon::GameFile> FindSecondDisc(const UICommon::GameFile& game) const;

  const Core::TitleDatabase& GetTitleDatabase() const { return m_title_database; }

  // Its cover, as in the VR menu, and the downloaded banner; null when it has none. SetPictures has
  // every game's files by path, each game's best first.
  using PictureFiles = std::map<std::string, std::vector<std::string>>;
  QPixmap GetCover(const std::string& path) const { return GetPicture(&m_covers, path); }
  QPixmap GetBanner(const std::string& path) const { return GetPicture(&m_banners, path); }
  bool HasCovers() const { return m_has_covers; }
  void SetPictures(const PictureFiles& covers, const PictureFiles& banners);

  void SetScale(float scale);
  float GetScale() const;

  const QStringList& GetAllTags() const;
  const QStringList GetGameTags(const std::string& path) const;

  void AddGameTag(const std::string& path, const QString& name);
  void RemoveGameTag(const std::string& path, const QString& name);

  void NewTag(const QString& name);
  void DeleteTag(const QString& name);

  void PurgeCache();

  // Where the covers come from.
  GameTracker& GetTracker() { return m_tracker; }

private:
  // Index in m_games, or -1 if it isn't found
  int FindGameIndex(const std::string& path) const;

  void OnEmulationStateChanged(Core::State state);

  QStringList m_tag_list;
  QMap<QString, QVariant> m_game_tags;

  GameTracker m_tracker;
  QList<std::shared_ptr<const UICommon::GameFile>> m_games;
  Core::TitleDatabase m_title_database;
  TimePlayed m_timer;
  struct Picture
  {
    // Those that are there, and when they were written.
    std::vector<std::pair<QString, qint64>> files;
    // Read when it is first shown.
    bool read = false;
    QPixmap picture;
  };

  using Pictures = std::map<std::string, Picture>;

  static QPixmap GetPicture(Pictures* pictures, const std::string& path);
  static std::vector<std::string> UpdatePictures(Pictures* pictures, const PictureFiles& files);

  mutable Pictures m_covers;
  mutable Pictures m_banners;
  bool m_has_covers = false;
  QString m_term;
  float m_scale = 1.0;
};
