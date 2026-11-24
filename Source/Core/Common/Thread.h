// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <tuple>
#endif

// Don't include Common.h here as it will break LogManager
#include "Common/CommonTypes.h"

// This may not be defined outside _WIN32
#ifndef _WIN32
#ifndef INFINITE
#define INFINITE 0xffffffff
#endif
#endif

namespace Common
{
int CurrentThreadId();

void SetThreadAffinity(std::thread::native_handle_type thread, u32 mask);
void SetCurrentThreadAffinity(u32 mask);

// The hottest emulator threads, for big.LITTLE performance-core pinning.
enum class ThreadCoreRole
{
  EmuCPU,    // PowerPC JIT ("CPU thread") — serial, latency-critical: dedicated fast core
  EmuVideo,  // FIFO/GPU-submit ("Video thread"): its own fast core, no contention with EmuCPU
  VRPacing,  // OpenXR frame-pacing thread
  VRSubmit,  // Vulkan async command-submit worker
};

// One CPU as the kernel reports it: cpu_capacity (0 when absent) and cpuinfo_max_freq (kHz, 0 when
// absent).
struct CpuCoreInfo
{
  int index = 0;
  long capacity = 0;
  long max_freq = 0;
};

// The cores the performance pin policy picked, -1 for none.
struct CoreSelection
{
  int emu_cpu = -1;    // the fastest allowed core
  int emu_video = -1;  // the second
  int light = -1;      // the third, shared by the VR pacing thread and the submit worker
};

// Pure policy (Linux/Android big.LITTLE): rank the allowed cores by capacity (else max frequency),
// fastest first, ties to the higher index. Cores within 5% of the slowest are not "fast" and are
// never picked; with all allowed cores equal nothing is picked. Each role gets a distinct core, or
// -1 when there is none left.
CoreSelection SelectCoresForRoles(std::span<const CpuCoreInfo> cores, std::span<const int> allowed);

// Linux only: undoes the single-core mask a pinned thread passes on to threads it creates. Called
// by SetCurrentThreadName, which every worker thread calls first. No-op while nothing is pinned.
void ResetInheritedThreadAffinity();

// Android and Linux: pin the calling thread to one of the device's performance (highest
// max-frequency) cores, chosen per role so these threads land on distinct fast cores
// and don't migrate across the big.LITTLE boundary. Cores are classified from
// cpufreq at runtime (correct on any Quest). Returns the pinned CPU index, or -1 if
// unavailable (other platforms, no distinct fast core, topology unreadable, or the syscall failed).
int PinCurrentThreadToPerformanceCore(ThreadCoreRole role);

void SleepCurrentThread(int ms);
void SwitchCurrentThread();  // On Linux, this is equal to sleep 1ms

// Use this function during a spin-wait to make the current thread
// relax while another thread is working. This may be more efficient
// than using events because event functions use kernel calls.
inline void YieldCPU()
{
  std::this_thread::yield();
}

void SetCurrentThreadName(const char* name);

#ifndef _WIN32
// Returns the lowest address of the stack and the size of the stack
std::tuple<void*, size_t> GetCurrentThreadStack();
#endif

}  // namespace Common
