#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Installs Baiji VR on the Steam Frame: copies the program, its libraries and its data to ~/baiji
# and adds a "baiji-vr" entry to the Steam library.  Safe to run again: it replaces bin, lib, share
# and the rest of the release as a whole (an old library does not stay), keeps bin/baiji-known-good,
# and never touches ~/.config/baiji or ~/.local/share/baiji (your saves and settings).
#   install.sh [--dry-run] [baijivr-*-linux-arm64.tar.gz]
# Without an argument it installs the folder it is in (the tarball carries this script at its top
# level, which is how Frametanium runs it).  It refuses while Baiji runs.
# The Steam entry "Baiji VR" and its art (art/ of the release, installed in ~/baiji/art) are set through
# Steam's local DevTools port by steam-entry.py: an entry for ~/baiji/bin/baiji-vr (or named Baiji VR or
# Baiji) is updated, else one is added.  When Steam is not reachable it falls back to the old way: a
# shortcuts.vdf that already names baiji/bin/baiji-vr is left alone, else steamos-add-to-steam; when that
# is missing or fails it prints the command.  Still exit 0.  SteamVR's settings are not touched.
# --dry-run prints what it would do and changes nothing.  It never asks a question.
#
# Environment (all optional):
#   BAIJI_VR_SKIP_STEAM=1   leave the Steam library alone (FrameDrop's run.sh sets it)
#   BAIJI_VR_CEF_PORT=N     Steam's DevTools port (default 8080), for a stand-in Steam
#   BAIJI_VR_TEST=1         allow a HOME other than /home/steamos (the program has that data path built in)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DEST=$HOME/baiji
DRY=
case "$1" in -h | --help) sed -n '3,/^set -e/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;; esac
if [ "$1" = --dry-run ]; then DRY=1; shift; fi

if [ "$HOME" != /home/steamos ] && [ "${BAIJI_VR_TEST:-}" != 1 ]; then
  echo "Baiji VR is built for the Steam Frame's /home/steamos (its data path is part of the program); HOME is $HOME. Nothing was installed." >&2
  exit 1
fi

act() { if [ -n "$DRY" ]; then echo "[dry run] would run: $*"; else "$@"; fi; }

# Refuse while a Baiji program of ~/baiji/bin runs: its executable is the file there.
for exe in /proc/[0-9]*/exe; do
  tgt=$(readlink "$exe" 2> /dev/null) || continue
  tgt=${tgt% (deleted)}
  case "$tgt" in "$DEST/bin/baiji" | "$DEST/bin/baiji-nogui")
    echo "Baiji is running (pid $(basename "$(dirname "$exe")")). Quit it and run install.sh again." >&2
    exit 1 ;;
  esac
done

SRC=$HERE
if [ -n "$1" ]; then
  [ -f "$1" ] || { echo "no such file: $1" >&2; exit 1; }
  SRC=$(mktemp -d)
  trap 'rm -rf "$SRC"' EXIT
  tar -xzf "$1" -C "$SRC"
  echo "installing $1"
else
  echo "installing the unpacked release in $SRC"
fi
for f in bin/baiji bin/baiji-vr bin/baiji-uninstall lib/baiji share/baiji art/portrait.png steam-entry.py; do
  [ -e "$SRC/$f" ] || { echo "not a Baiji VR release: $SRC/$f is missing" >&2; exit 1; }
done
if [ "$(readlink -f "$SRC")" = "$(readlink -f "$DEST")" ]; then
  echo "this is the installed copy; run install.sh from the unpacked release" >&2
  exit 1
fi

