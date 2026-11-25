// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#include <picojson.h>

#ifdef __linux__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "Common/CrashRecord.h"
#include "Common/FileUtil.h"
#include "Common/OpenPage.h"
#include "UICommon/ProblemReport.h"
#include "UICommon/ProblemReportSend.h"

#include <gtest/gtest.h>

namespace
{
namespace fs = std::filesystem;
using namespace ProblemReport;

// Somebody's: nobody's.
const Scrubber SOMEBODY{"/home/player", "player", "player-frame"};

class TempDir
{
public:
  TempDir() : m_path(File::CreateTempDir() + "/") {}
  ~TempDir() { fs::remove_all(m_path); }
  const std::string& Get() const { return m_path; }
  std::string operator/(std::string_view name) const { return m_path + std::string(name); }

private:
  std::string m_path;
};

void WriteFile(const std::string& path, std::string_view text)
{
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string ReadFile(const std::string& path)
{
  std::ifstream f(path, std::ios::binary);
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}

void SetAge(const std::string& path, s64 time)
{
  fs::last_write_time(path, fs::file_time_type::clock::from_sys(
                                std::chrono::sys_seconds(std::chrono::seconds(time))));
}

std::vector<std::pair<std::string, std::string>> ReadZip(const std::string& path)
{
  std::vector<std::pair<std::string, std::string>> files;
  void* reader = mz_zip_reader_create();
  if (mz_zip_reader_open_file(reader, path.c_str()) == MZ_OK &&
      mz_zip_reader_goto_first_entry(reader) == MZ_OK)
  {
    do
    {
      mz_zip_file* info = nullptr;
      mz_zip_reader_entry_get_info(reader, &info);
      std::string data(static_cast<std::size_t>(info->uncompressed_size), '\0');
      EXPECT_EQ(mz_zip_reader_entry_save_buffer(reader, data.data(), static_cast<s32>(data.size())),
                MZ_OK);
      files.emplace_back(info->filename, std::move(data));
    } while (mz_zip_reader_goto_next_entry(reader) == MZ_OK);
  }
  mz_zip_reader_delete(&reader);
  return files;
}

constexpr const char* MAPS =
    "00400000-00500000 r--p 00000000 103:02 12 /home/player/baiji/bin/baiji\n"
    "00500000-00900000 r-xp 00100000 103:02 12 /home/player/baiji/bin/baiji\n"
    "7f0000000000-7f0000010000 rw-p 00000000 00:00 0 [heap]\n"
    "7f1000000000-7f1000100000 r-xp 00000000 103:02 44 /usr/lib/libc.so.6 (deleted)\n"
    "7f2000000000-7f2000100000 rw-s 00000000 00:05 9 /dev/dri/renderD128\n";

LastRun MakeRun()
{
  LastRun run;
  run.started = 1700000000;
  run.program = {"0.9.0 (abc, main)", "baijivr@0.9.0", "/home/player/baiji/bin/baiji",
                 "00112233445566778899aabbccddeeff00112233"};
  run.game.id = "GM8E01";
  run.game.revision = 2;
  run.game.title = "Metroid Prime";
  run.game.display = "Stereoscopic 3D";
  run.game.gpu = "Turnip";
  run.game.driver = "turnip Mesa 26.1";
  run.settings = {"Display: Stereoscopic 3D", "Folder: /home/player/x"};
  return run;
}

LastRun MakeCrashRun()
{
  LastRun run = MakeRun();
  run.crash = true;
  run.signal = 11;
  run.code = 1;
  run.address = 0x10;
  run.thread = "Video thread";
  run.pc = 0x500010;
  // The recorder's own, the signal's trampoline, and from there on what crashed.
  run.frames = {0x600000, 0x7f1000000010, 0x500010, 0x500200};
  run.symbols = {"/home/player/baiji/bin/baiji(Handle+0x1) [0x600000]",
                 "/usr/lib/libc.so.6(+0x10) [0x7f1000000010]",
                 "/home/player/baiji/bin/baiji(Draw+0x1c) [0x500010]",
                 "/home/player/baiji/bin/baiji(+0x200) [0x500200]"};
  run.maps = MAPS;
  return run;
}

Context MakeContext(const std::string& log_dir)
{
  Context context;
  context.log_dir = log_dir;
  context.os = "SteamOS 3.8 (Linux 6.1, aarch64) on player-frame";
  context.settings = {"Display: Flat"};
  context.mods = {"60 FPS: On", "HD Textures 1.2: Off"};
  context.scrub = SOMEBODY;
  return context;
}

picojson::value ParseJson(const std::string& text)
{
  picojson::value value;
  EXPECT_EQ(picojson::parse(value, text), "");
  return value;
}

struct EnvelopeItem
{
  picojson::value header;
  std::string data;
};

// As the server reads it: by the lengths.
std::vector<EnvelopeItem> ParseEnvelope(std::string_view envelope, picojson::value* header)
{
  const auto line = [&envelope] {
    const std::size_t end = envelope.find('\n');
    std::string s(envelope.substr(0, end));
    envelope.remove_prefix(end == std::string_view::npos ? envelope.size() : end + 1);
    return s;
  };
  *header = ParseJson(line());
  std::vector<EnvelopeItem> items;
  while (!envelope.empty())
  {
    EnvelopeItem item;
    item.header = ParseJson(line());
    const auto length = static_cast<std::size_t>(item.header.get("length").get<double>());
    item.data = std::string(envelope.substr(0, length));
    EXPECT_EQ(item.data.size(), length);
    EXPECT_EQ(envelope.substr(length, 1), "\n");
    envelope.remove_prefix(length + 1);
    items.push_back(std::move(item));
  }
  return items;
}
}  // namespace

// ---- Scrubbing ----

TEST(ProblemReport, ScrubNames)
{
  EXPECT_EQ(SOMEBODY.Apply("open /home/player/Games/a.iso"), "open ~/Games/a.iso");
  EXPECT_EQ(SOMEBODY.Apply("file:///home/player"), "file://~");
  EXPECT_EQ(SOMEBODY.Apply("/home/player2/a"), "/home/player2/a");
  EXPECT_EQ(SOMEBODY.Apply("/media/player/card"), "/media/<user>/card");
  EXPECT_EQ(SOMEBODY.Apply("multiplayer players"), "multiplayer players");
  EXPECT_EQ(SOMEBODY.Apply("on player-frame."), "on <host>.");
  EXPECT_EQ(SOMEBODY.Apply(""), "");
  // What every Steam Frame and Deck has is nobody's name.
  const Scrubber steam{"/home/steamos/", "steamos", "steamos"};
  EXPECT_EQ(steam.Apply("/home/steamos/x steamos SteamOS"), "~/x steamos SteamOS");
  const Scrubber deck{"/home/deck", "deck", "x"};
  EXPECT_EQ(deck.Apply("/run/media/deck/x"), "/run/media/deck/x");
  EXPECT_EQ((Scrubber{"/", "", ""}).Apply("/a/b"), "/a/b");
}

TEST(ProblemReport, ScrubNetwork)
{
  EXPECT_EQ(ScrubNetwork("at 192.168.0.12, then"), "at <ip>, then");
  EXPECT_EQ(ScrubNetwork("10.0.0.1:5000"), "<ip>:5000");
  EXPECT_EQ(ScrubNetwork("(10.0.0.1)."), "(<ip>).");
  EXPECT_EQ(ScrubNetwork("Connected to 10.0.0.1."), "Connected to <ip>.");
  EXPECT_EQ(ScrubNetwork("10.0.0.1-10.0.0.9"), "<ip>-<ip>");
  EXPECT_EQ(ScrubNetwork("10.0.0.1/24"), "<ip>/24");
  EXPECT_EQ(ScrubNetwork("fe80::1ff:fe23:4567:890a%eth0"), "<ip>%eth0");
  EXPECT_EQ(ScrubNetwork("[2001:db8::1]:443"), "[<ip>]:443");
  EXPECT_EQ(ScrubNetwork("2001:0db8:0000:0000:0000:ff00:0042:8329"), "<ip>");
  EXPECT_EQ(ScrubNetwork("::1 and ::ffff:1.2.3.4"), "<ip> and <ip>");
  EXPECT_EQ(ScrubNetwork("fe80::1: no route"), "<ip>: no route");
  EXPECT_EQ(ScrubNetwork("mac aa:bb:cc:dd:ee:ff."), "mac <mac>.");
  EXPECT_EQ(ScrubNetwork("AA-BB-CC-00-11-22"), "<mac>");
  // What is none.
  for (const char* const text :
       {"00:12:345 Core/Core.cpp:12 I[CORE]: x", "Foo::Bar", "a::b::c", "std::vector", "v1.2.3.4",
        "1.2.3", "1.2.3.4.5", "256.1.1.1", "0x1234", "12:34:56", "Mesa 26.1.0", "dead::beefcake",
        "SDL3 3.2.20", "...", "::", "a:b", "End."})
  {
    EXPECT_EQ(ScrubNetwork(text), text);
  }
}

// ---- The run before ----

TEST(ProblemReport, MarkerRoundTrip)
{
  CrashRecord::Game game{"GM8E01", 2, "Metroid = Prime", "Flat", "gpu", "driver 1", "A: b=c\n\nD: e\n"};
  const std::string text =
      CrashRecord::MarkerText(77, 1700000000, {"1 (a)", "baijivr@1", "/x/baiji", "ab"}, game,
                              CrashRecord::Cause::DeviceLost, "Device lost in vkQueueSubmit");
  LastRun run;
  ParseMarker(text, &run);
  EXPECT_EQ(run.pid, 77);
  EXPECT_EQ(run.started, 1700000000);
  EXPECT_EQ(run.program.version, "1 (a)");
  EXPECT_EQ(run.program.exe, "/x/baiji");
  EXPECT_EQ(run.program.build_id, "ab");
  EXPECT_EQ(run.game.title, "Metroid = Prime");
  EXPECT_EQ(run.settings, (std::vector<std::string>{"A: b=c", "D: e"}));
  EXPECT_EQ(Classify(run), Kind::DeviceLost);
  EXPECT_EQ(run.detail, "Device lost in vkQueueSubmit");
  // Cut off in the middle.
  for (std::size_t size = 0; size < text.size(); ++size)
  {
    LastRun part;
    ParseMarker(std::string_view(text).substr(0, size), &part);
  }
}

TEST(ProblemReport, LastRunIsTakenOnce)
{
  const TempDir dir;
  const auto none = [](int) { return false; };
  EXPECT_FALSE(TakeLastRun(dir / "missing/", none));
  EXPECT_FALSE(TakeLastRun(dir.Get(), none));
  WriteFile(dir / "running-100", "pid=100\nstarted=10\nversion=old\n");
  WriteFile(dir / "running-200", "pid=200\nstarted=20\nversion=new\n");
  WriteFile(dir / "crash-200", "signal=11\ncode=1\n");
  WriteFile(dir / "running-200.tmp", "pi");
  WriteFile(dir / "running-300", "pid=300\nstarted=30\nversion=live\n");
  WriteFile(dir / "notes.txt", "not one of them");
  const auto alive = [](int pid) { return pid == 300; };

  // A start that cannot show it: the newest is kept for one that can.
  EXPECT_FALSE(TakeLastRun(dir.Get(), alive, false));
  EXPECT_FALSE(fs::exists(dir / "running-100"));
  EXPECT_TRUE(fs::exists(dir / "running-200"));

  const auto run = TakeLastRun(dir.Get(), alive);
  ASSERT_TRUE(run);
  EXPECT_EQ(run->program.version, "new");
  EXPECT_TRUE(run->crash);
  EXPECT_EQ(Classify(*run), Kind::Crashed);
  EXPECT_FALSE(TakeLastRun(dir.Get(), alive));
  std::set<std::string> left;
  for (const auto& entry : fs::directory_iterator(dir.Get()))
    left.insert(entry.path().filename().string());
  EXPECT_EQ(left, (std::set<std::string>{"notes.txt", "running-300"}));
}

TEST(ProblemReport, OtherInstances)
{
  EXPECT_FALSE(IsOtherInstance(0));
  EXPECT_FALSE(IsOtherInstance(-1));
#ifdef __linux__
  // This one, and one that is not Baiji VR.
  EXPECT_FALSE(IsOtherInstance(static_cast<int>(getpid())));
  EXPECT_FALSE(IsOtherInstance(1));
#endif
}

TEST(ProblemReport, Classify)
{
  LastRun run;
  EXPECT_EQ(Classify(run), Kind::Unexpected);
  run.crash = true;
  // A record that was not finished.
  EXPECT_EQ(Classify(run), Kind::Unexpected);
  run.signal = 11;
  EXPECT_EQ(Classify(run), Kind::Crashed);
  run.exception = "std::bad_alloc: std::bad_alloc";
  EXPECT_EQ(Classify(run), Kind::OutOfMemory);
  run.cause = CrashRecord::Cause::DeviceLost;
  EXPECT_EQ(Classify(run), Kind::DeviceLost);
  EXPECT_EQ(ReasonText(Kind::OutOfMemory), "Ran Out of Memory");
  EXPECT_EQ(ReasonText(Kind::DeviceLost), "Graphics Driver Stopped Responding");
  EXPECT_EQ(ReasonText(Kind::Crashed), "Baiji VR Crashed");
  EXPECT_EQ(ReasonText(Kind::Unexpected), "Closed Unexpectedly");
}

// ---- The report ----

TEST(ProblemReport, Modules)
{
  std::vector<MapRange> ranges;
  const auto modules = ParseMaps(MAPS, &ranges);
  ASSERT_EQ(modules.size(), 2u);
  EXPECT_EQ(modules[0].path, "/home/player/baiji/bin/baiji");
  EXPECT_EQ(modules[0].base, 0x400000u);
  EXPECT_EQ(modules[0].end, 0x900000u);
  EXPECT_EQ(modules[1].path, "/usr/lib/libc.so.6");
  EXPECT_EQ(ranges.size(), 3u);
  EXPECT_EQ(DebugId("00112233445566778899aabbccddeeff00112233"),
            "33221100-5544-7766-8899-aabbccddeeff");
  EXPECT_EQ(ReadBuildId("/dev/null"), "");
  EXPECT_EQ(ReadBuildId("/does/not/exist"), "");
}

TEST(ProblemReport, LogsOfARun)
{
  const TempDir dir;
  EXPECT_TRUE(FindLogs(dir.Get(), 0).empty());
  const auto log = [&dir](const char* name, s64 time) {
    WriteFile(dir / name, "x\n");
    SetAge(dir / name, time);
  };
  log("baiji-launcher.log", 5000);
  log("baiji-launcher.log.old", 4000);
  log("baiji-launcher.log.old2", 3000);
  log("baiji-launcher.log.old3", 2000);
  log("dolphin.log.old", 3000);
  const auto find = [&dir](s64 started) {
    std::vector<std::string> names;
    for (const std::string& path : FindLogs(dir.Get(), started))
      names.push_back(fs::path(path).filename().string());
    return names;
  };
  // This run's, and the one before it.
  EXPECT_EQ(find(0), (std::vector<std::string>{"baiji-launcher.log", "baiji-launcher.log.old",
                                               "dolphin.log.old"}));
  // Of the run that started at 2500: another has run since.
  EXPECT_EQ(find(2500), (std::vector<std::string>{"baiji-launcher.log.old2",
                                                  "baiji-launcher.log.old3", "dolphin.log.old"}));
}

TEST(ProblemReport, LastLinesOfALog)
{
  const TempDir dir;
  std::string text;
  for (int i = 0; i < 50; ++i)
    text += fmt::format("line {}\n", i);
  WriteFile(dir / "a.log", text + "the end");
  const LogFile log = ReadLog(dir / "a.log", 3);
  EXPECT_EQ(log.name, "a.log");
  EXPECT_EQ(log.lines, 3u);
  EXPECT_EQ(log.text, "line 48\nline 49\nthe end\n");
  EXPECT_EQ(ReadLog(dir / "a.log", 2000).lines, 51u);
  EXPECT_EQ(ReadLog(dir / "none.log", 3).lines, 0u);
  // A log without an end: no more than its last megabyte, and no half a line.
  WriteFile(dir / "long.log", std::string(3 << 20, 'x') + "\nlast\n");
  EXPECT_EQ(ReadLog(dir / "long.log", 10).text, "last\n");
}

TEST(ProblemReport, ManualReport)
{
  const TempDir dir;
  WriteFile(dir / "baiji-launcher.log", "opened /home/player/a.iso on player-frame from 10.0.0.7\n");
  LastRun now = MakeRun();
  now.settings.clear();
  now.started = 0;
  Report report = BuildManual(now, MakeContext(dir.Get()));
  SetNote(&report, "  It stopped.\nMy disc is in /home/player/Games  \n", SOMEBODY);
  EXPECT_EQ(ReportText(report), R"(Baiji VR Problem Report

Game: Metroid Prime
Game ID: GM8E01
Revision: 2
Display: Stereoscopic 3D
Version: 0.9.0 (abc, main)
System: SteamOS 3.8 (Linux 6.1, aarch64) on <host>
GPU: Turnip
Driver: turnip Mesa 26.1

What Happened?
  It stopped.
  My disc is in ~/Games

Mods
  60 FPS: On
  HD Textures 1.2: Off

Settings
  Display: Flat
)");
  EXPECT_EQ(FullText(report),
            ReportText(report) +
                "\nbaiji-launcher.log (Last 1 Lines)\nopened ~/a.iso on <host> from <ip>\n");
  const auto files = GetFiles(report);
  ASSERT_EQ(files.size(), 2u);
  EXPECT_EQ(files[0].first, "report.txt");
  EXPECT_EQ(files[1].first, "baiji-launcher.log");
}

TEST(ProblemReport, WithoutAGame)
{
  LastRun now;
  now.program.version = "0.9.0";
  const std::string text = ReportText(BuildManual(now, MakeContext("/does/not/exist/")));
  EXPECT_TRUE(text.contains("\nGame: None\nVersion: 0.9.0\n")) << text;
  EXPECT_TRUE(text.contains("\nGPU: Unknown\nDriver: Unknown\n")) << text;
  EXPECT_FALSE(text.contains("What Happened?"));
}

TEST(ProblemReport, CrashReport)
{
  const Report report = BuildCrash(MakeCrashRun(), MakeContext("/does/not/exist/"));
  EXPECT_EQ(report.headline, "Baiji VR Closed Unexpectedly");
  EXPECT_EQ(report.kind, Kind::Crashed);
  // From where it happened on.
  ASSERT_EQ(report.frames.size(), 2u);
  EXPECT_EQ(report.frames[0].symbol, "Draw+0x1c");
  EXPECT_EQ(report.frames[0].offset, 0x100010u);
  EXPECT_EQ(report.frames[1].symbol, "");
  ASSERT_EQ(report.modules.size(), 1u);
  EXPECT_EQ(report.modules[0].path, "~/baiji/bin/baiji");
  EXPECT_EQ(report.modules[0].build_id, "00112233445566778899aabbccddeeff00112233");
  const std::string text = ReportText(report);
  EXPECT_TRUE(text.starts_with("Baiji VR Crash Report\n\nReason: Baiji VR Crashed\n")) << text;
  EXPECT_TRUE(text.contains(R"(
Crash
  SIGSEGV (11), code 1, address 0x0000000000000010, thread Video thread
  #0  0x0000000000500010 baiji + 0x100010 (Draw+0x1c)
  #1  0x0000000000500200 baiji + 0x100200

Modules
  ~/baiji/bin/baiji 0x0000000000400000-0x0000000000900000 build id 00112233445566778899aabbccddeeff00112233
)")) << text;
  // Its own settings, not those of now.
  EXPECT_TRUE(text.contains("  Folder: ~/x\n"));
  EXPECT_FALSE(text.contains("Display: Flat"));
  EXPECT_FALSE(text.contains("player"));

  // Without a stack there is still where it was.
  LastRun bare = MakeCrashRun();
  bare.frames.clear();
  bare.symbols.clear();
  EXPECT_EQ(BuildCrash(bare, {}).frames.size(), 1u);
  // And nothing at all when it was killed.
  LastRun killed = MakeRun();
  const Report unexpected = BuildCrash(killed, {});
  EXPECT_EQ(unexpected.reason, "Closed Unexpectedly");
  EXPECT_TRUE(unexpected.frames.empty());
  EXPECT_FALSE(ReportText(unexpected).contains("\nCrash\n"));
}

TEST(ProblemReport, Zip)
{
  const TempDir dir;
  WriteFile(dir / "logs/baiji-launcher.log", std::string(5000, 'a') + "\nb\n");
  WriteFile(dir / "logs/baiji-launcher.log.old", "c\n");
  LastRun now = MakeRun();
  now.started = 0;
  const Report report = BuildManual(now, MakeContext(dir / "logs/"));
  std::string error;
  // The folder is made.
  const std::string path = SaveZip(report, dir / "home/Downloads", 1700000000, &error);
  ASSERT_FALSE(path.empty()) << error;
  EXPECT_EQ(fs::path(path).filename(), ZipName(1700000000));
  EXPECT_EQ(ReadZip(path), GetFiles(report));
  EXPECT_EQ(ReadZip(path).size(), 3u);
  // Another at the same time is another.
  const std::string second = SaveZip(report, dir / "home/Downloads", 1700000000, &error);
  EXPECT_NE(second, path);
  EXPECT_EQ(ReadZip(second), GetFiles(report));
  // Where none can be.
  WriteFile(dir / "file", "x");
  EXPECT_TRUE(SaveZip(report, dir / "file/Downloads", 1700000000, &error).empty());
  EXPECT_FALSE(error.empty());

  const std::string name = ZipName(1700000000);
  EXPECT_TRUE(name.starts_with("baijivr-report-2023111"));
  EXPECT_TRUE(name.ends_with(".zip"));
  EXPECT_EQ(name.size(), std::string("baijivr-report-20231114-221320.zip").size());
}

// ---- Sending ----

TEST(ProblemReport, Dsn)
{
  const auto dsn = ParseDsn("https://abc123@o1.ingest.example.com/456");
  ASSERT_TRUE(dsn);
  EXPECT_EQ(dsn->url, "https://o1.ingest.example.com/api/456/envelope/");
  EXPECT_EQ(dsn->key, "abc123");
  EXPECT_EQ(ParseDsn("http://k:secret@127.0.0.1:9000/a/b/7/")->url,
            "http://127.0.0.1:9000/a/b/api/7/envelope/");
  EXPECT_EQ(ParseDsn("http://k:secret@127.0.0.1:9000/7")->key, "k");
  for (const char* const bad : {"", "x", "https://host/1", "https://@host/1", "https://k@host",
                                "https://k@/1", "ftp://k@host/1", "k@https://host/1", "https://k@host/"})
  {
    EXPECT_FALSE(ParseDsn(bad)) << bad;
  }
}

TEST(ProblemReport, EventId)
{
  const std::string id = MakeEventId();
  EXPECT_EQ(id.size(), 32u);
  EXPECT_EQ(id.find_first_not_of("0123456789abcdef"), std::string::npos);
  EXPECT_EQ(id[12], '4');
  EXPECT_NE(id, MakeEventId());
}

TEST(ProblemReport, Title)
{
  Report report;
  EXPECT_EQ(MakeTitle(report), "Problem Report");
  report.note = "First \nSecond";
  EXPECT_EQ(MakeTitle(report), "First");
  report.note = std::string(116, 'a') + "éééé";
  EXPECT_EQ(MakeTitle(report), std::string(116, 'a') + "...");
  EXPECT_EQ(MakeTitle(BuildCrash(MakeCrashRun(), {})), "Baiji VR Closed Unexpectedly: Baiji VR Crashed");
  EXPECT_EQ(MakeTitle(BuildCrash(MakeRun(), {})), "Baiji VR Closed Unexpectedly");
}

TEST(ProblemReport, EnvelopeBytes)
{
  Report report;
  report.headline = "Report a Problem";
  report.version = "0.9.0 a";
  report.release = "baijivr@0.9.0";
  report.os = "SteamOS";
  report.note = "Hi \"you\"";
  report.logs.push_back({"baiji-launcher.log", "one\ntwo\n", 2});
  const std::string text = ReportText(report);
  const std::string event =
      R"({"environment":"development","event_id":"0123","extra":{"note":"Hi \"you\"",)"
      R"("system":"SteamOS","version":"0.9.0 a"},"level":"warning",)"
      R"("message":{"formatted":"Hi \"you\""},"platform":"native","release":"baijivr@0.9.0",)"
      R"("sdk":{"name":"baijivr","settings":{"infer_ip":"never"},"version":"1"},"server_name":"",)"
      R"("tags":{"reason":"manual","report":"manual"},"timestamp":1700000000,)"
      R"("user":{"geo":{}}})";
  EXPECT_EQ(MakeEnvelope(report, "0123", 1700000000),
            R"({"event_id":"0123"})"
            "\n" +
                fmt::format(R"({{"content_type":"application\/json","length":{},"type":"event"}})",
                            event.size()) +
                "\n" + event + "\n" +
                fmt::format(R"({{"content_type":"text\/plain","filename":"report.txt","length":{},)"
                            R"("type":"attachment"}})",
                            text.size()) +
                "\n" + text + "\n" +
                R"({"content_type":"text\/plain","filename":"baiji-launcher.log","length":8,)"
                R"("type":"attachment"})"
                "\none\ntwo\n\n");
}

