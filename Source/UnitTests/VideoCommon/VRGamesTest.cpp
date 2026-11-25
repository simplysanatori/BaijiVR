// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

// The Games tab: its list (FrameUI/games_snapshot.h), its tiles (games_tab.h), its covers
// (cover_art.h), its part of the menu, and the game list's side of it (UICommon/VRGames.h). The first
// parts are Azahar for Frame's tests.

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Common/FileUtil.h"
#include "Core/TimePlayed.h"
#include "Core/TitleDatabase.h"
#include "UICommon/GameFile.h"
#include "UICommon/VRGames.h"
#include "VideoCommon/VR/FrameUI/FileList.h"
#include "VideoCommon/VR/FrameUI/GamesHost.h"
#include "VideoCommon/VR/FrameUI/VrMenu.h"
#include "VideoCommon/VR/FrameUI/cover_art.h"
#include "VideoCommon/VR/FrameUI/games_tab.h"
#include "VideoCommon/VR/FrameUI/grid_focus.h"

using namespace FrameXr::Menu;
using namespace VR::UI;

namespace
{
GameEntry Game(const std::string& title, GameSystem system, const std::string& path = {})
{
  GameEntry g;
  g.title = title;
  g.file_name = title + " (File)";
  g.path = path.empty() ? "/games/" + title : path;
  g.system = system;
  return g;
}

GamesList Listed(std::vector<GameEntry> rows, const std::vector<std::string>& recent = {})
{
  return std::make_shared<const GamesSnapshot>(MakeSnapshot(std::move(rows), recent));
}

std::vector<std::string> Shown(const GamesList& list, SystemFilter chip, GamesSort sort,
                               const std::string& search = {})
{
  std::vector<std::string> out;
  for (const int i : GamesShown(list, chip, sort, search))
    out.push_back(list->games[static_cast<std::size_t>(i)].title);
  return out;
}

using Titles = std::vector<std::string>;

MenuState GamesMenu(int games, const std::string& running = {})
{
  std::vector<GameEntry> rows;
  for (int i = 0; i < games; ++i)
  {
    rows.push_back(Game("Game " + std::string(1, static_cast<char>('A' + i / 26)) +
                            static_cast<char>('A' + i % 26),
                        i % 2 == 0 ? GameSystem::GameCube : GameSystem::Wii));
  }
  MenuState state;
  state.tab = MenuTab::Games;
  state.title = "The Running Game";
  state.games.list = Listed(std::move(rows));
  state.games.running = running;
  state.games.UpdateShown();
  return state;
}

std::vector<MenuWidget> Of(const MenuState& state, MenuItem item)
{
  std::vector<MenuWidget> out;
  for (const MenuWidget& w : VrMenu::Layout(state))
  {
    if (w.item == item)
      out.push_back(w);
  }
  return out;
}
}  // namespace

TEST(VRGames, ListOrder)
{
  GamesList list = Listed({Game("beta", GameSystem::Wii), Game("Alpha", GameSystem::GameCube),
                           Game("Éclair", GameSystem::GameCube), Game("Zeta", GameSystem::Wii),
                           Game("Alpha again", GameSystem::GameCube, "/games/Alpha")},
                          {"/games/Zeta", "/games/not there"});
  // Each file once, the recently played first, then by title whatever its case and accents.
  EXPECT_EQ(Shown(list, SystemFilter::All, GamesSort::RecentlyPlayed),
            (Titles{"Zeta", "Alpha", "beta", "Éclair"}));
}

TEST(VRGames, SameTitlesAndOnesTheFontHasNot)
{
  const GamesList list =
      Listed({Game("Same", GameSystem::Wii, "/a"), Game("same", GameSystem::Wii, "/b"),
              Game("Same", GameSystem::GameCube, "/c"),
              Game("マリオ", GameSystem::GameCube, "/d")});
  std::set<std::string> titles;
  for (const GameEntry& g : list->games)
    titles.insert(g.title);
  EXPECT_EQ(titles, (std::set<std::string>{"Same (File)", "same (File)", "Same",
                                           "マリオ (File)"}));
}

