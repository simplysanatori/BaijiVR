// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QSize>
#include <QSortFilterProxyModel>

inline constexpr QSize GRID_COVER_SIZE(160, 224);
inline constexpr QSize GRID_BANNER_SIZE(144, 48);

// This subclass of QSortFilterProxyModel transforms the raw data into a
// single-column large icon + name to be displayed in a QListView.
class GridProxyModel final : public QSortFilterProxyModel
{
  Q_OBJECT

public:
  explicit GridProxyModel(QObject* parent = nullptr);
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

protected:
  bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
  bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
};
