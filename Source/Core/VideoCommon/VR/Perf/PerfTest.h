// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

#include <string>

#include "VideoCommon/VR/Perf/VRPerf.h"

// The performance test: Config/perf-test.txt in the user folder has steps (a name and settings),
// and a running game goes through them in turns. perf.csv has a line per second.
namespace VR::Perf::Test
{
// Any thread; cheap.
bool IsRequested();
// The host thread's.
void OnSecond(const Second& second);
std::string GetStepName();
}  // namespace VR::Perf::Test

#endif  // ENABLE_VR
