// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DolphinQt/VRModeSwitch.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <utility>
#include <variant>

#include <QAbstractButton>
#include <QApplication>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QWidget>

#include <fmt/format.h>

#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"

#include "Core/AchievementManager.h"
#include "Core/Autosave.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HW/Memmap.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/State.h"
#include "Core/System.h"

#include "DiscIO/Volume.h"
#include "DiscIO/VolumeWad.h"

#include "DolphinQt/Host.h"
#include "DolphinQt/QtUtils/ModalMessageBox.h"
#include "DolphinQt/QtUtils/QueueOnObject.h"
#include "DolphinQt/Settings.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"

#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/VideoConfig.h"
#ifdef ENABLE_VR
#include "VideoCommon/VR/OpenXRManager.h"
#endif

using ModeSwitch::Mode;

namespace
{
constexpr int POLL_MS = 50;
constexpr u32 MESSAGE_MS = 6000;

// The VR display that Flat was switched to from. Vulkan: none.
const Config::Info<OpenXRPresentationMode> LAST_VR_MODE{
    {Config::System::GFX, "VR", "LastVRMode"}, OpenXRPresentationMode::Vulkan};

// What OnConfigChanged holds the running game to. It is called on any thread.
enum class Held
{
  Nothing,
  Flat,
  VR,
};
std::atomic<Held> s_held{Held::Nothing};
std::atomic<OpenXRPresentationMode> s_wanted{OpenXRPresentationMode::Vulkan};

ModeSwitch::Displays GetDisplays()
{
  return {Config::GetBase(Config::GFX_VR_PRESENTATION_MODE), Config::GetBase(LAST_VR_MODE)};
}

const char* GetName(Mode mode)
{
  return mode == Mode::VR ? "VR" : "Flat";
}

bool IsInHeadset()
{
#ifdef ENABLE_VR
  return VR::g_openxr && VR::g_openxr->GetSwapchain();
#else
  return false;
#endif
}

// Where the OpenXR loader looks on Linux.
bool IsOpenXRAtHand()
{
#ifdef ENABLE_VR
  if (const char* const runtime = std::getenv("XR_RUNTIME_JSON"); runtime && *runtime)
    return File::Exists(runtime);
  const auto get = [](const char* name, std::string otherwise) {
    const char* const value = std::getenv(name);
    return value && *value ? std::string(value) : std::move(otherwise);
  };
  const std::string user = get("XDG_CONFIG_HOME", get("HOME", "") + "/.config");
  return File::Exists(user + "/openxr/1/active_runtime.json") ||
         File::Exists("/etc/xdg/openxr/1/active_runtime.json") ||
         File::Exists("/etc/openxr/1/active_runtime.json");
#else
  return false;
#endif
}

std::string GetStatePath(const std::string& game_id)
{
  return ModeSwitch::GetStatePath(File::GetUserPath(D_STATESAVES_IDX), game_id);
}

std::string GetFileName(const std::string& path)
{
  return path.substr(path.find_last_of('/') + 1);
}

// Not while a switch needs one.
void CleanStates()
{
  for (const std::string& path :
       ModeSwitch::GetStatesToDelete(File::GetUserPath(D_STATESAVES_IDX), std::time(nullptr)))
  {
    INFO_LOG_FMT(CORE, "Switch: deleting the kept state {}", GetFileName(path));
    File::Delete(path);
  }
}

s64 Now()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct Buttons
{
  bool menu = false;
  bool a = false;
  bool b = false;
};

// Of all of SDL's gamepads: Steam may leave one behind that no longer reports. (The HotkeyScheduler
// updates the inputs. The devices are let go of under the lock.) False: not now.
bool ReadGamepads(Buttons* buttons)
{
  if (!g_controller_interface.IsInit())
    return false;
  std::unique_lock lock(g_controller_interface.GetDevicesMutex(), std::try_to_lock);
  if (!lock.owns_lock())
    return false;
  for (const auto& device : g_controller_interface.GetAllDevices())
  {
    if (device->GetSource() != "SDL")
      continue;
    const auto is_down = [&device](std::string_view name) {
      const auto* const input = device->FindInput(name);
      return input && input->GetState() > 0.5;
    };
    buttons->menu |= is_down("Start");
    buttons->a |= is_down("Button S");
    buttons->b |= is_down("Button E");
  }
  return true;
}

// Something else has the controller: a dialog, a menu, or the window where buttons are mapped.
bool IsOtherWindowOpen()
{
  if (QApplication::activePopupWidget() || QApplication::activeModalWidget())
    return true;
  return std::ranges::any_of(QApplication::topLevelWidgets(), [](const QWidget* window) {
    return window->isVisible() &&
           (window->inherits("MappingWindow") || window->inherits("IOWindow"));
  });
}
}  // namespace

