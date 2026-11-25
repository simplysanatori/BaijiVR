// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Common/CrashRecord.h"
#include "Common/FileUtil.h"
#include "Common/MemoryUtil.h"
#include "Core/Core.h"
#include "Core/MachineContext.h"
#include "Core/MemTools.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/System.h"
#include "UICommon/ProblemReport.h"

#include "StubJit.h"

#include <gtest/gtest.h>

// Every test crashes in a child: Start is never called in the process that runs the tests.
namespace
{
namespace fs = std::filesystem;

constexpr std::size_t PAGE = 0x1000;
int* const NOWHERE = reinterpret_cast<int*>(0x1238);

class CrashRecordTest : public testing::Test
{
protected:
  CrashRecordTest() : m_dir(File::CreateTempDir() + "/reports/") {}
  ~CrashRecordTest() override { fs::remove_all(fs::path(m_dir).parent_path().parent_path()); }

  // How the child ended.
  int RunChild(const std::function<void()>& body)
  {
    const pid_t pid = fork();
    if (pid == 0)
    {
      // No core dump (with a limit of 1 not through a pipe either), and no one to notice.
      const rlimit limit{1, 1};
      setrlimit(RLIMIT_CORE, &limit);
      for (const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP})
        std::signal(sig, SIG_DFL);
      // The test would wait for a child that hangs.
      alarm(30);
      CrashRecord::Start(m_dir, {"1.0", "baijivr@1.0", "/proc/self/exe", "abcd"});
      body();
      _exit(0);
    }
    m_pid = pid;
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
  }

  std::string Path(std::string_view prefix) const
  {
    return m_dir + std::string(prefix) + std::to_string(m_pid);
  }
  bool HasMarker() const { return fs::exists(Path(CrashRecord::RUNNING_PREFIX)); }
  bool HasRecord() const { return fs::exists(Path(CrashRecord::CRASH_PREFIX)); }
  std::string Read(std::string_view prefix) const
  {
    std::ifstream f(Path(prefix));
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
  }

  // What the next start finds: a crash by this signal.
  ProblemReport::LastRun Expect(int status, int signal)
  {
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), signal);
    const auto run = ProblemReport::TakeLastRun(m_dir, &ProblemReport::IsOtherInstance);
    EXPECT_TRUE(run.has_value());
    EXPECT_TRUE(fs::is_empty(m_dir));
    if (!run)
      return {};
    EXPECT_TRUE(run->crash);
    EXPECT_EQ(run->signal, signal);
    EXPECT_EQ(run->pid, m_pid);
    EXPECT_EQ(run->program.release, "baijivr@1.0");
    EXPECT_FALSE(run->thread.empty());
    EXPECT_NE(run->pc, 0u);
    EXPECT_FALSE(run->frames.empty());
    EXPECT_FALSE(ProblemReport::ParseMaps(run->maps).empty());
    return *run;
  }

  std::string m_dir;
  int m_pid = 0;
};

// Handles what it is asked to.
class FaultJit : public StubJit
{
public:
  explicit FaultJit(Core::System& system) : StubJit(system) {}
  bool HandleFault(uintptr_t address, SContext*) override
  {
    if (address != reinterpret_cast<uintptr_t>(page))
      return false;
    Common::UnWriteProtectMemory(page, PAGE, false);
    ++handled;
    return true;
  }
  void* page = nullptr;
  int handled = 0;
};

// A page the JIT opens when it is written to, as with fastmem.
FaultJit* MakeJit()
{
  auto& system = Core::System::GetInstance();
  auto jit = std::make_unique<FaultJit>(system);
  FaultJit* const result = jit.get();
  result->page = Common::AllocateMemoryPages(PAGE);
  system.GetJitInterface().SetJit(std::move(jit));
  Core::DeclareAsCPUThread();
  return result;
}

void WriteTo(void* address)
{
  *static_cast<volatile int*>(address) = 5;
}
}  // namespace

TEST_F(CrashRecordTest, CleanExitLeavesNothing)
{
  const int status = RunChild([] { CrashRecord::CleanExit(); });
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_FALSE(HasMarker());
  EXPECT_FALSE(HasRecord());
}

