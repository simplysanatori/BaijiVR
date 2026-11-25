#!/usr/bin/env bash
# Finds the address that gives a build its Send Report button, and prints only whether it was found.
#   Tools/report-dsn.sh               "found (...)" and exit 0, or "not found" and exit 1
#   Tools/report-dsn.sh --file FILE   also writes it into FILE (mode 600), for BAIJI_REPORT_DSN_FILE;
#                                     the caller deletes FILE after configuring (compile-cross-sdk.sh does)
# Where from: $BAIJI_REPORT_DSN, else ~/.config/baijivr-dev/sentry-dsn (a developer's machine, mode 600).
# It is never committed, printed or logged; a build without one can save a report, not send it.
set -euo pipefail
out=""
case "${1:-}" in
  --file) [[ $# -eq 2 ]] || { echo "usage: $0 [--file FILE]" >&2; exit 2; }; out="$2" ;;
  "") ;;
  *) echo "usage: $0 [--file FILE]" >&2; exit 2 ;;
esac
local_file="$HOME/.config/baijivr-dev/sentry-dsn"
dsn=""
from=""
if [[ -n "${BAIJI_REPORT_DSN:-}" ]]; then
  dsn="$BAIJI_REPORT_DSN"
  from="the environment"
elif [[ -r "$local_file" ]]; then
  IFS= read -r dsn <"$local_file" || true
  dsn="${dsn//[$' \t\r']/}"
  from="~/.config/baijivr-dev/sentry-dsn"
fi
if [[ -z "$dsn" ]]; then
  echo "Report DSN: not found (no Send Report)"
  exit 1
fi
if ! [[ "$dsn" =~ ^https://[A-Za-z0-9]+@[A-Za-z0-9.:-]+/([A-Za-z0-9._/-]*/)?[0-9]+$ ]]; then
  echo "Report DSN: found in $from, but it is not an https Sentry DSN (its value is not printed)" >&2
  exit 2
fi
if [[ -n "$out" ]]; then
  (umask 077; printf '%s\n' "$dsn" >"$out")
  chmod 600 "$out"
fi
echo "Report DSN: found ($from)"
