// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/GameList/GridProxyModel.h"

#include <QGuiApplication>
#include <QPixmap>
#include <QSize>

#include "DolphinQt/GameList/GameListModel.h"
#include "DolphinQt/QtUtils/ImageConverter.h"
#include "DolphinQt/Resources.h"

#include "UICommon/GameFile.h"

GridProxyModel::GridProxyModel(QObject* parent) : QSortFilterProxyModel(parent)
{
  setDynamicSortFilter(true);
}

QVariant GridProxyModel::data(const QModelIndex& i, int role) const
{
  QModelIndex source_index = mapToSource(i);
  if (role == Qt::DisplayRole)
  {
    return sourceModel()->data(
        sourceModel()->index(source_index.row(), static_cast<int>(GameListModel::Column::Title)),
        Qt::DisplayRole);
  }
  else if (role == Qt::DecorationRole)
  {
    auto* model = static_cast<GameListModel*>(sourceModel());

    // In the screen's own pixels: GridDelegate draws it at the size of the tile. (Not the list's, which
    // is scaled down to a row.)
    const QSize size =
        (QSizeF(GRID_BANNER_SIZE) * model->GetScale() * qGuiApp->devicePixelRatio()).toSize();
    QPixmap banner = ToQPixmap(model->GetGameFile(source_index.row())->GetBannerImage());
    if (banner.isNull())
      banner = Resources::GetMisc(Resources::MiscID::BannerMissing).pixmap(size, 1.0);

    return banner.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  return QVariant();
}

bool GridProxyModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
  GameListModel* glm = qobject_cast<GameListModel*>(sourceModel());
  return glm->ShouldDisplayGameListItem(source_row);
}

bool GridProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
  if (left.data(GameListModel::SORT_ROLE) != right.data(GameListModel::SORT_ROLE))
    return QSortFilterProxyModel::lessThan(left, right);

  // If two items are otherwise equal, compare them by their title
  const auto right_title = sourceModel()
                               ->index(right.row(), static_cast<int>(GameListModel::Column::Title))
                               .data()
                               .toString();
  const auto left_title = sourceModel()
                              ->index(left.row(), static_cast<int>(GameListModel::Column::Title))
                              .data()
                              .toString();

  if (sortOrder() == Qt::AscendingOrder)
    return left_title < right_title;

  return right_title < left_title;
}
