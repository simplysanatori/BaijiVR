// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>

// Dynamic Resolution's decisions (from Azahar for Frame), free of Dolphin, threads and settings
// (unit tested). The internal resolution is a whole scale of the EFB: the Internal Resolution setting
// is the floor, up to MAX_UP steps more are drawn while the GPU has room for them.
//
// What the GPU's limit is here: a 60 fps game at 120 Hz hands over a pair of eye pictures every second
// refresh. The pair is submitted when the game shows its frame, the submit worker waits for the GPU and
// only then is it given to the runtime; the game goes on meanwhile and the pacing thread shows the last
// pair again. A pair whose GPU work is not done when the next one comes costs that next one
// (Sample::skipped). So the GPU has the game's own frame time (16.7 ms at 60 fps, 33 ms at 30 fps), not
// the headset's period, and being the limit means its work fills that time. Pairs left out are pictures
// too: the frame time is over what the game drew, shown or not (Interval).
//
// A second is bad (the GPU is the limit, Bad) with one of:
//  - the GPU's time BAD_SHARE of the game's frame time;
//  - pairs left out (MIN_SKIPPED, SKIPPED_SHARE), with the GPU busy (BUSY_SHARE of the frame time);
//  - the game under full speed, with the GPU busy and its time and the waits for it filling the frame
//    time;
//  - refreshes lost (xrWaitFrame or xrEndFrame late) with the GPU busy longer than a refresh: the
//    compositor has its work to do each refresh on the same GPU.
// Frames missed, or a slow game, while the GPU has room are the emulation's: a lower resolution does
// nothing for them, and they are never bad.
//
// A second is good (room for the next scale, Good) at full speed with nothing left out, when the
// predicted cost there is under UP_SHARE of BAD_SHARE of the frame time, or of UP_PERIODS refreshes when
// that is less (a 30 fps game: long GPU work makes the picture late and takes the compositor's turns).
//
// Not jarring (lessons of Azahar for Frame's sessions):
//  - DOWN_AFTER bad seconds in a row for a step down and UP_AFTER good ones for a step up, one scale at
//    a time; any other second ends a run;
//  - SETTLE seconds after a step are not judged (its own hitch, new textures at the new size), and good
//    ones do not count for HOLD_AFTER_DOWN more after a step down;
//  - a step up is judged on its predicted cost: the cost now times the pixels of the next scale over this
//    one's, replaced by what this game measured between the two (Ratio);
//  - a step down within FAIL_WINDOW of a step up: the step up failed. That scale waits RETRY_WAIT,
//    RETRY_GROWTH times that the second time, and is left alone after FAILS_FOR_GAME;
//  - remembered per game: the scale stepped down to, or one held REMEMBER_AFTER judged seconds. A game
//    starts there, one without starts at the setting;
//  - never a scale above what the screen can show (ScreenCoverage, CeilingHold), nor under the floor.
namespace VR::DynamicResolution
{
// About a second of the game.
struct Sample
{
  double seconds = 1.0;
  // The game ran for all of it (not paused, no menu), and how fast (1.0 is full speed).
  bool running = false;
  double speed = 1.0;
  // The headset: its period and the refreshes it got; every_refresh when a frame is submitted for each
  // one (the eager heartbeat).
  double period_ms = 0.0;
  std::uint32_t refreshes = 0;
  bool every_refresh = true;
  // New pairs shown, pairs whose GPU time is known and its average (submit to the end), pairs left out.
  std::uint32_t fresh = 0;
  std::uint32_t gpu_frames = 0;
  double gpu_ms = 0.0;
  std::uint32_t skipped = 0;
  // The waits of the CPU for the GPU, summed.
  double gpu_wait_ms = 0.0;
  // The EFB's scale as drawn at the end of it.
  int scale = 0;
};

class Controller
{
public:
  static constexpr int MAX_UP = 2;
  static constexpr int MAX_SCALE = 32;

