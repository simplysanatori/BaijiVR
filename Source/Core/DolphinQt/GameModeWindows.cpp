// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/GameModeWindows.h"

#include <algorithm>

#include <QApplication>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFontDialog>
#include <QInputDialog>
#include <QLayout>
#include <QMainWindow>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QProgressDialog>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include "Common/Logging/Log.h"
#include "Common/WindowFit.h"
#include "DolphinQt/Host.h"

namespace GameModeWindows
{
namespace
{
constexpr char SCROLLED[] = "baiji_game_mode_scroll";

Common::WindowFit::Size ToSize(const QSize& s)
{
  return {s.width(), s.height()};
}

QSize MinimumOf(const QWidget* window)
{
  return window->minimumSize().expandedTo(window->minimumSizeHint());
}

bool IsFitted(const QWidget* window, const QWidget* main)
{
  if (window == main || !window->isWindow() || window->testAttribute(Qt::WA_DontShowOnScreen))
    return false;
  switch (window->windowType())
  {
  case Qt::Dialog:
  case Qt::Window:
  case Qt::Tool:
  case Qt::Sheet:
    break;
  default:
    return false;  // popups, menus, tooltips, splash screens
  }
  // The render window is the game's picture, not a dialog.
  return !window->inherits("RenderWidget");
}

class Filter final : public QObject
{
public:
  explicit Filter(QWidget* main) : QObject(main), m_main(main) {}

  bool eventFilter(QObject* object, QEvent* event) override
  {
    if (event->type() != QEvent::Show || !m_main)
      return false;
    auto* window = qobject_cast<QWidget*>(object);
    if (!window || !IsFitted(window, m_main))
      return false;
    Fit(window, m_main, true);
    QPointer<QWidget> pointer{window};
    QTimer::singleShot(0, this, [this, pointer] {
      if (pointer && pointer->isVisible() && m_main)
        Fit(pointer, m_main, false);
    });
    return false;
  }

private:
  QPointer<QWidget> m_main;
};
}  // namespace

void Install(QWidget* main)
{
  if (!main || !Host::IsGameMode())
    return;
  qApp->installEventFilter(new Filter(main));
}

bool AddScrollArea(QWidget* window)
{
  if (!window || window->property(SCROLLED).toBool() || qobject_cast<QMainWindow*>(window))
    return false;
  // Qt's own dialogs lay themselves out again (and a message box fixes its size).
  if (qobject_cast<QMessageBox*>(window) || qobject_cast<QFileDialog*>(window) ||
      qobject_cast<QInputDialog*>(window) || qobject_cast<QProgressDialog*>(window) ||
      qobject_cast<QColorDialog*>(window) || qobject_cast<QFontDialog*>(window) ||
      window->inherits("QWizard"))
  {
    return false;
  }
  QLayout* const top = window->layout();
  if (!top || (top->sizeConstraint() != QLayout::SetDefaultConstraint &&
               top->sizeConstraint() != QLayout::SetMinimumSize))
  {
    return false;
  }
  QDialogButtonBox* buttons = nullptr;
  for (int i = top->count() - 1; i >= 0 && !buttons; --i)
    buttons = qobject_cast<QDialogButtonBox*>(top->itemAt(i)->widget());
  const QMargins margins = top->contentsMargins();
  if (buttons)
    top->removeWidget(buttons);  // (still the window's child, under the scroll area below)
  auto* content = new QWidget;
  content->setObjectName(QStringLiteral("baiji_game_mode_content"));
  content->setLayout(top);  // Qt takes the layout, with its widgets, from the window
  auto* scroll = new QScrollArea(window);
  scroll->setObjectName(QStringLiteral("baiji_game_mode_scroll"));
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setWidget(content);
  window->setMinimumSize(0, 0);  // the old layout's minimum, kept on the content
  auto* outer = new QVBoxLayout(window);
  outer->setContentsMargins(0, 0, 0, buttons ? margins.bottom() : 0);
  outer->setSpacing(0);
  outer->addWidget(scroll, 1);
  if (buttons)
  {
    buttons->setContentsMargins(margins.left(), std::max(0, top->spacing()), margins.right(), 0);
    outer->addWidget(buttons);
  }
  scroll->show();
  outer->activate();
  window->setProperty(SCROLLED, true);
  return true;
}

void Fit(QWidget* window, const QWidget* main, bool first)
{
  if (!window || !main)
    return;
  QSize room = main->size();
  if (const QScreen* screen = main->screen())
    room = room.boundedTo(screen->availableGeometry().size());
  // A window manager's title bar and borders, where there is one (gamescope has none).
  room -= window->frameGeometry().size() - window->geometry().size();
  const QSize asked = window->size();
  const QSize asked_min = MinimumOf(window);
  Common::WindowFit::Fit fit = Common::WindowFit::FitWindow(
      ToSize(room), ToSize(asked), ToSize(asked_min), ToSize(window->maximumSize()));
  bool scrolled = false;
  if (fit.too_small && first)
  {
    scrolled = AddScrollArea(window);
    if (scrolled)
    {
      fit = Common::WindowFit::FitWindow(ToSize(room), ToSize(asked), ToSize(MinimumOf(window)),
                                         ToSize(window->maximumSize()));
    }
  }
  const QSize size(fit.size.width, fit.size.height);
  if (size != window->size())
    window->resize(size);
  QRect target(QPoint(), window->size());
  target.moveCenter(main->geometry().center());
  if (window->geometry().topLeft() != target.topLeft())
    window->move(target.topLeft() + (window->pos() - window->geometry().topLeft()));
  if (first)
  {
    INFO_LOG_FMT(COMMON, "Game Mode window {} {}x{} (minimum {}x{}) shown at {}x{}{}",
                 window->metaObject()->className(), asked.width(), asked.height(),
                 asked_min.width(), asked_min.height(), window->width(), window->height(),
                 scrolled         ? ", content in a scroll area" :
                 fit.too_small    ? ", LARGER than the area" :
                                    "");
  }
}
}  // namespace GameModeWindows
