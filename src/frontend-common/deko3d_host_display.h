#pragma once

#include "common/deko3d/staging_texture.h"
#include "common/deko3d/swap_chain.h"
#include "common/window_info.h"
#include "core/host_display.h"
#include "postprocessing_chain.h"
#include <deko3d.hpp>
#include <memory>
#include <optional>

namespace FrontendCommon {

class Deko3DHostDisplay final : public HostDisplay
{
public:
  Deko3DHostDisplay();
  ~Deko3DHostDisplay();

  RenderAPI GetRenderAPI() const override;
  void* GetRenderDevice() const override;
  void* GetRenderContext() const override;

  bool HasRenderDevice() const override;
  bool HasRenderSurface() const override;

  bool CreateRenderDevice(const WindowInfo& wi, std::string_view adapter_name, bool debug_device,
                          bool threaded_presentation) override;
  bool InitializeRenderDevice(std::string_view shader_cache_directory, bool debug_device,
                              bool threaded_presentation) override;
  void DestroyRenderDevice() override;

  bool MakeRenderContextCurrent() override;
  bool DoneRenderContextCurrent() override;

  bool ChangeRenderWindow(const WindowInfo& new_wi) override;
  void ResizeRenderWindow(s32 new_window_width, s32 new_window_height) override;
  bool SupportsFullscreen() const override;
  bool IsFullscreen() override;
  bool SetFullscreen(bool fullscreen, u32 width, u32 height, float refresh_rate) override;
  AdapterAndModeList GetAdapterAndModeList() override;
  void DestroyRenderSurface() override;

  bool SetPostProcessingChain(const std::string_view& config) override;

  std::unique_ptr<HostDisplayTexture> CreateTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples,
                                                    HostDisplayPixelFormat format, const void* data, u32 data_stride,
                                                    bool dynamic = false) override;
  void UpdateTexture(HostDisplayTexture* texture, u32 x, u32 y, u32 width, u32 height, const void* texture_data,
                     u32 texture_data_stride) override;
  bool DownloadTexture(const void* texture_handle, HostDisplayPixelFormat texture_format, u32 x, u32 y, u32 width,
                       u32 height, void* out_data, u32 out_data_stride) override;
  bool SupportsDisplayPixelFormat(HostDisplayPixelFormat format) const override;
  bool BeginSetDisplayPixels(HostDisplayPixelFormat format, u32 width, u32 height, void** out_buffer,
                             u32* out_pitch) override;
  void EndSetDisplayPixels() override;

  void SetVSync(bool enabled) override;

  bool Render() override;
  bool RenderScreenshot(u32 width, u32 height, std::vector<u32>* out_pixels, u32* out_stride,
                        HostDisplayPixelFormat* out_format) override;

protected:
  struct UniformBuffer
  {
    float src_rect_left;
    float src_rect_top;
    float src_rect_width;
    float src_rect_height;
  };

  bool CreateResources() override;
  void DestroyResources() override;

  bool CreateImGuiContext() override;
  void DestroyImGuiContext() override;
  bool UpdateImGuiFontTexture() override;

  void RenderImGui();
  void RenderDisplay();

  void RenderDisplay(s32 left, s32 top, s32 width, s32 height, void* texture_handle, u32 texture_width,
                     s32 texture_height, s32 texture_view_x, s32 texture_view_y, s32 texture_view_width,
                     s32 texture_view_height, bool linear_filter);

private:
  std::optional<dk::Device> m_device;
  std::unique_ptr<Deko3D::SwapChain> m_swap_chain;

  Deko3D::StagingTexture m_upload_staging_texture;
  Deko3D::StagingTexture m_readback_staging_texture;
  Deko3D::Texture m_display_pixels_texture;

  dk::Shader m_vertex_shader, m_display_fragment_shader;
  Deko3D::MemoryHeap::Allocation m_vertex_shader_memory;
  Deko3D::MemoryHeap::Allocation m_display_fragment_shader_memory;

  Deko3D::MemoryHeap::Allocation m_uniform_buffer;
  Deko3D::MemoryHeap::Allocation m_sampler_buffer;
  Deko3D::MemoryHeap::Allocation m_descriptor_buffer;
};

} // namespace FrontendCommon
