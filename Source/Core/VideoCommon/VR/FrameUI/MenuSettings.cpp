// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/FrameUI/MenuSettings.h"

#include <algorithm>
#include <array>
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

std::string FormatPercent(float value)
{
  return fmt::format("{:.0f}%", value);
}

std::string FormatDegrees(float value)
{
  return fmt::format("{:.1f}\u00b0", value);
}

std::string FormatWorldScale(float value)
{
  return fmt::format("{:.4g}", value);
}

std::string FormatCurve(float value)
{
  return value < 0.5f ? "Off" : fmt::format("{:.0f}\u00b0", value);
}

std::string FormatHudDepth(float value)
{
  return value <= 0.001f ? "Off" : fmt::format("{:.2f} m", value);
}

std::string FormatGamma(float value)
{
  return value <= 1.01f ? "Off" : fmt::format("{:.1f}", value);
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

// As on the settings page: on Linux, Immersive's picture is always opaque.
#ifdef _WIN32
constexpr RowDisplay PASSTHROUGH_DISPLAY = RowDisplay::Any;
#else
constexpr RowDisplay PASSTHROUGH_DISPLAY = RowDisplay::Screen;
#endif

// Under the Saves header, as Azahar for Frame's.
constexpr const char* AUTOSAVE_EVERY = "Every";
constexpr const char* AUTOSAVE_KEEP = "Keep";

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

constexpr std::array SCREEN_CURVES{0, 15, 30, 45, 60, 75, 90};

std::vector<SettingRow> MakeScreenRows()
{
  using enum SettingId;
  constexpr MenuSection SECTION = MenuSection::Screen;
  SettingRow curve = MakeSlider(ScreenCurve, SECTION, "Screen Curve",
                                "Bends the screen around you.", {}, FormatCurve);
  for (const int degrees : SCREEN_CURVES)
    curve.options.push_back({fmt::format("{}", degrees), ToNumber(degrees)});
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
      With(std::move(curve), SettingStore::VRFile, RowDisplay::Screen),
      With(MakeChoice(AspectRatio, SECTION, RowKind::Segmented, "Aspect Ratio",
                      "The shape of the picture.",
                      {{"Auto", ToNumber(AspectMode::Auto)},
                       {"16:9", ToNumber(AspectMode::ForceWide)},
                       {"4:3", ToNumber(AspectMode::ForceStandard)}}),
           SettingStore::LocalGame, RowDisplay::Screen),
      With(MakeRow(LockScreen, SECTION, RowKind::Switch, "Lock Screen",
                   "Keeps the grips from moving the screen."),
           SettingStore::Global, RowDisplay::Screen),
      With(MakeRow(VirtualScreen, SECTION, RowKind::Switch, "2D Parts on a Screen",
                   "Puts menus, videos and the HUD on a screen in front of you."),
           SettingStore::VRFile, RowDisplay::Immersive),
  };
}

