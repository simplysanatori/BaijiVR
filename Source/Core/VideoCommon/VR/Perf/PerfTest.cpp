// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/Perf/PerfTest.h"

#ifdef ENABLE_VR

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <ctime>
#include <functional>
#include <mutex>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "VideoCommon/VR/FrameUI/VrUi.h"
#include "VideoCommon/VideoConfig.h"

namespace VR::Perf::Test
{
namespace
{
constexpr int START_AFTER_SECONDS = 15;

enum class Kind
{
  Bool,
  Int,
  ShaderMode,
};

struct KeySpec
{
  std::string_view name;
  Kind kind;
  bool needs_game_start;
};

constexpr KeySpec KEYS[] = {
    {"DynamicResolution", Kind::Bool, false},
    {"PinEmulationCores", Kind::Bool, true},
    {"PerformanceHint", Kind::Bool, true},
    {"AsyncEFBCopies", Kind::Bool, false},
    {"NonBlockingPerfQueries", Kind::Bool, false},
    {"SkipOverwrittenClears", Kind::Bool, false},
    {"InternalResolution", Kind::Int, false},
    {"ShaderCompilationMode", Kind::ShaderMode, false},
    {"EagerHeartbeat", Kind::Bool, false},
};

std::string Lower(std::string_view s)
{
  std::string out(s);
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

std::string_view Trim(std::string_view s)
{
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.remove_suffix(1);
  return s;
}

const KeySpec* FindKey(std::string_view name)
{
  const std::string lower = Lower(name);
  for (const KeySpec& key : KEYS)
  {
    if (Lower(key.name) == lower)
      return &key;
  }
  return nullptr;
}

bool ParseInt(std::string_view s, int* out)
{
  s = Trim(s);
  int value = 0;
  const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec != std::errc{} || ptr != s.data() + s.size())
    return false;
  *out = value;
  return true;
}

bool ParseBool(std::string_view s, bool* out)
{
  const std::string v = Lower(Trim(s));
  if (v == "1" || v == "true" || v == "on" || v == "yes")
    *out = true;
  else if (v == "0" || v == "false" || v == "off" || v == "no")
    *out = false;
  else
    return false;
  return true;
}

// Empty when fine, else why not.
std::string CheckValue(const KeySpec& key, std::string_view value)
{
  switch (key.kind)
  {
  case Kind::Bool:
  {
    bool b;
    return ParseBool(value, &b) ? "" : "needs true or false";
  }
  case Kind::Int:
  {
    int i;
    return ParseInt(value, &i) && i >= 0 && i <= 16 ? "" : "needs a number from 0 to 16";
  }
  case Kind::ShaderMode:
  {
    int i;
    return ParseInt(value, &i) && i >= 0 && i <= 3 ? "" : "needs 0, 1, 2 or 3";
  }
  }
  return "";
}

// ---- The run (host thread, and the state callback) ----

enum class Phase
{
  Off,      // no file, or the test is done
  Waiting,  // the game started; the first running seconds go by
  Running,
};

std::mutex s_mutex;
Phase s_phase = Phase::Off;
Plan s_plan;
std::vector<StepResult> s_results;  // by step index
int s_waited = 0;
int s_round = 0;
size_t s_order_pos = 0;
std::vector<size_t> s_order;
int s_in_step = 0;
std::vector<std::function<void()>> s_restore;  // put back what the step set

std::atomic<bool> s_requested{false};
std::once_flag s_once;
Common::EventHook s_state_hook;

std::string PlanPath()
{
  return File::GetUserPath(D_CONFIG_IDX) + "perf-test.txt";
}

template <typename T, typename V>
void SetKey(const Config::Info<T>& info, const V& value)
{
  if (Config::GetActiveLayerForConfig(info) == Config::LayerType::CurrentRun)
  {
    const T old = Config::Get(info);
    s_restore.push_back([&info, old] { Config::SetCurrent(info, old); });
  }
  else
  {
    s_restore.push_back([&info] { Config::DeleteKey(Config::LayerType::CurrentRun, info); });
  }
  Config::SetCurrent(info, static_cast<T>(value));
}

void ApplySetting(std::string_view key_name, std::string_view value)
{
  const KeySpec* key = FindKey(key_name);
  if (!key)
    return;
  bool b = false;
  int i = 0;
  ParseBool(value, &b);
  ParseInt(value, &i);
  const std::string_view n = key->name;
  if (n == "DynamicResolution")
    SetKey(Config::GFX_VR_DYNAMIC_RESOLUTION, b);
  else if (n == "AsyncEFBCopies")
    SetKey(Config::GFX_VR_ASYNC_EFB_COPIES, b);
  else if (n == "NonBlockingPerfQueries")
    SetKey(Config::GFX_VR_NON_BLOCKING_PERF_QUERIES, b);
  else if (n == "SkipOverwrittenClears")
    SetKey(Config::GFX_VR_SKIP_OVERWRITTEN_CLEARS, b);
  else if (n == "InternalResolution")
    SetKey(Config::GFX_EFB_SCALE, i);
  else if (n == "ShaderCompilationMode")
    SetKey(Config::GFX_SHADER_COMPILATION_MODE, static_cast<ShaderCompilationMode>(i));
  else if (n == "EagerHeartbeat")
    SetKey(Config::GFX_VR_EAGER_HEARTBEAT, b);
}

void RestoreAll()
{
  for (auto it = s_restore.rbegin(); it != s_restore.rend(); ++it)
    (*it)();
  s_restore.clear();
}

void EnterStep()
{
  RestoreAll();
  const Step& step = s_plan.steps[s_order[s_order_pos]];
  for (const auto& [key, value] : step.settings)
    ApplySetting(key, value);
  s_in_step = 0;
  INFO_LOG_FMT(VIDEO, "Performance test: round {}/{}, step '{}' ({}).", s_round + 1, s_plan.rounds,
               step.name, DescribeSwitches());
}

void Finish()
{
  RestoreAll();
  s_phase = Phase::Off;
  s_requested = false;

  const std::time_t now = std::time(nullptr);
  const std::string when = fmt::format("{:%Y-%m-%d %H:%M:%S}", fmt::localtime(now));
  const std::string path = File::GetUserPath(D_LOGS_IDX) +
                           fmt::format("perf-test-{:%Y%m%d-%H%M%S}.txt", fmt::localtime(now));
  File::CreateFullPath(path);
  File::WriteStringToFile(
      path, FormatReport(s_plan, s_results, SConfig::GetInstance().GetGameID(), when));

  const std::string done = File::GetUserPath(D_CONFIG_IDX) + "perf-test.done.txt";
  File::Delete(done);
  if (!File::Rename(PlanPath(), done))
    WARN_LOG_FMT(VIDEO, "Performance test: could not rename {}; it will run again.", PlanPath());
  INFO_LOG_FMT(VIDEO, "Performance test: done, numbers in {}.", path);
  UI::VrUi::PostToast("Performance Test Done", 8000);
}

// At a game start and at the first call: is there a file to run?
void Refresh()
{
  std::lock_guard lock(s_mutex);
  RestoreAll();
  s_phase = Phase::Off;
  s_requested = false;
  const std::string path = PlanPath();
  if (!File::Exists(path))
    return;
  std::string text;
  if (!File::ReadFileToString(path, text))
    return;
  s_plan = ParsePlan(text);
  for (const std::string& problem : s_plan.problems)
    WARN_LOG_FMT(VIDEO, "Performance test: {}", problem);
  if (s_plan.steps.empty())
  {
    WARN_LOG_FMT(VIDEO, "Performance test: {} has no usable steps.", path);
    return;
  }
  s_results.assign(s_plan.steps.size(), {});
  for (size_t i = 0; i < s_plan.steps.size(); ++i)
    s_results[i].name = s_plan.steps[i].name;
  s_waited = 0;
  s_round = 0;
  s_phase = Phase::Waiting;
  s_requested = true;
  INFO_LOG_FMT(VIDEO, "Performance test: {} steps, {} rounds, {} s each; starts {} s into the game.",
               s_plan.steps.size(), s_plan.rounds, s_plan.seconds, START_AFTER_SECONDS);
}

void OnStateChanged(Core::State state)
{
  if (state == Core::State::Starting)
  {
    Refresh();
  }
  else if (state == Core::State::Stopping || state == Core::State::Uninitialized)
  {
    std::lock_guard lock(s_mutex);
    if (s_phase != Phase::Off)
    {
      RestoreAll();
      INFO_LOG_FMT(VIDEO, "Performance test: the game stopped; the settings are put back.");
      s_phase = Phase::Off;
      s_requested = false;
    }
  }
}

void EnsureHooked()
{
  std::call_once(s_once, [] {
    s_state_hook = Core::AddOnStateChangedCallback(OnStateChanged);
    Refresh();  // the game that is running already
  });
}
}  // namespace

Plan ParsePlan(std::string_view text)
{
  Plan plan;
  int line_number = 0;
  while (!text.empty())
  {
    const size_t eol = text.find('\n');
    std::string_view line = text.substr(0, eol);
    text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
    ++line_number;
    line = Trim(line);
    if (line.empty() || line.front() == '#')
      continue;
    const auto problem = [&](std::string_view why) {
      plan.problems.push_back(fmt::format("line {}: {}", line_number, why));
    };

    const size_t colon = line.find(':');
    if (colon == std::string_view::npos)
    {
      const size_t eq = line.find('=');
      if (eq == std::string_view::npos)
      {
        problem("not a setting or a step");
        continue;
      }
      const std::string name = Lower(Trim(line.substr(0, eq)));
      int value = 0;
      if (!ParseInt(line.substr(eq + 1), &value))
      {
        problem("needs a whole number");
        continue;
      }
      if (name == "seconds" && value >= 1 && value <= 3600)
        plan.seconds = value;
      else if (name == "settle" && value >= 0 && value <= 3600)
        plan.settle = value;
      else if (name == "rounds" && value >= 1 && value <= 50)
        plan.rounds = value;
      else
        problem("unknown or out of range: seconds (1-3600), settle (0-3600), rounds (1-50)");
      continue;
    }

    Step step;
    step.name = std::string(Trim(line.substr(0, colon)));
    if (step.name.empty() || step.name.find('#') != std::string::npos ||
        step.name.find(',') != std::string::npos)
    {
      problem("a step needs a name without # or ,");
      continue;
    }
    if (std::ranges::any_of(plan.steps, [&](const Step& s) { return s.name == step.name; }))
    {
      problem("a step name is used twice");
      continue;
    }

    bool ok = true;
    std::string_view rest = line.substr(colon + 1);
    while (ok && !Trim(rest).empty())
    {
      const size_t semi = rest.find(';');
      const std::string_view item = Trim(rest.substr(0, semi));
      rest = semi == std::string_view::npos ? std::string_view{} : rest.substr(semi + 1);
      if (item.empty())
        continue;
      const size_t eq = item.find('=');
      if (eq == std::string_view::npos)
      {
        problem(fmt::format("'{}' is not Key=Value", item));
        ok = false;
        break;
      }
      const std::string_view key_name = Trim(item.substr(0, eq));
      const std::string_view value = Trim(item.substr(eq + 1));
      const KeySpec* key = FindKey(key_name);
      if (!key)
      {
        problem(fmt::format("unknown key '{}'", key_name));
        ok = false;
      }
      else if (key->needs_game_start)
      {
        problem(fmt::format("'{}' only applies at a game start; set it in the settings instead",
                            key->name));
        ok = false;
      }
      else if (const std::string why = CheckValue(*key, value); !why.empty())
      {
        problem(fmt::format("{} {}", key->name, why));
        ok = false;
      }
      else
      {
        step.settings.emplace_back(std::string(key->name), std::string(value));
      }
    }
    if (ok)
      plan.steps.push_back(std::move(step));
  }
  if (plan.settle >= plan.seconds)
  {
    plan.problems.push_back("settle is not below seconds; using no settling");
    plan.settle = 0;
  }
  return plan;
}

std::vector<size_t> RoundOrder(size_t step_count, int round)
{
  std::vector<size_t> order(step_count);
  for (size_t i = 0; i < step_count; ++i)
    order[i] = i;
  if (round % 2 != 0)
    std::ranges::reverse(order);
  return order;
}

Summary Summarize(std::vector<double> values)
{
  Summary s;
  s.n = values.size();
  if (values.empty())
    return s;
  std::ranges::sort(values);
  const auto percentile = [&](double p) {
    const double pos = p * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = std::min(lo + 1, values.size() - 1);
    return values[lo] + (values[hi] - values[lo]) * (pos - static_cast<double>(lo));
  };
  s.median = percentile(0.5);
  s.p5 = percentile(0.05);
  s.p95 = percentile(0.95);
  return s;
}

std::string FormatReport(const Plan& plan, const std::vector<StepResult>& results,
                         std::string_view game, std::string_view when)
{
  std::string out = fmt::format(
      "Performance test {} ({})\n{} s per step ({} s settling left out), {} rounds. Median [5th, "
      "95th percentile] over the counted seconds of all rounds.\n\n",
      when, game, plan.seconds, plan.settle, plan.rounds);
  const auto cell = [](const Summary& s) {
    return fmt::format("{:7.2f} [{:.2f}, {:.2f}]", s.median, s.p5, s.p95);
  };
  for (const StepResult& result : results)
  {
    std::vector<double> fps, speed, gpu, skipped, wait;
    for (const Sample& sample : result.samples)
    {
      fps.push_back(sample.fps);
      speed.push_back(sample.speed);
      if (sample.has_gpu)
        gpu.push_back(sample.gpu_ms);
      skipped.push_back(sample.skipped_pairs_per_s);
      wait.push_back(sample.gpu_wait_ms);
    }
    out += fmt::format("{} ({} seconds)\n", result.name, result.samples.size());
    out += fmt::format("  fps            {}\n", cell(Summarize(fps)));
    out += fmt::format("  speed          {}\n", cell(Summarize(speed)));
    out += gpu.empty() ? std::string("  gpu_ms         none\n") :
                         fmt::format("  gpu_ms         {}\n", cell(Summarize(gpu)));
    out += fmt::format("  skipped_pairs/s {}\n", cell(Summarize(skipped)));
    out += fmt::format("  gpu_wait_ms    {}\n\n", cell(Summarize(wait)));
  }
  return out;
}

bool IsRequested()
{
  EnsureHooked();
  return s_requested.load(std::memory_order_relaxed);
}

void OnSecond(const Second& second)
{
  if (!s_requested.load(std::memory_order_relaxed))
    return;
  std::lock_guard lock(s_mutex);
  if (s_phase == Phase::Off || !second.running)
    return;

  if (s_phase == Phase::Waiting)
  {
    if (++s_waited < START_AFTER_SECONDS)
      return;
    s_phase = Phase::Running;
    s_round = 0;
    s_order = RoundOrder(s_plan.steps.size(), 0);
    s_order_pos = 0;
    const int total = static_cast<int>(s_plan.steps.size()) * s_plan.rounds * s_plan.seconds;
    UI::VrUi::PostToast(fmt::format("Performance Test: {} min", (total + 59) / 60), 8000);
    EnterStep();
    return;
  }

  if (s_in_step >= s_plan.settle)
  {
    Sample sample;
    sample.fps = second.fps;
    sample.speed = second.speed;
    sample.has_gpu = second.gpu_frames > 0;
    sample.gpu_ms = second.gpu_ms;
    sample.skipped_pairs_per_s =
        second.seconds > 0.0 ? second.skipped_pairs / second.seconds : 0.0;
    sample.gpu_wait_ms = second.gpu_wait_ms;
    s_results[s_order[s_order_pos]].samples.push_back(sample);
  }
  if (++s_in_step < s_plan.seconds)
    return;

  if (++s_order_pos >= s_order.size())
  {
    if (++s_round >= s_plan.rounds)
    {
      Finish();
      return;
    }
    s_order = RoundOrder(s_plan.steps.size(), s_round);
    s_order_pos = 0;
  }
  EnterStep();
}

std::string GetStepName()
{
  if (!s_requested.load(std::memory_order_relaxed))
    return {};
  std::lock_guard lock(s_mutex);
  if (s_phase != Phase::Running)
    return {};
  return fmt::format("{}#{}{}", s_plan.steps[s_order[s_order_pos]].name, s_round + 1,
                     s_in_step < s_plan.settle ? "*" : "");
}
}  // namespace VR::Perf::Test

#endif  // ENABLE_VR
