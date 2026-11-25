// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "UICommon/GameCovers.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include <fmt/format.h>

#include "Common/Buffer.h"
#include "Common/FileUtil.h"
#include "Common/HttpRequest.h"
#include "Common/Image.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Common/Swap.h"

namespace UICommon
{
namespace
{
using Clock = std::chrono::steady_clock;

constexpr auto CONNECT_TIMEOUT = std::chrono::seconds{4};
constexpr auto TRANSFER_TIMEOUT = std::chrono::seconds{20};
// A site that does not answer is left alone for this long, so that it never holds up the list.
constexpr auto DOWN_TIME = std::chrono::minutes{15};
// A game without a cover anywhere is asked for again after this long.
constexpr auto MISSING_TIME = std::chrono::hours{24 * 7};
constexpr s64 MAX_COVER_BYTES = 8 << 20;
constexpr u32 MAX_COVER_SIDE = 4096;

std::string HostOf(const std::string& url)
{
  const size_t start = url.find("://") + 3;
  return url.substr(start, url.find('/', start) - start);
}

std::mutex s_hosts_mutex;
std::map<std::string, Clock::time_point> s_down_until;

bool IsDown(const std::string& host)
{
  std::lock_guard lk{s_hosts_mutex};
  const auto it = s_down_until.find(host);
  return it != s_down_until.end() && Clock::now() < it->second;
}

void SetDown(const std::string& host)
{
  std::lock_guard lk{s_hosts_mutex};
  s_down_until[host] = Clock::now() + DOWN_TIME;
}

std::string MissingPath(const std::string& game_id)
{
  return File::GetUserPath(D_COVERCACHE_IDX) + game_id + ".missing";
}

bool IsKnownMissing(const std::string& game_id)
{
  std::error_code ec;
  const auto time = std::filesystem::last_write_time(StringToPath(MissingPath(game_id)), ec);
  return !ec && std::filesystem::file_time_type::clock::now() - time < MISSING_TIME;
}

std::string UrlEncode(std::string_view text)
{
  std::string out;
  for (const char c : text)
  {
    const u8 b = static_cast<u8>(c);
    if (Common::IsAlnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out += c;
    else
      out += fmt::format("%{:02X}", b);
  }
  return out;
}

std::string_view RegionName(DiscIO::Region region)
{
  switch (region)
  {
  case DiscIO::Region::NTSC_J:
    return "Japan";
  case DiscIO::Region::PAL:
    return "Europe";
  case DiscIO::Region::NTSC_K:
    return "Korea";
  default:
    return "USA";
  }
}

// libretro-thumbnails has these as underscores.
std::string LibretroEscape(std::string name)
{
  for (char& c : name)
  {
    if (std::string_view("&*/:`<>?\\|\"").find(c) != std::string_view::npos)
      c = '_';
  }
  return name;
}

void RemoveEnd(std::string* text, std::string_view end)
{
  if (text->size() > end.size() && text->ends_with(end))
    text->resize(text->size() - end.size());
}

std::string WithoutTag(const std::string& name, std::string_view tag)
{
  const size_t start = name.find(fmt::format(" ({}", tag));
  if (start == std::string::npos)
    return name;
  const size_t end = name.find(')', start);
  return end == std::string::npos ? name : name.substr(0, start) + name.substr(end + 1);
}
}  // namespace

std::string GetCoverPath(const std::string& game_id)
{
  return File::GetUserPath(D_COVERCACHE_IDX) + game_id + ".png";
}

std::vector<std::string> LibretroNames(const CoverQuery& query)
{
  std::vector<std::string> names;
  const auto add = [&](const std::string& name) {
    std::string escaped = LibretroEscape(name);
    if (!escaped.empty() && std::ranges::find(names, escaped) == names.end())
      names.push_back(std::move(escaped));
  };

  // Files named after the disc, "Game (USA) (En,Fr,Es) (Rev 1)", as the pictures are.
  std::string file = query.file_name;
  if (const size_t dot = file.rfind('.'); dot != std::string::npos)
    file.resize(dot);
  RemoveEnd(&file, ".nkit");
  if (const size_t region_start = file.find(" ("); region_start != std::string::npos)
  {
    add(file);
    const std::string no_rev = WithoutTag(file, "Rev ");
    add(no_rev);
    add(WithoutTag(no_rev, "Disc "));
    if (const size_t region_end = file.find(')', region_start); region_end != std::string::npos)
      add(file.substr(0, region_end + 1));
  }

  if (!query.title.empty())
  {
    std::string title = query.title;
    // "Game: Part" is "Game - Part" there.
    for (size_t at = title.find(": "); at != std::string::npos; at = title.find(": ", at))
      title.replace(at, 2, " - ");
    for (const char* const languages : {"", " (En,Fr,Es)", " (En,Fr,De,Es,It)"})
      add(fmt::format("{} ({}){}", title, RegionName(query.region), languages));
  }
  return names;
}

std::vector<std::string> CoverUrls(const CoverQuery& query)
{
  std::vector<std::string> urls;
  const char* const system = query.is_wii ? "Nintendo_-_Wii" : "Nintendo_-_GameCube";
  for (const std::string& name : LibretroNames(query))
  {
    urls.push_back(fmt::format(
        "https://raw.githubusercontent.com/libretro-thumbnails/{}/master/Named_Boxarts/{}.png",
        system, UrlEncode(name)));
  }
  return urls;
}

bool NeedsCover(const CoverState& state)
{
  return !state.game_id.empty() && std::ranges::all_of(state.game_id, Common::IsAlnum) &&
         !state.has_custom && !state.has_cached && !state.known_missing;
}

void FillCoverCacheState(CoverState* state)
{
  state->has_cached = !state->game_id.empty() && File::Exists(GetCoverPath(state->game_id));
  state->known_missing = !state->game_id.empty() && IsKnownMissing(state->game_id);
}

std::optional<std::string> LinkTarget(const std::string& url, std::string_view body)
{
  if (body.size() > 255 || !body.ends_with(".png") || body.find('/') != body.npos)
    return std::nullopt;
  if (!std::ranges::all_of(body, [](char c) { return static_cast<u8>(c) >= 0x20; }))
    return std::nullopt;
  return url.substr(0, url.rfind('/') + 1) + UrlEncode(body);
}

bool StoreCover(std::span<const u8> png, const std::string& path)
{
  // The size from the header, before anything is decoded.
  constexpr std::array<u8, 8> SIGNATURE{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  if (png.size() < 24 || !std::equal(SIGNATURE.begin(), SIGNATURE.end(), png.begin()))
    return false;
  u32 width = Common::swap32(png.data() + 16);
  u32 height = Common::swap32(png.data() + 20);
  if (width == 0 || height == 0 || width > MAX_COVER_SIDE || height > MAX_COVER_SIDE)
    return false;

  Common::UniqueBuffer<u8> image;
  if (!Common::LoadPNG(png, &image, &width, &height))
    return false;

  std::vector<u8> pixels(image.data(), image.data() + size_t{width} * height * 4);
  while (height > COVER_MAX_HEIGHT && width > 1)
  {
    const u32 half_width = width / 2;
    const u32 half_height = height / 2;
    const size_t row = size_t{width} * 4;
    std::vector<u8> half(size_t{half_width} * half_height * 4);
    for (u32 y = 0; y < half_height; ++y)
    {
      for (u32 x = 0; x < half_width; ++x)
      {
        const size_t at = y * 2 * row + size_t{x} * 8;
        for (u32 c = 0; c < 4; ++c)
        {
          const u32 sum = pixels[at + c] + pixels[at + 4 + c] + pixels[at + row + c] +
                          pixels[at + row + 4 + c];
          half[(size_t{y} * half_width + x) * 4 + c] = static_cast<u8>((sum + 2) / 4);
        }
      }
    }
    pixels = std::move(half);
    width = half_width;
    height = half_height;
  }

  // Never half a file under the name that is looked for.
  const std::string temp = path + ".new";
  if (!Common::SavePNG(temp, pixels.data(), Common::ImageByteFormat::RGBA, width, height,
                       width * 4))
  {
    File::Delete(temp, File::IfAbsentBehavior::NoConsoleWarning);
    return false;
  }
  return File::Rename(temp, path);
}

bool EnsureCover(const CoverQuery& query)
{
  // The ID is part of a file name.
  if (query.game_id.empty() || !std::ranges::all_of(query.game_id, Common::IsAlnum))
    return false;
  const std::string path = GetCoverPath(query.game_id);
  if (File::Exists(path))
    return true;
  if (IsKnownMissing(query.game_id))
    return false;

  bool all_answered = true;
  std::vector<std::string> urls = CoverUrls(query);
  bool linked = false;
  for (size_t i = 0; i < urls.size(); ++i)
  {
    const std::string url = urls[i];
    const std::string host = HostOf(url);
    if (IsDown(host))
    {
      all_answered = false;
      continue;
    }

    const auto deadline = Clock::now() + TRANSFER_TIMEOUT;
    Common::HttpRequest request{CONNECT_TIMEOUT, [&](s64, s64 now, s64, s64) {
                                  return now <= MAX_COVER_BYTES && Clock::now() < deadline;
                                }};
    const auto response = request.Get(url, {}, Common::HttpRequest::AllowedReturnCodes::All);
    const s32 code = request.GetLastResponseCode();
    if (!response || code == 429 || code >= 500)
    {
      INFO_LOG_FMT(COMMON, "Covers: {} does not answer ({})", host, code);
      SetDown(host);
      all_answered = false;
      continue;
    }
    // libretro's doubles are links: a 200 with the other picture's file name.
    if (code == 200 && !linked)
    {
      const auto target = LinkTarget(url, std::string_view{
                                              reinterpret_cast<const char*>(response->data()),
                                              response->size()});
      if (target)
      {
        linked = true;
        urls.insert(urls.begin() + i + 1, *target);
        continue;
      }
    }
    if (code == 200 && StoreCover(*response, path))
    {
      INFO_LOG_FMT(COMMON, "Covers: {} from {}", query.game_id, url);
      File::Delete(MissingPath(query.game_id), File::IfAbsentBehavior::NoConsoleWarning);
      return true;
    }
  }
  if (all_answered)
    File::WriteStringToFile(MissingPath(query.game_id), "");
  return false;
}
}  // namespace UICommon
