// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/GameModsWidget.h"

#include <algorithm>

#include <QCheckBox>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QPushButton>
#include <QRectF>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include "DolphinQt/Host.h"
#include "DolphinQt/QtUtils/ClearLayoutRecursively.h"

namespace
{
// Filled seal with a check mark, in the palette's highlight colour.
class RecommendedBadge final : public QWidget
{
public:
  RecommendedBadge() { setFixedSize(16, 16); }

protected:
  void paintEvent(QPaintEvent*) override
  {
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().highlight());
    painter.drawEllipse(QRectF{0.5, 0.5, width() - 1.0, height() - 1.0});

    const QPen pen{palette().highlightedText().color(), 2.0, Qt::SolidLine, Qt::RoundCap,
                   Qt::RoundJoin};
    painter.setPen(pen);
    const qreal w = width();
    const qreal h = height();
    painter.drawPolyline(QPolygonF{{QPointF{w * 0.28, h * 0.52}, QPointF{w * 0.44, h * 0.68},
                                    QPointF{w * 0.72, h * 0.36}}});
  }
};

QString MegabytesText(u64 bytes)
{
  const u64 megabytes = std::max<u64>(1, (bytes + 500000) / 1000000);
  return QString::number(static_cast<qulonglong>(megabytes));
}
}  // namespace

GameModsWidget::GameModsWidget(const std::string& game_id, u16 revision)
    : m_game_id(game_id), m_revision(revision),
      m_last_phase(GameMods::GetProgress().phase)
{
  m_mods_button = new QPushButton(tr("Mods and Cheats"));
  m_mods_button->setCheckable(true);
  m_textures_button = new QPushButton(tr("Textures"));
  m_textures_button->setCheckable(true);
  auto* const refresh = new QPushButton(tr("Refresh"));

  auto* const top = new QHBoxLayout;
  top->addWidget(m_mods_button);
  top->addWidget(m_textures_button);
  top->addStretch(1);
  top->addWidget(refresh);

  auto* const container = new QWidget;
  m_rows = new QVBoxLayout{container};
  m_scroll = new QScrollArea;
  m_scroll->setWidget(container);
  m_scroll->setWidgetResizable(true);
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_scroll->setFrameShape(QFrame::NoFrame);

  m_progress_label = new QLabel;
  m_progress_bar = new QProgressBar;
  m_progress_bar->setTextVisible(false);
  auto* const cancel = new QPushButton(tr("Cancel"));
  auto* const progress_row = new QHBoxLayout;
  progress_row->addWidget(m_progress_bar, 1);
  progress_row->addWidget(cancel);
  auto* const progress_layout = new QVBoxLayout;
  progress_layout->setContentsMargins(0, 0, 0, 0);
  progress_layout->addWidget(m_progress_label);
  progress_layout->addLayout(progress_row);
  m_progress_box = new QWidget;
  m_progress_box->setLayout(progress_layout);
  m_progress_box->hide();

  m_error_label = new QLabel;
  m_error_label->setWordWrap(true);
  m_error_label->hide();
  m_changed_label = new QLabel(tr("Changes are for the game's next start."));
  m_changed_label->setWordWrap(true);
  m_changed_label->hide();

  auto* const layout = new QVBoxLayout{this};
  layout->addLayout(top);
  layout->addWidget(m_scroll, 1);
  layout->addWidget(m_progress_box);
  layout->addWidget(m_error_label);
  layout->addWidget(m_changed_label);

  m_timer = new QTimer{this};
  m_timer->setInterval(250);
  connect(m_timer, &QTimer::timeout, this, [this] { OnTimer(); });

  connect(m_mods_button, &QPushButton::clicked, this,
          [this] { SetList(GameMods::List::Mods); });
  connect(m_textures_button, &QPushButton::clicked, this,
          [this] { SetList(GameMods::List::Textures); });
  connect(refresh, &QPushButton::clicked, this, [this] {
    GameMods::RefreshCatalogAsync();
    QTimer::singleShot(3000, this, [this] { Reload(); });
  });
  connect(cancel, &QPushButton::clicked, this, [] { GameMods::CancelInstall(); });

  SetList(GameMods::List::Mods);
}

void GameModsWidget::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  m_timer->start();
  OnTimer();
}

void GameModsWidget::hideEvent(QHideEvent* event)
{
  m_timer->stop();
  QWidget::hideEvent(event);
}

void GameModsWidget::SetList(GameMods::List list)
{
  m_list = list;
  m_mods_button->setChecked(list == GameMods::List::Mods);
  m_textures_button->setChecked(list == GameMods::List::Textures);
  Reload();
}

void GameModsWidget::Reload()
{
  ClearLayoutRecursively(m_rows);
  const auto items = GameMods::GetItems(m_list, m_game_id, m_revision);
  for (size_t i = 0; i < items.size(); i++)
  {
    if (i != 0)
    {
      auto* const line = new QFrame;
      line->setFrameShape(QFrame::HLine);
      m_rows->addWidget(line);
    }
    AddRow(items[i]);
  }
  if (items.empty())
  {
    m_rows->addWidget(new QLabel(m_list == GameMods::List::Mods ?
                                     tr("No mods or cheats for this game.") :
                                     tr("No textures for this game.")));
  }
  m_rows->addStretch(1);
}

