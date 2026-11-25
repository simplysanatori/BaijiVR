// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>
#include <string>
#include <vector>

#include "VideoCommon/VR/FrameUI/geometry.h"
#include "VideoCommon/VR/FrameUI/md3.h"
#include "VideoCommon/VR/FrameUI/stats_place.h"
#include "VideoCommon/VR/Perf/Stats.h"

// The stats as the VR menu's Stats tab and the stats window show them, and where the window is (In
// Front; stats_place.h has On Wrist). After Azahar for Frame's. No OpenXR here (unit tested).
namespace VR::UI
{
struct StatsRow
{
  std::string label;
  std::string value;
  // It has just changed: shown clearly.
  bool changed = false;

  bool operator==(const StatsRow&) const = default;
};

struct StatsGroup
{
  std::string title;
  std::vector<StatsRow> rows;

  bool operator==(const StatsGroup&) const = default;
};

// For how long a change of the resolution in use is shown clearly.
constexpr double STATS_CHANGE_SECONDS = 5.0;

// What is not known has no row. Empty before the first second.
std::vector<StatsGroup> GetStatsGroups(const Perf::Stats& stats);
// The wrist's tiles, WRIST_COLUMNS in a row.
constexpr int WRIST_COLUMNS = 3;
constexpr int WRIST_ROWS = 3;
std::vector<StatsRow> GetWristStats(const Perf::Stats& stats);

// dp, of both pictures. They are in the top left corner of the texture.
constexpr float STATS_WIDTH = 340.0f;
constexpr float STATS_HEIGHT = 372.0f;
constexpr float STATS_WRIST_HEIGHT = 212.0f;

void DrawStats(FrameXr::Menu::Kit* kit, FrameXr::Menu::Canvas* canvas, const Perf::Stats& stats);
void DrawWristStats(FrameXr::Menu::Kit* kit, FrameXr::Menu::Canvas* canvas,
                    const Perf::Stats& stats);

// The window In Front: fixed in the room, a grip moves it, and it is never behind a screen.
class StatsWindow
{
public:
  using Screens = std::span<const FrameXr::Input::PanelQuad>;

  // In front of this head, at its usual distance and size.
  void Place(const FrameXr::Input::Posef& head, Screens screens);
  // Every frame: a screen that has come nearer pushes it along.
  void Keep(const FrameXr::Input::Vec3f& eye, Screens screens);

  void BeginGrab(const FrameXr::Input::Vec3f& eye, const FrameXr::Input::Vec3f& hit);
  // Held with this beam; the stick is its distance and size.
  void Move(const FrameXr::Input::Vec3f& eye, const FrameXr::Input::Ray& ray, float stick_x,
            float stick_y, float dt, Screens screens);

  FrameXr::Input::PanelQuad GetQuad() const;

private:
  float GetWidth() const;
  float GetHeight() const;
  float GetClearDistance(const FrameXr::Input::Vec3f& eye, const FrameXr::Input::Vec3f& direction,
                         float wanted, Screens screens) const;

  FrameXr::Input::Posef m_pose;
  float m_scale = 1.0f;
  float m_wanted = FrameXr::Vis::kStatsFrontDistance;
  FrameXr::Vis::StatsGrab m_grab;
};
}  // namespace VR::UI