TEST(ProblemReport, EnvelopeOfACrash)
{
  const TempDir dir;
  WriteFile(dir / "baiji-launcher.log", "/home/player/x\n");
  Report report = BuildCrash(MakeCrashRun(), MakeContext(dir.Get()));
  SetNote(&report, "I opened /home/player/x", SOMEBODY);
  const std::string envelope = MakeEnvelope(report, std::string(32, 'a'), 1700000000);
  picojson::value header;
  const auto items = ParseEnvelope(envelope, &header);
  EXPECT_EQ(header.serialize(), R"({"event_id":")" + std::string(32, 'a') + R"("})");
  ASSERT_EQ(items.size(), 3u);
  const picojson::value event = ParseJson(items[0].data);
  EXPECT_EQ(event.get("level").to_str(), "fatal");
  EXPECT_EQ(event.get("release").to_str(), "baijivr@0.9.0");
  EXPECT_EQ(event.get("tags").get("report").to_str(), "crash");
  EXPECT_EQ(event.get("tags").get("mode").to_str(), "vr");
  EXPECT_EQ(event.get("tags").get("game_id").to_str(), "GM8E01");
  const picojson::value exception = event.get("exception").get("values").get(0);
  EXPECT_EQ(exception.get("type").to_str(), "SIGSEGV");
  const picojson::value frames = exception.get("stacktrace").get("frames");
  // The oldest call first.
  ASSERT_EQ(frames.get<picojson::array>().size(), 2u);
  EXPECT_EQ(frames.get(1).get("function").to_str(), "Draw");
  EXPECT_EQ(frames.get(1).get("instruction_addr").to_str(), "0x500010");
  EXPECT_EQ(frames.get(1).get("image_addr").to_str(), "0x400000");
  const picojson::value image = event.get("debug_meta").get("images").get(0);
  EXPECT_EQ(image.get("type").to_str(), "elf");
  EXPECT_EQ(image.get("debug_id").to_str(), "33221100-5544-7766-8899-aabbccddeeff");
  EXPECT_EQ(image.get("image_size").get<double>(), 0x500000);

  // Nobody's name, and nothing but what See What's Sent shows.
  EXPECT_FALSE(envelope.contains("player"));
  EXPECT_EQ(event.get("server_name").to_str(), "");
  EXPECT_EQ(event.get("user").serialize(), R"({"geo":{}})");
  EXPECT_EQ(event.get("sdk").get("settings").get("infer_ip").to_str(), "never");
  std::set<std::string> keys;
  for (const auto& [key, value] : event.get<picojson::object>())
    keys.insert(key);
  EXPECT_EQ(keys, (std::set<std::string>{"debug_meta", "environment", "event_id", "exception",
                                         "extra", "level", "message", "platform", "release", "sdk",
                                         "server_name", "tags", "timestamp", "user"}));
  EXPECT_EQ(items[1].header.get("filename").to_str(), "report.txt");
  EXPECT_EQ(items[1].data, ReportText(report));
  EXPECT_EQ(items[2].data, "~/x\n");
}

