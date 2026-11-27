// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/Vulkan/VKPerfQuery.h"

#include <algorithm>
#include <cstring>

#include "Common/Assert.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/Timer.h"

#include "VideoBackends/Vulkan/CommandBufferManager.h"
#include "VideoBackends/Vulkan/StateTracker.h"
#include "VideoBackends/Vulkan/VKGfx.h"
#include "VideoBackends/Vulkan/VKTexture.h"
#include "VideoBackends/Vulkan/VulkanContext.h"
#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VideoEvents.h"

namespace Vulkan
{
PerfQuery::PerfQuery() = default;

PerfQuery::~PerfQuery()
{
  if (m_query_pool != VK_NULL_HANDLE)
    vkDestroyQueryPool(g_vulkan_context->GetDevice(), m_query_pool, nullptr);
}

bool PerfQuery::Initialize()
{
  if (!CreateQueryPool())
  {
    PanicAlertFmt("Failed to create query pool");
    return false;
  }

  // Vulkan requires query pools to be reset after creation
  ResetQuery();

  m_frame_end_event =
      GetVideoEvents().after_frame_event.Register([this](Core::System&) { OnFrameEnd(); });
  return true;
}

u32 PerfQuery::GetEFBViewCount()
{
  const auto* efb = static_cast<const VKFramebuffer*>(g_framebuffer_manager->GetEFBFramebuffer());
  return efb && efb->IsMultiview() ? MAX_QUERY_VIEWS : 1;
}

void PerfQuery::EnableQuery(PerfQueryGroup group)
{
  UpdateResultMode();

  // Block if there are no free slots.
  // Otherwise, try to keep half of them available.
  const u32 query_count = m_query_count.load(std::memory_order_relaxed);
  if (query_count > m_ring_size / 2)
    PartialFlush(query_count == m_ring_size);

  const bool nonblocking = m_nonblocking.load(std::memory_order_relaxed);
  if (nonblocking)
  {
    // The slot layout follows the view count, so retire every query of the old layout first.
    const u32 views = GetEFBViewCount();
    if (views != m_query_views)
    {
      if (m_query_count.load(std::memory_order_relaxed) != 0)
        PartialFlush(true);
      m_query_views = views;
    }
  }

  // Ensure command buffer is ready to go before beginning the query, that way we don't submit
  // a buffer with open queries.
  StateTracker::GetInstance()->Bind();

  if (group == PQG_ZCOMP_ZCOMPLOC || group == PQG_ZCOMP)
  {
    ActiveQuery& entry = m_query_buffer[m_query_next_pos];
    DEBUG_ASSERT(!entry.has_value);
    entry.has_value = true;
    entry.query_group = group;
    if (nonblocking)
    {
      entry.period = m_period;
      entry.period_seq = ++m_period_queries;
      entry.efb_width = g_framebuffer_manager->GetEFBWidth();
      entry.efb_height = g_framebuffer_manager->GetEFBHeight();
      m_answer_valid.store(false, std::memory_order_relaxed);
    }

    // Use precise queries if supported, otherwise boolean (which will be incorrect).
    VkQueryControlFlags flags =
        g_vulkan_context->SupportsPreciseOcclusionQueries() ? VK_QUERY_CONTROL_PRECISE_BIT : 0;

    // Ensure the query starts within a render pass.
    StateTracker::GetInstance()->BeginRenderPass();
    vkCmdBeginQuery(g_command_buffer_mgr->GetCurrentCommandBuffer(), m_query_pool,
                    m_query_next_pos * m_query_views, flags);
  }
}

void PerfQuery::DisableQuery(PerfQueryGroup group)
{
  if (group == PQG_ZCOMP_ZCOMPLOC || group == PQG_ZCOMP)
  {
    vkCmdEndQuery(g_command_buffer_mgr->GetCurrentCommandBuffer(), m_query_pool,
                  m_query_next_pos * m_query_views);
    ActiveQuery& entry = m_query_buffer[m_query_next_pos];
    entry.fence_counter = g_command_buffer_mgr->GetCurrentFenceCounter();

    m_query_next_pos = (m_query_next_pos + 1) % m_ring_size;
    m_query_count.fetch_add(1, std::memory_order_relaxed);
  }
}

void PerfQuery::ResetQuery()
{
  UpdateResultMode();
  if (!m_nonblocking.load(std::memory_order_relaxed))
  {
    ResetAllQueries();
    return;
  }

  // The queries of the previous period keep counting for it on the GPU; a new period starts.
  m_period++;
  m_period_index++;
  m_period_queries = 0;
  for (auto& result : m_results)
    result.store(0, std::memory_order_relaxed);
  m_answer_valid.store(false, std::memory_order_relaxed);
}

void PerfQuery::ResetAllQueries()
{
  m_query_count.store(0, std::memory_order_relaxed);
  m_query_readback_pos = 0;
  m_query_next_pos = 0;
  for (size_t i = 0; i < m_results.size(); ++i)
    m_results[i].store(0, std::memory_order_relaxed);

  // Reset entire query pool, ensuring all queries are ready to write to.
  StateTracker::GetInstance()->EndRenderPass();
  vkCmdResetQueryPool(g_command_buffer_mgr->GetCurrentCommandBuffer(), m_query_pool, 0,
                      m_nonblocking.load(std::memory_order_relaxed) ?
                          NONBLOCKING_QUERY_BUFFER_SIZE * MAX_QUERY_VIEWS :
                          PERF_QUERY_BUFFER_SIZE);

  std::memset(m_query_buffer.data(), 0, sizeof(ActiveQuery) * m_ring_size);
}

u32 PerfQuery::GetQueryResult(PerfQueryType type)
{
  u32 result = 0;
  if (type == PQ_ZCOMP_INPUT_ZCOMPLOC || type == PQ_ZCOMP_OUTPUT_ZCOMPLOC)
  {
    result = m_results[PQG_ZCOMP_ZCOMPLOC].load(std::memory_order_relaxed);
  }
  else if (type == PQ_ZCOMP_INPUT || type == PQ_ZCOMP_OUTPUT)
  {
    result = m_results[PQG_ZCOMP].load(std::memory_order_relaxed);
  }
  else if (type == PQ_BLEND_INPUT)
  {
    result = m_results[PQG_ZCOMP].load(std::memory_order_relaxed) +
             m_results[PQG_ZCOMP_ZCOMPLOC].load(std::memory_order_relaxed);
  }
  else if (type == PQ_EFB_COPY_CLOCKS)
  {
    result = m_results[PQG_EFB_COPY_CLOCKS].load(std::memory_order_relaxed);
  }

  return result / 4;
}

void PerfQuery::FlushResults()
{
  if (m_query_count.load(std::memory_order_relaxed) != 0)
    PartialFlush(true);

  ASSERT(m_query_count.load(std::memory_order_relaxed) == 0);
}

bool PerfQuery::IsFlushed() const
{
  if (m_nonblocking.load(std::memory_order_relaxed))
    return m_answer_valid.load(std::memory_order_acquire);
  return m_query_count.load(std::memory_order_relaxed) == 0;
}

bool PerfQuery::UseNonBlockingResults() const
{
  return m_nonblocking.load(std::memory_order_relaxed);
}

void PerfQuery::UpdateResultMode()
{
  const bool nonblocking =
      g_ActiveConfig.VRSessionActive() && g_ActiveConfig.vr_non_blocking_perf_queries;
  if (nonblocking == m_nonblocking.load(std::memory_order_relaxed))
    return;

  // Retire every query under the old rules, then start over.
  if (m_query_count.load(std::memory_order_relaxed) != 0)
    PartialFlush(true);
  m_markers.clear();
  m_recorded.clear();
  m_period++;
  m_period_index = 0;
  m_period_queries = 0;
  m_readback_period = ~u64{0};
  m_readback_seq = 0;
  m_readback_sums = {};
  m_nonblocking_stats = {};
  m_answer_valid.store(false, std::memory_order_relaxed);
  m_ring_size = nonblocking ? NONBLOCKING_QUERY_BUFFER_SIZE : PERF_QUERY_BUFFER_SIZE;
  m_query_views = 1;
  m_nonblocking.store(nonblocking, std::memory_order_relaxed);
  ResetAllQueries();
}

void PerfQuery::SetAnswer(const GroupSums& sums)
{
  for (size_t i = 0; i < m_results.size(); i++)
    m_results[i].store(static_cast<u32>(sums[i]), std::memory_order_relaxed);
  m_answer_valid.store(true, std::memory_order_release);
}

// Games read the pixel counters right after drawing what they measure, so a read waits for the
// GPU to finish every earlier command. Super Mario Sunshine does it for 7 one-draw visibility
// tests per frame when goop is on screen. A test is identified by its period's index in the frame
// (resets since the frame started) and its query count. If its queries are done, the answer is
// exact; if not, the same test of the previous frame answers it, and a marker records this frame's
// result once the GPU has it. Only the first occurrence of a test waits.
void PerfQuery::PrepareNonBlockingResult()
{
  // The setting was turned off since the read was queued: answer the blocking way.
  if (!m_nonblocking.load(std::memory_order_relaxed))
  {
    FlushResults();
    return;
  }

  // Collect what the GPU has finished, without waiting.
  ReadbackQueries();

  const u32 queries = m_period_queries;
  if (queries == 0)
  {
    m_nonblocking_stats.exact++;
    SetAnswer({});
    return;
  }
  if (m_readback_period == m_period && m_readback_seq >= queries)
  {
    m_nonblocking_stats.exact++;
    m_recorded[m_period_index] = {queries, m_readback_sums};
    SetAnswer(m_readback_sums);
    return;
  }

  const auto recorded = m_recorded.find(m_period_index);
  if (recorded != m_recorded.end() && recorded->second.queries == queries)
  {
    m_nonblocking_stats.previous++;
    m_markers.push_back({m_period, queries, m_period_index});
    SetAnswer(recorded->second.sums);
    return;
  }

  // A test not seen before: wait for it like the blocking path.
  PartialFlush(true);
  m_nonblocking_stats.waited++;
  const GroupSums sums = m_readback_period == m_period ? m_readback_sums : GroupSums{};
  m_recorded[m_period_index] = {queries, sums};
  SetAnswer(sums);
}

void PerfQuery::OnFrameAborted()
{
  m_period_index = 0;
}

void PerfQuery::OnStateLoaded()
{
  // The queries still on the GPU are read back as ever, but answer no test of the loaded state.
  m_markers.clear();
  m_recorded.clear();
  m_period_index = 0;
}

void PerfQuery::OnFrameEnd()
{
  m_period_index = 0;
  if (!m_nonblocking.load(std::memory_order_relaxed))
    return;

  // Keep the ring drained, so the markers of this frame's tests are recorded before they are read.
  ReadbackQueries();

  auto& stats = m_nonblocking_stats;
  const u64 now_us = Common::Timer::NowUs();
  if (stats.start_us == 0)
    stats.start_us = now_us;
  stats.frames++;
  if (now_us - stats.start_us < 5'000'000)
    return;
  const double frames = static_cast<double>(stats.frames);
  INFO_LOG_FMT(VIDEO,
               "NonBlockingPerfQuery: {:.1f} f/s | reads/f exact={:.1f} previous frame={:.1f} "
               "waited={:.1f} | tests recorded={} markers={} queries in flight={}",
               frames * 1'000'000.0 / (now_us - stats.start_us), stats.exact / frames,
               stats.previous / frames, stats.waited / frames, m_recorded.size(), m_markers.size(),
               m_query_count.load(std::memory_order_relaxed));
  stats = {};
  stats.start_us = now_us;
}

bool PerfQuery::CreateQueryPool()
{
  VkQueryPoolCreateInfo info = {
      VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,  // VkStructureType                  sType
      nullptr,                                   // const void*                      pNext
      0,                                         // VkQueryPoolCreateFlags           flags
      VK_QUERY_TYPE_OCCLUSION,                   // VkQueryType                      queryType
      // Blocking results only use the first PERF_QUERY_BUFFER_SIZE.
      NONBLOCKING_QUERY_BUFFER_SIZE * MAX_QUERY_VIEWS,  // uint32_t                  queryCount
      0  // VkQueryPipelineStatisticFlags    pipelineStatistics;
  };

  VkResult res = vkCreateQueryPool(g_vulkan_context->GetDevice(), &info, nullptr, &m_query_pool);
  if (res != VK_SUCCESS)
  {
    LOG_VULKAN_ERROR(res, "vkCreateQueryPool failed: ");
    return false;
  }

  return true;
}

void PerfQuery::ReadbackQueries()
{
  const bool nonblocking = m_nonblocking.load(std::memory_order_relaxed);
  u64 completed_fence_counter = g_command_buffer_mgr->GetCompletedFenceCounter();

  // Need to save these since ProcessResults will modify them.
  const u32 outstanding_queries = m_query_count.load(std::memory_order_relaxed);
  u32 readback_count = 0;
  for (u32 i = 0; i < outstanding_queries; i++)
  {
    u32 index = (m_query_readback_pos + readback_count) % m_ring_size;
    const ActiveQuery& entry = m_query_buffer[index];
    if (entry.fence_counter > completed_fence_counter)
    {
      // Nothing waits for these, so ask whether the GPU is done with them.
      if (!nonblocking || !g_command_buffer_mgr->PollFenceCounter(entry.fence_counter))
        break;
      completed_fence_counter = std::max(g_command_buffer_mgr->GetCompletedFenceCounter(),
                                         entry.fence_counter);
    }

    // If this wrapped around, we need to flush the entries before the end of the buffer.
    if (index < m_query_readback_pos)
    {
      ReadbackQueries(readback_count);
      DEBUG_ASSERT(m_query_readback_pos == 0);
      readback_count = 0;
    }

    readback_count++;
  }

  if (readback_count > 0)
    ReadbackQueries(readback_count);
}

void PerfQuery::ReadbackQueries(u32 query_count)
{
  // Should be at maximum query_count queries pending.
  ASSERT(query_count <= m_query_count.load(std::memory_order_relaxed) &&
         (m_query_readback_pos + query_count) <= m_ring_size);

  // Read back from the GPU.
  const u32 views = m_query_views;
  VkResult res = vkGetQueryPoolResults(
      g_vulkan_context->GetDevice(), m_query_pool, m_query_readback_pos * views,
      query_count * views, query_count * views * sizeof(PerfQueryDataType),
      m_query_result_buffer.data(), sizeof(PerfQueryDataType), VK_QUERY_RESULT_WAIT_BIT);
  if (res != VK_SUCCESS)
    LOG_VULKAN_ERROR(res, "vkGetQueryPoolResults failed: ");

  const bool nonblocking = m_nonblocking.load(std::memory_order_relaxed);
  if (nonblocking)
  {
    // These queries belong to completed command buffers, and the current one has not begun any of
    // them: reset in its init part, they leave the render pass open.
    vkCmdResetQueryPool(g_command_buffer_mgr->GetCurrentInitCommandBuffer(), m_query_pool,
                        m_query_readback_pos * views, query_count * views);
  }
  else
  {
    StateTracker::GetInstance()->EndRenderPass();
    vkCmdResetQueryPool(g_command_buffer_mgr->GetCurrentCommandBuffer(), m_query_pool,
                        m_query_readback_pos, query_count);
  }

  // Remove pending queries.
  for (u32 i = 0; i < query_count; i++)
  {
    u32 index = (m_query_readback_pos + i) % m_ring_size;
    ActiveQuery& entry = m_query_buffer[index];

    // Should have a fence associated with it (waiting for a result).
    DEBUG_ASSERT(entry.fence_counter != 0);
    entry.fence_counter = 0;
    entry.has_value = false;

    if (nonblocking)
    {
      // Sum per period, and record the sums the markers wait for.
      // A multiview query reports either one count per view or the total in its first slot.
      u64 samples = 0;
      for (u32 view = 0; view < views; view++)
        samples += m_query_result_buffer[i * views + view];
      // Like the blocking results below, this counts every EFB layer (two in stereo).
      u64 result = samples * EFB_WIDTH / entry.efb_width * EFB_HEIGHT / entry.efb_height;
      if (g_ActiveConfig.iMultisamples > 1)
        result /= g_ActiveConfig.iMultisamples;

      if (entry.period != m_readback_period)
      {
        m_readback_period = entry.period;
        m_readback_sums = {};
      }
      m_readback_sums[entry.query_group] += result;
      m_readback_seq = entry.period_seq;
      while (!m_markers.empty())
      {
        const Marker& marker = m_markers.front();
        if (marker.period > m_readback_period ||
            (marker.period == m_readback_period && marker.queries > m_readback_seq))
        {
          break;
        }
        if (marker.period == m_readback_period && marker.queries == m_readback_seq)
          m_recorded[marker.key] = {marker.queries, m_readback_sums};
        m_markers.pop_front();
      }
      continue;
    }

    // NOTE: Reported pixel metrics should be referenced to native resolution
    u64 native_res_result = static_cast<u64>(m_query_result_buffer[i]) * EFB_WIDTH /
                            g_framebuffer_manager->GetEFBWidth() * EFB_HEIGHT /
                            g_framebuffer_manager->GetEFBHeight();
    if (g_ActiveConfig.iMultisamples > 1)
      native_res_result /= g_ActiveConfig.iMultisamples;
    m_results[entry.query_group].fetch_add(static_cast<u32>(native_res_result),
                                           std::memory_order_relaxed);
  }

  m_query_readback_pos = (m_query_readback_pos + query_count) % m_ring_size;
  m_query_count.fetch_sub(query_count, std::memory_order_relaxed);
}

void PerfQuery::PartialFlush(bool blocking)
{
  // Submit a command buffer in the background if the front query is not bound to one.
  if (blocking || m_query_buffer[m_query_readback_pos].fence_counter ==
                      g_command_buffer_mgr->GetCurrentFenceCounter())
  {
    VKGfx::GetInstance()->ExecuteCommandBuffer(true, blocking);
  }

  ReadbackQueries();
}
}  // namespace Vulkan
