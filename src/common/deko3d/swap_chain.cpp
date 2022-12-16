// Copyright 2022 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#include "swap_chain.h"
#include "context.h"

namespace Deko3D {

SwapChain::SwapChain(const WindowInfo& wi) : m_window_info(wi) {}

SwapChain::~SwapChain()
{
  FreeSwapchain();
  FreeImages();
}

std::unique_ptr<SwapChain> SwapChain::Create(const WindowInfo& wi)
{
  std::unique_ptr<SwapChain> swapchain = std::make_unique<SwapChain>(wi);
  swapchain->CreateImages();
  swapchain->CreateSwapchain();
  return swapchain;
}

void SwapChain::FreeSwapchain()
{
  m_swapchain.destroy();
}

int SwapChain::AcquireImage()
{
  m_cur_acquire_fence = (m_cur_acquire_fence + 1) % NumAcquireFences;
  int slot;
  m_swapchain.acquireImage(slot, m_acquire_fences[m_cur_acquire_fence]);
  return slot;
}

void SwapChain::PresentImage(int imageSlot)
{
  g_deko3d_context->GetQueue().presentImage(m_swapchain, imageSlot);
}

void SwapChain::CreateImages()
{
  for (size_t i = 0; i < NumSwapchainEntries; i++)
  {
    m_images[i].Create(m_window_info.surface_width, m_window_info.surface_height, 1, 0, DkImageFormat_RGBA8_Unorm,
                       DkMsMode_1x, DkImageType_2D,
                       DkImageFlags_UsagePresent | DkImageFlags_UsageRender | DkImageFlags_HwCompression);
  }
}

void SwapChain::CreateSwapchain()
{

  std::array<DkImage const*, NumSwapchainEntries> images;
  for (size_t i = 0; i < NumSwapchainEntries; i++)
    images[i] = &m_images[i].GetImage();
  m_swapchain = dk::SwapchainMaker{g_deko3d_context->GetDevice(), m_window_info.window_handle, images}.create();
}

void SwapChain::FreeImages()
{
  for (size_t i = 0; i < NumSwapchainEntries; i++)
    m_images[i].Destroy();
}

} // namespace Deko3D