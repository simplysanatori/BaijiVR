// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoCommon/VR/FrameUI/Stats.h"

#ifdef ENABLE_VR

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <fmt/format.h>

#include "VideoCommon/VideoCommon.h"

namespace VR::UI
{
namespace
{
using namespace FrameXr::Menu;
using namespace FrameXr::Input;
namespace Vis = FrameXr::Vis;

constexpr float ASPECT = STATS_HEIGHT / STATS_WIDTH;

std::string FormatMs(double ms)
{
  return fmt::format("{:.1f} ms", ms);
}

std::string FormatScale(int scale)
{
  return fmt::format("{}x", scale);
}

std::string FormatSetting(int setting)
{
  return setting > 0 ? FormatScale(setting) : "Auto";
}

std::string FormatAgo(double seconds)
{
  const int whole = std::max(0, static_cast<int>(std::lround(seconds)));
  if (whole < 60)
    return fmt::format("{} s ago", whole);
  if (whole < 3600)
    return fmt::format("{} min ago", whole / 60);
  return fmt::format("{} h ago", whole / 3600);
}

std::string FormatInUse(const Perf::Stats& stats)
{
  if (stats.second.efb_scale == stats.setting)
    return FormatScale(stats.second.efb_scale);
  return fmt::format("{} (setting {})", FormatScale(stats.second.efb_scale),
                     FormatSetting(stats.setting));
}

std::string FormatChange(const DynamicResolution::Status::Change& change)
{
  return fmt::format("{} to {}", FormatScale(change.from), FormatScale(change.to));
}

std::string FormatDynamicResolution(const DynamicResolution::Status& status)
{
  if (!status.on)
    return "Off";
  if (status.floor <= 0)
    return "On";
  if (status.top <= status.floor)
    return fmt::format("On, {}", FormatScale(status.floor));
  return fmt::format("On, {} to {}", FormatScale(status.floor), FormatScale(status.top));
}

bool HasJustChanged(const Perf::Stats& stats)
{
  return stats.resolution.change && stats.resolution.change->ago < STATS_CHANGE_SECONDS;
}

double GetHz(const Perf::Second& second)
{
  return second.seconds > 0.0 ? second.refreshes / second.seconds : 0.0;
}

// The refreshes there was no time for. Only counted where every refresh gets a picture.
std::optional<int> GetLate(const Perf::Stats& stats)
{
  const Perf::Second& second = stats.second;
  if (!stats.every_refresh || second.period_ms <= 0.0)
    return std::nullopt;
  const double expected = second.seconds * 1000.0 / second.period_ms;
  return std::max(0, static_cast<int>(std::lround(expected)) - static_cast<int>(second.refreshes));
}

std::string FormatGpu(const Perf::Stats& stats)
{
  std::string text;
  if (stats.gpu)
    text = fmt::format("{:.0f}%", *stats.gpu);
  if (stats.gpu_mhz)
    text += fmt::format("{}{} MHz", text.empty() ? "" : ", ", *stats.gpu_mhz);
  return text;
}

// The largest size up to `size` that the text fits at.
float FitSize(const Kit& kit, float size, bool medium, const std::string& text, float room)
{
  const float width = kit.TextWidthSized(size, medium, text);
  return width > room ? size * room / width : size;
}
}  // namespace

std::vector<StatsGroup> GetStatsGroups(const Perf::Stats& stats)
{
  if (stats.serial == 0)
    return {};
  const Perf::Second& second = stats.second;
  const bool changed = HasJustChanged(stats);

  StatsGroup game{"Game", {}};
  game.rows.push_back({"Frame Rate", fmt::format("{:.1f} FPS", second.fps)});
  game.rows.push_back({"Speed", fmt::format("{:.0f}%", second.speed * 100.0)});
  if (stats.frames)
  {
    game.rows.push_back({"Frame Time", FormatMs(stats.frames->average_ms)});
    game.rows.push_back({"Peak Frame Time", FormatMs(stats.frames->peak_ms)});
    game.rows.push_back({"Slow Frames", fmt::format("{}", stats.frames->slow)});
  }

  StatsGroup resolution{"Resolution", {}};
  if (second.efb_scale > 0)
  {
    resolution.rows.push_back({"Internal Resolution", FormatInUse(stats), changed});
    resolution.rows.push_back(
        {"Picture", fmt::format("{} x {}", second.efb_scale * static_cast<int>(EFB_WIDTH),
                                second.efb_scale * static_cast<int>(EFB_HEIGHT))});
  }
  resolution.rows.push_back({"Dynamic Resolution", FormatDynamicResolution(stats.resolution)});
  if (const auto& change = stats.resolution.change)
  {
    resolution.rows.push_back(
        {"Last Change", fmt::format("{}, {}", FormatChange(*change), FormatAgo(change->ago)),
         changed});
  }

  StatsGroup system{"Headset", {}};
  system.rows.push_back({"Headset Frame Rate", fmt::format("{:.0f} Hz", GetHz(second))});
  if (const auto late = GetLate(stats))
    system.rows.push_back({"Late Frames", fmt::format("{}", *late)});
  if (second.gpu_frames > 0)
    system.rows.push_back({"GPU Time", FormatMs(second.gpu_ms)});
  if (const std::string gpu = FormatGpu(stats); !gpu.empty())
    system.rows.push_back({"GPU", gpu});
  if (stats.cpu)
    system.rows.push_back({"CPU", fmt::format("{:.0f}%", *stats.cpu)});

  return {std::move(game), std::move(resolution), std::move(system)};
}

std::vector<StatsRow> GetWristStats(const Perf::Stats& stats)
{
  if (stats.serial == 0)
    return {};
  const Perf::Second& second = stats.second;
  const std::string none = "-";
  const bool changed = HasJustChanged(stats);
  const auto& frames = stats.frames;
  const auto late = GetLate(stats);
  std::vector<StatsRow> tiles{
      {"FPS", fmt::format("{:.0f}", second.fps)},
      {"Speed", fmt::format("{:.0f}%", second.speed * 100.0)},
      // What it changed from and to, while that is news.
      {changed ? FormatChange(*stats.resolution.change) : "Resolution",
       second.efb_scale > 0 ? FormatScale(second.efb_scale) : none, changed},
      {"Frame ms", frames ? fmt::format("{:.1f}", frames->average_ms) : none},
      {"Peak ms", frames ? fmt::format("{:.1f}", frames->peak_ms) : none},
      {"Slow", frames ? fmt::format("{}", frames->slow) : none},
      {"Hz", fmt::format("{:.0f}", GetHz(second))},
      {"Late", late ? fmt::format("{}", *late) : none},
      {"GPU ms", second.gpu_frames > 0 ? fmt::format("{:.1f}", second.gpu_ms) : none},
  };
  return tiles;
}

void DrawStats(Kit* kit, Canvas* canvas, const Perf::Stats& stats)
{
  constexpr float PAD = 18.0f;
  constexpr float GAP = 13.0f;
  constexpr float SIZE = 15.0f;
  const std::vector<StatsGroup> groups = GetStatsGroups(stats);
  std::size_t rows = 0;
  for (const StatsGroup& group : groups)
    rows += group.rows.size();

  std::ranges::fill(canvas->px, 0u);
  kit->Begin(*canvas);
  kit->ClearClip();
  kit->Fill({1.0f, 1.0f, STATS_WIDTH - 1.0f, STATS_HEIGHT - 1.0f}, 18.0f, Tok::kSurfaceContainer,
            0.94f);
  if (rows == 0)
    return;
  // The same height with any number of rows.
  const float gaps = GAP * static_cast<float>(groups.size() - 1);
  const float row_h =
      std::min(26.0f, (STATS_HEIGHT - PAD * 2.0f - gaps) / static_cast<float>(rows));
  float y = PAD;
  for (const StatsGroup& group : groups)
  {
    if (&group != &groups.front())
    {
      kit->Line(PAD, y + GAP * 0.5f, STATS_WIDTH - PAD, y + GAP * 0.5f, 1.0f, Tok::kOutlineVariant);
      y += GAP;
    }
    for (const StatsRow& row : group.rows)
    {
      const Rect r{PAD, y, STATS_WIDTH - PAD, y + row_h};
      if (row.changed)
        kit->Fill({r.x0 - 8.0f, r.y0, r.x1 + 8.0f, r.y1}, 8.0f, Tok::kPrimary);
      const float base = y + row_h * 0.5f + SIZE * 0.36f;
      kit->TextSized(r.x0, base, SIZE, false, row.changed ? Tok::kOnPrimary : Tok::kOnSurfaceVariant,
                     row.label);
      const float room = r.W() - kit->TextWidthSized(SIZE, false, row.label) - 10.0f;
      kit->TextSized(r.x1, base, FitSize(*kit, SIZE, true, row.value, room), true,
                     row.changed ? Tok::kOnPrimary : Tok::kOnSurface, row.value, 2);
      y += row_h;
    }
  }
}

void DrawWristStats(Kit* kit, Canvas* canvas, const Perf::Stats& stats)
{
  constexpr float PAD = 10.0f;
  constexpr float VALUE = 26.0f;
  constexpr float LABEL = 14.0f;
  constexpr float TILE_W = (STATS_WIDTH - PAD * 2.0f) / WRIST_COLUMNS;
  constexpr float TILE_H = (STATS_WRIST_HEIGHT - PAD * 2.0f) / WRIST_ROWS;
  const std::vector<StatsRow> tiles = GetWristStats(stats);

  std::ranges::fill(canvas->px, 0u);
  kit->Begin(*canvas);
  kit->ClearClip();
  kit->Fill({1.0f, 1.0f, STATS_WIDTH - 1.0f, STATS_WRIST_HEIGHT - 1.0f}, 20.0f,
            Tok::kSurfaceContainer, 0.94f);
  for (std::size_t i = 0; i < tiles.size(); ++i)
  {
    const StatsRow& tile = tiles[i];
    const float x = PAD + TILE_W * static_cast<float>(i % WRIST_COLUMNS);
    const float y = PAD + TILE_H * static_cast<float>(i / WRIST_COLUMNS);
    const float cx = x + TILE_W * 0.5f;
    const float room = TILE_W - 10.0f;
    if (tile.changed)
      kit->Fill({x + 2.0f, y + 2.0f, x + TILE_W - 2.0f, y + TILE_H - 2.0f}, 12.0f, Tok::kPrimary);
    kit->TextSized(cx, y + TILE_H * 0.52f, FitSize(*kit, VALUE, true, tile.value, room), true,
                   tile.changed ? Tok::kOnPrimary : Tok::kOnSurface, tile.value, 1);
    kit->TextSized(cx, y + TILE_H * 0.52f + LABEL + 5.0f,
                   FitSize(*kit, LABEL, false, tile.label, room), false,
                   tile.changed ? Tok::kOnPrimary : Tok::kOnSurfaceVariant, tile.label, 1);
  }
}

// ---- Where it is --------------------------------------------------------------------------------

float StatsWindow::GetWidth() const
{
  return Vis::kStatsFrontWidth * m_scale;
}

float StatsWindow::GetHeight() const
{
  return GetWidth() * ASPECT;
}

PanelQuad StatsWindow::GetQuad() const
{
  PanelQuad quad;
  quad.pose = m_pose;
  quad.width_m = GetWidth();
  quad.height_m = GetHeight();
  quad.visible = true;
  return quad;
}

float StatsWindow::GetClearDistance(const Vec3f& eye, const Vec3f& direction, float wanted,
                                    Screens screens) const
{
  return Vis::StatsClearDistance(eye, direction, wanted, GetWidth(), GetHeight(),
                                 {screens.begin(), screens.end()});
}

void StatsWindow::Place(const Posef& head, Screens screens)
{
  const Vec3f direction = Vis::StatsFrontDirection(head);
  m_scale = 1.0f;
  m_wanted = GetClearDistance(head.position, direction, Vis::kStatsFrontDistance, screens);
  m_pose = Vis::StatsPoseAt(head.position, direction, m_wanted);
}

void StatsWindow::Keep(const Vec3f& eye, Screens screens)
{
  const Vec3f to = m_pose.position - eye;
  const float now = Length(to);
  if (now < Vis::kStatsMinDistance)
    return;
  const Vec3f direction = to * (1.0f / now);
  const float clear = GetClearDistance(eye, direction, now, screens);
  // It stands still until most of its gap to the screen is gone: the head's own movements change
  // that a little all the time.
  if (clear + Vis::kStatsScreenGap * 0.7f < now)
  {
    m_wanted = clear;
    m_pose = Vis::StatsPoseAt(eye, direction, clear);
  }
}

void StatsWindow::BeginGrab(const Vec3f& eye, const Vec3f& hit)
{
  m_grab.Begin(eye, m_pose, hit);
  m_wanted = Length(m_pose.position - eye);
}

void StatsWindow::Move(const Vec3f& eye, const Ray& ray, float stick_x, float stick_y, float dt,
                       Screens screens)
{
  dt = std::clamp(dt, 0.0f, 0.1f);
  const bool pushed = std::abs(stick_y) > Vis::kStatsStickThreshold;
  if (pushed)
  {
    m_wanted = std::clamp(m_wanted + stick_y * Vis::kStatsDepthSpeed * dt, Vis::kStatsMinDistance,
                          Vis::kStatsMaxDistance);
  }
  if (std::abs(stick_x) > Vis::kStatsStickThreshold)
  {
    m_scale = std::clamp(m_scale * std::exp(stick_x * Vis::kStatsScaleRate * dt),
                         Vis::kStatsMinScale, Vis::kStatsMaxScale);
  }
  const float now = Length(m_pose.position - eye);
  const Vec3f direction = m_grab.Direction(eye, ray, now);
  const float distance = GetClearDistance(eye, direction, m_wanted, screens);
  // Not pushed on through a screen.
  if (pushed)
    m_wanted = std::min(m_wanted, distance);
  m_pose = Vis::StatsPoseAt(eye, direction, distance);
}
}  // namespace VR::UI

#endif  // ENABLE_VR
