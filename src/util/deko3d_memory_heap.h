// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.
#pragma once

#include "common/types.h"

#include <stdint.h>

#include <deko3d.hpp>

class Deko3DMemoryHeap
{
public:
  Deko3DMemoryHeap();
  ~Deko3DMemoryHeap();

  bool Create(u32 size, u32 flags, u32 block_pool_size);
  void Destroy();

  dk::MemBlock GetMemBlock() { return m_memblock; }

  ALWAYS_INLINE bool IsValid() { return m_memblock; }

  struct Allocation
  {
    u32 blockIdx = 0;
    u32 offset = 0, size = 0;
  };

  Allocation Alloc(u32 size, u32 align);
  void Free(Allocation allocation);

  DkGpuAddr GPUPointer(const Allocation& allocation) { return m_memblock.getGpuAddr() + allocation.offset; }

  template<typename T>
  ALWAYS_INLINE T* CPUPointer(const Allocation& allocation)
  {
    return reinterpret_cast<T*>(static_cast<uint8_t*>(m_memblock.getCpuAddr()) + allocation.offset);
  }

private:
  struct Block
  {
    bool free;
    u32 offset, size;
    // it would probably be smarter to make those indices because that's smaller
    Block *sibling_left, *sibling_right;
    Block *next, *prev;
  };

  // this is a home made memory allocator based on TLSF (http://www.gii.upv.es/tlsf/)
  // I hope it doesn't have to many bugs

  // Free List
  u32 m_first_free_list = 0;
  u32* m_second_free_list_bits;
  Block** m_second_free_list;

  Block* m_block_pool;
  Block* m_block_pool_unused = nullptr;

  u32 m_used = 0;

  void BlockListPushFront(Block*& head, Block* block);
  Block* BlockListPopFront(Block*& head);
  void BlockListRemove(Block*& head, Block* block);
  void MapSizeToSecondLevel(u32 size, u32& fl, u32& sl);
  void MarkFree(Block* block);
  void UnmarkFree(Block* block);
  Block* SplitBlockRight(Block* block, u32 offset);
  Block* MergeBlocksLeft(Block* block, Block* other);

  dk::MemBlock m_memblock;
};
