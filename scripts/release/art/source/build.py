#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 simplysanatori
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws the artwork of Baiji VR: the SVGs beside this file, and from them the Steam library art
(scripts/release/art), the README's cover and the app's own pictures.

Needs rsvg-convert, and Pillow for the cover (a JPEG)."""

import io
import pathlib
import subprocess

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
ART = HERE.parent
REPO = ART.parents[2]

INK = "#0d2530"
WHITE = "#fdffff"

# The dolphin, in a box of 512: body with beak and flukes, back fin, flipper, mouth.
BODY = (
    "M126 228C134 286 166 326 210 324C262 318 284 264 300 214C312 168 336 120 388 109"
    "C408 105 420 98 429 86C436 77 441 72 448 61C458 47 481 55 477 75C474 100 470 127 463 154"
    "C458 174 456 194 450 214C424 290 372 372 296 408C208 446 106 380 86 252"
    "C82 222 64 208 44 196C28 186 34 168 52 166C78 164 98 170 112 182C118 162 136 146 160 140"
    "C178 136 188 150 180 166C166 190 124 202 126 228Z"
)
FIN = "M322 180C298 212 268 228 240 236C229 240 227 248 231 256C241 272 246 290 264 308L310 300Z"
FLIPPER = "M440 236C464 268 474 304 469 344C466 362 450 364 438 354C420 336 408 320 370 310Z"
MOUTH = "M464 80C452 114 434 138 404 149"
# Its extent, with the rounding stroke.
X0, Y0, X1, Y1 = 34, 51, 480, 420

# The letters: one stroke each, on a baseline at 0 with capitals of 100.
GLYPHS = {
    "B": (54, "M0 0V-100H27A24 24 0 0 1 27 -52H0M27 -52A26 26 0 0 1 27 0H0"),
    "a": (58, "M58 -60V0M58 -30A29 30 0 1 0 0 -30A29 30 0 1 0 58 -30"),
    "i": (8, "M4 -60V0"),
    "j": (8, "M4 -60V8Q4 29 -15 29"),
    "V": (76, "M0 -100L38 0L76 -100"),
    "R": (56, "M0 0V-100H29A26 26 0 0 1 29 -48H0M29 -48L56 0"),
}
STROKE = 18
GAP = 36
SPACE = 58


def dolphin(cx, cy, width, fill=WHITE, details=True, opacity=1):
    k = width / (X1 - X0)
    x, y = cx - k * (X0 + X1) / 2, cy - k * (Y0 + Y1) / 2
    s = "".join(f'<path d="{d}"/>' for d in (FIN, FLIPPER, BODY))
    s = f'<g fill="{fill}" stroke="{fill}" stroke-width="4" stroke-linejoin="round">{s}</g>'
    if details:
        s += (
            f'<path d="{MOUTH}" fill="none" stroke="#1b8fb2" stroke-width="9" stroke-linecap="round"/>'
            f'<circle cx="372" cy="172" r="14" fill="{INK}"/><circle cx="376.6" cy="167" r="4.6" fill="#fff"/>'
        )
    return f'<g transform="translate({x:.2f} {y:.2f}) scale({k:.4f})" opacity="{opacity}">{s}</g>'


def lettering(x, baseline, width):
    """The name, from x to x + width."""
    pen, glyphs = 0, []
    for c in "Baiji VR":
        if c == " ":
            pen += SPACE - GAP
            continue
        w, d = GLYPHS[c]
        glyphs.append((c, pen, d))
        pen += w + GAP
    total = pen - GAP + STROKE
    k = width / total

    s = ""
    for c, at, d in glyphs:
        s += f'<path transform="translate({at})" d="{d}"/>'
        if c in "ij":
            s += f'<circle cx="{at + 4}" cy="-95" r="12" fill="{WHITE}" stroke="none"/>'
    return (
        f'<g transform="translate({x + k * STROKE / 2:.2f} {baseline:.2f}) scale({k:.4f})" fill="none" stroke="{WHITE}" '
        f'stroke-width="{STROKE}" stroke-linecap="round" stroke-linejoin="round">{s}</g>'
    )


def rings(cx, cy, r):
    return "".join(
        f'<circle cx="{cx}" cy="{cy}" r="{r * q:.1f}" fill="#fff" opacity=".07"/>' for q in (1, 0.74, 0.45)
    )


def svg(w, h, body, background=True, radius=0):
    s = (
        "<!-- SPDX-License-Identifier: GPL-3.0-or-later. Drawn by build.py. -->\n"
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">'
    )
    if background:
        s += (
            '<defs><linearGradient id="bg" x2="0" y2="1"><stop stop-color="#55cbe1"/>'
            '<stop offset=".5" stop-color="#23a0c4"/><stop offset="1" stop-color="#1184ad"/></linearGradient>'
            f'<clipPath id="c"><rect width="{w}" height="{h}" rx="{radius}"/></clipPath></defs>'
            f'<g clip-path="url(#c)"><rect width="{w}" height="{h}" fill="url(#bg)"/>{body}</g>'
        )
    else:
        s += body
    return s + "</svg>\n"


def lockup(x, cy, width):
    """The dolphin and the name beside it, from x to x + width."""
    d = 0.355 * width
    text = 0.575 * width
    cap = 100 * text / 486
    return dolphin(x + d / 2, cy, d) + lettering(x + width - text, cy + cap * 0.53, text)


def pieces():
    return {
        "portrait": svg(
            600, 900,
            rings(60, 110, 170) + rings(585, 520, 210) + rings(70, 880, 230)
            + dolphin(300, 342, 496) + lettering(70, 738, 460),
        ),
        "wide": svg(
            920, 430,
            rings(40, 70, 170) + rings(880, 400, 210) + rings(470, 20, 120) + lockup(95, 215, 730),
        ),
        "hero": svg(
            1920, 620,
            rings(120, 60, 260) + rings(980, 640, 300) + rings(1820, 90, 230)
            + dolphin(1400, 315, 560, details=False, opacity=0.22),
        ),
        "cover": svg(
            1280, 640,
            rings(60, 100, 250) + rings(1220, 590, 310) + rings(660, 20, 180) + lockup(120, 320, 1040),
        ),
        "logo": svg(1400, 440, lockup(8, 220, 1384), background=False),
        "icon": svg(512, 512, dolphin(256, 258, 392)),
        "app": svg(512, 512, dolphin(256, 258, 392), radius=104),
    }


def render(name, width):
    r = subprocess.run(
        ["rsvg-convert", "-w", str(width), str(HERE / f"{name}.svg")], check=True, capture_output=True
    )
    return Image.open(io.BytesIO(r.stdout))


def main():
    for name, text in pieces().items():
        (HERE / f"{name}.svg").write_text(text)
    for name, width in (("portrait", 600), ("wide", 920), ("hero", 1920), ("icon", 512)):
        render(name, width).convert("RGB").save(ART / f"{name}.png", optimize=True)
    render("logo", 1400).save(ART / "logo.png", optimize=True)
    for n in (256, 128, 64):
        render("icon", n).convert("RGB").save(ART / f"icon-{n}.png", optimize=True)
    render("cover", 1280).convert("RGB").save(REPO / "docs/images/cover.jpg", quality=92, optimize=True)
    for suffix, n in (("", 200), ("@2x", 400), ("@4x", 800)):
        render("app", n).save(REPO / f"Data/Sys/Resources/baiji_logo{suffix}.png", optimize=True)
    render("app", 256).save(REPO / "Data/baiji.png", optimize=True)


if __name__ == "__main__":
    main()