#ifdef __linux__
namespace
{
// A server on this machine that answers one request.
class StubServer
{
public:
  explicit StubServer(std::string answer) : m_answer(std::move(answer))
  {
    m_socket = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    EXPECT_EQ(listen(m_socket, 1), 0);
    socklen_t size = sizeof(address);
    getsockname(m_socket, reinterpret_cast<sockaddr*>(&address), &size);
    m_port = ntohs(address.sin_port);
    m_thread = std::thread(&StubServer::Run, this);
  }
  ~StubServer()
  {
    Wait();
    close(m_socket);
  }
  void Wait()
  {
    if (!m_thread.joinable())
      return;
    m_stop = true;
    shutdown(m_socket, SHUT_RDWR);
    m_thread.join();
  }
  std::string GetDsn() const { return fmt::format("http://testkey@127.0.0.1:{}/42", m_port); }
  const std::string& GetRequest() const { return m_request; }

private:
  void Run()
  {
    const int client = accept4(m_socket, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0)
      return;
    const timeval wait{0, 100000};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
    std::size_t total = std::string::npos;
    char buffer[4096];
    while (!m_stop && m_request.size() < total)
    {
      const ssize_t size = recv(client, buffer, sizeof(buffer), 0);
      if (size == 0)
        break;
      if (size < 0)
        continue;
      m_request.append(buffer, static_cast<std::size_t>(size));
      const std::size_t body = m_request.find("\r\n\r\n");
      const std::size_t length = m_request.find("Content-Length: ");
      if (body != std::string::npos && length < body)
        total = body + 4 + std::strtoul(m_request.c_str() + length + 16, nullptr, 10);
    }
    if (m_answer.empty())
    {
      // Never answers.
      while (!m_stop && recv(client, buffer, sizeof(buffer), 0) != 0)
      {
      }
    }
    else
    {
      const std::string answer =
          "HTTP/1.1 " + m_answer + "\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
      send(client, answer.data(), answer.size(), MSG_NOSIGNAL);
    }
    close(client);
  }

