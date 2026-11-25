#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Packages a finished Frame stage for the Releases page, into an output folder outside the repository:
#   baijivr-<version>-linux-arm64.tar.gz            what Frametanium installs (install.sh and uninstall.sh at the top)
#   baijivr-<version>-framedrop-linux-arm64.zip     what FrameDrop installs (run.sh at the top)
#   baijivr.framedrop.json                          FrameDrop's manifest: the zip's release url and sha256
#   SHA256SUMS                                      of the three
# The tarball holds the stage (bin, lib/baiji, share), VERSION, COPYING, LICENSES/ (the licence texts, the
# FrameUI font and icon licences, Externals/licenses.md) and the two installer scripts, so the unpacked archive
# installs itself.  The programs in bin/ are stripped when an aarch64 strip is there.  art/ holds the Steam library art
# (portrait, wide, hero, logo, icon, icon-64/128/256) from scripts/release/art/, or the folder BAIJI_VR_ART names; it is required.
#   scripts/release/make-release.sh VERSION [stage [out-dir]]      (for example 0.9.0)
# The stage defaults to <repo>/state/stage-games/home/steamos/baiji (built with INSTALL_PREFIX=/home/steamos/baiji
# by scripts/compile-cross-sdk.sh).  The output folder defaults to ~/Games/BaijiSteamVR-frame/release-out
# (BAIJI_OUT names it as well) and must not be inside the repository.
# It refuses an incomplete stage, and a program with a "/home/" path in its strings other than /home/steamos
# (the data path); it lists them.  Needs no headset and no network.  Then check it with test-release.sh.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
VERSION=$1
[ -n "$VERSION" ] || { sed -n '3,/^set -e/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 2; }
STAGE_IN=${2:-$REPO/state/stage-games/home/steamos/baiji}
OUT=${3:-${BAIJI_OUT:-$HOME/Games/BaijiSteamVR-frame/release-out}}
OUT=$(readlink -m "$OUT")
case "$OUT/" in "$REPO"/*) echo "the output folder $OUT is inside the repository; name one outside it" >&2; exit 1 ;; esac

for f in bin/baiji bin/baiji-nogui bin/baiji-tool bin/baiji-vr bin/baiji-uninstall lib/baiji share/baiji/sys share/baiji/licenses; do
  [ -e "$STAGE_IN/$f" ] || { echo "the stage $STAGE_IN is not complete: $f is missing (run scripts/compile-cross-sdk.sh)" >&2; exit 1; }
done
ART=${BAIJI_VR_ART:-$HERE/art}
for f in portrait wide hero logo icon icon-64 icon-128 icon-256; do
  [ -f "$ART/$f.png" ] || { echo "missing art $ART/$f.png (BAIJI_VR_ART=DIR names another folder)" >&2; exit 1; }
done
for f in COPYING LICENSES Externals/licenses.md Source/Core/VideoCommon/VR/FrameUI/licenses scripts/release/install.sh \
         scripts/release/uninstall.sh scripts/release/framedrop-run.sh scripts/release/make-framedrop-zip.py scripts/release/steam-entry.py; do
  [ -e "$REPO/$f" ] || { echo "missing $REPO/$f" >&2; exit 1; }
done
# A program that names a home folder other than the Frame's holds the builder's paths.
BAD=
for p in "$STAGE_IN"/bin/*; do
  [ "$(head -c 4 "$p")" = "$(printf '\177ELF')" ] || continue
  H=$(strings -a "$p" | grep -F '/home/' | grep -v '/home/steamos' | sort -u | head -n 8 | cut -c1-140 || true)
  [ -z "$H" ] || BAD="$BAD
$(basename "$p"):
$(echo "$H" | sed 's/^/  /')"
done
if [ -n "$BAD" ]; then
  echo "refusing to package $STAGE_IN: a program names a /home/ path other than /home/steamos:$BAD" >&2
  exit 1
fi

mkdir -p "$OUT"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
T=$WORK/t
mkdir -p "$T"
cp -a "$STAGE_IN/." "$T/"
STRIP=$(command -v aarch64-linux-gnu-strip || command -v llvm-strip || true)
for p in "$T"/bin/*; do
  if [ -n "$STRIP" ] && [ "$(head -c 4 "$p")" = "$(printf '\177ELF')" ]; then "$STRIP" --strip-unneeded "$p"; fi
done
[ -n "$STRIP" ] || echo "warning: no aarch64 strip found; the programs are packaged as they are" >&2
cp "$HERE/install.sh" "$HERE/uninstall.sh" "$HERE/steam-entry.py" "$T/"
mkdir -p "$T/art"
for f in portrait wide hero logo icon icon-64 icon-128 icon-256; do cp "$ART/$f.png" "$T/art/"; done
chmod +x "$T/install.sh" "$T/uninstall.sh" "$T"/bin/*
echo "$VERSION" > "$T/VERSION"
cp "$REPO/COPYING" "$T/"
mkdir -p "$T/LICENSES"
cp "$REPO"/LICENSES/* "$REPO"/Source/Core/VideoCommon/VR/FrameUI/licenses/* "$T/LICENSES/"
cp "$REPO/Externals/licenses.md" "$T/LICENSES/Externals-licenses.md"

NAME=baijivr-$VERSION-linux-arm64.tar.gz
ZIP=baijivr-$VERSION-framedrop-linux-arm64.zip
(cd "$T" && tar --owner=0 --group=0 --numeric-owner --sort=name -cf - $(ls -A)) | gzip -n > "$OUT/$NAME"
python3 -I "$HERE/make-framedrop-zip.py" "$OUT/$NAME" --version "$VERSION" --out "$OUT" > /dev/null
(cd "$OUT" && sha256sum "$NAME" "$ZIP" baijivr.framedrop.json | tee SHA256SUMS)
echo "wrote $OUT/$NAME, $OUT/$ZIP, $OUT/baijivr.framedrop.json and $OUT/SHA256SUMS"
