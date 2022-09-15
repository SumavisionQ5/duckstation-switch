// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#pragma once
#include "../types.h"
#include <algorithm>
#include <deko3d.hpp>
#include <memory>
#include <optional>

#include "memory_heap.h"

namespace Deko3D {
class Texture
{
public:
  Texture();
  Texture(Texture&& move);
  Texture(const Texture&) = delete;
  ~Texture();

  Texture& operator=(Texture&& move);
  Texture& operator=(const Texture&) = delete;

  ALWAYS_INLINE bool IsValid() const { return m_valid; }

  /// An image is considered owned/managed if we control the memory.
  ALWAYS_INLINE bool IsOwned() const { return m_memory.size > 0; }

  ALWAYS_INLINE u32 GetWidth() const { return m_width; }
  ALWAYS_INLINE u32 GetHeight() const { return m_height; }
  ALWAYS_INLINE u32 GetLevels() const { return m_levels; }
  ALWAYS_INLINE u32 GetLayers() const { return m_layers; }
  ALWAYS_INLINE u32 GetMipWidth(u32 level) const { return std::max<u32>(m_width >> level, 1u); }
  ALWAYS_INLINE u32 GetMipHeight(u32 level) const { return std::max<u32>(m_height >> level, 1u); }
  ALWAYS_INLINE DkImageFormat GetFormat() const { return m_format; }
  ALWAYS_INLINE DkMsMode GetSamples() const { return m_samples; }
  ALWAYS_INLINE DkImageType GetViewType() const { return m_view_type; }
  ALWAYS_INLINE MemoryHeap::Allocation GetDeviceMemory() const { return m_memory; }
  ALWAYS_INLINE const dk::Image& GetImage() const { return m_image; }

  bool Create(u32 width, u32 height, u32 levels, u32 layers, DkImageFormat format, DkMsMode samples,
              DkImageType view_type, uint32_t flags);

  void Destroy(bool defer = true);

  void UpdateFromBuffer(dk::CmdBuf cmdbuf, u32 level, u32 layer, u32 x, u32 y, u32 width, u32 height, DkGpuAddr buffer);

private:
  bool m_valid = false;
  u32 m_width = 0;
  u32 m_height = 0;
  u32 m_levels = 0;
  u32 m_layers = 0;
  DkImageFormat m_format = DkImageFormat_None;
  DkMsMode m_samples = DkMsMode_1x;
  DkImageType m_view_type = DkImageType_2D;

  MemoryHeap::Allocation m_memory;
  dk::Image m_image;
};

} // namespace Deko3D
