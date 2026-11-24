// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef ENABLE_VR

#include "VideoBackends/Vulkan/VulkanLoader.h"

#define XR_USE_GRAPHICS_API_VULKAN

#include "VideoBackends/Vulkan/VulkanOverlay.h"

#include <cstring>
#include <utility>
#include <vector>

#include <openxr/openxr_platform.h>

#include "Common/Logging/Log.h"

#include "VideoBackends/Vulkan/VulkanContext.h"

namespace Vulkan
{
namespace
{
// The same scheme as Azahar for Frame's vis/async_overlay.cpp.
class VulkanOverlay final : public VR::UI::Overlay
{
public:
  explicit VulkanOverlay(QueueLockFunction queue_lock) : m_queue_lock(std::move(queue_lock)) {}

  ~VulkanOverlay() override { Destroy(); }

  bool Create(XrSession session, u32 width, u32 height)
  {
    m_width = width;
    m_height = height;
    m_device = g_vulkan_context->GetDevice();

    VkFormat format;
    if (!SelectFormat(session, &format))
      return false;

    XrSwapchainCreateInfo create_info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create_info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                             XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.format = static_cast<int64_t>(format);
    create_info.sampleCount = 1;
    create_info.width = width;
    create_info.height = height;
    create_info.faceCount = 1;
    create_info.arraySize = 1;
    create_info.mipCount = 1;
    XrResult result;
    {
      auto lock = m_queue_lock();
      result = xrCreateSwapchain(session, &create_info, &m_swapchain);
    }
    if (XR_FAILED(result))
    {
      WARN_LOG_FMT(VIDEO, "OpenXR: UI swapchain {}x{} failed ({}).", width, height,
                   static_cast<int>(result));
      m_swapchain = XR_NULL_HANDLE;
      return false;
    }

    u32 count = 0;
    xrEnumerateSwapchainImages(m_swapchain, 0, &count, nullptr);
    std::vector<XrSwapchainImageVulkanKHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    if (count == 0 || XR_FAILED(xrEnumerateSwapchainImages(
                          m_swapchain, count, &count,
                          reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))))
    {
      return false;
    }
    for (const auto& image : images)
      m_images.push_back(image.image);

    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = g_vulkan_context->GetGraphicsQueueFamilyIndex();
    if (vkCreateCommandPool(m_device, &pool_info, nullptr, &m_pool) != VK_SUCCESS)
      return false;

    VkCommandBufferAllocateInfo buffer_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    buffer_info.commandPool = m_pool;
    buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    buffer_info.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &buffer_info, &m_command_buffer) != VK_SUCCESS)
      return false;

    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(m_device, &fence_info, nullptr, &m_fence) != VK_SUCCESS)
      return false;

    return CreateStaging();
  }

  void Upload(const u8* rgba) override
  {
    std::memcpy(m_pending.data(), rgba, m_pending.size());
    m_has_pending = true;
  }

  void Poll() override
  {
    if (m_in_flight && vkGetFenceStatus(m_device, m_fence) == VK_SUCCESS)
    {
      vkResetFences(m_device, 1, &m_fence);
      m_has_image |= Release();
      m_in_flight = false;
    }
    if (m_has_pending && !m_in_flight)
      Start();
  }

  bool HasImage() const override { return m_has_image; }
  bool IsBusy() const override { return m_has_pending || m_in_flight; }
  XrSwapchain GetSwapchain() const override { return m_swapchain; }
  u32 GetWidth() const override { return m_width; }
  u32 GetHeight() const override { return m_height; }

