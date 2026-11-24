// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Downloads, unpacking and the one worker thread of GameMods.

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <functional>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include <fmt/format.h>
#include <mbedtls/sha256.h>
#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#include <picojson.h>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/HttpRequest.h"
#include "Common/IOFile.h"
#include "Common/Logging/Log.h"
#include "Common/ScopeGuard.h"
#include "Common/StringUtil.h"

#include "Core/GameMods/GameModsInternal.h"

namespace fs = std::filesystem;

namespace GameMods::Internal
{
namespace
{
constexpr const char* USER_AGENT = "Baiji";
constexpr const char* CATALOG_URL =
    "https://raw.githubusercontent.com/simplysanatori/gc-wii-game-fixes/main/catalog/";

std::atomic_bool s_cancel;     // the player cancelled the install
std::atomic_bool s_shutdown;   // the program is closing
std::mutex s_thread_mutex;     // the two threads below and s_busy
std::thread s_worker;
std::thread s_refresh;
bool s_busy = false;

std::mutex s_progress_mutex;
Progress s_progress;

bool Stopped()
{
  return s_cancel || s_shutdown;
}

void SetProgress(const std::function<void(Progress&)>& change)
{
  std::lock_guard lock{s_progress_mutex};
  change(s_progress);
}

std::string Lower(std::string s)
{
  for (char& c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// --- Addresses ------------------------------------------------------------------------------

// "host" of https://host/path; "" when it has a port, a user or is not https.
std::string HostOf(const std::string& url)
{
  constexpr std::string_view scheme = "https://";
  if (!url.starts_with(scheme))
    return {};
  const auto end = url.find_first_of("/?#", scheme.size());
  const std::string host = Lower(url.substr(scheme.size(), end == std::string::npos ? end : end - scheme.size()));
  if (host.empty() || host.find_first_of("@:[") != std::string::npos)
    return {};
  return host;
}

bool KnownHost(const std::string& host)
{
  static const char* known[] = {"github.com",
                                "api.github.com",
                                "objects.githubusercontent.com",
                                "release-assets.githubusercontent.com",
                                "raw.githubusercontent.com",
                                "codeload.github.com",
                                "gamebanana.com",
                                "files.gamebanana.com",
                                "drive.google.com",
                                "drive.usercontent.google.com"};
  return std::ranges::any_of(known, [&](const char* k) { return host == k; }) ||
         host.ends_with(".gamebanana.com");
}

bool UrlAllowed(const std::string& url, const std::string& from_host, const std::string& entry_host)
{
  const std::string host = HostOf(url);
  return !host.empty() && (host == from_host || host == entry_host || KnownHost(host));
}

std::string UrlEncode(std::string_view s)
{
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (const unsigned char c : s)
  {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
    {
      out += static_cast<char>(c);
    }
    else
    {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

// --- Google Drive pages ---------------------------------------------------------------------

std::string HtmlUnescape(std::string s)
{
  static const std::pair<const char*, const char*> table[] = {
      {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&#x3D;", "="}};
  for (const auto& [from, to] : table)
  {
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + 1))
      s.replace(p, std::strlen(from), to);
  }
  return s;
}

// The value of attribute name inside one tag.
std::string Attr(std::string_view tag, std::string_view name)
{
  const std::string needle = std::string(name) + "=";
  for (size_t p = tag.find(needle); p != std::string_view::npos; p = tag.find(needle, p + 1))
  {
    if (p > 0 && !std::isspace(static_cast<unsigned char>(tag[p - 1])))
      continue;
    const size_t q = p + needle.size();
    if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\''))
      continue;
    const auto end = tag.find(tag[q], q + 1);
    if (end == std::string_view::npos)
      return {};
    return HtmlUnescape(std::string(tag.substr(q + 1, end - q - 1)));
  }
  return {};
}

bool ContainsNoCase(const std::string& html, std::string_view needle)
{
  return Lower(html).find(needle) != std::string::npos;
}

// Drive's "can't scan for viruses" page has a form whose action is the real download.
struct DrivePage
{
  std::string confirm_url;
  std::string error;
};

DrivePage ParseDrivePage(const std::string& html)
{
  DrivePage page;
  for (size_t f = html.find("<form"); f != std::string::npos; f = html.find("<form", f + 1))
  {
    const auto tag_end = html.find('>', f);
    if (tag_end == std::string::npos)
      break;
    std::string action = Attr(std::string_view(html).substr(f, tag_end - f), "action");
    if (action.find("/download") == std::string::npos)
      continue;
    const auto form_end = html.find("</form>", tag_end);
    const std::string_view body = std::string_view(html).substr(
        tag_end, form_end == std::string::npos ? std::string_view::npos : form_end - tag_end);
    std::string query;
    for (auto i = body.find("<input"); i != std::string_view::npos; i = body.find("<input", i + 1))
    {
      const auto ie = body.find('>', i);
      if (ie == std::string_view::npos)
        break;
      const auto input = body.substr(i, ie - i);
      const std::string name = Attr(input, "name");
      if (name.empty() || (!Attr(input, "type").empty() && Attr(input, "type") != "hidden"))
        continue;
      query += (query.empty() ? "" : "&") + UrlEncode(name) + "=" + UrlEncode(Attr(input, "value"));
    }
    if (action.front() == '/')
      action = "https://drive.usercontent.google.com" + action;
    if (query.empty() || action.find('?') != std::string::npos || !KnownHost(HostOf(action)))
      continue;
    page.confirm_url = action + "?" + query;
    return page;
  }
  if (ContainsNoCase(html, "too many users") || ContainsNoCase(html, "download quota") ||
      ContainsNoCase(html, "quota exceeded") || ContainsNoCase(html, "view or download this file at this time"))
    page.error = "Google Drive is limiting downloads of this file today.";
  else if (ContainsNoCase(html, "you need access") || ContainsNoCase(html, "request access") ||
           ContainsNoCase(html, "servicelogin"))
    page.error = "The file on Google Drive is not shared with everyone.";
  else
    page.error = "Google Drive did not give the file.";
  return page;
}

struct DriveItem
{
  std::string id, name;
  bool is_folder = false;
};

std::vector<DriveItem> ParseDriveFolder(const std::string& html)
{
  std::vector<DriveItem> out;
  constexpr std::string_view open = "id=\"entry-";
  const std::string_view page = html;
  for (auto p = page.find(open); p != std::string_view::npos;)
  {
    const auto next = page.find(open, p + open.size());
    const auto chunk = page.substr(p, next == std::string_view::npos ? std::string_view::npos : next - p);
    p = next;
    DriveItem item;
    const auto id_end = chunk.find('"', open.size());
    if (id_end == std::string_view::npos)
      continue;
    item.id = std::string(chunk.substr(open.size(), id_end - open.size()));
    if (const auto href = chunk.find("href=\""); href != std::string_view::npos)
    {
      const auto end = chunk.find('"', href + 6);
      item.is_folder = chunk.substr(href + 6, end == std::string_view::npos ? 0 : end - href - 6).find("/folders/") !=
                       std::string_view::npos;
    }
    constexpr std::string_view title = "class=\"flip-entry-title\">";
    const auto t = chunk.find(title);
    if (t == std::string_view::npos)
      continue;
    const auto t_end = chunk.find("</div>", t);
    item.name = HtmlUnescape(
        std::string(chunk.substr(t + title.size(), t_end == std::string_view::npos ? 0 : t_end - t - title.size())));
    if (item.id.size() >= 10 && item.id.size() <= 80 && !item.name.empty() &&
        std::ranges::all_of(item.id, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; }))
      out.push_back(std::move(item));
  }
  return out;
}

// --- Fetching -------------------------------------------------------------------------------

struct Resolved
{
  std::string url;
  u64 size = 0;
};

struct Fetcher
{
  std::string entry_host;
  u64 base_done = 0;   // bytes of the earlier parts
  u64 total_hint = 0;  // all parts, when known

  Common::HttpRequest::ProgressCallback Callback()
  {
    return [this](s64 total, s64 now, s64, s64) {
      if (Stopped())
        return false;
      SetProgress([&](GameMods::Progress& p) {
        p.bytes_done = base_done + static_cast<u64>(std::max<s64>(now, 0));
        p.bytes_total = total_hint ? total_hint : (total > 0 ? base_done + static_cast<u64>(total) : 0);
      });
      return true;
    };
  }

  std::string NoReply() const
  {
    return Stopped() ? "Cancelled." : "There is no connection to the site, or it stopped answering.";
  }

  // A small page (JSON or HTML). "" or a sentence.
  std::string GetPage(const std::string& url, std::string* body, int* code = nullptr)
  {
    if (!UrlAllowed(url, "", entry_host))
      return "The address is not one this list may use.";
    Common::HttpRequest http{std::chrono::seconds{15}, Callback()};
    const auto response = http.Get(url, {{"User-Agent", USER_AGENT}, {"Accept", "*/*"}},
                                   Common::HttpRequest::AllowedReturnCodes::All);
    if (!response)
      return NoReply();
    body->assign(response->begin(), response->end());
    if (code)
      *code = http.GetLastResponseCode();
    return {};
  }

  std::string GetJson(const std::string& url, const char* site, picojson::value* json)
  {
    std::string body;
    int code = 0;
    if (const std::string error = GetPage(url, &body, &code); !error.empty())
      return error;
    if (code == 404)
      return "The page for this download was not found.";
    if (code == 403 || code == 429)
      return fmt::format("{} is limiting downloads from this network, try again later.", site);
    if (code != 200 || !picojson::parse(*json, body).empty())
      return fmt::format("{} gave an answer that could not be read.", site);
    return {};
  }

  // Where the file of this source is, and how big.
  std::string Resolve(const Source& s, Resolved* out)
  {
    if (s.type == "https")
    {
      out->url = s.url;
      return {};
    }
    if (s.type == "gdrive")
    {
      out->url = "https://drive.usercontent.google.com/download?id=" + s.file_id + "&export=download&confirm=t";
      return {};
    }
    picojson::value json;
    if (s.type == "github_release")
    {
      const std::string url = fmt::format("https://api.github.com/repos/{}/releases/{}", s.repo,
                                          s.tag == "latest" ? "latest" : "tags/" + s.tag);
      if (const std::string error = GetJson(url, "GitHub", &json); !error.empty())
        return error;
      if (json.is<picojson::object>() && json.get<picojson::object>().contains("assets") &&
          json.get<picojson::object>().at("assets").is<picojson::array>())
      {
        for (const auto& a : json.get<picojson::object>().at("assets").get<picojson::array>())
        {
          if (!a.is<picojson::object>())
            continue;
          const auto& ao = a.get<picojson::object>();
          const auto name = ao.find("name");
          const auto link = ao.find("browser_download_url");
          if (name == ao.end() || link == ao.end() || !name->second.is<std::string>() ||
              !link->second.is<std::string>() || !GlobMatch(s.asset_pattern, name->second.get<std::string>()))
            continue;
          out->url = link->second.get<std::string>();
          if (const auto size = ao.find("size"); size != ao.end() && size->second.is<double>())
            out->size = static_cast<u64>(size->second.get<double>());
          return {};
        }
      }
      return "The release has no file that matches.";
    }
    if (s.type == "gamebanana")
    {
      const std::string url = fmt::format("https://gamebanana.com/apiv11/Mod/{}/ProfilePage", s.mod);
      if (const std::string error = GetJson(url, "GameBanana", &json); !error.empty())
        return error;
      if (!json.is<picojson::object>())
        return "GameBanana gave an answer that could not be read.";
      const auto& o = json.get<picojson::object>();
      const auto flag = [&](const char* k) { return o.contains(k) && o.at(k).is<bool>() && o.at(k).get<bool>(); };
      if (flag("_bIsTrashed") || flag("_bIsWithheld") || flag("_bIsPrivate"))
        return "The mod is no longer on GameBanana.";
      double newest = -1;
      bool unscanned = false;
      if (o.contains("_aFiles") && o.at("_aFiles").is<picojson::array>())
      {
        for (const auto& f : o.at("_aFiles").get<picojson::array>())
        {
          if (!f.is<picojson::object>())
            continue;
          const auto& fo = f.get<picojson::object>();
          const auto text = [&](const char* k) {
            return fo.contains(k) && fo.at(k).is<std::string>() ? fo.at(k).get<std::string>() : std::string{};
          };
          const auto number = [&](const char* k) {
            return fo.contains(k) && fo.at(k).is<double>() ? fo.at(k).get<double>() : 0.0;
          };
          if ((fo.contains("_bIsArchived") && fo.at("_bIsArchived").is<bool>() && fo.at("_bIsArchived").get<bool>()) ||
              !GlobMatch(s.file_pattern, text("_sFile")) || text("_sDownloadUrl").empty())
            continue;
          if (text("_sAvResult") != "clean")
          {
            unscanned = true;
            continue;
          }
          if (number("_tsDateAdded") > newest)
          {
            newest = number("_tsDateAdded");
            out->url = text("_sDownloadUrl");
            out->size = static_cast<u64>(number("_nFilesize"));
          }
        }
      }
      if (out->url.empty())
        return unscanned ? "GameBanana has not scanned this file for viruses yet." : "The mod has no file that matches.";
      return {};
    }
    if (s.type == "gdrive_folder")
    {
      std::vector<std::pair<std::string, int>> queue{{s.folder_id, 0}};
      bool read_any = false;
      for (size_t i = 0; i < queue.size() && i < 6; ++i)
      {
        std::string html;
        int code = 0;
        if (!GetPage("https://drive.google.com/embeddedfolderview?id=" + queue[i].first, &html, &code).empty() ||
            code != 200)
          continue;
        read_any = true;
        auto items = ParseDriveFolder(html);
        std::ranges::sort(items, {}, &DriveItem::name);
        for (const auto& item : items)
        {
          if (!item.is_folder && GlobMatch(s.file_pattern, item.name))
          {
            out->url = "https://drive.usercontent.google.com/download?id=" + item.id + "&export=download&confirm=t";
            return {};
          }
        }
        if (queue[i].second < 2)
        {
          for (const auto& item : items)
          {
            if (item.is_folder)
              queue.emplace_back(item.id, queue[i].second + 1);
          }
        }
      }
      if (Stopped())
        return "Cancelled.";
      return read_any ? "The Google Drive folder has no file that matches." :
                        "The Google Drive folder could not be read (is it shared with everyone?).";
    }
    return "This kind of download is not supported here.";
  }

  // Follows redirects hop by hop, each checked, until a file comes. "" or a sentence.
  std::string Download(std::string url, File::IOFile* file)
  {
    int confirms = 0;
    std::string from_host = HostOf(url);
    if (!UrlAllowed(url, from_host, entry_host))
      return "The download address is not one this list may use.";
    for (int hop = 0; hop < 12; ++hop)
    {
      Common::HttpRequest http{std::chrono::seconds{20}, Callback()};
      bool wrote = false;
      std::string redirect;
      const auto page = http.GetToFile(url, file, &wrote, &redirect, {{"User-Agent", USER_AGENT}});
      if (!page)
        return NoReply();
      const int code = http.GetLastResponseCode();
      if (wrote)
        return {};
      if (code >= 300 && code < 400 && !redirect.empty())
      {
        const std::string host = HostOf(redirect);
        if (!UrlAllowed(redirect, HostOf(url), entry_host))
          return "The download was sent to a site this list may not use.";
        url = redirect;
        from_host = host;
        continue;
      }
      const std::string html(page->begin(), page->end());
      if (code == 200 && HostOf(url).find("google.com") != std::string::npos)
      {
        const DrivePage drive = ParseDrivePage(html);
        if (!drive.confirm_url.empty() && confirms++ < 2)
        {
          url = drive.confirm_url;
          continue;
        }
        return drive.error;
      }
      if (code == 404)
        return "The download was not found.";
      if (code == 429 || code == 403)
        return fmt::format("{} is limiting downloads from this network, try again later.", HostOf(url));
      if (code == 200)
        return "The address leads to a web page, not a file.";
      return fmt::format("The site refused the download ({}).", code);
    }
    return "The download was passed on too many times.";
  }
};

std::string HexSha256(const std::string& path, bool* stopped)
{
  std::ifstream file(path, std::ios::binary);
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0);
  std::vector<char> buffer(1 << 20);
  while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0)
  {
    if (Stopped())
    {
      *stopped = true;
      break;
    }
    mbedtls_sha256_update_ret(&ctx, reinterpret_cast<const unsigned char*>(buffer.data()),
                              static_cast<size_t>(file.gcount()));
  }
  std::array<unsigned char, 32> hash{};
  mbedtls_sha256_finish_ret(&ctx, hash.data());
  mbedtls_sha256_free(&ctx);
  std::string hex;
  for (const unsigned char b : hash)
    hex += fmt::format("{:02x}", b);
  return hex;
}

std::string SpaceError(u64 needed)
{
  return fmt::format("There is not enough free space: about {:.1f} GB is needed.",
                     static_cast<double>(needed) / (1024.0 * 1024.0 * 1024.0));
}

bool IsArchiveFile(const std::string& path)
{
  std::array<char, 6> magic{};
  File::IOFile file(path, "rb");
  if (!file || !file.ReadBytes(magic.data(), magic.size()))
    return false;
  static constexpr char SEVEN_ZIP[] = {'7', 'z', '\xBC', '\xAF', '\x27', '\x1C'};
  return std::memcmp(magic.data(), "PK", 2) == 0 || std::memcmp(magic.data(), "Rar!", 4) == 0 ||
         std::memcmp(magic.data(), SEVEN_ZIP, 6) == 0;
}

bool AppendFile(const std::string& from, const std::string& to)
{
  std::ifstream in(from, std::ios::binary);
  std::ofstream out(to, std::ios::binary | std::ios::app);
  out << in.rdbuf();
  return in.good() || in.eof() ? out.good() : false;
}

// The whole install of one entry, up to switching it on. "" or a sentence.
std::string Install(const Entry& entry, const std::string& game_id)
{
  const std::string work = WorkDir(entry.list, entry.id);
  const std::string part = work + ".part";
  std::error_code ec;
  File::CreateFullPath(work + "/");
  fs::remove_all(work, ec);
  Common::ScopeGuard cleanup{[&] {
    for (size_t i = 0; i < entry.parts.size(); ++i)
      fs::remove(part + "." + std::to_string(i), ec);
    fs::remove_all(work, ec);
  }};

  Fetcher fetcher;
  for (const Source& s : entry.parts)
  {
    if (s.type == "https")
      fetcher.entry_host = HostOf(s.url);
  }
  fetcher.total_hint = entry.size;

  // A part that is an archive of its own is unpacked at once, next to the others; a part that is
  // not (a volume of a split archive) goes on the end of the one before. Each archive is deleted
  // once unpacked.
  std::vector<std::string> dirs;
  std::string pending;
  const auto unpack_pending = [&]() -> std::string {
    if (pending.empty())
      return {};
    if (!entry.sha256.empty() && dirs.empty())
    {
      bool stopped = false;
      const std::string hash = HexSha256(pending, &stopped);
      if (stopped)
        return "Cancelled.";
      if (!Common::CaseInsensitiveEquals(hash, entry.sha256))
        return "The download is not the file the list expects.";
    }
    SetProgress([](Progress& p) { p.phase = Progress::Phase::Unpacking; });
    const std::string dir = work + "/p" + std::to_string(dirs.size());
    const std::string error = ExtractArchive(pending, dir, s_cancel);
    fs::remove(pending, ec);
    pending.clear();
    if (error.empty())
      dirs.push_back(dir);
    return error;
  };

  u64 done = 0;
  u64 total_size = entry.size;
  for (size_t i = 0; i < entry.parts.size(); ++i)
  {
    SetProgress([&](Progress& p) {
      p.phase = Progress::Phase::Downloading;
      p.bytes_done = done;
    });
    Resolved resolved;
    if (const std::string error = fetcher.Resolve(entry.parts[i], &resolved); !error.empty())
      return error;
    if (i == 0 && resolved.size > total_size && entry.parts.size() == 1)
      total_size = resolved.size;
    const auto space = fs::space(fs::path(File::GetUserPath(D_LOAD_IDX)), ec);
    const u64 needed = std::max(total_size, resolved.size) * 2 + (64ULL << 20);
    if (!ec && space.available < needed)
      return SpaceError(needed);
    fetcher.base_done = done;
    fetcher.total_hint = total_size;
    const std::string path = part + "." + std::to_string(i);
    u64 size = 0;
    {
      File::IOFile file(path, "wb");
      if (!file)
        return "The download file could not be created.";
      if (const std::string error = fetcher.Download(resolved.url, &file); !error.empty())
        return error;
      file.Flush();
      size = static_cast<u64>(file.GetSize());
      if (!file.Close())
        return "The download file could not be written.";
    }
    done += size;
    if (!pending.empty() && !IsArchiveFile(path))
    {
      if (!AppendFile(path, pending))
        return "The download file could not be written.";
      fs::remove(path, ec);
    }
    else
    {
      if (const std::string error = unpack_pending(); !error.empty())
        return error;
      pending = path;
    }
  }
  if (const std::string error = unpack_pending(); !error.empty())
    return error;

  const std::string merged = work + "/merged";
  if (const std::string error = MergeParts(dirs, merged, entry.list == List::Textures); !error.empty())
    return error;
  const std::string error = entry.list == List::Textures ? FinishTexturePack(merged, entry) :
                                                           FinishRiivolution(merged, entry);
  if (!error.empty())
    return error;

  std::lock_guard lock{StateMutex()};
  const Result on = SetEnabledLocked(entry.list, game_id, entry, true);
  return on.ok ? "" : on.error;
}

void Run(Entry entry, std::string game_id)
{
  const std::string error = Install(entry, game_id);
  SetProgress([&](Progress& p) {
    if (error.empty())
      p.phase = Progress::Phase::Done;
    else if (Stopped())
      p.phase = Progress::Phase::Cancelled;
    else
      p.phase = Progress::Phase::Failed;
    p.error = p.phase == Progress::Phase::Failed ? error : "";
  });
  if (!error.empty() && !Stopped())
    WARN_LOG_FMT(COMMON, "GameMods: install of {} failed: {}", entry.id, error);
  std::lock_guard lock{s_thread_mutex};
  s_busy = false;
}

// --- Archives -------------------------------------------------------------------------------

#ifndef _WIN32
std::string RunBsdtar(const std::string& archive, const std::string& dir, const std::atomic_bool& cancel)
{
  std::vector<std::string> args = {"bsdtar", "-x", "-f", archive, "-C", dir, "--no-same-owner", "--no-same-permissions"};
  std::vector<char*> argv;
  for (std::string& a : args)
    argv.push_back(a.data());
  argv.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  Common::ScopeGuard actions_guard{[&] { posix_spawn_file_actions_destroy(&actions); }};
  posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);

  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, "bsdtar", &actions, nullptr, argv.data(), environ);
  if (rc == ENOENT)
    return "7z and rar files need the program bsdtar, which is not installed.";
  if (rc != 0)
    return "The program that unpacks the file could not be started.";
  int status = 0;
  while (true)
  {
    const pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid)
      break;
    if (done < 0)
      return "The program that unpacks the file stopped unexpectedly.";
    if (cancel || s_shutdown)
    {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      return "Cancelled.";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    return "The file could not be unpacked (is it damaged?).";
  return {};
}
#endif

// Nothing but plain files and folders may be left.
std::string CheckTree(const std::string& dir)
{
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
  {
    const auto status = it->symlink_status(ec);
    if (!fs::is_regular_file(status) && !fs::is_directory(status))
      return "The archive holds links or special files, which are not allowed.";
  }
  return ec ? "The unpacked files could not be read." : "";
}

// Dolphin never needs these, and the pack may not be needed on other systems.
void RemoveJunk(const std::string& dir)
{
  std::error_code ec;
  fs::remove_all(fs::path(dir) / "__MACOSX", ec);
}

void RenameGameIdsOff(const std::string& dir)
{
  std::error_code ec;
  std::vector<fs::path> hits;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
  {
    if (!it->is_regular_file(ec) || Lower(it->path().extension().string()) != ".txt")
      continue;
    const std::string stem = it->path().stem().string();
    if (stem.size() == 6 || stem.size() == 3 || Lower(stem) == "all")
      hits.push_back(it->path());
  }
  for (const auto& p : hits)
    fs::rename(p, fs::path(p).replace_extension(".off"), ec);
}

// Puts a prepared folder in place of an older one.
std::string MoveInto(const fs::path& from, const fs::path& target)
{
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  const fs::path old = target.string() + ".old-gamemods";
  fs::remove_all(old, ec);
  if (fs::exists(target, ec))
    fs::rename(target, old, ec);
  if (ec)
    return "The older files could not be replaced.";
  fs::rename(from, target, ec);
  if (ec)
  {
    fs::rename(old, target, ec);
    return "The files could not be put in place.";
  }
  fs::remove_all(old, ec);
  return {};
}

std::vector<fs::path> Children(const fs::path& dir)
{
  std::vector<fs::path> list;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    list.push_back(it->path());
  std::ranges::sort(list);
  return list;
}
}  // namespace