  std::string m_answer;
  std::string m_request;
  std::atomic<bool> m_stop{false};
  std::thread m_thread;
  int m_socket = -1;
  int m_port = 0;
};

int RunCommand(const std::string& command)
{
  const int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::string GetRepository()
{
  return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path().string();
}
}  // namespace

TEST(ProblemReport, SendToAStub)
{
  Report report = BuildCrash(MakeCrashRun(), MakeContext("/does/not/exist/"));
  SetNote(&report, "A test", SOMEBODY);
  StubServer server("200 OK");
  const SendResult result = Send(report, server.GetDsn());
  server.Wait();
  EXPECT_TRUE(result.ok) << result.error;
  const std::string& request = server.GetRequest();
  EXPECT_TRUE(request.starts_with("POST /api/42/envelope/ HTTP/1.1\r\n")) << request.substr(0, 200);
  const std::size_t body = request.find("\r\n\r\n");
  ASSERT_NE(body, std::string::npos);
  const std::string headers = request.substr(0, body + 2);
  EXPECT_TRUE(headers.contains("\r\nX-Sentry-Auth: Sentry sentry_version=7, "
                               "sentry_client=baijivr/1, sentry_key=testkey\r\n"));
  EXPECT_TRUE(headers.contains("\r\nContent-Type: application/x-sentry-envelope\r\n"));
  EXPECT_TRUE(headers.contains("\r\nUser-Agent: baijivr/1\r\n"));
  EXPECT_FALSE(headers.contains("Expect:"));
  picojson::value header;
  const auto items = ParseEnvelope(std::string_view(request).substr(body + 4), &header);
  EXPECT_EQ(header.get("event_id").to_str(), result.event_id);
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(ParseJson(items[0].data).get("event_id").to_str(), result.event_id);
  EXPECT_EQ(items[1].data, ReportText(report));
}

TEST(ProblemReport, SendThatFails)
{
  const auto send = [](const char* answer) {
    StubServer server(answer);
    return Send({}, server.GetDsn());
  };
  SendResult result = send("429 Too Many Requests");
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error, "Too Many Reports: Try Again Later");
  EXPECT_TRUE(result.event_id.empty());
  EXPECT_EQ(send("500 No").error, "The Server Said 500");
  // It is not followed.
  EXPECT_EQ(send("302 Found\r\nLocation: http://127.0.0.1:1/").error, "The Server Said 302");
  EXPECT_EQ(Send({}, "").error, "No Place to Send To");