void GameModsWidget::AddRow(const GameMods::Item& item)
{
  auto* const row = new QWidget;
  auto* const row_layout = new QHBoxLayout{row};
  row_layout->setContentsMargins(0, 6, 0, 6);

  auto* const text = new QVBoxLayout;
  auto* const title = new QHBoxLayout;
  auto* const name = new QLabel(QString::fromStdString(item.name));
  QFont bold = name->font();
  bold.setBold(true);
  name->setFont(bold);
  name->setWordWrap(true);
  title->addWidget(name, 0, Qt::AlignVCenter);
  if (item.recommended)
  {
    auto* const badge = new RecommendedBadge;
    badge->setToolTip(tr("Recommended"));
    title->addWidget(badge, 0, Qt::AlignVCenter);
  }
  else if (item.verified)
  {
    auto* const chip = new QLabel(tr("Verified"));
    chip->setStyleSheet(
        QStringLiteral("QLabel { border: 1px solid palette(mid); border-radius: 3px; "
                       "padding: 0 4px; }"));
    title->addWidget(chip, 0, Qt::AlignVCenter);
  }
  title->addStretch(1);
  text->addLayout(title);

  if (!item.author.empty())
  {
    auto* const author = new QLabel(tr("by %1").arg(QString::fromStdString(item.author)));
    author->setWordWrap(true);
    text->addWidget(author);
  }
  if (!item.notes.empty())
  {
    auto* const notes = new QLabel(QString::fromStdString(item.notes));
    notes->setWordWrap(true);
    text->addWidget(notes);
  }
  row_layout->addLayout(text, 1);

  auto* const controls = new QVBoxLayout;
  const std::string id = item.id;
  if (item.state == GameMods::State::LinkOnly)
  {
    auto* const open = new QPushButton(tr("Open Page"));
    const QString homepage = QString::fromStdString(item.homepage);
    connect(open, &QPushButton::clicked, row, [homepage] { Host::OpenPage(homepage); });
    controls->addWidget(open);
  }
  else if (item.state == GameMods::State::Available && item.downloads)
  {
    const QString text_label =
        item.size != 0 ? tr("Download (%1 MB)").arg(MegabytesText(item.size)) : tr("Download");
    auto* const download = new QPushButton(text_label);
    connect(download, &QPushButton::clicked, this, [this, id] {
      if (!GameMods::StartInstall(m_list, m_game_id, id))
        ShowError(tr("Another download is already running."));
      else
        ShowError({});
      OnTimer();
    });
    controls->addWidget(download);
  }
  else
  {
    auto* const on = new QCheckBox(tr("On"));
    on->setChecked(item.state == GameMods::State::On);
    connect(on, &QCheckBox::toggled, this, [this, id](bool enabled) {
      const auto result = GameMods::SetEnabled(m_list, m_game_id, id, enabled);
      if (result.ok)
      {
        ShowError({});
        MarkChanged();
      }
      else
      {
        ShowError(QString::fromStdString(result.error));
      }
      Reload();
    });
    controls->addWidget(on);

    if (item.downloads)
    {
      auto* const remove = new QPushButton(tr("Remove"));
      connect(remove, &QPushButton::clicked, this, [this, id] {
        const auto result = GameMods::Remove(m_list, m_game_id, id);
        if (result.ok)
        {
          ShowError({});
          MarkChanged();
        }
        else
        {
          ShowError(QString::fromStdString(result.error));
        }
        Reload();
      });
      controls->addWidget(remove);
    }
  }
  controls->addStretch(1);
  row_layout->addLayout(controls);

  m_rows->addWidget(row);
}

void GameModsWidget::OnTimer()
{
  const GameMods::Progress progress = GameMods::GetProgress();
  const bool active = progress.phase == GameMods::Progress::Phase::Downloading ||
                      progress.phase == GameMods::Progress::Phase::Unpacking;
  if (active)
  {
    const QString name = QString::fromStdString(progress.name);
    const bool unpacking = progress.phase == GameMods::Progress::Phase::Unpacking;
    if (progress.bytes_total != 0)
    {
      const u64 percent = std::min<u64>(100, progress.bytes_done * 100 / progress.bytes_total);
      m_progress_bar->setRange(0, 100);
      m_progress_bar->setValue(static_cast<int>(percent));
      const QString percent_text = QString::number(static_cast<qulonglong>(percent));
      m_progress_label->setText(unpacking ? tr("Unpacking %1: %2%").arg(name, percent_text) :
                                            tr("Downloading %1: %2%").arg(name, percent_text));
    }
    else
    {
      m_progress_bar->setRange(0, 0);
      m_progress_label->setText(unpacking ? tr("Unpacking %1").arg(name) :
                                            tr("Downloading %1").arg(name));
    }
  }
  m_progress_box->setVisible(active);

  if (progress.phase == m_last_phase)
    return;
  m_last_phase = progress.phase;

  switch (progress.phase)
  {
  case GameMods::Progress::Phase::Done:
    MarkChanged();
    Reload();
    break;
  case GameMods::Progress::Phase::Failed:
    ShowError(QString::fromStdString(progress.error));
    Reload();
    break;
  case GameMods::Progress::Phase::Cancelled:
    Reload();
    break;
  default:
    break;
  }
}

void GameModsWidget::ShowError(const QString& text)
{
  m_error_label->setText(text);
  m_error_label->setVisible(!text.isEmpty());
}

void GameModsWidget::MarkChanged()
{
  m_changed_label->show();
}
