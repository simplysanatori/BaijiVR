// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Settings/VRPane.h"

#include <cmath>
#include <map>

#include <QCheckBox>
#include <QEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "Common/Config/Config.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/System.h"

#include "DolphinQt/Config/HideObjectAddEditDialog.h"
#include "DolphinQt/Debugger/CullingCodeFinderWidget.h"
#include "DolphinQt/Debugger/ShaderHunterWidget.h"
#include "DolphinQt/Settings.h"
#include "VideoCommon/HideObjectEngine.h"
#include "VideoCommon/ShaderHunter.h"
#include "VideoCommon/VideoConfig.h"
#ifdef ENABLE_VR
#include "VideoCommon/VR/OpenXRManager.h"
#endif

namespace
{
QString FormatMeters(float value)
{
  return QString::asprintf("%.1f m", value);
}

// The scale is exponential: small values need decimals to read meaningfully, large ones don't.
QString FormatWorldScale(float value)
{
  return QString::asprintf(value < 10.0f ? "%.2f" : (value < 100.0f ? "%.1f" : "%.0f"), value);
}

// As few decimals as it takes.
QString FormatPlain(float value)
{
  return QString::number(std::round(value * 100.0) / 100.0);
}
}  // namespace

// ---- VRSettingsPage ----------------------------------------------------------------------------