private:
  static bool SelectFormat(XrSession session, VkFormat* out_format)
  {
    u32 count = 0;
    if (XR_FAILED(xrEnumerateSwapchainFormats(session, 0, &count, nullptr)) || count == 0)
      return false;
    std::vector<int64_t> formats(count);
    if (XR_FAILED(xrEnumerateSwapchainFormats(session, count, &count, formats.data())))
      return false;

    // The pixels are sRGB-encoded.
    for (const VkFormat wanted : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM})
    {
      for (const int64_t format : formats)
      {
        if (format == static_cast<int64_t>(wanted))
        {
          *out_format = wanted;
          return true;
        }
      }
    }
    return false;
  }

  bool CreateStaging()
  {
    const VkDeviceSize size = VkDeviceSize{m_width} * m_height * 4;
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &buffer_info, nullptr, &m_staging) != VK_SUCCESS)
      return false;

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(m_device, m_staging, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(g_vulkan_context->GetPhysicalDevice(), &properties);
    constexpr VkMemoryPropertyFlags wanted =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (u32 i = 0; i < properties.memoryTypeCount; ++i)
    {
      if ((requirements.memoryTypeBits & (1u << i)) == 0 ||
          (properties.memoryTypes[i].propertyFlags & wanted) != wanted)
      {
        continue;
      }
      VkMemoryAllocateInfo allocate_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      allocate_info.allocationSize = requirements.size;
      allocate_info.memoryTypeIndex = i;
      if (vkAllocateMemory(m_device, &allocate_info, nullptr, &m_staging_memory) != VK_SUCCESS ||
          vkBindBufferMemory(m_device, m_staging, m_staging_memory, 0) != VK_SUCCESS ||
          vkMapMemory(m_device, m_staging_memory, 0, VK_WHOLE_SIZE, 0, &m_staging_map) !=
              VK_SUCCESS)
      {
        return false;
      }
      m_pending.assign(static_cast<std::size_t>(size), 0);
      return true;
    }
    return false;
  }

  bool Release()
  {
    XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    auto lock = m_queue_lock();
    return XR_SUCCEEDED(xrReleaseSwapchainImage(m_swapchain, &release_info));
  }

  void Start()
  {
    if (!m_acquired)
    {
      XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
      auto lock = m_queue_lock();
      if (XR_FAILED(xrAcquireSwapchainImage(m_swapchain, &acquire_info, &m_index)))
        return;
      m_acquired = true;
    }

    // A busy image is tried again next frame.
    XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait_info.timeout = 0;
    const XrResult wait_result = xrWaitSwapchainImage(m_swapchain, &wait_info);
    if (wait_result == XR_TIMEOUT_EXPIRED || XR_FAILED(wait_result))
      return;
    m_acquired = false;

    std::memcpy(m_staging_map, m_pending.data(), m_pending.size());
    m_has_pending = false;

    vkResetCommandBuffer(m_command_buffer, 0);
    VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_command_buffer, &begin_info);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // every texel is rewritten
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_images[m_index];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(m_command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {m_width, m_height, 1};
    vkCmdCopyBufferToImage(m_command_buffer, m_staging, m_images[m_index],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(m_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
    vkEndCommandBuffer(m_command_buffer);

    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &m_command_buffer;
    VkResult result;
    {
      auto lock = m_queue_lock();
      result = vkQueueSubmit(g_vulkan_context->GetGraphicsQueue(), 1, &submit_info, m_fence);
    }
    if (result != VK_SUCCESS)
    {
      // Nothing will signal the fence. The image holds nothing new, so it is not counted.
      ERROR_LOG_FMT(VIDEO, "OpenXR: UI copy failed ({}).", static_cast<int>(result));
      Release();
      return;
    }
    m_in_flight = true;
  }

  void Destroy()
  {
    if (m_in_flight)
    {
      vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, 1'000'000'000);
      Release();
      m_in_flight = false;
    }
    if (m_swapchain != XR_NULL_HANDLE)
    {
      auto lock = m_queue_lock();
      xrDestroySwapchain(m_swapchain);
      m_swapchain = XR_NULL_HANDLE;
    }
    if (m_staging_map)
      vkUnmapMemory(m_device, m_staging_memory);
    if (m_staging != VK_NULL_HANDLE)
      vkDestroyBuffer(m_device, m_staging, nullptr);
    if (m_staging_memory != VK_NULL_HANDLE)
      vkFreeMemory(m_device, m_staging_memory, nullptr);
    if (m_fence != VK_NULL_HANDLE)
      vkDestroyFence(m_device, m_fence, nullptr);
    if (m_pool != VK_NULL_HANDLE)
      vkDestroyCommandPool(m_device, m_pool, nullptr);
  }

  QueueLockFunction m_queue_lock;
  VkDevice m_device = VK_NULL_HANDLE;
  VkCommandPool m_pool = VK_NULL_HANDLE;
  VkCommandBuffer m_command_buffer = VK_NULL_HANDLE;
  VkFence m_fence = VK_NULL_HANDLE;
  VkBuffer m_staging = VK_NULL_HANDLE;
  VkDeviceMemory m_staging_memory = VK_NULL_HANDLE;
  void* m_staging_map = nullptr;

  XrSwapchain m_swapchain = XR_NULL_HANDLE;
  std::vector<VkImage> m_images;
  u32 m_width = 0;
  u32 m_height = 0;
  u32 m_index = 0;
  std::vector<u8> m_pending;
  bool m_has_pending = false;
  bool m_acquired = false;   // an image acquired, not yet waited for
  bool m_in_flight = false;  // a copy submitted, its image not yet released
  bool m_has_image = false;
};
}  // namespace

std::unique_ptr<VR::UI::Overlay> CreateVulkanOverlay(XrSession session, u32 width, u32 height,
                                                     QueueLockFunction queue_lock)
{
  auto overlay = std::make_unique<VulkanOverlay>(std::move(queue_lock));
  if (!overlay->Create(session, width, height))
    return nullptr;
  return overlay;
}
}  // namespace Vulkan

#endif  // ENABLE_VR