VRModeSwitch::VRModeSwitch(Core::System& system, QWidget* main, QMenuBar* menu_bar, Actions actions)
    : QObject(main), m_system(system), m_main(main), m_actions(std::move(actions))
{
  ModeSwitch::SetHandler([this](Mode mode) {
    // Out of the host job it comes in: the core dispatches those as it stops.
    QueueOnObject(this, [this, mode] { Request(mode); });
  });
  m_core_state_hook =
      Core::AddOnStateChangedCallback([this](Core::State state) { OnCoreState(state); });
  m_config_callback = Config::AddConfigChangedCallback(&VRModeSwitch::OnConfigChanged);

  if (!IsOpenXRAtHand())
    return;

  m_button = new QToolButton(menu_bar);
  // The gamepad's keys are not to stop at it.
  m_button->setFocusPolicy(Qt::NoFocus);
  menu_bar->setCornerWidget(m_button, Qt::TopRightCorner);
  connect(m_button, &QAbstractButton::clicked, this,
          [this] { Request(GetMode() == Mode::VR ? Mode::Flat : Mode::VR); });
  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this,
          &VRModeSwitch::UpdateButton);
  connect(&Settings::Instance(), &Settings::ConfigChanged, this, &VRModeSwitch::UpdateButton);
  UpdateButton();

  if (!::Host::IsGameMode())
    return;
  m_timer = new QTimer(this);
  m_timer->setInterval(POLL_MS);
  connect(m_timer, &QTimer::timeout, this, &VRModeSwitch::Poll);
  m_timer->start();
}

VRModeSwitch::~VRModeSwitch()
{
  ModeSwitch::SetHandler(nullptr);
  Config::RemoveConfigChangedCallback(m_config_callback);
  Disarm();
}

// ---- The game ---------------------------------------------------------------------------------

VRModeSwitch::Recipe VRModeSwitch::MakeRecipe(const BootParameters& boot)
{
  Recipe recipe;
  recipe.patches = boot.riivolution_patches;
  const auto set_disc = [&recipe](const BootParameters::Disc& disc) {
    recipe.path = disc.path;
    recipe.discs = disc.auto_disc_change_paths;
    if (disc.volume)
    {
      recipe.disc_game_id = disc.volume->GetGameID();
      recipe.disc_number = disc.volume->GetDiscNumber().value_or(0);
    }
  };

  if (const auto* const disc = std::get_if<BootParameters::Disc>(&boot.parameters))
  {
    recipe.kind = Recipe::Kind::Disc;
    set_disc(*disc);
  }
  else if (const auto* const file = std::get_if<BootParameters::Executable>(&boot.parameters))
  {
    recipe.kind = Recipe::Kind::File;
    recipe.path = file->path;
  }
  else if (const auto* const wad = std::get_if<DiscIO::VolumeWAD>(&boot.parameters))
  {
    // It is installed as it boots.
    if (const std::optional<u64> title_id = wad->GetTitleID())
    {
      recipe.kind = Recipe::Kind::Title;
      recipe.title_id = *title_id;
    }
  }
  else if (const auto* const title = std::get_if<BootParameters::NANDTitle>(&boot.parameters))
  {
    recipe.kind = Recipe::Kind::Title;
    recipe.title_id = title->id;
  }
  else if (const auto* const ipl = std::get_if<BootParameters::IPL>(&boot.parameters))
  {
    recipe.kind = Recipe::Kind::IPL;
    recipe.region = ipl->region;
    if (ipl->disc)
      set_disc(*ipl->disc);
  }
  return recipe;
}