// Names that the VR menu in the headset also uses have to be the same here.
VRSettingsPage::Text VRSettingsPage::GetText(const Config::Location& location)
{
  static const std::map<Config::Location, Text> texts{
      {Config::GFX_VR_PRESENTATION_MODE.GetLocation(),
       {tr("Display"), tr("How games are shown. During a game, change it in the VR menu.")}},

      // Screen
      {Config::GFX_VR_VIRTUAL_SCREEN.GetLocation(),
       {tr("2D Parts on a Screen"),
        tr("Puts menus, videos and the HUD on a screen in front of you. Turn it off for games "
           "that look wrong with it.")}},
      {Config::GFX_VR_SCREEN_DISTANCE.GetLocation(),
       {tr("Distance"), tr("How far away the screen is.")}},
      {Config::GFX_VR_SCREEN_SIZE.GetLocation(), {tr("Size"), tr("How tall the screen is.")}},
      {Config::GFX_VR_HUD_THICKNESS.GetLocation(),
       {tr("HUD Depth"), tr("Spreads the layers of the HUD apart in depth. Flat menus and videos "
                            "stay flat.")}},
      {Config::GFX_VR_SCREEN_DEPTH.GetLocation(),
       {tr("3D Depth"),
        tr("How deep the picture is. Natural follows the distance between your eyes and the "
           "screen's size. Lower is easier on the eyes.")}},
      {Config::GFX_STEREO_CONVERGENCE.GetLocation(),
       {tr("Convergence"),
        tr("How far things come out of the screen. Lower is easier on the eyes.")}},
      {Config::GFX_VR_LOCK_SCREEN.GetLocation(),
       {tr("Lock Screen"), tr("Keeps the grips from moving the screen.")}},

      // Immersive
      {Config::GFX_VR_UNITS_PER_METER.GetLocation(),
       {tr("World Scale"), tr("Game units per meter. Higher makes the world smaller, lower makes it bigger.")}},
      {Config::GFX_VR_STEREO_SEPARATION.GetLocation(),
       {tr("3D Strength"), tr("How deep the world looks. 100% is natural. Lower is easier on the "
                              "eyes.")}},
      {Config::GFX_VR_ENABLE_LEAN_BACK_ANGLE.GetLocation(),
       {tr("Lean Back"), tr("Tilts the camera, for games with a tilted view.")}},
      {Config::GFX_VR_LEAN_BACK_ANGLE.GetLocation(),
       {tr("Lean Back"), tr("Above 0 leans back, below 0 leans forward.")}},
      {Config::GFX_VR_ENABLE_CAMERA_FORWARD.GetLocation(),
       {tr("Camera Forward"), tr("Moves the camera forward or back.")}},
      {Config::GFX_VR_CAMERA_FORWARD.GetLocation(),
       {tr("Camera Forward"), tr("Above 0 is forward, below 0 is back.")}},
      {Config::GFX_VR_ENABLE_CAMERA_HEIGHT.GetLocation(),
       {tr("Camera Height"), tr("Moves the camera up or down.")}},
      {Config::GFX_VR_CAMERA_HEIGHT.GetLocation(),
       {tr("Camera Height"), tr("Above 0 is up, below 0 is down.")}},
      {Config::GFX_VR_ENABLE_CAMERA_ANCHOR.GetLocation(),
       {tr("Camera Anchor"),
        tr("Puts the camera on something in the game, for example a character's head. Only in "
           "games with an anchor set up.")}},
      {Config::GFX_VR_CAMERA_ANCHOR_SMOOTHING.GetLocation(),
       {tr("Anchor Smoothing"),
        tr("How softly the camera follows its anchor. 0 follows every movement.")}},
      {Config::GFX_VR_ENABLE_CONTROLLER_ANCHOR.GetLocation(),
       {tr("Controller Anchor"),
        tr("Puts things from the game in your hands, for example a sword. Only in games with an "
           "anchor set up.")}},
      {Config::GFX_VR_REMOVE_BARS.GetLocation(),
       {tr("Remove Cinematic Bars"), tr("Removes the black bars at the top and bottom.")}},

      // Headset
      {Config::GFX_VR_REQUESTED_REFRESH_RATE.GetLocation(),
       {tr("Refresh Rate"),
        tr("The headset's refresh rate. Auto leaves it as it is. A multiple of the game's frame "
           "rate is smoothest, for example 120 Hz for 60 FPS. Applies from the next game.")}},
      {Config::GFX_VR_RESOLUTION_SCALE.GetLocation(),
       {tr("Resolution Scale"),
        tr("Resolution of the picture in the headset. Higher is sharper and slower. Applies from "
           "the next game.")}},
      {Config::GFX_VR_EAGER_HEARTBEAT.GetLocation(),
       {tr("Frame Pacing"),
        tr("Every Refresh shows a picture at each refresh of the headset. Match Game shows one "
           "for each frame of the game, and lets SteamVR smooth the motion.")}},
      {Config::GFX_VR_PASSTHROUGH.GetLocation(),
       {tr("Passthrough"), tr("Shows your room around the game, if the headset can.")}},
      {Config::GFX_VR_GAMMA.GetLocation(),
       {tr("Gamma"), tr("Darkens a picture that is too bright.")}},
      {Config::GFX_VR_FOVEATION_LEVEL.GetLocation(),
       {tr("Foveated Rendering"),
        tr("Less detail at the edges of the view, for speed. Has to be off to change Display in "
           "the headset. Applies from the next game.")}},
      {Config::GFX_VR_FOVEATION_DYNAMIC.GetLocation(),
       {tr("Dynamic Foveation"), tr("Less detail only when the headset is busy.")}},

      // Menu and Pointers
      {Config::GFX_VR_POINTER_BEAMS.GetLocation(),
       {tr("Pointer Beams"), tr("Shows where the controllers point.")}},
      {Config::GFX_VR_TOASTS.GetLocation(),
       {tr("Messages"), tr("Shows short messages in the headset, for example Quick Saved.")}},

      // Autosave
      {Config::MAIN_AUTOSAVE_ENABLED.GetLocation(),
       {tr("Autosave"), tr("Saves a state while you play and when you close the game. Load it in "
                           "the VR menu.")}},
      {Config::MAIN_AUTOSAVE_MINUTES.GetLocation(),
       {tr("Autosave Every"), tr("Minutes of play between two autosaves.")}},
      {Config::MAIN_AUTOSAVE_KEEP.GetLocation(),
       {tr("Autosaves to Keep"), tr("Older ones are deleted.")}},

      // Advanced
      {Config::GFX_VR_REFERENCE_SPACE_MODE.GetLocation(),
       {tr("Starting Position"),
        tr("Where you are when a game starts: your own position, the middle of the play area at "
           "your height, or the middle of the play area. Applies from the next game.")}},
      {Config::GFX_VR_TRACKING_MODE.GetLocation(),
       {tr("Head Tracking"), tr("Which head movements move the camera.")}},
      {Config::GFX_VR_FORCED_VBI_FREQUENCY.GetLocation(),
       {tr("Force Game Refresh"),
        tr("Runs the game at this refresh rate. Many games then play too fast. Headset Rate is "
           "the closest rate in the list.")}},
      {Config::GFX_VR_USE_VULKAN_MULTIVIEW.GetLocation(),
       {tr("Draw Both Eyes at Once"), tr("Faster. Turn it off if one eye looks wrong. Applies "
                                         "from the next game.")}},
      {Config::GFX_VR_EFB_FOVEATION.GetLocation(),
       {tr("Foveate the Game's Own Picture"),
        tr("Uses Foveated Rendering earlier in the picture too. Faster in some games, slower in "
           "others.")}},
      // Performance
      {Config::GFX_VR_DYNAMIC_RESOLUTION.GetLocation(),
       {tr("Dynamic Resolution"),
        tr("Lowers the game's resolution when the headset cannot keep up, and raises it again "
           "when it can.")}},
      {Config::GFX_VR_PIN_EMULATION_CORES.GetLocation(),
       {tr("Pin Emulation Threads"),
        tr("Keeps the main emulation threads on the fastest CPU cores. For the next game "
           "start.")}},
      {Config::GFX_VR_PERFORMANCE_HINT.GetLocation(),
       {tr("Performance Hint"),
        tr("Asks the VR runtime for high CPU and GPU speed, if it offers that. For the next game "
           "start.")}},
      {Config::GFX_VR_ASYNC_EFB_COPIES.GetLocation(),
       {tr("Non-Blocking EFB Copies"),
        tr("Does not wait for the GPU when the game copies the picture to memory. Turn it off if "
           "a game's effects look wrong.")}},
      {Config::GFX_VR_NON_BLOCKING_PERF_QUERIES.GetLocation(),
       {tr("Non-Blocking Perf Queries"),
        tr("Does not wait for the GPU to answer the game's questions about what was drawn.")}},
      {Config::GFX_VR_SKIP_OVERWRITTEN_CLEARS.GetLocation(),
       {tr("Skip Overwritten Clears"),
        tr("Leaves out clears of the picture that the game draws over anyway.")}},
      {Config::GFX_VR_PERF_LOG.GetLocation(),
       {tr("Performance Log"), tr("Writes a line a second to perf.csv in the Logs folder.")}},

#ifndef BAIJI_STEAMVR
      {Config::GFX_VR_MIRROR_VIEW.GetLocation(),
       {tr("Desktop Mirror View"), tr("What the window on the desktop shows.")}},
#endif

      {Config::GFX_VR_EXACT_SCREEN_DEPTH.GetLocation(),
       {tr("Exact 2D Depth"), tr("Draws 2D parts in the game's own order. For menus that flicker "
                                 "or a map that is missing.")}},
      {Config::GFX_VR_AUTO_NATIVE_EFB_EFFECTS.GetLocation(),
       {tr("Full-View Effects"),
        tr("Blur, bloom and such fill the view and not the 2D screen. Turn it off if a 2D part "
           "leaves the screen.")}},
      {Config::GFX_VR_PANES_ON_SCREEN.GetLocation(),
       {tr("Small 3D Models on the Screen"),
        tr("Keeps 3D models in menus on the 2D screen, so that they do not sway with your "
           "head.")}},
      {Config::GFX_VR_HEAD_LOCKED_CURVATURE.GetLocation(),
       {tr("Head-Locked Curve"), tr("Curves the parts that are set to follow your head.")}},
      {Config::GFX_VR_CLEAR_EFB_COPIES.GetLocation(),
       {tr("Remove Wide Effects"),
        tr("Removes effects that are at least this wide, for example bloom. The whole picture "
           "is 640 wide, so use 400 to 640.")}},
      {Config::GFX_VR_DONT_CLEAR_SCREEN.GetLocation(),
       {tr("Don't Clear Screen"), tr("Keeps the last picture where the game draws nothing. For "
                                     "games that flicker.")}},
      {Config::GFX_VR_DISABLE_CPU_CULL.GetLocation(),
       {tr("Show Missing Objects"),
        tr("Draws objects that are otherwise left out. A little slower.")}},
      {Config::GFX_VR_FRAME_SIZE_FROM_XFB.GetLocation(),
       {tr("Match Shown Picture Size"),
        tr("Takes the size of the picture from what the game shows. Fixes Metroid Prime Trilogy "
           "and Mario Golf.")}},
      {Config::GFX_VR_DETECT_RENDER_TARGETS.GetLocation(),
       {tr("Steady Shadows and Reflections"),
        tr("Keeps shadows and reflections from moving with your head. Experimental.")}},
      {Config::GFX_VR_ORTHO_SCISSOR_FIX.GetLocation(),
       {tr("Fix Cut-Off 2D Parts"), tr("Shows all of 2D parts that are cut off, for example the "
                                       "item roulette in Mario Kart: Double Dash!!.")}},
      {Config::GFX_VR_DETECT_SKYBOX.GetLocation(),
       {tr("Distant Sky"), tr("Puts a sky that is too close far away.")}},
      {Config::GFX_VR_METROID_THERMAL_VISOR_FIX.GetLocation(),
       {tr("Metroid Prime Thermal Visor Fix"),
        tr("Shows the thermal visor in 3D in both eyes.")}},

      {Config::GFX_VR_PASSTHROUGH_REMOVE_BLACK_BG.GetLocation(),
       {tr("Show Room in Empty Areas"), tr("Shows the room where the game draws nothing.")}},
      {Config::GFX_VR_PASSTHROUGH_REMOVE_BLACK_CLEARS.GetLocation(),
       {tr("Show Room in Black Areas"),
        tr("Shows the room where the game leaves the picture black.")}},
      {Config::GFX_VR_PASSTHROUGH_SCENE_OPACITY.GetLocation(),
       {tr("Game Opacity"), tr("Lower shows the room through the game.")}},
      {Config::GFX_VR_PASSTHROUGH_COVERAGE_MODE.GetLocation(),
       {tr("Passthrough Quality"), tr("Exact keeps all effects of the game. Fast is faster.")}},

      {Config::GFX_VR_LOAD_CUSTOM_SHADERS.GetLocation(),
       {tr("Custom Shaders"), tr("Loads shaders from Load/Shaders and the game's ID.")}},
  };

  const auto it = texts.find(location);
  return it != texts.end() ? it->second : Text{};
}