  std::string dsn;
  {
    const StubServer gone("200 OK");
    dsn = gone.GetDsn();
  }
  EXPECT_EQ(Send({}, dsn).error, "Couldn't Connect");
}

TEST(ProblemReport, SendIsCancelled)
{
  StubServer server("");
  std::atomic<bool> cancel{false};
  std::thread player([&cancel] {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cancel = true;
  });
  const auto start = std::chrono::steady_clock::now();
  const SendResult result = Send({}, server.GetDsn(), &cancel);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error, "Cancelled");
  player.join();
}

TEST(ProblemReport, BuildWithoutAnAddress)
{
  // The tests' build: a release has one, and it is not in the repository.
  if (GetBuiltInDsn().empty())
  {
    EXPECT_FALSE(CanSend());
  }
  const std::string script = GetRepository() + "/Tools/report-dsn.sh";
  const TempDir dir;
  const auto run = [&](const std::string& environment, const std::string& arguments) {
    return RunCommand("env -u BAIJI_REPORT_DSN HOME='" + dir.Get() + "' " + environment + " bash '" +
                      script + "' " + arguments + " > '" + (dir / "out") + "' 2>&1");
  };
  EXPECT_EQ(run("", ""), 1);
  EXPECT_EQ(ReadFile(dir / "out"), "Report DSN: not found (no Send Report)\n");
  const std::string to_file = "--file '" + (dir / "dsn") + "'";
  EXPECT_EQ(run("", to_file), 1);
  EXPECT_FALSE(fs::exists(dir / "dsn"));

  EXPECT_EQ(run("BAIJI_REPORT_DSN=https://sekret@example.com/1", to_file), 0);
  EXPECT_EQ(ReadFile(dir / "out"), "Report DSN: found (the environment)\n");
  EXPECT_EQ(ReadFile(dir / "dsn"), "https://sekret@example.com/1\n");
  EXPECT_EQ(fs::status(dir / "dsn").permissions(), fs::perms::owner_read | fs::perms::owner_write);

  WriteFile(dir / ".config/baijivr-dev/sentry-dsn", "https://filekey@example.com/2 \n");
  EXPECT_EQ(run("", to_file), 0);
  EXPECT_EQ(ReadFile(dir / "dsn"), "https://filekey@example.com/2\n");
  EXPECT_FALSE(ReadFile(dir / "out").contains("filekey"));

  EXPECT_EQ(run("BAIJI_REPORT_DSN='https://sekret@example.com/1\"; rm'", to_file), 2);
  EXPECT_FALSE(ReadFile(dir / "out").contains("sekret"));
}