std::string ExtractZip(const std::string& zip, const std::string& dir, const std::atomic_bool& cancel)
{
  void* reader = mz_zip_reader_create();
  Common::ScopeGuard delete_guard{[&] { mz_zip_reader_delete(&reader); }};
  if (mz_zip_reader_open_file(reader, zip.c_str()) != MZ_OK)
    return "The file is not a readable zip (is it damaged?).";
  Common::ScopeGuard close_guard{[&] { mz_zip_reader_close(reader); }};

  // First every name, so that nothing is written from an archive that has a bad one.
  u64 total = 0;
  std::vector<std::string> names;
  for (int32_t rc = mz_zip_reader_goto_first_entry(reader); rc == MZ_OK; rc = mz_zip_reader_goto_next_entry(reader))
  {
    mz_zip_file* info = nullptr;
    if (mz_zip_reader_entry_get_info(reader, &info) != MZ_OK || !info->filename)
      return "The zip could not be read.";
    if (mz_zip_attrib_is_symlink(info->external_fa, info->version_madeby) == MZ_OK)
      return "The archive holds links, which are not allowed.";
    const auto safe = SafeRelativePath(info->filename);
    if (!safe)
      return "The archive holds a file with an unsafe path, so it was not unpacked.";
    total += static_cast<u64>(info->uncompressed_size);
    names.push_back(*safe);
  }
  std::error_code ec;
  const auto space = fs::space(fs::path(dir).parent_path(), ec);
  if (!ec && space.available < total + (64ULL << 20))
    return SpaceError(total + (64ULL << 20));

  const fs::path root(dir);
  fs::create_directories(root, ec);
  size_t index = 0;
  std::vector<u8> buffer(1 << 16);
  for (int32_t rc = mz_zip_reader_goto_first_entry(reader); rc == MZ_OK;
       rc = mz_zip_reader_goto_next_entry(reader), ++index)
  {
    if (cancel || s_shutdown)
      return "Cancelled.";
    const fs::path target = root / names[index];
    if (mz_zip_reader_entry_is_dir(reader) == MZ_OK)
    {
      fs::create_directories(target, ec);
      continue;
    }
    fs::create_directories(target.parent_path(), ec);
    File::IOFile out(target.string(), "wb");
    if (!out)
      return "A file could not be written.";
    if (mz_zip_reader_entry_open(reader) != MZ_OK)
      return "The zip could not be read (is it locked with a password?).";
    Common::ScopeGuard entry_guard{[&] { mz_zip_reader_entry_close(reader); }};
    while (true)
    {
      const int32_t n = mz_zip_reader_entry_read(reader, buffer.data(), static_cast<int32_t>(buffer.size()));
      if (n < 0)
        return "The zip could not be read (is it damaged?).";
      if (n == 0)
        break;
      if (!out.WriteBytes(buffer.data(), static_cast<size_t>(n)))
        return "A file could not be written (is the disk full?).";
      if (cancel || s_shutdown)
        return "Cancelled.";
    }
  }
  return CheckTree(dir);
}

