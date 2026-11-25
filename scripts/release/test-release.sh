#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Packages a stage with make-release.sh and checks the result the way Frametanium and FrameDrop use it, in a
# private HOME with BAIJI_VR_TEST=1 and a stand-in steamos-add-to-steam that records its calls.  It never starts
# the emulator and touches nothing outside its scratch folder.
#   scripts/release/test-release.sh [stage]
# The stage defaults to <repo>/state/stage-games/home/steamos/baiji.  BAIJI_TEST_DIR=DIR keeps the scratch
# folder there (the release is in DIR/out); otherwise it is removed.  Prints PASS / FAIL lines; exit 1 on any FAIL.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
STAGE=${1:-$REPO/state/stage-games/home/steamos/baiji}
case "${1:-}" in -h | --help) sed -n '3,/^set -u/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;; esac
if [ -n "${BAIJI_TEST_DIR:-}" ]; then T=$BAIJI_TEST_DIR; rm -rf "$T"; mkdir -p "$T"; else T=$(mktemp -d "${TMPDIR:-/tmp}/baijivr-release-test.XXXXXX"); fi
T=$(readlink -f "$T")
trap '[ -n "${BAIJI_TEST_DIR:-}" ] || rm -rf "$T"; kill $SLEEPER 2> /dev/null' EXIT
SLEEPER=
fails=0
ok() { echo "PASS $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
check() { local what=$1; shift; if "$@"; then ok "$what"; else bad "$what"; fi; }
no() { ! "$@"; }
cond() { local what=$1; shift; if eval "$*"; then ok "$what"; else bad "$what"; fi; }

# The art: the release's own (BAIJI_TEST_ART names another folder).
ART_IN=${BAIJI_TEST_ART:-$HERE/art}
mkdir -p "$T/art"
for f in portrait wide hero logo icon icon-64 icon-128 icon-256; do cp "$ART_IN/$f.png" "$T/art/$f.png"; done
export BAIJI_VR_ART=$T/art

OUT=$T/out
bash "$HERE/make-release.sh" 0.0.0-test "$STAGE" "$OUT" > "$T/make.log" 2>&1 || { cat "$T/make.log"; echo "FAIL make-release.sh"; exit 1; }
TAR=$OUT/baijivr-0.0.0-test-linux-arm64.tar.gz
ZIP=$OUT/baijivr-0.0.0-test-framedrop-linux-arm64.zip
MAN=$OUT/baijivr.framedrop.json
ls -l "$TAR" "$ZIP" | awk '{print "size", $5, $9}'

# ---- the files
check "sha256 in SHA256SUMS" bash -c "cd '$OUT' && sha256sum -c --quiet SHA256SUMS"
check "manifest sha256 is the zip's" test "$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["files"][0]["sha256"])' "$MAN")" = "$(sha256sum "$ZIP" | cut -d' ' -f1)"
check "manifest names the release url" grep -q 'releases/download/v0.0.0-test/baijivr-0.0.0-test-framedrop-linux-arm64.zip' "$MAN"
python3 -I - "$ZIP" << 'PY' > "$T/ziplist.txt"
import sys, zipfile, stat
for i in zipfile.ZipFile(sys.argv[1]).infolist():
    print(("L " if stat.S_ISLNK(i.external_attr >> 16) else "F ") + i.filename)
PY
check "zip holds no symbolic links" no grep -q '^L ' "$T/ziplist.txt"
for f in run.sh VERSION install.sh uninstall.sh; do check "zip has $f at the top" grep -qx "F $f" "$T/ziplist.txt"; done
check "zip has bin/baiji" grep -qx 'F bin/baiji' "$T/ziplist.txt"
tar -tzf "$TAR" > "$T/tarlist.txt"
for f in install.sh uninstall.sh steam-entry.py art/portrait.png art/icon-256.png VERSION COPYING LICENSES/GPL-3.0-or-later.txt LICENSES/Externals-licenses.md bin/baiji lib/baiji/; do
  check "tarball has $f" grep -qx "$f" "$T/tarlist.txt"
done
check "tarball has no saves or settings" no grep -E '(^|/)(User|Config|\.config|\.local)/' "$T/tarlist.txt"

# ---- a scratch home, a stand-in steamos-add-to-steam
H=$T/home
mkdir -p "$H" "$T/bin"
printf '#!/bin/bash\necho "$@" >> "%s/steam.log"\n[ -z "$FAKE_STEAM_FAIL" ]\n' "$T" > "$T/bin/steamos-add-to-steam"
chmod +x "$T/bin/steamos-add-to-steam"
REALPATH=$PATH
export HOME=$H BAIJI_VR_TEST=1 PATH=$T/bin:$REALPATH BAIJI_VR_CEF_PORT=1   # port 1: Steam is not reachable
unset BAIJI_VR_SKIP_STEAM XDG_STATE_HOME XDG_DATA_HOME XDG_CONFIG_HOME FAKE_STEAM_FAIL FRAMEDROP_RUN_DRY
APP=$H/baiji
steamcalls() { cat "$T/steam.log" 2> /dev/null | wc -l; }
mkdir -p "$T/unpacked"
tar -xzf "$TAR" -C "$T/unpacked"
U=$T/unpacked

# 1. not the Frame's HOME
out=$(HOME=$H BAIJI_VR_TEST= bash "$U/install.sh" 2>&1); rc=$?
check "refuses a HOME other than /home/steamos without BAIJI_VR_TEST" test "$rc" = 1 -a ! -e "$APP"

# 2. fresh install (as Frametanium: the unpacked folder)
mkdir -p "$H/.config/baiji" "$H/.local/share/baiji/GC"
echo cfg > "$H/.config/baiji/Dolphin.ini"; echo save > "$H/.local/share/baiji/GC/card.raw"
bash "$U/install.sh" > "$T/i1.log" 2>&1; rc=$?
check "fresh install exits 0" test "$rc" = 0
for f in bin/baiji bin/baiji-vr bin/baiji-uninstall lib/baiji share/baiji/sys art/icon.png VERSION COPYING; do check "installed $f" test -e "$APP/$f"; done
check "bin/baiji is executable" test -x "$APP/bin/baiji"
check "one steamos-add-to-steam call, for baiji-vr" test "$(cat "$T/steam.log")" = "$APP/bin/baiji-vr"
check "no install.sh left in ~/baiji" test ! -e "$APP/install.sh"
check "no .new or .old left" test -z "$(ls -d "$APP"/*.new "$APP"/*.old 2> /dev/null)"
check "config untouched" test "$(cat "$H/.config/baiji/Dolphin.ini")" = cfg
check "saves untouched" test "$(cat "$H/.local/share/baiji/GC/card.raw")" = save

# 3. update over an install with an extra old library and a known-good
: > "$APP/lib/baiji/libold-extra.so.1"; : > "$APP/share/baiji/old-extra"
echo known > "$APP/bin/baiji-known-good"; chmod +x "$APP/bin/baiji-known-good"
: > "$H/.local/share/baiji/GC/other.raw"
bash "$U/install.sh" > "$T/i2.log" 2>&1; rc=$?
check "update exits 0" test "$rc" = 0
check "the old library is gone" test ! -e "$APP/lib/baiji/libold-extra.so.1"
check "old share file is gone" test ! -e "$APP/share/baiji/old-extra"
check "baiji-known-good kept" test "$(cat "$APP/bin/baiji-known-good")" = known
check "update keeps config and saves" test -f "$H/.config/baiji/Dolphin.ini" -a -f "$H/.local/share/baiji/GC/card.raw" -a -f "$H/.local/share/baiji/GC/other.raw"
check "the library is the stage's" cmp -s "$STAGE/lib/baiji/$(ls "$STAGE/lib/baiji" | head -1)" "$APP/lib/baiji/$(ls "$STAGE/lib/baiji" | head -1)"

# 4. refuse while running: a copy of sleep as the installed baiji
cp "$(type -P sleep)" "$APP/bin/baiji.run" && mv "$APP/bin/baiji.run" "$APP/bin/baiji"
"$APP/bin/baiji" 120 & SLEEPER=$!
sleep 0.3
echo marker > "$APP/lib/baiji/marker"
out=$(bash "$U/install.sh" 2>&1); rc=$?
check "refuses while baiji runs" test "$rc" = 1 -a -e "$APP/lib/baiji/marker"
kill $SLEEPER; wait $SLEEPER 2> /dev/null; SLEEPER=
bash "$U/install.sh" > "$T/i3.log" 2>&1
check "installs again once it stopped" test ! -e "$APP/lib/baiji/marker" -a "$(head -c 4 "$APP/bin/baiji" | od -An -c | tr -d ' ')" = '177ELF'

# 5. existing shortcut: not duplicated
: > "$T/steam.log"
mkdir -p "$H/.local/share/Steam/userdata/1234/config"
printf '\x00shortcuts\x00\x00Exe\x00"/home/steamos/baiji/bin/baiji-vr"\x00' > "$H/.local/share/Steam/userdata/1234/config/shortcuts.vdf"
bash "$U/install.sh" > "$T/i4.log" 2>&1
check "an existing entry is left alone" test "$(steamcalls)" = 0
rm -rf "$H/.local/share/Steam"

# 6. tool missing, and failing
rm -rf "$APP"; : > "$T/steam.log"
out=$(PATH=$REALPATH bash "$U/install.sh" 2>&1); rc=$?
if PATH=$REALPATH command -v steamos-add-to-steam > /dev/null; then echo "skip: a real steamos-add-to-steam is on this PATH"; else
  cond "tool missing: exit 0 and the command in a note" 'test "$rc" = 0 && echo "$out" | grep -q "steamos-add-to-steam $APP/bin/baiji-vr" && test -x "$APP/bin/baiji"'
fi
out=$(FAKE_STEAM_FAIL=1 bash "$U/install.sh" 2>&1); rc=$?
cond "tool failing: exit 0 and a note" 'test "$rc" = 0 && echo "$out" | grep -q '\''not added'\'''
out=$(BAIJI_VR_SKIP_STEAM=1 bash "$U/install.sh" 2>&1)
check "BAIJI_VR_SKIP_STEAM skips the call" test "$(steamcalls)" = 1

# 7. dry run changes nothing
rm -rf "$APP"; : > "$T/steam.log"
out=$(bash "$U/install.sh" --dry-run 2>&1); rc=$?
check "dry run changes nothing" test "$rc" = 0 -a ! -e "$APP" -a "$(steamcalls)" = 0
cond "dry run says what it would do" 'echo "$out" | grep -q "would look for an entry"'
mkdir -p "$APP/bin"; echo old > "$APP/bin/baiji-vr"
bash "$U/install.sh" --dry-run > /dev/null 2>&1
check "dry run leaves an install alone" test "$(cat "$APP/bin/baiji-vr")" = old

# 8. a tarball argument (not from the unpacked folder)
rm -rf "$APP"
(cd "$T" && bash "$U/install.sh" "$TAR" > "$T/i5.log" 2>&1); rc=$?
check "tarball argument installs" test "$rc" = 0 -a -x "$APP/bin/baiji-vr" -a "$(cat "$APP/VERSION")" = 0.0.0-test

# 9. FrameDrop: the zip unpacked
rm -rf "$APP" "$H/.local/state"; : > "$T/steam.log"
mkdir -p "$T/fd"
python3 -I -c 'import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$ZIP" "$T/fd"   # extractall drops exec bits: as FrameDrop may
check "the unpack lost the exec bits (as expected)" test ! -x "$T/fd/bin/baiji-vr"
export FRAMEDROP_RUN_DRY=1
out=$(sh "$T/fd/run.sh" 2>&1); rc=$?
cond "run.sh first run installs and stops at the dry run" 'test "$rc" = 0 -a -x "$APP/bin/baiji-vr" && echo "$out" | grep -q "would start: $APP/bin/baiji-vr"'
check "run.sh restored exec bits and made no Steam call" test -x "$T/fd/install.sh" -a "$(steamcalls)" = 0
check "run.sh logged and noted the version" test -s "$H/.local/state/framedrop-baijivr/install.log" -a "$(cat "$H/.local/state/framedrop-baijivr/installed-version")" = 0.0.0-test
echo marker > "$APP/lib/baiji/marker"
out=$(sh "$T/fd/run.sh" 2>&1)
cond "run.sh second run does not reinstall" 'test -e "$APP/lib/baiji/marker" && ! echo "$out" | grep -q installing'
echo 0.0.1-test > "$T/fd/VERSION"
out=$(sh "$T/fd/run.sh" 2>&1)
check "run.sh with a new VERSION reinstalls" test ! -e "$APP/lib/baiji/marker" -a "$(cat "$H/.local/state/framedrop-baijivr/installed-version")" = 0.0.1-test
chmod -x "$T/fd/install.sh" "$T/fd/bin/"*
echo marker > "$APP/lib/baiji/marker"; echo 0.0.2-test > "$T/fd/VERSION"
sh "$T/fd/run.sh" > /dev/null 2>&1
check "run.sh works after the exec bits are lost" test ! -e "$APP/lib/baiji/marker" -a -x "$APP/bin/baiji" -a -x "$T/fd/bin/baiji"
unset FRAMEDROP_RUN_DRY

# 10. Frametanium: unpack, install, uninstall
rm -rf "$APP" "$H/.local/state"
F=$H/.local/share/frametanium/apps/baijivr
mkdir -p "$F" && tar -xzf "$TAR" -C "$F"
bash "$F/install.sh" > /dev/null 2>&1
check "Frametanium install.sh from the unpacked folder" test -x "$APP/bin/baiji-vr"
bash "$F/uninstall.sh" > "$T/u1.log" 2>&1; rc=$?
check "uninstall.sh removes the program and keeps saves and settings" test "$rc" = 0 -a ! -e "$APP" -a -f "$H/.local/share/baiji/GC/card.raw" -a -f "$H/.config/baiji/Dolphin.ini"
bash "$F/uninstall.sh" > "$T/u2.log" 2>&1; rc=$?
check "uninstall.sh works when ~/baiji is already gone" test "$rc" = 0
check "uninstall.sh --purge passes its arguments on" bash -c "bash '$F/install.sh' > /dev/null 2>&1 && bash '$F/uninstall.sh' --purge > /dev/null 2>&1 && [ ! -e '$H/.local/share/baiji' ] && [ ! -e '$H/.config/baiji' ]"

# 11. Steam's DevTools port (a stand-in): add, update by program, update by old name, two entries, odd HOME, art bytes
python3 -I "$HERE/mock-steam-cef.py" 0 "$T/st.json" "$T/mock.log" &
MOCK=$!
for _ in $(seq 50); do [ -s "$T/st.json.port" ] && break; sleep 0.1; done
export BAIJI_VR_CEF_PORT=$(cat "$T/st.json.port")
sj() { python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1])); print(eval(sys.argv[2]))' "$T/st.json" "$1"; }
rm -rf "$APP"; : > "$T/steam.log"; : > "$T/mock.log"
bash "$U/install.sh" > "$T/s1.log" 2>&1
check "Steam up: one entry added, no steamos-add-to-steam" test "$(sj 'len(d["shortcuts"])')" = 1 -a "$(steamcalls)" = 0
check "entry is named Baiji VR, program and start folder quoted" test "$(sj 'list(d["shortcuts"].values())[0]["name"]')" = "Baiji VR" -a "$(sj 'list(d["shortcuts"].values())[0]["exe"]')" = "\"$APP/bin/baiji-vr\"" -a "$(sj 'list(d["shortcuts"].values())[0]["start"]')" = "\"$APP/bin\""
check "icon is ~/baiji/art/icon.png" test "$(sj 'list(d["shortcuts"].values())[0]["icon"]')" = "$APP/art/icon.png"
ARTOK=1
for k in "0 portrait" "1 hero" "2 logo" "3 wide"; do
  set -- $k
  want=$(sha256sum "$T/art/$2.png" | cut -d' ' -f1)
  [ "$(sj "list(d['shortcuts'].values())[0]['art']['$1'][1]")" = "$want" ] || ARTOK=
