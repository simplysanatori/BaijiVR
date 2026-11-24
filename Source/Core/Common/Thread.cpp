// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/Thread.h"

#ifdef _WIN32
#include <Windows.h>
#include <processthreadsapi.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <string>

#include <fmt/format.h>
#if defined(ANDROID) || defined(__linux__)
#include <sched.h>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <utility>
#include "Common/Logging/Log.h"
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#elif defined BSD4_4 || defined __FreeBSD__ || defined __OpenBSD__
#include <pthread_np.h>
#elif defined __NetBSD__
#include <sched.h>
#elif defined __HAIKU__
#include <OS.h>
#endif

#ifdef USE_VTUNE
#include <ittnotify.h>
#pragma comment(lib, "libittnotify.lib")
#endif

#include "Common/CommonTypes.h"
#ifdef _WIN32
#include "Common/StringUtil.h"
#endif

namespace Common
{
int CurrentThreadId()
{
#ifdef _WIN32
  return GetCurrentThreadId();
#elif defined __APPLE__
  return mach_thread_self();
#else
  return 0;
#endif
}

#ifdef _WIN32

void SetThreadAffinity(std::thread::native_handle_type thread, u32 mask)
{
  SetThreadAffinityMask(thread, mask);
}

void SetCurrentThreadAffinity(u32 mask)
{
  SetThreadAffinityMask(GetCurrentThread(), mask);
}

// Supporting functions
void SleepCurrentThread(int ms)
{
  Sleep(ms);
}

void SwitchCurrentThread()
{
  SwitchToThread();
}

// Sets the debugger-visible name of the current thread.
// Uses trick documented in:
// https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code
static void SetCurrentThreadNameViaException(const char* name)
{
  static const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push, 8)
  struct THREADNAME_INFO
  {
    DWORD dwType;      // must be 0x1000
    LPCSTR szName;     // pointer to name (in user addr space)
    DWORD dwThreadID;  // thread ID (-1=caller thread)
    DWORD dwFlags;     // reserved for future use, must be zero
  } info;
#pragma pack(pop)

  info.dwType = 0x1000;
  info.szName = name;
  info.dwThreadID = static_cast<DWORD>(-1);
  info.dwFlags = 0;

  __try
  {
    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
  }
  __except (EXCEPTION_CONTINUE_EXECUTION)
  {
  }
}

static void SetCurrentThreadNameViaApi(const char* name)
{
  // If possible, also set via the newer API. On some versions of Server it needs to be manually
  // resolved. This API allows being able to observe the thread name even if a debugger wasn't
  // attached when the name was set (see above link for more info).
  static auto pSetThreadDescription = (decltype(&SetThreadDescription))GetProcAddress(
      GetModuleHandleA("kernel32"), "SetThreadDescription");
  if (pSetThreadDescription)
  {
    pSetThreadDescription(GetCurrentThread(), UTF8ToWString(name).c_str());
  }
}

void SetCurrentThreadName(const char* name)
{
  SetCurrentThreadNameViaException(name);
  SetCurrentThreadNameViaApi(name);
}

#else  // !WIN32, so must be POSIX threads

void SetThreadAffinity(std::thread::native_handle_type thread, u32 mask)
{
#ifdef __APPLE__
  thread_policy_set(pthread_mach_thread_np(thread), THREAD_AFFINITY_POLICY, (integer_t*)&mask, 1);
#elif (defined __linux__ || defined BSD4_4 || defined __FreeBSD__ || defined __NetBSD__) &&        \
    !(defined ANDROID)
#ifndef __NetBSD__
#ifdef __FreeBSD__
  cpuset_t cpu_set;
#else
  cpu_set_t cpu_set;
#endif
  CPU_ZERO(&cpu_set);

  for (int i = 0; i != sizeof(mask) * 8; ++i)
    if ((mask >> i) & 1)
      CPU_SET(i, &cpu_set);

  pthread_setaffinity_np(thread, sizeof(cpu_set), &cpu_set);
#else
  cpuset_t* cpu_set = cpuset_create();

  for (int i = 0; i != sizeof(mask) * 8; ++i)
    if ((mask >> i) & 1)
      cpuset_set(i, cpu_set);

  pthread_setaffinity_np(thread, cpuset_size(cpu_set), cpu_set);
  cpuset_destroy(cpu_set);
#endif
#endif
}

void SetCurrentThreadAffinity(u32 mask)
{
  SetThreadAffinity(pthread_self(), mask);
}

void SleepCurrentThread(int ms)
{
  usleep(1000 * ms);
}

void SwitchCurrentThread()
{
  usleep(1000 * 1);
}