std::string ExtractArchive(const std::string& archive, const std::string& dir, const std::atomic_bool& cancel)
{
  std::array<char, 6> magic{};
  {
    File::IOFile file(archive, "rb");
    if (!file || !file.ReadBytes(magic.data(), magic.size()))
      return "The download is empty.";
  }
  if (std::memcmp(magic.data(), "PK", 2) == 0)
    return ExtractZip(archive, dir, cancel);
  static constexpr char SEVEN_ZIP[] = {'7', 'z', '\xBC', '\xAF', '\x27', '\x1C'};
  if (std::memcmp(magic.data(), SEVEN_ZIP, 6) != 0 && std::memcmp(magic.data(), "Rar!", 4) != 0)
    return "The download is not a zip, 7z or rar file.";
#ifdef _WIN32
  return "7z and rar files are not supported here.";
#else
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (const std::string error = RunBsdtar(archive, dir, cancel); !error.empty())
    return error;
  return CheckTree(dir);
#endif
}

namespace
{
std::set<std::string> TopNames(const fs::path& dir)
{
  std::set<std::string> names;
  for (const auto& kid : Children(dir))
    names.insert(kid.filename().string());
  return names;
}

// Moves the content of from into to; where both have a folder they are merged, files of from win.
void MergeInto(const fs::path& from, const fs::path& to)
{
  std::error_code ec;
  fs::create_directories(to, ec);
  for (const auto& kid : Children(from))
  {
    const fs::path target = to / kid.filename();
    if (fs::is_directory(kid, ec) && fs::is_directory(target, ec))
    {
      MergeInto(kid, target);
      continue;
    }
    fs::remove_all(target, ec);
    fs::rename(kid, target, ec);
  }
}
}  // namespace

