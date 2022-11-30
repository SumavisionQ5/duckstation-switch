// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.
#include "context.h"
#include "../log.h"
#include "common/assert.h"
Log_SetChannel(Deko3DContext);

namespace Deko3D {

constexpr size_t GeneralHeapSize = 1024 * 1024 * 128;
constexpr size_t ImageHeapSize = 1024 * 1024 * 128;
constexpr size_t ShaderHeapSize = 1024 * 1024 * 32;

constexpr u32 TextureUploadBufferSize = 1024 * 1024 * 32;

void DebugOut(void* userData, const char* context, DkResult result, const char* message)
{
  Log_DebugPrintf("%s -> %d\n", message, result);
}

void CmdBufAddMem(void* userData, DkCmdBuf cmdbuf, size_t minReqSize)
{
  Context* ctx = reinterpret_cast<Context*>(userData);
  DebugAssert(ctx->GetCmdBuf() == cmdbuf);
  ctx->AddCommandBufferMemory(minReqSize);
}

Context::Context(dk::Device device)
  : m_device(device), m_queue(dk::QueueMaker{device}.setFlags(DkQueueFlags_Graphics).create()),
    m_general_heap(device, GeneralHeapSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1024),
    m_image_heap(device, ImageHeapSize, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 1024),
    m_shader_heap(device, ShaderHeapSize,
                  DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 512)
{
  for (int i = 0; i < NumCmdBufSegments; i++)
  {
    m_frame_resources[i].cmdbuf = dk::CmdBufMaker{device}.setUserData(this).setCbAddMem(CmdBufAddMem).create();
  }
}

Context::~Context()
{
  WaitGPUIdle();

  for (int i = 0; i < NumCmdBufSegments; i++)
    m_frame_resources[i].cmdbuf.destroy();

  m_general_heap.Destroy();
  m_image_heap.Destroy();
  m_shader_heap.Destroy();

  m_queue.destroy();
  m_device.destroy();
}

bool Context::Create(const WindowInfo* wi)
{
  dk::Device device = dk::DeviceMaker{}
                        .setFlags(DkDeviceFlags_DepthZeroToOne | DkDeviceFlags_OriginLowerLeft)
                        .setCbDebug(DebugOut)
                        .create();

  g_deko3d_context.reset(new Context(device));

  g_deko3d_context->ActivateCommandBuffer(0);

  g_deko3d_context->m_texture_upload_buffer.Create(TextureUploadBufferSize);

  return true;
}

void Context::Destroy()
{
  g_deko3d_context->WaitGPUIdle();
  g_deko3d_context->m_texture_upload_buffer.Destroy(false);

  g_deko3d_context.reset();
}

void Context::WaitGPUIdle()
{
  m_queue.waitIdle();
}

void Context::AddCommandBufferMemory(size_t minSize)
{
  FrameResources& resources = m_frame_resources[m_cur_cmd_buf];

  size_t size = std::max<size_t>(minSize, 1024 * 1024);
  MemoryHeap::Allocation mem = m_general_heap.Alloc(size, DK_CMDMEM_ALIGNMENT);
  resources.cmdbuf.addMemory(m_general_heap.GetMemBlock(), mem.offset, size);
  resources.cmd_memory_used.push_back(mem);
}

void Context::WaitForCommandBufferCompletion(u32 index)
{
  m_frame_resources[index].fence.wait();

  const u64 now_completed_counter = m_frame_resources[index].fence_counter;
  u32 cleanup_index = (m_cur_cmd_buf + 1) % NumCmdBufSegments;
  while (cleanup_index != m_cur_cmd_buf)
  {
    FrameResources& resources = m_frame_resources[cleanup_index];
    if (resources.fence_counter > now_completed_counter)
      break;

    if (resources.fence_counter > m_completed_fence_counter)
    {
      for (auto& it : resources.pending_frees)
        std::get<0>(it)->Free(std::get<1>(it));
      resources.pending_frees.clear();
    }

    cleanup_index = (cleanup_index + 1) % NumCmdBufSegments;
  }

  m_completed_fence_counter = now_completed_counter;
}

void Context::SubmitCommandBuffer(dk::Fence* wait_fence, bool flush)
{
  FrameResources& resources = m_frame_resources[m_cur_cmd_buf];

  if (wait_fence)
    m_queue.waitFence(*wait_fence);
  m_queue.submitCommands(resources.cmdbuf.finishList());
  m_queue.signalFence(resources.fence);
  resources.submitted = true;

  if (flush)
    m_queue.flush();
}

void Context::MoveToNextCommandBuffer()
{
  ActivateCommandBuffer((m_cur_cmd_buf + 1) % NumCmdBufSegments);
}

void Context::ActivateCommandBuffer(u32 index)
{
  FrameResources& resources = m_frame_resources[index];

  if (resources.fence_counter > m_completed_fence_counter)
    WaitForCommandBufferCompletion(index);

  resources.submitted = false;

  if (resources.cmd_memory_used.size() > 0)
  {
    // clear rolls back to the start of the last memory block
    // we'll free all the previous blocks
    resources.cmdbuf.clear();
    for (size_t i = 0; i < resources.cmd_memory_used.size() - 1; i++)
      m_general_heap.Free(resources.cmd_memory_used[i]);

    MemoryHeap::Allocation lastBlock = resources.cmd_memory_used[resources.cmd_memory_used.size() - 1];
    resources.cmd_memory_used.clear();
    resources.cmd_memory_used.push_back(lastBlock);
  }

  m_cur_cmd_buf = index;
  resources.fence_counter = m_next_fence_counter++;
}

void Context::ExecuteCommandBuffer(bool wait_for_completion)
{
  const u32 current_buffer = m_cur_cmd_buf;
  SubmitCommandBuffer();
  MoveToNextCommandBuffer();

  if (wait_for_completion)
    WaitForCommandBufferCompletion(current_buffer);
}

void Context::WaitForFenceCounter(u64 fence_counter)
{
  if (m_completed_fence_counter >= fence_counter)
    return;

  // Find the first command buffer which covers this counter value.
  u32 index = (m_cur_cmd_buf + 1) % NumCmdBufSegments;
  while (index != m_cur_cmd_buf)
  {
    if (m_frame_resources[index].fence_counter >= fence_counter)
      break;

    index = (index + 1) % NumCmdBufSegments;
  }

  Assert(index != m_cur_cmd_buf);
  WaitForCommandBufferCompletion(index);
}

void Context::DeferedFree(MemoryHeap* heap, MemoryHeap::Allocation block)
{
  m_frame_resources[m_cur_cmd_buf].pending_frees.push_back(std::make_tuple(heap, block));
}

} // namespace Deko3D

std::unique_ptr<Deko3D::Context> g_deko3d_context;