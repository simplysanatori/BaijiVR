// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Common/OpenPage.h"

#include <algorithm>
#include <cstdlib>

#if defined(__linux__) && !defined(ANDROID)
#include <cerrno>
#include <thread>

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Common/Logging/Log.h"
#include "Common/ScopeGuard.h"

extern char** environ;
#endif

namespace Common::OpenPage
{
bool IsPage(std::string_view url)
{
  constexpr std::string_view SCHEME = "https://";
  return url.starts_with(SCHEME) && url.size() > SCHEME.size() && url.size() <= 400 &&
         std::ranges::none_of(url, [](char c) {
           const auto u = static_cast<unsigned char>(c);
           return u <= ' ' || u >= 0x7F || c == '"' || c == '\'' || c == '\\' || c == '`';
         });
}

std::vector<std::string> MakeArguments(Opener opener, std::string_view url)
{
  if (!IsPage(url))
    return {};
  switch (opener)
  {
  case Opener::Steam:
    return {"steam", "steam://openurl/" + std::string(url)};
  case Opener::Xdg:
    return {"xdg-open", std::string(url)};
  case Opener::Nothing:
    break;
  }
  return {};
}

std::vector<std::string> CleanEnvironment(const char* const* environment)
{
  std::vector<std::string> clean;
  for (; environment && *environment; ++environment)
  {
    const std::string_view entry = *environment;
    if (!entry.starts_with("LD_LIBRARY_PATH=") && !entry.starts_with("LD_PRELOAD=") &&
        !entry.starts_with("QT_") && !entry.starts_with("XR_RUNTIME_JSON="))
    {
      clean.emplace_back(entry);
    }
  }
  return clean;
}

bool IsGameMode()
{
  const char* const desktop = std::getenv("XDG_CURRENT_DESKTOP");
  return desktop && std::string_view(desktop) == "gamescope";
}

#if defined(__linux__) && !defined(ANDROID)
static bool IsOnPath(std::string_view path, std::string_view name)
{
  while (!path.empty())
  {
    const std::string_view dir = path.substr(0, path.find(':'));
    path.remove_prefix(std::min(dir.size() + 1, path.size()));
    // (An empty one is the current directory to a shell.)
    if (dir.empty())
      continue;
    const std::string file = std::string(dir) + '/' + std::string(name);
    struct stat info;
    if (stat(file.c_str(), &info) == 0 && S_ISREG(info.st_mode) && access(file.c_str(), X_OK) == 0)
      return true;
  }
  return false;
}

Opener ChooseOpener(std::string_view path, bool game_mode)
{
  if (game_mode && IsOnPath(path, "steam"))
    return Opener::Steam;
  return IsOnPath(path, "xdg-open") ? Opener::Xdg : Opener::Nothing;
}

Opener Open(const std::string& url)
{
  const char* const path = std::getenv("PATH");
  const Opener opener = ChooseOpener(path ? path : "", IsGameMode());
  std::vector<std::string> arguments = MakeArguments(opener, url);
  if (arguments.empty())
    return Opener::Nothing;
  std::vector<std::string> environment = CleanEnvironment(environ);
  const auto to_pointers = [](std::vector<std::string>& strings) {
    std::vector<char*> pointers;
    for (std::string& s : strings)
      pointers.push_back(s.data());
    pointers.push_back(nullptr);
    return pointers;
  };
  const std::vector<char*> argv = to_pointers(arguments);
  const std::vector<char*> envp = to_pointers(environment);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  Common::ScopeGuard actions_guard([&] { posix_spawn_file_actions_destroy(&actions); });
  posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
#ifdef __GLIBC_PREREQ
#if __GLIBC_PREREQ(2, 34)
  posix_spawn_file_actions_addclosefrom_np(&actions, STDERR_FILENO + 1);
#endif
#endif
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  Common::ScopeGuard attributes_guard([&] { posix_spawnattr_destroy(&attributes); });
  sigset_t none;
  sigemptyset(&none);
  posix_spawnattr_setsigmask(&attributes, &none);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK);

  pid_t pid = 0;
  const int error = posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), envp.data());
  if (error != 0)
  {
    ERROR_LOG_FMT(COMMON, "{} could not be started ({})", arguments[0], error);
    return Opener::Nothing;
  }
  NOTICE_LOG_FMT(COMMON, "Opened {} with {}", url, arguments[0]);
  std::thread([pid] {
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {
    }
  }).detach();
  return opener;
}
#else
Opener ChooseOpener(std::string_view, bool)
{
  return Opener::Nothing;
}

Opener Open(const std::string&)
{
  return Opener::Nothing;
}
#endif
}  // namespace Common::OpenPage
