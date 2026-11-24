// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The size of a window Baiji opens in Steam's Game Mode. gamescope shows the newest window of
// the app as the whole picture, scaled to fit SteamVR's 16:9 theater screen: a dialog of another
// shape filled only part of it, and one taller than the screen lost its buttons under the bottom
// edge. So every such window gets a 16:9 size inside the main window's. Pure arithmetic, no Qt.

#pragma once

#include <algorithm>
#include <cmath>

namespace Common::WindowFit
{
struct Size
{
  int width = 0;
  int height = 0;
};

// The smallest part of the area a window takes, as a share of its width and height: a small
// dialog is not stretched across the whole screen, nor shown huge.
constexpr double MIN_SHARE = 0.5;

struct Fit
{
  Size size;
  // The window's minimum size does not fit the area (its content needs a scroll area).
  bool too_small = false;
};

// The largest 16:9 size inside `window` (an even width).
inline Size Area16x9(Size window)
{
  if (window.width <= 0 || window.height <= 0)
    return {};
  int width = window.width;
  int height = window.height;
  if (static_cast<long long>(width) * 9 > static_cast<long long>(height) * 16)
    width = 2 * static_cast<int>(std::lround(height * 16.0 / 9.0 / 2.0));
  else
    height = static_cast<int>(std::lround(width * 9.0 / 16.0));
  return {std::min(width, window.width), std::min(height, window.height)};
}

// The size for a window of size `current` with the given minimum and maximum sizes (0 = none),
// shown over a main window of size `window`: 16:9, large enough for its current size, at least
// MIN_SHARE of the area and at most all of it. A maximum smaller than that (a message box has a
// fixed size) is kept, and so is a minimum larger than the area (`too_small`).
inline Fit FitWindow(Size window, Size current, Size minimum, Size maximum)
{
  Fit fit;
  const Size area = Area16x9(window);
  if (area.width <= 0 || area.height <= 0)
  {
    fit.size = current;
    return fit;
  }
  const int need_w = std::max(current.width, minimum.width);
  const int need_h = std::max(current.height, minimum.height);
  const double share = std::clamp(std::max(static_cast<double>(need_w) / area.width,
                                           static_cast<double>(need_h) / area.height),
                                  MIN_SHARE, 1.0);
  Size size{2 * static_cast<int>(std::lround(area.width * share / 2.0)),
            static_cast<int>(std::lround(area.height * share))};
  size.width = std::min(size.width, area.width);
  size.height = std::min(size.height, area.height);
  if (maximum.width > 0)
    size.width = std::min(size.width, maximum.width);
  if (maximum.height > 0)
    size.height = std::min(size.height, maximum.height);
  fit.too_small = minimum.width > size.width || minimum.height > size.height;
  size.width = std::max(size.width, minimum.width);
  size.height = std::max(size.height, minimum.height);
  fit.size = size;
  return fit;
}
}  // namespace Common::WindowFit