std::vector<SettingRow> MakeGeneralRows()
{
  using enum SettingId;
  constexpr MenuSection SECTION = MenuSection::General;
  SettingRow every = MakeSlider(AutosaveEvery, SECTION, AUTOSAVE_EVERY,
                                "Minutes of play between two autosaves.", {}, FormatMinutes);
  for (const int minutes : {1, 2, 5, 10, 15})
    every.options.push_back({fmt::format("{}", minutes), ToNumber(minutes)});
  return {
      With(MakeRow(Passthrough, SECTION, RowKind::Switch, "Passthrough",
                   "Shows your room around the game, if the headset can."),
           SettingStore::VRFile, PASSTHROUGH_DISPLAY),
      MakeRow(PointerBeams, SECTION, RowKind::Switch, "Pointer Beams",
              "Shows where the controllers point during a game. The menu always shows them."),
      MakeRow(Messages, SECTION, RowKind::Switch, "Messages",
              "Shows short messages, for example Quick Saved."),
      With(MakeChoice(FramePacing, SECTION, RowKind::Segmented, "Frame Pacing",
                      "Every Refresh shows a picture at each refresh of the headset. Match Game "
                      "shows one for each frame of the game.",
                      {{"Every Refresh", 1.0f}, {"Match Game", 0.0f}}),
           SettingStore::VRFile),
      With(MakeChoice(RefreshRate, SECTION, RowKind::Dropdown, "Refresh Rate",
                      "The headset's refresh rate. Auto leaves it as it is. A multiple of the "
                      "game's frame rate is smoothest.",
                      {{"Auto", ToNumber(Config::GFX_VR_REQUESTED_REFRESH_RATE_AUTO)},
                       {"60 Hz", ToNumber(Config::GFX_VR_REQUESTED_REFRESH_RATE_60)},
                       {"72 Hz", ToNumber(Config::GFX_VR_REQUESTED_REFRESH_RATE_72)},
                       {"90 Hz", ToNumber(Config::GFX_VR_REQUESTED_REFRESH_RATE_90)},
                       {"120 Hz", ToNumber(Config::GFX_VR_REQUESTED_REFRESH_RATE_120)}}),
           SettingStore::VRFile, RowDisplay::Any, Applies::NextGame),
      MakeHeader(SECTION, "Saves"),
      With(MakeRow(Autosave, SECTION, RowKind::Switch, "Autosave",
                   "Saves a state while you play and when you close the game."),
           SettingStore::LocalGame),
      With(std::move(every), SettingStore::LocalGame),
      With(MakeSlider(AutosaveKeep, SECTION, AUTOSAVE_KEEP, "Older ones are deleted.",
                      {1.0f, 10.0f, 1.0f}, FormatNumber),
           SettingStore::LocalGame),
  };
}

