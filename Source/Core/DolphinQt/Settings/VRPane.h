// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <QFont>
#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>

#include "Common/Config/Config.h"
#include "Common/Config/ConfigInfo.h"
#include "Common/Config/Layer.h"

#include "DolphinQt/Config/ConfigControls/ConfigBool.h"
#include "DolphinQt/Config/ConfigControls/ConfigChoice.h"
#include "DolphinQt/Config/ConfigControls/ConfigFloatSlider.h"
#include "DolphinQt/Config/ConfigControls/ConfigSlider.h"

class CullingCodeFinderWidget;
class QBoxLayout;
class QGridLayout;
class QVBoxLayout;
class ShaderHunterWidget;
enum class OpenXRReferenceSpaceMode : int;
enum class OpenXRPresentationMode : int;

namespace Core
{
enum class State;
}

// The VR settings as the settings window's VR page and a game's own VR page both show them. The
// game's page gives a layer, and the settings are then that layer's.
// Every row names the displays it applies to and is hidden in the others.
class VRSettingsPage final : public QObject
{
public:
  enum Display : unsigned
  {
    Flat = 1 << 0,
    Immersive = 1 << 1,
    Screen3D = 1 << 2,
    // A 2D screen in the headset. Not offered, but an old config can still ask for it.
    Screen2D = 1 << 3,
  };
  static constexpr unsigned TWO_EYES = Immersive | Screen3D;
  static constexpr unsigned HEADSET = TWO_EYES | Screen2D;
  static constexpr unsigned ALL = HEADSET | Flat;

  struct Text
  {
    QString label;
    QString description;
  };

  VRSettingsPage(QWidget* parent, QBoxLayout* layout, Config::Layer* layer = nullptr,
                 Config::Layer* global_layer = nullptr);

  void AddDisplay();
  QGridLayout* AddScreen();
  void AddImmersive();
  void AddHeadset();
  // Returns the layout for further groups in the collapsed part.
  QVBoxLayout* AddAdvanced();
  // Call once every row is there.
  void Finish();

  QGridLayout* AddGroup(const QString& title, unsigned displays, QBoxLayout* layout = nullptr);
  void AddRow(QGridLayout* layout, unsigned displays, QWidget* label, QWidget* control,
              QWidget* value = nullptr);
  ConfigBool* AddBool(QGridLayout* layout, unsigned displays, const Config::Info<bool>& setting);
  // `enable`, if given, is a checkbox in the place of the label.
  ConfigFloatSlider*
  AddSlider(QGridLayout* layout, unsigned displays, const Config::Info<float>& setting,
            float minimum, float maximum, float step, std::function<QString(float)> format,
            const Config::Info<bool>* enable = nullptr,
            ConfigFloatSlider::ScaleMode scale = ConfigFloatSlider::ScaleMode::Linear);

  template <typename T>
  ConfigChoiceMap<T>* AddChoice(QGridLayout* layout, unsigned displays,
                                const Config::Info<T>& setting,
                                const std::vector<std::pair<QString, T>>& options)
  {
    auto* choice = Prepare(new ConfigChoiceMap<T>(options, setting, m_layer, m_global_layer));
    AddRow(layout, displays, MakeLabel(setting.GetLocation()), choice);
    return choice;
  }

  // Gives the control its tooltip, and keeps scrolling the page from changing it.
  template <typename Control>
  Control* Prepare(Control* control)
  {
    const Config::Location location = control->GetLocation();
    const Text text = GetText(location);
    control->SetTitle(text.label);
    control->SetDescription(text.description);
    control->setFocusPolicy(Qt::StrongFocus);
    control->installEventFilter(this);

    if (m_global_layer && m_global_layer->Exists(location))
    {
      QFont font = control->font();
      font.setItalic(true);
      control->setFont(font);
    }
    return control;
  }

  static Text GetText(const Config::Location& location);
  static QLabel* MakeLabel(const Config::Location& location);

  // The game's own value, then the bundled one for the game, then the base one.
  template <typename T>
  T Read(const Config::Info<T>& setting) const
  {
    if (!m_layer)
      return Config::Get(setting);
    if (m_layer->Exists(setting.GetLocation()))
      return m_layer->Get(setting);
    if (m_global_layer && m_global_layer->Exists(setting.GetLocation()))
      return m_global_layer->Get(setting);
    return Config::GetBase(setting);
  }

  ConfigChoiceMap<OpenXRPresentationMode>* m_display = nullptr;
  ConfigChoiceMap<int>* m_refresh_rate = nullptr;
  ConfigChoiceMap<OpenXRReferenceSpaceMode>* m_reference_space_mode = nullptr;
  ConfigChoiceMap<int>* m_forced_vbi_frequency = nullptr;
  ConfigBool* m_multiview = nullptr;
  ConfigBool* m_exact_screen_depth = nullptr;
  ConfigBool* m_auto_native_efb_effects = nullptr;
  ConfigBool* m_thermal_visor_fix = nullptr;

private:
  bool eventFilter(QObject* object, QEvent* event) override;

  Display GetDisplay() const;
  void OnDisplayChosen();
  void UpdateDisplay();
  void ShowOnlyIn(unsigned displays, QWidget* widget);

  QBoxLayout* m_layout;
  Config::Layer* m_layer;
  Config::Layer* m_global_layer;
  QLabel* m_display_text = nullptr;
  std::vector<std::pair<unsigned, QWidget*>> m_widgets;
};

class VRPane final : public QWidget
{
  Q_OBJECT
public:
  explicit VRPane(QWidget* parent = nullptr);

private:
  void AddScreenRows(QGridLayout* layout);
  void AddMenuAndPointers();
  void AddAutosave();
  void AddTools(QVBoxLayout* layout);

  void OnEmulationStateChanged(Core::State state);
  void ResetScreen();
  void ResetSettings();

  VRSettingsPage* m_page = nullptr;
  QPointer<CullingCodeFinderWidget> m_culling_finder_widget;
  QPointer<ShaderHunterWidget> m_shader_hunter_widget;
};
