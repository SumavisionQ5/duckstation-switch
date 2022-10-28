// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#include "texture.h"
#include "../align.h"
#include "../assert.h"
#include "../log.h"
#include "../string_util.h"
#include "context.h"
#include <algorithm>
Log_SetChannel(Texture);

namespace Deko3D {
Texture::Texture() = default;

Texture::Texture(Texture&& move) : m_memory(move.m_memory), m_image(move.m_image)
{
  m_view_type = move.m_view_type;
  m_width = move.m_width;
  m_height = move.m_height;
  m_layers = move.m_layers;
  m_levels = move.m_levels;
  m_samples = move.m_samples;
  m_valid = move.m_valid;

  move.ClearBaseProperties();
  move.m_valid = false;
  move.m_memory = {};
  move.m_image = {};
}

Texture::~Texture()
{
  if (IsValid())
    Destroy(true);
}

Deko3D::Texture& Texture::operator=(Texture&& move)
{
  if (IsValid())
    Destroy(true);

  std::swap(m_width, move.m_width);
  std::swap(m_height, move.m_height);
  std::swap(m_levels, move.m_levels);
  std::swap(m_layers, move.m_layers);
  std::swap(m_format, move.m_format);
  std::swap(m_samples, move.m_samples);
  std::swap(m_view_type, move.m_view_type);
  std::swap(m_memory, move.m_memory);
  std::swap(m_image, move.m_image);
  std::swap(m_valid, move.m_valid);

  return *this;
}

bool Texture::Create(u32 width, u32 height, u32 levels, u32 layers, DkImageFormat format, DkMsMode samples,
                     DkImageType view_type, uint32_t flags)
{
  if (IsValid())
    Destroy(true);

  dk::ImageLayout layout;
  dk::ImageLayoutMaker{g_deko3d_context->GetDevice()}
    .setDimensions(width, height, layers)
    .setMipLevels(levels)
    .setFormat(format)
    .setMsMode(samples)
    .setType(view_type)
    .setFlags(flags)
    .initialize(layout);

  m_memory = g_deko3d_context->GetImageHeap().Alloc(layout.getSize(), layout.getAlignment());

  m_image.initialize(layout, g_deko3d_context->GetImageHeap().GetMemBlock(), m_memory.offset);

  switch (format)
  {
    case DkImageFormat_RGBA8_Unorm:
      m_format = Format::RGBA8;
      break;
    case DkImageFormat_BGRA8_Unorm:
      m_format = Format::BGRA8;
      break;
    case DkImageFormat_BGR565_Unorm:
      m_format = Format::RGB565;
      break;
    case DkImageFormat_BGR5A1_Unorm:
      m_format = Format::RGBA5551;
      break;
    case DkImageFormat_R8_Unorm:
      m_format = Format::R8;
      break;
    case DkImageFormat_Z16:
      m_format = Format::D16;
      break;
    default:
      Panic("Unknown texture format");
  }
  m_width = width;
  m_height = height;
  m_levels = levels;
  m_layers = layers;
  m_samples = 1 << samples;
  m_view_type = view_type;
  m_valid = true;
  return true;
}

void Texture::Destroy(bool defer /* = true */)
{
  // If we don't have device memory allocated, the image is not owned by us (e.g. swapchain)
  if (m_memory.size > 0)
  {
    DebugAssert(IsValid());

    if (defer)
      g_deko3d_context->DeferedFree(&g_deko3d_context->GetImageHeap(), m_memory);
    else
      g_deko3d_context->GetImageHeap().Free(m_memory);
  }

  m_width = 0;
  m_height = 0;
  m_levels = 0;
  m_layers = 0;
  m_samples = 0;
  m_view_type = DkImageType_2D;
  m_memory = {};
  m_image = {};
  m_valid = false;
}

void Texture::UpdateFromBuffer(dk::CmdBuf cmdbuf, u32 level, u32 layer, u32 x, u32 y, u32 width, u32 height,
                               DkGpuAddr buffer, u32 pitch)
{
  DebugAssert(IsValid());

  dk::ImageView dstView{m_image};
  dstView.setMipLevels(level);

  cmdbuf.copyBufferToImage({buffer, pitch, 0}, dstView, {x, y, layer, width, height, 1});
}

u32 Texture::CalcUpdatePitch(u32 width) const
{
  return width * GetPixelSize();
}

bool Texture::BeginUpdate(u32 width, u32 height, void** out_buffer, u32* out_pitch)
{
  const u32 pitch = CalcUpdatePitch(width);
  const u32 required_size = pitch * height;
  StreamBuffer& buffer = g_deko3d_context->GetTextureUploadBuffer();
  if (required_size > buffer.GetCurrentSize())
    return false;

  // TODO: allocate temporary buffer if this fails...
  if (!buffer.ReserveMemory(required_size, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT))
  {
    g_deko3d_context->ExecuteCommandBuffer(false);
    if (!buffer.ReserveMemory(required_size, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT))
      return false;
  }

  *out_buffer = buffer.GetCurrentHostPointer();
  *out_pitch = pitch;
  return true;
}

void Texture::EndUpdate(u32 x, u32 y, u32 width, u32 height, u32 level, u32 layer)
{
  const u32 pitch = CalcUpdatePitch(width);
  const u32 required_size = pitch * height;

  StreamBuffer& buffer = g_deko3d_context->GetTextureUploadBuffer();
  const u32 buffer_offset = buffer.GetCurrentOffset();
  buffer.CommitMemory(required_size);

  UpdateFromBuffer(g_deko3d_context->GetCmdBuf(), level, layer, x, y, width, height,
                   g_deko3d_context->GetGeneralHeap().GpuAddr(buffer.GetBuffer()) + buffer_offset, pitch);
}

bool Texture::Update(u32 x, u32 y, u32 width, u32 height, u32 level, u32 layer, const void* data, u32 data_pitch)
{
  const u32 pitch = CalcUpdatePitch(width);
  const u32 required_size = pitch * height;
  StreamBuffer& sbuffer = g_deko3d_context->GetTextureUploadBuffer();

  MemoryHeap& heap = g_deko3d_context->GetGeneralHeap();

  // If the texture is larger than half our streaming buffer size, use a separate buffer.
  // Otherwise allocation will either fail, or require lots of cmdbuffer submissions.
  if (required_size > (g_deko3d_context->GetTextureUploadBuffer().GetCurrentSize() / 2))
  {
    const u32 size = data_pitch * height;
    MemoryHeap::Allocation buffer = heap.Alloc(size, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);

    // Immediately queue it for freeing after the command buffer finishes, since it's only needed for the copy.
    g_deko3d_context->DeferedFree(&heap, buffer);

    StringUtil::StrideMemCpy(heap.CpuAddr<void>(buffer), pitch, data, data_pitch, std::min(data_pitch, pitch), height);

    UpdateFromBuffer(g_deko3d_context->GetCmdBuf(), level, layer, x, y, width, height, heap.GpuAddr(buffer), pitch);
    return true;
  }
  else
  {
    if (!sbuffer.ReserveMemory(required_size, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT))
    {
      g_deko3d_context->ExecuteCommandBuffer(false);
      if (!sbuffer.ReserveMemory(required_size, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT))
      {
        Log_ErrorPrintf("Failed to reserve texture upload memory (%u bytes).", required_size);
        return false;
      }
    }

    const u32 buffer_offset = sbuffer.GetCurrentOffset();
    StringUtil::StrideMemCpy(sbuffer.GetCurrentHostPointer(), pitch, data, data_pitch, std::min(data_pitch, pitch),
                             height);
    sbuffer.CommitMemory(required_size);

    UpdateFromBuffer(g_deko3d_context->GetCmdBuf(), level, layer, x, y, width, height,
                     heap.GpuAddr(sbuffer.GetBuffer()) + buffer_offset, pitch);
    return true;
  }
}

} // namespace Deko3D
