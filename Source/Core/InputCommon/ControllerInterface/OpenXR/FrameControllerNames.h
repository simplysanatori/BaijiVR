// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

// The Steam Frame controllers' inputs, named as they are printed on them. Configs and profiles
// written before used Quest Touch names ("Right Button A", "Left Button Squeeze").
namespace ciface::OpenXR
{
// The present name of an input, given either name.
std::string_view GetFrameInputName(std::string_view name);

// Rewrites the old names in a control expression, and moves what an old default bound to a grip
// (they move the screen now) or to a button the Frame does not have. `control` is the config key
// ("Buttons/Z", "Nunchuk/Buttons/C"). Expressions without old names are left as they are.
void UpdateFrameExpression(std::string_view control, std::string* expression);
}  // namespace ciface::OpenXR
