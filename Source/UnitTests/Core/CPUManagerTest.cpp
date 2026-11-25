// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/CPU.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "UICommon/UICommon.h"

// A job for the paused CPU thread is run, also when it comes just as the thread goes to sleep
// (State::SaveAs of a paused game waits for its job).
TEST(CPUManager, JobsOfAPausedGameAreRun)
{
  using namespace std::chrono_literals;

  const std::string profile_path = File::CreateTempDir();
  ASSERT_FALSE(profile_path.empty());
  UICommon::SetUserDirectory(profile_path);
  Config::Init();
  SConfig::Init();
  Config::SetCurrent(Config::MAIN_TIME_TRACKING, false);

  auto& system = Core::System::GetInstance();
  auto& cpu = system.GetCPU();
  Core::DeclareAsCPUThread();
  cpu.Init(PowerPC::CPUCore::Interpreter);
  Core::UndeclareAsCPUThread();
  std::thread thread([&cpu] {
    Core::DeclareAsCPUThread();
    cpu.Run();
  });

  for (int i = 0; i < 100000; ++i)
  {
    auto done = std::make_shared<std::promise<void>>();
    auto future = done->get_future();
    const bool was_unpaused = cpu.PauseAndLock();
    cpu.AddCPUThreadJob([done] { done->set_value(); });
    cpu.RestoreStateAndUnlock(was_unpaused);
    const bool ran = future.wait_for(5s) == std::future_status::ready;
    EXPECT_TRUE(ran) << "job " << i;
    if (!ran)
      break;
  }

  cpu.Stop();
  thread.join();
  cpu.Shutdown();

  SConfig::Shutdown();
  Config::Shutdown();
  File::DeleteDirRecursively(profile_path);
}
