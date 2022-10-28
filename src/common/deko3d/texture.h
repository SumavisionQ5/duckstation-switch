// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#pragma once
#include "../gpu_texture.h"
#include "../types.h"
#include <algorithm>
#include <deko3d.hpp>
#include <memory>
#include <optional>

#include "memory_heap.h"

namespace Deko3D {
class Texture : public GPUTexture
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

  ALWAYS_INLINE DkImageType GetViewType() const { return m_view_type; }
  ALWAYS_INLINE MemoryHeap::Allocation GetDeviceMemory() const { return m_memory; }
  ALWAYS_INLINE const dk::Image& GetImage() const { return m_image; }
  DkImageFormat GetDkFormat();

  bool Create(u32 width, u32 height, u32 levels, u32 layers, DkImageFormat format, DkMsMode samples,
              DkImageType view_type, uint32_t flags);

  void Destroy(bool defer = true);

  void UpdateFromBuffer(dk::CmdBuf cmdbuf, u32 level, u32 layer, u32 x, u32 y, u32 width, u32 height, DkGpuAddr buffer,
                        u32 pitch = 0);

  u32 CalcUpdatePitch(u32 width) const;
  bool BeginUpdate(u32 width, u32 height, void** out_buffer, u32* out_pitch);
  void EndUpdate(u32 x, u32 y, u32 width, u32 height, u32 level, u32 layer);
  bool Update(u32 x, u32 y, u32 width, u32 height, u32 level, u32 layer, const void* data, u32 data_pitch);

private:
  bool m_valid = false;
  DkImageType m_view_type = DkImageType_2D;

  MemoryHeap::Allocation m_memory;
  dk::Image m_image;
};

} // namespace Deko3D