TEST_F(CrashRecordTest, NothingIsWrittenAfterACleanExit)
{
  RunChild([] {
    CrashRecord::CleanExit();
    CrashRecord::NoteCause(CrashRecord::Cause::DeviceLost, "late");
    WriteTo(NOWHERE);
  });
  EXPECT_FALSE(HasMarker());
  EXPECT_FALSE(HasRecord());
}

TEST_F(CrashRecordTest, KilledLeavesTheMarker)
{
  const int status = RunChild([] {
    CrashRecord::NoteGame({"GM8E01", 2, "Title\nwith a line", "Flat", "gpu", "driver", "A: 1\nB: 2\n"});
    raise(SIGKILL);
  });
  EXPECT_EQ(WTERMSIG(status), SIGKILL);
  EXPECT_TRUE(HasMarker());
  EXPECT_FALSE(HasRecord());
  const auto run = ProblemReport::TakeLastRun(m_dir, &ProblemReport::IsOtherInstance);
  ASSERT_TRUE(run);
  EXPECT_FALSE(run->crash);
  EXPECT_EQ(run->game.id, "GM8E01");
  EXPECT_EQ(run->game.revision, 2);
  EXPECT_EQ(run->game.title, "Title with a line");
  EXPECT_EQ(run->settings, (std::vector<std::string>{"A: 1", "B: 2"}));
  EXPECT_EQ(ProblemReport::Classify(*run), ProblemReport::Kind::Unexpected);
}

TEST_F(CrashRecordTest, Fault)
{
  const auto run = Expect(RunChild([] { WriteTo(NOWHERE); }), SIGSEGV);
  EXPECT_EQ(run.address, reinterpret_cast<u64>(NOWHERE));
  EXPECT_EQ(run.code, SEGV_MAPERR);
  EXPECT_EQ(ProblemReport::Classify(run), ProblemReport::Kind::Crashed);
  // The stack starts where it happened, in this program, which has a build ID.
  const auto report = ProblemReport::BuildCrash(run, {});
  ASSERT_FALSE(report.frames.empty());
  EXPECT_EQ(report.frames.front().address, run.pc);
  ASSERT_GE(report.frames.front().module, 0);
  const auto& module = report.modules[static_cast<std::size_t>(report.frames.front().module)];
  EXPECT_EQ(fs::path(module.path).filename(), fs::read_symlink("/proc/self/exe").filename());
  EXPECT_FALSE(module.build_id.empty());
}

TEST_F(CrashRecordTest, OtherSignals)
{
  EXPECT_EQ(Expect(RunChild([] { std::abort(); }), SIGABRT).signal, SIGABRT);
  EXPECT_EQ(Expect(RunChild([] { raise(SIGFPE); }), SIGFPE).signal, SIGFPE);
  EXPECT_EQ(Expect(RunChild([] { raise(SIGBUS); }), SIGBUS).signal, SIGBUS);
  EXPECT_EQ(Expect(RunChild([] { raise(SIGILL); }), SIGILL).signal, SIGILL);
  EXPECT_EQ(Expect(RunChild([] { raise(SIGTRAP); }), SIGTRAP).signal, SIGTRAP);
}

TEST_F(CrashRecordTest, Terminate)
{
  const auto run = Expect(RunChild([] { std::terminate(); }), SIGABRT);
  EXPECT_EQ(run.exception, "std::terminate called");
}

TEST_F(CrashRecordTest, UncaughtException)
{
  // (Thrown by the standard library: the tests have no exceptions.)
  const auto run = Expect(RunChild([] {
                            std::string s;
                            volatile std::size_t at = 5;
                            s.at(at) = 'x';
                          }),
                          SIGABRT);
  EXPECT_TRUE(run.exception.starts_with("std::out_of_range: ")) << run.exception;
}

TEST_F(CrashRecordTest, OutOfMemory)
{
  const auto run = Expect(RunChild([] {
                            volatile std::size_t size = ~std::size_t{0} / 4;
                            std::string s;
                            s.reserve(size);
                            WriteTo(s.data());
                          }),
                          SIGABRT);
  EXPECT_EQ(ProblemReport::Classify(run), ProblemReport::Kind::OutOfMemory) << run.exception;
}

