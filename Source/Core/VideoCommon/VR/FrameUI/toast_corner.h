// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Where a short toast goes in Stereoscopic 3D, from Azahar for Frame (vis/shader_badge.h, hud.cpp
// ToastQuad, vis/autosave_label.h), with no OpenXR calls (unit tested in VRToastTest.cpp): just
// inside the screen's top-right corner, in the screen's plane (on its curve when the screen is
// curved), so it moves with the screen. Only when all of it is in both eyes' view as it appears;
// else it is ahead of you.

#pragma once

#include <array>

#include "VideoCommon/VR/FrameUI/types.h"

namespace FrameXr::Vis {

/// One eye: its pose and its field of view's half angles (radians, left and down negative), as
/// XrFovf.
struct EyeFov {
    Input::Posef pose;
    float left = 0.0f, right = 0.0f, up = 0.0f, down = 0.0f;
};

/// Whether `point` is in front of `eye` and at least `margin_deg` inside its field of view.
bool PointInView(const EyeFov& eye, const Input::Vec3f& point, float margin_deg);

} // namespace FrameXr::Vis

namespace FrameXr::Vis::Toast {

/// A toast's height over the screen's, and its inset from the screen's top and right edges (the
/// same fraction).
constexpr float kToastHeight = 0.1f;
constexpr float kScreenInset = 0.03f;
/// All of it is at least this far inside each eye's field of view.
constexpr float kInViewMarginDeg = 5.0f;

/// A toast's quad: its centre pose and size (metres).
struct Placed {
    Input::Posef pose;
    float width = 0.0f;
    float height = 0.0f;
};

/// On a curved screen the toast's ends stay this far in front of the cylinder.
constexpr float kCurveClear = 0.002f;
/// How far in front of the screen's surface the toast's centre goes (metres): 3 mm on a flat
/// screen; on a cylinder of `curve_radius` the sagitta of the flat toast's width, w^2 / (8 r), and
/// kCurveClear, so its ends do not go behind the curve.
float CurveLift(float toast_width, float curve_radius);

/// Normal view: on the screen centred at `screen`, `width` x `height` metres, flat or (with
/// `curve_radius` above 0) a cylinder of that radius as ScreenRig::CylinderFor makes it. `aspect`:
/// the toast's width over its height. Shrunk to fit a very small screen. On the cylinder its
/// centre is CurveLift in front of the surface. `share`: its height over the screen's.
Placed OnScreen(const Input::Posef& screen, float width, float height, float curve_radius,
                float aspect, float share = kToastHeight);

/// All of a quad on a screen's corner well in both eyes' view.
bool InView(const std::array<EyeFov, 2>& eyes, const Placed& p);

} // namespace FrameXr::Vis::Toast