TEST(VRGames, Chips)
{
  const GamesList both = Listed({Game("A", GameSystem::GameCube), Game("B", GameSystem::Wii),
                                 Game("C", GameSystem::Other)});
  EXPECT_EQ(GameChips(both), (std::vector{SystemFilter::All, SystemFilter::GameCube,
                                          SystemFilter::Wii}));
  EXPECT_EQ(Shown(both, SystemFilter::Wii, GamesSort::Title), (Titles{"B"}));
  EXPECT_EQ(Shown(both, SystemFilter::GameCube, GamesSort::Title), (Titles{"A"}));
  EXPECT_EQ(Shown(both, SystemFilter::All, GamesSort::Title).size(), 3u);
  // With games of one system there are none.
  const GamesList one = Listed({Game("A", GameSystem::Wii), Game("B", GameSystem::Wii)});
  EXPECT_TRUE(GameChips(one).empty());
  EXPECT_EQ(GameChip(one, SystemFilter::GameCube), SystemFilter::All);
  EXPECT_TRUE(GameChips(nullptr).empty());
}

TEST(VRGames, SortAndSearch)
{
  std::vector<GameEntry> rows{Game("Cee", GameSystem::Wii), Game("Bee", GameSystem::GameCube),
                              Game("Dee", GameSystem::Wii)};
  rows[2].favorite = true;
  rows[0].played_s = 10;
  rows[2].played_s = 20;
  const GamesList list = Listed(rows, {"/games/Cee"});
  const auto sorted = [&](GamesSort sort) { return Shown(list, SystemFilter::All, sort); };
  EXPECT_EQ(sorted(GamesSort::RecentlyPlayed), (Titles{"Cee", "Bee", "Dee"}));
  EXPECT_EQ(sorted(GamesSort::FavouritesFirst), (Titles{"Dee", "Bee", "Cee"}));
  EXPECT_EQ(sorted(GamesSort::Title), (Titles{"Bee", "Cee", "Dee"}));
  EXPECT_EQ(sorted(GamesSort::PlayTime), (Titles{"Dee", "Cee", "Bee"}));
  EXPECT_EQ(Shown(list, SystemFilter::All, GamesSort::Title, "bE"), (Titles{"Bee"}));
  EXPECT_EQ(Shown(list, SystemFilter::All, GamesSort::Title, "(file)").size(), 3u);
  EXPECT_TRUE(Shown(list, SystemFilter::GameCube, GamesSort::Title, "Cee").empty());
}

TEST(VRGames, SmallLine)
{
  GameEntry g = Game("A", GameSystem::GameCube);
  g.region = "North America";
  EXPECT_EQ(GameLines(g, false).front(), "GameCube · North America");
  g.system = GameSystem::Wii;
  EXPECT_EQ(GameLines(g, true).front(), "Wii · Running");
  g.missing = true;
  EXPECT_EQ(GameLines(g, false).front(), "File Not Found");
}

TEST(VRGames, TitleLines)
{
  frame_xr::ui::InitFonts();
  const Kit kit(1.5f);
  bool cut = true;
  EXPECT_EQ(GameTitleLines(kit, "Tetris", kGameTextW, &cut).size(), 1u);
  EXPECT_FALSE(cut);
  const std::string title = "The Legend of Zelda: The Wind Waker and Ocarina of Time Master Quest "
                            "Promotional Bonus Disc of the Year";
  const auto lines = GameTitleLines(kit, title, kGameTextW, &cut);
  EXPECT_EQ(lines.size(), static_cast<std::size_t>(kGameTitleLines));
  EXPECT_TRUE(cut);
  EXPECT_TRUE(lines.back().ends_with("…"));
  for (const std::string& line : lines)
    EXPECT_LE(kit.TextWidth(Type::RailLabel, line), kGameTextW + 0.5f);
  EXPECT_GT(GameTitleWrapped(kit, title, kGameTextW).size(), lines.size());
}

TEST(VRGames, ScrollRestsOnARow)
{
  EXPECT_EQ(GamesRestScroll(100.0f, 1000.0f), 0.0f);
  EXPECT_EQ(GamesRestScroll(120.0f, 1000.0f), kGameRow);
  EXPECT_EQ(GamesRestScroll(990.0f, 1000.0f), 1000.0f);
  EXPECT_EQ(TitleScroll(0.5f, 18.0f), 0.0f);
  EXPECT_GT(TitleScroll(2.0f, 18.0f), 0.0f);
  EXPECT_EQ(TitleScroll(5.0f, 0.0f), 0.0f);
}

