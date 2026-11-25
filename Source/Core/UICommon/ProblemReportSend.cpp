// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// SPDX-License-Identifier: GPL-2.0-or-later

#include "UICommon/ProblemReportSend.h"

#include <array>
#include <chrono>
#include <ctime>

#include <fmt/format.h>
#include <picojson.h>

#include "Common/HttpRequest.h"
#include "Common/Random.h"
#include "Common/Version.h"

#include "UICommon/ProblemReport.h"
// Made by CMake, never committed: BAIJI_REPORT_DSN and BAIJI_REPORT_ENVIRONMENT.
#include "baiji_report_config.h"

namespace ProblemReport
{
std::optional<Dsn> ParseDsn(std::string_view dsn)
{
  const std::size_t scheme_end = dsn.find("://");
  const std::size_t at = dsn.find('@');
  if (scheme_end == std::string_view::npos || at == std::string_view::npos || at < scheme_end)
    return std::nullopt;
  const std::string_view scheme = dsn.substr(0, scheme_end);
  std::string_view key = dsn.substr(scheme_end + 3, at - scheme_end - 3);
  key = key.substr(0, key.find(':'));
  const std::size_t slash = dsn.find('/', at);
  if (slash == std::string_view::npos || key.empty() || (scheme != "https" && scheme != "http"))
    return std::nullopt;
  const std::string_view host = dsn.substr(at + 1, slash - at - 1);
  std::string_view path = dsn.substr(slash);
  while (path.ends_with('/'))
    path.remove_suffix(1);
  const std::size_t last = path.rfind('/');
  if (host.empty() || last == std::string_view::npos)
    return std::nullopt;
  return Dsn{fmt::format("{}://{}{}/api{}/envelope/", scheme, host, path.substr(0, last),
                         path.substr(last)),
             std::string(key)};
}

std::string GetBuiltInDsn()
{
  return BAIJI_REPORT_DSN;
}

bool CanSend()
{
  return ParseDsn(GetBuiltInDsn()).has_value();
}

std::string MakeEventId()
{
  std::array<u8, 16> bytes;
  Common::Random::Generate(bytes.data(), bytes.size());
  // A random UUID's marks.
  bytes[6] = (bytes[6] & 0x0F) | 0x40;
  bytes[8] = (bytes[8] & 0x3F) | 0x80;
  std::string id;
  for (const u8 b : bytes)
    id += fmt::format("{:02x}", b);
  return id;
}

std::string MakeTitle(const Report& report)
{
  if (report.kind != Kind::Manual)
  {
    return report.kind == Kind::Unexpected ? report.headline :
                                             report.headline + ": " + report.reason;
  }
  std::string line = report.note.substr(0, report.note.find('\n'));
  while (line.ends_with(' ') || line.ends_with('\r'))
    line.pop_back();
  if (line.empty())
    return "Problem Report";
  if (line.size() > 120)
  {
    std::size_t cut = 117;
    // Not inside a UTF-8 character.
    while (cut > 0 && (static_cast<u8>(line[cut]) & 0xC0) == 0x80)
      --cut;
    line = line.substr(0, cut) + "...";
  }
  return line;
}

namespace
{
picojson::value Text(const std::string& s)
{
  return picojson::value(s);
}

picojson::value Join(const std::vector<std::string>& lines)
{
  std::string s;
  for (const std::string& line : lines)
    s += line + '\n';
  return Text(s);
}

std::string Address(u64 v)
{
  return fmt::format("0x{:x}", v);
}

picojson::value MakeException(const Report& report)
{
  picojson::object exception;
  // "SIGSEGV", and what is known of why.
  exception["type"] = Text(report.crash.substr(0, report.crash.find(' ')));
  exception["value"] = Text(report.exception.empty() ? report.reason : report.exception);
  picojson::object mechanism;
  mechanism["type"] = Text("signalhandler");
  mechanism["handled"] = picojson::value(false);
  exception["mechanism"] = picojson::value(mechanism);
  // The oldest call first.
  picojson::array frames;
  for (auto it = report.frames.rbegin(); it != report.frames.rend(); ++it)
  {
    picojson::object frame;
    frame["instruction_addr"] = Text(Address(it->address));
    if (it->module >= 0)
    {
      const Module& module = report.modules[static_cast<std::size_t>(it->module)];
      frame["package"] = Text(module.path);
      frame["image_addr"] = Text(Address(module.base));
    }
    if (!it->symbol.empty())
      frame["function"] = Text(it->symbol.substr(0, it->symbol.rfind('+')));
    frames.emplace_back(frame);
  }
  if (!frames.empty())
  {
    picojson::object stacktrace;
    stacktrace["frames"] = picojson::value(frames);
    exception["stacktrace"] = picojson::value(stacktrace);
  }
  picojson::object values;
  values["values"] = picojson::value(picojson::array{picojson::value(exception)});
  return picojson::value(values);
}

picojson::value MakeDebugMeta(const Report& report)
{
  picojson::array images;
  for (const Module& module : report.modules)
  {
    picojson::object image;
    image["type"] = Text("elf");
    image["code_file"] = Text(module.path);
    image["image_addr"] = Text(Address(module.base));
    image["image_size"] = picojson::value(static_cast<double>(module.end - module.base));
    if (!module.build_id.empty())
    {
      image["code_id"] = Text(module.build_id);
      image["debug_id"] = Text(DebugId(module.build_id));
    }
    images.emplace_back(image);
  }
  picojson::object meta;
  meta["images"] = picojson::value(images);
  return picojson::value(meta);
}

std::string MakeEvent(const Report& report, const std::string& event_id, s64 time)
{
  const bool manual = report.kind == Kind::Manual;
  picojson::object event;
  event["event_id"] = Text(event_id);
  event["timestamp"] = picojson::value(static_cast<double>(time));
  event["platform"] = Text("native");
  event["level"] = Text(manual ? "warning" : "fatal");
  event["release"] = Text(report.release.empty() ? GetReleaseName() : report.release);
  event["environment"] = Text(BAIJI_REPORT_ENVIRONMENT);
  event["server_name"] = Text("");
  picojson::object message;
  message["formatted"] = Text(MakeTitle(report));
  event["message"] = picojson::value(message);

  picojson::object tags;
  const auto tag = [&tags](const char* key, const std::string& value) {
    if (!value.empty())
      tags[key] = Text(value.substr(0, 200));
  };
  tag("report", manual ? "manual" : "crash");
  tag("reason", std::string(KindKey(report.kind)));
  tag("mode", report.display.empty() ? "" : report.display == "Flat" ? "flat" : "vr");
  tag("display", report.display);
  tag("game_id", report.game_id);
  tag("gpu", report.gpu);
  tag("driver", report.driver);
  event["tags"] = picojson::value(tags);

  picojson::object extra;
  extra["version"] = Text(report.version);
  extra["system"] = Text(report.os);
  if (!report.title.empty())
    extra["game"] = Text(report.title);
  if (!report.detail.empty())
    extra["detail"] = Text(report.detail);
  if (!report.note.empty())
    extra["note"] = Text(report.note);
  if (!report.mods.empty())
    extra["mods"] = Join(report.mods);
  if (!report.settings.empty())
    extra["settings"] = Join(report.settings);
  event["extra"] = picojson::value(extra);

  if (!report.crash.empty())
    event["exception"] = MakeException(report);
  if (!report.modules.empty())
    event["debug_meta"] = MakeDebugMeta(report);

  // No address of the sender, and no place made of it.
  picojson::object sdk_settings;
  sdk_settings["infer_ip"] = Text("never");
  picojson::object sdk;
  sdk["name"] = Text("baijivr");
  sdk["version"] = Text("1");
  sdk["settings"] = picojson::value(sdk_settings);
  event["sdk"] = picojson::value(sdk);
  picojson::object user;
  user["geo"] = picojson::value(picojson::object{});
  event["user"] = picojson::value(user);
  return picojson::value(event).serialize();
}
}  // namespace

std::string MakeEnvelope(const Report& report, const std::string& event_id, s64 time)
{
  picojson::object header;
  header["event_id"] = Text(event_id);
  std::string envelope = picojson::value(header).serialize() + '\n';
  const auto add = [&envelope](const char* type, const std::string& name, const char* content_type,
                               const std::string& data) {
    picojson::object item;
    item["type"] = Text(type);
    item["length"] = picojson::value(static_cast<double>(data.size()));
    item["content_type"] = Text(content_type);
    if (!name.empty())
      item["filename"] = Text(name);
    envelope += picojson::value(item).serialize() + '\n' + data + '\n';
  };
  add("event", "", "application/json", MakeEvent(report, event_id, time));
  for (const auto& [name, data] : GetFiles(report))
    add("attachment", name, "text/plain", data);
  return envelope;
}

SendResult Send(const Report& report, std::string_view dsn_text, const std::atomic<bool>* cancel)
{
  SendResult result;
  const std::optional<Dsn> dsn = ParseDsn(dsn_text);
  if (!dsn)
  {
    result.error = "No Place to Send To";
    return result;
  }
  const std::string event_id = MakeEventId();
  const std::string envelope = MakeEnvelope(report, event_id, std::time(nullptr));
  Common::HttpRequest http{std::chrono::seconds{15},
                           [cancel](s64, s64, s64, s64) { return !cancel || !*cancel; }};
  // The key is in a header: the address may be in the log.
  const auto reply = http.Post(
      dsn->url, envelope,
      {{"Content-Type", "application/x-sentry-envelope"},
       {"X-Sentry-Auth", "Sentry sentry_version=7, sentry_client=baijivr/1, sentry_key=" + dsn->key},
       {"User-Agent", "baijivr/1"},
       {"Expect", std::nullopt}},
      Common::HttpRequest::AllowedReturnCodes::All);
  if (!reply)
  {
    result.error = cancel && *cancel ? "Cancelled" : "Couldn't Connect";
    return result;
  }
  const s32 status = http.GetLastResponseCode();
  result.ok = status >= 200 && status < 300;
  if (result.ok)
    result.event_id = event_id;
  else if (status == 429)
    result.error = "Too Many Reports: Try Again Later";
  else
    result.error = fmt::format("The Server Said {}", status);
  return result;
}
}  // namespace ProblemReport