# Everything the release holds except these scripts goes in as whole folders and files.
ENTRIES=
for p in "$SRC"/*; do
  n=$(basename "$p")
  case "$n" in install.sh | uninstall.sh | run.sh | steam-entry.py) ;; *) ENTRIES="$ENTRIES $n" ;; esac
done

[ -z "$DRY" ] || echo "[dry run] nothing below is done; HOME=$HOME DEST=$DEST"
act mkdir -p "$DEST"
# All copies are made beside the old ones first, so a failed copy leaves the old install whole.
for n in $ENTRIES; do
  if [ -n "$DRY" ]; then echo "[dry run] would copy $SRC/$n -> $DEST/$n.new, then replace $DEST/$n"; continue; fi
  rm -rf "${DEST:?}/$n.new"
  cp -a "$SRC/$n" "$DEST/$n.new"
done
if [ -z "$DRY" ]; then
  chmod +x "$DEST/bin.new/"* 2> /dev/null || true
  if [ -f "$DEST/bin/baiji-known-good" ]; then
    cp -a "$DEST/bin/baiji-known-good" "$DEST/bin.new/baiji-known-good"
    echo "kept $DEST/bin/baiji-known-good"
  fi
fi
for n in $ENTRIES; do
  [ -z "$DRY" ] || continue
  rm -rf "${DEST:?}/$n.old"
  if [ -e "$DEST/$n" ]; then mv "$DEST/$n" "$DEST/$n.old"; fi
  mv "$DEST/$n.new" "$DEST/$n"
  rm -rf "${DEST:?}/$n.old"
done

# The Steam library entry: steam-entry.py through Steam's DevTools port (name, program, art), else the old ways.
STEAM_NOTE=
LAUNCHER=$DEST/bin/baiji-vr
fallback() {
  HAVE=
  for vdf in "$HOME"/.local/share/Steam/userdata/*/config/shortcuts.vdf "$HOME"/.steam/steam/userdata/*/config/shortcuts.vdf; do
    if [ -f "$vdf" ] && grep -aq 'baiji/bin/baiji-vr' "$vdf"; then HAVE=$vdf; break; fi
  done
  AGAIN="Run install.sh again with Steam open to set its name and art."
  if [ -n "$HAVE" ]; then
    STEAM_NOTE="Steam already has an entry for baiji-vr ($HAVE); it was left as it is. $AGAIN"
  elif command -v steamos-add-to-steam > /dev/null 2>&1 && steamos-add-to-steam "$LAUNCHER" > /dev/null 2>&1; then
    STEAM_NOTE="Added $LAUNCHER to the Steam library (restart Steam if it does not show). $AGAIN"
  else
    STEAM_NOTE="Steam's library entry was not added. When you want one, run: steamos-add-to-steam $LAUNCHER
$AGAIN"
  fi
}
if [ -n "${BAIJI_VR_SKIP_STEAM:-}" ]; then
  STEAM_NOTE="BAIJI_VR_SKIP_STEAM is set: the Steam library was left alone."
elif [ -n "$DRY" ]; then
  python3 -I "$SRC/steam-entry.py" --exe "$LAUNCHER" --art "$SRC/art" --icon "$DEST/art/icon.png" --dry-run
  echo "[dry run] if Steam is not reachable: look for baiji/bin/baiji-vr in shortcuts.vdf, else run: steamos-add-to-steam $LAUNCHER"
else
  rc=0
  python3 -I "$SRC/steam-entry.py" --exe "$LAUNCHER" --art "$DEST/art" --icon "$DEST/art/icon.png" || rc=$?
  case $rc in
    0) STEAM_NOTE="Steam library entry \"Baiji VR\" runs $LAUNCHER, with its art. Steam may need a restart to show the art everywhere." ;;
    11) STEAM_NOTE="Steam has more than one Baiji entry; they were left as they are." ;;
    *) fallback ;;
  esac
fi

if [ -n "$DRY" ]; then
  [ -z "$STEAM_NOTE" ] || echo "[dry run] $STEAM_NOTE"
  echo "[dry run] done: nothing was changed."
  exit 0
fi
cat << MSG

Baiji VR $(head -n 1 "$DEST/VERSION" 2> /dev/null) installed in $DEST.
Your saves and settings are in ~/.local/share/baiji and ~/.config/baiji (kept on every update).
$STEAM_NOTE
MSG