std::vector<SettingRow> MakeGraphicsRows()
{
  using enum SettingId;
  constexpr MenuSection SECTION = MenuSection::Graphics;
  return {
      MakeHeader(SECTION, "Stereoscopic 3D"),
      With(MakeSlider(ScreenDepth, SECTION, "3D Depth",
                      "How deep the picture is. Lower is easier on the eyes.",
                      {0.0f, 100.0f, 5.0f}, FormatDepth),
           SettingStore::VRFile, RowDisplay::Screen),
      With(MakeSlider(ScreenWorldScale, SECTION, "World Scale",
                      "Higher makes the world smaller, lower makes it bigger, as Immersive's "
                      "World Scale.",
                      {0.0f, Config::GFX_STEREO_CONVERGENCE_MAXIMUM, 1.0f}, FormatNumber),
           SettingStore::LocalGame, RowDisplay::Screen),
      MakeHeader(SECTION, "Picture"),
      With(MakeChoice(InternalResolution, SECTION, RowKind::Dropdown, "Internal Resolution",
                      "How sharp the game is drawn. Higher needs more of the GPU.",
                      {{"Auto", 0.0f}, {"1x", 1.0f}, {"2x", 2.0f}, {"3x", 3.0f}, {"4x", 4.0f}}),
           SettingStore::LocalGame),
      With(MakeRow(DynamicResolution, SECTION, RowKind::Switch, "Dynamic Resolution",
                   "Stereoscopic 3D: up to two steps over Internal Resolution while the GPU has "
                   "room. Immersive: steps down while the GPU is the limit."),
           SettingStore::VRFile),
      With(MakeChoice(TextureFiltering, SECTION, RowKind::Dropdown, "Texture Filtering",
                      "Default is the game's own. Nearest shows pixels, Linear smooths.",
                      {{"Default", ToNumber(TextureFilteringMode::Default)},
                       {"Nearest", ToNumber(TextureFilteringMode::Nearest)},
                       {"Linear", ToNumber(TextureFilteringMode::Linear)}}),
           SettingStore::LocalGame),
      With(MakeChoice(AnisotropicFiltering, SECTION, RowKind::Dropdown, "Anisotropic Filtering",
                      "Sharpens textures seen at a slant.",
                      {{"Default", ToNumber(AnisotropicFilteringMode::Default)},
                       {"1x", ToNumber(AnisotropicFilteringMode::Force1x)},
                       {"2x", ToNumber(AnisotropicFilteringMode::Force2x)},
                       {"4x", ToNumber(AnisotropicFilteringMode::Force4x)},
                       {"8x", ToNumber(AnisotropicFilteringMode::Force8x)},
                       {"16x", ToNumber(AnisotropicFilteringMode::Force16x)}}),
           SettingStore::LocalGame),
      With(MakeChoice(AntiAliasing, SECTION, RowKind::Dropdown, "Anti-Aliasing",
                      "Smooths edges. Needs more of the GPU.",
                      {{"None", 1.0f}, {"2x", 2.0f}, {"4x", 4.0f}, {"8x", 8.0f}}),
           SettingStore::LocalGame),
      With(MakeChoice(ShaderCompilation, SECTION, RowKind::Dropdown, "Shader Compilation",
                      "Ubershaders stutter less. Exclusive and Hybrid need more of the GPU.",
                      {{"Specialized", ToNumber(ShaderCompilationMode::Synchronous)},
                       {"Exclusive Ubershaders",
                        ToNumber(ShaderCompilationMode::SynchronousUberShaders)},
                       {"Hybrid Ubershaders",
                        ToNumber(ShaderCompilationMode::AsynchronousUberShaders)},
                       {"Skip Drawing",
                        ToNumber(ShaderCompilationMode::AsynchronousSkipRendering)}}),
           SettingStore::LocalGame),
      With(MakeRow(WidescreenHack, SECTION, RowKind::Switch, "Widescreen Hack",
                   "Shows more at the sides in games without widescreen. Objects can be missing "
                   "there."),
           SettingStore::LocalGame),
      With(MakeRow(SkipEFBAccess, SECTION, RowKind::Switch, "Skip EFB Access from CPU",
                   "Faster. Some effects and game logic need it off."),
           SettingStore::LocalGame),
      With(MakeRow(EFBCopiesToTexture, SECTION, RowKind::Switch, "Store EFB Copies to Texture Only",
                   "Faster. Some effects need it off."),
           SettingStore::LocalGame),
      With(MakeRow(XFBCopiesToTexture, SECTION, RowKind::Switch, "Store XFB Copies to Texture Only",
                   "Faster. Some videos and loading screens need it off."),
           SettingStore::LocalGame),
      With(MakeChoice(FoveatedRendering, SECTION, RowKind::Dropdown, "Foveated Rendering",
                      "Less detail at the edges of the view, for speed. Has to be off to change "
                      "Display in the headset.",
                      {{"Off", 0.0f}, {"Low", 1.0f}, {"Medium", 2.0f}, {"High", 3.0f}}),
           SettingStore::VRFile, RowDisplay::Immersive, Applies::NextGame),
      With(MakeSlider(Gamma, SECTION, "Gamma", "Darkens a picture that is too bright.",
                      {Config::GFX_VR_GAMMA_MIN, Config::GFX_VR_GAMMA_MAX,
                       Config::GFX_VR_GAMMA_STEP},
                      FormatGamma),
           SettingStore::VRFile),
      MakeHeader(SECTION, "Performance"),
      With(MakeRow(NonBlockingEFBCopies, SECTION, RowKind::Switch, "Non-Blocking EFB Copies",
                   "Does not wait for the GPU when the game copies the picture to memory."),
           SettingStore::VRFile),
      With(MakeRow(NonBlockingPerfQueries, SECTION, RowKind::Switch, "Non-Blocking Perf Queries",
                   "Does not wait for the GPU to answer the game's questions about what was "
                   "drawn."),
           SettingStore::VRFile),
      With(MakeRow(SkipOverwrittenClears, SECTION, RowKind::Switch, "Skip Overwritten Clears",
                   "Leaves out clears of the picture that are drawn over anyway."),
           SettingStore::VRFile, RowDisplay::Immersive),
      With(MakeRow(PinEmulationThreads, SECTION, RowKind::Switch, "Pin Emulation Threads",
                   "Keeps the main emulation threads on the fastest CPU cores."),
           SettingStore::Global, RowDisplay::Any, Applies::NextGame),
      With(MakeRow(PerformanceHint, SECTION, RowKind::Switch, "Performance Hint",
                   "Asks the VR runtime for high CPU and GPU speed, if it offers that."),
           SettingStore::Global, RowDisplay::Any, Applies::NextGame),
      MakeRow(PerformanceLog, SECTION, RowKind::Switch, "Performance Log",
              "Writes a line a second to perf.csv in the Logs folder."),
  };
}

