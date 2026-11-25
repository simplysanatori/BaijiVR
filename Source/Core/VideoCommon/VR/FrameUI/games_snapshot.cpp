// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoCommon/VR/FrameUI/games_snapshot.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "VideoCommon/VR/FrameUI/ui_ttf.h"

namespace FrameXr::Menu {

namespace {

/// For comparing: lower case, without accents.
std::string Folded(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        const char32_t cp = frame_xr::ui::NextUtf8(s, i);
        out += cp < 0x80 || cp >= 0x180 ? frame_xr::ui::Utf8Of(cp) : frame_xr::ui::FoldedText(cp);
    }
    std::transform(out.begin(), out.end(), out.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return out;
}

bool HasSystem(const GamesList& list, SystemFilter chip) {
    return std::any_of(list->games.begin(), list->games.end(), [chip](const GameEntry& g) {
        return FilterMatches(chip, g.system);
    });
}

} // namespace

bool NameDrawable(const std::string& utf8) {
    for (std::size_t i = 0; i < utf8.size();) {
        const std::size_t start = i;
        const char32_t cp = frame_xr::ui::NextUtf8(utf8, i);
        if (frame_xr::ui::Utf8Of(cp) != utf8.substr(start, i - start)) {
            return false; // no UTF-8
        }
        if (cp < 0x20 || (cp >= 0x180 && frame_xr::ui::FoldedText(cp) == "?")) {
            return false;
        }
    }
    return true;
}

GamesSnapshot MakeSnapshot(std::vector<GameEntry> rows, const std::vector<std::string>& recent) {
    GamesSnapshot out;
    std::unordered_set<std::string> seen;
    for (GameEntry& g : rows) {
        if (!g.path.empty() && seen.insert(g.path).second) {
            out.games.push_back(std::move(g));
        }
    }
    std::map<std::pair<GameSystem, std::string>, int> titles;
    for (GameEntry& g : out.games) {
        if (g.title.empty() || (!NameDrawable(g.title) && !g.file_name.empty())) {
            g.title = g.file_name;
        }
        titles[{g.system, Folded(g.title)}]++;
    }
    for (GameEntry& g : out.games) {
        if (titles[{g.system, Folded(g.title)}] > 1 && !g.file_name.empty()) {
            g.title = g.file_name;
        }
    }
    std::unordered_map<std::string, std::size_t> played;
    for (std::size_t i = 0; i < recent.size(); i++) {
        played.emplace(recent[i], i);
    }
    const auto place = [&played, &recent](const GameEntry& g) {
        const auto it = played.find(g.path);
        return it == played.end() ? recent.size() : it->second;
    };
    std::stable_sort(out.games.begin(), out.games.end(),
                     [&place](const GameEntry& a, const GameEntry& b) {
                         const std::size_t pa = place(a), pb = place(b);
                         return pa != pb ? pa < pb : Folded(a.title) < Folded(b.title);
                     });
    return out;
}

std::vector<SystemFilter> GameChips(const GamesList& list) {
    std::vector<SystemFilter> chips;
    if (!list) {
        return chips;
    }
    for (int i = 1; i < kSystemFilters; i++) {
        if (HasSystem(list, static_cast<SystemFilter>(i))) {
            chips.push_back(static_cast<SystemFilter>(i));
        }
    }
    if (chips.size() < 2) {
        return {};
    }
    chips.insert(chips.begin(), SystemFilter::All);
    return chips;
}

SystemFilter GameChip(const GamesList& list, SystemFilter wanted) {
    const auto chips = GameChips(list);
    return std::find(chips.begin(), chips.end(), wanted) != chips.end() ? wanted : SystemFilter::All;
}

std::vector<int> GamesShown(const GamesList& list, SystemFilter chip, GamesSort sort,
                            const std::string& search) {
    std::vector<int> out;
    if (!list) {
        return out;
    }
    const auto& games = list->games;
    const std::string wanted = Folded(search);
    for (std::size_t i = 0; i < games.size(); i++) {
        const GameEntry& g = games[i];
        if (FilterMatches(chip, g.system) &&
            (wanted.empty() || Folded(g.title).find(wanted) != std::string::npos ||
             Folded(g.file_name).find(wanted) != std::string::npos)) {
            out.push_back(static_cast<int>(i));
        }
    }
    if (sort == GamesSort::RecentlyPlayed) { // the list's own order
        return out;
    }
    const auto at = [&games](int i) -> const GameEntry& { return games[static_cast<std::size_t>(i)]; };
    std::stable_sort(out.begin(), out.end(), [&](int x, int y) {
        const GameEntry& a = at(x);
        const GameEntry& b = at(y);
        if (sort == GamesSort::FavouritesFirst && a.favorite != b.favorite) {
            return a.favorite;
        }
        if (sort == GamesSort::PlayTime && a.played_s != b.played_s) {
            return a.played_s > b.played_s;
        }
        return Folded(a.title) < Folded(b.title);
    });
    return out;
}

const char* ChipName(SystemFilter chip) {
    static constexpr std::array<const char*, kSystemFilters> kNames{"All", "GameCube", "Wii"};
    return kNames[static_cast<std::size_t>(chip)];
}

const char* SystemName(GameSystem system) {
    switch (system) {
    case GameSystem::GameCube:
        return "GameCube";
    case GameSystem::Wii:
        return "Wii";
    default:
        return "Homebrew";
    }
}

const char* SortName(GamesSort sort) {
    switch (sort) {
    case GamesSort::FavouritesFirst:
        return "Favourites First";
    case GamesSort::Title:
        return "Title";
    case GamesSort::PlayTime:
        return "Play Time";
    default:
        return "Recently Played";
    }
}

std::vector<std::string> GameLines(const GameEntry& game, bool running) {
    if (game.missing) {
        return {"File Not Found"};
    }
    const std::string system = SystemName(game.system);
    if (running) {
        return {system + " \u00b7 Running", "Running"};
    }
    if (!game.region.empty()) {
        return {system + " \u00b7 " + game.region, system};
    }
    return {system};
}

} // namespace FrameXr::Menu
