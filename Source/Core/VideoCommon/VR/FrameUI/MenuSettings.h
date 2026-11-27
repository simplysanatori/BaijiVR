// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"

#include "Core/GameSettingScope.h"

namespace Config
{
struct Location;
}

// The settings in the VR menu as data: what rows there are, where they are, what they say and the
// config behind them. What a setting is called here is what it is called on the settings page
// (VRPane). No OpenXR here (unit tested).
namespace VR::UI
{
enum class SettingId : int
{
  None = -1,

  // Screen
  Display,
  Distance,
  Size,
  ScreenCurve,
  AspectRatio,
  LockScreen,
  VirtualScreen,

  // General
  WindowScale,
  Passthrough,
  PointerBeams,
  Messages,
  FramePacing,
  RefreshRate,
  Autosave,
  AutosaveEvery,
  AutosaveKeep,

  // Graphics
  ScreenDepth,
  ScreenWorldScale,
  InternalResolution,
  DynamicResolution,
  TextureFiltering,
  AnisotropicFiltering,
  AntiAliasing,
  ShaderCompilation,
  WidescreenHack,
  SkipEFBAccess,
  EFBCopiesToTexture,
  XFBCopiesToTexture,
  FoveatedRendering,
  SmartFoveatedEFB,
  Gamma,
  NonBlockingEFBCopies,
  NonBlockingPerfQueries,
  SkipOverwrittenClears,
  VulkanRecordingWorker,
  PinEmulationThreads,
  PerformanceHint,
  PerformanceLog,

  // Immersive
  ImmersiveMode,  // Display, as a switch
  WorldScale,
  Strength3D,
  LeanBackOn,
  LeanBack,
  CameraForwardOn,
  CameraForward,
  CameraHeightOn,
  CameraHeight,
  HeadTracking,
  HudDepth,
  RemoveBars,
  HeadCPUCull,
  WiimoteMotionPitch,
  PerEyeScreenCopies,
  SkipBackgroundFill,

  // Stats
  ShowStats,
  StatsPosition,

  Count,
};

constexpr std::size_t NUM_SETTINGS = static_cast<std::size_t>(SettingId::Count);

// Where the rows are: the Position tab, a section of the Settings tab, or the Stats tab.
enum class MenuSection
{
  Screen,
  General,
  Graphics,
  Immersive,
  Stats,
  Count,
};

constexpr std::size_t NUM_SECTIONS = static_cast<std::size_t>(MenuSection::Count);

enum class RowKind
{
  Header,
  Switch,
  Segmented,
  Dropdown,
  Slider,
};

enum class SettingScope
{
  Global,
  PerGame,
};

using SettingStore = GameSettingScope::Store;

enum class Applies
{
  Now,
  NextGame,
  NextStart,  // of Baiji VR
};

enum class RowDisplay
{
  Any,
  Screen,  // Stereoscopic 3D
  Immersive,
};

struct SettingOption
{
  std::string label;
  float value = 0.0f;
};

struct SettingRow
{
  SettingId id = SettingId::None;
  MenuSection section = MenuSection::General;
  RowKind kind = RowKind::Switch;
  std::string label;
  std::string tooltip;
  SettingStore store = SettingStore::Global;
  Applies applies = Applies::Now;
  RowDisplay display = RowDisplay::Any;
  // Segmented, Dropdown, and a Slider that stops only at these.
  std::vector<SettingOption> options;
  // Slider.
  float minimum = 0.0f;
  float maximum = 1.0f;
  float step = 1.0f;
  bool exponential = false;
  std::string (*format)(float) = nullptr;
  // Slider: the switch that turns it on, at the end of its row and not in a row of its own.
  SettingId turned_on_by = SettingId::None;
  bool in_next_row = false;
};

// Every setting as a number: a switch is 0 or 1, a choice is its option's value.
using SettingNumbers = std::array<float, NUM_SETTINGS>;

struct SettingValues
{
  SettingNumbers global{};
  SettingNumbers game{};  // what the running game has
  // The settings that the game has a value of its own for, so that the global one is not used, and
  // those of them where that is the player's.
  std::bitset<NUM_SETTINGS> game_value;
  std::bitset<NUM_SETTINGS> own_value;
  // Anti-aliasing the GPU can do.
  std::vector<u32> aa_modes;

  bool operator==(const SettingValues&) const = default;
};

struct SettingState
{
  SettingValues values;
  SettingScope scope = SettingScope::PerGame;
  bool has_game = false;
  bool immersive = false;
  bool can_switch_mode = true;
  bool can_curve = true;  // the VR runtime

  // The running game's, or the global one, as chosen.
  float Get(SettingId id) const;
  bool IsOn(SettingId id) const { return Get(id) != 0.0f; }
  // What is shown until the config has it.
  void Set(SettingId id, float value);

  bool operator==(const SettingState&) const = default;
};

// Why a row cannot be changed.
enum class Lock
{
  None,
  GameValue,
  ScreenOnly,
  ImmersiveOnly,
  DisplayFixed,
  AutosaveOff,
  SwitchedOff,
  CannotCurve,
};

const std::vector<SettingRow>& GetSettingRows();
const SettingRow& GetSettingRow(SettingId id);

// How many settings of a section the player gave a value for the running game only.
int CountOwnValues(MenuSection section, const SettingState& state);
std::string DescribeOwnValues(int count);

Lock GetLock(const SettingRow& row, const SettingState& state);
std::string ExplainLock(Lock lock, const SettingRow& row);

// The screen's curve with the grip and the D-Pad: one step more or less.
float StepScreenCurve(float degrees, int steps);

// The options there are to choose from.
std::vector<SettingOption> GetOptions(const SettingRow& row, const SettingState& state);
// The option for a value, or -1.
int FindOption(const std::vector<SettingOption>& options, float value);

// 0..1 along a slider, and back (at the slider's steps).
float SliderFraction(const SettingRow& row, float value);
float SliderValue(const SettingRow& row, float t);
std::string FormatValue(const SettingRow& row, float value);

// The config key of a row: the one its control on the Qt settings pages has
// (Tools/check-vr-menu.py compares the two).
const Config::Location& GetSettingLocation(SettingId id);

// The config. Host thread.
SettingValues ReadSettings();
// Without `save` (a slider that is being moved), a game's value is for now only.
void WriteSetting(SettingId id, float value, SettingScope scope, bool save);
// The player's values for this game go.
void UseGlobalValues(MenuSection section);
}  // namespace VR::UI
