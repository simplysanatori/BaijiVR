// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "VideoCommon/VR/FrameUI/PadArt.h"
#include "VideoCommon/VR/FrameUI/PadEdit.h"

namespace VR::UI
{
// The emulated controller the running game has, and what is on its controls. Host thread, as all
// of these.
std::pair<PadKind, PadLabels> GetPadBindings(bool wii);

// Why Edit Controls cannot change them, or empty.
std::string GetPadEditLock(PadKind kind);
PadTable GetPadTable(PadKind kind);
// Into the mapping, and saved.
void SetPadBindings(PadKind kind, const std::vector<std::pair<PadTarget, PadBinding>>& bindings);
// The Steam Frame profile for this controller.
void ResetPadBindings(PadKind kind);

// The four above are of the mapping the game has: the profile its settings name, as Dolphin has it
// ([Controls] PadProfile1 or WiimoteProfile1 in the game's INI), or else the one of every game.
enum class PadScope
{
  Global,
  Game,
};
// Game: the game's settings name a profile.
PadScope GetPadScope(PadKind kind);
// Why they cannot, or empty.
std::string GetPadScopeLock(PadKind kind);
// Game: a profile "<game ID> <controller>" from the mapping the game has now, named in its
// settings. Global: they name none ("Use Global Values"), and the profile that Game made is gone.
void SetPadScope(PadKind kind, PadScope scope);

// The profiles the mapping window lists for this controller, the one the game's settings name (or
// empty), and another one for the game (empty: none; a profile stays what it is). A Wii Remote's
// profile is of one controller: the game has that one then.
std::vector<std::string> GetPadProfiles(PadKind kind);
std::string GetPadProfile(PadKind kind);
void SetPadProfile(PadKind kind, const std::string& name);

// An input expression in words: "`A` | `Right Trigger`" is "A or Right Trigger".
std::string DescribeExpression(std::string expression);
// The same for a stick's or a D-Pad's "up": "`Left Stick Up`" is "Left Stick".
std::string DescribeUp(std::string expression);
}  // namespace VR::UI
