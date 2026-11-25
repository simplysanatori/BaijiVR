#!/usr/bin/env python3
# Copyright 2026 Dolphin Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes Data/Sys/covers.txt: the names of libretro-thumbnails' pictures by game ID.

Each line is "ID KINDS Name": KINDS has B (box), T (title screen), S (screenshot) and L (logo) for
the folders that have a picture of this name. Run from the root of the repository:

    python3 Tools/make-cover-names.py
"""

import json
import os
import re
import sys
import unicodedata
import urllib.request

KINDS = {"Named_Boxarts": "B", "Named_Titles": "T", "Named_Snaps": "S", "Named_Logos": "L"}
SYSTEMS = {"Nintendo_-_Wii": "RS", "Nintendo_-_GameCube": "GDPU"}
# The fourth letter of an ID, and the regions in a picture's name, best first.
REGIONS = {
    "E": ["USA"],
    "N": ["USA"],
    "J": ["Japan"],
    "K": ["Korea", "USA"],
    "Q": ["Korea", "Japan"],
    "T": ["Korea", "USA"],
    "W": ["Taiwan", "Asia", "Japan"],
    "D": ["Germany", "Europe"],
    "F": ["France", "Europe"],
    "I": ["Italy", "Europe"],
    "S": ["Spain", "Europe"],
    "H": ["Netherlands", "Europe"],
    "R": ["Russia", "Europe"],
    "U": ["Australia", "Europe"],
}
PAL = ["Europe", "Australia", "United Kingdom"]
ANY = ["World", "USA", "Europe", "Australia", "Japan", "Korea"]
SKIP = re.compile(r"\((Beta|Demo|Proto|Sample|DLC|Kiosk|Debug|Bonus|Alt|Unl|Aftermarket)", re.I)


def get(repo, path):
    url = f"https://api.github.com/repos/libretro-thumbnails/{repo}/git/trees/{path}"
    request = urllib.request.Request(url, headers={"User-Agent": "make-cover-names"})
    with urllib.request.urlopen(request, timeout=60) as response:
        tree = json.load(response)
    if tree["truncated"]:
        sys.exit(f"{repo}: the list is not whole")
    return tree["tree"]


def key(title):
    """What is left of a title when its spelling does not count."""
    title = unicodedata.normalize("NFKD", title).encode("ascii", "ignore").decode().lower()
    title = re.sub(r"\b(the|a|an)\b", "", title).replace("&", "").replace(" and ", " ")
    return re.sub(r"[^a-z0-9]", "", title)


def tags(name):
    return re.findall(r"\(([^)]*)\)", name)


def rank(name, regions):
    """Smaller is better; None is no picture of this game."""
    found = tags(name)
    places = found[0].split(", ") if found else []
    for at, region in enumerate(regions):
        if region in places:
            break
    else:
        return None
    languages = next((t for t in found[1:] if re.fullmatch(r"[A-Z][a-z](,[A-Z][a-z])*", t)), "En")
    later_disc = re.search(r"\(Disc [2-9]", name) is not None
    return (at, "En" not in languages.split(","), later_disc, "(Rev " in name, len(found), name)


def main():
    titles = {}
    with open("Data/Sys/wiitdb-en.txt", encoding="utf-8") as file:
        for line in file:
            match = re.match(r"(\w{6}) = (.+)", line.strip())
            if match:
                titles[match[1]] = match[2]

    lines = []
    for repo, letters in SYSTEMS.items():
        kinds = {}
        for entry in get(repo, "master"):
            if entry["path"] in KINDS:
                for picture in get(repo, entry["sha"]):
                    name = picture["path"].removesuffix(".png")
                    if name != picture["path"] and not SKIP.search(name):
                        kinds.setdefault(name, set()).add(KINDS[entry["path"]])
        by_key = {}
        for name in kinds:
            by_key.setdefault(key(name.split(" (")[0]), []).append(name)

        for game_id, title in titles.items():
            if game_id[0] not in letters or not game_id.isalnum():
                continue
            names = by_key.get(key(title), [])
            regions = REGIONS.get(game_id[3], PAL) + ANY
            best = {}
            for name in names:
                order = rank(name, regions)
                if order is None:
                    continue
                for kind in kinds[name]:
                    if kind not in best or order < best[kind][0]:
                        best[kind] = (order, name)
            chosen = {}
            for kind, (_, name) in best.items():
                chosen.setdefault(name, []).append(kind)
            for name, of in chosen.items():
                lines.append(f"{game_id} {''.join(k for k in 'BTSL' if k in of)} {name}\n")

    with open("Data/Sys/covers.txt", "w", encoding="utf-8", newline="\n") as file:
        file.writelines(sorted(lines))
    print(f"{len(lines)} lines, {os.path.getsize('Data/Sys/covers.txt')} bytes")


if __name__ == "__main__":
    main()
