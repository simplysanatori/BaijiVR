// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DolphinQt/GamepadNavigation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMessageBox>
#include <QScrollBar>
#include <QTimer>

#include "Common/Logging/Log.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "DolphinQt/GameList/GameList.h"
#include "DolphinQt/Host.h"
#include "DolphinQt/Settings.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace
{
constexpr int TICK_MS = 16;
// A click or key from elsewhere keeps the steps off for this long: the layout may send both.
constexpr s64 OTHER_INPUT_NS = 150'000'000;
constexpr s64 REFOCUS_NS = 200'000'000;
constexpr float SCROLL_DEADZONE = 0.2f;
constexpr float SCROLL_PAGES_PER_SECOND = 1.5f;

s64 Now()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

float State(const ciface::Core::Device& device, std::string_view name)
{
  const auto* const input = device.FindInput(name);
  return input ? static_cast<float>(input->GetState()) : 0.0f;
}

// An axis from its two halves (each may also go below 0, towards the other).
float Axis(const ciface::Core::Device& device, std::string_view positive, std::string_view negative)
{
  return std::clamp(std::max(0.0f, State(device, positive)) -
                        std::max(0.0f, State(device, negative)),
                    -1.0f, 1.0f);
}

// A dialog or another window of Baiji's, as opposed to the main window, a menu or a tooltip.
bool IsDialogLike(const QWidget* window, const QWidget* main)
{
  if (window == main || !window->isVisible() || qobject_cast<const QDockWidget*>(window) ||
      window->inherits("RenderWidget"))
  {
    return false;
  }
  switch (window->windowType())
  {
  case Qt::Dialog:
  case Qt::Window:
  case Qt::Tool:
  case Qt::Sheet:
    return true;
  default:
    return false;
  }
}

// Where a button of the controller is being mapped, it is not a key as well.
bool IsMapping(const QWidget* window)
{
  return window->inherits("MappingWindow") || window->inherits("IOWindow");
}
}  // namespace

void GamepadNavigation::Install(QWidget* main, GameList* list)
{
  if (main && list && Host::IsGameMode())
    new GamepadNavigation(main, list);
}

GamepadNavigation::GamepadNavigation(QWidget* main, GameList* list)
    : QObject(main), m_main(main), m_list(list), m_timer(new QTimer(this)),
      m_other_input_ns(Now() - OTHER_INPUT_NS)
{
  m_timer->setInterval(TICK_MS);
  connect(m_timer, &QTimer::timeout, this, &GamepadNavigation::Tick);
  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this,
          [this](Core::State state) { SetRunning(state == Core::State::Uninitialized); });
  SetRunning(Core::IsUninitialized(Core::System::GetInstance()));
  qApp->installEventFilter(this);
}

void GamepadNavigation::SetRunning(bool running)
{
  if (running == m_timer->isActive())
    return;
  // What is held when a game starts or ends is no press in the window.
  m_target = Target::None;
  m_last_tick_ns = 0;
  if (running)
    m_timer->start();
  else
    m_timer->stop();
}

bool GamepadNavigation::eventFilter(QObject* object, QEvent* event)
{
  // A Return that the widget has no use for goes on to the dialog, whose default button may be Close:
  // only a dialog that asks one thing gets it.
  if (event == m_return && qobject_cast<QDialog*>(object) && !qobject_cast<QMessageBox*>(object) &&
      !qobject_cast<QInputDialog*>(object) && !qobject_cast<QFileDialog*>(object))
  {
    return true;
  }
  switch (event->type())
  {
  case QEvent::MouseButtonPress:
  case QEvent::MouseButtonRelease:
  case QEvent::MouseButtonDblClick:
  case QEvent::Wheel:
  case QEvent::KeyPress:
  case QEvent::KeyRelease:
    if (event->spontaneous())
      m_other_input_ns = Now();
    break;
  default:
    break;
  }
  return false;
}

bool GamepadNavigation::Pad::Moved(const Pad& from) const
{
  const auto differ = [](float x, float y) { return std::abs(x - y) > 0.1f; };
  return a != from.a || b != from.b || dpad_up != from.dpad_up || dpad_down != from.dpad_down ||
         dpad_left != from.dpad_left || dpad_right != from.dpad_right ||
         left_bumper != from.left_bumper || right_bumper != from.right_bumper ||
         differ(left_x, from.left_x) || differ(left_y, from.left_y) ||
         differ(right_y, from.right_y);
}

