// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "VideoCommon/VR/FrameUI/Stats.h"
#include "VideoCommon/VR/FrameUI/VrMenu.h"

using namespace VR::UI;
using namespace FrameXr::Input;
using FrameXr::Menu::Canvas;
using FrameXr::Menu::DrawnText;
using FrameXr::Menu::Kit;
namespace Vis = FrameXr::Vis;
namespace Perf = VR::Perf;

namespace
{
constexpr float PI = 3.14159265f;
constexpr Vec3f EYE{0.0f, 1.6f, 0.0f};

Quatf Turn(const Vec3f& axis, float degrees)
{
  const float half = degrees * PI / 360.0f;
  return {axis.x * std::sin(half), axis.y * std::sin(half), axis.z * std::sin(half),
          std::cos(half)};
}

Posef Head(float yaw = 0.0f, float pitch = 0.0f)
{
  return {Multiply(Turn({0, 1, 0}, yaw), Turn({1, 0, 0}, pitch)), EYE};
}

PanelQuad Screen(float distance, float curve_radius = 0.0f)
{
  PanelQuad screen;
  screen.pose = {{0, 0, 0, 1}, EYE + Vec3f{0, 0, -distance}};
  screen.width_m = 2.7f;
  screen.height_m = 1.5f;
  screen.visible = true;
  screen.curve_radius = curve_radius;
  return screen;
}

// Every corner, and the middle, seen from the eye before any of the screen.
bool IsInFront(const StatsWindow& window, const PanelQuad& screen)
{
  const PanelQuad quad = window.GetQuad();
  for (const float x : {-0.5f, 0.0f, 0.5f})
  {
    for (const float y : {-0.5f, 0.0f, 0.5f})
    {
      const Vec3f point =
          Transform(quad.pose, {x * quad.width_m, y * quad.height_m, 0.0f});
      const Ray ray{EYE, Normalized(point - EYE)};
      const QuadHit hit = IntersectQuad(ray, screen);
      if (hit.in_bounds && hit.t <= Length(point - EYE))
        return false;
    }
  }
  return true;
}

float DistanceOf(const StatsWindow& window)
{
  return Length(window.GetQuad().pose.position - EYE);
}

Perf::Stats MakeStats()
{
  Perf::Stats stats;
  stats.serial = 1;
  stats.second.seconds = 1.0;
  stats.second.refreshes = 118;
  stats.second.period_ms = 1000.0 / 120.0;
  stats.second.gpu_frames = 60;
  stats.second.gpu_ms = 6.24;
  stats.second.fps = 59.94;
  stats.second.speed = 1.0;
  stats.second.efb_scale = 3;
  stats.second.running = true;
  stats.frames = Perf::FrameTimes::Second{60, 16.68, 21.3, 2};
  stats.cpu = 43.4f;
  stats.gpu = 87.0f;
  stats.gpu_mhz = 1050;
  stats.setting = 2;
  stats.resolution.on = true;
  stats.resolution.floor = 2;
  stats.resolution.top = 4;
  stats.resolution.change = {4, 3, 5.2};
  return stats;
}

std::string Find(const std::vector<StatsGroup>& groups, const std::string& label)
{
  for (const StatsGroup& group : groups)
  {
    const auto it = std::ranges::find(group.rows, label, &StatsRow::label);
    if (it != group.rows.end())
      return it->value;
  }
  return "(none)";
}

bool IsChanged(const std::vector<StatsGroup>& groups, const std::string& label)
{
  for (const StatsGroup& group : groups)
  {
    const auto it = std::ranges::find(group.rows, label, &StatsRow::label);
    if (it != group.rows.end())
      return it->changed;
  }
  return false;
}
}  // namespace

// ---- The numbers ----

