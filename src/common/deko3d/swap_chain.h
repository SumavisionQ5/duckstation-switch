// Copyright 2022 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#pragma once

#include "../window_info.h"
#include "texture.h"
#include <array>
#include <memory>

#include <deko3d.hpp>

namespace Deko3D {

class SwapChain
{
public:
  static constexpr size_t NumSwapchainEntries = 2;

  SwapChain(const WindowInfo& wi);
  ~SwapChain();

  static std::unique_ptr<SwapChain> Create(const WindowInfo& wi);

  ALWAYS_INLINE const WindowInfo& GetWindowInfo() const { return m_window_info; }
  ALWAYS_INLINE const Texture& GetImage(int imageSlot) const { return m_images[imageSlot]; }

  int AcquireImage();
  void PresentImage(int imageSlot);

  void CreateImages();
  void CreateSwapchain();

  void FreeImages();
  void FreeSwapchain();

  dk::Fence& GetCurrentAcquireFence() { return m_acquire_fences[m_cur_acquire_fence]; }

private:
  // this is pretty stupid we could also just use a single fence
  // because **deko secret** there are internal and external fences
  // and acquire fences are the latter which means the bookkeeping is all
  // done for us.
  static constexpr size_t NumAcquireFences = 4;

  std::array<dk::Fence, NumAcquireFences> m_acquire_fences;
  u32 m_cur_acquire_fence = 0;
  dk::Swapchain m_swapchain;
  WindowInfo m_window_info;

  std::array<Texture, NumSwapchainEntries> m_images;
};

} // namespace Deko3D