QLabel* VRSettingsPage::MakeLabel(const Config::Location& location)
{
  return new QLabel(GetText(location).label + QLatin1Char(':'));
}

VRSettingsPage::VRSettingsPage(QWidget* parent, QBoxLayout* layout, Config::Layer* layer,
                               Config::Layer* global_layer)
    : QObject(parent), m_layout(layout), m_layer(layer), m_global_layer(global_layer)
{
}

bool VRSettingsPage::eventFilter(QObject* object, QEvent* event)
{
  // The page is long: scrolling it must not change what happens to be under the pointer.
  if (event->type() == QEvent::Wheel && !static_cast<QWidget*>(object)->hasFocus())
  {
    event->ignore();
    return true;
  }
  return QObject::eventFilter(object, event);
}

void VRSettingsPage::ShowOnlyIn(unsigned displays, QWidget* widget)
{
  if (displays != ALL)
    m_widgets.emplace_back(displays, widget);
}

QGridLayout* VRSettingsPage::AddGroup(const QString& title, unsigned displays, QBoxLayout* layout)
{
  auto* const group = new QGroupBox(title);
  (layout ? layout : m_layout)->addWidget(group);
  ShowOnlyIn(displays, group);

  auto* const grid = new QGridLayout(group);
  grid->setColumnStretch(1, 1);
  return grid;
}

void VRSettingsPage::AddRow(QGridLayout* layout, unsigned displays, QWidget* label,
                            QWidget* control, QWidget* value)
{
  const int row = layout->rowCount();
  if (label)
  {
    layout->addWidget(label, row, 0);
    ShowOnlyIn(displays, label);
  }
  layout->addWidget(control, row, label ? 1 : 0, 1, (label ? 2 : 3) - (value ? 1 : 0));
  ShowOnlyIn(displays, control);
  if (value)
  {
    layout->addWidget(value, row, 2);
    ShowOnlyIn(displays, value);
  }
}

ConfigBool* VRSettingsPage::AddBool(QGridLayout* layout, unsigned displays,
                                    const Config::Info<bool>& setting)
{
  auto* const check = Prepare(new ConfigBool(GetText(setting.GetLocation()).label, setting, m_layer,
                                             false, m_global_layer));
  AddRow(layout, displays, nullptr, check);
  return check;
}

ConfigFloatSlider* VRSettingsPage::AddSlider(QGridLayout* layout, unsigned displays,
                                             const Config::Info<float>& setting, float minimum,
                                             float maximum, float step,
                                             std::function<QString(float)> format,
                                             const Config::Info<bool>* enable,
                                             ConfigFloatSlider::ScaleMode scale)
{
  auto* const slider = Prepare(
      new ConfigFloatSlider(minimum, maximum, setting, step, m_layer, scale, m_global_layer));

  auto* const value = new QLabel;
  // So that the slider does not change its length with its value.
  value->setMinimumWidth(value->fontMetrics().horizontalAdvance(QStringLiteral("000.00 m")));
  const auto update = [slider, value, format = std::move(format)] {
    value->setText(format(slider->GetValue()));
  };
  update();
  connect(slider, &ConfigFloatSlider::valueChanged, slider, update);

  QWidget* label;
  if (enable)
  {
    label = Prepare(new ConfigBool(GetText(enable->GetLocation()).label, *enable, m_layer, false,
                                   m_global_layer));
  }
  else
  {
    label = new ConfigFloatLabel(GetText(setting.GetLocation()).label + QLatin1Char(':'), slider);
  }
  AddRow(layout, displays, label, slider, value);
  return slider;
}

void VRSettingsPage::AddDisplay()
{
  auto* const layout = AddGroup(tr("Display"), ALL);

  // An old config can hold OpenXRPresentationMode::Legacy, which is not a choice here. It is shown
  // as the display it gives (UpdateDisplay), and only another choice replaces it.
  m_display = Prepare(new ConfigChoiceMap<OpenXRPresentationMode>(
      {{tr("Stereoscopic 3D"), OpenXRPresentationMode::StereoScreen},
       {tr("Immersive"), OpenXRPresentationMode::Immersive},
       {tr("Flat"), OpenXRPresentationMode::Vulkan}},
      Config::GFX_VR_PRESENTATION_MODE, m_layer, m_global_layer));
  m_display->setPlaceholderText(tr("2D Screen"));
  connect(m_display, &QComboBox::currentIndexChanged, this, &VRSettingsPage::OnDisplayChosen);
  layout->addWidget(m_display, 0, 0, 1, 3);

  m_display_text = new QLabel;
  m_display_text->setWordWrap(true);
  layout->addWidget(m_display_text, 1, 0, 1, 3);
}

void VRSettingsPage::OnDisplayChosen()
{
  const OpenXRPresentationMode mode = Read(Config::GFX_VR_PRESENTATION_MODE);
  if (mode != OpenXRPresentationMode::Legacy)
  {
    const auto set = [this](const Config::Info<bool>& setting, bool value) {
      if (m_layer)
        m_layer->Set(setting, value);
      else
        Config::SetBaseOrCurrent(setting, value);
    };
    Config::ConfigChangeCallbackGuard guard;
    set(Config::GFX_VR_ENABLE_OPENXR, mode != OpenXRPresentationMode::Vulkan);
    set(Config::GFX_VR_FLAT_SCREEN, false);
    set(Config::GFX_VR_STEREO_SCREEN, mode == OpenXRPresentationMode::StereoScreen);
  }
  UpdateDisplay();
}

