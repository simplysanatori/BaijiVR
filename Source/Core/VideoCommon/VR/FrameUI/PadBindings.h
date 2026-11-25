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
// Into the mapping, which is the same in every game, and saved.
void SetPadBindings(PadKind kind, const std::vector<std::pair<PadTarget, PadBinding>>& bindings);
// The Steam Frame profile for this controller.
void ResetPadBindings(PadKind kind);

// An input expression in words: "`A` | `Right Trigger`" is "A or Right Trigger".
std::string DescribeExpression(std::string expression);
// The same for a stick's or a D-Pad's "up": "`Left Stick Up`" is "Left Stick".
std::string DescribeUp(std::string expression);
}  // namespace VR::UI
