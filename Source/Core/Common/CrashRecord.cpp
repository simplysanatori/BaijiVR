// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/CrashRecord.h"

#include <fmt/format.h>

#ifdef HAVE_CRASH_RECORD
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <typeinfo>
#include <utility>

#include <cxxabi.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace CrashRecord
{
std::string_view CauseKey(Cause cause)
{
  return cause == Cause::DeviceLost ? "device-lost" : "";
}

Cause CauseFromKey(std::string_view key)
{
  return key == CauseKey(Cause::DeviceLost) ? Cause::DeviceLost : Cause::Nothing;
}

// A newline in a title would start a new key.
static std::string OneLine(std::string_view s)
{
  std::string out(s.substr(0, 400));
  for (char& c : out)
  {
    if (c == '\n' || c == '\r')
      c = ' ';
  }
  return out;
}

std::string MarkerText(int pid, s64 started, const Program& program, const Game& game, Cause cause,
                       std::string_view detail)
{
  std::string s =
      fmt::format("pid={}\nstarted={}\nversion={}\nrelease={}\nexe={}\nexe_build_id={}\n", pid,
                  started, OneLine(program.version), OneLine(program.release),
                  OneLine(program.exe), OneLine(program.build_id));
  if (!game.id.empty())
  {
    s += fmt::format("game_id={}\nrevision={}\ntitle={}\n", OneLine(game.id), game.revision,
                     OneLine(game.title));
    s += fmt::format("display={}\ngpu={}\ndriver={}\n", OneLine(game.display), OneLine(game.gpu),
                     OneLine(game.driver));
  }
  if (cause != Cause::Nothing)
    s += fmt::format("cause={}\ndetail={}\n", CauseKey(cause), OneLine(detail));
  std::string_view rest = game.settings;
  while (!rest.empty())
  {
    const std::size_t end = rest.find('\n');
    if (const std::string_view line = rest.substr(0, end); !line.empty())
      s += fmt::format("setting={}\n", line);
    if (end == std::string_view::npos)
      break;
    rest.remove_prefix(end + 1);
  }
  return s;
}

#ifdef HAVE_CRASH_RECORD
namespace
{
std::mutex s_mutex;
std::string s_dir;
Program s_program;
s64 s_started = 0;
Game s_game;
Cause s_cause_noted = Cause::Nothing;
std::string s_detail;

// Read by the signal handler without a lock.
int s_pid = 0;
char s_crash_path[4096];
std::atomic<int> s_cause{0};
char s_cause_detail[512];
char s_exception[512];
std::atomic<bool> s_in_handler{false};
std::atomic<bool> s_clean{false};
constexpr std::array SIGNALS{SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
struct sigaction s_old[SIGNALS.size()];
std::terminate_handler s_old_terminate = nullptr;
// Not on the stack: the thread that crashed may not have much of it left.
char s_buffer[4096];
void* s_frames[64];

std::string MarkerPath()
{
  return fmt::format("{}{}{}", s_dir, RUNNING_PREFIX, s_pid);
}

void WriteMarkerLocked()
{
  if (s_dir.empty() || s_clean)
    return;
  const std::string path = MarkerPath();
  const std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::trunc);
    if (!f)
      return;
    f << MarkerText(s_pid, s_started, s_program, s_game, s_cause_noted, s_detail);
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
}

// ---- async-signal-safe output ----

void PutRaw(int fd, const char* s, std::size_t n)
{
  while (n > 0)
  {
    const ssize_t written = write(fd, s, n);
    if (written <= 0)
      return;
    s += written;
    n -= static_cast<std::size_t>(written);
  }
}

void Put(int fd, const char* s)
{
  PutRaw(fd, s, std::strlen(s));
}

void PutHex(int fd, u64 v)
{
  char buffer[18] = {'0', 'x'};
  for (int i = 0; i < 16; ++i)
  {
    const auto digit = static_cast<unsigned>((v >> (60 - i * 4)) & 0xF);
    buffer[2 + i] = static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
  }
  PutRaw(fd, buffer, sizeof(buffer));
}

void PutDec(int fd, int v)
{
  char buffer[12];
  std::size_t i = sizeof(buffer);
  unsigned u = v < 0 ? 0u - static_cast<unsigned>(v) : static_cast<unsigned>(v);
  do
  {
    buffer[--i] = static_cast<char>('0' + u % 10);
    u /= 10;
  } while (u != 0);
  if (v < 0)
    buffer[--i] = '-';
  PutRaw(fd, buffer + i, sizeof(buffer) - i);
}

// A value in one line: newlines become spaces.
void PutLine(int fd, const char* key, const char* value)
{
  Put(fd, key);
  Put(fd, "=");
  for (std::size_t i = 0; i < 512 && value[i]; ++i)
    PutRaw(fd, value[i] == '\n' || value[i] == '\r' ? " " : value + i, 1);
  Put(fd, "\n");
}

void WriteRecord(int fd, int sig, const siginfo_t* info, const ucontext_t* context)
{
  // In order of safety: the plain fields and the memory map, with write() only, then the stack.
  Put(fd, "signal=");
  PutDec(fd, sig);
  Put(fd, "\ncode=");
  PutDec(fd, info ? info->si_code : 0);
  Put(fd, "\naddr=");
  // A signal that was sent has the sender there.
  PutHex(fd, info && info->si_code > 0 ? reinterpret_cast<u64>(info->si_addr) : 0);
  Put(fd, "\n");
  char name[17] = {};
  prctl(PR_GET_NAME, name, 0, 0, 0);
  PutLine(fd, "thread", name);
  if (const int cause = s_cause.load(); cause != 0)
  {
    const std::string_view key = CauseKey(static_cast<Cause>(cause));
    Put(fd, "cause=");
    PutRaw(fd, key.data(), key.size());
    Put(fd, "\n");
    PutLine(fd, "detail", s_cause_detail);
  }
  if (s_exception[0] != 0)
    PutLine(fd, "exception", s_exception);
  if (context)
  {
#if defined(__aarch64__)
    Put(fd, "pc=");
    PutHex(fd, context->uc_mcontext.pc);
    Put(fd, "\nlr=");
    PutHex(fd, context->uc_mcontext.regs[30]);
    Put(fd, "\n");
#elif defined(__x86_64__)
    Put(fd, "pc=");
    PutHex(fd, static_cast<u64>(context->uc_mcontext.gregs[REG_RIP]));
    Put(fd, "\n");
#endif
  }
  Put(fd, "maps-begin\n");
  if (const int maps = open("/proc/self/maps", O_RDONLY | O_CLOEXEC); maps >= 0)
  {
    ssize_t n;
    while ((n = read(maps, s_buffer, sizeof(s_buffer))) > 0)
      PutRaw(fd, s_buffer, static_cast<std::size_t>(n));
    close(maps);
  }
  Put(fd, "maps-end\n");
  fsync(fd);
  // The one call that can hang or fault (it unwinds through libgcc): the record has the rest.
  const int count = backtrace(s_frames, static_cast<int>(std::size(s_frames)));
  for (int i = 0; i < count; ++i)
  {
    Put(fd, "frame=");
    PutHex(fd, reinterpret_cast<u64>(s_frames[i]));
    Put(fd, "\n");
  }
  Put(fd, "symbols-begin\n");
  backtrace_symbols_fd(s_frames, count, fd);
  Put(fd, "symbols-end\n");
  fsync(fd);
}

void OnTerminate()
{
  if (const std::exception_ptr exception = std::current_exception())
  {
    try
    {
      std::rethrow_exception(exception);
    }
    catch (const std::exception& e)
    {
      int status = 0;
      char* const readable = abi::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &status);
      std::snprintf(s_exception, sizeof(s_exception), "%s: %s",
                    status == 0 && readable ? readable : typeid(e).name(), e.what());
      std::free(readable);
    }
    catch (...)
    {
      std::snprintf(s_exception, sizeof(s_exception), "an exception of unknown type");
    }
  }
  else
  {
    std::snprintf(s_exception, sizeof(s_exception), "std::terminate called");
  }
  if (s_old_terminate)
    s_old_terminate();
  std::abort();
}
}  // namespace