// As VideoConfig::Refresh resolves it.
VRSettingsPage::Display VRSettingsPage::GetDisplay() const
{
  switch (Read(Config::GFX_VR_PRESENTATION_MODE))
  {
  case OpenXRPresentationMode::Vulkan:
    return Flat;
  case OpenXRPresentationMode::Immersive:
    return Immersive;
  case OpenXRPresentationMode::StereoScreen:
    return Screen3D;
  case OpenXRPresentationMode::Legacy:
    break;
  }

  if (!Read(Config::GFX_VR_ENABLE_OPENXR))
    return Flat;
  if (Read(Config::GFX_VR_FLAT_SCREEN))
    return Screen2D;
  return Read(Config::GFX_VR_STEREO_SCREEN) ? Screen3D : Immersive;
}

void VRSettingsPage::UpdateDisplay()
{
  const Display display = GetDisplay();

  int index = -1;
  switch (display)
  {
  case Screen3D:
    index = 0;
    m_display_text->setText(tr("The game is on a 3D screen in the room."));
    break;
  case Immersive:
    index = 1;
    m_display_text->setText(tr("The game world is all around you."));
    break;
  case Flat:
    index = 2;
    m_display_text->setText(tr("The game is in a normal window, without VR."));
    break;
  case Screen2D:
    m_display_text->setText(tr("The game is on a 2D screen in the room."));
    break;
  }
  {
    const QSignalBlocker blocker(m_display);
    m_display->setCurrentIndex(index);
  }

  for (const auto& [displays, widget] : m_widgets)
    widget->setVisible((displays & display) != 0);
}

void VRSettingsPage::Finish()
{
  m_layout->addStretch();

  // After the controls' own handlers, as m_display's index is one of the things updated.
  connect(&Settings::Instance(), &Settings::ConfigChanged, this, &VRSettingsPage::UpdateDisplay);
  UpdateDisplay();
}

QGridLayout* VRSettingsPage::AddScreen()
{
  auto* const layout = AddGroup(tr("Screen"), HEADSET);

  // In Immersive, the screen is the one for the game's 2D parts.
  AddBool(layout, Immersive, Config::GFX_VR_VIRTUAL_SCREEN);
  AddSlider(layout, HEADSET, Config::GFX_VR_SCREEN_DISTANCE, Config::GFX_VR_SCREEN_DISTANCE_MIN,
            Config::GFX_VR_SCREEN_DISTANCE_MAX, Config::GFX_VR_SCREEN_DISTANCE_STEP, FormatMeters);
  AddSlider(layout, HEADSET, Config::GFX_VR_SCREEN_SIZE, Config::GFX_VR_SCREEN_SIZE_MIN,
            Config::GFX_VR_SCREEN_SIZE_MAX, Config::GFX_VR_SCREEN_SIZE_STEP, FormatMeters);
  AddSlider(layout, Immersive, Config::GFX_VR_HUD_THICKNESS, Config::GFX_VR_HUD_THICKNESS_MIN,
            Config::GFX_VR_HUD_THICKNESS_MAX, Config::GFX_VR_HUD_THICKNESS_STEP, [](float value) {
              return value <= 0.001f ? tr("Off") : QString::asprintf("%.2f m", value);
            });
  return layout;
}

void VRSettingsPage::AddImmersive()
{
  auto* const layout = AddGroup(tr("Immersive"), Immersive);

  AddSlider(layout, ALL, Config::GFX_VR_UNITS_PER_METER, Config::GFX_VR_UNITS_PER_METER_MIN,
            Config::GFX_VR_UNITS_PER_METER_MAX, Config::GFX_VR_UNITS_PER_METER_STEP,
            FormatWorldScale, nullptr, ConfigFloatSlider::ScaleMode::Exponential);
  AddSlider(layout, ALL, Config::GFX_VR_STEREO_SEPARATION, Config::GFX_VR_STEREO_SEPARATION_MIN,
            Config::GFX_VR_STEREO_SEPARATION_MAX, Config::GFX_VR_STEREO_SEPARATION_STEP,
            [](float value) { return QString::asprintf("%.0f%%", value); });
  AddSlider(layout, ALL, Config::GFX_VR_LEAN_BACK_ANGLE, Config::GFX_VR_LEAN_BACK_ANGLE_MIN,
            Config::GFX_VR_LEAN_BACK_ANGLE_MAX, Config::GFX_VR_LEAN_BACK_ANGLE_STEP,
            [](float value) { return QString::asprintf("%.1f°", value); },
            &Config::GFX_VR_ENABLE_LEAN_BACK_ANGLE);
  AddSlider(layout, ALL, Config::GFX_VR_CAMERA_FORWARD, Config::GFX_VR_CAMERA_FORWARD_MIN,
            Config::GFX_VR_CAMERA_FORWARD_MAX, Config::GFX_VR_CAMERA_FORWARD_STEP, FormatMeters,
            &Config::GFX_VR_ENABLE_CAMERA_FORWARD);
  AddSlider(layout, ALL, Config::GFX_VR_CAMERA_HEIGHT, Config::GFX_VR_CAMERA_HEIGHT_MIN,
            Config::GFX_VR_CAMERA_HEIGHT_MAX, Config::GFX_VR_CAMERA_HEIGHT_STEP, FormatMeters,
            &Config::GFX_VR_ENABLE_CAMERA_HEIGHT);
  AddBool(layout, ALL, Config::GFX_VR_ENABLE_CAMERA_ANCHOR);
  AddSlider(layout, ALL, Config::GFX_VR_CAMERA_ANCHOR_SMOOTHING,
            Config::GFX_VR_CAMERA_ANCHOR_SMOOTHING_MIN, Config::GFX_VR_CAMERA_ANCHOR_SMOOTHING_MAX,
            Config::GFX_VR_CAMERA_ANCHOR_SMOOTHING_STEP,
            [](float value) { return QString::asprintf("%.2f", value); });
  AddBool(layout, ALL, Config::GFX_VR_ENABLE_CONTROLLER_ANCHOR);
  AddBool(layout, ALL, Config::GFX_VR_REMOVE_BARS);
}