std::unique_ptr<BootParameters> VRModeSwitch::MakeBoot(const Recipe& recipe)
{
  switch (recipe.kind)
  {
  case Recipe::Kind::Disc:
  {
    auto volume = DiscIO::CreateDiscForCore(recipe.path);
    if (!volume)
      return nullptr;
    auto boot = std::make_unique<BootParameters>(
        BootParameters::Disc{recipe.path, std::move(volume), recipe.discs});
    // The ones it was started with: a state goes with the files of its game.
    AddRiivolutionPatches(boot.get(), recipe.patches);
    return boot;
  }
  case Recipe::Kind::File:
    return File::Exists(recipe.path) ? BootParameters::GenerateFromFile(recipe.path) : nullptr;
  case Recipe::Kind::Title:
    return std::make_unique<BootParameters>(BootParameters::NANDTitle{recipe.title_id});
  case Recipe::Kind::IPL:
  {
    if (recipe.path.empty())
      return std::make_unique<BootParameters>(BootParameters::IPL{recipe.region});
    auto volume = DiscIO::CreateDiscForCore(recipe.path);
    if (!volume)
      return nullptr;
    return std::make_unique<BootParameters>(BootParameters::IPL{
        recipe.region, BootParameters::Disc{recipe.path, std::move(volume), recipe.discs}});
  }
  case Recipe::Kind::None:
    break;
  }
  return nullptr;
}

bool VRModeSwitch::UseCurrentDisc(Recipe* recipe) const
{
  if (recipe->disc_game_id.empty())
    return true;

  // A state does not know its disc. The game does: the ID of the one in the drive is at 0.
  u8 number = 0;
  {
    const Core::CPUThreadGuard guard(m_system);
    number = m_system.GetMemory().Read_U8(6);
  }
  if (number == recipe->disc_number)
    return recipe->other_discs.empty();

  std::vector<std::string> paths = recipe->discs;
  paths.insert(paths.end(), recipe->other_discs.begin(), recipe->other_discs.end());
  for (const std::string& path : paths)
  {
    const auto volume = DiscIO::CreateDisc(path);
    if (!volume || volume->GetGameID() != recipe->disc_game_id ||
        volume->GetDiscNumber().value_or(0) != number)
    {
      continue;
    }
    // Patches are for a disc. These were not the ones of this one.
    if (!recipe->patches.empty())
      return false;
    recipe->path = path;
    recipe->disc_number = number;
    recipe->other_discs.clear();
    // The first of them is the one in the drive.
    if (const auto it = std::ranges::find(recipe->discs, path); it != recipe->discs.end())
      std::rotate(recipe->discs.begin(), it, recipe->discs.end());
    else
      recipe->discs.clear();
    return true;
  }
  return false;
}

void VRModeSwitch::AddDiscs(const std::vector<std::string>& paths)
{
  m_recipe.other_discs.insert(m_recipe.other_discs.end(), paths.begin(), paths.end());
}

void VRModeSwitch::BeforeBoot(const BootParameters& boot)
{
  if (m_phase != Phase::Booting)
  {
    m_recipe = MakeRecipe(boot);
    Disarm();
    const ModeSwitch::Displays displays = GetDisplays();
    // A VR display was chosen in the settings: Flat is no longer what was switched to.
    if (displays.base != OpenXRPresentationMode::Vulkan)
      Config::SetBase(LAST_VR_MODE, OpenXRPresentationMode::Vulkan);
    // Flat is for every game, but not over a display on the command line.
    if (ModeSwitch::IsFlatForAll(displays) &&
        Config::GetActiveLayerForConfig(Config::GFX_VR_PRESENTATION_MODE.GetLocation()) !=
            Config::LayerType::CommandLine)
    {
      Arm(Mode::Flat, OpenXRPresentationMode::Vulkan);
    }
  }
  m_boot_pending = true;
}

void VRModeSwitch::AfterBoot(bool booted)
{
  if (booted)
  {
    m_recipe.game_id = SConfig::GetInstance().GetGameID();
    return;
  }
  m_boot_pending = false;
  Disarm();
  Config::DeleteKey(Config::LayerType::CurrentRun, Config::GFX_VR_PRESENTATION_MODE);
  if (m_phase == Phase::Booting)
    Fail("The game did not start again.");
}

std::string VRModeSwitch::GetRefusal() const
{
  const Core::State state = Core::GetState(m_system);
  if (state != Core::State::Running && state != Core::State::Paused)
    return "Wait until the game runs.";
  if (AchievementManager::GetInstance().IsHardcoreModeActive())
    return "Not in Hardcore Mode: it has no save states. Close the game first.";
  if (NetPlay::IsNetPlayRunning())
    return "Not during NetPlay.";
  if (m_system.GetMovie().IsMovieActive())
    return "Not during a recording.";
  if (m_recipe.kind == Recipe::Kind::None ||
      m_recipe.game_id != SConfig::GetInstance().GetGameID())
  {
    return "Not in this game. Close it first.";
  }
  return {};
}