std::string MergeParts(const std::vector<std::string>& dirs, const std::string& out, bool drop_wrappers)
{
  // Separate archives that already share their top level are merged as they are; otherwise each
  // loses its own wrapper folders first.
  bool lined_up = true;
  for (size_t i = 1; i < dirs.size(); ++i)
  {
    const auto first = TopNames(dirs[0]), next = TopNames(dirs[i]);
    lined_up = lined_up && std::ranges::any_of(next, [&](const std::string& n) { return first.contains(n); });
  }
  std::error_code ec;
  fs::create_directories(out, ec);
  for (const std::string& dir : dirs)
  {
    RemoveJunk(dir);
    fs::path root(dir);
    if (drop_wrappers && dirs.size() > 1 && !lined_up)
    {
      for (auto kids = Children(root); kids.size() == 1 && fs::is_directory(kids[0]); kids = Children(root))
        root = kids[0];
    }
    MergeInto(root, out);
  }
  return fs::exists(out, ec) && !Children(out).empty() ? "" : "The download holds no files.";
}

std::string FinishTexturePack(const std::string& unpacked, const Entry& entry)
{
  RemoveJunk(unpacked);
  fs::path root(unpacked);
  // Wrapper folders: a folder that holds nothing but one folder.
  for (auto kids = Children(root); kids.size() == 1 && fs::is_directory(kids[0]); kids = Children(root))
    root = kids[0];
  if (entry.layout == "variants")
  {
    // Several folders of textures are alternatives; only the first in name order is used.
    for (const auto& kid : Children(root))
    {
      if (fs::is_directory(kid))
      {
        root = kid;
        break;
      }
    }
  }
  if (Children(root).empty())
    return "The download holds no files.";
  RenameGameIdsOff(root.string());
  if (const std::string error = MoveInto(root, TextureDir(entry.id)); !error.empty())
    return error;
  std::error_code ec;
  fs::remove_all(unpacked, ec);
  return {};
}