GamepadNavigation::Pad GamepadNavigation::ReadDevice(const ciface::Core::Device& device)
{
  Pad pad;
  pad.a = State(device, "Button S") > 0.5f;
  pad.b = State(device, "Button E") > 0.5f;
  pad.dpad_up = State(device, "Pad N") > 0.5f;
  pad.dpad_down = State(device, "Pad S") > 0.5f;
  pad.dpad_left = State(device, "Pad W") > 0.5f;
  pad.dpad_right = State(device, "Pad E") > 0.5f;
  pad.left_bumper = State(device, "Shoulder L") > 0.5f;
  pad.right_bumper = State(device, "Shoulder R") > 0.5f;
  pad.left_x = Axis(device, "Left X+", "Left X-");
  pad.left_y = Axis(device, "Left Y+", "Left Y-");
  pad.right_y = Axis(device, "Right Y+", "Right Y-");
  return pad;
}

bool GamepadNavigation::ReadPad(Pad* pad)
{
  if (!g_controller_interface.IsInit())
    return false;

  // (The HotkeyScheduler updates the inputs. The devices are let go of under the lock.)
  std::unique_lock lock(g_controller_interface.GetDevicesMutex(), std::try_to_lock);
  if (!lock.owns_lock())
    return true;
  std::map<std::string, Pad> pads;
  std::string use;
  for (const auto& device : g_controller_interface.GetAllDevices())
  {
    if (device->GetSource() != "SDL" || !device->FindInput("Pad N") ||
        !device->FindInput("Button S"))
    {
      continue;
    }
    const std::string name = device->GetQualifiedName();
    if (use.empty() || name == m_device)
      use = name;
    pads.emplace(name, ReadDevice(*device));
  }

  std::erase_if(m_seen, [&pads](const auto& seen) { return !pads.contains(seen.first); });
  for (const auto& [name, now] : pads)
  {
    const auto [seen, added] = m_seen.try_emplace(name, now);
    if (!added && now.Moved(seen->second))
    {
      seen->second = now;
      use = name;
    }
  }
  if (use != m_device)
  {
    m_device = use;
    INFO_LOG_FMT(CONTROLLERINTERFACE, "Window navigation: gamepad {}", use.empty() ? "none" : use);
  }
  if (use.empty())
    return false;
  *pad = pads[use];
  return true;
}

QWidget* GamepadNavigation::GetWindow() const
{
  if (QWidget* const popup = QApplication::activePopupWidget())
    return popup;
  if (QWidget* const modal = QApplication::activeModalWidget())
    return modal;
  QWidget* const active = QApplication::activeWindow();
  if (active && IsDialogLike(active, m_main))
    return active;
  for (QWidget* const window : QApplication::topLevelWidgets())
  {
    if (IsDialogLike(window, m_main))
      return window;
  }
  return nullptr;
}

GamepadNavigation::Target GamepadNavigation::GetTarget() const
{
  // Only while Baiji has the keys: the controller reaches SDL in the background, too.
  const QWidget* const active = QApplication::activeWindow();
  if (!m_main || !m_list || !active)
    return Target::None;

  Common::ListNavState state;
  state.game_running = !Core::IsUninitialized(Core::System::GetInstance());
  if (state.game_running)
    return Target::None;
  for (const QWidget* const window : QApplication::topLevelWidgets())
  {
    if (window->isVisible() && IsMapping(window))
      return Target::None;
    state.dialog_open |= IsDialogLike(window, m_main);
  }
  state.popup_open = QApplication::activePopupWidget() != nullptr;
  state.dialog_open |= QApplication::activeModalWidget() != nullptr;
  if (state.popup_open || state.dialog_open)
    return Target::Window;
  const auto* const view = qobject_cast<QAbstractItemView*>(m_list->currentWidget());
  state.list_visible = view && view->isVisible();
  state.main_active = active == m_main;
  return Common::ListNavAllowed(state) ? Target::List : Target::None;
}

