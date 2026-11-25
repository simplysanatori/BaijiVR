// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

// The Qt window with the controller in Steam's Game Mode, while no game runs: the left stick or the
// D-pad moves the game list, A starts the game, B is Escape, and the right stick scrolls. It reads
// SDL's gamepads, which is what Steam Input gives the app in gamepad mode, and sends keys to Baiji's
// own widgets. It never moves or clicks the mouse, and it does nothing on a desktop.

#pragma once

#include <map>
#include <string>

#include <QObject>
#include <QPointer>
#include <QString>

#include "Common/CommonTypes.h"
#include "Common/ListNav.h"

namespace ciface::Core
{
class Device;
}

class GameList;
class QAbstractScrollArea;
class QTimer;
class QWidget;

class GamepadNavigation final : public QObject
{
public:
  // Made only when Host::IsGameMode(); a child of `main`.
  static void Install(QWidget* main, GameList* list);

  bool eventFilter(QObject* object, QEvent* event) override;

private:
  // The gamepad now. The sticks are positive right and up.
  struct Pad
  {
    bool a = false;
    bool b = false;
    bool dpad_up = false;
    bool dpad_down = false;
    bool dpad_left = false;
    bool dpad_right = false;
    float left_x = 0;
    float left_y = 0;
    float right_y = 0;

    // More than noise apart.
    bool Moved(const Pad& from) const;
  };

  // What the controller drives now.
  enum class Target
  {
    None,
    List,    // the game list
    Window,  // a menu, a dropdown or a dialog, GetWindow()
  };

  GamepadNavigation(QWidget* main, GameList* list);

  void SetRunning(bool running);
  void Tick();

  // The SDL gamepad that moved last, or else the first: Steam may leave one behind that no longer
  // reports. False: there is none. Leaves `pad` as it is while the devices are being updated.
  bool ReadPad(Pad* pad);
  static Pad ReadDevice(const ciface::Core::Device& device);

  Target GetTarget() const;
  // The open menu or dropdown, or else the dialog (or other window) over the main window.
  QWidget* GetWindow() const;

  void StepList(int key);
  void StartGame();
  void Escape();
  void Scroll(float stick, float seconds);
  QAbstractScrollArea* GetScrollArea(Target target) const;

  // Sends a key press and release to `widget` from the event loop (what it opens may be modal, and
  // then this would not be called again until it closes).
  void SendKey(QWidget* widget, int key, const QString& text = {});

  QPointer<QWidget> m_main;
  QPointer<GameList> m_list;
  QTimer* m_timer;

  Common::ListNav m_vertical;
  Common::ListNav m_horizontal;
  Common::ButtonPress m_a;
  Common::ButtonPress m_b;

  Pad m_pad;
  Target m_target = Target::None;
  QPointer<QWidget> m_window;
  s64 m_last_tick_ns = 0;
  s64 m_refocus_ns = 0;
  s64 m_other_input_ns;  // a click, wheel or key that came from the window system
  float m_scroll_rest = 0;
  std::string m_device;
  std::map<std::string, Pad> m_seen;  // by device, as it was when it last moved
};
