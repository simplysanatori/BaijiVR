// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

#include "VideoCommon/VR/Perf/VRPerf.h"

// Dynamic Resolution (GFX_VR_DYNAMIC_RESOLUTION): the Internal Resolution setting is the floor, up to
// two steps more while the GPU has room. Everything here runs on the host thread.
namespace VR::DynamicResolution
{
void OnSecond(const Perf::Second& second);
}  // namespace VR::DynamicResolution

#endif  // ENABLE_VR