done
check "the art bytes sent are the release's (portrait, hero, logo, wide)" test -n "$ARTOK"
check "art is installed in ~/baiji/art" cmp -s "$T/art/portrait.png" "$APP/art/portrait.png"
ID=$(sj 'list(d["shortcuts"])[0]')
bash "$U/install.sh" > "$T/s2.log" 2>&1
check "update by program: same appid, still one entry" test "$(sj 'len(d["shortcuts"])')" = 1 -a "$(sj 'list(d["shortcuts"])[0]')" = "$ID"
# an old entry named Baiji with another program is renamed and pointed here
printf '{"shortcuts": {"2147500000": {"name": "Baiji", "exe": "\\"/elsewhere/baiji-vr\\"", "start": "", "icon": "", "art": {}}}}' > "$T/st.json"
bash "$U/install.sh" > "$T/s3.log" 2>&1
check "update by old name: appid kept, renamed, program set" test "$(sj 'len(d["shortcuts"])')" = 1 -a "$(sj 'list(d["shortcuts"])[0]')" = 2147500000 -a "$(sj 'list(d["shortcuts"].values())[0]["name"]')" = "Baiji VR" -a "$(sj 'list(d["shortcuts"].values())[0]["exe"]')" = "\"$APP/bin/baiji-vr\""
# two entries: left alone, said so
printf '{"shortcuts": {"2147500001": {"name": "Baiji", "exe": "\\"/a/x\\"", "start": "", "icon": "", "art": {}}, "2147500002": {"name": "Baiji VR", "exe": "\\"/b/y\\"", "start": "", "icon": "", "art": {}}}}' > "$T/st.json"
: > "$T/steam.log"
out=$(bash "$U/install.sh" 2>&1)
cond "two entries: none changed, said so, no fallback call" 'test "$(sj "len(d[\"shortcuts\"])")" = 2 -a "$(sj "d[\"shortcuts\"][\"2147500001\"][\"exe\"]")" = "\"/a/x\"" -a "$(steamcalls)" = 0 && echo "$out" | grep -q "more than one Baiji entry"'
# an odd HOME: every string is JSON-encoded into the JavaScript
rm -f "$T/st.json"
OH="$T/ho\"me 'x\\ \$y é"
mkdir -p "$OH"
HOME=$OH bash "$U/install.sh" > "$T/s4.log" 2>&1
check "odd HOME characters reach Steam unchanged" test "$(sj 'list(d["shortcuts"].values())[0]["exe"]')" = "\"$OH/baiji/bin/baiji-vr\"" -a -x "$OH/baiji/bin/baiji-vr"
# a dry run lists the calls and does not connect
rm -f "$T/st.json"; : > "$T/mock.log"
out=$(bash "$U/install.sh" --dry-run 2>&1)
cond "dry run lists the Steam calls and sends none" 'test ! -s "$T/mock.log" && echo "$out" | grep -q "AddShortcut(" && echo "$out" | grep -q "SetCustomArtworkForApp("'
kill $MOCK 2> /dev/null
# unreachable (port closed): the old way, with a note to run again
export BAIJI_VR_CEF_PORT=1
rm -rf "$APP"; : > "$T/steam.log"
out=$(bash "$U/install.sh" 2>&1); rc=$?
cond "Steam unreachable: exit 0, steamos-add-to-steam, a note to run again" 'test "$rc" = 0 -a "$(steamcalls)" = 1 && echo "$out" | grep -q "again with Steam open"'

# 12. the packaged program, only with qemu
if command -v qemu-aarch64 > /dev/null; then
  qemu-aarch64 -L / "$U/bin/baiji-tool" --help > /dev/null 2>&1 && ok "baiji-tool --help under qemu" || echo "note: baiji-tool --help did not run under qemu (no aarch64 sysroot is expected here)"
else
  echo "skip: qemu-aarch64 is not installed; the program was not run"
fi

echo
if [ "$fails" = 0 ]; then echo "all passed"; else echo "$fails FAILED"; exit 1; fi