void SetCurrentThreadName(const char* name)
{
  ResetInheritedThreadAffinity();
#ifdef __APPLE__
  pthread_setname_np(name);
#elif defined __FreeBSD__ || defined __OpenBSD__
  pthread_set_name_np(pthread_self(), name);
#elif defined(__NetBSD__)
  pthread_setname_np(pthread_self(), "%s", const_cast<char*>(name));
#elif defined __HAIKU__
  rename_thread(find_thread(nullptr), name);
#else
  // linux doesn't allow to set more than 16 bytes, including \0.
  pthread_setname_np(pthread_self(), std::string(name).substr(0, 15).c_str());
#endif
#ifdef USE_VTUNE
  // VTune uses OS thread names by default but probably supports longer names when set via its own
  // API.
  __itt_thread_set_name(name);
#endif
}

#endif  // !WIN32

CoreSelection SelectCoresForRoles(std::span<const CpuCoreInfo> cores, std::span<const int> allowed)
{
  struct Candidate
  {
    int index;
    long key;
    long freq;
  };
  std::vector<Candidate> candidates;
  const bool use_capacity =
      std::ranges::any_of(cores, [](const CpuCoreInfo& core) { return core.capacity > 0; });
  for (const CpuCoreInfo& core : cores)
  {
    if (std::ranges::find(allowed, core.index) == allowed.end())
      continue;
    const long key = use_capacity ? core.capacity : core.max_freq;
    if (key <= 0)
      continue;  // unreadable: neither fast nor slow, leave it out
    candidates.push_back({core.index, key, core.max_freq});
  }
  if (candidates.size() < 2)
    return {};

  std::ranges::sort(candidates, [](const Candidate& a, const Candidate& b) {
    if (a.key != b.key)
      return a.key > b.key;
    if (a.freq != b.freq)
      return a.freq > b.freq;
    return a.index > b.index;
  });
  const long fastest = candidates.front().key;
  const long slowest = candidates.back().key;
  if (fastest - slowest <= fastest / 20)
    return {};  // all equal (the x86 desktop): nothing to choose

  const long fast_floor = slowest + slowest / 20;  // above this, a core is not in the slowest tier
  std::vector<int> fast;
  for (const Candidate& candidate : candidates)
  {
    if (candidate.key > fast_floor)
      fast.push_back(candidate.index);
  }

  CoreSelection selection;
  if (fast.size() > 0)
    selection.emu_cpu = fast[0];
  if (fast.size() > 1)
    selection.emu_video = fast[1];
  if (fast.size() > 2)
    selection.light = fast[2];
  return selection;
}

#if defined(ANDROID)

namespace
{
// Indices of the device's fastest CPU cores (all cores sharing the top cpufreq tier),
// in ascending order. Detected once. On Quest 3 this is {2,3,4,5}; cpu0/1 are the
// slower cores. The app runs as top-app so it may use all of them.
std::vector<int> s_performance_cores;
std::once_flag s_performance_cores_once;

void DetectPerformanceCores()
{
  std::vector<std::pair<int, long>> cores;  // (cpu index, max frequency)
  long top_freq = 0;
  for (int cpu = 0; cpu < 64; ++cpu)
  {
    char path[128];
    std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq",
                  cpu);
    FILE* file = std::fopen(path, "r");
    if (!file)
      break;  // CPU nodes are contiguous; the first missing one marks the end.
    long freq = 0;
    if (std::fscanf(file, "%ld", &freq) == 1 && freq > 0)
    {
      cores.emplace_back(cpu, freq);
      top_freq = std::max(top_freq, freq);
    }
    std::fclose(file);
  }

  for (const auto& [index, freq] : cores)
  {
    if (freq == top_freq)
      s_performance_cores.push_back(index);
  }
}
}  // namespace

int PinCurrentThreadToPerformanceCore(ThreadCoreRole role)
{
  std::call_once(s_performance_cores_once, DetectPerformanceCores);
  if (s_performance_cores.empty())
    return -1;

  // Hand out distinct cores from the fast tier, top-down (highest core index is usually
  // the turbo/prime core). EmuCPU gets the fastest, then EmuVideo, then the VR helpers.
  // If there are fewer fast cores than roles, later roles share (wrap).
  int slot = 0;
  switch (role)
  {
  case ThreadCoreRole::EmuCPU:
    slot = 0;
    break;
  case ThreadCoreRole::EmuVideo:
    slot = 1;
    break;
  case ThreadCoreRole::VRPacing:
  case ThreadCoreRole::VRSubmit:
    // Meta's runtime confines RendererWorker-registered threads to a cpuset (on Quest 3,
    // {3,4,5}); a pin outside it silently fails. Both light VR workers therefore share the
    // 3rd fast core — inside that set, and off the EmuCPU/EmuVideo cores (top two).
    slot = 2;
    break;
  }
  const int count = static_cast<int>(s_performance_cores.size());
  const int core = s_performance_cores[count - 1 - (slot % count)];

  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(core, &set);
  if (sched_setaffinity(0, sizeof(set), &set) != 0)
    return -1;
  return core;
}