void VRSettingsPage::AddHeadset()
{
  auto* const layout = AddGroup(tr("Headset"), HEADSET);

  m_refresh_rate =
      AddChoice<int>(layout, ALL, Config::GFX_VR_REQUESTED_REFRESH_RATE,
                     {{tr("Auto"), Config::GFX_VR_REQUESTED_REFRESH_RATE_AUTO},
                      {tr("60 Hz"), Config::GFX_VR_REQUESTED_REFRESH_RATE_60},
                      {tr("72 Hz"), Config::GFX_VR_REQUESTED_REFRESH_RATE_72},
                      {tr("90 Hz"), Config::GFX_VR_REQUESTED_REFRESH_RATE_90},
                      {tr("120 Hz"), Config::GFX_VR_REQUESTED_REFRESH_RATE_120}});
  AddSlider(layout, ALL, Config::GFX_VR_RESOLUTION_SCALE, Config::GFX_VR_RESOLUTION_SCALE_MIN,
            Config::GFX_VR_RESOLUTION_SCALE_MAX, Config::GFX_VR_RESOLUTION_SCALE_STEP,
            [](float value) { return QString::asprintf("%.2fx", value); });
  AddChoice<bool>(layout, ALL, Config::GFX_VR_EAGER_HEARTBEAT,
                  {{tr("Every Refresh"), true}, {tr("Match Game"), false}});
  AddSlider(layout, TWO_EYES, Config::GFX_VR_GAMMA, Config::GFX_VR_GAMMA_MIN,
            Config::GFX_VR_GAMMA_MAX, Config::GFX_VR_GAMMA_STEP, [](float value) {
              return value <= 1.01f ? tr("Off") : QString::asprintf("%.1f", value);
            });
  AddChoice<int>(layout, Immersive, Config::GFX_VR_FOVEATION_LEVEL,
                 {{tr("Off"), 0}, {tr("Low"), 1}, {tr("Medium"), 2}, {tr("High"), 3}});
  AddBool(layout, Immersive, Config::GFX_VR_FOVEATION_DYNAMIC);
  // On Linux, Immersive's picture is always opaque (VulkanOpenXR turns coverage off there), so
  // only the room around a screen can be seen.
#if defined(_WIN32)
  AddBool(layout, TWO_EYES, Config::GFX_VR_PASSTHROUGH);
#else
  AddBool(layout, Screen3D, Config::GFX_VR_PASSTHROUGH);
#endif
}

QVBoxLayout* VRSettingsPage::AddAdvanced()
{
  auto* const button = new QToolButton;
  button->setText(tr("Advanced"));
  button->setCheckable(true);
  button->setAutoRaise(true);
  button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  button->setArrowType(Qt::RightArrow);
  m_layout->addWidget(button, 0, Qt::AlignLeft);

  auto* const box = new QWidget;
  box->hide();
  m_layout->addWidget(box);
  auto* const box_layout = new QVBoxLayout(box);
  box_layout->setContentsMargins(0, 0, 0, 0);

  connect(button, &QToolButton::toggled, box, [button, box](bool open) {
    button->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    box->setVisible(open);
  });

  auto* const tracking = AddGroup(tr("Tracking and Speed"), HEADSET, box_layout);
  m_reference_space_mode = AddChoice<OpenXRReferenceSpaceMode>(
      tracking, ALL, Config::GFX_VR_REFERENCE_SPACE_MODE,
      {{tr("Where You Are"), OpenXRReferenceSpaceMode::Local},
       {tr("Play Area, Your Height"), OpenXRReferenceSpaceMode::StageHeight},
       {tr("Play Area"), OpenXRReferenceSpaceMode::Stage}});
  AddChoice<OpenXRTrackingMode>(tracking, Immersive, Config::GFX_VR_TRACKING_MODE,
                                {{tr("Turn and Move"), OpenXRTrackingMode::Full6DoF},
                                 {tr("Turn Only"), OpenXRTrackingMode::Rotation3DoF},
                                 {tr("Off"), OpenXRTrackingMode::None}});
  m_forced_vbi_frequency =
      AddChoice<int>(tracking, ALL, Config::GFX_VR_FORCED_VBI_FREQUENCY,
                     {{tr("Headset Rate"), Config::GFX_VR_FORCED_VBI_FREQUENCY_AUTO},
                      {tr("Off"), Config::GFX_VR_FORCED_VBI_FREQUENCY_OFF},
                      {tr("72 Hz"), Config::GFX_VR_FORCED_VBI_FREQUENCY_72},
                      {tr("90 Hz"), Config::GFX_VR_FORCED_VBI_FREQUENCY_90},
                      {tr("120 Hz"), Config::GFX_VR_FORCED_VBI_FREQUENCY_120}});
  m_multiview = AddBool(tracking, Immersive, Config::GFX_VR_USE_VULKAN_MULTIVIEW);
  AddBool(tracking, Immersive, Config::GFX_VR_EFB_FOVEATION);
#ifndef BAIJI_STEAMVR
  AddChoice<OpenXRMirrorView>(tracking, HEADSET, Config::GFX_VR_MIRROR_VIEW,
                              {{tr("Both Eyes"), OpenXRMirrorView::BothEyes},
                               {tr("Left Eye"), OpenXRMirrorView::LeftEye},
                               {tr("Right Eye"), OpenXRMirrorView::RightEye},
                               {tr("None"), OpenXRMirrorView::None}});
#endif

  auto* const fixes = AddGroup(tr("Game Fixes"), TWO_EYES, box_layout);
  m_exact_screen_depth = AddBool(fixes, Immersive, Config::GFX_VR_EXACT_SCREEN_DEPTH);
  m_auto_native_efb_effects = AddBool(fixes, Immersive, Config::GFX_VR_AUTO_NATIVE_EFB_EFFECTS);
  AddBool(fixes, Immersive, Config::GFX_VR_PANES_ON_SCREEN);
  AddBool(fixes, Immersive, Config::GFX_VR_ORTHO_SCISSOR_FIX);
  AddBool(fixes, Immersive, Config::GFX_VR_DISABLE_CPU_CULL);
  AddBool(fixes, Immersive, Config::GFX_VR_DETECT_SKYBOX);
  AddBool(fixes, Immersive, Config::GFX_VR_DETECT_RENDER_TARGETS);
  AddBool(fixes, Immersive, Config::GFX_VR_FRAME_SIZE_FROM_XFB);
  AddBool(fixes, Immersive, Config::GFX_VR_DONT_CLEAR_SCREEN);
  m_thermal_visor_fix = AddBool(fixes, ALL, Config::GFX_VR_METROID_THERMAL_VISOR_FIX);

  auto* const clear_efb = Prepare(
      new ConfigSlider(Config::GFX_VR_CLEAR_EFB_MIN, Config::GFX_VR_CLEAR_EFB_MAX,
                       Config::GFX_VR_CLEAR_EFB_COPIES, m_layer, Config::GFX_VR_CLEAR_EFB_STEP,
                       m_global_layer));
  auto* const clear_efb_value = new QLabel;
  const auto update_clear_efb = [clear_efb, clear_efb_value] {
    clear_efb_value->setText(clear_efb->value() == 0 ? tr("Off") :
                                                       QString::number(clear_efb->value()));
  };
  update_clear_efb();
  connect(clear_efb, &ConfigSlider::valueChanged, clear_efb, update_clear_efb);
  AddRow(fixes, Immersive,
         new ConfigSliderLabel(GetText(clear_efb->GetLocation()).label + QLatin1Char(':'),
                               clear_efb),
         clear_efb, clear_efb_value);
  AddSlider(fixes, Immersive, Config::GFX_VR_HEAD_LOCKED_CURVATURE,
            Config::GFX_VR_HEAD_LOCKED_CURVATURE_MIN, Config::GFX_VR_HEAD_LOCKED_CURVATURE_MAX,
            Config::GFX_VR_HEAD_LOCKED_CURVATURE_STEP, [](float value) {
              return value <= 0.001f ? tr("Off") : QString::asprintf("%.2f", value);
            });

#if defined(_WIN32)
  auto* const passthrough = AddGroup(tr("Passthrough"), TWO_EYES, box_layout);
  AddBool(passthrough, ALL, Config::GFX_VR_PASSTHROUGH_REMOVE_BLACK_BG);
  AddBool(passthrough, ALL, Config::GFX_VR_PASSTHROUGH_REMOVE_BLACK_CLEARS);
  AddSlider(passthrough, ALL, Config::GFX_VR_PASSTHROUGH_SCENE_OPACITY,
            Config::GFX_VR_PASSTHROUGH_SCENE_OPACITY_MIN,
            Config::GFX_VR_PASSTHROUGH_SCENE_OPACITY_MAX,
            Config::GFX_VR_PASSTHROUGH_SCENE_OPACITY_STEP,
            [](float value) { return QString::asprintf("%.2f", value); });
  AddChoice<VRPassthroughCoverageMode>(passthrough, ALL, Config::GFX_VR_PASSTHROUGH_COVERAGE_MODE,
                                       {{tr("Exact"), VRPassthroughCoverageMode::Exact},
                                        {tr("Fast"), VRPassthroughCoverageMode::Fast}});
  QWidget* const passthrough_group = passthrough->parentWidget();
  const auto update_passthrough_backend = [passthrough_group] {
    passthrough_group->setEnabled(Config::Get(Config::MAIN_GFX_BACKEND) == "Vulkan");
  };
  connect(&Settings::Instance(), &Settings::ConfigChanged, passthrough_group,
          update_passthrough_backend);
  update_passthrough_backend();
#endif

  auto* const shaders = AddGroup(tr("Shaders"), ALL, box_layout);
  AddBool(shaders, ALL, Config::GFX_VR_LOAD_CUSTOM_SHADERS);

  auto* const performance = AddGroup(tr("Performance"), ALL, box_layout);
  AddBool(performance, ALL, Config::GFX_VR_DYNAMIC_RESOLUTION);
  if (!m_layer)  // not per game: these are read once, or are a log
  {
    AddBool(performance, ALL, Config::GFX_VR_PIN_EMULATION_CORES);
    AddBool(performance, ALL, Config::GFX_VR_PERFORMANCE_HINT);
  }
  AddBool(performance, ALL, Config::GFX_VR_ASYNC_EFB_COPIES);
  AddBool(performance, ALL, Config::GFX_VR_NON_BLOCKING_PERF_QUERIES);
  AddBool(performance, ALL, Config::GFX_VR_SKIP_OVERWRITTEN_CLEARS);
  if (!m_layer)
    AddBool(performance, ALL, Config::GFX_VR_PERF_LOG);
  return box_layout;
}