TEST(VRGames, Cover)
{
  // A portrait box: all of it, in the middle; beside it a dimmed copy.
  GameIcon box;
  box.w = 200;
  box.h = 280;
  box.rgba.resize(200 * 280 * 4);
  for (std::size_t i = 0; i < box.rgba.size(); i += 4)
  {
    box.rgba[i] = 200;
    box.rgba[i + 3] = 255;
  }
  const GameIcon cover = ComposeCover(box);
  ASSERT_EQ(cover.w, kCoverPxW);
  ASSERT_EQ(cover.h, kCoverPxH);
  const auto red = [&](int x, int y) {
    return cover.rgba[static_cast<std::size_t>(y * cover.w + x) * 4];
  };
  EXPECT_EQ(red(kCoverPxW / 2, kCoverPxH / 2), 200);
  EXPECT_LT(red(2, kCoverPxH / 2), 150);
  EXPECT_GT(red(2, kCoverPxH / 2), 60);
  EXPECT_EQ(ComposeCover({}).w, 0);
}

TEST(VRGames, TabLayout)
{
  const MenuState state = GamesMenu(40);
  EXPECT_EQ(Of(state, MenuItem::GamesChip).size(), 3u);
  EXPECT_EQ(Of(state, MenuItem::GamesSort).size(), 1u);
  // Second on the rail.
  EXPECT_LT(Of(state, MenuItem::TabMain)[0].rect.y0, Of(state, MenuItem::TabGames)[0].rect.y0);
  EXPECT_LT(Of(state, MenuItem::TabGames)[0].rect.y0, Of(state, MenuItem::TabPosition)[0].rect.y0);
  const auto tiles = Of(state, MenuItem::GameTile);
  ASSERT_GE(tiles.size(), 8u);
  for (const MenuWidget& tile : tiles)
  {
    EXPECT_GE(tile.rect.x0, kGamesX0);
    EXPECT_LE(tile.rect.x1, kGamesX1);
  }
  const auto list = VrMenu::GetList(state);
  ASSERT_TRUE(list && list->games && list->bar.Valid());
  // Its end is a row's edge, too.
  EXPECT_EQ(GamesRestScroll(list->bar.max_scroll, list->bar.max_scroll), list->bar.max_scroll);
  EXPECT_EQ(list->bar.max_scroll, kGameRow * 8.0f);

  MenuState scrolled = state;
  GetScroll(&scrolled, *list) = list->bar.max_scroll;
  EXPECT_EQ(scrolled.games.scroll, list->bar.max_scroll);
  EXPECT_EQ(Of(scrolled, MenuItem::GameTile).back().label, "Game BN");
  // A tile that scrolled away is not under the pointer.
  EXPECT_EQ(VrMenu::At(VrMenu::Layout(scrolled), kGamesX0 + 50.0f, 60.0f)->item,
            MenuItem::GamesChip);
}

TEST(VRGames, OptionsAndQuestion)
{
  MenuState state = GamesMenu(6, "/games/Game AB");
  state.games.popup = MenuGames::Popup::Options;
  state.games.game = "/games/Game AC";
  auto menus = Of(state, MenuItem::Popup);
  ASSERT_EQ(menus.size(), 1u);
  EXPECT_EQ(menus[0].options,
            (Titles{"Play", "Favourite", "Choose Another Cover", "No Cover", "Use My Own Image",
                    "Reset Cover", "Download Thumbnails"}));
  EXPECT_EQ(menus[0].selected, -1);
  state.games.game = state.games.running;
  EXPECT_EQ(Of(state, MenuItem::Popup)[0].options[0], "Continue");

  state.games.popup = MenuGames::Popup::Question;
  state.games.game = "/games/Game AC";
  menus = Of(state, MenuItem::Popup);
  ASSERT_EQ(menus.size(), 1u);
  EXPECT_EQ(menus[0].label, "Start Game AC?");
  EXPECT_EQ(menus[0].options, (Titles{"Start Game AC?", "Game AB will close."}));
  EXPECT_EQ(menus[0].selected, 1);
  state.games.loses_progress = true;
  EXPECT_EQ(Of(state, MenuItem::Popup)[0].options.size(), 3u);
  const auto answers = Of(state, MenuItem::PopupOption);
  ASSERT_EQ(answers.size(), 2u);
  for (const MenuWidget& answer : answers)
  {
    const auto widgets = VrMenu::Layout(state);
    const MenuWidget* const at = VrMenu::At(widgets, answer.rect.Cx(), answer.rect.Cy());
    ASSERT_NE(at, nullptr);
    EXPECT_EQ(at->item, MenuItem::PopupOption);
    EXPECT_EQ(at->index, answer.index);
  }
  // Its game is gone from the list: no question.
  state.games.game = "/games/not there";
  EXPECT_TRUE(Of(state, MenuItem::Popup).empty());
  state.games.UpdateShown();
  EXPECT_EQ(state.games.popup, MenuGames::Popup::None);
}

