// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DolphinQt/TheaterDock.h"

#include <utility>

#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QWidget>

#include <fmt/format.h>

#include "Common/Config/Config.h"
#include "Common/Logging/Log.h"
#include "Core/Config/MainSettings.h"

using namespace std::chrono_literals;

namespace
{
// [Interface] TheaterDock
constexpr int THEATER = 1;       // in the theater, and back into the dashboard before a VR game
constexpr int THEATER_STAY = 2;  // in the theater, and left there

constexpr auto VRCMD_LIMIT = 10s;
constexpr auto START_WAIT = 20s;
constexpr auto AFTER_VR_WAIT = 15s;
constexpr auto POLL = 250ms;
// The dashboard makes the frame a moment after the overlay can be shown.
constexpr auto FRAME_WAIT = 750ms;
constexpr auto AGAIN_WAIT = 2s;
constexpr int AGAIN_AFTER_VR = 2;
constexpr auto LEAVE_LIMIT = 5s;

std::string Describe(const std::optional<Common::OverlayState>& state)
{
  if (!state)
    return "missing";
  return fmt::format("{}, {}, {}", state->picture ? "picture" : "no picture",
                     state->in_dashboard ? "in the dashboard" : "not in the dashboard",
                     state->visible ? "visible" : "not visible");
}
}  // namespace

TheaterDock* TheaterDock::Install(QWidget* main)
{
  // (Not Host::IsGameMode(), which can be forced.)
  if (!main || qEnvironmentVariable("XDG_CURRENT_DESKTOP") != QStringLiteral("gamescope"))
    return nullptr;

  const int mode = Config::Get(Config::MAIN_INTERFACE_THEATER_DOCK);
  // For tests.
  QString vrcmd = qEnvironmentVariable("BAIJI_VRCMD");
  if (vrcmd.isEmpty())
    vrcmd = QStringLiteral("/opt/steamvr/bin/linuxarm64/vrcmd");
  std::string key =
      Common::GetGameOverlayKey(qEnvironmentVariable("SteamAppId").trimmed().toStdString());

  const char* off = nullptr;
  if (mode != THEATER && mode != THEATER_STAY)
    off = "TheaterDock is 0";
  else if (key.empty())
    off = "no SteamAppId";
  else if (!QFileInfo(vrcmd).isExecutable())
    off = "no vrcmd";
  if (off)
  {
    NOTICE_LOG_FMT(CORE, "Theater: off ({})", off);
    return nullptr;
  }
  NOTICE_LOG_FMT(CORE, "Theater: {} with {}", key, vrcmd.toStdString());
  return new TheaterDock(main, mode, std::move(vrcmd), std::move(key));
}

TheaterDock::TheaterDock(QWidget* main, int mode, QString vrcmd, std::string key)
    : QObject(main), m_main(main), m_mode(mode), m_vrcmd(std::move(vrcmd)), m_key(std::move(key))
{
}

TheaterDock::~TheaterDock()
{
  Stop();
}

// ---- vrcmd --------------------------------------------------------------------------------------

void TheaterDock::Stop()
{
  ++m_step;
  m_move = Move::None;
  if (!m_process)
    return;
  m_process->disconnect(this);
  // It goes once it has ended: the destructor of one that runs would wait for it.
  m_process->setParent(nullptr);
  connect(m_process, &QProcess::finished, m_process, &QObject::deleteLater);
  m_process->kill();
  m_process = nullptr;
}

void TheaterDock::Run(const QStringList& args, std::function<void(const std::string&)> done)
{
  const int step = ++m_step;
  auto* const process = new QProcess(this);
  m_process = process;

  // Steam's overlay and Baiji's own libraries are not for it.
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.remove(QStringLiteral("LD_PRELOAD"));
  environment.remove(QStringLiteral("LD_LIBRARY_PATH"));
  process->setProcessEnvironment(environment);
  process->setStandardInputFile(QProcess::nullDevice());
  process->setStandardErrorFile(QProcess::nullDevice());

  const auto finish = [this, process, step, done = std::move(done)](const std::string& out) {
    process->deleteLater();
    if (step != m_step)
      return;
    m_process = nullptr;
    done(out);
  };
  connect(process, &QProcess::finished, this, [process, finish](int, QProcess::ExitStatus status) {
    const std::string out = process->readAllStandardOutput().toStdString();
    finish(status == QProcess::NormalExit ? out : std::string());
  });
  connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart)
      finish({});
  });
  QTimer::singleShot(VRCMD_LIMIT, process, [process] {
    WARN_LOG_FMT(CORE, "Theater: vrcmd took too long");
    process->kill();
  });
  process->start(m_vrcmd, args, QIODevice::ReadOnly);
}

void TheaterDock::Dock(const char* where, std::function<void()> done)
{
  Run({QStringLiteral("--dock-overlay"), QString::fromLatin1(where), QString::fromStdString(m_key)},
      [done = std::move(done)](const std::string&) { done(); });
}

