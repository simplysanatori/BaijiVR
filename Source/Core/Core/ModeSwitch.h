// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

// Switching between VR and Flat (Azahar for Frame's rules). A running game cannot change it: the
// VR boot has no window surface and the Flat one no OpenXR session. So the game's state is saved, the
// game is started again in the other mode, and the state is loaded: DolphinQt/VRModeSwitch.*. Here
// is what that shares with the VR menu and the unit tests.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "Common/CommonTypes.h"
#include "Common/IOFile.h"
#include "Core/State.h"
#include "VideoCommon/VideoConfig.h"

namespace ModeSwitch
{
enum class Mode
{
  Flat,
  VR,
};

using Handler = std::function<void(Mode)>;

inline Handler& GetHandler()
{
  static Handler s_handler;
  return s_handler;
}

// Host thread. The Qt window's; without it there is nothing to switch with.
inline void SetHandler(Handler handler)
{
  GetHandler() = std::move(handler);
}

// Host thread. False: cannot be done here. Else the switch begins, or says why it does not.
inline bool Request(Mode mode)
{
  if (!GetHandler())
    return false;
  GetHandler()(mode);
  return true;
}

// The modes in the settings: the display of every game, and the VR one to go back to
// (Vulkan: none, the player chose Flat in the settings or never left VR).
struct Displays
{
  OpenXRPresentationMode base = OpenXRPresentationMode::StereoScreen;
  OpenXRPresentationMode last_vr = OpenXRPresentationMode::Vulkan;

  bool operator==(const Displays&) const = default;
};

// `running`: the display of the game that runs now, or else the one it would have.
inline Displays GoFlat(Displays displays, OpenXRPresentationMode running)
{
  if (displays.base != OpenXRPresentationMode::Vulkan)
    displays.last_vr = displays.base;
  else if (displays.last_vr == OpenXRPresentationMode::Vulkan)
    displays.last_vr = running;
  // Something to go back to, and what says that Flat was switched to.
  if (displays.last_vr == OpenXRPresentationMode::Vulkan)
    displays.last_vr = OpenXRPresentationMode::StereoScreen;
  displays.base = OpenXRPresentationMode::Vulkan;
  return displays;
}

inline Displays GoVR(Displays displays)
{
  if (displays.base == OpenXRPresentationMode::Vulkan)
  {
    displays.base = displays.last_vr != OpenXRPresentationMode::Vulkan ?
                        displays.last_vr :
                        OpenXRPresentationMode::StereoScreen;
  }
  displays.last_vr = OpenXRPresentationMode::Vulkan;
  return displays;
}

// Flat was switched to, so it is Flat for every game, also for one with a display of its own.
inline bool IsFlatForAll(const Displays& displays)
{
  return displays.base == OpenXRPresentationMode::Vulkan &&
         displays.last_vr != OpenXRPresentationMode::Vulkan;
}

// A game that is started in a mode: what its display has to be changed to, if it has to. A game's
// own display (GameSettingsVR, GameSettings) is put over every game's whenever its files are read.
// `wanted`: while it starts, the VR display it had before (Vulkan: none).
inline std::optional<OpenXRPresentationMode> Correct(Mode mode, OpenXRPresentationMode now,
                                                     const Displays& displays,
                                                     OpenXRPresentationMode wanted)
{
  if (mode == Mode::Flat)
  {
    // (Legacy is the OpenXR hotkey's. A display chosen in the settings meanwhile is the player's.)
    const bool is_vr =
        now == OpenXRPresentationMode::Immersive || now == OpenXRPresentationMode::StereoScreen;
    if (is_vr && displays.base == OpenXRPresentationMode::Vulkan)
      return OpenXRPresentationMode::Vulkan;
    return std::nullopt;
  }
  if (wanted != OpenXRPresentationMode::Vulkan)
    return now != wanted ? std::make_optional(wanted) : std::nullopt;
  if (now == OpenXRPresentationMode::Vulkan && displays.base != OpenXRPresentationMode::Vulkan)
    return displays.base;
  return std::nullopt;
}

// Whether a state saved without compression was written to its end: State::SaveAs cannot tell.
inline bool IsWholeState(const std::string& path)
{
  File::IOFile file(path, "rb");
  State::StateHeaderLegacy legacy{};
  State::StateHeaderVersion version{};
  if (!file || !file.ReadArray(&legacy, 1) || legacy.lzo_size != 0 || !file.ReadArray(&version, 1) ||
      version.version_string_length > 4096)
  {
    return false;
  }
  State::StateExtendedBaseHeader base{};
  if (!file.Seek(version.version_string_length, File::SeekOrigin::Current) ||
      !file.ReadArray(&base, 1) || base.compression_type != State::CompressionType::Uncompressed ||
      base.uncompressed_size == 0)
  {
    return false;
  }
  return file.Tell() + base.payload_offset + base.uncompressed_size == file.GetSize();
}

// Menu held 3 s in Flat asks Switch to VR? Counts only a press that starts after Menu was seen up,
// and not one that is `blocked` at any time: another window has the controller, or the question is
// there already.
class MenuHold
{
public:
  static constexpr s64 ASK_NS = 3'000'000'000;

  // True once, when the press is long enough.
  bool Update(bool menu, bool blocked, s64 now_ns)
  {
    if (!menu)
    {
      m_seen_up = true;
      m_down = false;
      return false;
    }
    if (!m_seen_up)
      return false;
    if (!m_down)
    {
      m_down = true;
      m_spent = false;
      m_since_ns = now_ns;
    }
    m_spent |= blocked;
    if (m_spent || now_ns - m_since_ns < ASK_NS)
      return false;
    m_spent = true;
    return true;
  }

  void Reset() { *this = {}; }

private:
  bool m_seen_up = false;
  bool m_down = false;
  bool m_spent = false;
  s64 m_since_ns = 0;
};

// The question's A (Yes) and B (No). It is answered when the button is let go, so that the game
// does not get the press as it goes on. What is held as it opens is no answer.
class Answer
{
public:
  enum class Result
  {
    None,
    Yes,
    No,
  };

  Result Update(bool yes, bool no)
  {
    if (yes || no)
    {
      if (m_seen_up && m_held == Result::None)
        m_held = no ? Result::No : Result::Yes;
      return Result::None;
    }
    m_seen_up = true;
    return std::exchange(m_held, Result::None);
  }

  void Reset() { *this = {}; }

private:
  bool m_seen_up = false;
  Result m_held = Result::None;
};
}  // namespace ModeSwitch
