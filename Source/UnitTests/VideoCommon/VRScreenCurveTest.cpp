// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

// Screen Curve and the eye distance (after Azahar for Frame's tests, frame_xr/screen_rig.cpp and
// ipd.cpp).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "VideoCommon/VR/FrameUI/ScreenPlacement.h"
#include "VideoCommon/VR/FrameUI/geometry.h"
#include "VideoCommon/VR/FrameUI/ipd_smoother.h"
#include "VideoCommon/VR/OpenXRManager.h"

using namespace FrameXr::Input;
using FrameXr::Ipd::Smoother;

namespace
{
constexpr float DEG = 3.14159265f / 180.0f;
constexpr std::array<int, 6> STEPS{15, 30, 45, 60, 75, 90};
constexpr float WIDTH = 1.5f * 16.0f / 9.0f;
constexpr float HEIGHT = 1.5f;

// Turned, raised and away from the origin, as a grip leaves it.
Posef Placed()
{
  return VR::UI::PlaceScreen({{0.3f, 1.6f, -0.2f}, 0.7f}, {2.0f, HEIGHT, 25.0f, 10.0f});
}

PanelQuad Screen(const Posef& pose, int curve)
{
  return {pose, WIDTH, HEIGHT, true, CurveRadius(WIDTH, curve)};
}

// The point of the curved screen at (u, v), from the cylinder layer's own definition: `radius` from
// its axis, u along the central angle, v down its height.
Vec3f OnCylinder(const Cylinder& c, float u, float v)
{
  const float angle = (u - 0.5f) * c.central_angle;
  const float height = c.radius * c.central_angle / c.aspect_ratio;
  return Transform(c.pose, {c.radius * std::sin(angle), (0.5f - v) * height,
                            -c.radius * std::cos(angle)});
}

void ExpectNear(const Vec3f& a, const Vec3f& b, float margin)
{
  EXPECT_NEAR(a.x, b.x, margin);
  EXPECT_NEAR(a.y, b.y, margin);
  EXPECT_NEAR(a.z, b.z, margin);
}

XrCompositionLayerQuad Quad(XrEyeVisibility eye)
{
  const Posef pose = Placed();
  XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
  quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
  quad.space = reinterpret_cast<XrSpace>(std::uintptr_t{0x1234});
  quad.eyeVisibility = eye;
  quad.subImage.swapchain =
      reinterpret_cast<XrSwapchain>(std::uintptr_t{eye == XR_EYE_VISIBILITY_LEFT ? 0x10u : 0x20u});
  quad.subImage.imageRect = {{8, 16}, {1920, 1080}};
  quad.subImage.imageArrayIndex = 1;
  quad.pose = {{pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w},
               {pose.position.x, pose.position.y, pose.position.z}};
  quad.size = {WIDTH, HEIGHT};
  return quad;
}

constexpr s64 PERIOD_NS = 8'333'333;  // 120 Hz

// `seconds` of frames from frame `f` on; returns the next frame. `each` sees every value.
template <typename Distance, typename Each>
int Feed(Smoother& s, int f, double seconds, Distance distance, Each each, bool valid = true)
{
  const int n = static_cast<int>(seconds * 120.0 + 0.5);
  for (int i = 0; i < n; ++i, ++f)
    each(s.Add(f * PERIOD_NS, distance(f), valid));
  return f;
}

int Feed(Smoother& s, int f, double seconds, float distance, bool valid = true)
{
  return Feed(s, f, seconds, [distance](int) { return distance; }, [](float) {}, valid);
}
}  // namespace

