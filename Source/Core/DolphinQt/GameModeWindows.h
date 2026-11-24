// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// SPDX-License-Identifier: GPL-3.0-or-later

// The windows Baiji opens in Steam's Game Mode (dialogs, message boxes, file dialogs, Settings,
// Mapping, a game's Properties...) sized for SteamVR's theater screen. gamescope shows the newest
// window as the whole picture, scaled to fit the 16:9 screen (Common/WindowFit.h has the rule).
// Only active when Host::IsGameMode(); nothing changes on a desktop.

#pragma once

class QWidget;

namespace GameModeWindows
{
// Fits every top-level window other than `main` (and popups, tooltips, the render window) on its
// Show event, and once more right after (a message box sets its size in its own showEvent).
void Install(QWidget* main);

// Resizes `window` to the Game Mode size for `main` and centres it over `main`. `first` (the Show
// event): a window whose minimum size is larger than the area gets its content in a scroll area,
// the dialog buttons staying under it. GUI thread.
void Fit(QWidget* window, const QWidget* main, bool first);

// The content of `window` (its layout but the last QDialogButtonBox) into a scroll area. False,
// changing nothing, for Qt's own dialogs, a window without a plain layout, or when done already.
bool AddScrollArea(QWidget* window);
}  // namespace GameModeWindows
