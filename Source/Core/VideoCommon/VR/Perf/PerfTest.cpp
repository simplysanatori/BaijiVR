// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/VR/Perf/PerfTest.h"

#ifdef ENABLE_VR

namespace VR::Perf::Test
{
bool IsRequested()
{
  return false;
}

void OnSecond(const Second&)
{
}

std::string GetStepName()
{
  return {};
}
}  // namespace VR::Perf::Test

#endif  // ENABLE_VR
