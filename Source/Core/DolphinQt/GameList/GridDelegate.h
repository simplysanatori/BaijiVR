// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QStyledItemDelegate>

class GameListModel;

// A tile of the Grid View: the cover or the banner, then the game's name below it with room around both.
// Tiles share the width of the view evenly.
class GridDelegate final : public QStyledItemDelegate
{
  Q_OBJECT

public:
  GridDelegate(const GameListModel& model, QObject* parent);

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;

private:
  QSize PictureSize() const;

  const GameListModel& m_model;
};