TEST(VRGames, WithoutAWindow)
{
  MenuState state = GamesMenu(1);
  state.games.has_window = false;
  EXPECT_EQ(Of(state, MenuItem::GameTile).size(), 1u);
  EXPECT_TRUE(Of(state, MenuItem::GameOptions).empty());
  EXPECT_TRUE(Of(state, MenuItem::GamesSort).empty());
  // Nothing at all.
  state.games = {};
  EXPECT_TRUE(Of(state, MenuItem::GameTile).empty());
}

TEST(VRGames, Drawing)
{
  frame_xr::ui::InitFonts();
  Kit kit(1.5f);
  Canvas canvas;
  canvas.Init(1260, 840);
  MenuState state = GamesMenu(9, "/games/Game AA");
  auto list = std::make_shared<GamesSnapshot>(*state.games.list);
  list->games[1].title = "A Title That Is Very Much Longer Than The Three Lines That A Tile Has For It";
  list->games[2].favorite = true;
  state.games.list = list;
  VrMenu::GamesDraw games;
  int asked = 0;
  games.get_cover = [&](const GameEntry&) -> const GameIcon* {
    ++asked;
    return nullptr;
  };
  VrMenu::Draw(&kit, &canvas, state, [](const std::string&) { return nullptr; }, &games);
  // Only the tiles that show.
  EXPECT_EQ(asked, 8);
  ASSERT_EQ(games.cut_titles.size(), 1u);
  EXPECT_EQ(games.cut_titles[0].first, 1);
  for (const auto popup :
       {MenuGames::Popup::Sort, MenuGames::Popup::Options, MenuGames::Popup::Question})
  {
    state.games.popup = popup;
    state.games.game = list->games[7].path;
    VrMenu::Draw(&kit, &canvas, state, [](const std::string&) { return nullptr; }, &games);
  }
}

TEST(VRGames, Publishing)
{
  const u32 before = GetPublishedGamesSerial();
  const GamesList list = Listed({Game("A", GameSystem::Wii)});
  PublishGames(list, "/games/A");
  PublishedGames published = GetPublishedGames();
  EXPECT_EQ(published.serial, before + 1);
  EXPECT_EQ(published.list, list);
  EXPECT_EQ(published.running, "/games/A");
  // The same games again: the same list, which is not drawn anew.
  PublishGames(Listed({Game("A", GameSystem::Wii)}), {});
  published = GetPublishedGames();
  EXPECT_EQ(published.list, list);
  EXPECT_TRUE(published.running.empty());

  EXPECT_EQ(GetGamesHost(), nullptr);
  std::string asked;
  SetGamesHost({.play = [&](const std::string& path) -> std::string {
    asked = path;
    return "Not During NetPlay";
  }});
  ASSERT_NE(GetGamesHost(), nullptr);
  EXPECT_EQ(GetGamesHost()->play("/games/A"), "Not During NetPlay");
  EXPECT_EQ(asked, "/games/A");
  SetGamesHost({});
  EXPECT_EQ(GetGamesHost(), nullptr);
}

