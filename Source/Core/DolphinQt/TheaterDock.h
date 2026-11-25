// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

// Baiji's window in SteamVR's theater, the big screen that stays when the dashboard closes: while the
// dashboard is open Steam Input gives the controllers to Steam's own interface. From Azahar for Frame
// (citra_qt/frame_mode.cpp, core/steam_cef.cpp).
//
// gamescope makes the window the overlay valve.steam.desktopgame.<SteamAppId>, and SteamVR's vrcmd
// moves that: --dock-overlay theater once it can be shown, and back into the dashboard before a VR
// game, where the dashboard that this opens is closed again. These are requests to the running
// SteamVR. No setting or file is written. vrcmd takes about 2 s a call and runs on its own.
//
// Only in Game Mode, started by Steam, with vrcmd there. [Interface] TheaterDock: 0 is off.

#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "Common/TheaterOverlay.h"

class QLabel;
class QProcess;
class QWidget;

class TheaterDock final : public QObject
{
public:
  // nullptr: off.
  static TheaterDock* Install(QWidget* main);
  ~TheaterDock() override;

  // To the theater, unless it is there or on its way.
  void Show();

  // Before a VR game's session. False: it can start now. True: `then` is called once the window is
  // out of the theater, LEAVE_LIMIT later at the latest.
  bool Leave(std::function<void()> then);
  bool IsLeaving() const { return static_cast<bool>(m_then); }
  // `then` is not called.
  void CancelLeave();

  // A VR game starts. SteamVR closes the window's screen with it and does not bring it back.
  void OnVRGame();

private:
  using Clock = std::chrono::steady_clock;

  enum class Move
  {
    None,
    ToTheater,
    ToDashboard,
  };

  TheaterDock(QWidget* main, int mode, QString vrcmd, std::string key);

  // The move under way ends.
  void Stop();
  // vrcmd's output, empty when it failed or took too long.
  void Run(const QStringList& args, std::function<void(const std::string&)> done);
  void Dock(const char* where, std::function<void()> done);
  void After(std::chrono::milliseconds time, std::function<void()> then);

  void Wait();
  void Check(bool again);
  void FinishLeave();

  std::optional<Common::OverlayState> Find(const std::string& listing) const;

  QPointer<QWidget> m_main;
  int m_mode;
  QString m_vrcmd;
  std::string m_key;

  QPointer<QProcess> m_process;
  int m_step = 0;  // counts the steps: what belongs to an earlier one does nothing
  Move m_move = Move::None;
  bool m_docked = false;
  bool m_show_next = false;  // once it is out of the theater
  bool m_after_vr = false;
  int m_tries = 0;
  Clock::time_point m_deadline;
  std::function<void()> m_then;
  int m_leave = 0;  // counts them
  QPointer<QLabel> m_label;
};
