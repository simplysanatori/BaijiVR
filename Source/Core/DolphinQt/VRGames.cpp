// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/VRGames.h"

#include <algorithm>
#include <ctime>
#include <memory>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <QImage>
#include <QTimer>

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/TimePlayed.h"
#include "DolphinQt/GameList/GameListModel.h"
#include "DolphinQt/Settings.h"
#include "UICommon/GameFile.h"
#include "VideoCommon/OnScreenDisplay.h"

namespace
{
// A scan brings many games one after another.
constexpr int GATHER_MS = 300;
constexpr u32 MESSAGE_MS = 5000;
constexpr qint64 MAX_IMAGE_BYTES = 32 << 20;
}  // namespace

VRGames::VRGames(GameListModel& model, Play play, QObject* parent)
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
       .choose_cover =
           [this](const std::string& path, VR::UI::CoverChoice choice, const std::string& image) {
             ChooseCover(path, choice, image);
           },
       .add_folder = [this](const std::string& path) { AddFolder(path); },
       .download_thumbnails =
           [this] {
             OSD::AddMessage("Downloading Thumbnails", MESSAGE_MS);
             m_model.GetTracker().DownloadCovers();
           },
       .publish = [this] { Publish(); }});
  connect(&m_model.GetTracker(), &GameTracker::CoversDownloaded, this,
          &VRGames::OnCoversDownloaded);
  connect(&m_model.GetTracker(), &GameTracker::CoverDownloaded, this, &VRGames::OnCoverDownloaded);
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

void VRGames::AddFolder(const std::string& path)
{
  const std::vector<std::string> paths = Config::GetIsoPaths();
  if (std::ranges::find(paths, path) != paths.end())
  {
    OSD::AddMessage("Folder Already Added", MESSAGE_MS);
    return;
  }
  // As the Paths page: the window's list, and the question about covers.
  Settings::Instance().AddPath(QString::fromStdString(path));
  Config::Save();
  OSD::AddMessage("Folder Added", MESSAGE_MS);
}

// ---- Covers -------------------------------------------------------------------------------------

void VRGames::SetCover(const std::string& path, UICommon::VRGameStore::Cover cover)
{
  const std::shared_ptr<const UICommon::GameFile> game = m_model.FindGame(path);
  if (!game)
    return;
  m_store.SetCover(UICommon::GetVRGameKey(*game), cover);
  m_list.CoverChanged(path);
  Publish();
}

void VRGames::ChooseCover(const std::string& path, VR::UI::CoverChoice choice,
                          const std::string& image)
{
  using VR::UI::CoverChoice;
  using Cover = UICommon::VRGameStore::Cover;
  const std::shared_ptr<const UICommon::GameFile> game = m_model.FindGame(path);
  if (!game)
    return;
  switch (choice)
  {
  case CoverChoice::Default:
    SetCover(path, Cover::Default);
    File::Delete(UICommon::GetOwnCoverPath(UICommon::GetVRGameKey(*game)),
                 File::IfAbsentBehavior::NoConsoleWarning);
    break;
  case CoverChoice::None:
    SetCover(path, Cover::None);
    break;
  case CoverChoice::Box:
    m_model.GetTracker().DownloadCover(path, UICommon::CoverKind::Box);
    break;
  case CoverChoice::Title:
    m_model.GetTracker().DownloadCover(path, UICommon::CoverKind::Title);
    break;
  case CoverChoice::Snap:
    m_model.GetTracker().DownloadCover(path, UICommon::CoverKind::Snap);
    break;
  case CoverChoice::Own:
  {
    if (static_cast<qint64>(File::GetSize(image)) > MAX_IMAGE_BYTES)
    {
      OSD::AddMessage("Image Too Large", MESSAGE_MS);
      break;
    }
    const QImage picture(QString::fromStdString(image));
    if (picture.isNull())
    {
      OSD::AddMessage("Not a PNG or JPEG Image", MESSAGE_MS);
      break;
    }
    const std::string own = UICommon::GetOwnCoverPath(UICommon::GetVRGameKey(*game));
    constexpr int SIDE = UICommon::COVER_MAX_HEIGHT;
    File::CreateFullPath(own);
    if (!picture.scaled(SIDE, SIDE, Qt::KeepAspectRatio, Qt::SmoothTransformation)
             .convertToFormat(QImage::Format_RGBA8888)
             .save(QString::fromStdString(own), "PNG"))
    {
      OSD::AddMessage("The Image Could Not Be Kept", MESSAGE_MS);
      break;
    }
    SetCover(path, Cover::Own);
    break;
  }
  }
}

void VRGames::OnCoversDownloaded(int asked, int found, int unanswered)
{
  if (asked == 0)
    OSD::AddMessage("No Covers to Download", MESSAGE_MS);
  else if (found == 0)
    OSD::AddMessage(unanswered > 0 ? "Could Not Reach GitHub" : "No Covers Found", MESSAGE_MS);
  else if (found == 1)
    OSD::AddMessage("1 Cover Downloaded", MESSAGE_MS);
  else
    OSD::AddMessage(fmt::format("{} Covers Downloaded", found), MESSAGE_MS);
}

void VRGames::OnCoverDownloaded(const std::string& path, UICommon::CoverKind kind, bool found,
                                bool unanswered)
{
  using Cover = UICommon::VRGameStore::Cover;
  if (!found)
  {
    OSD::AddMessage(unanswered ? "Could Not Reach GitHub" : "No Covers Found", MESSAGE_MS);
    return;
  }
  SetCover(path, kind == UICommon::CoverKind::Title ? Cover::Title :
                 kind == UICommon::CoverKind::Snap  ? Cover::Snap :
                                                      Cover::Box);
}