// scripts/baiji-vr with a program that only writes what it is told.
TEST(ProblemReport, TheLauncherKeepsTheLogsOfTheLastRuns)
{
  const std::string script = GetRepository() + "/scripts/baiji-vr";
  ASSERT_TRUE(fs::exists(script));
  for (const bool debug : {false, true})
  {
    const TempDir home;
    const std::string logs = home / ".local/share/baiji/Logs/";
    WriteFile(home / "root/bin/baiji", "#!/usr/bin/env bash\necho \"run $RUN\"\n"
                                       "[[ -z \"$FULL\" ]] || echo \"full $RUN\" >> '" +
                                           logs + "dolphin.log'\n");
    fs::permissions(home / "root/bin/baiji", fs::perms::owner_all);
    if (debug)
      WriteFile(home / ".config/baiji/debug-logging", "");
    const auto run = [&](int number, bool full) {
      return RunCommand(fmt::format("env HOME='{}' BAIJI_ROOT='{}' RUN={} FULL={} bash '{}'",
                                    home.Get(), home / "root", number, full ? "1" : "", script));
    };
    // The first run has nothing to turn over, not even the folder.
    EXPECT_EQ(run(1, false), 0);
    EXPECT_EQ(ReadFile(logs + "baiji-launcher.log"), "run 1\n");
    EXPECT_FALSE(fs::exists(logs + "baiji-launcher.log.old"));
    EXPECT_EQ(run(2, true), 0);
    EXPECT_EQ(ReadFile(logs + "baiji-launcher.log.old"), "run 1\n");
    for (int i = 3; i <= 12; ++i)
      EXPECT_EQ(run(i, i <= 4), 0);
    EXPECT_EQ(ReadFile(logs + "baiji-launcher.log"), "run 12\n");
    EXPECT_EQ(ReadFile(logs + "baiji-launcher.log.old"), "run 11\n");
    for (int i = 2; i <= 8; ++i)
      EXPECT_EQ(ReadFile(fmt::format("{}baiji-launcher.log.old{}", logs, i)), fmt::format("run {}\n", 12 - i));
    EXPECT_FALSE(fs::exists(logs + "baiji-launcher.log.old9"));
    // A run without the other log turns none over.
    EXPECT_FALSE(fs::exists(logs + "dolphin.log"));
    EXPECT_EQ(ReadFile(logs + "dolphin.log.old"), "full 4\n");
    EXPECT_EQ(ReadFile(logs + "dolphin.log.old2"), "full 3\n");
    EXPECT_EQ(ReadFile(logs + "dolphin.log.old3"), "full 2\n");
  }
}

