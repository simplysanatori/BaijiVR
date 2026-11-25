// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoCommon/VR/FrameUI/games_tab.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "VideoCommon/VR/FrameUI/md3_fonts.h"
#include "VideoCommon/VR/FrameUI/ui_ttf.h"

namespace FrameXr::Menu {

namespace Sym = ::FrameXr::Menu::Icon;

namespace {

constexpr Type kTitle = Type::RailLabel; ///< 14, Medium

// In a tile
constexpr float kTileRadius = 16.0f;
constexpr float kArtX = 8.0f, kArtY = 8.0f, kArtW = 144.0f, kArtH = 104.0f, kArtRadius = 10.0f;
constexpr float kBannerInset = 12.0f, kBannerRadius = 6.0f;
constexpr float kMarkAt = 24.0f;       ///< the star's circle: from the left and the top
constexpr float kMarkR = 13.0f, kStar = 18.0f;
constexpr float kOptionsR = 14.0f;
constexpr float kMarkInk = 0.86f;      ///< the circles' opacity
constexpr float kTitleBase = 132.0f, kSmallBase = 190.0f, kSmall = 13.0f;
constexpr const char* kTitleCut = " \u2026";

/// The first letters of a title's words, three at most.
std::string Initials(const std::string& title) {
    std::string out;
    int letters = 0;
    bool word = true;
    for (std::size_t i = 0; i < title.size() && letters < 3;) {
        const char32_t cp = frame_xr::ui::NextUtf8(title, i);
        const bool letter = (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') ||
                            (cp >= 'a' && cp <= 'z') || cp >= 0xC0;
        if (letter && word) {
            out += frame_xr::ui::Utf8Of(cp >= 'a' && cp <= 'z' ? cp - 32 : cp);
            letters++;
        }
        word = !letter && cp != '\'';
    }
    return out;
}

/// The art area: the cover over it (cover_art.h), else the banner in its middle, else the title's
/// initials.
void DrawArt(Kit& kit, const GameEntry& g, const GameIcon* cover, const Rect& box, float opacity) {
    kit.Fill(box, kArtRadius, Tok::kSurfaceContainer);
    const auto drawable = [](const GameIcon* i) {
        return i && i->w > 0 && i->h > 0 &&
               i->rgba.size() >=
                   static_cast<std::size_t>(i->w) * static_cast<std::size_t>(i->h) * 4;
    };
    if (drawable(cover)) {
        kit.ImageFit(cover->rgba.data(), cover->w, cover->h, box, kArtRadius, opacity);
        return;
    }
    if (drawable(g.icon.get())) {
        const Rect banner{box.x0 + kBannerInset, box.y0 + kBannerInset, box.x1 - kBannerInset,
                          box.y1 - kBannerInset};
        kit.ImageFit(g.icon->rgba.data(), g.icon->w, g.icon->h, banner, kBannerRadius, opacity);
        return;
    }
    constexpr float kSize = 18.0f;
    kit.TextSized(box.Cx(), box.Cy() + kit.CapHeightSized(kSize, true) * 0.5f, kSize, true,
                  Tok::kOnSurfaceVariant, Initials(g.title), 1, opacity);
}

/// The star's and Options' dark circle.
void MarkCircle(Kit& kit, float cx, float cy, float r) {
    kit.Dot(cx, cy, r, Tok::kSurface, kMarkInk);
}

} // namespace

std::string GameCount(std::size_t n, bool searching) {
    const std::string one = searching ? " Match" : " Game", many = searching ? " Matches" : " Games";
    return std::to_string(n) + (n == 1 ? one : many);
}

std::string GameTooltip(const GameEntry& g, bool running) {
    std::string out = running ? "Continue" : "";
    if (g.favorite) {
        out += out.empty() ? "In Favourites" : " \u00b7 In Favourites";
    }
    return out;
}

std::vector<std::string> GameTitleWrapped(const Kit& kit, const std::string& title, float width) {
    const auto fits = [&kit, width](const std::string& s) {
        return kit.TextWidth(kTitle, s) <= width;
    };
    std::vector<std::string> lines;
    std::string line;
    for (std::size_t at = 0; at < title.size();) {
        std::size_t end = std::min(title.find(' ', at), title.size());
        if (title.compare(end, 3, " - ") == 0) {
            end += 2; // a dash stays with the word before it
        }
        std::string word = title.substr(at, end - at);
        at = end + 1;
        if (word.empty()) {
            continue;
        }
        if (!line.empty() && fits(line + " " + word)) {
            line += " " + word;
            continue;
        }
        if (!line.empty()) {
            lines.push_back(std::exchange(line, {}));
        }
        while (!fits(word)) { // longer than a line: as much of it as fits, at least a character
            std::size_t cut = 0;
            for (std::size_t i = 0; i < word.size();) {
                frame_xr::ui::NextUtf8(word, i);
                if (cut > 0 && !fits(word.substr(0, i))) {
                    break;
                }
                cut = i;
            }
            if (cut >= word.size()) {
                break;
            }
            lines.push_back(word.substr(0, cut));
            word.erase(0, cut);
        }
        line = word;
    }
    if (!line.empty() || lines.empty()) {
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> GameTitleLines(const Kit& kit, const std::string& title, float width,
                                        bool* cut) {
    std::vector<std::string> lines = GameTitleWrapped(kit, title, width);
    const bool more = lines.size() > kGameTitleLines;
    if (more) {
        lines.resize(kGameTitleLines);
        std::string& last = lines.back();
        while (kit.TextWidth(kTitle, last + kTitleCut) > width && last.find(' ') != std::string::npos) {
            last.erase(last.rfind(' '));
        }
        last += kTitleCut;
        last = kit.Fit(kTitle, last, width); // one long word: the dots alone do not fit after it
    }
    if (cut) {
        *cut = more;
    }
    return lines;
}

float GamesRestScroll(float scroll, float max_scroll) {
    return std::clamp(std::round(scroll / kGameRow) * kGameRow, 0.0f, std::max(0.0f, max_scroll));
}

float TitleScroll(float t, float overflow) {
    constexpr float kWait = 1.2f, kSpeed = 14.0f; // s, dp a second
    if (overflow <= 0.0f) {
        return 0.0f;
    }
    const float move = overflow / kSpeed;
    const float at = std::fmod(std::max(0.0f, t), kWait + move + kWait);
    return std::round(std::clamp((at - kWait) * kSpeed, 0.0f, overflow));
}

int GameAt(const GamesList& list, const std::string& path) {
    for (std::size_t i = 0; list && i < list->games.size(); i++) {
        if (list->games[i].path == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DrawGamesIcon(Kit& kit, float cx, float cy, bool active) {
    for (const float dx : {-5.0f, 5.0f}) {
        for (const float dy : {-5.0f, 5.0f}) {
            const Rect r{cx + dx - 4.0f, cy + dy - 4.0f, cx + dx + 4.0f, cy + dy + 4.0f};
            if (active) {
                kit.Fill(r, 1.5f, Tok::kOnSecondaryContainer);
            } else {
                kit.Stroke(r, 1.5f, 2.0f, Tok::kOnSurfaceVariant);
            }
        }
    }
}

void DrawSearchGlyph(Kit& kit, float cx, float cy, uint32_t rgb, float size) {
    const float u = size / 24.0f;
    const float x = cx - 2.5f * u, y = cy - 2.5f * u, r = 6.5f * u;
    kit.Stroke({x - r, y - r, x + r, y + r}, r, 2.0f * u, rgb);
    kit.Line(x + 4.8f * u, y + 4.8f * u, cx + 8.0f * u, cy + 8.0f * u, 2.0f * u, rgb);
}

void DrawSortGlyph(Kit& kit, float cx, float cy, uint32_t rgb) {
    kit.Line(cx - 8.0f, cy - 5.0f, cx + 8.0f, cy - 5.0f, 2.0f, rgb);
    kit.Line(cx - 8.0f, cy, cx + 3.0f, cy, 2.0f, rgb);
    kit.Line(cx - 8.0f, cy + 5.0f, cx - 3.0f, cy + 5.0f, 2.0f, rgb);
}

void DrawAddFolderGlyph(Kit& kit, float cx, float cy, uint32_t rgb) {
    kit.Stroke({cx - 10.0f, cy - 6.0f, cx + 10.0f, cy + 8.0f}, 2.0f, 2.0f, rgb);
    kit.Fill({cx - 10.0f, cy - 8.0f, cx - 1.0f, cy - 4.0f}, 2.0f, rgb); // its tab
    kit.Line(cx + 1.5f, cy + 1.0f, cx + 6.5f, cy + 1.0f, 2.0f, rgb);
    kit.Line(cx + 4.0f, cy - 1.5f, cx + 4.0f, cy + 3.5f, 2.0f, rgb);
}

float DrawGameTile(Kit& kit, const GameEntry& g, const GameIcon* cover, const GameTileLook& look,
                   const Rect& r, const Rect& clip) {
    const bool running = look.running;
    const float op = g.missing ? kDisabledContent : 1.0f;
    kit.Fill(r, kTileRadius, running ? Tok::kSecondaryContainer : Tok::kSurfaceContainerHigh);
    kit.StateLayer(r, kTileRadius, Tok::kOnSurface, look.tile);

    const Rect art{r.x0 + kArtX, r.y0 + kArtY, r.x0 + kArtX + kArtW, r.y0 + kArtY + kArtH};
    DrawArt(kit, g, cover, art, op);
    if (g.favorite) {
        MarkCircle(kit, r.x0 + kMarkAt, r.y0 + kMarkAt, kMarkR);
        kit.Icon(r.x0 + kMarkAt, r.y0 + kMarkAt, kStar, Sym::kStar, true, Tok::kOnSurface, op);
    }
    if (look.pointed) {
        const float cx = r.x1 - kMarkAt, cy = r.y0 + kMarkAt;
        MarkCircle(kit, cx, cy, kOptionsR);
        kit.StateLayer({cx - kOptionsR, cy - kOptionsR, cx + kOptionsR, cy + kOptionsR}, kOptionsR,
                       Tok::kOnSurface, look.options);
        for (const float dy : {-5.0f, 0.0f, 5.0f}) {
            kit.Dot(cx, cy + dy, 1.7f, Tok::kOnSurface);
        }
    }

    // The title; one that does not fit scrolls by while its tile is pointed at
    bool cut = false;
    float overflow = 0.0f;
    const uint32_t title_ink = running ? Tok::kOnSecondaryContainer : Tok::kOnSurface;
    auto lines = GameTitleLines(kit, g.title, kGameTextW, &cut);
    const auto count = static_cast<float>(lines.size());
    float scrolled = 0.0f;
    if (cut) {
        auto whole = GameTitleWrapped(kit, g.title, kGameTextW);
        overflow = static_cast<float>(whole.size() - kGameTitleLines) * kGameTitlePitch;
        if (look.pointed && look.title_scroll > 0.0f) {
            scrolled = look.title_scroll;
            lines = std::move(whole);
        }
    }
    const float top = r.y0 + kTitleBase - kGameTitlePitch + 4.0f;
    if (scrolled > 0.0f) {
        kit.Clip({clip.x0, std::max(clip.y0, top), clip.x1,
                  std::min(clip.y1, top + count * kGameTitlePitch)});
    }
    for (std::size_t i = 0; i < lines.size(); i++) {
        kit.Text(r.Cx(), r.y0 + kTitleBase + static_cast<float>(i) * kGameTitlePitch - scrolled,
                 kTitle, title_ink, lines[i], 1, op);
    }
    if (scrolled > 0.0f) {
        kit.Clip(clip);
    }

    // The small line, at the same place on every tile
    const float base = r.y0 + kSmallBase;
    const bool medium = running && !g.missing;
    const float before = medium ? 16.0f : 0.0f; // the play arrow
    std::string small;
    for (const std::string& line : GameLines(g, running)) {
        small = line;
        if (kit.TextWidthSized(kSmall, medium, line) <= kGameTextW - before) {
            break;
        }
    }
    const uint32_t ink = g.missing ? Tok::kError
                         : running ? Tok::kOnSecondaryContainer
                                   : Tok::kOnSurfaceVariant;
    const float left = r.Cx() - (kit.TextWidthSized(kSmall, medium, small) + before) * 0.5f;
    if (medium) {
        kit.Icon(left + 6.0f, base - 4.7f, 16.0f, Sym::kPlayArrow, true, ink);
    }
    kit.TextSized(left + before, base, kSmall, medium, ink, small);
    return overflow;
}

void DrawGamesChip(Kit& kit, const Rect& r, const std::string& name, bool on, State state) {
    const uint32_t ink = on ? Tok::kOnSecondaryContainer : Tok::kOnSurfaceVariant;
    if (on) {
        kit.Fill(r, kGamesChipRadius, Tok::kSecondaryContainer);
    } else {
        kit.Stroke(r, kGamesChipRadius, 1.0f, Tok::kOutline);
    }
    kit.StateLayer(r, kGamesChipRadius, ink, state);
    const float tw = kit.TextWidth(Type::Label, name);
    const float x0 = r.Cx() - (tw + (on ? 22.0f : 0.0f)) * 0.5f;
    if (on) {
        kit.Icon(x0 + 9.0f, r.Cy(), 18.0f, Sym::kCheck, false, ink);
    }
    kit.TextMid(x0 + (on ? 22.0f : 0.0f), r.Cy(), Type::Label, ink, name);
}

} // namespace FrameXr::Menu