void TheaterDock::After(std::chrono::milliseconds time, std::function<void()> then)
{
  const int step = ++m_step;
  QTimer::singleShot(time, this, [this, step, then = std::move(then)] {
    if (step == m_step)
      then();
  });
}

std::optional<Common::OverlayState> TheaterDock::Find(const std::string& listing) const
{
  const auto line = Common::FindOverlayLine(listing, m_key);
  if (!line)
    return std::nullopt;
  return Common::ParseOverlayLine(*line);
}

// ---- To the theater -----------------------------------------------------------------------------

void TheaterDock::Show()
{
  if (m_move == Move::ToDashboard)
  {
    // It ends first: it opened the dashboard, and closes it.
    m_show_next = !IsLeaving();
    return;
  }
  if (m_docked || m_move == Move::ToTheater)
    return;
  Stop();
  m_move = Move::ToTheater;
  m_tries = m_after_vr ? AGAIN_AFTER_VR : 0;
  m_deadline = Clock::now() + (m_after_vr ? AFTER_VR_WAIT : START_WAIT);
  NOTICE_LOG_FMT(CORE, "Theater: waiting for the window{}", m_after_vr ? " after VR" : "");
  Wait();
}

void TheaterDock::Wait()
{
  Run({QStringLiteral("--overlays")}, [this](const std::string& out) {
    const auto state = Find(out);
    if (state && state->IsShowable())
    {
      After(FRAME_WAIT, [this] {
        m_docked = true;  // from now on: a move that is stopped may still happen
        Dock("theater", [this] { Check(false); });
      });
    }
    else if (Clock::now() < m_deadline)
    {
      After(POLL, [this] { Wait(); });
    }
    else if (m_tries > 0)
    {
      --m_tries;
      NOTICE_LOG_FMT(CORE, "Theater: overlay {}, again", Describe(state));
      After(AGAIN_WAIT, [this] {
        m_deadline = Clock::now() + AFTER_VR_WAIT;
        Wait();
      });
    }
    else
    {
      m_move = Move::None;
      NOTICE_LOG_FMT(CORE, "Theater: overlay {}, nothing moved", Describe(state));
    }
  });
}

void TheaterDock::Check(bool again)
{
  Run({QStringLiteral("--overlays")}, [this, again](const std::string& out) {
    const auto state = Find(out);
    NOTICE_LOG_FMT(CORE, "Theater: docked{} (overlay {})", again ? " again" : "", Describe(state));
    if (again || !m_after_vr || !state || state->visible)
    {
      m_move = Move::None;
      return;
    }
    // After VR it may not show until it has been somewhere else.
    Dock("world", [this] { Dock("theater", [this] { Check(true); }); });
  });
}

// ---- Before VR ----------------------------------------------------------------------------------

bool TheaterDock::Leave(std::function<void()> then)
{
  const bool under_way = m_move == Move::ToDashboard;
  m_show_next = false;
  if (!under_way)
    Stop();
  if (!under_way && (!m_docked || m_mode == THEATER_STAY))
    return false;

  NOTICE_LOG_FMT(CORE, "Theater: back into the dashboard before VR");
  m_then = std::move(then);
  m_move = Move::ToDashboard;
  m_label = new QLabel(tr("Starting in VR"), m_main);
  m_label->setAutoFillBackground(true);
  m_label->setFrameShape(QFrame::StyledPanel);
  m_label->setMargin(24);
  m_label->adjustSize();
  m_label->move((m_main->width() - m_label->width()) / 2,
                (m_main->height() - m_label->height()) / 2);
  m_label->show();
  m_label->raise();

  // The game does not wait longer than this, whatever vrcmd does. (The move goes on.)
  const int leave = ++m_leave;
  QTimer::singleShot(LEAVE_LIMIT, this, [this, leave] {
    if (leave != m_leave || !IsLeaving())
      return;
    WARN_LOG_FMT(CORE, "Theater: not out of it in time, VR anyway");
    FinishLeave();
  });
  if (under_way)
    return true;
  Dock("dashboard", [this] {
    m_docked = false;
    // Docking there opened the dashboard.
    Run({QStringLiteral("--hidedashboard")}, [this](const std::string&) {
      m_move = Move::None;
      NOTICE_LOG_FMT(CORE, "Theater: left, dashboard closed");
      FinishLeave();
      if (std::exchange(m_show_next, false))
        Show();
    });
  });
  return true;
}

void TheaterDock::FinishLeave()
{
  const auto then = std::exchange(m_then, nullptr);
  CancelLeave();
  if (then)
    then();
}

void TheaterDock::CancelLeave()
{
  m_then = nullptr;
  if (m_label)
    m_label->deleteLater();
}

void TheaterDock::OnVRGame()
{
  // (A move out of the theater that takes long closes the dashboard yet.)
  if (m_move != Move::ToDashboard)
    Stop();
  m_show_next = false;
  m_docked = false;
  m_after_vr = true;
}