  static constexpr double BAD_SHARE = 0.85;
  static constexpr double BUSY_SHARE = 0.6;
  static constexpr std::uint32_t MIN_SKIPPED = 2;
  static constexpr double SKIPPED_SHARE = 0.04;
  static constexpr double FULL_SPEED = 0.97;
  static constexpr double FAST_FORWARD = 1.1;
  static constexpr double LOST_SHARE = 0.95;
  static constexpr double UP_SHARE = 0.75;
  static constexpr double UP_PERIODS = 2.0;
  // Fewer timed pairs say nothing (a loading screen).
  static constexpr std::uint32_t MIN_GPU_FRAMES = 5;

  static constexpr int DOWN_AFTER = 5;
  static constexpr int UP_AFTER = 10;
  static constexpr int SETTLE = 5;
  static constexpr int HOLD_AFTER_DOWN = 5;
  static constexpr int FAIL_WINDOW = 60;
  static constexpr int RETRY_WAIT = 180;
  static constexpr int RETRY_GROWTH = 3;
  static constexpr int FAILS_FOR_GAME = 3;
  static constexpr int REMEMBER_AFTER = 60;

  // The judged seconds after a step that measure the new scale, and the limits of the ratio they give.
  static constexpr int MEASURE_SECONDS = 3;
  static constexpr double RATIO_MIN = 1.0;
  static constexpr double RATIO_OVER_PIXELS = 1.25;
  // A scale's cost is the median of its last COST_KEEP judged seconds, once there are COST_SECONDS.
  static constexpr int COST_KEEP = 120;
  static constexpr int COST_SECONDS = 10;

  enum class Step
  {
    None,
    Up,
    Down,
  };

  // Judges a second. `max_up` is how far above the floor it may go, `ceiling_up` how far the screen can
  // show (its moves are no step). Returns the steps above the floor to draw at now.
  int Judge(const Sample& s, int floor, int max_up, int ceiling_up = MAX_UP)
  {
    m_last_step = Step::None;
    m_now += std::max(0.0, s.seconds);
    max_up = std::clamp(max_up, 0, MAX_UP);
    const int shown_before = Up();
    m_level = std::min(m_level, max_up);
    m_ceiling = std::clamp(ceiling_up, 0, max_up);
    if (Up() != shown_before)
    {
      m_held = 0;
      m_measure_from = 0;
      m_bad = m_good = 0;
      m_settle = SETTLE;
    }
    if (!s.running || s.seconds <= 0.0 || s.period_ms <= 0.0 || s.speed > FAST_FORWARD)
    {
      m_bad = m_good = 0;
      return Up();
    }
    // The video thread has not drawn at the new scale yet.
    if (s.scale != floor + Up())
    {
      m_bad = m_good = 0;
      m_settle = SETTLE;
      return Up();
    }
    if (m_settle > 0)
    {
      --m_settle;
      return Up();
    }

    ++m_held;
    const bool holding = m_hold > 0;
    if (holding)
      --m_hold;
    if (s.gpu_frames >= MIN_GPU_FRAMES && ValidScale(s.scale))
    {
      Measure(s);
      m_costs[s.scale][m_cost_n[s.scale]++ % COST_KEEP] = static_cast<float>(Cost(s));
    }

    if (Bad(s))
    {
      m_good = 0;
      AddToRun(&m_bad, s);
      if (m_bad >= DOWN_AFTER && Up() > 0)
      {
        if (m_now - m_last_up <= FAIL_WINDOW && ValidScale(s.scale))
        {
          const int fails = ++m_failed[s.scale];
          m_blocked_until[s.scale] =
              fails >= FAILS_FOR_GAME ? NEVER :
                                        m_now + RETRY_WAIT * std::pow(RETRY_GROWTH, fails - 1);
          m_last_up = -NEVER;
        }
        m_level = Up() - 1;
        m_hold = HOLD_AFTER_DOWN;
        StepDone(Step::Down, s.scale);
      }
      return Up();
    }

    m_bad = 0;
    if (!holding && Good(s))
    {
      AddToRun(&m_good, s);
      if (m_good >= UP_AFTER && m_level < std::min(max_up, m_ceiling) && !Blocked(s.scale + 1))
      {
        ++m_level;
        m_last_up = m_now;
        StepDone(Step::Up, s.scale);
      }
    }
    else
    {
      m_good = 0;
    }
    return Up();
  }

