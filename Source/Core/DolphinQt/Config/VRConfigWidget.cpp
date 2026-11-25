// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/VRConfigWidget.h"

#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <utility>

#include <fmt/format.h>

#include <QGroupBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/Config/Layer.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"

#include "Core/Config/GraphicsSettings.h"
#include "Core/ConfigLoaders/GameConfigLoader.h"
#include "Core/ConfigManager.h"
#include "DolphinQt/Config/ConfigControls/ConfigBool.h"
#include "DolphinQt/Config/ConfigControls/ConfigChoice.h"
#include "DolphinQt/Config/GameConfigEdit.h"
#include "DolphinQt/QtUtils/QtUtils.h"
#include "DolphinQt/QtUtils/WrapInScrollArea.h"
#include "DolphinQt/Settings.h"
#include "DolphinQt/Settings/VRPane.h"
#include "VideoCommon/VideoConfig.h"

namespace
{
constexpr std::string_view VR_SECTION_NAME = "Graphics.VR";
constexpr std::string_view LEGACY_VR_SECTION_NAME = "GFX.VR";

Config::Location GetVRLocation(const std::string& key)
{
  return {Config::System::GFX, "VR", key};
}

// Common::IniFile cannot round-trip the repeated `$Name` records used by the custom VR override
// sections. Rewriting a per-game VR setting through IniFile therefore strips the names and merges
// the override bodies. Replace only the two VR settings sections as text and preserve every other
// section byte-for-byte.
std::string ReadFileWithoutVRSettingsSections(const std::string& path)
{
  std::ifstream file(path);
  if (!file.is_open())
    return {};

  std::ostringstream out;
  bool skipping = false;
  std::string line;
  while (std::getline(file, line))
  {
    std::string trimmed = line;
    if (!trimmed.empty() && trimmed.back() == '\r')
      trimmed.pop_back();

    if (trimmed == "[Graphics.VR]" || trimmed == "[GFX.VR]")
    {
      skipping = true;
      continue;
    }
    if (skipping && !trimmed.empty() && trimmed.front() == '[')
      skipping = false;

    if (!skipping)
      out << line << '\n';
  }
  return out.str();
}

class VRGameConfigLayerLoader final : public Config::ConfigLayerLoader
{
public:
  VRGameConfigLayerLoader(std::string game_id, std::optional<u16> revision, bool global)
      : ConfigLayerLoader(global ? Config::LayerType::GlobalGame : Config::LayerType::LocalGame),
        m_game_id(std::move(game_id)), m_revision(revision), m_global(global)
  {
  }

  void Load(Config::Layer* layer) override
  {
    LoadFiles(layer);
    m_known = GetValues(*layer);
  }

  // Since they were loaded or saved here.
  bool IsFileChanged() const { return GetValues(*ReadFiles()) != m_known; }

  // Only what was changed here goes into the file: the rest is what the file has by now.
  void Save(Config::Layer* layer) override
  {
    if (m_global)
      return;

    const Values mine = GetValues(*layer);
    const auto changed = [&](const Config::Location& location) {
      const auto in = [&location](const Values& values) {
        const auto it = values.find(location);
        return it == values.end() ? std::nullopt : std::optional<std::string>(it->second);
      };
      return in(mine) != in(m_known);
    };
    const Values files = GetValues(*ReadFiles());
    for (const auto& [location, value] : files)
    {
      if (!changed(location))
        layer->Set(location, value);
    }
    for (const auto& [location, value] : mine)
    {
      if (!files.contains(location) && !changed(location))
        layer->DeleteKey(location);
    }
    m_known = GetValues(*layer);
    Write(*layer);
  }

private:
  using Values = std::map<Config::Location, std::string>;

  static Values GetValues(const Config::Layer& layer)
  {
    Values values;
    for (const auto& [location, value] : layer.GetLayerMap())
    {
      if (value)
        values.emplace(location, *value);
    }
    return values;
  }

  std::unique_ptr<Config::Layer> ReadFiles() const
  {
    auto layer = std::make_unique<Config::Layer>(GetLayer());
    LoadFiles(layer.get());
    return layer;
  }