TEST_F(CrashRecordTest, NotedCause)
{
  const auto run = Expect(RunChild([] {
                            CrashRecord::NoteCause(CrashRecord::Cause::DeviceLost, "In a test");
                            std::abort();
                          }),
                          SIGABRT);
  EXPECT_EQ(run.detail, "In a test");
  EXPECT_EQ(ProblemReport::Classify(run), ProblemReport::Kind::DeviceLost);
}

TEST_F(CrashRecordTest, TheCauseIsForgottenWithTheGame)
{
  RunChild([] {
    CrashRecord::NoteGame({.id = "GM8E01"});
    CrashRecord::NoteCause(CrashRecord::Cause::DeviceLost, "In a test");
    CrashRecord::NoteGame({});
    raise(SIGKILL);
  });
  EXPECT_FALSE(Read(CrashRecord::RUNNING_PREFIX).contains("cause="));
}

// What fastmem and the recorder do to each other.

TEST_F(CrashRecordTest, AFaultOfTheJitLeavesNoRecord)
{
  const int status = RunChild([] {
    FaultJit* const jit = MakeJit();
    // A game that is stopped, and another one.
    for (int game = 0; game < 2; ++game)
    {
      EMM::InstallExceptionHandler();
      for (int i = 0; i < 100; ++i)
      {
        Common::WriteProtectMemory(jit->page, PAGE, false);
        WriteTo(jit->page);
      }
      EMM::UninstallExceptionHandler();
    }
    CrashRecord::CleanExit();
    _exit(jit->handled == 200 ? 0 : 1);
  });
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  EXPECT_FALSE(HasRecord());
  EXPECT_FALSE(HasMarker());
}

TEST_F(CrashRecordTest, AFaultTheJitDoesNotHandle)
{
  const auto run = Expect(RunChild([] {
                            FaultJit* const jit = MakeJit();
                            EMM::InstallExceptionHandler();
                            Common::WriteProtectMemory(jit->page, PAGE, false);
                            WriteTo(jit->page);
                            if (jit->handled == 1)
                              WriteTo(NOWHERE);
                          }),
                          SIGSEGV);
  // As it was: not one that was raised again.
  EXPECT_EQ(run.address, reinterpret_cast<u64>(NOWHERE));
  EXPECT_EQ(run.code, SEGV_MAPERR);
  const auto report = ProblemReport::BuildCrash(run, {});
  ASSERT_FALSE(report.frames.empty());
  EXPECT_EQ(report.frames.front().address, run.pc);
}

TEST_F(CrashRecordTest, AfterAGameWasStopped)
{
  const auto run = Expect(RunChild([] {
                            MakeJit();
                            for (int game = 0; game < 2; ++game)
                            {
                              EMM::InstallExceptionHandler();
                              EMM::UninstallExceptionHandler();
                            }
                            WriteTo(NOWHERE);
                          }),
                          SIGSEGV);
  EXPECT_EQ(run.address, reinterpret_cast<u64>(NOWHERE));
}

TEST_F(CrashRecordTest, ASignalThatWasSent)
{
  const auto run = Expect(RunChild([] {
                            MakeJit();
                            EMM::InstallExceptionHandler();
                            kill(getpid(), SIGSEGV);
                            sleep(20);
                          }),
                          SIGSEGV);
  EXPECT_EQ(run.code, SI_USER);
}

#ifdef __x86_64__
TEST_F(CrashRecordTest, AnAddressThatCannotBe)
{
  // The kernel does not say which (SI_KERNEL).
  const auto run = Expect(RunChild([] {
                            MakeJit();
                            EMM::InstallExceptionHandler();
                            WriteTo(reinterpret_cast<void*>(0x1234567812345678));
                          }),
                          SIGSEGV);
  EXPECT_EQ(run.code, SI_KERNEL);
}
#endif

TEST_F(CrashRecordTest, AChildIsNotTheRun)
{
  // A program that is being started, between fork and exec.
  const int status = RunChild([] {
    const pid_t pid = fork();
    if (pid == 0)
      WriteTo(NOWHERE);
    int child = 0;
    waitpid(pid, &child, 0);
    _exit(WIFSIGNALED(child) && WTERMSIG(child) == SIGSEGV ? 0 : 1);
  });
  EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  EXPECT_TRUE(HasMarker());
  EXPECT_EQ(std::ranges::distance(fs::directory_iterator(m_dir), fs::directory_iterator()), 1);
}
