// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.
#pragma once

#include "../window_info.h"
#include "common/types.h"
#include "memory_heap.h"
#include "swap_chain.h"
#include <array>
#include <deko3d.hpp>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace Deko3D {

class Context
{
public:
  ~Context();
  static bool Create(const WindowInfo* wi);
  static void Destroy();

  MemoryHeap& GetGeneralHeap() { return m_general_heap; }
  MemoryHeap& GetImageHeap() { return m_image_heap; }
  MemoryHeap& GetShaderHeap() { return m_shader_heap; }

  void DeferedFree(MemoryHeap* heap, MemoryHeap::Allocation block);

  ALWAYS_INLINE dk::Device GetDevice() { return m_device; }
  ALWAYS_INLINE dk::Queue GetQueue() { return m_queue; }
  ALWAYS_INLINE dk::CmdBuf GetCmdBuf() { return m_frame_resources[m_cur_cmd_buf].cmdbuf; }

  ALWAYS_INLINE u64 GetCompletedFenceCounter() const { return m_completed_fence_counter; }
  ALWAYS_INLINE u64 GetCurrentFenceCounter() { return m_frame_resources[m_cur_cmd_buf].fence_counter; }

  void SubmitCommandBuffer(dk::Fence* wait_fence = nullptr, bool flush = true);
  void MoveToNextCommandBuffer();
  void ActivateCommandBuffer(u32 index);
  void ExecuteCommandBuffer(bool wait_for_completion);
  void WaitForCommandBufferCompletion(u32 index);

  void WaitForFenceCounter(u64 fence_counter);
  void WaitGPUIdle();

  void AddCommandBufferMemory(size_t minSize);

private:
  Context(dk::Device device);

  static constexpr int NumCmdBufSegments = 2;

  struct FrameResources
  {
    std::vector<MemoryHeap::Allocation> cmd_memory_used;
    dk::Fence fence = {};
    u64 fence_counter = 0;
    dk::CmdBuf cmdbuf;

    std::vector<std::tuple<MemoryHeap*, MemoryHeap::Allocation>> pending_frees;
  };

  dk::Device m_device;
  dk::Queue m_queue;
  MemoryHeap m_general_heap, m_image_heap, m_shader_heap;

  u32 m_cur_cmd_buf = 0;
  std::array<FrameResources, NumCmdBufSegments> m_frame_resources;
  u64 m_completed_fence_counter = 0;
  u64 m_next_fence_counter = 0;
};

} // namespace Deko3D

extern std::unique_ptr<Deko3D::Context> g_deko3d_context;