// ---- Opening a page ----

TEST(OpenPage, OnlyPages)
{
  using namespace Common::OpenPage;
  EXPECT_TRUE(IsPage("https://ko-fi.com/sana748653"));
  EXPECT_TRUE(IsPage("https://example.com/a?b=c&d=%20#e"));
  for (const char* const bad :
       {"", "https://", "http://example.com", "file:///etc/passwd", "steam://run/1", "HTTPS://x.com",
        "https://example.com/a b", "https://example.com/\n", "https://example.com/\"x", "-x",
        "https://example.com/`id`", " https://example.com"})
  {
    EXPECT_FALSE(IsPage(bad)) << bad;
    EXPECT_TRUE(MakeArguments(Opener::Steam, bad).empty()) << bad;
    EXPECT_TRUE(MakeArguments(Opener::Xdg, bad).empty()) << bad;
    EXPECT_EQ(Open(bad), Opener::Nothing) << bad;
  }
  EXPECT_EQ(MakeArguments(Opener::Steam, "https://ko-fi.com/sana748653"),
            (std::vector<std::string>{"steam", "steam://openurl/https://ko-fi.com/sana748653"}));
  EXPECT_EQ(MakeArguments(Opener::Xdg, "https://ko-fi.com/sana748653"),
            (std::vector<std::string>{"xdg-open", "https://ko-fi.com/sana748653"}));
  EXPECT_TRUE(MakeArguments(Opener::Nothing, "https://ko-fi.com/sana748653").empty());
}

