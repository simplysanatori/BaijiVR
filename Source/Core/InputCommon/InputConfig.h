// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/IniFile.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/DynamicInputTextureManager.h"

namespace ControllerEmu
{
class EmulatedController;
}

class InputConfig
{
public:
  InputConfig(const std::string& ini_name, const std::string& gui_name,
              const std::string& profile_directory_name, const std::string& profile_key);

  ~InputConfig();

  bool LoadConfig();
  void SaveConfig();

  // For a controller that may have another mapping than the one in the file. `load` is given the
  // file's and loads that one or another; `save` saves the mapping where it is from and changes
  // the file's section only if the mapping is the file's. They return false for a controller that
  // is not theirs.
  using Override = bool (*)(int index, Common::IniFile::Section* section);
  void SetOverrides(Override load, Override save)
  {
    m_load_override = load;
    m_save_override = save;
  }

  // Changes when a mapping was changed by something else than the window that shows it.
  u32 GetGeneration() const { return m_generation; }
  void OnMappingChanged()
  {
    ++m_generation;
    ++m_changes;
  }
  // Changes when a mapping was changed or saved by anything.
  u32 GetChanges() const { return m_changes; }

  template <typename T, typename... Args>
  void CreateController(Args&&... args)
  {
    m_controllers.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
  }

  ControllerEmu::EmulatedController* GetController(int index) const;
  void ClearControllers();
  bool ControllersNeedToBeCreated() const;
  bool IsControllerControlledByGamepadDevice(int index) const;

  std::string GetGUIName() const { return m_gui_name; }
  std::string GetProfileKey() const { return m_profile_key; }
  std::string GetProfileDirectoryName() const { return m_profile_directory_name; }
  std::string GetUserProfileDirectoryPath() const;
  std::string GetSysProfileDirectoryPath() const;
  int GetControllerCount() const;

  // These should be used after creating all controllers and before clearing them, respectively.
  void RegisterHotplugCallback();
  void UnregisterHotplugCallback();

  void GenerateControllerTextures(const Common::IniFile& file);
  void GenerateControllerTextures();

private:
  Common::EventHook m_hotplug_event_hook;
  std::vector<std::unique_ptr<ControllerEmu::EmulatedController>> m_controllers;
  const std::string m_ini_name;
  const std::string m_gui_name;
  const std::string m_profile_directory_name;
  const std::string m_profile_key;
  Override m_load_override = nullptr;
  Override m_save_override = nullptr;
  std::atomic<u32> m_generation{0};
  std::atomic<u32> m_changes{0};
  InputCommon::DynamicInputTextureManager m_dynamic_input_tex_config_manager;
};