bool VRModeSwitch::SaveState()
{
  File::CreateFullPath(m_state_path);
  // Not moved to the "undo save state" file. One that was kept is the player's.
  if (File::Exists(m_state_path) &&
      !File::Rename(m_state_path, ModeSwitch::GetOldStatePath(
                                      File::GetUserPath(D_STATESAVES_IDX), m_recipe.game_id)))
  {
    return false;  // else it would pass for this one
  }

  State::SaveOptions options;
  options.compress = false;  // faster, and its size is known
  options.wait_for_completion = true;
  State::SaveAs(m_system, m_state_path, options);
  if (!ModeSwitch::IsWholeState(m_state_path))
  {
    File::Delete(m_state_path, File::IfAbsentBehavior::NoConsoleWarning);
    return false;
  }
  const Core::CPUThreadGuard guard(m_system);
  m_state_ticks = m_system.GetCoreTiming().GetTicks();
  return true;
}

void VRModeSwitch::Request(Mode mode)
{
  if (IsSwitching() || (m_question && m_question->isVisible()))
    return;

  if (Core::IsUninitialized(m_system))
  {
    Remember(mode, Config::Get(Config::GFX_VR_PRESENTATION_MODE));
    UpdateButton();
    if (m_button)
    {
      QToolTip::showText(m_button->mapToGlobal(QPoint(0, m_button->height())),
                         mode == Mode::VR ? tr("Games start in VR.") : tr("Games start in Flat."),
                         m_button);
    }
    return;
  }
  if (mode == GetMode())
    return;

  std::string refusal = GetRefusal();
  if (refusal.empty() && mode == Mode::VR && !IsOpenXRAtHand())
    refusal = "OpenXR was not found.";
  Recipe recipe = m_recipe;
  if (refusal.empty() && !UseCurrentDisc(&recipe))
    refusal = "Not with a changed disc. Close the game first.";
  if (!refusal.empty())
  {
    OSD::AddMessage(fmt::format("Switch to {}: {}", GetName(mode), refusal), MESSAGE_MS);
    return;
  }

  INFO_LOG_FMT(CORE, "Switching to {}", GetName(mode));
  OSD::AddMessage(fmt::format("Switching to {}", GetName(mode)), MESSAGE_MS);
  // What the game writes from now on would not be in the state.
  const bool was_running = Core::GetState(m_system) == Core::State::Running;
  Core::SetState(m_system, Core::State::Paused, true, true);
  m_state_path = GetStatePath(recipe.game_id);
  if (!SaveState())
  {
    ERROR_LOG_FMT(CORE, "Switching to {}: the state was not saved", GetName(mode));
    if (was_running)
      Core::SetState(m_system, Core::State::Running, true, true);
    OSD::AddMessage(fmt::format("Switch to {}: the game's state could not be saved.", GetName(mode)),
                    MESSAGE_MS);
    return;
  }

  m_recipe = std::move(recipe);
  m_target = mode;
  m_fell_back = false;
  if (mode == Mode::Flat)
  {
    m_vr_game_id = m_recipe.game_id;
    m_vr_display = Config::Get(Config::GFX_VR_PRESENTATION_MODE);
  }
  m_phase = Phase::Stopping;
  UpdateButton();
  // The game is saved. A few switches are not to push out the autosaves.
  Autosave::End();
  m_actions.stop();
}

bool VRModeSwitch::OnStopComplete(bool cancelled)
{
  m_boot_pending = false;
  m_in_vr = false;
  Disarm();
  Config::DeleteKey(Config::LayerType::CurrentRun, Config::GFX_VR_PRESENTATION_MODE);
  if (m_question)
    m_question->reject();

  if (m_phase == Phase::Idle)
    return false;
  if (m_phase == Phase::Booting || cancelled)
  {
    // The game ended before its state was seen to be loaded, or the player wants something else.
    Fail(cancelled ? "The switch was stopped." : "The game did not start again.");
    return false;
  }
  Boot();
  return true;
}

