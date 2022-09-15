// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#include "staging_texture.h"
#include "../assert.h"
#include "context.h"
#include "util.h"
#include <stdio.h>

namespace Deko3D {

StagingTexture::StagingTexture() = default;

StagingTexture::StagingTexture(StagingTexture&& move)
  : m_memory(std::move(move.m_memory)), m_flush_fence_counter(move.m_flush_fence_counter), m_width(move.m_width),
    m_height(move.m_height), m_texel_size(move.m_texel_size), m_map_stride(move.m_map_stride)
{
  move.m_flush_fence_counter = 0;
  move.m_width = 0;
  move.m_height = 0;
  move.m_texel_size = 0;
  move.m_map_stride = 0;
}

StagingTexture& StagingTexture::operator=(StagingTexture&& move)
{
  if (IsValid())
    Destroy(true);

  std::swap(m_memory, move.m_memory);
  std::swap(m_flush_fence_counter, move.m_flush_fence_counter);
  std::swap(m_width, move.m_width);
  std::swap(m_height, move.m_height);
  std::swap(m_texel_size, move.m_texel_size);
  std::swap(m_map_stride, move.m_map_stride);
  return *this;
}

StagingTexture::~StagingTexture()
{
  if (IsValid())
    Destroy(true);
}

bool StagingTexture::Create(DkImageFormat format, u32 width, u32 height)
{
  const u32 texel_size = Util::GetTexelSize(format);
  const u32 map_stride = texel_size * width;
  const u32 buffer_size = map_stride * height;

  MemoryHeap::Allocation memory =
    g_deko3d_context->GetGeneralHeap().Alloc(buffer_size, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);

  if (IsValid())
    Destroy(true);

  m_memory = std::move(memory);
  m_width = width;
  m_height = height;
  m_texel_size = texel_size;
  m_map_stride = map_stride;
  return true;
}

void StagingTexture::Destroy(bool defer /* = true */)
{
  if (!IsValid())
    return;

  if (defer)
    g_deko3d_context->DeferedFree(&g_deko3d_context->GetGeneralHeap(), m_memory);
  else
    g_deko3d_context->GetGeneralHeap().Free(m_memory);
  m_memory = {};
  m_flush_fence_counter = 0;
  m_width = 0;
  m_height = 0;
  m_texel_size = 0;
  m_map_stride = 0;
}

void StagingTexture::CopyFromTexture(dk::CmdBuf command_buffer, Texture& src_texture, u32 src_x, u32 src_y,
                                     u32 src_layer, u32 src_level, u32 dst_x, u32 dst_y, u32 width, u32 height)
{
  Assert((src_x + width) <= src_texture.GetWidth() && (src_y + height) <= src_texture.GetHeight());
  Assert((dst_x + width) <= m_width && (dst_y + height) <= m_height);

  // Issue the image->buffer copy, but delay it for now.
  DkCopyBuf dst{GetGPUAddr() + dst_y * m_map_stride + dst_x * m_texel_size, m_map_stride, height};
  dk::ImageView src{src_texture.GetImage()};
  src.setMipLevels(src_level, 1);
  command_buffer.copyImageToBuffer(src, {src_x, src_y, src_layer, width, height, 1}, dst);
}

void StagingTexture::CopyFromTexture(Texture& src_texture, u32 src_x, u32 src_y, u32 src_layer, u32 src_level,
                                     u32 dst_x, u32 dst_y, u32 width, u32 height)
{
  CopyFromTexture(g_deko3d_context->GetCmdBuf(), src_texture, src_x, src_y, src_layer, src_level, dst_x, dst_y, width,
                  height);

  m_needs_flush = true;
  m_flush_fence_counter = g_deko3d_context->GetCurrentFenceCounter();
}

void StagingTexture::CopyToTexture(dk::CmdBuf command_buffer, u32 src_x, u32 src_y, Texture& dst_texture, u32 dst_x,
                                   u32 dst_y, u32 dst_layer, u32 dst_level, u32 width, u32 height)
{
  Assert((dst_x + width) <= dst_texture.GetWidth() && (dst_y + height) <= dst_texture.GetHeight());
  Assert((src_x + width) <= m_width && (src_y + height) <= m_height);

  // Issue the image->buffer copy, but delay it for now.
  DkCopyBuf src{GetGPUAddr() + src_y * m_map_stride + src_x * m_texel_size, m_map_stride, height};
  dk::ImageView dst{dst_texture.GetImage()};
  dst.setMipLevels(dst_layer, 1);
  printf("issuing copy (%u %u) (%u %u) %u*%u\n", src_x, src_y, dst_x, dst_y, width, height);
  command_buffer.copyBufferToImage(src, dst, {dst_x, dst_y, dst_layer, width, height, 1});
}

void StagingTexture::CopyToTexture(u32 src_x, u32 src_y, Texture& dst_texture, u32 dst_x, u32 dst_y, u32 dst_layer,
                                   u32 dst_level, u32 width, u32 height)
{
  CopyToTexture(g_deko3d_context->GetCmdBuf(), src_x, src_y, dst_texture, dst_x, dst_y, dst_layer, dst_level, width,
                height);

  m_needs_flush = true;
  m_flush_fence_counter = g_deko3d_context->GetCurrentFenceCounter();
}

void StagingTexture::Flush()
{
  if (!m_needs_flush)
    return;

  // Is this copy in the current command buffer?
  if (g_deko3d_context->GetCurrentFenceCounter() == m_flush_fence_counter)
  {
    // Execute the command buffer and wait for it to finish.
    g_deko3d_context->ExecuteCommandBuffer(true);
  }
  else
  {
    // Wait for the GPU to finish with it.
    g_deko3d_context->WaitForFenceCounter(m_flush_fence_counter);
  }

  m_needs_flush = false;
}

void StagingTexture::ReadTexels(u32 src_x, u32 src_y, u32 width, u32 height, void* out_ptr, u32 out_stride)
{
  Assert((src_x + width) <= m_width && (src_y + height) <= m_height);
  PrepareForAccess();

  // Offset pointer to point to start of region being copied out.
  const char* current_ptr = GetMappedPointer();
  current_ptr += src_y * m_map_stride;
  current_ptr += src_x * m_texel_size;

  // Optimal path: same dimensions, same stride.
  if (src_x == 0 && width == m_width && m_map_stride == out_stride)
  {
    std::memcpy(out_ptr, current_ptr, m_map_stride * height);
    return;
  }

  size_t copy_size = std::min<u32>(width * m_texel_size, m_map_stride);
  char* dst_ptr = reinterpret_cast<char*>(out_ptr);
  for (u32 row = 0; row < height; row++)
  {
    std::memcpy(dst_ptr, current_ptr, copy_size);
    current_ptr += m_map_stride;
    dst_ptr += out_stride;
  }
}

void StagingTexture::ReadTexel(u32 x, u32 y, void* out_ptr)
{
  Assert(x < m_width && y < m_height);
  PrepareForAccess();

  const char* src_ptr = GetMappedPointer() + y * GetMappedStride() + x * m_texel_size;
  std::memcpy(out_ptr, src_ptr, m_texel_size);
}

void StagingTexture::WriteTexels(u32 dst_x, u32 dst_y, u32 width, u32 height, const void* in_ptr, u32 in_stride)
{
  Assert((dst_x + width) <= m_width && (dst_y + height) <= m_height);
  PrepareForAccess();

  // Offset pointer to point to start of region being copied to.
  char* current_ptr = GetMappedPointer();
  current_ptr += dst_y * m_map_stride;
  current_ptr += dst_x * m_texel_size;

  // Optimal path: same dimensions, same stride.
  if (dst_x == 0 && width == m_width && m_map_stride == in_stride)
  {
    std::memcpy(current_ptr, in_ptr, m_map_stride * height);
    return;
  }

  size_t copy_size = std::min<u32>(width * m_texel_size, m_map_stride);
  const char* src_ptr = reinterpret_cast<const char*>(in_ptr);
  for (u32 row = 0; row < height; row++)
  {
    std::memcpy(current_ptr, src_ptr, copy_size);
    current_ptr += m_map_stride;
    src_ptr += in_stride;
  }
}

void StagingTexture::WriteTexel(u32 x, u32 y, const void* in_ptr)
{
  Assert(x < m_width && y < m_height);
  PrepareForAccess();

  char* dest_ptr = GetMappedPointer() + y * m_map_stride + x * m_texel_size;
  std::memcpy(dest_ptr, in_ptr, m_texel_size);
}

void StagingTexture::PrepareForAccess()
{
  Assert(IsMapped());
  if (m_needs_flush)
    Flush();
}

} // namespace Deko3D