void HandleSignal(int sig, siginfo_t* info, void* raw_context)
{
  // (A child between fork and exec has these handlers too, and is not this run.)
  if (getpid() == s_pid && !s_clean && !s_in_handler.exchange(true))
  {
    // A watchdog: SIGALRM's default action ends the process five seconds from now, should the
    // unwinder wait for a lock that the crashed thread holds.
    struct sigaction alarm_action{};
    alarm_action.sa_handler = SIG_DFL;
    sigemptyset(&alarm_action.sa_mask);
    sigaction(SIGALRM, &alarm_action, nullptr);
    sigset_t alarm_set;
    sigemptyset(&alarm_set);
    sigaddset(&alarm_set, SIGALRM);
    pthread_sigmask(SIG_UNBLOCK, &alarm_set, nullptr);
    alarm(5);
    if (const int fd = open(s_crash_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600); fd >= 0)
    {
      WriteRecord(fd, sig, info, static_cast<const ucontext_t*>(raw_context));
      close(fd);
    }
    alarm(0);
  }
  // The handler that was there before has it now, else the default action.
  for (std::size_t i = 0; i < SIGNALS.size(); ++i)
  {
    if (SIGNALS[i] == sig)
      sigaction(sig, &s_old[i], nullptr);
  }
  // A fault comes back when its instruction runs again. One that was sent does not, and neither does
  // a breakpoint on x86: it is raised again, and taken once this returns.
  if (!info || info->si_code <= 0 || sig == SIGABRT || sig == SIGTRAP)
    raise(sig);
}

