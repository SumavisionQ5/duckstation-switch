#pragma once

#include "../types.h"
#include "memory_heap.h"
#include <deque>
#include <memory>

namespace Deko3D {

class StreamBuffer
{
public:
  StreamBuffer();
  StreamBuffer(StreamBuffer&& move);
  StreamBuffer(const StreamBuffer&) = delete;
  ~StreamBuffer();

  StreamBuffer& operator=(StreamBuffer&& move);
  StreamBuffer& operator=(const StreamBuffer&) = delete;

  ALWAYS_INLINE bool IsValid() const { return m_buffer.size > 0; }
  ALWAYS_INLINE MemoryHeap::Allocation GetBuffer() const { return m_buffer; }
  ALWAYS_INLINE const MemoryHeap::Allocation* GetBufferPointer() const { return &m_buffer; }
  ALWAYS_INLINE u8* GetHostPointer() const { return m_host_pointer; }
  ALWAYS_INLINE u8* GetCurrentHostPointer() const { return m_host_pointer + m_current_offset; }
  ALWAYS_INLINE u32 GetCurrentSize() const { return m_size; }
  ALWAYS_INLINE u32 GetCurrentSpace() const { return m_current_space; }
  ALWAYS_INLINE u32 GetCurrentOffset() const { return m_current_offset; }

  bool Create(u32 size);
  void Destroy(bool defer);

  bool ReserveMemory(u32 num_bytes, u32 alignment);
  void CommitMemory(u32 final_num_bytes);

  bool AllocateBuffer(u32 size);
  void UpdateCurrentFencePosition();
  void UpdateGPUPosition();

  // Waits for as many fences as needed to allocate num_bytes bytes from the buffer.
  bool WaitForClearSpace(u32 num_bytes);

  u32 m_size = 0;
  u32 m_current_offset = 0;
  u32 m_current_space = 0;
  u32 m_current_gpu_position = 0;
  MemoryHeap::Allocation m_buffer;

  u8* m_host_pointer = nullptr;

  // List of fences and the corresponding positions in the buffer
  std::deque<std::pair<u64, u32>> m_tracked_fences;
};

} // namespace Deko3D
