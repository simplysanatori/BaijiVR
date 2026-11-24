// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include <QWidget>

#include "Common/CommonTypes.h"
#include "Core/GameMods/GameMods.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;

// Mods and textures for one game, from the GameMods catalogue. Lives in the game's Properties.
class GameModsWidget final : public QWidget
{
public:
  GameModsWidget(const std::string& game_id, u16 revision);

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private:
  void SetList(GameMods::List list);
  void Reload();
  void AddRow(const GameMods::Item& item);
  void OnTimer();
  void ShowError(const QString& text);
  void MarkChanged();

  std::string m_game_id;
  u16 m_revision;
  GameMods::List m_list = GameMods::List::Mods;
  GameMods::Progress::Phase m_last_phase;

  QPushButton* m_mods_button;
  QPushButton* m_textures_button;
  QScrollArea* m_scroll;
  QVBoxLayout* m_rows;
  QWidget* m_progress_box;
  QLabel* m_progress_label;
  QProgressBar* m_progress_bar;
  QLabel* m_error_label;
  QLabel* m_changed_label;
  QTimer* m_timer;
};