// ---- VRPane ------------------------------------------------------------------------------------

VRPane::VRPane(QWidget* parent) : QWidget(parent)
{
  auto* const main_layout = new QVBoxLayout(this);

  m_page = new VRSettingsPage(this, main_layout);
  m_page->AddDisplay();
  AddScreenRows(m_page->AddScreen());
  m_page->AddImmersive();
  m_page->AddHeadset();
  AddMenuAndPointers();
  AddAutosave();
  AddTools(m_page->AddAdvanced());

  connect(m_page->m_forced_vbi_frequency, &QComboBox::currentIndexChanged, this,
          [](int) { Config::SetBaseOrCurrent(Config::GFX_VR_AUTO_VBI_FROM_HMD, false); });

  const auto update_thermal_visor_fix = [](bool checked) {
    if (Config::Get(Config::GFX_VR_METROID_D3D_THERMAL_PALETTE_FIX) != checked)
      Config::SetBaseOrCurrent(Config::GFX_VR_METROID_D3D_THERMAL_PALETTE_FIX, checked);
  };
  connect(m_page->m_thermal_visor_fix, &QCheckBox::toggled, this, update_thermal_visor_fix);
  update_thermal_visor_fix(m_page->m_thermal_visor_fix->isChecked());

  // Immersive VR only changes these two options' defaults (see GetVRExactScreenDepth). Show the
  // value in effect; the options stay editable, and a change here is saved.
  const auto update_immersive_screen_options = [this] {
    const bool immersive_vr =
        Config::Get(Config::GFX_VR_PRESENTATION_MODE) == OpenXRPresentationMode::Immersive;
    const QSignalBlocker exact_screen_depth_blocker(m_page->m_exact_screen_depth);
    const QSignalBlocker auto_native_efb_effects_blocker(m_page->m_auto_native_efb_effects);
    m_page->m_exact_screen_depth->setChecked(GetVRExactScreenDepth(immersive_vr));
    m_page->m_auto_native_efb_effects->setChecked(GetVRAutoNativeEfbEffects(immersive_vr));
  };
  connect(m_page->m_display, &QComboBox::currentIndexChanged, this,
          [update_immersive_screen_options](int) { update_immersive_screen_options(); });
  connect(&Settings::Instance(), &Settings::ConfigChanged, this, update_immersive_screen_options);
  update_immersive_screen_options();

  auto* const actions_layout = new QHBoxLayout;
  actions_layout->addStretch();
  auto* const reset_settings = new QPushButton(tr("Reset VR Settings"));
  connect(reset_settings, &QPushButton::clicked, this, &VRPane::ResetSettings);
  actions_layout->addWidget(reset_settings);
  main_layout->addLayout(actions_layout);

  m_page->Finish();

  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this,
          &VRPane::OnEmulationStateChanged);
  OnEmulationStateChanged(Core::GetState(Core::System::GetInstance()));
}

