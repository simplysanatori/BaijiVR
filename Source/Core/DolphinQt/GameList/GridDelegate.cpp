// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/GameList/GridDelegate.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QStyle>
#include <QTextLayout>
#include <QTextOption>

#include "DolphinQt/GameList/GameListModel.h"
#include "DolphinQt/GameList/GridProxyModel.h"

namespace
{
constexpr int MARGIN = 14;       // around the tile
constexpr int HIGHLIGHT_PAD = 8;  // the selection, outside the banner
constexpr int TITLE_GAP = 10;    // from the banner to the name
constexpr int TITLE_LINES = 2;
}  // namespace

GridDelegate::GridDelegate(const GameListModel& model, QObject* parent)
    : QStyledItemDelegate(parent), m_model(model)
{
}

QSize GridDelegate::BannerSize() const
{
  return GRID_BANNER_SIZE * m_model.GetScale();
}

QSize GridDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
  const QSize banner = BannerSize();
  int width = banner.width() + 2 * MARGIN;
  if (const auto* view = qobject_cast<const QAbstractItemView*>(option.widget))
  {
    // Qt keeps room for the scroll bar, also while there is none, and wraps a row that fills the rest to
    // the last pixel.
    const int scroll_bar = view->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr,
                                                      view->verticalScrollBar());
    const int room = view->maximumViewportSize().width() - scroll_bar - 2;
    width = std::max(width, room / std::max(1, room / width));
  }
  const int height = banner.height() + TITLE_GAP +
                     QFontMetrics(option.font).lineSpacing() * TITLE_LINES + 2 * MARGIN;
  return QSize(width, height);
}

void GridDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const
{
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  painter->setRenderHint(QPainter::SmoothPixmapTransform);

  const QSize banner = BannerSize();
  const QRect tile((option.rect.center().x() - banner.width() / 2), option.rect.top() + MARGIN,
                   banner.width(), option.rect.height() - 2 * MARGIN);

  const bool selected = option.state & QStyle::State_Selected;
  if (selected || option.state & QStyle::State_MouseOver)
  {
    QColor fill = option.palette.color(QPalette::Highlight);
    fill.setAlpha(selected ? 90 : 35);
    painter->setPen(selected ? QPen(option.palette.color(QPalette::Highlight), 2) : Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(
        tile.adjusted(-HIGHLIGHT_PAD, -HIGHLIGHT_PAD, HIGHLIGHT_PAD, HIGHLIGHT_PAD), 8, 8);
  }

  const QPixmap pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
  if (!pixmap.isNull())
  {
    QRect target(QPoint(), pixmap.size().scaled(banner, Qt::KeepAspectRatio));
    target.moveCenter(QRect(tile.topLeft(), banner).center());
    painter->drawPixmap(target, pixmap);
  }

  const QFontMetrics metrics(option.font);
  const QString title = index.data(Qt::DisplayRole).toString();
  QTextLayout layout(title, option.font);
  QTextOption text_option(Qt::AlignHCenter);
  text_option.setWrapMode(QTextOption::WordWrap);
  layout.setTextOption(text_option);
  painter->setPen(option.palette.color(QPalette::Text));
  int y = tile.top() + banner.height() + TITLE_GAP;
  layout.beginLayout();
  for (int line_number = 0; line_number < TITLE_LINES; ++line_number)
  {
    QTextLine line = layout.createLine();
    if (!line.isValid())
      break;
    line.setLineWidth(tile.width());
    QString text = title.mid(line.textStart(), line.textLength()).trimmed();
    if (line_number == TITLE_LINES - 1)
      text = title.mid(line.textStart());
    text = metrics.elidedText(text, Qt::ElideRight, tile.width());
    painter->drawText(QRect(tile.left(), y, tile.width(), metrics.lineSpacing()),
                      Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine, text);
    y += metrics.lineSpacing();
  }
  layout.endLayout();
  painter->restore();
}
