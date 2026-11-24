// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

#include <openxr/openxr.h>

#include "Common/CommonTypes.h"

namespace VR::UI
{
// A swapchain for a quad layer whose pixels come from the CPU. Nothing here waits for the GPU or
// the runtime (except the destructor), so it may change on the XR pacing thread while a game runs:
// a copy is submitted with a fence and its image released on a later frame, once the fence has
// signalled. Until then the layer shows the image released before. All calls on that thread.
class Overlay
{
public:
  virtual ~Overlay() = default;

  // Queues Width() * Height() * 4 bytes: premultiplied sRGB RGBA, rows top down. While a copy is
  // in flight only the newest pixels queued are kept.
  virtual void Upload(const u8* rgba) = 0;

  // Once a frame, not while holding the video handoff lock: releases a finished copy, starts the
  // next.
  virtual void Poll() = 0;

  // Released at least once: may be submitted.
  virtual bool HasImage() const = 0;
  // The image shown is not yet the newest Upload.
  virtual bool IsBusy() const = 0;

  virtual XrSwapchain GetSwapchain() const = 0;
  virtual u32 GetWidth() const = 0;
  virtual u32 GetHeight() const = 0;
};
}  // namespace VR::UI

#endif  // ENABLE_VR
