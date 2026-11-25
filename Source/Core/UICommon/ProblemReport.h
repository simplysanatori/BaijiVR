// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/CrashRecord.h"

// Report a Problem, and the report of a run that did not end cleanly (Common/CrashRecord.h), after
// Azahar for Frame's. No Qt: Help > Report a Problem and the VR menu's row are its users. What a
// report holds is what "See What's Sent" shows, what Save Report puts in the zip and what Send Report
// sends (ProblemReportSend.h), and all of it is scrubbed. Nothing is sent without the player's tap.
namespace ProblemReport
{
// ---- What the last run left ----

struct LastRun
{
  bool crash = false;  // a fatal signal wrote a record
  int pid = 0;
  s64 started = 0;  // seconds since the epoch
  CrashRecord::Program program;
  CrashRecord::Game game;
  std::vector<std::string> settings;  // "Label: value"
  CrashRecord::Cause cause = CrashRecord::Cause::Nothing;
  std::string detail;
  // The record.
  int signal = 0;
  int code = 0;
  u64 address = 0;
  std::string thread;
  std::string exception;
  u64 pc = 0;
  u64 lr = 0;
  std::vector<u64> frames;           // newest first
  std::vector<std::string> symbols;  // backtrace_symbols lines, in the frames' order
  std::string maps;                  // /proc/self/maps
};

void ParseMarker(std::string_view text, LastRun* run);
void ParseCrash(std::string_view text, LastRun* run);

// `dir` ends with '/'. The newest run that did not end cleanly; its files and those of older ones are
// removed. The ones of a process that still runs (`alive`) are left alone. Without `take` there is
// nothing to show it: the newest stays for a later start.
std::optional<LastRun> TakeLastRun(const std::string& dir, const std::function<bool(int)>& alive,
                                   bool take = true);
// Another process that is this program.
bool IsOtherInstance(int pid);

enum class Kind
{
  Manual,  // Report a Problem
  OutOfMemory,
  DeviceLost,
  Crashed,     // a fatal signal
  Unexpected,  // none of these: killed, frozen, power
};

Kind Classify(const LastRun& run);
// "Baiji VR Crashed"; "" for Manual.
std::string ReasonText(Kind kind);
// "crashed": a tag.
std::string_view KindKey(Kind kind);
std::string SignalName(int signal);

// ---- Scrubbing ----

struct Scrubber
{
  std::string home;  // becomes "~"
  std::string user;  // "<user>", but not SteamOS's own "steamos" and "deck"
  std::string host;  // "<host>"

  // And the network addresses.
  std::string Apply(std::string_view text) const;
  static Scrubber ForThisMachine();
};

// IPv4, IPv6 and MAC addresses become "<ip>" and "<mac>".
std::string ScrubNetwork(std::string_view text);

// ---- The report ----

// One file of /proc/self/maps.
struct Module
{
  std::string path;
  u64 base = 0;  // its mapping at offset 0
  u64 end = 0;
  std::string build_id;  // hex, from the file
};
struct MapRange
{
  u64 start = 0;
  u64 end = 0;
  std::size_t module = 0;
};
// Regular files only.
std::vector<Module> ParseMaps(std::string_view maps, std::vector<MapRange>* ranges = nullptr);
// The GNU build ID of an ELF file, as hex.
std::string ReadBuildId(const std::string& path);
// Sentry's debug ID for one.
std::string DebugId(std::string_view build_id);

struct Frame
{
  u64 address = 0;
  int module = -1;
  u64 offset = 0;
  std::string symbol;
};

struct LogFile
{
  std::string name;
  std::string text;  // its last lines
  std::size_t lines = 0;
};

struct Report
{
  Kind kind = Kind::Manual;
  std::string headline;
  std::string reason;
  std::string detail;
  std::string game_id;  // empty: no game
  u16 revision = 0;
  std::string title;
  std::string display;  // "Flat", "Stereoscopic 3D", "Immersive"
  s64 started = 0;
  std::string version;
  std::string release;
  std::string os;
  std::string gpu;
  std::string driver;
  std::vector<std::string> settings;
  std::vector<std::string> mods;
  std::string crash;  // "SIGSEGV (11), code 1, address 0x..., thread Video thread"
  std::string exception;
  std::vector<Frame> frames;
  std::vector<Module> modules;  // that the frames are in
  std::vector<LogFile> logs;
  std::string note;  // "What Happened?", or "What Were You Doing?"
};

// What a report has besides the run.
struct Context
{
  std::string log_dir;
  std::size_t log_lines = 2000;
  std::string os;
  std::vector<std::string> settings;  // for a run without a game
  std::vector<std::string> mods;
  Scrubber scrub;
};

// The launcher's and the emulator's logs of a run and of the one before it. `started`: a run's, 0 is
// the newest.
std::vector<std::string> FindLogs(const std::string& dir, s64 started);
// The last lines of a file (and no more than its last megabyte).
LogFile ReadLog(const std::string& path, std::size_t lines);

// Of this run, as it is now: no crash.
Report BuildManual(const LastRun& now, const Context& context);
Report BuildCrash(const LastRun& run, const Context& context);

// The player's words, scrubbed, too.
void SetNote(Report* report, std::string_view note, const Scrubber& scrub);

// report.txt, and that with the logs under it (See What's Sent).
std::string ReportText(const Report& report);
std::string FullText(const Report& report);
// Of a saved or sent report: name and contents.
std::vector<std::pair<std::string, std::string>> GetFiles(const Report& report);

// "baijivr-report-20261125-153012.zip" (local time).
std::string ZipName(s64 time);
// Into `dir`, which is made when missing. The zip's path, or empty and why not.
std::string SaveZip(const Report& report, const std::string& dir, s64 time, std::string* error);

// ---- This machine and this run. Host thread ----

std::string GetOSText();
// ~/Downloads.
std::string GetSaveDir();
// "0.9.0 (9bc8d65d4d, frame-menu)", "baijivr@0.9.0".
std::string GetVersionText();
std::string GetReleaseName();
// "Label: value" lines of the VR menu's settings and some of Dolphin's.
std::vector<std::string> GetSettings();
// A game's mods that are on, or here but off.
std::vector<std::string> GetMods(const std::string& game_id, u16 revision);
// Empty without a running game.
CrashRecord::Game DescribeGame();

Report MakeManual();
Report MakeCrash(const LastRun& run);

// At the start, once the user directory is known: looks for what the last run left, and starts this
// run's record. `can_show`: this start has a window to show it in.
void StartRun(bool can_show);
// A clean exit.
void EndRun();
// What StartRun found, once.
std::optional<LastRun> TakePending();

// The VR menu's row. Saves at once: what to say about it.
std::string SaveForVRMenu();
}  // namespace ProblemReport
