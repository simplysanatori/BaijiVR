// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <utility>

#include "VideoCommon/VR/FrameUI/PadArt.h"

namespace VR::UI
{
// The emulated controller the running game has, and what is on its controls. Host thread.
std::pair<PadKind, PadLabels> GetPadBindings(bool wii);

// An input expression in words: "`A` | `Right Trigger`" is "A or Right Trigger".
std::string DescribeExpression(std::string expression);
// The same for a stick's or a D-Pad's "up": "`Left Stick Up`" is "Left Stick".
std::string DescribeUp(std::string expression);
}  // namespace VR::UI