TEST(VRStats, FrameTimes)
{
  Perf::FrameTimes times;
  u64 now = 1'000'000;
  times.Add(now);
  for (int i = 0; i < 30; ++i)
    times.Add(now += 33'333);
  Perf::FrameTimes::Second second = times.Take();
  EXPECT_EQ(second.frames, 30u);
  EXPECT_NEAR(second.average_ms, 33.3, 0.1);
  EXPECT_NEAR(second.peak_ms, 33.3, 0.1);
  // At 30 fps a frame is not slow for that.
  EXPECT_EQ(second.slow, 0u);

  times.Add(now += 100'000);
  times.Add(now += 33'333);
  second = times.Take();
  EXPECT_EQ(second.frames, 2u);
  EXPECT_EQ(second.slow, 1u);
  EXPECT_NEAR(second.peak_ms, 100.0, 0.1);

  // The time in a pause is not a frame's.
  times.Break();
  times.Add(now += 60'000'000);
  times.Add(now += 16'667);
  second = times.Take();
  EXPECT_EQ(second.frames, 1u);
  EXPECT_EQ(second.slow, 0u);
  EXPECT_NEAR(second.peak_ms, 16.7, 0.1);
  EXPECT_EQ(times.Take().frames, 0u);
}

TEST(VRStats, ScaleWatch)
{
  Perf::ScaleWatch watch;
  watch.Update(0, 1);
  watch.Update(2, 2);
  EXPECT_FALSE(watch.GetChange().has_value());
  watch.Update(2, 3);
  EXPECT_FALSE(watch.GetChange().has_value());
  watch.Update(4, 4);
  ASSERT_TRUE(watch.GetChange().has_value());
  EXPECT_EQ(watch.GetChange()->from, 2);
  EXPECT_EQ(watch.GetChange()->to, 4);
  EXPECT_EQ(watch.GetChange()->at_us, 4u);
  watch.Update(4, 9);
  EXPECT_EQ(watch.GetChange()->at_us, 4u);
  // Before the first frame it is not known.
  watch.Update(0, 10);
  watch.Update(4, 11);
  EXPECT_EQ(watch.GetChange()->at_us, 4u);
  watch.Update(3, 12);
  EXPECT_EQ(watch.GetChange()->from, 4);
  EXPECT_EQ(watch.GetChange()->to, 3);
  watch.Reset();
  watch.Update(3, 13);
  EXPECT_FALSE(watch.GetChange().has_value());
}

TEST(VRStats, CpuLoad)
{
  Perf::CpuLoad load;
  const auto read = [&load](const std::string& text) {
    std::istringstream stream(text);
    return load.Update(stream);
  };
  EXPECT_FALSE(read("cpu  100 0 100 700 100 0 0 0 0 0\n").has_value());
  const auto busy = read("cpu  140 0 110 720 130 0 0 0 0 0\n");
  ASSERT_TRUE(busy.has_value());
  EXPECT_NEAR(*busy, 50.0f, 0.1f);
  EXPECT_FALSE(read("").has_value());
  EXPECT_FALSE(read("intr 1 2 3").has_value());
}

// ---- The text ----

TEST(VRStats, Text)
{
  EXPECT_TRUE(GetStatsGroups({}).empty());
  EXPECT_TRUE(GetWristStats({}).empty());

  Perf::Stats stats = MakeStats();
  auto groups = GetStatsGroups(stats);
  EXPECT_EQ(Find(groups, "Frame Rate"), "59.9 FPS");
  EXPECT_EQ(Find(groups, "Speed"), "100%");
  EXPECT_EQ(Find(groups, "Frame Time"), "16.7 ms");
  EXPECT_EQ(Find(groups, "Peak Frame Time"), "21.3 ms");
  EXPECT_EQ(Find(groups, "Slow Frames"), "2");
  EXPECT_EQ(Find(groups, "Headset Frame Rate"), "118 Hz");
  EXPECT_EQ(Find(groups, "Late Frames"), "2");
  EXPECT_EQ(Find(groups, "GPU Time"), "6.2 ms");
  EXPECT_EQ(Find(groups, "CPU"), "43%");
  EXPECT_EQ(Find(groups, "GPU"), "87%, 1050 MHz");
  EXPECT_EQ(Find(groups, "Internal Resolution"), "3x (setting 2x)");
  EXPECT_EQ(Find(groups, "Picture"), "1920 x 1584");
  EXPECT_EQ(Find(groups, "Dynamic Resolution"), "On, 2x to 4x");
  EXPECT_EQ(Find(groups, "Last Change"), "4x to 3x, 5 s ago");

  stats.setting = 3;
  EXPECT_EQ(Find(GetStatsGroups(stats), "Internal Resolution"), "3x");
  stats.setting = 0;
  EXPECT_EQ(Find(GetStatsGroups(stats), "Internal Resolution"), "3x (setting Auto)");
  stats.resolution.on = false;
  EXPECT_EQ(Find(GetStatsGroups(stats), "Dynamic Resolution"), "Off");
  stats.resolution.on = true;
  stats.resolution.top = 2;
  EXPECT_EQ(Find(GetStatsGroups(stats), "Dynamic Resolution"), "On, 2x");
  stats.resolution.change->ago = 130.0;
  EXPECT_EQ(Find(GetStatsGroups(stats), "Last Change"), "4x to 3x, 2 min ago");
  stats.resolution.change->ago = 7300.0;
  EXPECT_EQ(Find(GetStatsGroups(stats), "Last Change"), "4x to 3x, 2 h ago");
}

TEST(VRStats, WhatIsNotKnownHasNoRow)
{
  Perf::Stats stats = MakeStats();
  stats.frames.reset();
  stats.cpu.reset();
  stats.gpu.reset();
  stats.gpu_mhz.reset();
  stats.second.gpu_frames = 0;
  stats.second.efb_scale = 0;
  stats.resolution.change.reset();
  // Match Game shows a picture for a frame of the game, not for every refresh.
  stats.every_refresh = false;
  const auto groups = GetStatsGroups(stats);
  for (const char* const label :
       {"Frame Time", "Peak Frame Time", "Slow Frames", "CPU", "GPU", "GPU Time", "Late Frames",
        "Internal Resolution", "Picture", "Last Change"})
  {
    EXPECT_EQ(Find(groups, label), "(none)") << label;
  }
  EXPECT_EQ(Find(groups, "Frame Rate"), "59.9 FPS");
  const auto tiles = GetWristStats(stats);
  ASSERT_EQ(tiles.size(), static_cast<std::size_t>(WRIST_COLUMNS * WRIST_ROWS));
  EXPECT_EQ(tiles[3].value, "-");
  EXPECT_EQ(tiles[7].value, "-");

  stats.gpu_mhz = 900;
  EXPECT_EQ(Find(GetStatsGroups(stats), "GPU"), "900 MHz");
}

TEST(VRStats, AChangeShowsForAFewSeconds)
{
  Perf::Stats stats = MakeStats();
  stats.resolution.change->ago = 0.3;
  EXPECT_TRUE(IsChanged(GetStatsGroups(stats), "Internal Resolution"));
  EXPECT_TRUE(IsChanged(GetStatsGroups(stats), "Last Change"));
  EXPECT_FALSE(IsChanged(GetStatsGroups(stats), "Frame Rate"));
  EXPECT_EQ(GetWristStats(stats)[2].label, "4x to 3x");
  EXPECT_EQ(GetWristStats(stats)[2].value, "3x");
  EXPECT_TRUE(GetWristStats(stats)[2].changed);

  stats.resolution.change->ago = STATS_CHANGE_SECONDS;
  EXPECT_FALSE(IsChanged(GetStatsGroups(stats), "Internal Resolution"));
  EXPECT_FALSE(IsChanged(GetStatsGroups(stats), "Last Change"));
  EXPECT_EQ(GetWristStats(stats)[2].label, "Resolution");
  EXPECT_FALSE(GetWristStats(stats)[2].changed);
}

TEST(VRStats, EverythingFits)
{
  Perf::Stats longest = MakeStats();
  longest.setting = 0;
  longest.second.fps = 119.9;
  longest.second.speed = 10.0;
  longest.resolution.change->ago = 3500.0;
  longest.frames->peak_ms = 1234.5;
  Perf::Stats changed = MakeStats();
  changed.resolution.change->ago = 1.0;
  Perf::Stats least = MakeStats();
  least.frames.reset();
  least.resolution.change.reset();

  Kit kit(1.5f);
  Canvas canvas;
  canvas.Init(static_cast<int>(STATS_WIDTH * 1.5f), static_cast<int>(STATS_HEIGHT * 1.5f));
  for (const Perf::Stats& stats : {longest, changed, least})
  {
    for (const bool wrist : {false, true})
    {
      std::vector<DrawnText> drawn;
      kit.RecordText(&drawn);
      if (wrist)
        DrawWristStats(&kit, &canvas, stats);
      else
        DrawStats(&kit, &canvas, stats);
      kit.RecordText(nullptr);
      EXPECT_GE(drawn.size(), 18u);
      for (const DrawnText& text : drawn)
      {
        EXPECT_GE(text.box.x0, 4.0f) << text.text;
        EXPECT_LE(text.box.x1, STATS_WIDTH - 4.0f) << text.text;
        EXPECT_GE(text.box.y0, 4.0f) << text.text;
        EXPECT_LE(text.box.y1, (wrist ? STATS_WRIST_HEIGHT : STATS_HEIGHT) - 4.0f) << text.text;
      }
      for (std::size_t i = 0; i < drawn.size(); ++i)
      {
        for (std::size_t k = i + 1; k < drawn.size(); ++k)
        {
          const auto& a = drawn[i].box;
          const auto& b = drawn[k].box;
          EXPECT_FALSE(a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1)
              << drawn[i].text << " and " << drawn[k].text;
        }
      }
    }
  }
}

// ---- The tab ----

TEST(VRStats, Tab)
{
  MenuState state;
  state.tab = MenuTab::Stats;
  for (const bool has_stats : {false, true})
  {
    if (has_stats)
    {
      Perf::Stats stats = MakeStats();
      stats.setting = 0;
      stats.resolution.change->ago = 3500.0;
      state.stats = GetStatsGroups(stats);
    }
    const auto widgets = VrMenu::Layout(state);
    std::vector<SettingId> settings;
    std::size_t rows = 0;
    for (const MenuWidget& w : widgets)
    {
      if (w.item == MenuItem::Setting)
        settings.push_back(static_cast<SettingId>(w.index));
      rows += w.kind == MenuWidget::Kind::Stat;
      EXPECT_GE(w.rect.x0, 0.0f);
      EXPECT_LE(w.rect.x1, VrMenu::WIDTH - 16.0f);
      EXPECT_LE(w.rect.y1, VrMenu::HEIGHT - 16.0f - 8.0f) << w.label;
      EXPECT_NE(w.kind, MenuWidget::Kind::Scrollbar);
    }
    EXPECT_EQ(settings, (std::vector{SettingId::ShowStats, SettingId::StatsPosition}));
    EXPECT_EQ(rows, has_stats ? 14u : 0u);
    EXPECT_EQ(std::ranges::find(widgets, "No Stats Yet", &MenuWidget::label) != widgets.end(),
              !has_stats);

    for (std::size_t i = 0; i < widgets.size(); ++i)
    {
      for (std::size_t k = i + 1; k < widgets.size(); ++k)
      {
        const auto& a = widgets[i].rect;
        const auto& b = widgets[k].rect;
        EXPECT_FALSE(a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1)
            << widgets[i].label << " and " << widgets[k].label;
      }
    }
    // Nothing is cut short.
    Kit kit(1.5f);
    for (const MenuWidget& w : widgets)
    {
      if (w.kind == MenuWidget::Kind::Stat)
      {
        EXPECT_LE(kit.TextWidth(FrameXr::Menu::Type::Body, w.label) + 12.0f +
                      kit.TextWidth(FrameXr::Menu::Type::Value, w.value),
                  w.rect.W())
            << w.label;
      }
    }
  }
}

// ---- Where it is ----

TEST(VRStats, InFrontOfYou)
{
  for (const float yaw : {0.0f, 70.0f, -160.0f})
  {
    for (const float pitch : {0.0f, 80.0f, -80.0f})
    {
      const Posef head = Head(yaw, pitch);
      StatsWindow window;
      window.Place(head, {});
      const PanelQuad quad = window.GetQuad();
      EXPECT_NEAR(DistanceOf(window), 1.0f, 1e-3f);
      const Vec3f to = Normalized(quad.pose.position - EYE);
      // A little below where you look, up to its limit.
      const float look = std::clamp(pitch, -25.0f, 25.0f);
      EXPECT_NEAR(Vis::PitchOf(to) * 180.0f / PI, look - 12.0f, 0.1f);
      EXPECT_NEAR(std::remainder(Vis::YawOf(to) * 180.0f / PI - yaw, 360.0f), 0.0f, 0.1f);
      // Upright, and its front to you.
      EXPECT_NEAR(Rotate(quad.pose.orientation, {0, 1, 0}).y, 1.0f, 1e-3f);
      EXPECT_LT(Dot(Rotate(quad.pose.orientation, {0, 0, 1}), to), 0.0f);
      EXPECT_NEAR(quad.width_m, 0.30f, 1e-3f);
    }
  }
}

TEST(VRStats, NeverBehindTheScreen)
{
  for (const float curve_radius : {0.0f, 2.0f})
  {
    for (const float distance : {0.6f, 0.9f, 1.05f, 2.0f})
    {
      const std::array screens{Screen(distance, curve_radius)};
      StatsWindow window;
      window.Place(Head(), screens);
      EXPECT_TRUE(IsInFront(window, screens[0])) << distance;

      // Pushed away as far as it goes.
      const Vec3f middle = window.GetQuad().pose.position;
      const Ray ray{EYE, Normalized(middle - EYE)};
      window.BeginGrab(EYE, middle);
      for (int i = 0; i < 300; ++i)
        window.Move(EYE, ray, 0.0f, 1.0f, 0.011f, screens);
      EXPECT_TRUE(IsInFront(window, screens[0])) << distance;
      if (distance > 1.5f)
      {
        EXPECT_GT(DistanceOf(window), 1.5f);
      }
      // As large as it gets.
      for (int i = 0; i < 300; ++i)
        window.Move(EYE, ray, 1.0f, 0.0f, 0.011f, screens);
      EXPECT_NEAR(window.GetQuad().width_m, 0.60f, 1e-3f);
      EXPECT_TRUE(IsInFront(window, screens[0])) << distance;

      // The stick's way back is at once, not after what it was pushed on for.
      const float before = DistanceOf(window);
      window.Move(EYE, ray, 0.0f, -1.0f, 0.05f, screens);
      EXPECT_LT(DistanceOf(window), std::max(before, Vis::kStatsMinDistance + 1e-3f));
    }
  }
}

TEST(VRStats, AScreenThatComesNearerPushesIt)
{
  std::array screens{Screen(3.0f)};
  StatsWindow window;
  window.Place(Head(), screens);
  EXPECT_NEAR(DistanceOf(window), 1.0f, 1e-3f);
  const Vec3f before = window.GetQuad().pose.position;
  window.Keep(EYE, screens);
  EXPECT_NEAR(Length(window.GetQuad().pose.position - before), 0.0f, 1e-6f);

  for (float distance = 3.0f; distance >= 0.6f; distance -= 0.01f)
  {
    screens[0] = Screen(distance);
    window.Keep(EYE, screens);
    EXPECT_TRUE(IsInFront(window, screens[0])) << distance;
  }
  EXPECT_LT(DistanceOf(window), 0.6f);
  // It stays there when the screen goes away.
  const Vec3f near_one = window.GetQuad().pose.position;
  screens[0] = Screen(3.0f);
  window.Keep(EYE, screens);
  EXPECT_NEAR(Length(window.GetQuad().pose.position - near_one), 0.0f, 1e-6f);

  // Close to the screen, the head's own small movements do not move it.
  screens[0] = Screen(0.8f);
  window.Place(Head(), screens);
  const Vec3f placed = window.GetQuad().pose.position;
  for (const float x : {0.02f, -0.02f, 0.04f, -0.04f})
    window.Keep(EYE + Vec3f{x, x * 0.5f, x}, screens);
  EXPECT_NEAR(Length(window.GetQuad().pose.position - placed), 0.0f, 1e-6f);
}

TEST(VRStats, AGripCarriesIt)
{
  StatsWindow window;
  window.Place(Head(), {});
  const PanelQuad quad = window.GetQuad();
  const Vec3f hand{0.2f, 1.2f, -0.2f};
  // Held near a corner: it does not jump.
  const Vec3f corner = Transform(quad.pose, {0.1f, 0.1f, 0.0f});
  window.BeginGrab(EYE, corner);
  window.Move(EYE, {hand, Normalized(corner - hand)}, 0.0f, 0.0f, 0.011f, {});
  EXPECT_LT(Length(window.GetQuad().pose.position - quad.pose.position), 0.03f);

  // Pointed to the right it goes there, at its distance.
  const Vec3f right = corner + Vec3f{0.5f, 0.0f, 0.0f};
  for (int i = 0; i < 10; ++i)
    window.Move(EYE, {hand, Normalized(right - hand)}, 0.0f, 0.0f, 0.011f, {});
  EXPECT_GT(window.GetQuad().pose.position.x, quad.pose.position.x + 0.2f);
  EXPECT_NEAR(DistanceOf(window), 1.0f, 1e-2f);

  // A second of the stick is a metre, a stick at rest nothing.
  const float distance = DistanceOf(window);
  for (int i = 0; i < 50; ++i)
    window.Move(EYE, {hand, Normalized(right - hand)}, 0.2f, 0.3f, 0.01f, {});
  EXPECT_NEAR(DistanceOf(window), distance, 1e-2f);
  for (int i = 0; i < 50; ++i)
    window.Move(EYE, {hand, Normalized(right - hand)}, 0.0f, 1.0f, 0.01f, {});
  EXPECT_NEAR(DistanceOf(window), distance + 0.5f, 0.02f);

  // Placed again: as new.
  window.Place(Head(), {});
  EXPECT_NEAR(Length(window.GetQuad().pose.position - quad.pose.position), 0.0f, 1e-5f);
}

TEST(VRStats, OnWrist)
{
  // Held to play, pointing ahead below the head: it faces away.
  const Posef playing{{0, 0, 0, 1}, {-0.2f, 1.15f, -0.35f}};
  EXPECT_FALSE(Vis::WristShown(Vis::WristFacingDeg(Vis::WristStatsPose(playing), EYE), false));

  // The forearm across the body, the back of the hand to the face.
  const Posef local = Vis::WristLocalPose();
  Posef watch;
  watch.position = {0.0f, 1.25f, -0.35f};
  // Turns the panel's front to the eyes.
  const Vec3f to_eye = Normalized(EYE - watch.position);
  const Vec3f x_axis{1.0f, 0.0f, 0.0f};
  const Quatf wanted = FromBasis(x_axis, Cross(to_eye, x_axis), to_eye);
  watch.orientation = Normalized(Multiply(wanted, Conjugate(local.orientation)));
  const Posef panel = Vis::WristStatsPose(watch);
  const float facing = Vis::WristFacingDeg(panel, EYE);
  EXPECT_LT(facing, 30.0f);
  EXPECT_TRUE(Vis::WristShown(facing, false));
  // On the outer side of the left controller, past its handle, not in it.
  EXPECT_LT(local.position.x, -0.03f);
  EXPECT_GT(local.position.z, 0.117f);

  // It does not flicker at the edge.
  EXPECT_FALSE(Vis::WristShown(37.0f, false));
  EXPECT_TRUE(Vis::WristShown(37.0f, true));
  EXPECT_FALSE(Vis::WristShown(41.0f, true));
}