TEST(VRScreenCurve, OffIsFlat)
{
  EXPECT_EQ(CurveRadius(WIDTH, 0), 0.0f);
  EXPECT_EQ(CurveRadius(WIDTH, -15), 0.0f);
  EXPECT_EQ(CurveRadius(0.0f, 45), 0.0f);
  // A typed value is never more than half a turn.
  EXPECT_EQ(CurveRadius(WIDTH, 100'000), CurveRadius(WIDTH, kMaxCurveDeg));
}

TEST(VRScreenCurve, KeepsItsSizeShapeAndPlace)
{
  const Posef pose = Placed();
  for (const int deg : STEPS)
  {
    const Cylinder c = CylinderFor(pose, WIDTH, HEIGHT, CurveRadius(WIDTH, deg));
    EXPECT_NEAR(c.central_angle, deg * DEG, 1e-5f);
    EXPECT_NEAR(c.radius * c.central_angle, WIDTH, 1e-5f);
    EXPECT_NEAR(c.radius * c.central_angle / c.aspect_ratio, HEIGHT, 1e-5f);
    ExpectNear(OnCylinder(c, 0.5f, 0.5f), pose.position, 1e-5f);
    // Its sides come towards the viewer, the same on both.
    const Vec3f left = Transform(Inverse(pose), OnCylinder(c, 0.0f, 0.5f));
    const Vec3f right = Transform(Inverse(pose), OnCylinder(c, 1.0f, 0.5f));
    EXPECT_GT(left.z, 0.0f);
    EXPECT_NEAR(left.z, right.z, 1e-5f);
    EXPECT_NEAR(left.x, -right.x, 1e-5f);
    EXPECT_LT(right.x, WIDTH * 0.5f);
  }
}

TEST(VRScreenCurve, FlatLayersAreTheQuadsAsTheyAre)
{
  std::array<XrCompositionLayerQuad, 2> quads{Quad(XR_EYE_VISIBILITY_LEFT),
                                              Quad(XR_EYE_VISIBILITY_RIGHT)};
  const std::array<XrCompositionLayerQuad, 2> before = quads;
  std::array<XrCompositionLayerCylinderKHR, 2> cylinders{};
  std::vector<XrCompositionLayerBaseHeader*> layers;
  VR::AddScreenLayers(quads, CurveRadius(WIDTH, 0), &cylinders, &layers);
  ASSERT_EQ(layers.size(), 2u);
  for (std::size_t i = 0; i < 2; ++i)
  {
    EXPECT_EQ(static_cast<void*>(layers[i]), static_cast<void*>(&quads[i]));
    EXPECT_EQ(std::memcmp(&quads[i], &before[i], sizeof(quads[i])), 0);
  }
}

TEST(VRScreenCurve, CurvedLayersAreTheSamePictures)
{
  std::array<XrCompositionLayerQuad, 2> quads{Quad(XR_EYE_VISIBILITY_LEFT),
                                              Quad(XR_EYE_VISIBILITY_RIGHT)};
  std::array<XrCompositionLayerCylinderKHR, 2> cylinders{};
  std::vector<XrCompositionLayerBaseHeader*> layers;
  const float radius = CurveRadius(WIDTH, 45);
  VR::AddScreenLayers(quads, radius, &cylinders, &layers);
  ASSERT_EQ(layers.size(), 2u);
  for (std::size_t i = 0; i < 2; ++i)
  {
    const XrCompositionLayerCylinderKHR& layer = cylinders[i];
    EXPECT_EQ(static_cast<void*>(layers[i]), static_cast<const void*>(&layer));
    EXPECT_EQ(layers[i]->type, XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR);
    EXPECT_EQ(layer.next, nullptr);
    EXPECT_EQ(layer.layerFlags, quads[i].layerFlags);
    EXPECT_EQ(layer.space, quads[i].space);
    EXPECT_EQ(layer.eyeVisibility, quads[i].eyeVisibility);
    EXPECT_EQ(layer.subImage.swapchain, quads[i].subImage.swapchain);
    EXPECT_EQ(layer.subImage.imageArrayIndex, quads[i].subImage.imageArrayIndex);
    EXPECT_EQ(std::memcmp(&layer.subImage.imageRect, &quads[i].subImage.imageRect,
                          sizeof(XrRect2Di)),
              0);
    EXPECT_EQ(layer.radius, radius);
    EXPECT_NEAR(layer.centralAngle, 45 * DEG, 1e-5f);
    EXPECT_NEAR(layer.aspectRatio, WIDTH / HEIGHT, 1e-5f);
    const Vec3f ahead = Rotate(Placed().orientation, {0.0f, 0.0f, -radius});
    ExpectNear(Vec3f{layer.pose.position.x, layer.pose.position.y, layer.pose.position.z} + ahead,
               Placed().position, 1e-5f);
    EXPECT_EQ(layer.pose.orientation.y, quads[i].pose.orientation.y);
    EXPECT_EQ(layer.pose.orientation.w, quads[i].pose.orientation.w);
  }

  // The flat screen's one layer.
  layers.clear();
  VR::AddScreenLayers(std::span(quads).first(1), radius, &cylinders, &layers);
  ASSERT_EQ(layers.size(), 1u);
  EXPECT_EQ(static_cast<void*>(layers[0]), static_cast<void*>(&cylinders[0]));
}

TEST(VRScreenCurve, BeamsLandOnTheCurve)
{
  const Posef pose = Placed();
  // Where a hand can be: in front of the screen, off to a side, low.
  const std::array<Vec3f, 3> hands{Transform(pose, {0.0f, 0.0f, 1.9f}),
                                   Transform(pose, {0.35f, -0.5f, 1.5f}),
                                   Transform(pose, {-0.4f, 0.1f, 1.7f})};
  for (const int deg : STEPS)
  {
    const PanelQuad screen = Screen(pose, deg);
    const Cylinder c = CylinderFor(pose, WIDTH, HEIGHT, screen.curve_radius);
    for (const Vec3f& hand : hands)
    {
      for (const float u : {0.02f, 0.25f, 0.5f, 0.75f, 0.98f})
      {
        for (const float v : {0.02f, 0.5f, 0.98f})
        {
          const Vec3f point = OnCylinder(c, u, v);
          const Vec3f to = point - hand;
          const QuadHit hit = IntersectQuad({hand, Normalized(to)}, screen);
          ASSERT_TRUE(hit.in_bounds) << deg << " " << u << " " << v;
          EXPECT_NEAR(hit.u, u, 1e-3f);
          EXPECT_NEAR(hit.v, v, 1e-3f);
          EXPECT_NEAR(hit.t, Length(to), 2e-3f);
          ExpectNear(hit.point_world, point, 2e-3f);
        }
      }
      // Off its sides and its top: not on it, so a grip there does not take it.
      constexpr std::array<std::pair<float, float>, 3> OUTSIDE{{{-0.03f, 0.5f}, {1.03f, 0.5f}, {0.5f, -0.03f}}};
      for (const auto &[u, v] : OUTSIDE)
      {
        const QuadHit hit = IntersectQuad({hand, Normalized(OnCylinder(c, u, v) - hand)}, screen);
        EXPECT_FALSE(hit.in_bounds) << deg << " " << u << " " << v;
        // The Wii pointer goes on from there.
        EXPECT_TRUE(hit.on_plane);
        EXPECT_NEAR(hit.u, u, 1e-3f);
      }
    }
    // Hidden, or pointing away.
    PanelQuad hidden = screen;
    hidden.visible = false;
    EXPECT_FALSE(IntersectQuad({hands[0], Normalized(pose.position - hands[0])}, hidden).on_plane);
    EXPECT_FALSE(IntersectQuad({hands[0], Normalized(hands[0] - pose.position)}, screen).in_bounds);
  }
}

TEST(VRScreenCurve, TheFlatHitTestIsThePlane)
{
  const Posef pose = Placed();
  const PanelQuad screen = Screen(pose, 0);
  ASSERT_EQ(screen.curve_radius, 0.0f);
  const Vec3f hand = Transform(pose, {0.3f, -0.4f, 1.6f});
  for (const float u : {-0.2f, 0.1f, 0.5f, 0.9f, 1.2f})
  {
    const Vec3f point = Transform(pose, {(u - 0.5f) * WIDTH, 0.2f * HEIGHT, 0.0f});
    const QuadHit hit = IntersectQuad({hand, Normalized(point - hand)}, screen);
    ASSERT_TRUE(hit.on_plane);
    EXPECT_EQ(hit.in_bounds, u > 0.0f && u < 1.0f);
    EXPECT_NEAR(hit.u, u, 1e-4f);
    EXPECT_NEAR(hit.v, 0.3f, 1e-4f);
    ExpectNear(hit.point_world, point, 1e-3f);
  }
}

// 3D Depth's width is the arc's: at the screen's middle natural 3D puts the two pictures of a thing at
// infinity the eyes' distance apart, as on the flat screen.
TEST(VRScreenCurve, NaturalShiftIsAlongTheArc)
{
  constexpr float EYES = 0.064f;
  // Each eye's picture moves that much, and 1 in NDC is half the width.
  const float shift = VR::UI::NaturalShift(EYES, WIDTH);
  for (const int deg : STEPS)
  {
    const Cylinder c = CylinderFor({}, WIDTH, HEIGHT, CurveRadius(WIDTH, deg));
    const Vec3f left = OnCylinder(c, 0.5f - shift * 0.5f, 0.5f);
    const Vec3f right = OnCylinder(c, 0.5f + shift * 0.5f, 0.5f);
    EXPECT_NEAR(Length(right - left), EYES, 1e-4f);
    EXPECT_LE(Length(right - left), EYES + 1e-6f);  // never past parallel eyes
  }
}

// The menu and the toasts are at most 0.85 of the way to the screen's middle (VrUi::NearerThanScreen).
TEST(VRScreenCurve, NoPartIsMuchNearerThanTheMiddle)
{
  constexpr float DISTANCE = 2.0f;
  const Posef pose{{}, {0.0f, 0.0f, -DISTANCE}};
  for (const int deg : STEPS)
  {
    for (float width = 0.2f; width < 12.0f; width += 0.1f)
    {
      const Cylinder c = CylinderFor(pose, width, HEIGHT, CurveRadius(width, deg));
      for (float u = 0.0f; u <= 1.0f; u += 0.05f)
        EXPECT_GT(Length(OnCylinder(c, u, 0.5f)), 0.92f * DISTANCE) << deg << " " << width;
    }
  }
}

TEST(VREyeDistance, IsTheFallbackUntilKnown)
{
  Smoother s;
  float logged = 0.0f;
  EXPECT_EQ(s.Value(), 0.065f);
  int f = Feed(s, 0, 1.0, 0.061f, false);
  f = Feed(s, f, 1.0, 0.0f);
  f = Feed(s, f, 1.0, NAN);
  f = Feed(s, f, 1.0, 0.2f);
  EXPECT_FALSE(s.Known());
  EXPECT_EQ(s.Value(), 0.065f);
  EXPECT_FALSE(s.TakeLog(logged));
  f = Feed(s, f, FrameXr::Ipd::kWindow / 120.0, 0.0634f);
  EXPECT_TRUE(s.Known());
  EXPECT_NEAR(s.Target(), 0.0634f, 1e-6f);
  Feed(s, f, 0.5, 0.0634f);
  EXPECT_NEAR(s.Value(), 0.0634f, 1e-6f);
  EXPECT_TRUE(s.TakeLog(logged));
  EXPECT_NEAR(logged, 0.0634f, 1e-6f);
  EXPECT_FALSE(s.TakeLog(logged));
}

TEST(VREyeDistance, ANewSessionStartsFromTheLast)
{
  Smoother s;
  s.SetFallback(0.061f);
  EXPECT_FALSE(s.Known());
  EXPECT_EQ(s.Value(), 0.061f);
  s.SetFallback(0.2f);
  EXPECT_EQ(s.Value(), 0.061f);
  Feed(s, 0, 1.0, 0.0634f);
  s.SetFallback(0.07f);
  EXPECT_NEAR(s.Value(), 0.0634f, 1e-6f);
}

TEST(VREyeDistance, JitterIsHeld)
{
  Smoother s;
  int f = Feed(s, 0, 1.5, 0.0634f);
  const float settled = s.Value();
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> noise(-0.0004f, 0.0004f);
  int moved = 0;
  f = Feed(
      s, f, 10.0,
      [&](int i) {
        if (i % 97 == 0)
          return 0.080f;
        return 0.0634f + noise(rng) + ((i & 1) ? 0.00025f : -0.00025f);
      },
      [&](float v) { moved += v != settled; });
  EXPECT_EQ(moved, 0);
  f = Feed(s, f, 0.3, 0.050f, false);
  Feed(s, f, 1.0, 0.0634f);
  EXPECT_EQ(s.Value(), settled);
}

TEST(VREyeDistance, ADialTurnIsAGlide)
{
  Smoother s;
  int f = Feed(s, 0, 20.0, 0.063f);
  EXPECT_NEAR(s.Value(), 0.063f, 1e-6f);
  float last = s.Value();
  float max_step = 0.0f;
  int frames = 0;
  int settled_at = -1;
  f = Feed(
      s, f, 2.0, [](int) { return 0.068f; },
      [&](float v) {
        EXPECT_GE(v, last);
        EXPECT_LE(v, 0.068f + 1e-6f);
        max_step = std::max(max_step, v - last);
        last = v;
        if (settled_at < 0 && std::abs(v - 0.068f) < 0.0001f)
          settled_at = frames;
        ++frames;
      });
  ASSERT_GE(settled_at, 0);
  EXPECT_LT(settled_at, 60);  // half a second
  EXPECT_LT(max_step, 0.0025f);
  Feed(s, f, 1.0, 0.0625f);
  EXPECT_NEAR(s.Value(), 0.0625f, 1e-5f);
}

// The video thread locates the views once or more a game frame, at times that can repeat.
TEST(VREyeDistance, RepeatedAndEarlierTimesAreFine)
{
  Smoother s;
  for (int i = 0; i < 120; ++i)
  {
    s.Add(i / 2 * 33'333'333, 0.063f, true);
    s.Add(i / 2 * 33'333'333 - 1'000'000, 0.063f, true);
  }
  EXPECT_NEAR(s.Value(), 0.063f, 1e-5f);
}
