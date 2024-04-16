#pragma once

#include "common/types.h"

#include "deko3d_memory_heap.h"
#include <deque>
#include <memory>

class Deko3DDevice;

class Deko3DStreamBuffer
{
public:
  ~Deko3DStreamBuffer();

  ALWAYS_INLINE bool IsValid() const { return m_buffer.size > 0; }
  ALWAYS_INLINE Deko3DMemoryHeap::Allocation GetBuffer() const { return m_buffer; }
  ALWAYS_INLINE const Deko3DMemoryHeap::Allocation* GetBufferPointer() const { return &m_buffer; }
  ALWAYS_INLINE u8* GetHostPointer() const { return m_host_pointer; }
  ALWAYS_INLINE u8* GetCurrentHostPointer() const { return m_host_pointer + m_current_offset; }
  ALWAYS_INLINE u32 GetCurrentSize() const { return m_buffer.size; }
  ALWAYS_INLINE u32 GetCurrentSpace() const { return m_current_space; }
  ALWAYS_INLINE u32 GetCurrentOffset() const { return m_current_offset; }
  ALWAYS_INLINE DkGpuAddr GetCurrentPointer() const { return m_pointer + m_current_offset; }
  ALWAYS_INLINE DkGpuAddr GetPointer() const { return m_pointer; }

  bool ReserveMemory(u32 num_bytes, u32 alignment);
  void CommitMemory(u32 final_num_bytes);

  void UpdateCurrentFencePosition();
  void UpdateGPUPosition();

  static std::unique_ptr<Deko3DStreamBuffer> Create(u32 size);
private:
  Deko3DStreamBuffer(Deko3DMemoryHeap::Allocation buffer);

  // Waits for as many fences as needed to allocate num_bytes bytes from the buffer.
  bool WaitForClearSpace(u32 num_bytes);

  u32 m_current_offset = 0;
  u32 m_current_space = 0;
  u32 m_current_gpu_position = 0;
  Deko3DMemoryHeap::Allocation m_buffer;

  u8* m_host_pointer = nullptr;
  DkGpuAddr m_pointer = DK_GPU_ADDR_INVALID;

  // List of fences and the corresponding positions in the buffer
  std::deque<std::pair<u64, u32>> m_tracked_fences;
};