std::string FinishRiivolution(const std::string& unpacked, const Entry& entry)
{
  RemoveJunk(unpacked);
  if (FindXml(unpacked, entry.xml).empty())
    return "The download has no Riivolution file that the list expects.";
  return MoveInto(unpacked, RiivolutionDir(entry.id));
}

bool StartWorker(const Entry& entry, const std::string& game_id)
{
  std::lock_guard lock{s_thread_mutex};
  if (s_busy || s_shutdown)
    return false;
  if (s_worker.joinable())
    s_worker.join();
  s_busy = true;
  s_cancel = false;
  SetProgress([&](Progress& p) {
    p = {};
    p.phase = Progress::Phase::Downloading;
    p.id = entry.id;
    p.name = entry.name;
    p.bytes_total = entry.size;
  });
  s_worker = std::thread(Run, entry, game_id);
  return true;
}

void StopWorkers()
{
  s_shutdown = true;
  std::thread worker, refresh;
  {
    std::lock_guard lock{s_thread_mutex};
    worker = std::move(s_worker);
    refresh = std::move(s_refresh);
  }
  if (worker.joinable())
    worker.join();
  if (refresh.joinable())
    refresh.join();
  s_shutdown = false;
}
}  // namespace GameMods::Internal

namespace GameMods
{
using namespace Internal;

void CancelInstall()
{
  s_cancel = true;
}

Progress GetProgress()
{
  std::lock_guard lock{s_progress_mutex};
  return s_progress;
}

void RefreshCatalogAsync()
{
  static std::chrono::steady_clock::time_point last{};
  std::lock_guard lock{s_thread_mutex};
  const auto now = std::chrono::steady_clock::now();
  if (s_shutdown || (last != std::chrono::steady_clock::time_point{} && now - last < std::chrono::minutes(10)))
    return;
  last = now;
  if (s_refresh.joinable())
    s_refresh.join();
  s_refresh = std::thread([] {
    for (const List list : {List::Mods, List::Textures})
    {
      Common::HttpRequest http{std::chrono::seconds{10}, [](s64, s64, s64, s64) { return !s_shutdown.load(); }};
      const std::string name = list == List::Mods ? "mods.json" : "textures.json";
      const auto response = http.Get(std::string(CATALOG_URL) + name, {{"User-Agent", USER_AGENT}},
                                     Common::HttpRequest::AllowedReturnCodes::All);
      // Not published yet or no network: nothing to say.
      if (!response || http.GetLastResponseCode() != 200)
        continue;
      const std::string text(response->begin(), response->end());
      if (!ParseCatalog(list, text))
        continue;
      const std::string path = CatalogCachePath(list);
      File::CreateFullPath(path);
      if (File::WriteStringToFile(path + ".new", text))
        File::Rename(path + ".new", path);
    }
  });
}
}  // namespace GameMods

namespace GameMods::Internal
{
std::string ResolveSource(const Source& source, std::string* url, u64* size)
{
  Fetcher fetcher;
  Resolved resolved;
  const std::string error = fetcher.Resolve(source, &resolved);
  *url = resolved.url;
  *size = resolved.size;
  return error;
}
}  // namespace GameMods::Internal