  void LoadFiles(Config::Layer* layer) const
  {
    Common::IniFile ini;
    const std::string directory = m_global ? File::GetSysDirectory() + GAMESETTINGSVR_DIR DIR_SEP :
                                             File::GetUserPath(D_GAMESETTINGSVR_IDX);

    for (const std::string& filename : ConfigLoaders::GetGameIniFilenames(m_game_id, m_revision))
    {
      ini.Load(directory + filename, true);
    }

    LoadSection(layer, ini.GetSection(LEGACY_VR_SECTION_NAME));
    LoadSection(layer, ini.GetSection(VR_SECTION_NAME));

    // Keep old per-game files useful in the new controls. They are written back using the current
    // names the next time the user changes a setting.
    const Config::Location passthrough = GetVRLocation("Passthrough");
    const Config::Location legacy_ar_mode = GetVRLocation("ARMode");
    if (!layer->Exists(passthrough))
    {
      if (const std::optional<bool> value = layer->Get<bool>(legacy_ar_mode))
        layer->Set(passthrough, *value);
    }

    const Config::Location forced_vbi = GetVRLocation("ForcedVBIFrequency");
    const Config::Location legacy_auto_vbi = GetVRLocation("AutoVBIFromHMD");
    if (!layer->Exists(forced_vbi))
    {
      if (const std::optional<bool> value = layer->Get<bool>(legacy_auto_vbi))
      {
        layer->Set(forced_vbi, *value ? Config::GFX_VR_FORCED_VBI_FREQUENCY_90 :
                                        Config::GFX_VR_FORCED_VBI_FREQUENCY_OFF);
      }
    }
  }

  void Write(const Config::Layer& layer) const
  {
    const std::string path = GetSavePath();
    std::string base = ReadFileWithoutVRSettingsSections(path);
    while (!base.empty() &&
           (base.back() == '\n' || base.back() == '\r' || base.back() == ' '))
    {
      base.pop_back();
    }
    if (!base.empty())
      base += '\n';

    std::ostringstream out;
    out << base;
    bool wrote_header = false;

    for (const auto& [location, value] : layer.GetLayerMap())
    {
      if (location.system != Config::System::GFX || location.section != "VR" || !value ||
          location.key == "ARMode" || location.key == "AutoVBIFromHMD")
      {
        continue;
      }

      if (!wrote_header)
      {
        out << '[' << VR_SECTION_NAME << "]\n";
        wrote_header = true;
      }
      out << location.key << " = " << *value << '\n';
    }

    File::CreateFullPath(path);
    const std::string contents = out.str();
    if (contents.empty())
    {
      File::Delete(path, File::IfAbsentBehavior::NoConsoleWarning);
      return;
    }

    std::ofstream file(path, std::ios::trunc);
    file << contents;
  }

  static void LoadSection(Config::Layer* layer, const Common::IniFile::Section* section)
  {
    if (!section)
      return;

    for (const auto& [key, value] : section->GetValues())
      layer->Set(GetVRLocation(key), value);
  }

  std::string GetSavePath() const
  {
    const std::string directory = File::GetUserPath(D_GAMESETTINGSVR_IDX);
    if (m_revision)
    {
      const std::string revision_path =
          fmt::format("{}{}r{}.ini", directory, m_game_id, *m_revision);
      if (File::Exists(revision_path))
        return revision_path;
    }
    return fmt::format("{}{}.ini", directory, m_game_id);
  }

  const std::string m_game_id;
  const std::optional<u16> m_revision;
  const bool m_global;
  Values m_known;
};

void ResetTabPages(QTabWidget* tab)
{
  while (tab->count() > 0)
  {
    QWidget* const page = tab->widget(0);
    tab->removeTab(0);
    delete page;
  }
}

void PopulateVRConfigTab(QTabWidget* tab, const std::string& path, const std::string& game_id,
                         std::optional<u16> revision, bool read_only)
{
  for (const std::string& filename : ConfigLoaders::GetGameIniFilenames(game_id, revision))
  {
    const std::string ini_path = path + filename;
    if (File::Exists(ini_path))
    {
      auto* edit = new GameConfigEdit(nullptr, QString::fromStdString(ini_path), read_only);
      tab->addTab(edit, QString::fromStdString(filename));
    }
  }
}

bool HasTab(const QTabWidget* tab, const QString& name)
{
  for (int i = 0; i < tab->count(); ++i)
  {
    if (tab->tabText(i) == name)
      return true;
  }
  return false;
}
}  // namespace

