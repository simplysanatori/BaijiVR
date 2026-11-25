// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

// VR to Flat and back with a game running: its state is saved to a file of its own, the game is
// stopped and started again in the other mode, and the state is loaded (see Core/ModeSwitch.h). The
// next start uses the mode last switched to. In Flat, the VR button in the menu bar and Menu held
// for 3 s go back.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QObject>
#include <QPointer>

#include "Common/Config/Config.h"
#include "Common/HookableEvent.h"
#include "Core/Boot/Boot.h"
#include "Core/ModeSwitch.h"
#include "DiscIO/Enums.h"
#include "DiscIO/RiivolutionParser.h"

class QMenuBar;
class QMessageBox;
class QTimer;
class QToolButton;
class QWidget;

namespace Core
{
class System;
enum class State;
}  // namespace Core

class VRModeSwitch final : public QObject
{
public:
  struct Actions
  {
    // Stops the game, without a question.
    std::function<void()> stop;
    std::function<void(std::unique_ptr<BootParameters>)> boot;
  };

  VRModeSwitch(Core::System& system, QWidget* main, QMenuBar* menu_bar, Actions actions);
  ~VRModeSwitch() override;

  // From the save to the check of the load.
  bool IsSwitching() const { return m_phase != Phase::Idle; }

  // MainWindow::StartGame, around BootManager::BootCore.
  void BeforeBoot(const BootParameters& boot);
  void AfterBoot(bool booted);

  // Discs that may be in the drive from now on.
  void AddDiscs(const std::vector<std::string>& paths);

  // MainWindow::OnStopComplete. True: the game is started again.
  bool OnStopComplete(bool cancelled);

  void Request(ModeSwitch::Mode mode);

private:
  enum class Phase
  {
    Idle,
    Stopping,
    Booting,
  };

  // How the running game was started.
  struct Recipe
  {
    enum class Kind
    {
      None,
      Disc,
      File,
      Title,
      IPL,
    };

    Kind kind = Kind::None;
    std::string path;
    std::vector<std::string> discs;        // the ones to change to, as the boot has them
    std::vector<std::string> other_discs;  // the ones the player changed to
    std::vector<DiscIO::Riivolution::Patch> patches;
    std::string disc_game_id;
    u8 disc_number = 0;
    u64 title_id = 0;
    DiscIO::Region region = DiscIO::Region::Unknown;
    std::string game_id;  // as it ran after the boot
  };

  static Recipe MakeRecipe(const BootParameters& boot);
  // The disc in the drive becomes the recipe's. False: it is not known which one that is.
  bool UseCurrentDisc(Recipe* recipe) const;
  static std::unique_ptr<BootParameters> MakeBoot(const Recipe& recipe);

  // Empty when the running game can be switched, or else why not.
  std::string GetRefusal() const;
  bool SaveState();
  void Boot();
  // The game runs again: its time, and whether it is in the headset.
  void OnRunning(u64 ticks, bool in_vr);
  // The state's file stays.
  void Fail(const std::string& message);

  static void Remember(ModeSwitch::Mode mode, OpenXRPresentationMode running);
  static void Arm(ModeSwitch::Mode mode, OpenXRPresentationMode wanted);
  static void Disarm();
  static void OnConfigChanged();

  void OnCoreState(Core::State state);

  ModeSwitch::Mode GetMode() const;
  void UpdateButton();
  void Poll();
  void Ask();

  Core::System& m_system;
  QPointer<QWidget> m_main;
  Actions m_actions;

  Phase m_phase = Phase::Idle;
  ModeSwitch::Mode m_target = ModeSwitch::Mode::Flat;
  bool m_fell_back = false;
  Recipe m_recipe;
  std::string m_state_path;
  u64 m_state_ticks = 0;
  // The display the game had in VR, for the way back.
  std::string m_vr_game_id;
  OpenXRPresentationMode m_vr_display = OpenXRPresentationMode::Vulkan;

  std::atomic<bool> m_boot_pending{false};
  std::atomic<bool> m_in_vr{false};  // the running game
  Common::EventHook m_core_state_hook;
  Config::ConfigChangedCallbackID m_config_callback;

  QPointer<QToolButton> m_button;
  QTimer* m_timer = nullptr;
  ModeSwitch::MenuHold m_menu_hold;
  ModeSwitch::Answer m_answer;
  QPointer<QMessageBox> m_question;
};
