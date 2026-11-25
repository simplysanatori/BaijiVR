// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.
// SPDX-License-Identifier: GPL-3.0-or-later

// A cover as a game's tile shows it (games_tab.cpp): made once, as it is loaded, at the size the
// menu draws it.

#pragma once

#include "VideoCommon/VR/FrameUI/games_snapshot.h"

namespace FrameXr::Menu {

/// The art area, 144 x 104 dp at the menu's 1.5 pixels a dp.
constexpr int kCoverPxW = 216, kCoverPxH = 156;

/// The whole of `cover` (straight RGBA8) over a dimmed, soft copy of it that fills the art area.
/// Empty for an empty image.
GameIcon ComposeCover(const GameIcon& cover);

} // namespace FrameXr::Menu
