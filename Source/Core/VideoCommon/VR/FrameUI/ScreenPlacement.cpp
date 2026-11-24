// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/FrameUI/ScreenPlacement.h"

#include <algorithm>
#include <cmath>

namespace VR::UI
{
namespace
{
using namespace FrameXr::Input;

constexpr float PI = 3.14159265f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float STICK_THRESHOLD = 0.25f;
// Metres a second at full stick, 1 m away; as much again for each metre further.
constexpr float DISTANCE_RATE = 0.8f;
constexpr float SIZE_RATE = 0.8f;

float WrapAngle(float angle)
{
  while (angle > PI)
    angle -= 2.0f * PI;
  while (angle < -PI)
    angle += 2.0f * PI;
  return angle;
}

float YawOf(const Vec3f& dir)
{
  return std::atan2(-dir.x, -dir.z);
}

float PitchOf(const Vec3f& dir)
{
  const float length = Length(dir);
  return length > 0.0f ? std::asin(std::clamp(dir.y / length, -1.0f, 1.0f)) : 0.0f;
}

Vec3f DirectionFrom(float yaw, float pitch)
{
  const float c = std::cos(pitch);
  return {-std::sin(yaw) * c, std::sin(pitch), -std::cos(yaw) * c};
}

// Where the beam meets the sphere of `radius` round `centre` (the far side: the hand is inside
// it), else the point that far along the beam.
Vec3f SpherePoint(const Vec3f& centre, const Ray& ray, float radius)
{
  const Vec3f m = ray.origin - centre;
  const float b = Dot(m, ray.direction);
  const float c = Dot(m, m) - radius * radius;
  const float disc = b * b - c;
  float t = radius;
  if (disc >= 0.0f)
  {
    const float far_side = -b + std::sqrt(disc);
    if (far_side > 0.0f)
      t = far_side;
  }
  return ray.origin + ray.direction * t;
}

// 0 within the threshold, then up to +-1.
float StickValue(float value)
{
  const float magnitude = std::abs(value);
  if (!(magnitude > STICK_THRESHOLD))
    return 0.0f;
  return std::copysign((magnitude - STICK_THRESHOLD) / (1.0f - STICK_THRESHOLD), value);
}
}  // namespace

Posef PlaceScreen(const ScreenAnchor& anchor, const ScreenPlacement& placement)
{
  const Vec3f centre =
      anchor.position + DirectionFrom(anchor.yaw + placement.yaw_deg * DEG_TO_RAD,
                                      placement.pitch_deg * DEG_TO_RAD) *
                            placement.distance;
  return {FaceViewer(centre, anchor.position, {0.0f, 1.0f, 0.0f}), centre};
}

ScreenPlacement MoveWithStick(ScreenPlacement placement, float stick_x, float stick_y, float dt)
{
  dt = std::clamp(dt, 0.0f, 0.1f);
  const float y = StickValue(stick_y);
  const float x = StickValue(stick_x);
  // One at a time: whichever way the stick is pushed most.
  if (y != 0.0f && std::abs(stick_y) >= std::abs(stick_x))
  {
    placement.distance = std::clamp(placement.distance * std::exp(y * DISTANCE_RATE * dt),
                                    ScreenPlacement::MIN_DISTANCE, ScreenPlacement::MAX_DISTANCE);
  }
  else if (x != 0.0f)
  {
    placement.height = std::clamp(placement.height * std::exp(x * SIZE_RATE * dt),
                                  ScreenPlacement::MIN_HEIGHT, ScreenPlacement::MAX_HEIGHT);
  }
  return placement;
}

void ScreenGrab::Begin(const ScreenAnchor& anchor, const ScreenPlacement& placement, const Ray& ray)
{
  const Vec3f to_hit = SpherePoint(anchor.position, ray, placement.distance) - anchor.position;
  m_yaw_offset =
      WrapAngle(anchor.yaw + placement.yaw_deg * DEG_TO_RAD - YawOf(to_hit));
  m_pitch_offset = placement.pitch_deg * DEG_TO_RAD - PitchOf(to_hit);
}

ScreenPlacement ScreenGrab::Update(const ScreenAnchor& anchor, ScreenPlacement placement,
                                   const Ray& ray) const
{
  const Vec3f to_hit = SpherePoint(anchor.position, ray, placement.distance) - anchor.position;
  placement.yaw_deg =
      WrapAngle(YawOf(to_hit) + m_yaw_offset - anchor.yaw) / DEG_TO_RAD;
  placement.pitch_deg =
      std::clamp((PitchOf(to_hit) + m_pitch_offset) / DEG_TO_RAD, -ScreenPlacement::MAX_PITCH_DEG,
                 ScreenPlacement::MAX_PITCH_DEG);
  return placement;
}

ScreenPlacement ScreenGrab::Nudge(ScreenPlacement placement, float yaw_deg)
{
  m_yaw_offset += yaw_deg * DEG_TO_RAD;
  placement.yaw_deg = WrapAngle((placement.yaw_deg + yaw_deg) * DEG_TO_RAD) / DEG_TO_RAD;
  return placement;
}
}  // namespace VR::UI
