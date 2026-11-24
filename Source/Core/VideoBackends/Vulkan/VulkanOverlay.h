// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef ENABLE_VR

// VulkanLoader.h must come first: it defines VK_NO_PROTOTYPES before vulkan.h.
#include "VideoBackends/Vulkan/VulkanLoader.h"

#include <functional>
#include <memory>
#include <mutex>

#include "VideoCommon/VR/FrameUI/Overlay.h"

namespace Vulkan
{
// Takes the lock on the queue the XrSession is bound to.
using QueueLockFunction = std::function<std::unique_lock<std::mutex>()>;

// A VR UI layer's swapchain (VR::UI::Overlay), or null when the runtime or the device refuses.
std::unique_ptr<VR::UI::Overlay> CreateVulkanOverlay(XrSession session, u32 width, u32 height,
                                                     QueueLockFunction queue_lock);
}  // namespace Vulkan

#endif  // ENABLE_VR
