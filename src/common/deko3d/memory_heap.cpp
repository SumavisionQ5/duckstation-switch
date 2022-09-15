// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.
#include "memory_heap.h"
#include "common/assert.h"

#include <cstdio>
#include <cstring>

namespace Deko3D {

void MemoryHeap::BlockListPushFront(Block*& head, Block* block)
{
  if (head)
  {
    DebugAssert(head->prev == nullptr);
    head->prev = block;
  }

  block->prev = nullptr;
  block->next = head;
  head = block;
}

MemoryHeap::Block* MemoryHeap::BlockListPopFront(Block*& head)
{
  Block* result = head;
  DebugAssertMsg(result, "popping from empty block list");

  head = result->next;
  if (head)
    head->prev = nullptr;

  return result;
}

void MemoryHeap::BlockListRemove(Block*& head, Block* block)
{
  DebugAssert((head == block) == !block->prev);
  if (!block->prev)
    head = block->next;

  if (block->prev)
    block->prev->next = block->next;
  if (block->next)
    block->next->prev = block->prev;
}

void MemoryHeap::MapSizeToSecondLevel(uint32_t size, uint32_t& fl, uint32_t& sl)
{
  DebugAssertMsg(size >= 32, "block smaller than 32 bytes? Maybe freeing uninitialized block?");
  fl = 31 - __builtin_clz(size);
  sl = (size - (1 << fl)) >> (fl - 5);
}

void MemoryHeap::MarkFree(Block* block)
{
  DebugAssert(!block->free);
  block->free = true;
  uint32_t fl, sl;
  MapSizeToSecondLevel(block->size, fl, sl);

  BlockListPushFront(m_secondFreeList[(fl - 5) * 32 + sl], block);

  m_firstFreeList |= 1 << (fl - 5);
  m_secondFreeListBits[fl - 5] |= 1 << sl;
}

void MemoryHeap::UnmarkFree(Block* block)
{
  DebugAssert(block->free);
  block->free = false;
  uint32_t fl, sl;
  MapSizeToSecondLevel(block->size, fl, sl);

  BlockListRemove(m_secondFreeList[(fl - 5) * 32 + sl], block);

  if (!m_secondFreeList[(fl - 5) * 32 + sl])
  {
    m_secondFreeListBits[fl - 5] &= ~(1 << sl);

    if (m_secondFreeListBits[fl - 5] == 0)
      m_firstFreeList &= ~(1 << (fl - 5));
  }
}

// makes a new block to the right and returns it
MemoryHeap::Block* MemoryHeap::SplitBlockRight(Block* block, uint32_t offset)
{
  DebugAssert(!block->free);
  DebugAssert(offset < block->size);
  Block* newBlock = BlockListPopFront(m_blockPoolUnused);

  newBlock->offset = block->offset + offset;
  newBlock->size = block->size - offset;
  newBlock->siblingLeft = block;
  newBlock->siblingRight = block->siblingRight;
  newBlock->free = false;
  if (newBlock->siblingRight)
  {
    DebugAssert(newBlock->siblingRight->siblingLeft == block);
    newBlock->siblingRight->siblingLeft = newBlock;
  }

  block->size -= newBlock->size;
  block->siblingRight = newBlock;

  return newBlock;
}

MemoryHeap::Block* MemoryHeap::MergeBlocksLeft(Block* block, Block* other)
{
  DebugAssert(block->siblingRight == other);
  DebugAssert(other->siblingLeft == block);
  DebugAssert(!block->free);
  DebugAssert(!other->free);
  DebugAssert(block->offset + block->size == other->offset);
  block->size += other->size;
  block->siblingRight = other->siblingRight;
  if (block->siblingRight)
  {
    DebugAssert(block->siblingRight->siblingLeft == other);
    block->siblingRight->siblingLeft = block;
  }

  BlockListPushFront(m_blockPoolUnused, other);

  return block;
}

MemoryHeap::MemoryHeap(dk::Device device, uint32_t size, uint32_t flags, uint32_t blockPoolSize)
{
  DebugAssert((size & (DK_MEMBLOCK_ALIGNMENT - 1)) == 0 && "block size not properly aligned");
  uint32_t sizeLog2 = 31 - __builtin_clz(size);
  if (((uint32_t)1 << sizeLog2) > size)
    sizeLog2++; // round up to the next power of two
  DebugAssert(sizeLog2 >= 5);

  uint32_t rows = sizeLog2 - 4; // remove rows below 32 bytes and round up to next

  m_secondFreeListBits = new uint32_t[rows];
  memset(m_secondFreeListBits, 0, rows * 4);
  m_secondFreeList = new Block*[rows * 32];
  memset(m_secondFreeList, 0, rows * 32 * 8);

  m_blockPool = new Block[blockPoolSize];
  memset(m_blockPool, 0, sizeof(Block) * blockPoolSize);
  for (uint32_t i = 0; i < blockPoolSize; i++)
    BlockListPushFront(m_blockPoolUnused, &m_blockPool[i]);

  // insert heap into the free list
  Block* heap = BlockListPopFront(m_blockPoolUnused);
  heap->offset = 0;
  heap->size = size - (flags & DkMemBlockFlags_Code ? DK_SHADER_CODE_UNUSABLE_SIZE : 0);
  heap->siblingLeft = nullptr;
  heap->siblingRight = nullptr;
  heap->free = false;
  MarkFree(heap);

  m_memblock = dk::MemBlockMaker{device, size}.setFlags(flags).create();
}

MemoryHeap::~MemoryHeap()
{
  Destroy();
}

void MemoryHeap::Destroy()
{
  if (m_valid)
  {
    m_memblock.destroy();

    delete[] m_blockPool;
    delete[] m_secondFreeList;
    delete[] m_secondFreeListBits;
    m_valid = false;
  }
}

MemoryHeap::Allocation MemoryHeap::Alloc(uint32_t size, uint32_t align)
{
  DebugAssert(size > 0);
  DebugAssertMsg((align & (align - 1)) == 0, "alignment must be a power of two");
  // minimum alignment (and thus size) is 32 bytes
  align = std::max((uint32_t)32, align);
  size = (size + align - 1) & ~(align - 1);

  // printf("allocating %f MB on heap %p (used %f%%)\n", (float)size/(1024.f*1024.f), m_memblock.getCpuAddr(),
  // (float)Used/m_memblock.getSize());

  uint32_t fl, sl;
  MapSizeToSecondLevel(size + (align > 32 ? align : 0), fl, sl);

  uint32_t secondFreeListBits = m_secondFreeListBits[fl - 5] & (0xFFFFFFFF << (sl + 1));

  if (sl == 31 || secondFreeListBits == 0)
  {
    uint32_t firstFreeListBits = m_firstFreeList & (0xFFFFFFFF << (fl - 5 + 1));
    DebugAssertMsg(firstFreeListBits, "out of memory :(");

    fl = __builtin_ctz(firstFreeListBits) + 5;
    secondFreeListBits = m_secondFreeListBits[fl - 5];
    DebugAssert(secondFreeListBits);
    sl = __builtin_ctz(secondFreeListBits);
  }
  DebugAssertMsg(secondFreeListBits, "out of memory :(");

  sl = __builtin_ctz(secondFreeListBits);

  Block* block = m_secondFreeList[(fl - 5) * 32 + sl];
  UnmarkFree(block);

  // align within the block
  if ((block->offset & (align - 1)) > 0)
  {
    DebugAssert(align > 32);
    Block* newBlock = SplitBlockRight(block, ((block->offset + align - 1) & ~(align - 1)) - block->offset);
    MarkFree(block);
    block = newBlock;
  }
  // put remaining data back
  if (block->size > size)
  {
    MarkFree(SplitBlockRight(block, size));
  }

  DebugAssert((block->offset & (align - 1)) == 0);
  DebugAssert(block->size == size);
  return {(uint32_t)(block - m_blockPool), block->offset, block->size};
}

void MemoryHeap::Free(Allocation allocation)
{
  printf("freeing %d %d %d\n", allocation.blockIdx, allocation.offset, allocation.size);
  Block* block = &m_blockPool[allocation.blockIdx];
  DebugAssert(!block->free);

  if (block->siblingLeft && block->siblingLeft->free)
  {
    UnmarkFree(block->siblingLeft);
    block = MergeBlocksLeft(block->siblingLeft, block);
  }
  if (block->siblingRight && block->siblingRight->free)
  {
    UnmarkFree(block->siblingRight);
    block = MergeBlocksLeft(block, block->siblingRight);
  }

  MarkFree(block);
}

} // namespace Deko3D