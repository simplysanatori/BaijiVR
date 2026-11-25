// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// SPDX-License-Identifier: GPL-2.0-or-later

#include "UICommon/ProblemReport.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <fmt/format.h>
#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#ifdef __linux__
#include <csignal>
#include <pwd.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif
#ifdef BAIJI_TEST_HOOKS
#include <thread>
#endif

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/ScopeGuard.h"
#include "Common/StringUtil.h"
#include "Common/TimeUtil.h"
#include "Common/Version.h"

#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/GameMods/GameMods.h"
#include "Core/System.h"

#include "VideoCommon/VideoConfig.h"
#ifdef ENABLE_VR
#include "VideoCommon/VR/FrameUI/GamesHost.h"
#include "VideoCommon/VR/FrameUI/MenuSettings.h"
#include "VideoCommon/VR/OpenXRManager.h"
#endif

namespace fs = std::filesystem;

namespace ProblemReport
{
namespace
{
std::string ReadFile(const std::string& path)
{
  std::ifstream f(path, std::ios::binary);
  std::ostringstream s;
  if (f)
    s << f.rdbuf();
  return s.str();
}

template <typename F>
void ForLines(std::string_view text, F&& f)
{
  while (!text.empty())
  {
    const std::size_t end = text.find('\n');
    f(text.substr(0, end));
    if (end == std::string_view::npos)
      break;
    text.remove_prefix(end + 1);
  }
}

template <typename T>
T Parse(std::string_view s, int base = 10)
{
  T value{};
  std::from_chars(s.data(), s.data() + s.size(), value, base);
  return value;
}

// "0x00007f..."
u64 ParseHex(std::string_view s)
{
  if (s.starts_with("0x"))
    s.remove_prefix(2);
  return Parse<u64>(s, 16);
}

std::string Hex(u64 v)
{
  return fmt::format("0x{:016x}", v);
}

std::string Basename(std::string_view path)
{
  const std::size_t slash = path.rfind('/');
  return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::string LocalTime(s64 time, const char* format)
{
  const std::optional<std::tm> tm = Common::LocalTime(static_cast<time_t>(time));
  char buffer[64];
  if (!tm || std::strftime(buffer, sizeof(buffer), format, &*tm) == 0)
    return {};
  return buffer;
}

std::pair<std::string_view, std::string_view> SplitValue(std::string_view line)
{
  const std::size_t at = line.find('=');
  if (at == std::string_view::npos)
    return {};
  return {line.substr(0, at), line.substr(at + 1)};
}

// "Draw+0x1c" out of the backtrace_symbols line "path(Draw+0x1c) [0x...]". Without a name: empty.
std::string SymbolOf(std::string_view line)
{
  const std::size_t open = line.find('(');
  const std::size_t close = open == std::string_view::npos ? open : line.find(')', open);
  if (close == std::string_view::npos || close <= open + 1 || line[open + 1] == '+')
    return {};
  return std::string(line.substr(open + 1, close - open - 1));
}
}  // namespace

// ---- What the last run left ---------------------------------------------------------------------

void ParseMarker(std::string_view text, LastRun* run)
{
  ForLines(text, [&](std::string_view line) {
    const auto [key, value] = SplitValue(line);
    if (key == "pid")
      run->pid = Parse<int>(value);
    else if (key == "started")
      run->started = Parse<s64>(value);
    else if (key == "version")
      run->program.version = value;
    else if (key == "release")
      run->program.release = value;
    else if (key == "exe")
      run->program.exe = value;
    else if (key == "exe_build_id")
      run->program.build_id = value;
    else if (key == "game_id")
      run->game.id = value;
    else if (key == "revision")
      run->game.revision = Parse<u16>(value);
    else if (key == "title")
      run->game.title = value;
    else if (key == "display")
      run->game.display = value;
    else if (key == "gpu")
      run->game.gpu = value;
    else if (key == "driver")
      run->game.driver = value;
    else if (key == "cause")
      run->cause = CrashRecord::CauseFromKey(value);
    else if (key == "detail")
      run->detail = value;
    else if (key == "setting")
      run->settings.emplace_back(value);
  });
}

void ParseCrash(std::string_view text, LastRun* run)
{
  run->crash = true;
  std::string_view part;
  ForLines(text, [&](std::string_view line) {
    if (line == "symbols-begin" || line == "maps-begin")
    {
      part = line;
    }
    else if (line == "symbols-end" || line == "maps-end")
    {
      part = {};
    }
    else if (part == "symbols-begin")
    {
      run->symbols.emplace_back(line);
    }
    else if (part == "maps-begin")
    {
      run->maps.append(line).push_back('\n');
    }
    else
    {
      const auto [key, value] = SplitValue(line);
      if (key == "signal")
        run->signal = Parse<int>(value);
      else if (key == "code")
        run->code = Parse<int>(value);
      else if (key == "addr")
        run->address = ParseHex(value);
      else if (key == "thread")
        run->thread = value;
      else if (key == "exception")
        run->exception = value;
      else if (key == "pc")
        run->pc = ParseHex(value);
      else if (key == "lr")
        run->lr = ParseHex(value);
      else if (key == "frame")
        run->frames.push_back(ParseHex(value));
      else if (key == "cause")  // the marker may not have it yet
        run->cause = CrashRecord::CauseFromKey(value);
      else if (key == "detail")
        run->detail = value;
    }
  });
}

std::optional<LastRun> TakeLastRun(const std::string& dir, const std::function<bool(int)>& alive,
                                   bool take)
{
  struct Files
  {
    std::vector<std::string> paths;
    LastRun run;
    bool real = false;
  };
  std::map<int, Files> runs;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
  {
    const fs::directory_entry& entry = *it;
    const std::string name = entry.path().filename().string();
    const bool is_marker = name.starts_with(CrashRecord::RUNNING_PREFIX);
    if (!is_marker && !name.starts_with(CrashRecord::CRASH_PREFIX))
      continue;
    const std::string_view number = std::string_view(name).substr(
        is_marker ? CrashRecord::RUNNING_PREFIX.size() : CrashRecord::CRASH_PREFIX.size());
    const int pid = Parse<int>(number);
    if (pid <= 0 || alive(pid))
      continue;
    Files& files = runs[pid];
    files.paths.push_back(entry.path().string());
    if (number.ends_with(".tmp"))
      continue;
    files.real = true;
    const std::string text = ReadFile(entry.path().string());
    if (is_marker)
      ParseMarker(text, &files.run);
    else
      ParseCrash(text, &files.run);
  }
  auto newest = runs.end();
  for (auto it = runs.begin(); it != runs.end(); ++it)
  {
    if (it->second.real &&
        (newest == runs.end() || it->second.run.started >= newest->second.run.started))
    {
      newest = it;
    }
  }
  std::optional<LastRun> last;
  for (auto it = runs.begin(); it != runs.end(); ++it)
  {
    if (it == newest && !take)
      continue;
    for (const std::string& path : it->second.paths)
      fs::remove(path, ec);
    if (it == newest)
      last = std::move(it->second.run);
  }
  return last;
}

bool IsOtherInstance([[maybe_unused]] int pid)
{
#ifdef __linux__
  if (pid <= 0 || pid == static_cast<int>(getpid()) || (kill(pid, 0) != 0 && errno != EPERM))
    return false;
  std::error_code ec;
  const std::string exe = fs::read_symlink(fmt::format("/proc/{}/exe", pid), ec).filename().string();
  return exe.starts_with("baiji");
#else
  return false;
#endif
}

Kind Classify(const LastRun& run)
{
  if (run.cause == CrashRecord::Cause::DeviceLost)
    return Kind::DeviceLost;
  if (!run.crash || run.signal == 0)
    return Kind::Unexpected;
  return run.exception.starts_with("std::bad_alloc") ? Kind::OutOfMemory : Kind::Crashed;
}

std::string ReasonText(Kind kind)
{
  switch (kind)
  {
  case Kind::OutOfMemory:
    return "Ran Out of Memory";
  case Kind::DeviceLost:
    return "Graphics Driver Stopped Responding";
  case Kind::Crashed:
    return "Baiji VR Crashed";
  case Kind::Unexpected:
    return "Closed Unexpectedly";
  case Kind::Manual:
    break;
  }
  return {};
}

std::string_view KindKey(Kind kind)
{
  switch (kind)
  {
  case Kind::OutOfMemory:
    return "out-of-memory";
  case Kind::DeviceLost:
    return "device-lost";
  case Kind::Crashed:
    return "crashed";
  case Kind::Unexpected:
    return "unexpected";
  case Kind::Manual:
    break;
  }
  return "manual";
}

std::string SignalName(int signal)
{
  static constexpr std::array<std::pair<int, const char*>, 6> NAMES{{{4, "SIGILL"},
                                                                     {5, "SIGTRAP"},
                                                                     {6, "SIGABRT"},
                                                                     {7, "SIGBUS"},
                                                                     {8, "SIGFPE"},
                                                                     {11, "SIGSEGV"}}};
  for (const auto& [number, name] : NAMES)
  {
    if (number == signal)
      return name;
  }
  return fmt::format("signal {}", signal);
}

// ---- Scrubbing ----------------------------------------------------------------------------------

namespace
{
bool IsHexDigit(char c)
{
  return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

bool IsWordChar(char c)
{
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool IsIPv4(std::string_view s)
{
  int parts = 0;
  for (;;)
  {
    const std::size_t dot = s.find('.');
    const std::string_view part = s.substr(0, dot);
    if (part.empty() || part.size() > 3 ||
        !std::ranges::all_of(part, [](char c) { return c >= '0' && c <= '9'; }) ||
        Parse<int>(part) > 255)
    {
      return false;
    }
    ++parts;
    if (dot == std::string_view::npos)
      return parts == 4;
    s.remove_prefix(dot + 1);
  }
}

bool IsIPv6(std::string_view s)
{
  // The last two groups may be an IPv4 address.
  std::string mapped;
  if (const std::size_t colon = s.rfind(':');
      colon != std::string_view::npos && IsIPv4(s.substr(colon + 1)))
  {
    mapped = std::string(s.substr(0, colon + 1)) + "0:0";
    s = mapped;
  }
  const auto colons = std::ranges::count(s, ':');
  const std::size_t gap = s.find("::");
  if (colons < 2 || colons > 7 || (colons != 7 && gap == std::string_view::npos) ||
      (gap != std::string_view::npos && s.find("::", gap + 1) != std::string_view::npos) ||
      !std::ranges::any_of(s, IsHexDigit))
  {
    return false;
  }
  std::size_t group = 0;
  for (const char c : s)
  {
    if (c == ':')
      group = 0;
    else if (!IsHexDigit(c) || ++group > 4)
      return false;
  }
  return true;
}

// At the start of `s`.
bool HasMac(std::string_view s)
{
  if (s.size() < 17 || (s[2] != ':' && s[2] != '-'))
    return false;
  for (std::size_t i = 0; i < 17; ++i)
  {
    if (i % 3 == 2 ? s[i] != s[2] : !IsHexDigit(s[i]))
      return false;
  }
  return s.size() == 17 || (!IsWordChar(s[17]) && (s[17] != s[2] || s.size() == 18 ||
                                                   !IsHexDigit(s[18])));
}

// Whole words (with `paths`: or a path that is or starts with it).
std::string ReplaceWord(std::string_view text, std::string_view from, std::string_view to,
                        bool path = false)
{
  std::string out;
  out.reserve(text.size());
  std::size_t i = 0;
  for (;;)
  {
    const std::size_t at = text.find(from, i);
    if (at == std::string_view::npos)
      return out.append(text.substr(i));
    const std::size_t after = at + from.size();
    const bool left = path || at == 0 || !IsWordChar(text[at - 1]);
    const bool right = after >= text.size() || !IsWordChar(text[after]);
    out.append(text.substr(i, at - i)).append(left && right ? to : from);
    i = after;
  }
}
}  // namespace

std::string ScrubNetwork(std::string_view text)
{
  std::string out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size())
  {
    const auto in_run = [](char c) { return IsHexDigit(c) || c == '.' || c == ':'; };
    if (!in_run(text[i]) || (i > 0 && (IsWordChar(text[i - 1]) || text[i - 1] == '.')))
    {
      out.push_back(text[i++]);
      continue;
    }
    if (HasMac(text.substr(i)))
    {
      out += "<mac>";
      i += 17;
      continue;
    }
    std::size_t end = i;
    while (end < text.size() && in_run(text[end]))
      ++end;
    // What ends a sentence is not of the address ("::" is).
    while (end > i && (text[end - 1] == '.' ||
                       (text[end - 1] == ':' && !(end - i >= 2 && text[end - 2] == ':'))))
    {
      --end;
    }
    if (end == i)
    {
      out.push_back(text[i++]);
      continue;
    }
    const std::string_view run = text.substr(i, end - i);
    i = end;
    const std::size_t colon = run.rfind(':');
    if (end < text.size() && IsWordChar(text[end]))
      out.append(run);
    else if (IsIPv4(run) || IsIPv6(run))
      out += "<ip>";
    else if (colon != std::string_view::npos && IsIPv4(run.substr(0, colon)))
      out.append("<ip>").append(run.substr(colon));  // and its port
    else
      out.append(run);
  }
  return out;
}

std::string Scrubber::Apply(std::string_view text) const
{
  std::string out(text);
  std::string folder = home;
  while (folder.size() > 1 && folder.back() == '/')
    folder.pop_back();
  // First, so that "/home/player/x" is "~/x" and not "/home/<user>/x".
  if (folder.size() > 1)
    out = ReplaceWord(out, folder, "~", true);
  // Before the user's, which is often in it ("player-frame").
  if (host.size() >= 2 && host != "steamos" && host != "localhost")
    out = ReplaceWord(out, host, "<host>");
  if (user.size() >= 2 && user != "steamos" && user != "deck" && user != "root")
    out = ReplaceWord(out, user, "<user>");
  return ScrubNetwork(out);
}

Scrubber Scrubber::ForThisMachine()
{
  Scrubber scrub;
  if (const char* const home_dir = std::getenv("HOME"))
    scrub.home = home_dir;
#ifdef __linux__
  if (const passwd* const pw = getpwuid(getuid()))
  {
    scrub.user = pw->pw_name ? pw->pw_name : "";
    if (scrub.home.empty() && pw->pw_dir)
      scrub.home = pw->pw_dir;
  }
  char host_name[256] = {};
  if (gethostname(host_name, sizeof(host_name) - 1) == 0)
    scrub.host = host_name;
#endif
  return scrub;
}

// ---- Modules ------------------------------------------------------------------------------------

std::vector<Module> ParseMaps(std::string_view maps, std::vector<MapRange>* ranges)
{
  std::vector<Module> modules;
  ForLines(maps, [&](std::string_view line) {
    // start-end perms offset dev inode path
    std::istringstream s{std::string(line)};
    std::string range, perms, offset, dev, inode, path;
    if (!(s >> range >> perms >> offset >> dev >> inode))
      return;
    std::getline(s, path);
    const std::size_t first = path.find_first_not_of(' ');
    if (first == std::string::npos)
      return;
    path = path.substr(first);
    // Replaced while it ran: its name still is the module's.
    if (constexpr std::string_view DELETED = " (deleted)"; path.ends_with(DELETED))
      path.resize(path.size() - DELETED.size());
    // Not [stack], a GPU buffer (reading /dev/dri blocks) or shared memory.
    if (path[0] != '/' || path.starts_with("/dev/") || path.starts_with("/memfd:") || inode == "0")
      return;
    const std::size_t dash = range.find('-');
    if (dash == std::string::npos)
      return;
    const u64 start = Parse<u64>(std::string_view(range).substr(0, dash), 16);
    const u64 end = Parse<u64>(std::string_view(range).substr(dash + 1), 16);
    auto it = std::ranges::find(modules, path, &Module::path);
    if (it == modules.end())
    {
      // Its first mapping is its ELF header.
      if (Parse<u64>(offset, 16) != 0)
        return;
      modules.push_back({path, start, end, {}});
      it = modules.end() - 1;
    }
    it->end = std::max(it->end, end);
    if (ranges)
      ranges->push_back({start, end, static_cast<std::size_t>(it - modules.begin())});
  });
  return modules;
}

std::string ReadBuildId(const std::string& path)
{
  std::error_code ec;
  if (!fs::is_regular_file(path, ec))
    return {};
  std::ifstream f(path, std::ios::binary);
  std::array<u8, 64> header{};
  const auto read = [&f](auto& into) {
    f.read(reinterpret_cast<char*>(into.data()), static_cast<std::streamsize>(into.size()));
    return static_cast<std::size_t>(f.gcount()) == into.size();
  };
  const auto get = [](const auto& bytes, std::size_t at, auto value) {
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return value;
  };
  // 64 bits, little-endian.
  if (!read(header) || std::memcmp(header.data(), "\x7f"
                                                  "ELF",
                                   4) != 0 ||
      header[4] != 2 || header[5] != 1)
  {
    return {};
  }
  const u64 table = get(header, 32, u64{});
  const u16 entry_size = get(header, 54, u16{});
  const u16 count = std::min<u16>(get(header, 56, u16{}), 128);
  for (u16 i = 0; i < count; ++i)
  {
    std::array<u8, 56> entry{};
    f.seekg(static_cast<std::streamoff>(table + u64{i} * entry_size));
    if (!read(entry))
      return {};
    constexpr u32 PT_NOTE_TYPE = 4;
    const u64 size = get(entry, 32, u64{});
    if (get(entry, 0, u32{}) != PT_NOTE_TYPE || size > 0x10000)
      continue;
    std::vector<u8> notes(size);
    f.seekg(static_cast<std::streamoff>(get(entry, 8, u64{})));
    if (!read(notes))
      return {};
    std::size_t at = 0;
    while (at + 12 <= notes.size())
    {
      const u64 name_size = get(notes, at, u32{});
      const u64 desc_size = get(notes, at + 4, u32{});
      const u32 type = get(notes, at + 8, u32{});
      const std::size_t desc = at + 12 + ((name_size + 3) & ~u64{3});
      if (desc + desc_size > notes.size())
        break;
      if (type == 3 && name_size == 4 && std::memcmp(&notes[at + 12], "GNU", 4) == 0)
      {
        std::string hex;
        for (u64 b = 0; b < desc_size; ++b)
          hex += fmt::format("{:02x}", notes[desc + b]);
        return hex;
      }
      at = desc + ((desc_size + 3) & ~u64{3});
    }
  }
  return {};
}

std::string DebugId(std::string_view build_id)
{
  // Its first 16 bytes as a GUID, whose first three fields are little-endian.
  std::array<u8, 16> bytes{};
  for (std::size_t i = 0; i < bytes.size() && i * 2 + 1 < build_id.size(); ++i)
    bytes[i] = Parse<u8>(build_id.substr(i * 2, 2), 16);
  std::reverse(bytes.begin(), bytes.begin() + 4);
  std::swap(bytes[4], bytes[5]);
  std::swap(bytes[6], bytes[7]);
  std::string id;
  for (std::size_t i = 0; i < bytes.size(); ++i)
  {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      id += '-';
    id += fmt::format("{:02x}", bytes[i]);
  }
  return id;
}

// ---- Logs ---------------------------------------------------------------------------------------

std::vector<std::string> FindLogs(const std::string& dir, s64 started)
{
  std::vector<std::string> found;
  for (const char* const name : {"baiji-launcher.log", MAIN_LOG})
  {
    // Newest first, as scripts/baiji-vr turns them over.
    std::vector<std::string> files;
    std::error_code ec;
    for (int i = 0; i <= 8; ++i)
    {
      std::string path = dir + name;
      if (i > 0)
        path += i == 1 ? std::string(".old") : fmt::format(".old{}", i);
      if (fs::file_size(path, ec) > 0 && !ec)
        files.push_back(std::move(path));
    }
    // The run's own is the oldest that was written to after it started.
    std::size_t first = 0;
    for (std::size_t i = 0; started > 0 && i < files.size(); ++i)
    {
      const auto written = fs::last_write_time(files[i], ec);
      if (!ec && std::chrono::duration_cast<std::chrono::seconds>(
                     fs::file_time_type::clock::to_sys(written).time_since_epoch())
                         .count() >= started)
      {
        first = i;
      }
    }
    for (std::size_t i = first; i < files.size() && i < first + 2; ++i)
      found.push_back(files[i]);
  }
  return found;
}

LogFile ReadLog(const std::string& path, std::size_t lines)
{
  LogFile log;
  log.name = Basename(path);
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    return log;
  constexpr std::streamoff MOST = 1024 * 1024;
  const std::streamoff size = f.tellg();
  f.seekg(std::max<std::streamoff>(size - MOST, 0));
  std::string line;
  // Not the rest of a line.
  if (size > MOST)
    std::getline(f, line);
  std::deque<std::string> tail;
  while (std::getline(f, line))
  {
    tail.push_back(std::move(line));
    if (tail.size() > lines)
      tail.pop_front();
  }
  log.lines = tail.size();
  for (const std::string& l : tail)
    log.text.append(l).push_back('\n');
  return log;
}

// ---- The report ---------------------------------------------------------------------------------

namespace
{
Report Build(const LastRun& run, Kind kind, const Context& context)
{
  const Scrubber& scrub = context.scrub;
  const auto apply = [&scrub](std::vector<std::string> lines) {
    for (std::string& line : lines)
      line = scrub.Apply(line);
    return lines;
  };
  Report report;
  report.kind = kind;
  report.game_id = run.game.id;
  report.revision = run.game.revision;
  report.title = scrub.Apply(run.game.title);
  report.display = run.game.display;
  report.version = run.program.version;
  report.release = run.program.release;
  report.os = scrub.Apply(context.os);
  report.gpu = run.game.gpu;
  report.driver = run.game.driver;
  report.settings = apply(run.settings.empty() ? context.settings : run.settings);
  report.mods = apply(context.mods);
  for (const std::string& path : FindLogs(context.log_dir, run.started))
  {
    LogFile log = ReadLog(path, context.log_lines);
    log.text = scrub.Apply(log.text);
    report.logs.push_back(std::move(log));
  }
  return report;
}

// The stack, newest first, without the handler's own frames: from where the signal came.
std::vector<std::pair<u64, std::string>> GetStack(const LastRun& run)
{
  std::vector<std::pair<u64, std::string>> stack;
  for (std::size_t i = 0; i < run.frames.size(); ++i)
    stack.emplace_back(run.frames[i], i < run.symbols.size() ? SymbolOf(run.symbols[i]) : "");
  if (stack.empty())
  {
    // The unwinder did not get to it.
    for (const u64 address : {run.pc, run.lr})
    {
      if (address != 0)
        stack.emplace_back(address, "");
    }
    return stack;
  }
  if (const auto at = std::ranges::find(stack, run.pc, &std::pair<u64, std::string>::first);
      run.pc != 0 && at != stack.end())
  {
    stack.erase(stack.begin(), at);
    return stack;
  }
  // Else it is after the signal's trampoline.
  for (std::size_t i = 0; i < run.symbols.size() && i < stack.size(); ++i)
  {
    if (run.symbols[i].find("__kernel_rt_sigreturn") == std::string::npos)
      continue;
    stack.erase(stack.begin(), stack.begin() + static_cast<std::ptrdiff_t>(i) + 1);
    if (run.pc != 0)
      stack.insert(stack.begin(), {run.pc, std::string()});
    break;
  }
  return stack;
}
}  // namespace

Report BuildManual(const LastRun& now, const Context& context)
{
  Report report = Build(now, Kind::Manual, context);
  report.headline = "Report a Problem";
  return report;
}

Report BuildCrash(const LastRun& run, const Context& context)
{
  const Scrubber& scrub = context.scrub;
  Report report = Build(run, Classify(run), context);
  report.headline = "Baiji VR Closed Unexpectedly";
  report.reason = ReasonText(report.kind);
  report.detail = scrub.Apply(run.detail);
  report.started = run.started;
  if (!run.crash || run.signal == 0)
    return report;

  report.crash = fmt::format("{} ({}), code {}, address {}", SignalName(run.signal), run.signal,
                             run.code, Hex(run.address));
  if (!run.thread.empty())
    report.crash += ", thread " + scrub.Apply(run.thread);
  report.exception = scrub.Apply(run.exception);
  std::vector<MapRange> ranges;
  const std::vector<Module> all = ParseMaps(run.maps, &ranges);
  for (const auto& [address, symbol] : GetStack(run))
  {
    Frame frame;
    frame.address = address;
    frame.symbol = scrub.Apply(symbol);
    const auto range = std::ranges::find_if(
        ranges, [a = address](const MapRange& r) { return a >= r.start && a < r.end; });
    if (range != ranges.end())
    {
      const Module& module = all[range->module];
      auto it = std::ranges::find(report.modules, module.path, &Module::path);
      if (it == report.modules.end())
      {
        report.modules.push_back(module);
        it = report.modules.end() - 1;
        // The program's from when it ran: an update may have replaced its file.
        it->build_id = module.path == run.program.exe && !run.program.build_id.empty() ?
                           run.program.build_id :
                           ReadBuildId(module.path);
      }
      frame.module = static_cast<int>(it - report.modules.begin());
      frame.offset = address - it->base;
    }
    report.frames.push_back(std::move(frame));
  }
  for (Module& module : report.modules)
    module.path = scrub.Apply(module.path);
  return report;
}

void SetNote(Report* report, std::string_view note, const Scrubber& scrub)
{
  report->note = scrub.Apply(StripWhitespace(note));
}

std::string ReportText(const Report& report)
{
  const bool manual = report.kind == Kind::Manual;
  std::string s = manual ? "Baiji VR Problem Report\n\n" : "Baiji VR Crash Report\n\n";
  const auto row = [&s](std::string_view label, std::string_view value) {
    if (!value.empty())
      s += fmt::format("{}: {}\n", label, value);
  };
  const auto known = [](const std::string& value) { return value.empty() ? "Unknown" : value; };
  const auto list = [&s](std::string_view title, const std::vector<std::string>& lines) {
    if (lines.empty())
      return;
    s += fmt::format("\n{}\n", title);
    for (const std::string& line : lines)
      s += fmt::format("  {}\n", line);
  };
  row("Reason", report.reason);
  row("Detail", report.detail);
  row("Game", report.game_id.empty() ? "None" : known(report.title));
  if (!report.game_id.empty())
  {
    row("Game ID", report.game_id);
    row("Revision", fmt::format("{}", report.revision));
  }
  row("Display", report.display);
  if (report.started > 0)
    row("Started", LocalTime(report.started, "%Y-%m-%d %H:%M"));
  row("Version", known(report.version));
  row("System", report.os);
  row("GPU", known(report.gpu));
  row("Driver", known(report.driver));
  if (!report.note.empty())
  {
    s += manual ? "\nWhat Happened?\n" : "\nWhat Were You Doing?\n";
    ForLines(report.note, [&s](std::string_view line) { s += fmt::format("  {}\n", line); });
  }
  list("Mods", report.mods);
  list("Settings", report.settings);
  if (!report.crash.empty())
  {
    s += fmt::format("\nCrash\n  {}\n", report.crash);
    if (!report.exception.empty())
      s += fmt::format("  Exception: {}\n", report.exception);
    for (std::size_t i = 0; i < report.frames.size(); ++i)
    {
      const Frame& frame = report.frames[i];
      s += fmt::format("  #{:<2} {}", i, Hex(frame.address));
      if (frame.module >= 0)
      {
        s += fmt::format(" {} + 0x{:x}",
                         Basename(report.modules[static_cast<std::size_t>(frame.module)].path),
                         frame.offset);
      }
      s += frame.symbol.empty() ? "\n" : fmt::format(" ({})\n", frame.symbol);
    }
    std::vector<std::string> modules;
    for (const Module& module : report.modules)
    {
      modules.push_back(fmt::format("{} {}-{} build id {}", module.path, Hex(module.base),
                                    Hex(module.end),
                                    module.build_id.empty() ? "none" : module.build_id));
    }
    list("Modules", modules);
  }
  return s;
}

std::string FullText(const Report& report)
{
  std::string s = ReportText(report);
  for (const LogFile& log : report.logs)
    s += fmt::format("\n{} (Last {} Lines)\n", log.name, log.lines) + log.text;
  return s;
}

std::vector<std::pair<std::string, std::string>> GetFiles(const Report& report)
{
  std::vector<std::pair<std::string, std::string>> files{{"report.txt", ReportText(report)}};
  for (const LogFile& log : report.logs)
    files.emplace_back(log.name, log.text);
  return files;
}

// ---- The zip ------------------------------------------------------------------------------------

std::string ZipName(s64 time)
{
  return "baijivr-report-" + LocalTime(time, "%Y%m%d-%H%M%S") + ".zip";
}

static bool WriteZip(const std::vector<std::pair<std::string, std::string>>& files,
                     const std::string& path, s64 time)
{
  void* writer = mz_zip_writer_create();
  if (!writer)
    return false;
  Common::ScopeGuard guard([&writer] { mz_zip_writer_delete(&writer); });
  mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
  if (mz_zip_writer_open_file(writer, path.c_str(), 0, 0) != MZ_OK)
    return false;
  bool ok = true;
  for (const auto& [name, data] : files)
  {
    mz_zip_file info{};
    info.filename = name.c_str();
    info.modified_date = static_cast<time_t>(time);
    info.version_madeby = (MZ_HOST_SYSTEM_UNIX << 8) | 20;
    info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
    info.flag = MZ_ZIP_FLAG_UTF8;
    info.external_fa = 0100644u << 16;
    ok = ok && mz_zip_writer_add_buffer(writer, const_cast<char*>(data.data()),
                                        static_cast<s32>(data.size()), &info) == MZ_OK;
  }
  return mz_zip_writer_close(writer) == MZ_OK && ok;
}

std::string SaveZip(const Report& report, const std::string& dir, s64 time, std::string* error)
{
  std::error_code ec;
  fs::create_directories(dir, ec);
  // Another in the same second is another file.
  fs::path path;
  do
  {
    path = fs::path(dir) / ZipName(time++);
  } while (fs::exists(path, ec));
  if (WriteZip(GetFiles(report), path.string(), time - 1))
    return path.string();
  fs::remove(path, ec);
  if (error)
    *error = fs::is_directory(dir, ec) ? "The file cannot be written" : "The folder cannot be made";
  return {};
}

// ---- This machine and this run -------------------------------------------------------------------

std::string GetOSText()
{
  std::map<std::string, std::string, std::less<>> release;
  ForLines(ReadFile("/etc/os-release"), [&release](std::string_view line) {
    const auto [key, value] = SplitValue(line);
    std::string text(value);
    std::erase(text, '"');
    release[std::string(key)] = std::move(text);
  });
  std::string s = release["NAME"].empty() ? "Linux" : release["NAME"];
  if (!release["VERSION_ID"].empty())
    s += " " + release["VERSION_ID"];
  if (!release["BUILD_ID"].empty())
    s += " build " + release["BUILD_ID"];
#ifdef __linux__
  utsname u{};
  if (uname(&u) == 0)
    s += fmt::format(" (Linux {}, {})", u.release, u.machine);
#endif
  return s;
}

std::string GetSaveDir()
{
  const char* const home = std::getenv("HOME");
  return home && *home ? std::string(home) + "/Downloads" : File::GetUserPath(D_USER_IDX);
}

std::string GetVersionText()
{
  return fmt::format("{} ({}, {})", BAIJI_VERSION, Common::GetScmDescStr(),
                     Common::GetScmBranchStr());
}

std::string GetReleaseName()
{
  return "baijivr@" BAIJI_VERSION;
}

std::vector<std::string> GetSettings()
{
  const auto on = [](bool value) { return value ? "On" : "Off"; };
  std::vector<std::string> lines{
      fmt::format("Backend: {}", Config::Get(Config::MAIN_GFX_BACKEND)),
      fmt::format("Dual Core: {}", on(Config::Get(Config::MAIN_CPU_THREAD))),
      fmt::format("Fastmem: {}", on(Config::Get(Config::MAIN_FASTMEM)))};
#ifdef ENABLE_VR
  const VR::UI::SettingValues values = VR::UI::ReadSettings();
  // Some have the same words under another heading.
  std::string heading;
  VR::UI::MenuSection section = VR::UI::MenuSection::Screen;
  for (const VR::UI::SettingRow& row : VR::UI::GetSettingRows())
  {
    if (std::exchange(section, row.section) != row.section)
      heading.clear();
    if (row.kind == VR::UI::RowKind::Header)
      heading = row.label + " > ";
    // (Immersive Mode is Display again.)
    if (row.id == VR::UI::SettingId::None || row.id == VR::UI::SettingId::ImmersiveMode)
      continue;
    const auto i = static_cast<std::size_t>(row.id);
    const std::string value = row.kind == VR::UI::RowKind::Switch ?
                                  on(values.game[i] != 0.0f) :
                                  VR::UI::FormatValue(row, values.game[i]);
    lines.push_back(fmt::format("{}{}: {}{}", heading, row.label, value,
                                values.own_value[i] ? " (Per-Game)" : ""));
  }
#endif
  return lines;
}

std::vector<std::string> GetMods(const std::string& game_id, u16 revision)
{
  std::vector<std::string> lines;
  for (const GameMods::List list : {GameMods::List::Mods, GameMods::List::Textures})
  {
    for (const GameMods::Item& item : GameMods::GetItems(list, game_id, revision))
    {
      const bool on = item.state == GameMods::State::On;
      // (Cheats that are off are only listed.)
      if (on || (item.state == GameMods::State::Off && item.downloads))
      {
        lines.push_back(fmt::format("{}{}: {}", item.name,
                                    item.version.empty() ? "" : " " + item.version,
                                    on ? "On" : "Off"));
      }
    }
  }
  return lines;
}

static std::string GetDisplayText(bool game_runs)
{
#ifdef ENABLE_VR
  // Also where VR was asked for and did not start.
  if (game_runs && !(VR::g_openxr && VR::g_openxr->GetSwapchain()))
    return "Flat";
#endif
  switch (Config::Get(Config::GFX_VR_PRESENTATION_MODE))
  {
  case OpenXRPresentationMode::Vulkan:
    return "Flat";
  case OpenXRPresentationMode::Immersive:
    return "Immersive";
  case OpenXRPresentationMode::StereoScreen:
    return "Stereoscopic 3D";
  case OpenXRPresentationMode::Legacy:
    break;
  }
  return Config::Get(Config::GFX_VR_ENABLE_OPENXR) ? "VR" : "Flat";
}

CrashRecord::Game DescribeGame()
{
  CrashRecord::Game game;
  const Core::State state = Core::GetState(Core::System::GetInstance());
  if (state == Core::State::Uninitialized)
    return game;
  const SConfig& config = SConfig::GetInstance();
  game.id = config.GetGameID();
  game.revision = config.GetRevision();
  game.title = config.GetTitleName();
  // While it starts or stops the graphics backend is the video thread's.
  const bool runs = state == Core::State::Running || state == Core::State::Paused;
  game.display = GetDisplayText(runs);
  if (!runs)
    return game;
  const auto adapter = static_cast<std::size_t>(std::max(g_Config.iAdapter, 0));
  if (adapter < g_backend_info.Adapters.size())
    game.gpu = g_backend_info.Adapters[adapter];
  game.driver = g_backend_info.DriverName;
  for (const std::string& line : GetSettings())
    game.settings += line + '\n';
  return game;
}

static CrashRecord::Program DescribeProgram()
{
  CrashRecord::Program program;
  program.version = GetVersionText();
  program.release = GetReleaseName();
#ifdef __linux__
  std::error_code ec;
  program.exe = fs::read_symlink("/proc/self/exe", ec).string();
  program.build_id = ReadBuildId(program.exe);
#endif
  return program;
}

static Context MakeContext(const CrashRecord::Game& game)
{
  Context context;
  context.log_dir = File::GetUserPath(D_LOGS_IDX);
  context.os = GetOSText();
  context.settings = GetSettings();
  if (!game.id.empty())
    context.mods = GetMods(game.id, game.revision);
  context.scrub = Scrubber::ForThisMachine();
  return context;
}

Report MakeManual()
{
  LastRun now;
  now.program.version = GetVersionText();
  now.program.release = GetReleaseName();
  now.game = DescribeGame();
  if (now.game.id.empty())
    now.game.display = GetDisplayText(false);
  return BuildManual(now, MakeContext(now.game));
}

Report MakeCrash(const LastRun& run)
{
  return BuildCrash(run, MakeContext(run.game));
}

static std::optional<LastRun> s_pending;
static Common::EventHook s_state_hook;

#ifdef BAIJI_TEST_HOOKS
// BAIJI_TEST_CRASH=segv|abort|trap|throw, after BAIJI_TEST_CRASH_SECONDS (5). Not in a build without
// the unit tests.
static void StartTestCrash()
{
  const char* const kind = std::getenv("BAIJI_TEST_CRASH");
  if (!kind)
    return;
  const char* const seconds = std::getenv("BAIJI_TEST_CRASH_SECONDS");
  std::thread([kind = std::string(kind), delay = seconds ? std::atoi(seconds) : 5]() mutable {
    std::this_thread::sleep_for(std::chrono::seconds(delay));
    if (kind == "abort")
      std::abort();
    if (kind == "trap")
      __builtin_trap();
    if (kind == "throw")
      kind[0] = std::string().at(1);  // std::out_of_range
    volatile int* volatile nowhere = nullptr;
    *nowhere = 1;
  }).detach();
}
#endif

void StartRun(bool can_show)
{
  const std::string dir = File::GetUserPath(D_USER_IDX) + "CrashReports" DIR_SEP;
  s_pending = TakeLastRun(dir, &IsOtherInstance, can_show);
  CrashRecord::Start(dir, DescribeProgram());
#ifdef ENABLE_VR
  VR::UI::SetReportSaver(&SaveForVRMenu);
#endif
  s_state_hook = Core::AddOnStateChangedCallback([](Core::State state) {
    if (state == Core::State::Paused || state == Core::State::Stopping)
      return;
    // (The same again, after a pause, changes nothing.)
    Core::QueueHostJob([](Core::System&) { CrashRecord::NoteGame(DescribeGame()); }, true);
  });
#ifdef BAIJI_TEST_HOOKS
  StartTestCrash();
#endif
}

void EndRun()
{
  s_state_hook.reset();
  CrashRecord::CleanExit();
}

std::optional<LastRun> TakePending()
{
  return std::exchange(s_pending, std::nullopt);
}

std::string SaveForVRMenu()
{
  std::string error;
  const std::string path = SaveZip(MakeManual(), GetSaveDir(), std::time(nullptr), &error);
  if (path.empty())
  {
    ERROR_LOG_FMT(COMMON, "The problem report was not saved: {}", error);
    return "The Report Cannot Be Saved";
  }
  NOTICE_LOG_FMT(COMMON, "Problem report saved as {}", Basename(path));
  return "Report Saved in Downloads";
}
}  // namespace ProblemReport
