#!/usr/bin/env python3
# Copyright 2026 Dolphin Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Checks the Steam Frame profiles: every input is one the OpenXR device has, and no input does two
things on one controller without being in SHARED. Needs no build."""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEVICE = ROOT / "Source/Core/InputCommon/ControllerInterface/OpenXR/OpenXR.cpp"
PROFILES = ROOT / "Data/Sys/Profiles"

# On purpose.
SHARED = [
    {"Shake/X", "Shake/Y", "Shake/Z"},
]


def get_names():
    text = DEVICE.read_text()
    names = set(re.findall(r'return "([^"]+)"', text))
    # The other ones are of a hand. Not those that the mapping windows do not list.
    for name in re.findall(r'" ([A-Z][^"]+)"', text):
        if not name.startswith(("Raw ", "System", "Grip Pressure")):
            names.update(f"{hand} {name}" for hand in ("Left", "Right"))
    return names


def check(path, names):
    errors = []
    uses = {}
    lines = path.read_text().splitlines()
    for line in lines:
        if "=" not in line or line.startswith("#"):
            continue
        key, value = (part.strip() for part in line.split("=", 1))
        if key == "Rumble/Motor":
            if value.strip("`") not in names:
                errors.append(f"{key}: no output {value}")
            continue
        if "`" not in value:
            continue
        for part in value.split("|"):
            match = re.fullmatch(r"\s*`([^`]+)`\s*", part)
            if match is None:
                errors.append(f"{key}: cannot be changed in the VR menu: {value}")
                continue
            uses.setdefault(match[1], set()).add(key)
            if match[1] not in names:
                errors.append(f"{key}: no input {match[1]}")
    for name, keys in uses.items():
        if len(keys) > 1 and not any(keys <= shared for shared in SHARED):
            errors.append(f"{name}: {', '.join(sorted(keys))}")
    if "Device = OpenXR/0/OpenXR Controller" not in lines:
        errors.append("another device")
    return errors


def main():
    names = get_names()
    failed = False
    paths = sorted(PROFILES.glob("GCPad/Steam Frame*.ini"))
    paths += sorted(PROFILES.glob("Wiimote/Steam Frame*.ini"))
    for path in paths:
        errors = check(path, names)
        print(f"{path.relative_to(ROOT)}: {'; '.join(errors) if errors else 'ok'}")
        failed |= bool(errors)
    return 1 if failed or len(paths) < 5 else 0


if __name__ == "__main__":
    sys.exit(main())