#elif defined(__linux__)

namespace
{
struct LinuxCorePlan
{
  CoreSelection selection;
  cpu_set_t allowed_mask;
};
LinuxCorePlan s_plan;
std::once_flag s_plan_once;
std::atomic<bool> s_any_pinned{false};
thread_local bool t_pinned = false;

long ReadSysfsLong(int cpu, const char* file)
{
  char path[128];
  std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/%s", cpu, file);
  FILE* f = std::fopen(path, "r");
  if (!f)
    return 0;
  long value = 0;
  if (std::fscanf(f, "%ld", &value) != 1)
    value = 0;
  std::fclose(f);
  return value;
}

void DetectPlan()
{
  CPU_ZERO(&s_plan.allowed_mask);
  if (sched_getaffinity(0, sizeof(s_plan.allowed_mask), &s_plan.allowed_mask) != 0)
  {
    WARN_LOG_FMT(COMMON, "Core pinning: sched_getaffinity failed; no pinning.");
    return;
  }

  std::vector<CpuCoreInfo> cores;
  std::vector<int> allowed;
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
  {
    if (!CPU_ISSET(cpu, &s_plan.allowed_mask))
      continue;
    allowed.push_back(cpu);
    cores.push_back({cpu, ReadSysfsLong(cpu, "cpu_capacity"),
                     ReadSysfsLong(cpu, "cpufreq/cpuinfo_max_freq")});
  }

  std::string layout;
  for (const CpuCoreInfo& core : cores)
  {
    layout += fmt::format("{}cpu{}(cap {}, {} MHz)", layout.empty() ? "" : ", ", core.index,
                          core.capacity, core.max_freq / 1000);
  }
  s_plan.selection = SelectCoresForRoles(cores, allowed);
  const CoreSelection& s = s_plan.selection;
  INFO_LOG_FMT(COMMON, "Core pinning: allowed cores: {}. Chosen: emulated CPU {}, video {}, VR pacing "
                       "and submit {} (-1 is not pinned).",
               layout, s.emu_cpu, s.emu_video, s.light);
}
}  // namespace

int PinCurrentThreadToPerformanceCore(ThreadCoreRole role)
{
  std::call_once(s_plan_once, DetectPlan);
  int core = -1;
  switch (role)
  {
  case ThreadCoreRole::EmuCPU:
    core = s_plan.selection.emu_cpu;
    break;
  case ThreadCoreRole::EmuVideo:
    core = s_plan.selection.emu_video;
    break;
  case ThreadCoreRole::VRPacing:
  case ThreadCoreRole::VRSubmit:
    core = s_plan.selection.light;
    break;
  }
  if (core < 0 || !CPU_ISSET(core, &s_plan.allowed_mask))
    return -1;

  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(core, &set);
  if (sched_setaffinity(0, sizeof(set), &set) != 0)
    return -1;
  t_pinned = true;
  s_any_pinned.store(true, std::memory_order_relaxed);
  return core;
}

void ResetInheritedThreadAffinity()
{
  if (t_pinned || !s_any_pinned.load(std::memory_order_relaxed))
    return;
  // Threads inherit the creator's affinity; a thread that is not itself pinned goes back to the
  // process's allowed cores.
  sched_setaffinity(0, sizeof(s_plan.allowed_mask), &s_plan.allowed_mask);
}

#else

int PinCurrentThreadToPerformanceCore(ThreadCoreRole /*role*/)
{
  return -1;
}

#endif  // ANDROID

#if !defined(__linux__) || defined(ANDROID)
void ResetInheritedThreadAffinity()
{
}
#endif

#ifndef _WIN32
std::tuple<void*, size_t> GetCurrentThreadStack()
{
  void* stack_addr;
  size_t stack_size;

  pthread_t self = pthread_self();

#ifdef __APPLE__
  stack_size = pthread_get_stacksize_np(self);
  stack_addr = reinterpret_cast<u8*>(pthread_get_stackaddr_np(self)) - stack_size;
#elif defined __OpenBSD__
  stack_t stack;
  pthread_stackseg_np(self, &stack);

  stack_addr = reinterpret_cast<u8*>(stack.ss_sp) - stack.ss_size;
  stack_size = stack.ss_size;
#else
  pthread_attr_t attr;

#ifdef __FreeBSD__
  pthread_attr_init(&attr);
  pthread_attr_get_np(self, &attr);
#else
  // Linux and NetBSD
  pthread_getattr_np(self, &attr);
#endif

  pthread_attr_getstack(&attr, &stack_addr, &stack_size);

  pthread_attr_destroy(&attr);
#endif

  return std::make_tuple(stack_addr, stack_size);
}

#endif

}  // namespace Common