// A slider with its switch in front of it in the table and at the end of its row in the menu.
void AddOptionalSlider(std::vector<SettingRow>* rows, SettingId on, SettingRow slider,
                       std::string tooltip)
{
  SettingRow enable =
      MakeRow(on, slider.section, RowKind::Switch, slider.label, std::move(tooltip));
  enable.store = slider.store;
  enable.display = slider.display;
  enable.in_next_row = true;
  slider.turned_on_by = on;
  rows->push_back(std::move(enable));
  rows->push_back(std::move(slider));
}

std::vector<SettingRow> MakeImmersiveRows()
{
  using enum SettingId;
  constexpr MenuSection SECTION = MenuSection::Immersive;
  constexpr RowDisplay DISPLAY = RowDisplay::Immersive;
  std::vector<SettingRow> rows;
  SettingRow world = With(
      MakeSlider(WorldScale, SECTION, "World Scale",
                 "Game units per meter. Higher makes the world smaller, lower makes it bigger.",
                 {Config::GFX_VR_UNITS_PER_METER_MIN, Config::GFX_VR_UNITS_PER_METER_MAX,
                  Config::GFX_VR_UNITS_PER_METER_STEP},
                 FormatWorldScale),
      SettingStore::VRFile, DISPLAY);
  world.exponential = true;
  rows.push_back(std::move(world));
  rows.push_back(
      With(MakeSlider(Strength3D, SECTION, "3D Strength",
                      "How deep the world looks. 100% is natural. Lower is easier on the eyes.",
                      {Config::GFX_VR_STEREO_SEPARATION_MIN, Config::GFX_VR_STEREO_SEPARATION_MAX,
                       Config::GFX_VR_STEREO_SEPARATION_STEP},
                      FormatPercent),
           SettingStore::VRFile, DISPLAY));
  AddOptionalSlider(
      &rows, LeanBackOn,
      With(MakeSlider(LeanBack, SECTION, "Lean Back", "Above 0 leans back, below 0 leans forward.",
                      {Config::GFX_VR_LEAN_BACK_ANGLE_MIN, Config::GFX_VR_LEAN_BACK_ANGLE_MAX,
                       Config::GFX_VR_LEAN_BACK_ANGLE_STEP},
                      FormatDegrees),
           SettingStore::VRFile, DISPLAY),
      "Tilts the camera, for games with a tilted view.");
  AddOptionalSlider(
      &rows, CameraForwardOn,
      With(MakeSlider(CameraForward, SECTION, "Camera Forward",
                      "Above 0 is forward, below 0 is back.",
                      {Config::GFX_VR_CAMERA_FORWARD_MIN, Config::GFX_VR_CAMERA_FORWARD_MAX,
                       Config::GFX_VR_CAMERA_FORWARD_STEP},
                      FormatMeters),
           SettingStore::VRFile, DISPLAY),
      "Moves the camera forward or back.");
  AddOptionalSlider(
      &rows, CameraHeightOn,
      With(MakeSlider(CameraHeight, SECTION, "Camera Height", "Above 0 is up, below 0 is down.",
                      {Config::GFX_VR_CAMERA_HEIGHT_MIN, Config::GFX_VR_CAMERA_HEIGHT_MAX,
                       Config::GFX_VR_CAMERA_HEIGHT_STEP},
                      FormatMeters),
           SettingStore::VRFile, DISPLAY),
      "Moves the camera up or down.");
  rows.push_back(With(MakeChoice(HeadTracking, SECTION, RowKind::Dropdown, "Head Tracking",
                                 "Which head movements move the camera.",
                                 {{"Turn and Move", ToNumber(OpenXRTrackingMode::Full6DoF)},
                                  {"Turn Only", ToNumber(OpenXRTrackingMode::Rotation3DoF)},
                                  {"Off", ToNumber(OpenXRTrackingMode::None)}}),
                      SettingStore::VRFile, DISPLAY));
  rows.push_back(With(
      MakeSlider(HudDepth, SECTION, "HUD Depth",
                 "Spreads the layers of the HUD apart in depth. Flat menus and videos stay flat.",
                 {Config::GFX_VR_HUD_THICKNESS_MIN, Config::GFX_VR_HUD_THICKNESS_MAX,
                  Config::GFX_VR_HUD_THICKNESS_STEP},
                 FormatHudDepth),
      SettingStore::VRFile, DISPLAY));
  rows.push_back(With(MakeRow(RemoveBars, SECTION, RowKind::Switch, "Remove Cinematic Bars",
                              "Removes the black bars at the top and bottom."),
                      SettingStore::VRFile, DISPLAY));
  return rows;
}

