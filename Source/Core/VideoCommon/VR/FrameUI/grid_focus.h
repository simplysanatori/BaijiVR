// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.
// SPDX-License-Identifier: GPL-3.0-or-later

// The games the VR menu's Games tab lists: what the game list found, published as one shared list that
// nobody changes after (a new list replaces it). No Qt and no emulator types (from Azahar for Frame,
// with Dolphin's systems).

// The Games tab with the D-pad and the left stick: where the focus goes (GridFocus) and when a
// held direction moves it again (DirRepeat). No drawing and no OpenXR.

#pragma once

#include <algorithm>
#include <cstdint>

namespace FrameXr::Menu::Focus {

enum class Dir { None, Up, Down, Left, Right };
/// Top: the row of chips and buttons over the grid.
enum class Area { Grid, Top, Rail };

/// What there is to move over now.
struct Shape {
    int columns = 4;
    int games = 0;       ///< that show
    int top = 0;         ///< chips and buttons
    int above = 0;       ///< the one of them above the focused tile's column
    int rail = 0;        ///< tabs on the rail
    int rail_active = 0; ///< this tab's place on it
};

/// The focus. It shows after a direction was pressed and hides when the beam moves.
struct GridFocus {
    bool shown = false;
    Area area = Area::Grid;
    int tile = 0; ///< the place in the grid (kept while the focus is elsewhere)
    int top = 0;
    int rail = 0;

    bool operator==(const GridFocus&) const = default;

    /// A direction was pressed. The first one only shows the focus where it was.
    void Move(Dir dir, const Shape& s) {
        if (dir == Dir::None) {
            return;
        }
        tile = std::clamp(tile, 0, std::max(0, s.games - 1));
        if (!shown) {
            shown = true;
            if (area == Area::Grid && s.games == 0) {
                ToTop(s);
            }
            return;
        }
        switch (area) {
        case Area::Grid:
            MoveGrid(dir, s);
            break;
        case Area::Top:
            top = std::clamp(top, 0, std::max(0, s.top - 1));
            if (dir == Dir::Left && top == 0) {
                ToRail(s);
            } else if (dir == Dir::Left || dir == Dir::Right) {
                top = std::clamp(top + (dir == Dir::Left ? -1 : 1), 0, std::max(0, s.top - 1));
            } else if (dir == Dir::Down && s.games > 0) {
                area = Area::Grid;
            }
            break;
        case Area::Rail:
            rail = std::clamp(rail, 0, std::max(0, s.rail - 1));
            if (dir == Dir::Up || dir == Dir::Down) {
                rail = std::clamp(rail + (dir == Dir::Up ? -1 : 1), 0, std::max(0, s.rail - 1));
            } else if (dir == Dir::Right) {
                if (s.games > 0) {
                    area = Area::Grid;
                } else {
                    ToTop(s);
                }
            }
            break;
        }
    }

private:
    void MoveGrid(Dir dir, const Shape& s) {
        const int cols = std::max(1, s.columns);
        const int row = tile / cols, col = tile % cols;
        const int last_row = std::max(0, s.games - 1) / cols;
        switch (dir) {
        case Dir::Left:
            if (col == 0) {
                ToRail(s);
            } else {
                tile--;
            }
            break;
        case Dir::Right:
            if (col + 1 < cols && tile + 1 < s.games) {
                tile++;
            }
            break;
        case Dir::Up:
            if (row == 0) {
                ToTop(s);
            } else {
                tile -= cols;
            }
            break;
        case Dir::Down: // a short last row: its last game
            if (row < last_row) {
                tile = std::min(tile + cols, s.games - 1);
            }
            break;
        default:
            break;
        }
    }
    void ToTop(const Shape& s) {
        if (s.top > 0) {
            area = Area::Top;
            top = s.above;
        }
    }
    void ToRail(const Shape& s) {
        if (s.rail > 0) {
            area = Area::Rail;
            rail = s.rail_active;
        }
    }
};

/// Where the list must scroll so that a row of it (`y0` to `y1` from the list's start) is in
/// view, `view_h` of it showing: no further than needed.
inline float ScrollToShow(float scroll, float y0, float y1, float view_h, float max_scroll) {
    if (y0 < scroll) {
        scroll = y0;
    } else if (y1 > scroll + view_h) {
        scroll = y1 - view_h;
    }
    return std::clamp(scroll, 0.0f, std::max(0.0f, max_scroll));
}

/// A held direction: the first step at once, then repeats, as the game list's (input/list_nav.h).
class DirRepeat {
public:
    static constexpr std::int64_t kDelayNs = 400'000'000, kRepeatNs = 110'000'000;
    static constexpr float kStickOn = 0.6f, kStickOff = 0.4f;

    /// Once per frame, the left stick (x right, y up) and the D-pad. `enabled` false: nothing
    /// comes out, and a direction held as it becomes true does nothing until let go.
    Dir Update(bool enabled, Dir dpad, float x, float y, std::int64_t now_ns) {
        const Dir dir = dpad != Dir::None ? dpad : Stick(x, y);
        if (!enabled) {
            held = dir;
            blocked = true;
            return Dir::None;
        }
        if (dir != held) {
            held = dir;
            blocked = false;
            next_ns = now_ns + kDelayNs;
            return dir;
        }
        if (dir == Dir::None || blocked || now_ns < next_ns) {
            return Dir::None;
        }
        next_ns = std::max(next_ns + kRepeatNs, now_ns + kRepeatNs / 2);
        return dir;
    }

private:
    Dir Stick(float x, float y) {
        const float ax = x < 0.0f ? -x : x, ay = y < 0.0f ? -y : y;
        const float along = stick == Dir::Left || stick == Dir::Right ? ax : ay;
        if (stick != Dir::None && along >= kStickOff) {
            return stick; // the first direction keeps until let go
        }
        stick = std::max(ax, ay) < kStickOn ? Dir::None
                : ax > ay                   ? (x < 0.0f ? Dir::Left : Dir::Right)
                                            : (y < 0.0f ? Dir::Down : Dir::Up);
        return stick;
    }

    Dir stick = Dir::None;
    Dir held = Dir::None;
    bool blocked = true;
    std::int64_t next_ns = 0;
};

} // namespace FrameXr::Menu::Focus
