// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

#include <atomic>

#include "Common/CommonTypes.h"
#include "Common/VR/PadFilter.h"

// The shortcuts of View in Flat, which has no VR session to read them: Quick Save, Quick Load, Fast
// Forward, and stopping the game. The controller's device filters what it reads; HotkeyScheduler
// says whether a game runs, and takes what was pressed.
namespace Common::VR
{
class FlatShortcuts final
{
public:
  struct Pressed
  {
    bool quick_save = false;
    bool quick_load = false;
    bool fast_forward = false;
    bool stop = false;
  };

  static void SetGameRunning(bool running) { s_running.store(running); }

  // Not from two threads at once.
  void Filter(::VR::UI::Controllers* controllers, s64 now_ns)
  {
    if (!s_running.load())
    {
      m_filter.Reset();
      return;
    }
    // Menu and the right stick's click are the game's, and Switch to VR? reads Menu by itself.
    const ::VR::UI::Controllers raw = *controllers;
    for (auto& controller : *controllers)
      controller.menu_button = controller.thumbstick_button = false;
    const ::VR::UI::PadEvents events = m_filter.Update(controllers, now_ns, false, -1);
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
      (*controllers)[i].menu_button = raw[i].menu_button;
      (*controllers)[i].thumbstick_button = raw[i].thumbstick_button;
    }
    if (events.quick_save)
      s_quick_save = true;
    if (events.quick_load)
      s_quick_load = true;
    if (events.fast_forward)
      s_fast_forward = true;
    if (events.stop)
      s_stop = true;
  }

  void Reset() { m_filter.Reset(); }

  // Since the last time.
  static Pressed Take()
  {
    return {s_quick_save.exchange(false), s_quick_load.exchange(false),
            s_fast_forward.exchange(false), s_stop.exchange(false)};
  }

private:
  ::VR::UI::PadFilter m_filter;

  static inline std::atomic<bool> s_running{false};
  static inline std::atomic<bool> s_quick_save{false};
  static inline std::atomic<bool> s_quick_load{false};
  static inline std::atomic<bool> s_fast_forward{false};
  static inline std::atomic<bool> s_stop{false};
};
}  // namespace Common::VR

#endif  // ENABLE_VR