VRConfigWidget::VRConfigWidget(std::string game_id, std::optional<u16> revision, QWidget* parent)
    : QWidget(parent), m_game_id(std::move(game_id)), m_revision(revision),
      m_global_layer(std::make_unique<Config::Layer>(
          std::make_unique<VRGameConfigLayerLoader>(m_game_id, m_revision, true)))
{
  auto loader = std::make_unique<VRGameConfigLayerLoader>(m_game_id, m_revision, false);
  m_is_file_changed = [loader = loader.get()] { return loader->IsFileChanged(); };
  m_layer = std::make_unique<Config::Layer>(std::move(loader));
  CreateWidgets();
  m_saved_layer_state = GetLayerState();

  connect(&Settings::Instance(), &Settings::ConfigChanged, this, [this] { SaveSettings(); });

  // Config controls update their bold state on ConfigChanged. Delay this for the same reason as
  // Game Config: fonts are not reliably propagated until the widget hierarchy has been created.
  QTimer::singleShot(100, this, [] { Config::OnConfigChanged(); });
}

VRConfigWidget::~VRConfigWidget()
{
  m_layer->Save();
  if (SConfig::GetInstance().GetGameID() == m_game_id)
    SConfig::GetInstance().ReloadGameVRConfigOverrides();
}

void VRConfigWidget::CreateWidgets()
{
  Config::Layer* const layer = m_layer.get();

  auto* settings_page = new QWidget;
  auto* page = new VRSettingsPage(this, new QVBoxLayout(settings_page), layer,
                                  m_global_layer.get());
  page->AddDisplay();
  page->AddScreen();
  page->AddImmersive();
  page->AddHeadset();
  page->AddAdvanced();

  // Immersive VR only changes these two options' defaults (see GetVRExactScreenDepth): when
  // neither this game's own file nor GFX.ini sets one, show the Immersive default. The options
  // stay editable, and a change here is saved for this game.
  const auto update_immersive_screen_options = [layer, page] {
    const bool immersive_vr =
        page->Read(Config::GFX_VR_PRESENTATION_MODE) == OpenXRPresentationMode::Immersive;
    if (!immersive_vr)
      return;
    // A bundled profile's value does not count in Immersive VR (ApplyGameVRConfigOverrides
    // skips it there); only this game's own file and GFX.ini do.
    const auto inherits_default = [layer](const Config::Info<bool>& setting) {
      const auto& location = setting.GetLocation();
      return !layer->Exists(location) &&
             !Config::GetLayer(Config::LayerType::Base)->Exists(location);
    };
    if (inherits_default(Config::GFX_VR_EXACT_SCREEN_DEPTH))
    {
      const QSignalBlocker blocker(page->m_exact_screen_depth);
      page->m_exact_screen_depth->setChecked(false);
    }
    if (inherits_default(Config::GFX_VR_AUTO_NATIVE_EFB_EFFECTS))
    {
      const QSignalBlocker blocker(page->m_auto_native_efb_effects);
      page->m_auto_native_efb_effects->setChecked(true);
    }
  };
  connect(page->m_display, &QComboBox::currentIndexChanged, this,
          [update_immersive_screen_options](int) { update_immersive_screen_options(); });
  connect(&Settings::Instance(), &Settings::ConfigChanged, this,
          update_immersive_screen_options);
  update_immersive_screen_options();

  page->Finish();

  auto* editor_page = new QWidget;
  auto* editor_layout = new QVBoxLayout(editor_page);
  auto* default_group = new QGroupBox(tr("VR Default Config (Read Only)"));
  auto* default_layout = new QVBoxLayout(default_group);
  m_default_tab = new QTabWidget;
  default_layout->addWidget(m_default_tab);
  auto* local_group = new QGroupBox(tr("VR User Config"));
  auto* local_layout = new QVBoxLayout(local_group);
  m_local_tab = new QTabWidget;
  local_layout->addWidget(m_local_tab);
  editor_layout->addWidget(default_group);
  editor_layout->addWidget(local_group);

  auto* tabs = new QTabWidget;
  tabs->addTab(GetWrappedWidget(settings_page), tr("Settings"));
  const int editor_index = tabs->addTab(editor_page, tr("Editor"));

  connect(tabs, &QTabWidget::currentChanged, this, [this, editor_index](int index) {
    if (index == editor_index)
    {
      m_layer->Save();
      RefreshEditorTabs();
    }

    if (m_prev_tab_index == editor_index)
      ReloadSettings();

    m_prev_tab_index = index;
  });

  const QString help_message =
      tr("Italics mark default game settings and bold marks user settings. "
         "Right-click a control to remove its user setting.");
  auto* help_label = new QLabel(tr("VR settings for this game only."));
  auto* help_widget = QtUtils::CreateIconWarning(this, QStyle::SP_MessageBoxQuestion, help_label);
  help_widget->setToolTip(help_message);

  auto* root_layout = new QVBoxLayout;
  root_layout->addWidget(help_widget);
  root_layout->addWidget(tabs);
  setLayout(root_layout);

  RefreshEditorTabs();
}

