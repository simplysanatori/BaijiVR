// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// From Azahar for Frame's shader_badge.cpp and toast_fit.cpp: a short toast's place in the screen's
// corner, whether both eyes see all of it, and how long toasts stay.

#include <algorithm>
#include <array>
#include <cmath>

#include <gtest/gtest.h>

#include "VideoCommon/VR/FrameUI/geometry.h"
#include "VideoCommon/VR/FrameUI/toast_corner.h"
#include "VideoCommon/VR/FrameUI/toast_fit.h"

using namespace FrameXr;
using namespace FrameXr::Vis::Toast;

namespace
{
constexpr float DEG = 3.14159265f / 180.0f;
constexpr float ASPECT = 512.0f / 96.0f;  // a short toast's box
constexpr float WIDTHS[] = {16.0f / 9.0f, 4.0f / 3.0f, 1.5f};

Input::Posef Screen()
{
  Input::Posef pose;
  pose.position = {0.0f, 0.0f, -2.0f};
  return pose;
}

// A point of the toast (from -0.5 to 0.5 of its size).
Input::Vec3f Corner(const Placed& toast, float x, float y)
{
  return Input::Transform(toast.pose, {x * toast.width, y * toast.height, 0.0f});
}

// Two eyes, level, turned to the left by `yaw_deg`.
std::array<Vis::EyeFov, 2> Eyes(float half_deg, float yaw_deg = 0.0f)
{
  std::array<Vis::EyeFov, 2> eyes;
  for (std::size_t i = 0; i < eyes.size(); ++i)
  {
    const float half = yaw_deg * DEG * 0.5f;
    eyes[i].pose.orientation = {0.0f, std::sin(half), 0.0f, std::cos(half)};
    eyes[i].pose.position = {i == 0 ? -0.032f : 0.032f, 0.0f, 0.0f};
    eyes[i].left = eyes[i].down = -half_deg * DEG;
    eyes[i].right = eyes[i].up = half_deg * DEG;
  }
  return eyes;
}
}  // namespace

TEST(VRToast, JustInsideTheScreensTopRightCorner)
{
  for (const float width : WIDTHS)
  {
    for (const float height : {0.6f, 1.0f, 2.5f})
    {
      const float w = width * height;
      const Placed toast = OnScreen(Screen(), w, height, 0.0f, ASPECT);
      EXPECT_NEAR(toast.height, height * kToastHeight, 1e-5f);
      EXPECT_NEAR(toast.width, toast.height * ASPECT, 1e-5f);
      const Input::Vec3f top_right = Corner(toast, 0.5f, 0.5f);
      EXPECT_NEAR(top_right.x, w / 2 - height * kScreenInset, 1e-4f);
      EXPECT_NEAR(top_right.y, height / 2 - height * kScreenInset, 1e-4f);
      // In the screen's plane, a few millimetres in front.
      EXPECT_GT(top_right.z, -2.0f);
      EXPECT_LT(top_right.z, -1.99f);
      const Input::Vec3f bottom_left = Corner(toast, -0.5f, -0.5f);
      EXPECT_GT(bottom_left.x, 0.0f);
      EXPECT_GT(bottom_left.y, height * 0.35f);
    }
  }
}

TEST(VRToast, MovesAndTurnsWithTheScreen)
{
  const Placed at_rest = OnScreen(Screen(), 5.0f / 3.0f, 1.0f, 0.0f, ASPECT);
  Input::Posef moved;
  const float half = 15.0f * DEG;
  moved.orientation = {0.0f, std::sin(half), 0.0f, std::cos(half)};
  moved.position = {-0.7f, 0.3f, -1.5f};
  const Placed toast = OnScreen(moved, 5.0f / 3.0f * 1.4f, 1.4f, 0.0f, ASPECT);
  const Input::Vec3f local = Input::Transform(Input::Inverse(moved), toast.pose.position);
  const Input::Vec3f rest = Input::Transform(Input::Inverse(Screen()), at_rest.pose.position);
  EXPECT_NEAR(local.x, rest.x * 1.4f, 1e-4f);
  EXPECT_NEAR(local.y, rest.y * 1.4f, 1e-4f);
  EXPECT_NEAR(Input::Dot(Input::Rotate(toast.pose.orientation, {0, 0, 1}),
                         Input::Rotate(moved.orientation, {0, 0, 1})),
              1.0f, 1e-4f);
}