void GamepadNavigation::Tick()
{
  const s64 now = Now();
  const float seconds =
      m_last_tick_ns == 0 ? 0.0f : std::min((now - m_last_tick_ns) / 1e9f, 0.1f);
  m_last_tick_ns = now;

  Pad pad = m_pad;
  if (QApplication::activeWindow() == nullptr || !ReadPad(&pad))
    pad = {};
  m_pad = pad;

  // A dialog that closes may leave no window with the keys.
  if (QApplication::activeWindow())
  {
    m_refocus_ns = 0;
  }
  else if (m_target == Target::Window && (!m_window || !m_window->isVisible()))
  {
    m_refocus_ns = now;
  }
  else if (m_refocus_ns != 0 && now - m_refocus_ns >= REFOCUS_NS)
  {
    m_refocus_ns = 0;
    if (QWidget* const next = GetWindow() ? GetWindow() : m_main.data())
      next->activateWindow();
  }

  // The first tick on something else: what is held was meant for what was there before.
  const Target target = GetTarget();
  QWidget* const window = target == Target::Window ? GetWindow() : nullptr;
  const bool same = target == m_target && window == m_window;
  if (target != m_target)
  {
    INFO_LOG_FMT(CONTROLLERINTERFACE, "Window navigation: drives {}",
                 target == Target::List   ? "the game list" :
                 target == Target::Window ? "a window" :
                                            "nothing");
  }
  m_target = target;
  m_window = window;

  const bool list = same && target == Target::List;
  const bool in_window = same && target == Target::Window;
  const bool other = now - m_other_input_ns < OTHER_INPUT_NS;

  const bool steps = (list || in_window) && !other;
  const Common::ListNav::Result result =
      m_vertical.Update(steps, pad.dpad_up, pad.dpad_down,
                        Common::MainAxis(pad.left_y, pad.left_x), pad.a, now);
  const int vertical = result.step;
  const int horizontal = m_horizontal
                             .Update(steps, pad.dpad_left, pad.dpad_right,
                                     -Common::MainAxis(pad.left_x, pad.left_y), false, now)
                             .step;
  const bool a = m_a.Update(in_window, pad.a, m_other_input_ns, now);
  const bool b =
      m_b.Update(same && target != Target::None, pad.b, m_other_input_ns, now);
  const bool left_bumper =
      m_left_bumper.Update(in_window, pad.left_bumper, m_other_input_ns, now);
  const bool right_bumper =
      m_right_bumper.Update(in_window, pad.right_bumper, m_other_input_ns, now);

  if (list)
  {
    if (vertical != 0)
      StepList(vertical < 0 ? Qt::Key_Up : Qt::Key_Down);
    else if (horizontal != 0)
      StepList(horizontal < 0 ? Qt::Key_Left : Qt::Key_Right);
    else if (result.activate)
      StartGame();
    else if (result.tap)
      SelectFirst();
  }
  else if (in_window)
  {
    if (vertical != 0)
      SendToWindow(vertical < 0 ? Qt::Key_Up : Qt::Key_Down);
    else if (horizontal != 0)
      SendToWindow(horizontal < 0 ? Qt::Key_Left : Qt::Key_Right);
    else if (left_bumper || right_bumper)
      SendToWindow(left_bumper ? Qt::Key_Backtab : Qt::Key_Tab);
    else if (a)
      PressInWindow();
  }
  if (b)
    Escape();
  if (same && target != Target::None)
    Scroll(pad.right_y, seconds);
}

void GamepadNavigation::StepList(int key)
{
  auto* const view = qobject_cast<QAbstractItemView*>(m_list->currentWidget());
  if (!view || !view->model() || view->model()->rowCount() == 0)
    return;
  // The table has no left and right: its columns are one game.
  if ((key == Qt::Key_Left || key == Qt::Key_Right) && view->inherits("QTableView"))
    return;
  if (!view->hasFocus())
    view->setFocus(Qt::OtherFocusReason);
  if (!SelectFirst())
    SendKey(view, key);
}

bool GamepadNavigation::SelectFirst()
{
  auto* const view = qobject_cast<QAbstractItemView*>(m_list->currentWidget());
  if (!view || !view->model() || view->model()->rowCount() == 0 ||
      view->selectionModel()->hasSelection())
  {
    return false;
  }
  const QModelIndex first = view->model()->index(0, 0);
  view->setCurrentIndex(first);
  view->selectionModel()->select(first, QItemSelectionModel::ClearAndSelect |
                                            QItemSelectionModel::Rows);
  view->scrollTo(first);
  return true;
}

void GamepadNavigation::StartGame()
{
  auto* const view = qobject_cast<QAbstractItemView*>(m_list->currentWidget());
  if (!view || !view->selectionModel()->hasSelection())
    return;
  SendKey(view, Qt::Key_Return, QStringLiteral("\r"));
}