  // A game's start, `up` steps above the floor (the remembered scale). What the last one measured goes.
  void Start(int up)
  {
    *this = Controller{};
    m_level = std::clamp(up, 0, MAX_UP);
    if (m_level > 0)
    {
      m_settle = SETTLE;
      m_hold = HOLD_AFTER_DOWN;
    }
  }

  // Judged again after a while without (turned off and on): the level stays.
  void Resume()
  {
    m_bad = m_good = 0;
    m_settle = SETTLE;
    m_hold = HOLD_AFTER_DOWN;
    m_measure_from = 0;
  }

  // The steps drawn above the floor, and the level the seconds decided (without the screen's ceiling).
  int Up() const { return std::min(m_level, m_ceiling); }
  int Level() const { return m_level; }
  Step LastStep() const { return m_last_step; }
  // The level is one to remember for the game: stepped down to, or held.
  bool IsSettled() const
  {
    return m_last_step == Step::Down || (m_held >= REMEMBER_AFTER && Up() == m_level);
  }
  bool Blocked(int scale) const { return ValidScale(scale) && m_now < m_blocked_until[scale]; }

  // The game's frame time: over the pairs it drew, the ones left out too.
  static double Interval(const Sample& s)
  {
    const std::uint32_t pairs = Pairs(s);
    return pairs ? 1000.0 * s.seconds / pairs : 0.0;
  }

  // The GPU's time for one of the game's frames, with the waits for it on the way there. It may count
  // some of it twice, which only makes a step up less likely.
  static double Cost(const Sample& s)
  {
    const std::uint32_t pairs = Pairs(s);
    return s.gpu_ms + (pairs ? s.gpu_wait_ms / pairs : 0.0);
  }

  static bool Bad(const Sample& s)
  {
    if (s.gpu_frames < MIN_GPU_FRAMES)
      return false;
    const double interval = Interval(s);
    if (s.gpu_ms >= BAD_SHARE * interval)
      return true;
    if (s.gpu_ms >= BUSY_SHARE * interval)
    {
      if (s.skipped >= MIN_SKIPPED && s.skipped >= SKIPPED_SHARE * Pairs(s))
        return true;
      if (s.speed < FULL_SPEED && Cost(s) >= BAD_SHARE * interval)
        return true;
    }
    return LostRefreshes(s) && s.gpu_ms >= s.period_ms;
  }

  // What the cost of the next scale has to be under.
  static double UpBudget(const Sample& s)
  {
    return UP_SHARE * BAD_SHARE * std::min(Interval(s), UP_PERIODS * s.period_ms);
  }

  bool Good(const Sample& s) const
  {
    return s.gpu_frames >= MIN_GPU_FRAMES && s.speed >= FULL_SPEED && s.skipped == 0 &&
           !LostRefreshes(s) && PredictUp(s) < UpBudget(s);
  }

  double PredictUp(const Sample& s) const { return Cost(s) * Ratio(s.scale); }

  static double PixelRatio(int scale)
  {
    const double f = std::max(1, scale);
    return ((f + 1.0) / f) * ((f + 1.0) / f);
  }

  // The cost of scale + 1 over scale's: the pixels', lowered by what says it is cheaper (a step down, the
  // medians of both scales in this game), but never under what a step up between the two measured.
  double Ratio(int scale) const
  {
    scale = std::clamp(scale, 1, MAX_SCALE - 1);
    double ratio = PixelRatio(scale);
    if (m_down_ratio[scale] > 0.0)
      ratio = std::min(ratio, m_down_ratio[scale]);
    const double low = MedianCost(scale);
    const double high = MedianCost(scale + 1);
    if (low > 0.0 && high > 0.0)
      ratio = std::min(ratio, std::max(RATIO_MIN, high / low));
    return std::max(ratio, m_up_ratio[scale]);
  }