void VRPane::AddScreenRows(QGridLayout* layout)
{
  m_page->AddSlider(layout, VRSettingsPage::Screen3D, Config::GFX_VR_SCREEN_DEPTH, 0.0f, 100.0f,
                    5.0f, [](float value) {
                      if (value < 0.5f)
                        return tr("Off");
                      return value > 99.5f ? tr("Natural") : QString::asprintf("%.0f%%", value);
                    });
  m_page->AddSlider(layout, VRSettingsPage::Screen3D, Config::GFX_STEREO_CONVERGENCE, 0.0f,
                    Config::GFX_STEREO_CONVERGENCE_MAXIMUM, 0.01f, FormatPlain);

  constexpr unsigned screen = VRSettingsPage::Screen3D | VRSettingsPage::Screen2D;
  m_page->AddBool(layout, screen, Config::GFX_VR_LOCK_SCREEN);
  auto* const reset_screen = new QPushButton(tr("Reset Screen"));
  reset_screen->setToolTip(tr("Puts the screen back in front of you, at its usual distance and size."));
  connect(reset_screen, &QPushButton::clicked, this, &VRPane::ResetScreen);
  m_page->AddRow(layout, screen, nullptr, reset_screen);
}

void VRPane::AddMenuAndPointers()
{
  auto* const layout = m_page->AddGroup(tr("Menu and Pointers"), VRSettingsPage::HEADSET);
  m_page->AddBool(layout, VRSettingsPage::ALL, Config::GFX_VR_POINTER_BEAMS);
  m_page->AddBool(layout, VRSettingsPage::ALL, Config::GFX_VR_TOASTS);
}

void VRPane::AddAutosave()
{
  auto* const group_layout = m_page->AddGroup(tr("Autosave"), VRSettingsPage::ALL);
  auto* const enabled =
      m_page->AddBool(group_layout, VRSettingsPage::ALL, Config::MAIN_AUTOSAVE_ENABLED);

  auto* const options = new QWidget;
  m_page->AddRow(group_layout, VRSettingsPage::ALL, nullptr, options);
  auto* const options_layout = new QGridLayout(options);
  options_layout->setContentsMargins(0, 0, 0, 0);
  options_layout->setColumnStretch(1, 1);

  // As the VR menu has them.
  std::vector<std::pair<QString, u32>> minutes;
  for (const u32 m : {1u, 2u, 5u, 10u, 15u})
    minutes.emplace_back(tr("%1 min").arg(m), m);
  auto* const every = m_page->AddChoice<u32>(options_layout, VRSettingsPage::ALL,
                                             Config::MAIN_AUTOSAVE_MINUTES, minutes);

  auto* const keep =
      m_page->Prepare(new ConfigSliderU32(1, 10, Config::MAIN_AUTOSAVE_KEEP));
  auto* const keep_value = new QLabel;
  keep_value->setMinimumWidth(keep_value->fontMetrics().horizontalAdvance(QStringLiteral("00")));
  const auto update_keep = [keep, keep_value] {
    keep_value->setText(QString::number(keep->value()));
  };
  update_keep();
  connect(keep, &ConfigSliderU32::valueChanged, keep, update_keep);
  m_page->AddRow(options_layout, VRSettingsPage::ALL,
                 VRSettingsPage::MakeLabel(keep->GetLocation()), keep, keep_value);

  const auto update = [options, every] {
    options->setVisible(Config::Get(Config::MAIN_AUTOSAVE_ENABLED));
    // For a number of minutes from the config file that is not in the list.
    every->setPlaceholderText(tr("%1 min").arg(Config::Get(Config::MAIN_AUTOSAVE_MINUTES)));
  };
  connect(enabled, &QCheckBox::toggled, this, update);
  connect(&Settings::Instance(), &Settings::ConfigChanged, this, update);
  update();
}

void VRPane::AddTools(QVBoxLayout* layout)
{
  auto* const tools_layout =
      m_page->AddGroup(tr("Developer Tools"), VRSettingsPage::ALL, layout);

  auto* add_hide_object_code_btn = new QPushButton(tr("Add Hide Object Code"));
  connect(add_hide_object_code_btn, &QPushButton::clicked, this, [this] {
    const std::string game_id = SConfig::GetInstance().GetGameID();
    if (game_id.empty() || game_id == "00000000")
    {
      QMessageBox::information(this, tr("No Game Running"),
                               tr("Start a game before adding Hide Object codes."));
      return;
    }

    auto codes = HideObjectEngine::LoadFromINI(game_id);
    HideObjectAddEditDialog dialog(this, nullptr, codes);
    if (dialog.exec() == QDialog::Accepted)
    {
      codes.push_back(dialog.GetResult());
      HideObjectEngine::SaveToINI(game_id, codes);
      HideObjectEngine::Engine::GetInstance().ApplyCodes(codes);
    }
    else
    {
      // Restore the active code set if temporary brute-force values were applied.
      HideObjectEngine::Engine::GetInstance().ApplyCodes(codes);
    }
  });
  tools_layout->addWidget(add_hide_object_code_btn, 0, 0);

  auto* shader_hunter_btn = new QPushButton(tr("Shader Hunter"));
  connect(shader_hunter_btn, &QPushButton::clicked, this, [this] {
    const std::string game_id = SConfig::GetInstance().GetGameID();
    if (game_id.empty() || game_id == "00000000")
    {
      QMessageBox::information(this, tr("No Game Running"),
                               tr("Start a game before searching for shaders."));
      return;
    }

    if (m_shader_hunter_widget)
    {
      m_shader_hunter_widget->show();
      m_shader_hunter_widget->raise();
      m_shader_hunter_widget->activateWindow();
      return;
    }

    m_shader_hunter_widget = new ShaderHunterWidget(this);
    m_shader_hunter_widget->setAttribute(Qt::WA_DeleteOnClose, true);
    m_shader_hunter_widget->show();
    connect(m_shader_hunter_widget, &QObject::destroyed, this,
            [this] { m_shader_hunter_widget = nullptr; });
  });
  tools_layout->addWidget(shader_hunter_btn, 1, 0);

  auto* culling_finder_btn = new QPushButton(tr("Culling Code Finder"));
  connect(culling_finder_btn, &QPushButton::clicked, this, [this] {
    const std::string game_id = SConfig::GetInstance().GetGameID();
    if (game_id.empty() || game_id == "00000000")
    {
      QMessageBox::information(this, tr("No Game Running"),
                               tr("Start a game before searching for culling codes."));
      return;
    }

    if (m_culling_finder_widget)
    {
      m_culling_finder_widget->show();
      m_culling_finder_widget->raise();
      m_culling_finder_widget->activateWindow();
      return;
    }

    m_culling_finder_widget = new CullingCodeFinderWidget(this);
    m_culling_finder_widget->setAttribute(Qt::WA_DeleteOnClose, true);
    m_culling_finder_widget->show();
    connect(m_culling_finder_widget, &QObject::destroyed, this,
            [this] { m_culling_finder_widget = nullptr; });
  });
  tools_layout->addWidget(culling_finder_btn, 2, 0);

  auto* const log_draws = new QCheckBox(tr("Log Draws"));
  log_draws->setToolTip(
      tr("Log every draw call with projection, viewport, scissor, and shader hashes.\n"
         "Also logs EFB clear operations. Use to identify how visual elements are drawn.\n"
         "Open Log Configuration and enable INFO for Video to see the output.\n"
         "WARNING: generates a LOT of log output — enable briefly, then disable."));
  log_draws->setChecked(ShaderHunter::GetInstance().IsDebugLogging());
  connect(log_draws, &QCheckBox::toggled, this,
          [](bool checked) { ShaderHunter::GetInstance().SetDebugLogging(checked); });
  tools_layout->addWidget(log_draws, 3, 0);
}

