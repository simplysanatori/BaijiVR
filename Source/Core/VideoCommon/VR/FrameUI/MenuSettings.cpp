// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/FrameUI/MenuSettings.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include "Common/Config/Config.h"

#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"

#include "VideoCommon/VideoConfig.h"

namespace VR::UI
{
namespace
{
std::size_t Index(SettingId id)
{
  return static_cast<std::size_t>(id);
}

template <typename T>
float ToNumber(T value)
{
  return static_cast<float>(value);
}

template <typename T>
T FromNumber(float number)
{
  if constexpr (std::is_same_v<T, bool>)
    return number != 0.0f;
  else if constexpr (std::is_same_v<T, float>)
    return number;
  else
    return static_cast<T>(std::lround(number));
}

// ---- Values in words ----

std::string FormatMeters(float value)
{
  return fmt::format("{:.1f} m", value);
}

std::string FormatNumber(float value)
{
  return fmt::format("{:.0f}", value);
}

std::string FormatMinutes(float value)
{
  return fmt::format("{:.0f} min", value);
}

std::string FormatDepth(float value)
{
  if (value < 0.5f)
    return "Off";
  return value > 99.5f ? "Natural" : fmt::format("{:.0f}%", value);
}

// ---- The rows ----

SettingRow MakeRow(SettingId id, MenuSection section, RowKind kind, std::string label,
                   std::string tooltip)
{
  SettingRow row;
  row.id = id;
  row.section = section;
  row.kind = kind;
  row.label = std::move(label);
  row.tooltip = std::move(tooltip);
  return row;
}

SettingRow MakeHeader(MenuSection section, std::string label)
{
  return MakeRow(SettingId::None, section, RowKind::Header, std::move(label), {});
}

struct Range
{
  float minimum = 0.0f;
  float maximum = 1.0f;
  float step = 1.0f;
};

SettingRow MakeSlider(SettingId id, MenuSection section, std::string label, std::string tooltip,
                      Range range, std::string (*format)(float))
{
  SettingRow row = MakeRow(id, section, RowKind::Slider, std::move(label), std::move(tooltip));
  row.minimum = range.minimum;
  row.maximum = range.maximum;
  row.step = range.step;
  row.format = format;
  return row;
}

SettingRow MakeChoice(SettingId id, MenuSection section, RowKind kind, std::string label,
                      std::string tooltip, std::vector<SettingOption> options)
{
  SettingRow row = MakeRow(id, section, kind, std::move(label), std::move(tooltip));
  row.options = std::move(options);
  return row;
}

SettingRow With(SettingRow row, SettingStore store, RowDisplay display = RowDisplay::Any,
                Applies applies = Applies::Now)
{
  row.store = store;
  row.display = display;
  row.applies = applies;
  return row;
}

std::vector<SettingRow> MakeScreenRows()
{
  using enum SettingId;
  constexpr MenuSection SECTION = MenuSection::Screen;
  return {
      With(MakeChoice(Display, SECTION, RowKind::Segmented, "Display", "How the game is shown.",
                      {{"Stereoscopic 3D", ToNumber(OpenXRPresentationMode::StereoScreen)},
                       {"Immersive", ToNumber(OpenXRPresentationMode::Immersive)}}),
           SettingStore::VRFile),
      With(MakeSlider(Distance, SECTION, "Distance",
                      "How far away the screen is. In Immersive, the one for 2D parts.",
                      {Config::GFX_VR_SCREEN_DISTANCE_MIN, Config::GFX_VR_SCREEN_DISTANCE_MAX,
                       Config::GFX_VR_SCREEN_DISTANCE_STEP},
                      FormatMeters),
           SettingStore::VRFile),
      With(MakeSlider(Size, SECTION, "Size",
                      "How tall the screen is. In Immersive, the one for 2D parts.",
                      {Config::GFX_VR_SCREEN_SIZE_MIN, Config::GFX_VR_SCREEN_SIZE_MAX,
                       Config::GFX_VR_SCREEN_SIZE_STEP},
                      FormatMeters),
           SettingStore::VRFile),
      With(MakeChoice(AspectRatio, SECTION, RowKind::Segmented, "Aspect Ratio",
                      "The shape of the picture.",
                      {{"Auto", ToNumber(AspectMode::Auto)},
                       {"16:9", ToNumber(AspectMode::ForceWide)},
                       {"4:3", ToNumber(AspectMode::ForceStandard)}}),
           SettingStore::LocalGame, RowDisplay::Screen),
      With(MakeRow(LockScreen, SECTION, RowKind::Switch, "Lock Screen",
                   "Keeps the grips from moving the screen."),
           SettingStore::Global, RowDisplay::Screen),
  };
}

std::vector<SettingRow> MakeGeneralRows()
{
  using enum SettingId;
  constexpr MenuSection SECTION = MenuSection::General;
  SettingRow every = MakeSlider(AutosaveEvery, SECTION, "Autosave Every",
                                "Minutes of play between two autosaves.", {}, FormatMinutes);
  for (const int minutes : {1, 2, 5, 10, 15})
    every.options.push_back({fmt::format("{}", minutes), ToNumber(minutes)});
  return {
      MakeRow(PointerBeams, SECTION, RowKind::Switch, "Pointer Beams",
              "Shows where the controllers point during a game. The menu always shows them."),
      MakeRow(Messages, SECTION, RowKind::Switch, "Messages",
              "Shows short messages, for example Quick Saved."),
      With(MakeChoice(FramePacing, SECTION, RowKind::Segmented, "Frame Pacing",
                      "Every Refresh shows a picture at each refresh of the headset. Match Game "
                      "shows one for each frame of the game.",
                      {{"Every Refresh", 1.0f}, {"Match Game", 0.0f}}),
           SettingStore::VRFile),
      MakeHeader(SECTION, "Saves"),
      With(MakeRow(Autosave, SECTION, RowKind::Switch, "Autosave",
                   "Saves a state while you play and when you close the game."),
           SettingStore::LocalGame),
      With(std::move(every), SettingStore::LocalGame),
      With(MakeSlider(AutosaveKeep, SECTION, "Autosaves to Keep", "Older ones are deleted.",
                      {1.0f, 10.0f, 1.0f}, FormatNumber),
           SettingStore::LocalGame),
  };
}

std::vector<SettingRow> MakeGraphicsRows()
{
  using enum SettingId;
  constexpr MenuSection SECTION = MenuSection::Graphics;
  return {
      With(MakeSlider(ScreenDepth, SECTION, "3D Depth",
                      "How deep the picture is. Lower is easier on the eyes.",
                      {0.0f, 100.0f, 5.0f}, FormatDepth),
           SettingStore::VRFile, RowDisplay::Screen),
      With(MakeSlider(Convergence, SECTION, "Convergence",
                      "How far things come out of the screen. Lower is easier on the eyes.",
                      {0.0f, Config::GFX_STEREO_CONVERGENCE_MAXIMUM, 1.0f}, FormatNumber),
           SettingStore::LocalGame, RowDisplay::Screen),
      With(MakeChoice(InternalResolution, SECTION, RowKind::Dropdown, "Internal Resolution",
                      "How sharp the game is drawn. Higher needs more of the GPU.",
                      {{"Auto", 0.0f}, {"1x", 1.0f}, {"2x", 2.0f}, {"3x", 3.0f}, {"4x", 4.0f}}),
           SettingStore::LocalGame),
  };
}

// ---- The config behind them ----

struct Binding
{
  SettingId id = SettingId::None;
  std::function<float()> read;
  std::function<float()> read_base;
  std::function<void(float)> write;
};

template <typename T>
Binding Bind(SettingId id, const Config::Info<T>& info)
{
  return {id, [&info] { return ToNumber(Config::Get(info)); },
          [&info] { return ToNumber(Config::GetBase(info)); },
          [&info](float value) { Config::SetBaseOrCurrent(info, FromNumber<T>(value)); }};
}

const std::vector<Binding>& GetBindings()
{
  static const std::vector<Binding> bindings{
      Bind(SettingId::Display, Config::GFX_VR_PRESENTATION_MODE),
      Bind(SettingId::Distance, Config::GFX_VR_SCREEN_DISTANCE),
      Bind(SettingId::Size, Config::GFX_VR_SCREEN_SIZE),
      Bind(SettingId::AspectRatio, Config::GFX_ASPECT_RATIO),
      Bind(SettingId::LockScreen, Config::GFX_VR_LOCK_SCREEN),
      Bind(SettingId::PointerBeams, Config::GFX_VR_POINTER_BEAMS),
      Bind(SettingId::Messages, Config::GFX_VR_TOASTS),
      Bind(SettingId::FramePacing, Config::GFX_VR_EAGER_HEARTBEAT),
      Bind(SettingId::Autosave, Config::MAIN_AUTOSAVE_ENABLED),
      Bind(SettingId::AutosaveEvery, Config::MAIN_AUTOSAVE_MINUTES),
      Bind(SettingId::AutosaveKeep, Config::MAIN_AUTOSAVE_KEEP),
      Bind(SettingId::ScreenDepth, Config::GFX_VR_SCREEN_DEPTH),
      Bind(SettingId::Convergence, Config::GFX_STEREO_CONVERGENCE),
      Bind(SettingId::InternalResolution, Config::GFX_EFB_SCALE),
  };
  return bindings;
}
}  // namespace

const std::vector<SettingRow>& GetSettingRows()
{
  static const std::vector<SettingRow> rows = [] {
    std::vector<SettingRow> all;
    for (const auto& part : {MakeScreenRows(), MakeGeneralRows(), MakeGraphicsRows()})
      all.insert(all.end(), part.begin(), part.end());
    return all;
  }();
  return rows;
}

const SettingRow& GetSettingRow(SettingId id)
{
  return *std::ranges::find(GetSettingRows(), id, &SettingRow::id);
}

float SettingState::Get(SettingId id) const
{
  return (scope == SettingScope::Global ? values.global : values.game)[Index(id)];
}

void SettingState::Set(SettingId id, float value)
{
  const bool global = scope == SettingScope::Global ||
                      GetSettingRow(id).store == SettingStore::Global;
  if (global)
    values.global[Index(id)] = value;
  if (!global || !values.game_value[Index(id)])
    values.game[Index(id)] = value;
}

Lock GetLock(const SettingRow& row, const SettingState& state)
{
  if (row.display == RowDisplay::Screen && state.immersive)
    return Lock::ScreenOnly;
  if (row.display == RowDisplay::Immersive && !state.immersive)
    return Lock::ImmersiveOnly;
  if (row.id == SettingId::Display && !state.can_switch_mode)
    return Lock::DisplayFixed;
  if ((row.id == SettingId::AutosaveEvery || row.id == SettingId::AutosaveKeep) &&
      !state.IsOn(SettingId::Autosave))
  {
    return Lock::AutosaveOff;
  }
  if (row.turned_on_by != SettingId::None && !state.IsOn(row.turned_on_by))
    return Lock::SwitchedOff;
  return Lock::None;
}

std::string ExplainLock(Lock lock, const SettingRow& row)
{
  switch (lock)
  {
  case Lock::ScreenOnly:
    return "Only in Stereoscopic 3D.";
  case Lock::ImmersiveOnly:
    return "Only in Immersive.";
  case Lock::DisplayFixed:
    return "Choose a Display on the VR settings page first.";
  case Lock::AutosaveOff:
    return "Turn on Autosave to change it.";
  case Lock::SwitchedOff:
    return fmt::format("Turn on {} to change it.", row.label);
  case Lock::None:
    break;
  }
  return {};
}

std::vector<SettingOption> GetOptions(const SettingRow& row, const SettingState& state)
{
  return row.options;
}

int FindOption(const std::vector<SettingOption>& options, float value)
{
  const auto it = std::ranges::find(options, value, &SettingOption::value);
  return it != options.end() ? static_cast<int>(it - options.begin()) : -1;
}

float SliderFraction(const SettingRow& row, float value)
{
  if (!row.options.empty())
  {
    // The last one that is not over it.
    std::size_t at = 0;
    while (at + 1 < row.options.size() && row.options[at + 1].value <= value)
      ++at;
    return static_cast<float>(at) / static_cast<float>(row.options.size() - 1);
  }
  const float t = row.exponential ? std::log(std::max(value, row.minimum) / row.minimum) /
                                        std::log(row.maximum / row.minimum) :
                                    (value - row.minimum) / (row.maximum - row.minimum);
  return std::clamp(t, 0.0f, 1.0f);
}

float SliderValue(const SettingRow& row, float t)
{
  t = std::clamp(t, 0.0f, 1.0f);
  if (!row.options.empty())
  {
    const float at = std::round(t * static_cast<float>(row.options.size() - 1));
    return row.options[static_cast<std::size_t>(at)].value;
  }
  float value = row.minimum + t * (row.maximum - row.minimum);
  float step = row.step;
  if (row.exponential)
  {
    // Two digits are enough.
    value = row.minimum * std::pow(row.maximum / row.minimum, t);
    step = std::max(step, std::pow(10.0f, std::floor(std::log10(value)) - 1.0f));
  }
  return std::clamp(std::round(value / step) * step, row.minimum, row.maximum);
}

std::string FormatValue(const SettingRow& row, float value)
{
  if (row.format)
    return row.format(value);
  const int option = FindOption(row.options, value);
  if (option >= 0)
    return row.options[static_cast<std::size_t>(option)].label;
  // From the settings page or a config file.
  return row.id == SettingId::InternalResolution ? fmt::format("{:.0f}x", value) : "Other";
}

SettingValues ReadSettings()
{
  SettingValues values;
  for (const Binding& binding : GetBindings())
  {
    values.game[Index(binding.id)] = binding.read();
    values.global[Index(binding.id)] = binding.read_base();
  }
  values.aa_modes = g_backend_info.AAModes;
  return values;
}

void WriteSetting(SettingId id, float value)
{
  std::ranges::find(GetBindings(), id, &Binding::id)->write(value);
}
}  // namespace VR::UI
