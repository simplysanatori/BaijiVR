// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "VideoCommon/VR/FrameUI/geometry.h"

// Where the virtual screen is: a distance and a direction from where the player's head was when
// the view was last recentred, always facing that point. A grip carries it round (Azahar for
// Frame's free grab), that hand's stick sets its distance and size. No OpenXR here (unit tested).
namespace VR::UI
{
struct ScreenPlacement
{
  static constexpr float MIN_DISTANCE = 0.5f;
  static constexpr float MAX_DISTANCE = 10.0f;
  static constexpr float MIN_HEIGHT = 0.5f;
  static constexpr float MAX_HEIGHT = 5.0f;
  static constexpr float MAX_PITCH_DEG = 75.0f;

  float distance = 2.0f;
  float height = 1.5f;
  // Degrees from straight ahead of the anchor: positive is left, and up.
  float yaw_deg = 0.0f;
  float pitch_deg = 0.0f;

  bool operator==(const ScreenPlacement&) const = default;
};

// The player's head at the last recentre: its place and its heading (radians, positive left).
struct ScreenAnchor
{
  FrameXr::Input::Vec3f position;
  float yaw = 0.0f;
};

// The screen's pose: it faces +Z (towards the player) like a quad layer.
FrameXr::Input::Posef PlaceScreen(const ScreenAnchor& anchor, const ScreenPlacement& placement);

// The screen with a stick for `dt` seconds: up and down is further and nearer (1 m/s, more from
// further away), right and left is bigger and smaller.
ScreenPlacement MoveWithStick(ScreenPlacement placement, float stick_x, float stick_y, float dt);

// A grip holding the screen: it stays under the beam, at its distance.
class ScreenGrab
{
public:
  void Begin(const ScreenAnchor& anchor, const ScreenPlacement& placement,
             const FrameXr::Input::Ray& ray);
  ScreenPlacement Update(const ScreenAnchor& anchor, ScreenPlacement placement,
                         const FrameXr::Input::Ray& ray) const;
  // Round the anchor, under the beam as it is.
  ScreenPlacement Nudge(ScreenPlacement placement, float yaw_deg);

private:
  float m_yaw_offset = 0.0f;
  float m_pitch_offset = 0.0f;
};
}  // namespace VR::UI