void Start(const std::string& dir, Program program)
{
  std::lock_guard lock(s_mutex);
  if (!s_dir.empty() || dir.empty())
    return;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  s_dir = dir;
  s_program = std::move(program);
  s_started = std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
  s_pid = static_cast<int>(getpid());
  std::snprintf(s_crash_path, sizeof(s_crash_path), "%s",
                fmt::format("{}{}{}", dir, CRASH_PREFIX, s_pid).c_str());
  // An earlier process of this number.
  std::filesystem::remove(s_crash_path, ec);
  WriteMarkerLocked();
  // Its first call loads the unwinder: not in the handler.
  void* frames[4];
  backtrace(frames, 4);
  // On the thread's own stack: the alternate one that the CPU thread has is small, and the fastmem
  // handler does not use it either.
  struct sigaction action{};
  action.sa_sigaction = &HandleSignal;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  for (std::size_t i = 0; i < SIGNALS.size(); ++i)
    sigaction(SIGNALS[i], &action, &s_old[i]);
  s_old_terminate = std::set_terminate(&OnTerminate);
}

void NoteGame(Game game)
{
  std::lock_guard lock(s_mutex);
  if (game == s_game)
    return;
  s_game = std::move(game);
  if (s_game.id.empty())
  {
    s_cause_noted = Cause::Nothing;
    s_detail.clear();
    s_cause = 0;
  }
  WriteMarkerLocked();
}

void NoteCause(Cause cause, std::string_view detail)
{
  std::lock_guard lock(s_mutex);
  s_cause_noted = cause;
  s_detail = OneLine(detail);
  std::snprintf(s_cause_detail, sizeof(s_cause_detail), "%s", s_detail.c_str());
  s_cause = static_cast<int>(cause);
  WriteMarkerLocked();
}

void CleanExit()
{
  std::lock_guard lock(s_mutex);
  if (s_dir.empty())
    return;
  s_clean = true;
  std::error_code ec;
  std::filesystem::remove(MarkerPath(), ec);
  std::filesystem::remove(s_crash_path, ec);
}

#else

void Start(const std::string&, Program)
{
}

void NoteGame(Game)
{
}

void NoteCause(Cause, std::string_view)
{
}

void CleanExit()
{
}
#endif
}  // namespace CrashRecord
