// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.
#pragma once

#include <stdint.h>

#include <deko3d.hpp>

namespace Deko3D {

class MemoryHeap
{
public:
  struct Allocation
  {
    uint32_t blockIdx = 0;
    uint32_t offset = 0, size = 0;
  };

  MemoryHeap(dk::Device device, uint32_t size, uint32_t flags, uint32_t blockPoolSize);
  ~MemoryHeap();

  void Destroy();

  dk::MemBlock GetMemBlock() { return m_memblock; }

  Allocation Alloc(uint32_t size, uint32_t align);
  void Free(Allocation allocation);

  DkGpuAddr GpuAddr(const Allocation& allocation) { return m_memblock.getGpuAddr() + allocation.offset; }

  template<typename T>
  T* CpuAddr(const Allocation& allocation)
  {
    return reinterpret_cast<T*>(static_cast<uint8_t*>(m_memblock.getCpuAddr()) + allocation.offset);
  }

private:
  struct Block
  {
    bool free;
    uint32_t offset, size;
    // it would probably be smarter to make those indices because that's smaller
    Block *siblingLeft, *siblingRight;
    Block *next, *prev;
  };

  // this is a home made memory allocator based on TLSF (http://www.gii.upv.es/tlsf/)
  // I hope it doesn't have to many bugs

  // Free List
  uint32_t m_firstFreeList = 0;
  uint32_t* m_secondFreeListBits;
  Block** m_secondFreeList;

  Block* m_blockPool;
  Block* m_blockPoolUnused = nullptr;

  uint32_t m_used = 0;

  bool m_valid = true;

  void BlockListPushFront(Block*& head, Block* block);
  Block* BlockListPopFront(Block*& head);
  void BlockListRemove(Block*& head, Block* block);
  void MapSizeToSecondLevel(uint32_t size, uint32_t& fl, uint32_t& sl);
  void MarkFree(Block* block);
  void UnmarkFree(Block* block);
  Block* SplitBlockRight(Block* block, uint32_t offset);
  Block* MergeBlocksLeft(Block* block, Block* other);

  dk::MemBlock m_memblock;
};

} // namespace Deko3D