// ---- The config behind them ----

struct Binding
{
  SettingId id = SettingId::None;
  const Config::Location* location = nullptr;
  SettingStore store = SettingStore::Global;
  std::function<float()> read;
  std::function<float()> read_base;
  std::function<void(float, SettingScope, bool)> write;
  std::function<void()> use_global;
};

template <typename T>
Binding Bind(SettingId id, const Config::Info<T>& info)
{
  const SettingStore store = GetSettingRow(id).store;
  return {id,
          &info.GetLocation(),
          store,
          [&info] { return ToNumber(Config::Get(info)); },
          [&info] { return ToNumber(Config::GetBase(info)); },
          [&info, store](float number, SettingScope scope, bool save) {
            const T value = FromNumber<T>(number);
            if (store == SettingStore::Global || scope == SettingScope::Global)
              GameSettingScope::SetGlobal(info, value, store);
            else
              GameSettingScope::SetForGame(info, value, store, save);
          },
          [&info, store] { GameSettingScope::UseGlobal(info, store); }};
}

// On where the setting is off.
Binding BindInverted(SettingId id, const Config::Info<bool>& info)
{
  const auto invert = [](float value) { return value != 0.0f ? 0.0f : 1.0f; };
  const Binding plain = Bind(id, info);
  Binding binding = plain;
  binding.read = [plain, invert] { return invert(plain.read()); };
  binding.read_base = [plain, invert] { return invert(plain.read_base()); };
  binding.write = [plain, invert](float value, SettingScope scope, bool save) {
    plain.write(invert(value), scope, save);
  };
  return binding;
}

// The display that is shown stays until it is switched (QueueSwitchMode).
Binding BindDisplay()
{
  Binding binding = Bind(SettingId::Display, Config::GFX_VR_PRESENTATION_MODE);
  binding.use_global = [location = binding.location] {
    if (GameSettingScope::IsGameRunning() &&
        GameSettingScope::HasOwnValue(*location, SettingStore::VRFile))
    {
      GameSettingScope::RemoveFromVRFile(*location);
    }
  };
  return binding;
}