void VRModeSwitch::Boot()
{
  m_phase = Phase::Booting;
  // A display chosen in the settings meanwhile is the player's.
  const bool go_back = GetDisplays().base == OpenXRPresentationMode::Vulkan;
  Remember(m_target, m_vr_display);
  Arm(m_target, m_target == Mode::VR && go_back && m_vr_game_id == m_recipe.game_id ?
                    m_vr_display :
                    OpenXRPresentationMode::Vulkan);
  std::unique_ptr<BootParameters> boot = MakeBoot(m_recipe);
  if (!boot)
  {
    Disarm();
    Fail("The game's file was not found.");
    return;
  }
  // Not deleted there: whether it was loaded is seen in OnRunning.
  boot->boot_session_data.SetSavestateData(m_state_path, DeleteSavestateAfterBoot::No);
  OnConfigChanged();
  m_actions.boot(std::move(boot));
}

void VRModeSwitch::OnRunning(u64 ticks, bool in_vr)
{
  // From here on the player may change it.
  s_wanted = OpenXRPresentationMode::Vulkan;
  UpdateButton();

  if (m_phase != Phase::Booting)
  {
    if (!IsSwitching())
      CleanStates();
    if (const std::string path = GetStatePath(m_recipe.game_id); File::Exists(path))
    {
      OSD::AddMessage(fmt::format("A switch did not end. The game's state from before it is {}",
                                  GetFileName(path)),
                      MESSAGE_MS);
    }
    return;
  }

  // A game that starts anew is at the beginning of its time.
  const bool loaded = ticks >= m_state_ticks;
  INFO_LOG_FMT(CORE, "Switched to {}: in the headset {}, time {} (saved at {})", GetName(m_target),
               in_vr, ticks, m_state_ticks);
  if (m_target == Mode::VR && !in_vr && !m_fell_back)
  {
    ERROR_LOG_FMT(CORE, "VR did not start: back to Flat");
    m_fell_back = true;
    m_target = Mode::Flat;
    m_phase = Phase::Stopping;
    m_actions.stop();
    return;
  }
  if (!loaded)
  {
    Fail("The game's state could not be loaded.");
    return;
  }
  m_phase = Phase::Idle;
  File::Delete(m_state_path);
  UpdateButton();
  if (m_fell_back)
    OSD::AddMessage("VR did not start. Back in Flat.", MESSAGE_MS);
  else
    OSD::AddMessage(fmt::format("Switched to {}", GetName(m_target)), 2000);
}

void VRModeSwitch::Fail(const std::string& message)
{
  m_phase = Phase::Idle;
  UpdateButton();
  CleanStates();
  const std::string text =
      File::Exists(m_state_path) ?
          fmt::format("{} It is kept as {}", message, GetFileName(m_state_path)) :
          message;
  ERROR_LOG_FMT(CORE, "Switching to {}: {}", GetName(m_target), text);
  if (Core::IsUninitialized(m_system))
  {
    // Not from within what stops or starts the game.
    QTimer::singleShot(0, this, [this, text] {
      ModalMessageBox::warning(m_main, tr("Switch"), QString::fromStdString(text));
    });
  }
  else
  {
    OSD::AddMessage(text, 2 * MESSAGE_MS);
  }
}

// ---- The display ------------------------------------------------------------------------------

void VRModeSwitch::Remember(Mode mode, OpenXRPresentationMode running)
{
  const ModeSwitch::Displays now = GetDisplays();
  const ModeSwitch::Displays next =
      mode == Mode::VR ? ModeSwitch::GoVR(now) : ModeSwitch::GoFlat(now, running);
  {
    const Config::ConfigChangeCallbackGuard guard;
    Config::SetBase(Config::GFX_VR_PRESENTATION_MODE, next.base);
    Config::SetBase(LAST_VR_MODE, next.last_vr);
    // As the settings page has it.
    Config::SetBase(Config::GFX_VR_ENABLE_OPENXR, mode == Mode::VR);
  }
  Config::Save();
}

void VRModeSwitch::Arm(Mode mode, OpenXRPresentationMode wanted)
{
  s_wanted = wanted;
  s_held = mode == Mode::VR ? Held::VR : Held::Flat;
}

void VRModeSwitch::Disarm()
{
  s_held = Held::Nothing;
  s_wanted = OpenXRPresentationMode::Vulkan;
}