TEST(OpenPage, Choice)
{
  using namespace Common::OpenPage;
  const TempDir dir;
  const auto program = [&dir](const char* name) {
    WriteFile(dir / name, "#!/bin/sh\n");
    fs::permissions(dir / name, fs::perms::owner_all);
  };
  const std::string path = "/does/not/exist::" + dir.Get();
  EXPECT_EQ(ChooseOpener(path, true), Opener::Nothing);
  EXPECT_EQ(ChooseOpener("", true), Opener::Nothing);
  program("xdg-open");
  EXPECT_EQ(ChooseOpener(path, true), Opener::Xdg);
  WriteFile(dir / "steam", "not a program");
  EXPECT_EQ(ChooseOpener(path, true), Opener::Xdg);
  program("steam");
  EXPECT_EQ(ChooseOpener(path, true), Opener::Steam);
  EXPECT_EQ(ChooseOpener(path, false), Opener::Xdg);
}

TEST(OpenPage, Environment)
{
  const char* const ours[] = {"HOME=/home/player",
                              "LD_LIBRARY_PATH=/home/player/baiji/lib",
                              "LD_PRELOAD=x.so",
                              "QT_PLUGIN_PATH=/x",
                              "QT_QPA_PLATFORM=xcb",
                              "QT_QPA_PLATFORM_PLUGIN_PATH=/x",
                              "XR_RUNTIME_JSON=/x.json",
                              "DISPLAY=:2",
                              "QTX=1",
                              nullptr};
  EXPECT_EQ(Common::OpenPage::CleanEnvironment(ours),
            (std::vector<std::string>{"HOME=/home/player", "DISPLAY=:2", "QTX=1"}));
}

TEST(OpenPage, StartsAndDoesNotWait)
{
  using namespace Common::OpenPage;
  const TempDir dir;
  // It is a stand-in that says what it got, and takes its time.
  for (const char* const name : {"steam", "xdg-open"})
  {
    WriteFile(dir / name, "#!/bin/sh\n/bin/sleep 1\n/bin/echo \"$0 $* [$QT_TEST]\" > '" +
                              (dir / "out.tmp") + "'\n/bin/mv '" + (dir / "out.tmp") + "' '" +
                              (dir / "out") + "'\n");
    fs::permissions(dir / name, fs::perms::owner_all);
  }
  const std::string old_path = std::getenv("PATH");
  const char* const old_desktop = std::getenv("XDG_CURRENT_DESKTOP");
  const std::string desktop = old_desktop ? old_desktop : "";
  std::string path = dir.Get();
  path.pop_back();
  setenv("PATH", path.c_str(), 1);
  setenv("QT_TEST", "ours", 1);
  setenv("XDG_CURRENT_DESKTOP", "gamescope", 1);
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(Open("https://ko-fi.com/sana748653"), Opener::Steam);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(900));
  setenv("PATH", old_path.c_str(), 1);
  unsetenv("QT_TEST");
  if (old_desktop)
    setenv("XDG_CURRENT_DESKTOP", desktop.c_str(), 1);
  else
    unsetenv("XDG_CURRENT_DESKTOP");
  for (int i = 0; i < 50 && !fs::exists(dir / "out"); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(ReadFile(dir / "out"),
            (dir / "steam") + " steam://openurl/https://ko-fi.com/sana748653 []\n");
}
#endif