TEST(VRToast, AllOfItIsInFrontOfACurvedScreen)
{
  EXPECT_NEAR(CurveLift(0.48f, 0.0f), 0.003f, 1e-6f);
  EXPECT_NEAR(CurveLift(0.48f, 1.2f), 0.026f, 1e-4f);
  EXPECT_GT(CurveLift(0.48f, 1.0f), CurveLift(0.48f, 2.0f));
  for (const float width : {5.0f / 3.0f, 2.5f, 1.2f})
  {
    for (int deg = 15; deg <= 90; deg += 15)
    {
      SCOPED_TRACE(deg);
      const float height = width * 0.6f;
      const float radius = Input::CurveRadius(width, deg);
      ASSERT_GT(radius, 0.0f);
      const Placed toast = OnScreen(Screen(), width, height, radius, ASPECT);
      const Input::Cylinder cylinder = Input::CylinderFor(Screen(), width, height, radius);
      float least = 1e9f;
      float most = -1e9f;
      for (int i = 0; i <= 20; ++i)
      {
        const Input::Vec3f p =
            Corner(toast, -0.5f + static_cast<float>(i) / 20.0f, 0.5f) - cylinder.pose.position;
        const float clear = radius - std::hypot(p.x, p.z);
        least = std::min(least, clear);
        most = std::max(most, clear);
      }
      EXPECT_GE(least, 0.001f);
      EXPECT_LE(most, CurveLift(toast.width, radius) + 1e-4f);

      // It faces the axis, as far along the arc as the flat one is to the right.
      const Input::Vec3f d = toast.pose.position - cylinder.pose.position;
      const Input::Vec3f to_axis = Input::Normalized(Input::Vec3f{-d.x, 0.0f, -d.z});
      EXPECT_NEAR(Input::Dot(Input::Rotate(toast.pose.orientation, {0, 0, 1}), to_axis), 1.0f,
                  1e-4f);
      const Placed flat = OnScreen(Screen(), width, height, 0.0f, ASPECT);
      EXPECT_NEAR(std::atan2(d.x, -d.z) * radius, flat.pose.position.x, 0.001f);
    }
  }
}

TEST(VRToast, SmallerWhereItIsTooWide)
{
  const Placed toast = OnScreen(Screen(), 0.3f, 1.0f, 0.0f, ASPECT);
  EXPECT_LE(toast.width, 0.3f - 2.0f * kScreenInset + 1e-5f);
  EXPECT_NEAR(toast.width / toast.height, ASPECT, 1e-3f);
  // A toast as wide as the atlas still fits a 4:3 screen.
  constexpr float WIDEST = static_cast<float>(ToastFit::kAtlasW) / ToastFit::kRowPx;
  const Placed wide = OnScreen(Screen(), 4.0f / 3.0f, 1.0f, 0.0f, WIDEST);
  EXPECT_GT(Corner(wide, -0.5f, 0.0f).x, -2.0f / 3.0f);
}

TEST(VRToast, OnlyWhereBothEyesSeeAllOfIt)
{
  const Placed toast = OnScreen(Screen(), 16.0f / 9.0f, 1.0f, 0.0f, ASPECT);
  // Its corner is 23 degrees to the right and 13 up.
  EXPECT_TRUE(InView(Eyes(45.0f), toast));
  EXPECT_FALSE(InView(Eyes(26.0f), toast));
  // Turned away.
  EXPECT_FALSE(InView(Eyes(45.0f, 30.0f), toast));
  EXPECT_FALSE(InView(Eyes(45.0f, 180.0f), toast));
  // One eye is not enough.
  std::array<Vis::EyeFov, 2> eyes = Eyes(45.0f);
  eyes[0].right = 20.0f * DEG;
  EXPECT_FALSE(InView(eyes, toast));
  // A screen too big for its corner to be in view.
  EXPECT_FALSE(InView(Eyes(45.0f), OnScreen(Screen(), 16.0f / 3.0f, 3.0f, 0.0f, ASPECT)));
}

TEST(VRToast, HowLongTheyStay)
{
  EXPECT_EQ(ToastFit::DurationNs(ToastTime::Brief), 1'500'000'000);
  EXPECT_EQ(ToastFit::DurationNs(ToastTime::Short), 2'000'000'000);
  EXPECT_EQ(ToastFit::DurationNs(ToastTime::Long), 5'000'000'000);
}