void VRModeSwitch::OnConfigChanged()
{
  const Held held = s_held;
  if (held == Held::Nothing)
    return;
  const auto correct = ModeSwitch::Correct(held == Held::VR ? Mode::VR : Mode::Flat,
                                           Config::Get(Config::GFX_VR_PRESENTATION_MODE),
                                           GetDisplays(), s_wanted);
  if (correct)
    Config::SetCurrent(Config::GFX_VR_PRESENTATION_MODE, *correct);
}

void VRModeSwitch::OnCoreState(Core::State state)
{
  // What the game was held to ends with its settings.
  if (state == Core::State::Stopping)
    Disarm();
  // The CPU thread, before the game's first step. The state is loaded by now.
  if ((state == Core::State::Running || state == Core::State::Paused) &&
      m_boot_pending.exchange(false))
  {
    const bool in_vr = IsInHeadset();
    const u64 ticks = m_system.GetCoreTiming().GetTicks();
    m_in_vr = in_vr;
    QueueOnObject(this, [this, ticks, in_vr] { OnRunning(ticks, in_vr); });
  }
}

Mode VRModeSwitch::GetMode() const
{
  if (!Core::IsUninitialized(m_system))
    return m_in_vr ? Mode::VR : Mode::Flat;
  const OpenXRPresentationMode mode = Config::Get(Config::GFX_VR_PRESENTATION_MODE);
  if (mode == OpenXRPresentationMode::Legacy)
    return Config::Get(Config::GFX_VR_ENABLE_OPENXR) ? Mode::VR : Mode::Flat;
  return mode == OpenXRPresentationMode::Vulkan ? Mode::Flat : Mode::VR;
}

// ---- The window -------------------------------------------------------------------------------

void VRModeSwitch::UpdateButton()
{
  if (!m_button)
    return;
  const bool in_vr = GetMode() == Mode::VR;
  m_button->setText(in_vr ? QStringLiteral("FLAT") : QStringLiteral("VR"));
  m_button->setToolTip(in_vr ? tr("Switch to Flat") : tr("Switch to VR"));
  m_button->setEnabled(!IsSwitching());
  // On a desktop there are the settings, but not while a game runs.
  m_button->setVisible(::Host::IsGameMode() || ModeSwitch::IsFlatForAll(GetDisplays()));
}

void VRModeSwitch::Poll()
{
  Buttons buttons;
  // Only while Baiji has the keys: the controller reaches SDL in the background, too.
  if (!QApplication::activeWindow() || IsSwitching() || GetMode() == Mode::VR)
  {
    m_menu_hold.Reset();
    return;
  }
  if (!ReadGamepads(&buttons))
    return;

  if (m_question)
  {
    m_menu_hold.Update(buttons.menu, true, Now());
    // (Without a game, GamepadNavigation presses them, and is first.)
    const ModeSwitch::Answer::Result answer = m_answer.Update(buttons.a, buttons.b);
    if (answer != ModeSwitch::Answer::Result::None)
    {
      m_question
          ->button(answer == ModeSwitch::Answer::Result::Yes ? QMessageBox::Yes : QMessageBox::No)
          ->click();
    }
    return;
  }

  if (m_menu_hold.Update(buttons.menu, IsOtherWindowOpen(), Now()))
    Ask();
}

void VRModeSwitch::Ask()
{
  const bool pause = Core::GetState(m_system) == Core::State::Running;
  if (pause)
    Core::SetState(m_system, Core::State::Paused);

  auto* const box =
      new QMessageBox(QMessageBox::Question, tr("Switch to VR"), tr("Switch to VR?"),
                      QMessageBox::Yes | QMessageBox::No, m_main);
  box->setInformativeText(tr("A: Yes    B: No"));
  box->setDefaultButton(QMessageBox::Yes);
  box->setEscapeButton(QMessageBox::No);
  box->setAttribute(Qt::WA_DeleteOnClose);
  m_question = box;
  m_answer.Reset();
  connect(box, &QMessageBox::finished, this, [this, box, pause](int) {
    const bool yes = box->clickedButton() == box->button(QMessageBox::Yes);
    m_question = nullptr;
    if (m_main)
      m_main->activateWindow();
    INFO_LOG_FMT(CORE, "Switch to VR? {}", yes ? "Yes" : "No");
    if (pause && Core::GetState(m_system) == Core::State::Paused)
      Core::SetState(m_system, Core::State::Running);
    if (yes)
      QTimer::singleShot(0, this, [this] { Request(Mode::VR); });  // once the box is gone
  });
  box->open();
}