const std::vector<Binding>& GetBindings()
{
  static const std::vector<Binding> bindings{
      BindDisplay(),
      Bind(SettingId::Distance, Config::GFX_VR_SCREEN_DISTANCE),
      Bind(SettingId::Size, Config::GFX_VR_SCREEN_SIZE),
      Bind(SettingId::ScreenCurve, Config::GFX_VR_SCREEN_CURVE),
      Bind(SettingId::AspectRatio, Config::GFX_ASPECT_RATIO),
      Bind(SettingId::LockScreen, Config::GFX_VR_LOCK_SCREEN),
      Bind(SettingId::VirtualScreen, Config::GFX_VR_VIRTUAL_SCREEN),
      Bind(SettingId::Passthrough, Config::GFX_VR_PASSTHROUGH),
      Bind(SettingId::PointerBeams, Config::GFX_VR_POINTER_BEAMS),
      Bind(SettingId::Messages, Config::GFX_VR_TOASTS),
      Bind(SettingId::FramePacing, Config::GFX_VR_EAGER_HEARTBEAT),
      Bind(SettingId::RefreshRate, Config::GFX_VR_REQUESTED_REFRESH_RATE),
      Bind(SettingId::Autosave, Config::MAIN_AUTOSAVE_ENABLED),
      Bind(SettingId::AutosaveEvery, Config::MAIN_AUTOSAVE_MINUTES),
      Bind(SettingId::AutosaveKeep, Config::MAIN_AUTOSAVE_KEEP),
      Bind(SettingId::ScreenDepth, Config::GFX_VR_SCREEN_DEPTH),
      Bind(SettingId::ScreenWorldScale, Config::GFX_STEREO_CONVERGENCE),
      Bind(SettingId::InternalResolution, Config::GFX_EFB_SCALE),
      Bind(SettingId::DynamicResolution, Config::GFX_VR_DYNAMIC_RESOLUTION),
      Bind(SettingId::TextureFiltering, Config::GFX_ENHANCE_FORCE_TEXTURE_FILTERING),
      Bind(SettingId::AnisotropicFiltering, Config::GFX_ENHANCE_MAX_ANISOTROPY),
      Bind(SettingId::AntiAliasing, Config::GFX_MSAA),
      Bind(SettingId::ShaderCompilation, Config::GFX_SHADER_COMPILATION_MODE),
      Bind(SettingId::WidescreenHack, Config::GFX_WIDESCREEN_HACK),
      BindInverted(SettingId::SkipEFBAccess, Config::GFX_HACK_EFB_ACCESS_ENABLE),
      Bind(SettingId::EFBCopiesToTexture, Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM),
      Bind(SettingId::XFBCopiesToTexture, Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM),
      Bind(SettingId::FoveatedRendering, Config::GFX_VR_FOVEATION_LEVEL),
      Bind(SettingId::Gamma, Config::GFX_VR_GAMMA),
      Bind(SettingId::NonBlockingEFBCopies, Config::GFX_VR_ASYNC_EFB_COPIES),
      Bind(SettingId::NonBlockingPerfQueries, Config::GFX_VR_NON_BLOCKING_PERF_QUERIES),
      Bind(SettingId::SkipOverwrittenClears, Config::GFX_VR_SKIP_OVERWRITTEN_CLEARS),
      Bind(SettingId::PinEmulationThreads, Config::GFX_VR_PIN_EMULATION_CORES),
      Bind(SettingId::PerformanceHint, Config::GFX_VR_PERFORMANCE_HINT),
      Bind(SettingId::PerformanceLog, Config::GFX_VR_PERF_LOG),
      Bind(SettingId::WorldScale, Config::GFX_VR_UNITS_PER_METER),
      Bind(SettingId::Strength3D, Config::GFX_VR_STEREO_SEPARATION),
      Bind(SettingId::LeanBackOn, Config::GFX_VR_ENABLE_LEAN_BACK_ANGLE),
      Bind(SettingId::LeanBack, Config::GFX_VR_LEAN_BACK_ANGLE),
      Bind(SettingId::CameraForwardOn, Config::GFX_VR_ENABLE_CAMERA_FORWARD),
      Bind(SettingId::CameraForward, Config::GFX_VR_CAMERA_FORWARD),
      Bind(SettingId::CameraHeightOn, Config::GFX_VR_ENABLE_CAMERA_HEIGHT),
      Bind(SettingId::CameraHeight, Config::GFX_VR_CAMERA_HEIGHT),
      Bind(SettingId::HeadTracking, Config::GFX_VR_TRACKING_MODE),
      Bind(SettingId::HudDepth, Config::GFX_VR_HUD_THICKNESS),
      Bind(SettingId::RemoveBars, Config::GFX_VR_REMOVE_BARS),
  };
  return bindings;
}
}  // namespace

