// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

#include "Common/CommonTypes.h"

#if defined(__linux__) && !defined(ANDROID)
#define HAVE_CRASH_RECORD 1
#include <signal.h>
#endif

// What the next start needs to say why this run ended badly (from Azahar for Frame). Linux only:
// elsewhere nothing is written and nothing is installed.
//
// Start writes a run marker (running-<pid>: the version, the game and its settings, a cause noted on
// the way) and installs handlers for the fatal signals and for std::terminate. A fatal signal writes
// crash-<pid> (the signal, the address, the thread, the stack, /proc/self/maps) with
// async-signal-safe calls, then takes the process as it would have. A clean exit removes both. A
// marker whose process is gone is a run that did not end cleanly: UICommon/ProblemReport.h reads it.
//
// The fastmem handler (Core/MemTools.cpp) is installed over this one while a game runs. It handles
// its own faults and passes on the rest. Nothing here sends anything anywhere.
namespace CrashRecord
{
enum class Cause : u8
{
  Nothing,
  DeviceLost,  // Vulkan reported VK_ERROR_DEVICE_LOST
};

// "device-lost", "" for Nothing.
std::string_view CauseKey(Cause cause);
Cause CauseFromKey(std::string_view key);

constexpr std::string_view RUNNING_PREFIX = "running-";
constexpr std::string_view CRASH_PREFIX = "crash-";

struct Game
{
  std::string id;  // empty: none runs
  u16 revision = 0;
  std::string title;
  std::string display;  // "Stereoscopic 3D"
  std::string gpu;
  std::string driver;
  std::string settings;  // lines

  bool operator==(const Game&) const = default;
};

struct Program
{
  std::string version;
  std::string release;  // "baijivr@0.9.0"
  // Its file, and the build ID of that: the file may be replaced while it runs.
  std::string exe;
  std::string build_id;
};

// `dir` ends with '/' and is made when missing. Once per process: later calls do nothing.
void Start(const std::string& dir, Program program);
// Writes the marker again when the game changed. Without a game, the noted cause is forgotten.
void NoteGame(Game game);
// What the next start names if this run then ends badly. Any thread but the XR pacing thread.
void NoteCause(Cause cause, std::string_view detail);
// The marker and any record go, and there are none from here on.
void CleanExit();

// For the tests.
std::string MarkerText(int pid, s64 started, const Program& program, const Game& game, Cause cause,
                       std::string_view detail);

#ifdef HAVE_CRASH_RECORD
// The handler that Start installs.
void HandleSignal(int sig, siginfo_t* info, void* raw_context);
#endif
}  // namespace CrashRecord
