// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#include "texture.h"
#include "../assert.h"
#include "context.h"
#include <algorithm>

namespace Deko3D {
Texture::Texture() = default;

Texture::Texture(Texture&& move)
  : m_valid(move.m_valid), m_width(move.m_width), m_height(move.m_height), m_levels(move.m_levels),
    m_layers(move.m_layers), m_format(move.m_format), m_samples(move.m_samples), m_view_type(move.m_view_type),
    m_memory(move.m_memory), m_image(move.m_image)
{
  move.m_width = 0;
  move.m_height = 0;
  move.m_levels = 0;
  move.m_layers = 0;
  move.m_format = DkImageFormat_None;
  move.m_samples = DkMsMode_1x;
  move.m_view_type = DkImageType_2D;
  move.m_memory = {};
  move.m_image = {};
  move.m_valid = false;
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

  m_width = width;
  m_height = height;
  m_levels = levels;
  m_layers = layers;
  m_format = format;
  m_samples = samples;
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
  m_format = DkImageFormat_None;
  m_samples = DkMsMode_1x;
  m_view_type = DkImageType_2D;
  m_memory = {};
  m_image = {};
  m_valid = false;
}

void Texture::UpdateFromBuffer(dk::CmdBuf cmdbuf, u32 level, u32 layer, u32 x, u32 y, u32 width, u32 height,
                               DkGpuAddr buffer)
{
  DebugAssert(IsValid());

  dk::ImageView dstView{m_image};
  dstView.setMipLevels(level);

  cmdbuf.copyBufferToImage({buffer, 0, 0}, dstView, {x, y, layer, width, height, 1});
}

} // namespace Deko3D
