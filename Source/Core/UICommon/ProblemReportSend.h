// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <string_view>

#include "Common/CommonTypes.h"

// Send Report: one request to Sentry with what "See What's Sent" shows (an event, and the report's
// files as attachments), only on the player's tap. Nothing else of Sentry is in here. The event names
// no user and no host, asks for the sender's address not to be attached (infer_ip "never") and for no
// place to be made of it (an empty user.geo).
namespace ProblemReport
{
struct Report;

struct Dsn
{
  std::string url;  // "https://o1.ingest.sentry.io/api/<project>/envelope/"
  std::string key;
};

// scheme://key[:secret]@host[:port]/[prefix/]project
std::optional<Dsn> ParseDsn(std::string_view dsn);

// The one this build was made with (Tools/report-dsn.sh), or empty.
std::string GetBuiltInDsn();
// Without one there is no Send Report.
bool CanSend();

// 32 hexadecimal digits.
std::string MakeEventId();
// The event's title: the first line of the player's words, or "Problem Report".
std::string MakeTitle(const Report& report);
// What is sent.
std::string MakeEnvelope(const Report& report, const std::string& event_id, s64 time);

struct SendResult
{
  bool ok = false;
  std::string error;     // for the player: "Couldn't Connect"
  std::string event_id;  // of what was sent
};
// Waits for the server: not on the host thread. `cancel` ends it early.
SendResult Send(const Report& report, std::string_view dsn,
                const std::atomic<bool>* cancel = nullptr);
}  // namespace ProblemReport