void VRPane::OnEmulationStateChanged(Core::State state)
{
  const bool running = state != Core::State::Uninitialized;
  m_page->m_display->setEnabled(!running);
  m_page->m_refresh_rate->setEnabled(!running);
  m_page->m_reference_space_mode->setEnabled(!running);
  m_page->m_multiview->setEnabled(!running);
}

// Reset Screen of the VR menu.
void VRPane::ResetScreen()
{
  {
    Config::ConfigChangeCallbackGuard guard;
    for (const auto* setting : {&Config::GFX_VR_SCREEN_DISTANCE, &Config::GFX_VR_SCREEN_SIZE,
                                &Config::GFX_VR_SCREEN_YAW, &Config::GFX_VR_SCREEN_PITCH})
    {
      Config::SetBaseOrCurrent(*setting, setting->GetDefaultValue());
    }
  }
#ifdef ENABLE_VR
  if (VR::g_openxr)
    VR::g_openxr->RequestFlatScreenReanchor();
#endif
}

void VRPane::ResetSettings()
{
  Config::ConfigChangeCallbackGuard guard;

  const auto reset = [](const auto&... settings) {
    (Config::SetBaseOrCurrent(settings, settings.GetDefaultValue()), ...);
  };

  // Display
  reset(Config::GFX_VR_ENABLE_OPENXR, Config::GFX_VR_FLAT_SCREEN, Config::GFX_VR_STEREO_SCREEN,
        Config::GFX_VR_PRESENTATION_MODE);

  // Screen
  reset(Config::GFX_VR_VIRTUAL_SCREEN, Config::GFX_VR_SCREEN_DISTANCE, Config::GFX_VR_SCREEN_SIZE,
        Config::GFX_VR_SCREEN_YAW, Config::GFX_VR_SCREEN_PITCH, Config::GFX_VR_HUD_THICKNESS,
        Config::GFX_VR_SCREEN_DEPTH, Config::GFX_STEREO_CONVERGENCE, Config::GFX_VR_LOCK_SCREEN);

  // Immersive
  reset(Config::GFX_VR_UNITS_PER_METER, Config::GFX_VR_STEREO_SEPARATION,
        Config::GFX_VR_ENABLE_LEAN_BACK_ANGLE, Config::GFX_VR_LEAN_BACK_ANGLE,
        Config::GFX_VR_ENABLE_CAMERA_FORWARD, Config::GFX_VR_CAMERA_FORWARD,
        Config::GFX_VR_ENABLE_CAMERA_HEIGHT, Config::GFX_VR_CAMERA_HEIGHT,
        Config::GFX_VR_ENABLE_CAMERA_ANCHOR, Config::GFX_VR_CAMERA_ANCHOR_SMOOTHING,
        Config::GFX_VR_ENABLE_CONTROLLER_ANCHOR, Config::GFX_VR_REMOVE_BARS);

  // Headset
  reset(Config::GFX_VR_REQUESTED_REFRESH_RATE, Config::GFX_VR_RESOLUTION_SCALE,
        Config::GFX_VR_EAGER_HEARTBEAT, Config::GFX_VR_GAMMA, Config::GFX_VR_FOVEATION_LEVEL,
        Config::GFX_VR_FOVEATION_DYNAMIC, Config::GFX_VR_PASSTHROUGH);

  // Menu and Pointers
  reset(Config::GFX_VR_POINTER_BEAMS, Config::GFX_VR_TOASTS);

  // Advanced
  reset(Config::GFX_VR_REFERENCE_SPACE_MODE, Config::GFX_VR_TRACKING_MODE,
        Config::GFX_VR_FORCED_VBI_FREQUENCY, Config::GFX_VR_AUTO_VBI_FROM_HMD,
        Config::GFX_VR_USE_VULKAN_MULTIVIEW, Config::GFX_VR_EFB_FOVEATION,
        Config::GFX_VR_MIRROR_VIEW, Config::GFX_VR_PANES_ON_SCREEN,
        Config::GFX_VR_ORTHO_SCISSOR_FIX, Config::GFX_VR_DISABLE_CPU_CULL,
        Config::GFX_VR_DETECT_SKYBOX, Config::GFX_VR_DETECT_RENDER_TARGETS,
        Config::GFX_VR_FRAME_SIZE_FROM_XFB, Config::GFX_VR_DONT_CLEAR_SCREEN,
        Config::GFX_VR_METROID_THERMAL_VISOR_FIX, Config::GFX_VR_METROID_D3D_THERMAL_PALETTE_FIX,
        Config::GFX_VR_CLEAR_EFB_COPIES, Config::GFX_VR_HEAD_LOCKED_CURVATURE,
        Config::GFX_VR_LOAD_CUSTOM_SHADERS, Config::GFX_VR_PASSTHROUGH_REMOVE_BLACK_BG,
        Config::GFX_VR_PASSTHROUGH_REMOVE_BLACK_CLEARS, Config::GFX_VR_PASSTHROUGH_SCENE_OPACITY,
        Config::GFX_VR_PASSTHROUGH_COVERAGE_MODE);
  // Unset rather than written, so Immersive VR's own defaults apply again
  // (GetVRExactScreenDepth / GetVRAutoNativeEfbEffects).
  Config::DeleteKey(Config::LayerType::Base, Config::GFX_VR_EXACT_SCREEN_DEPTH);
  Config::DeleteKey(Config::LayerType::Base, Config::GFX_VR_AUTO_NATIVE_EFB_EFFECTS);
}
