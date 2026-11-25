#!/usr/bin/env python3
"""Checks that the VR menu's settings and the Qt settings pages are two views of one store.

For every row of the VR menu (VideoCommon/VR/FrameUI/MenuSettings.cpp) it finds the Qt control of
the same config key (DolphinQt/Settings/VRPane.cpp, or Dolphin's own Graphics pages) and compares
the name, the choices and their order, a slider's range and step, and the displays it is for. It
prints the table (--table) and what is in only one of the two, and fails on a difference.

Usage: Tools/check-vr-menu.py [--table]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE = os.path.join(ROOT, "Source", "Core")
MENU = os.path.join(CORE, "VideoCommon", "VR", "FrameUI", "MenuSettings.cpp")
PANE = os.path.join(CORE, "DolphinQt", "Settings", "VRPane.cpp")
QT_CONFIG = os.path.join(CORE, "DolphinQt", "Config")

# Rows the VR menu has that Qt shows in another way, or the other way round, and why.
NOTES = {
    "GFX_VR_SCREEN_CURVE": "a slider with the same seven stops; Qt has a list",
    "MAIN_AUTOSAVE_MINUTES": "a slider with the same five stops; Qt has a list",
    "GFX_VR_PRESENTATION_MODE": "Flat is Switch to Flat on the menu's Main tab",
    "GFX_EFB_SCALE": "Auto and 1x to 4x of Dolphin's list; a higher one shows as it is",
    "GFX_ASPECT_RATIO": "Auto, 16:9 and 4:3 of Dolphin's list",
    "GFX_MSAA": "the MSAA modes of Dolphin's list that the GPU has; no SSAA",
}
# Config keys of Dolphin's own pages: where they are in Qt.
DOLPHIN_PAGES = {
    "GFX_ASPECT_RATIO": "Graphics, General",
    "GFX_EFB_SCALE": "Graphics, Enhancements",
    "GFX_ENHANCE_FORCE_TEXTURE_FILTERING": "Graphics, Enhancements",
    "GFX_ENHANCE_MAX_ANISOTROPY": "Graphics, Enhancements",
    "GFX_MSAA": "Graphics, Enhancements",
    "GFX_SHADER_COMPILATION_MODE": "Graphics, General",
    "GFX_WIDESCREEN_HACK": "Graphics, Enhancements",
    "GFX_HACK_EFB_ACCESS_ENABLE": "Graphics, Hacks",
    "GFX_HACK_SKIP_EFB_COPY_TO_RAM": "Graphics, Hacks",
    "GFX_HACK_SKIP_XFB_COPY_TO_RAM": "Graphics, Hacks",
}
# Qt pages that are per game too (a game's Properties, VR): VRSettingsPage's own groups.
PER_GAME_GROUPS = {"AddDisplay", "AddScreen", "AddImmersive", "AddHeadset", "AddAdvanced"}
QT_DISPLAYS = {
    "ALL": "Any", "HEADSET": "Any", "TWO_EYES": "Any", "Screen3D": "Screen",
    "Immersive": "Immersive", "screen": "Screen",
}


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def call_end(text, start):
    """The index after the parenthesis that closes the one at or after `start`."""
    depth = 0
    i = text.index("(", start)
    in_string = False
    while i < len(text):
        c = text[i]
        if in_string:
            if c == "\\":
                i += 1
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced call")


def strings(text):
    return [s.encode().decode("unicode_escape").encode("latin-1").decode("utf-8")
            if "\\u" in s else s for s in re.findall(r'"((?:[^"\\]|\\.)*)"', text)]


def joined_literal(text):
    """Adjacent string literals are one string."""
    return re.sub(r'"\s*\n\s*"', "", text)


def norm(token):
    token = token.strip().replace("ToNumber(", "").rstrip(")")
    token = token.replace("Config::", "").replace("0.0f", "0").replace(".0f", "")
    token = {"true": "1", "false": "0"}.get(token, token)
    return token.rstrip("f") if re.fullmatch(r"[0-9.]+f", token) else token


def parse_menu():
    text = joined_literal(read(MENU))
    keys = {}
    for kind, sid, key in re.findall(r"(Bind|BindInverted)\(SettingId::(\w+), Config::(\w+)\)", text):
        keys[sid] = key
    keys["Display"] = "GFX_VR_PRESENTATION_MODE"

    rows = {}
    section = None
    display_alias = {}
    for m in re.finditer(r"constexpr RowDisplay (\w+) = RowDisplay::(\w+);", text):
        display_alias[m.group(1)] = m.group(2)
    # PASSTHROUGH_DISPLAY: the Linux one.
    display_alias["PASSTHROUGH_DISPLAY"] = "Screen"

    starts = [(m.start(), m.group(1), m.group(2)) for m in
              re.finditer(r"\b(MakeRow|MakeChoice|MakeSlider)\((\w+), SECTION", text)]
    for at, maker, sid in starts:
        end = call_end(text, at)
        body = text[at:end]
        section = re.findall(r"constexpr MenuSection SECTION = MenuSection::(\w+);", text[:at])[-1]
        # The name: a literal, or a constant.
        name = re.match(r"\w+\(\w+, SECTION,(?: RowKind::\w+,)?\s*(\w+|\"(?:[^\"\\]|\\.)*\")", body).group(1)
        if not name.startswith('"'):
            name = re.search(r'constexpr const char\* %s = ("[^"]*");' % name, text).group(1)
        row = {"id": sid, "maker": maker, "section": section, "label": name[1:-1],
               "store": "Global", "display": "Any", "choices": [], "range": None}
        if maker == "MakeChoice":
            row["choices"] = [(s, norm(v)) for s, v in
                              re.findall(r'\{"((?:[^"\\]|\\.)*)", ([^{}]+?)\}', body)]
        if maker == "MakeSlider":
            r = re.search(r"\{([^{}\"]*)\}", body)
            if r and r.group(1).strip():
                row["range"] = [norm(t) for t in r.group(1).split(",")]
        # The With(...) around it, or the one that takes the variable it was put in.
        before = text[:at].rstrip()
        with_at = None
        if before.endswith("With("):
            with_at = before.rindex("With(")
        else:
            var = re.search(r"SettingRow (\w+) =\s*$", before)
            if var:
                moved = re.search(r"With\(std::move\(%s\)" % var.group(1), text)
                with_at = moved.start() if moved else None
        if with_at is not None:
            tail = text[with_at:call_end(text, with_at)]
            tail = tail[len(body):] if tail.startswith("With(" + body) else tail
            tail = tail.replace(body, "")
            store = re.search(r"SettingStore::(\w+)", tail)
            if store:
                row["store"] = store.group(1)
            display = re.search(r"RowDisplay::(\w+)|\b(DISPLAY|PASSTHROUGH_DISPLAY)\b", tail)
            if display:
                row["display"] = display.group(1) or display_alias[display.group(2)]
        rows[sid] = row

    # Stops that are filled in loops.
    for sid, values in (("ScreenCurve", re.search(r"SCREEN_CURVES\{([^}]*)\}", text).group(1)),
                        ("AutosaveEvery", re.search(r"minutes : \{([^}]*)\}", text).group(1))):
        rows[sid]["stops"] = [v.strip() for v in values.split(",")]
    # The switch of a slider has its store and display.
    for on, slider in re.findall(r"AddOptionalSlider\(\s*&rows, (\w+),\s*With\(MakeSlider\((\w+),", text):
        rows[on] = dict(rows[slider], id=on, maker="MakeRow", range=None, switch_of=slider)
    # The Immersive World Scale is put in a variable with its With().
    for row in rows.values():
        row["key"] = keys.get(row["id"])
    return rows


def parse_pane():
    text = joined_literal(read(PANE))
    labels = {}
    for key, label in re.findall(
            r'\{Config::(\w+)\.GetLocation\(\),\s*\{tr\("((?:[^"\\]|\\.)*)"\)', text):
        labels[key] = label
    controls = {}
    functions = [(m.start(), m.group(2)) for m in
                 re.finditer(r"\n\w[\w:<>\* ]*?(VRSettingsPage|VRPane)::(\w+)\([^;{]*\)\s*\{", text)]
    def function_at(i):
        name = None
        at = 0
        for start, n in functions:
            if start <= i:
                name, at = n, start
        return name, at
    for m in re.finditer(r"\b(AddBool|AddSlider|AddChoice<[\w:]+>)\(", text):
        end = call_end(text, m.start())
        body = text[m.start():end]
        args = body[body.index("(") + 1:]
        key = re.search(r"Config::(\w+)", args)
        if not key or "const Config::Info" in body:
            continue
        display = args.split(",")[1].strip().replace("VRSettingsPage::", "")
        group, group_at = function_at(m.start())
        # What its group is for, where it is for all of that.
        layout = args.split(",")[0].strip()
        of_group = re.search(r"%s =\s*(?:m_page->)?AddGroup\(tr\(\"[^\"]*\"\), (?:VRSettingsPage::)?(\w+)" % layout,
                             text[group_at:m.start()])
        if display == "ALL" and of_group:
            display = of_group.group(1)
        # Not on a game's own page.
        since = text[group_at:m.start()].rpartition("if (!m_layer)")
        if since[1] and ("}" not in since[2] if re.sub(r"^\s*//[^\n]*", "", since[2]).lstrip().startswith("{") else ";" not in since[2]):
            group += " (not per game)"
        control = {"kind": m.group(1).split("<")[0], "display": display,
                   "group": group, "choices": [], "range": None}
        if control["kind"] == "AddChoice":
            control["choices"] = [(s, norm(v)) for s, v in
                                  re.findall(r'\{tr\("((?:[^"\\]|\\.)*)"\), ([^{}]+?)\}', body)]
        if control["kind"] == "AddSlider":
            parts = [p.strip() for p in args.split(",")]
            control["range"] = [norm(p) for p in parts[3:6]]
            enable = re.search(r"&Config::(\w+)\)\s*;?\s*$", body)
            if enable:
                controls[enable.group(1)] = dict(control, kind="AddBool", range=None)
        # Passthrough is there once for each system: the last one is Linux's.
        controls[key.group(1)] = control
    display = re.search(r"ConfigChoiceMap<OpenXRPresentationMode>\(\s*\{(.*?)\},\s*Config::", text, re.S)
    controls["GFX_VR_PRESENTATION_MODE"] = {
        "kind": "AddChoice", "display": "ALL", "group": "AddDisplay", "range": None,
        "choices": [(s, norm(v)) for s, v in
                    re.findall(r'\{tr\("((?:[^"\\]|\\.)*)"\), ([^{}]+?)\}', display.group(1))]}
    minutes = re.search(r"for \(const u32 m : \{([^}]*)\}\)", text)
    controls["MAIN_AUTOSAVE_MINUTES"] = {
        "kind": "AddChoice", "display": "ALL", "group": "AddAutosave", "range": None,
        "stops": [v.strip().rstrip("u") for v in minutes.group(1).split(",")], "choices": []}
    keep = re.search(r"ConfigSliderU32\((\d+), (\d+), Config::MAIN_AUTOSAVE_KEEP\)", text)
    controls["MAIN_AUTOSAVE_KEEP"] = {"kind": "AddSlider", "display": "ALL", "group": "AddAutosave",
                                      "range": [keep.group(1), keep.group(2), "1"], "choices": []}
    return labels, controls


def qt_uses(key):
    for folder, _, files in os.walk(QT_CONFIG):
        for name in files:
            if name.endswith(".cpp") and ("Config::" + key) in read(os.path.join(folder, name)):
                return True
    return False


def main():
    menu = parse_menu()
    labels, pane = parse_pane()
    problems = []
    table = []
    for row in menu.values():
        key = row["key"]
        if not key:
            problems.append("%s has no config key" % row["id"])
            continue
        qt = pane.get(key)
        where = scope = ""
        if qt:
            per_game = qt["group"] in PER_GAME_GROUPS
            where = "VR page, %s" % qt["group"].replace("Add", "")
            label = labels.get(key)
            if label != row["label"] and not row.get("switch_of"):
                problems.append("%s: called '%s' in the menu and '%s' in Qt" % (key, row["label"], label))
            want = QT_DISPLAYS.get(qt["display"], "?")
            if want != row["display"]:
                problems.append("%s: for %s in the menu and %s (%s) in Qt" %
                                (key, row["display"], want, qt["display"]))
            menu_values = [v for _, v in row["choices"]] or row.get("stops") or []
            qt_values = [v for _, v in qt["choices"]] or qt.get("stops") or []
            if key == "GFX_VR_PRESENTATION_MODE":
                qt_values = qt_values[:2]
            if menu_values or qt_values:
                if menu_values != qt_values:
                    problems.append("%s: choices %s in the menu and %s in Qt" % (key, menu_values, qt_values))
                names = [s for s, _ in row["choices"]]
                qt_names = [s for s, _ in qt["choices"]][:len(names)]
                if names and qt_names and names != qt_names:
                    problems.append("%s: choices are called %s in the menu and %s in Qt" % (key, names, qt_names))
            elif row["range"] or qt["range"]:
                if row["range"] != qt["range"]:
                    problems.append("%s: range and step %s in the menu and %s in Qt" %
                                    (key, row["range"], qt["range"]))
            # A game's own value: the VR file where Qt's per-game VR page has the row.
            is_vr_key = key.startswith("GFX_VR_")
            if per_game and row["store"] != "VRFile":
                problems.append("%s: per game in Qt's VR page, but %s in the menu" % (key, row["store"]))
            if not per_game and row["store"] == "VRFile":
                problems.append("%s: per game in the menu only (%s is not on a game's VR page)" %
                                (key, qt["group"]))
            if not is_vr_key and row["store"] == "VRFile":
                problems.append("%s: not a VR key, but in the VR file" % key)
            scope = {"VRFile": "Global, or GameSettingsVR/<ID>.ini", "Global": "Global only",
                     "LocalGame": "Global, or GameSettings/<ID>.ini"}[row["store"]]
        elif key in DOLPHIN_PAGES:
            where = DOLPHIN_PAGES[key]
            if not qt_uses(key):
                problems.append("%s: no Qt page uses it" % key)
            if row["store"] != "LocalGame":
                problems.append("%s: Dolphin keeps a game's value in GameSettings, the menu in %s" %
                                (key, row["store"]))
            scope = "Global, or GameSettings/<ID>.ini"
        else:
            problems.append("%s (%s): not on a Qt page" % (key, row["label"]))
        table.append((row["label"], key, where, "%s" % row["section"], scope, NOTES.get(key, "")))

    menu_keys = {row["key"] for row in menu.values()}
    only_qt = sorted(k for k in pane if k not in menu_keys)
    if "--table" in sys.argv:
        print("| Row | Key | Qt | VR menu | Scope | Note |")
        print("|---|---|---|---|---|---|")
        for line in table:
            print("| " + " | ".join(line) + " |")
        print()
        print("Only on the Qt VR page: " + ", ".join("%s (%s)" % (labels.get(k, k), k) for k in only_qt))
    for problem in problems:
        print("DIFFERENT: " + problem)
    print("%d rows, %d only in Qt, %d differences" % (len(table), len(only_qt), len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
