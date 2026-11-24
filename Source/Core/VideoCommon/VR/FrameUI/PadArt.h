// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "VideoCommon/VR/FrameUI/md3.h"

// The controller diagrams of the VR menu's Controls tab: the emulated controller with a callout for
// each control that says what presses it. Everything is drawn as shapes, at any size. No OpenXR
// here.
namespace VR::UI
{
enum class PadKind
{
  GameCube,
  WiiRemote,
  WiiRemoteSideways,
  WiiNunchuk,
};

enum class PadControl
{
  A,
  B,
  X,
  Y,
  Z,
  L,
  R,
  Start,
  Stick,
  CStick,
  DPad,
  One,
  Two,
  Plus,
  Minus,
  Home,
  Pointer,
  Shake,
  C,
  NunchukZ,
  NunchukStick,
  Count,
};

struct PadLabels
{
  std::array<std::string, static_cast<size_t>(PadControl::Count)> text;

  std::string& operator[](PadControl control) { return text[static_cast<size_t>(control)]; }
  const std::string& operator[](PadControl control) const
  {
    return text[static_cast<size_t>(control)];
  }
  bool operator==(const PadLabels&) const = default;
};

// Fits the diagram in `area` (dp), over what is there. A control with an empty label is drawn
// without a callout; labels of controls the controller does not have are ignored. Draw from one
// thread, as the kit.
void DrawPad(FrameXr::Menu::Kit* kit, FrameXr::Menu::Canvas* canvas,
             const FrameXr::Menu::Rect& area, PadKind kind, const PadLabels& labels);
}  // namespace VR::UI