const std::vector<SettingRow>& GetSettingRows()
{
  static const std::vector<SettingRow> rows = [] {
    std::vector<SettingRow> all;
    for (const auto& part :
         {MakeScreenRows(), MakeGeneralRows(), MakeGraphicsRows(), MakeImmersiveRows()})
      all.insert(all.end(), part.begin(), part.end());
    return all;
  }();
  return rows;
}

const SettingRow& GetSettingRow(SettingId id)
{
  return *std::ranges::find(GetSettingRows(), id, &SettingRow::id);
}

int CountOwnValues(MenuSection section, const SettingState& state)
{
  return static_cast<int>(
      std::ranges::count_if(GetSettingRows(), [&](const SettingRow& row) {
        return row.section == section && row.id != SettingId::None &&
               state.values.own_value[Index(row.id)];
      }));
}

std::string DescribeOwnValues(int count)
{
  if (count == 0)
    return "No Per-Game Values Set";
  return fmt::format("{} Per-Game Value{} Set", count, count == 1 ? "" : "s");
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

float StepScreenCurve(float degrees, int steps)
{
  // From between two of them, the next one that way.
  float stepped = degrees;
  for (; steps > 0; --steps)
  {
    const auto it = std::ranges::find_if(SCREEN_CURVES, [&](int c) { return ToNumber(c) > stepped; });
    stepped = it != SCREEN_CURVES.end() ? ToNumber(*it) : ToNumber(SCREEN_CURVES.back());
  }
  for (; steps < 0; ++steps)
  {
    float next = ToNumber(SCREEN_CURVES.front());
    for (const int c : SCREEN_CURVES)
    {
      if (ToNumber(c) < stepped)
        next = ToNumber(c);
    }
    stepped = next;
  }
  return stepped;
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
  if (row.id != SettingId::None && state.scope == SettingScope::Global &&
      state.values.game_value[Index(row.id)])
  {
    return Lock::GameValue;
  }
  return Lock::None;
}

std::string ExplainLock(Lock lock, const SettingRow& row)
{
  switch (lock)
  {
  case Lock::GameValue:
    return "This game has its own value: choose Per-Game to change it.";
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
  if (row.id != SettingId::AntiAliasing)
    return row.options;
  std::vector<SettingOption> options;
  for (const SettingOption& option : row.options)
  {
    if (option.value <= 1.0f ||
        std::ranges::contains(state.values.aa_modes, static_cast<u32>(option.value)))
    {
      options.push_back(option);
    }
  }
  return options;
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
    values.game_value[Index(binding.id)] =
        GameSettingScope::HasGameValue(*binding.location, binding.store);
    values.own_value[Index(binding.id)] =
        GameSettingScope::HasOwnValue(*binding.location, binding.store);
  }
  values.aa_modes = g_backend_info.AAModes;
  return values;
}

void WriteSetting(SettingId id, float value, SettingScope scope, bool save)
{
  std::ranges::find(GetBindings(), id, &Binding::id)->write(value, scope, save);
}

void UseGlobalValues(MenuSection section)
{
  for (const Binding& binding : GetBindings())
  {
    if (GetSettingRow(binding.id).section == section && binding.store != SettingStore::Global)
      binding.use_global();
  }
}
}  // namespace VR::UI