void GamepadNavigation::Escape()
{
  QWidget* const window = m_target == Target::Window ? GetWindow() : m_main.data();
  if (!window)
    return;
  QWidget* const focus = window->focusWidget();
  SendKey(focus ? focus : window, Qt::Key_Escape, QString(QChar(0x1b)));
  // Escape closes menus and dialogs. Game Mode gives the other windows no close button.
  if (window != m_main && window->windowType() != Qt::Popup && !qobject_cast<QDialog*>(window))
    QMetaObject::invokeMethod(window, &QWidget::close, Qt::QueuedConnection);
}

void GamepadNavigation::SendToWindow(int key)
{
  QWidget* const window = GetWindow();
  if (!window)
    return;
  QWidget* const focus = window->focusWidget();
  // Nothing has the focus yet: the first widget gets it.
  if (!focus && window->windowType() != Qt::Popup)
    key = key == Qt::Key_Up || key == Qt::Key_Backtab ? Qt::Key_Backtab : Qt::Key_Tab;
  SendKey(focus ? focus : window, key);
}

void GamepadNavigation::PressInWindow()
{
  QWidget* const window = GetWindow();
  if (!window)
    return;
  QWidget* const focus = window->focusWidget();
  const auto* const combo = qobject_cast<QComboBox*>(focus);
  const auto* const group = qobject_cast<QGroupBox*>(focus);
  if (qobject_cast<QAbstractButton*>(focus) || (combo && !combo->isEditable()) ||
      (group && group->isCheckable()))
  {
    SendKey(focus, Qt::Key_Space, QStringLiteral(" "));
  }
  else if (focus || window->windowType() == Qt::Popup)
  {
    SendKey(focus ? focus : window, Qt::Key_Return, QStringLiteral("\r"));
  }
  else
  {
    SendToWindow(Qt::Key_Tab);
  }
}

QAbstractScrollArea* GamepadNavigation::GetScrollArea(Target target) const
{
  if (target == Target::List)
    return qobject_cast<QAbstractScrollArea*>(m_list->currentWidget());

  const auto scrolls = [](const QAbstractScrollArea* area) {
    return area && area->isVisible() &&
           area->verticalScrollBar()->maximum() > area->verticalScrollBar()->minimum();
  };
  QWidget* const window = GetWindow();
  if (!window)
    return nullptr;
  // Around what has the focus, or else the largest one.
  for (QWidget* widget = window->focusWidget(); widget; widget = widget->parentWidget())
  {
    auto* const area = qobject_cast<QAbstractScrollArea*>(widget);
    if (scrolls(area))
      return area;
    if (widget == window)
      break;
  }
  QAbstractScrollArea* best = nullptr;
  for (auto* const area : window->findChildren<QAbstractScrollArea*>())
  {
    if (scrolls(area) && (!best || area->width() * area->height() > best->width() * best->height()))
      best = area;
  }
  return best;
}

void GamepadNavigation::Scroll(float stick, float seconds)
{
  if (std::abs(stick) < SCROLL_DEADZONE)
  {
    m_scroll_rest = 0;
    return;
  }
  QAbstractScrollArea* const area = GetScrollArea(m_target);
  if (!area)
    return;
  QScrollBar* const bar = area->verticalScrollBar();
  const float push = (std::abs(stick) - SCROLL_DEADZONE) / (1.0f - SCROLL_DEADZONE);
  m_scroll_rest += std::copysign(push * push, -stick) * SCROLL_PAGES_PER_SECOND *
                   static_cast<float>(bar->pageStep()) * seconds;
  const int whole = static_cast<int>(m_scroll_rest);
  if (whole == 0)
    return;
  m_scroll_rest -= whole;
  bar->setValue(bar->value() + whole);
}

void GamepadNavigation::SendKey(QWidget* widget, int key, const QString& text)
{
  INFO_LOG_FMT(CONTROLLERINTERFACE, "Window navigation: {} to {}",
               QKeySequence(key).toString().toStdString(), widget->metaObject()->className());
  QMetaObject::invokeMethod(
      this,
      [this, target = QPointer<QWidget>(widget), key, text] {
        for (const QEvent::Type type : {QEvent::KeyPress, QEvent::KeyRelease})
        {
          if (!target)
            return;
          QKeyEvent event(type, key, Qt::NoModifier, text);
          // What the key opens may be modal, and then more keys are sent before this returns.
          const QEvent* const outer = m_return;
          m_return = key == Qt::Key_Return ? &event : nullptr;
          QCoreApplication::sendEvent(target, &event);
          m_return = outer;
        }
      },
      Qt::QueuedConnection);
}
