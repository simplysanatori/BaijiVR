// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

#include "VideoCommon/VR/Perf/VRPerf.h"

struct VideoConfig;

// Dynamic Resolution (GFX_VR_DYNAMIC_RESOLUTION): the Internal Resolution setting is the floor, up to
// two steps more while the GPU has room. The rule is in DynamicResolutionController.h. The setting is
// never written: the video thread adds the steps to it.
namespace VR::DynamicResolution
{
// Host thread, about once a second.
void OnSecond(const Perf::Second& second);

// Video thread, at the end of each frame (UpdateActiveConfig): the EFB scale to draw at. The setting
// with Dynamic Resolution off, and in Immersive and flat Vulkan, which have no GPU time (only
// Stereoscopic 3D's pairs are timed).
int GetEFBScale(const VideoConfig& config);
}  // namespace VR::DynamicResolution

#endif  // ENABLE_VR
