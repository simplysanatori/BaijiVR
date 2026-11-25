#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Removes Baiji VR (what Frametanium runs): baiji-uninstall --yes, from the folder this script is in,
# so it works when ~/baiji is already gone.  Your saves and settings stay; extra arguments such as
# --purge or --migrate go to baiji-uninstall.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
exec bash "$HERE/bin/baiji-uninstall" --yes "$@"