TEST(VRGames, FromTheGameList)
{
  const std::string directory = File::CreateTempDir();
  ASSERT_FALSE(directory.empty());
  const std::string ini = directory + "/VRGames.ini";
  {
    UICommon::VRGameStore store(ini);
    EXPECT_FALSE(store.IsFavourite("One"));
    store.SetFavourite("One.iso", true);
    store.SetLastPlayed("Two.iso", 1'700'000'000);
  }
  UICommon::VRGameStore store(ini);
  EXPECT_TRUE(store.IsFavourite("One.iso"));
  EXPECT_EQ(store.GetLastPlayed("Two.iso"), 1'700'000'000);
  EXPECT_EQ(store.GetLastPlayed("One.iso"), 0);

  const auto one = std::make_shared<const UICommon::GameFile>("/nowhere/One.iso");
  const auto two = std::make_shared<const UICommon::GameFile>("/nowhere/Two.iso");
  EXPECT_EQ(UICommon::GetVRGameKey(*one), "One.iso");
  const Core::TitleDatabase titles;
  const TimePlayed time_played;
  const std::set<std::string> missing{"/nowhere/One.iso"};
  UICommon::VRGameList list;
  const std::vector<std::shared_ptr<const UICommon::GameFile>> files{one, two};
  const GamesSnapshot games = list.Make(files, {titles, store, time_played, missing});
  ASSERT_EQ(games.games.size(), 2u);
  // The last played first.
  EXPECT_EQ(games.games[0].path, "/nowhere/Two.iso");
  EXPECT_TRUE(games.games[1].favorite);
  EXPECT_TRUE(games.games[1].missing);
  EXPECT_EQ(games.games[1].covers[0], "/nowhere/One.cover.png");
  // Nothing new: the same. Another file object: its cover may be another.
  EXPECT_EQ(list.Make(files, {titles, store, time_played, missing}), games);
  const std::vector<std::shared_ptr<const UICommon::GameFile>> changed{
      std::make_shared<const UICommon::GameFile>("/nowhere/One.iso"), two};
  const GamesSnapshot again = list.Make(changed, {titles, store, time_played, missing});
  EXPECT_NE(again.games[1].cover_version, games.games[1].cover_version);
  EXPECT_EQ(again.games[0].cover_version, games.games[0].cover_version);

  store.SetFavourite("One.iso", false);
  EXPECT_FALSE(UICommon::VRGameStore(ini).IsFavourite("One.iso"));

  EXPECT_EQ(UICommon::FindRunningVRGame(games, "/nowhere/One.iso", "X"), "/nowhere/One.iso");
  EXPECT_EQ(UICommon::FindRunningVRGame(games, "/elsewhere", "X"), "");
  File::DeleteDirRecursively(directory);
}

TEST(VRGames, Typing)
{
  MenuState state = GamesMenu(30);
  EXPECT_EQ(Of(state, MenuItem::GamesSearch).size(), 1u);
  EXPECT_TRUE(Of(state, MenuItem::GamesKey).empty());
  state.games.typing = true;
  const std::vector<MenuWidget> keys = Of(state, MenuItem::GamesKey);
  EXPECT_EQ(keys.size(), 40u);
  for (const MenuWidget& key : keys)
  {
    EXPECT_GE(key.rect.x0, kGamesX0);
    EXPECT_LE(key.rect.x1, kGamesX1 + 0.5f);
    EXPECT_LE(key.rect.y1, VrMenu::HEIGHT);
  }
  for (const MenuWidget& tile : Of(state, MenuItem::GameTile))
    EXPECT_LT(tile.rect.y0, kGamesKeysY0);
  // No space in front.
  state.games.Type(' ');
  EXPECT_TRUE(state.games.search.empty());
  for (const char key : std::string("game aa"))
    state.games.Type(key);
  EXPECT_EQ(state.games.search, "game aa");
  EXPECT_EQ(state.games.shown.size(), 1u);
  state.games.Type('\b');
  EXPECT_EQ(state.games.search, "game a");
  EXPECT_EQ(state.games.shown.size(), 26u);
  state.games.Type('\n');
  EXPECT_FALSE(state.games.typing);
  // Where All was; its cross ends it.
  EXPECT_EQ(Of(state, MenuItem::GamesSearchClear).size(), 1u);
  EXPECT_EQ(Of(state, MenuItem::GamesChip).size(), 2u);
  for (const char key : std::string("zzz"))
    state.games.Type(key);
  EXPECT_TRUE(state.games.shown.empty());
  // The chip's, and the button under No Matches.
  EXPECT_EQ(Of(state, MenuItem::GamesSearchClear).size(), 2u);
}

TEST(VRGames, Folders)
{
  EXPECT_TRUE(IsGameFileName("A.ISO"));
  EXPECT_TRUE(IsGameFileName("a.b.rvz"));
  EXPECT_FALSE(IsGameFileName("rvz"));
  EXPECT_FALSE(IsGameFileName("a.png"));
  EXPECT_TRUE(IsImageFileName("a.PNG"));
  EXPECT_FALSE(IsImageFileName("a.rvz"));

  const std::string root = File::CreateTempDir();
  ASSERT_FALSE(root.empty());
  File::CreateDir(root + "/wii");
  File::CreateDir(root + "/Empty");
  File::CreateDir(root + "/.hidden");
  for (const char* const name : {"/one.iso", "/wii/two.rvz", "/wii/three.wbfs", "/wii/notes.txt"})
    File::WriteStringToFile(root + name, "");
  File::WriteStringToFile(root + "/picture.png", "");

  const FileListing folder = ReadFileList(root, FilePurpose::Folder);
  EXPECT_TRUE(folder.readable);
  EXPECT_EQ(folder.path, root);
  EXPECT_FALSE(folder.parent.empty());
  EXPECT_EQ(folder.games, 1);
  ASSERT_EQ(folder.rows.size(), 2u);
  EXPECT_EQ(folder.rows[0].name, "Empty");
  EXPECT_EQ(folder.rows[0].games, 0);
  EXPECT_EQ(folder.rows[1].name, "wii");
  EXPECT_EQ(folder.rows[1].games, 2);
  EXPECT_FALSE(folder.places.empty());

  const FileListing image = ReadFileList(root, FilePurpose::Image);
  ASSERT_EQ(image.rows.size(), 3u);
  EXPECT_EQ(image.rows[2].name, "picture.png");
  EXPECT_FALSE(image.rows[2].is_folder);

  EXPECT_FALSE(ReadFileList(root + "/not there", FilePurpose::Folder).readable);
  EXPECT_TRUE(ReadFileList("/", FilePurpose::Folder).parent.empty());
  EXPECT_EQ(GetFileListStart({root + "/not there", root}), root);

  // The page: Up, the rows, the button.
  MenuState state = GamesMenu(3);
  state.page = MenuPage::Files;
  state.files.listing = folder;
  EXPECT_EQ(Of(state, MenuItem::FileRow).size(), 3u);
  ASSERT_EQ(Of(state, MenuItem::FileAdd).size(), 1u);
  EXPECT_FALSE(Of(state, MenuItem::FileAdd)[0].disabled);
  state.files.wanted = "/somewhere";
  EXPECT_TRUE(Of(state, MenuItem::FileAdd)[0].disabled);
  state.files.wanted.clear();
  state.files.purpose = FilePurpose::Image;
  EXPECT_TRUE(Of(state, MenuItem::FileAdd).empty());
  // The D-Pad: from the top row of the list to what is over it, and down again.
  const std::vector<MenuWidget> widgets = VrMenu::Layout(state);
  const MenuWidget up = Of(state, MenuItem::FileRow)[0];
  const MenuWidget* const next = VrMenu::Next(widgets, up.rect.Cx(), up.rect.Cy(), 0, 1);
  ASSERT_TRUE(next);
  EXPECT_EQ(next->item, MenuItem::FileRow);
  EXPECT_EQ(next->index, 0);
  const MenuWidget* const above = VrMenu::Next(widgets, up.rect.Cx(), up.rect.Cy(), 0, -1);
  ASSERT_TRUE(above);
  EXPECT_NE(above->item, MenuItem::FileRow);
  File::DeleteDirRecursively(root);
}

TEST(VRGames, CoverChoices)
{
  const std::string root = File::CreateTempDir();
  ASSERT_FALSE(root.empty());
  using Cover = UICommon::VRGameStore::Cover;
  {
    UICommon::VRGameStore store(root + "/VRGames.ini");
    EXPECT_EQ(store.GetCover("RSBE01"), Cover::Default);
    store.SetCover("RSBE01", Cover::None);
    store.SetCover("GFEE01", Cover::Own);
    store.SetCover("GM4E01", Cover::Snap);
    store.SetCover("GM4E01", Cover::Default);
  }
  const UICommon::VRGameStore store(root + "/VRGames.ini");
  EXPECT_EQ(store.GetCover("RSBE01"), Cover::None);
  EXPECT_EQ(store.GetCover("GFEE01"), Cover::Own);
  EXPECT_EQ(store.GetCover("GM4E01"), Cover::Default);
  File::DeleteDirRecursively(root);
}

TEST(VRGames, FocusMoves)
{
  using namespace Focus;
  Shape s{.columns = 4, .games = 10, .top = 3, .above = 2, .rail = 7, .rail_active = 1};
  GridFocus f;
  f.Move(Dir::Right, s);  // the first one only shows it
  EXPECT_TRUE(f.shown);
  EXPECT_EQ(f.tile, 0);
  f.Move(Dir::Right, s);
  f.Move(Dir::Down, s);
  EXPECT_EQ(f.tile, 5);
  for (int i = 0; i < 3; ++i)
    f.Move(Dir::Right, s);
  EXPECT_EQ(f.tile, 7);
  f.Move(Dir::Down, s);  // the short last row's last game
  EXPECT_EQ(f.tile, 9);
  GridFocus up = f;
  up.Move(Dir::Up, s);
  up.Move(Dir::Up, s);
  EXPECT_EQ(up.tile, 1);
  up.Move(Dir::Up, s);
  EXPECT_EQ(up.area, Area::Top);
  EXPECT_EQ(up.top, 2);
  up.Move(Dir::Left, s);
  up.Move(Dir::Down, s);
  EXPECT_EQ(up.area, Area::Grid);
  EXPECT_EQ(up.tile, 1);
  f.Move(Dir::Left, s);
  f.Move(Dir::Left, s);
  EXPECT_EQ(f.area, Area::Rail);
  EXPECT_EQ(f.rail, 1);
  f.Move(Dir::Right, s);
  EXPECT_EQ(f.area, Area::Grid);
  EXPECT_EQ(f.tile, 8);
  s.games = 3;
  f.Move(Dir::Left, s);
  EXPECT_EQ(f.tile, 1);
  EXPECT_EQ(ScrollToShow(0.0f, 424.0f, 624.0f, 420.0f, 600.0f), 204.0f);
  EXPECT_EQ(ScrollToShow(300.0f, 212.0f, 412.0f, 420.0f, 600.0f), 212.0f);
  EXPECT_EQ(ScrollToShow(50.0f, 0.0f, 200.0f, 420.0f, -5.0f), 0.0f);
}

TEST(VRGames, HeldDirections)
{
  using namespace Focus;
  DirRepeat repeat;
  EXPECT_EQ(repeat.Update(false, Dir::Up, 0.0f, 0.0f, 0), Dir::None);
  // Held as it became true: nothing until let go.
  EXPECT_EQ(repeat.Update(true, Dir::Up, 0.0f, 0.0f, 1'000'000'000), Dir::None);
  EXPECT_EQ(repeat.Update(true, Dir::None, 0.0f, 0.0f, 1'100'000'000), Dir::None);
  EXPECT_EQ(repeat.Update(true, Dir::None, 0.0f, -0.9f, 1'200'000'000), Dir::Down);
  EXPECT_EQ(repeat.Update(true, Dir::None, 0.0f, -0.9f, 1'300'000'000), Dir::None);
  EXPECT_EQ(repeat.Update(true, Dir::None, 0.0f, -0.9f, 1'700'000'000), Dir::Down);
}

TEST(VRGames, QuestionFits)
{
  MenuState state = GamesMenu(0);
  state.games.list = Listed({Game("A", GameSystem::Wii),
                             Game("A Very Long Title of a Game: With a Subtitle That Goes On and On "
                                  "and On, Until It Has Run Out of Words",
                                  GameSystem::GameCube, "/long")});
  state.games.running = "/games/A";
  state.games.UpdateShown();
  state.games.popup = MenuGames::Popup::Question;
  float height = 0.0f;
  for (const char* const path : {"/games/A", "/long"})
  {
    state.games.game = path;
    const std::vector<MenuWidget> box = Of(state, MenuItem::Popup);
    const std::vector<MenuWidget> buttons = Of(state, MenuItem::PopupOption);
    ASSERT_EQ(box.size(), 1u);
    ASSERT_EQ(buttons.size(), 2u);
    EXPECT_LE(box[0].selected, 3);
    EXPECT_GE(box[0].rect.y0, 0.0f);
    EXPECT_LE(box[0].rect.y1, VrMenu::HEIGHT);
    // The box follows the text; the buttons are in its middle.
    EXPECT_GT(box[0].rect.H(), height);
    height = box[0].rect.H();
    EXPECT_NEAR((buttons[0].rect.x0 + buttons[1].rect.x1) * 0.5f, box[0].rect.Cx(), 0.1f);
    EXPECT_LT(buttons[0].rect.y1, box[0].rect.y1);
    std::string words;
    for (int i = 0; i < box[0].selected; ++i)
      words += (i ? " " : "") + box[0].options[static_cast<std::size_t>(i)];
    EXPECT_EQ(words, box[0].label);
  }
}