  // 0 with too few seconds.
  double MedianCost(int scale) const
  {
    if (!ValidScale(scale) || m_cost_n[scale] < COST_SECONDS)
      return 0.0;
    auto values = m_costs[scale];
    const int n = std::min(m_cost_n[scale], COST_KEEP);
    std::nth_element(values.begin(), values.begin() + n / 2, values.begin() + n);
    return values[n / 2];
  }

private:
  static constexpr double NEVER = 1e18;

  static bool ValidScale(int scale) { return scale >= 1 && scale < MAX_SCALE; }
  static std::uint32_t Pairs(const Sample& s) { return std::max(s.fresh, s.gpu_frames) + s.skipped; }

  static bool LostRefreshes(const Sample& s)
  {
    return s.every_refresh && s.refreshes < LOST_SHARE * 1000.0 * s.seconds / s.period_ms;
  }

  void AddToRun(int* run, const Sample& s)
  {
    if (*run == 0)
      m_run_sum = 0.0;
    ++*run;
    m_run_sum += Cost(s);
  }

  // A step was taken from `from`, whose cost was the run that led to it. The first judged seconds at the
  // new scale measure it.
  void StepDone(Step step, int from)
  {
    m_measure_from = from;
    m_measure_cost = m_run_sum / (step == Step::Down ? m_bad : m_good);
    m_measure_sum = 0.0;
    m_measure_n = 0;
    m_last_step = step;
    m_held = 0;
    m_bad = m_good = 0;
    m_settle = SETTLE;
  }

  // After a step up the ratio is as measured: the same scene a few seconds apart. The run before a step
  // down was the GPU behind, its cost with the wait for earlier work, so it can only say that the step is
  // cheaper than thought: a heavy scene that passes cannot keep the scale away.
  void Measure(const Sample& s)
  {
    if (m_measure_from == 0)
      return;
    if (std::abs(s.scale - m_measure_from) != 1)
    {
      m_measure_from = 0;
      return;
    }
    m_measure_sum += Cost(s);
    if (++m_measure_n < MEASURE_SECONDS)
      return;
    const double cost = m_measure_sum / m_measure_n;
    const bool down = s.scale < m_measure_from;
    const int low = std::min(s.scale, m_measure_from);
    const double cost_low = down ? cost : m_measure_cost;
    const double cost_high = down ? m_measure_cost : cost;
    m_measure_from = 0;
    if (cost_low <= 0.0)
      return;
    const double most = PixelRatio(low) * (down ? 1.0 : RATIO_OVER_PIXELS);
    const double ratio = std::clamp(cost_high / cost_low, RATIO_MIN, most);
    double& kept = down ? m_down_ratio[low] : m_up_ratio[low];
    if (down && ratio >= (kept > 0.0 ? kept : most))
      return;
    kept = kept > 0.0 ? 0.5 * (kept + ratio) : ratio;
  }

  int m_level = 0;
  int m_ceiling = MAX_UP;
  int m_bad = 0;
  int m_good = 0;
  int m_settle = 0;
  int m_hold = 0;
  // Judged seconds at this level.
  int m_held = 0;
  Step m_last_step = Step::None;
  double m_now = 0.0;
  double m_last_up = -NEVER;
  // The cost summed over the run of bad or good seconds.
  double m_run_sum = 0.0;

  // All per scale.
  std::array<int, MAX_SCALE> m_failed{};
  std::array<double, MAX_SCALE> m_blocked_until{};
  std::array<double, MAX_SCALE> m_up_ratio{};
  std::array<double, MAX_SCALE> m_down_ratio{};
  std::array<std::array<float, COST_KEEP>, MAX_SCALE> m_costs{};
  std::array<int, MAX_SCALE> m_cost_n{};