void VRConfigWidget::SaveSettings()
{
  if (GetLayerState() == m_saved_layer_state)
  {
    // The VR menu changed it: the controls show it, and it is not what they save.
    if (m_is_file_changed())
    {
      m_layer->DeleteAllKeys();
      m_layer->Load();
      m_saved_layer_state = GetLayerState();
      Config::OnConfigChanged();
    }
    return;
  }

  m_layer->Save();
  m_saved_layer_state = GetLayerState();
  if (SConfig::GetInstance().GetGameID() == m_game_id)
    SConfig::GetInstance().ReloadGameVRConfigOverrides();
}

void VRConfigWidget::ReloadSettings()
{
  m_layer->DeleteAllKeys();
  m_layer->Load();
  m_saved_layer_state = GetLayerState();
  Config::OnConfigChanged();
  if (SConfig::GetInstance().GetGameID() == m_game_id)
    SConfig::GetInstance().ReloadGameVRConfigOverrides();
}

void VRConfigWidget::RefreshEditorTabs()
{
  if (!m_default_tab || !m_local_tab || m_game_id.empty())
    return;

  ResetTabPages(m_default_tab);
  ResetTabPages(m_local_tab);

  PopulateVRConfigTab(m_default_tab, File::GetSysDirectory() + GAMESETTINGSVR_DIR DIR_SEP,
                      m_game_id, m_revision, true);
  PopulateVRConfigTab(m_local_tab, File::GetUserPath(D_GAMESETTINGSVR_IDX), m_game_id, m_revision,
                      false);

  const QString local_tab_name = QString::fromStdString(m_game_id + ".ini");
  if (!HasTab(m_local_tab, local_tab_name))
  {
    m_local_tab->addTab(
        new GameConfigEdit(nullptr, QString::fromStdString(GetLocalINIPath()), false),
        local_tab_name);
  }
}

std::string VRConfigWidget::GetLocalINIPath() const
{
  return File::GetUserPath(D_GAMESETTINGSVR_IDX) + m_game_id + ".ini";
}

std::string VRConfigWidget::GetLayerState() const
{
  std::string state;
  for (const auto& [location, value] : m_layer->GetLayerMap())
  {
    const auto append_part = [&state](std::string_view part) {
      state += std::to_string(part.size());
      state += ':';
      state += part;
    };

    state += std::to_string(static_cast<int>(location.system));
    state += ':';
    append_part(location.section);
    append_part(location.key);
    state += value ? '1' : '0';
    if (value)
      append_part(*value);
  }
  return state;
}