  // The scale stepped from, 0 with nothing to measure.
  int m_measure_from = 0;
  double m_measure_cost = 0.0;
  double m_measure_sum = 0.0;
  int m_measure_n = 0;
};

// What the screen can show: a scale with more than SCREEN_OVERSAMPLE times the display's pixels that the
// screen covers, on both axes, only costs GPU time.
constexpr double SCREEN_OVERSAMPLE = 2.5;
constexpr int PICTURE_WIDTH = 640;
constexpr int PICTURE_HEIGHT = 480;
struct ScreenCover
{
  // The screen's width and height over its distance.
  double tan_w = 0.0;
  double tan_h = 0.0;
  // The eye's field of view: tan(right) - tan(left), tan(up) - tan(down).
  double eye_tan_w = 0.0;
  double eye_tan_h = 0.0;
  // The runtime's recommended eye picture.
  double eye_px_w = 0.0;
  double eye_px_h = 0.0;
};

// The scale, unrounded. 0: not known.
inline double ScreenCoverage(const ScreenCover& c)
{
  if (!(c.tan_w > 0.0 && c.tan_h > 0.0 && c.eye_tan_w > 0.0 && c.eye_tan_h > 0.0 &&
        c.eye_px_w > 0.0 && c.eye_px_h > 0.0))
  {
    return 0.0;
  }
  const double across = c.eye_px_w * (c.tan_w / c.eye_tan_w) / PICTURE_WIDTH;
  const double up = c.eye_px_h * (c.tan_h / c.eye_tan_h) / PICTURE_HEIGHT;
  return SCREEN_OVERSAMPLE * std::max(across, up);
}

// The ceiling that gives, held: a screen carried or resized goes through many sizes. A new value is taken
// after SECONDS in a row, a higher one only MARGIN past its whole scale.
class CeilingHold
{
public:
  static constexpr int SECONDS = 3;
  static constexpr double MARGIN = 0.2;

  // Once a second. 0: no ceiling.
  int Next(double coverage)
  {
    int scale = coverage > 0.0 ? std::max(1, static_cast<int>(std::floor(coverage + 1e-9))) : 0;
    if (m_now > 0 && scale > m_now && coverage < scale + MARGIN)
      --scale;
    if (scale != m_candidate)
      m_seen = 0;
    m_candidate = scale;
    if (scale != m_now && ++m_seen >= SECONDS)
    {
      m_now = scale;
      m_seen = 0;
    }
    return m_now;
  }

private:
  int m_now = 0;
  int m_candidate = 0;
  int m_seen = 0;
};

// The steps above the floor to start a game at, for the scale remembered for it (0: none).
constexpr int StartUp(int floor, int remembered, int max_up)
{
  return std::clamp(remembered - floor, 0, std::max(0, max_up));
}

// The remembered scales, a line per game: "<game ID> <scale>". Other lines are left out.
using ScaleStore = std::map<std::string, int>;

inline bool IsStoredGame(const std::string& game)
{
  return game.size() <= 32 && game.find_first_not_of('0') != std::string::npos &&
         std::all_of(game.begin(), game.end(),
                     [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

inline std::string FormatStore(const ScaleStore& store)
{
  std::string out;
  for (const auto& [game, scale] : store)
  {
    if (IsStoredGame(game) && scale >= 1 && scale < Controller::MAX_SCALE)
      out += game + ' ' + std::to_string(scale) + '\n';
  }
  return out;
}

inline ScaleStore ParseStore(const std::string& text)
{
  ScaleStore store;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line))
  {
    std::istringstream words(line);
    std::string game;
    std::string rest;
    int scale = 0;
    if (words >> game >> scale && !(words >> rest) && IsStoredGame(game) && scale >= 1 &&
        scale < Controller::MAX_SCALE)
    {
      store[game] = scale;
    }
  }
  return store;
}
}  // namespace VR::DynamicResolution
