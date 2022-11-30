#pragma once

#include "common/deko3d/swap_chain.h"
#include "common/window_info.h"
#include "core/host_display.h"
#include "postprocessing_chain.h"
#include <deko3d.hpp>
#include <memory>
#include <optional>

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

  bool CreateRenderDevice(const WindowInfo& wi) override;
  bool InitializeRenderDevice() override;

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

  virtual std::unique_ptr<GPUTexture> CreateTexture(u32 width, u32 height, u32 layers, u32 levels, u32 samples,
                                                    GPUTexture::Format format, const void* data, u32 data_stride,
                                                    bool dynamic = false);
  virtual bool BeginTextureUpdate(GPUTexture* texture, u32 width, u32 height, void** out_buffer, u32* out_pitch);
  virtual void EndTextureUpdate(GPUTexture* texture, u32 x, u32 y, u32 width, u32 height);

  virtual bool UpdateTexture(GPUTexture* texture, u32 x, u32 y, u32 width, u32 height, const void* data, u32 pitch);

  virtual bool DownloadTexture(GPUTexture* texture, u32 x, u32 y, u32 width, u32 height, void* out_data,
                               u32 out_data_stride);

  bool SupportsTextureFormat(GPUTexture::Format format) const;

  void SetVSync(bool enabled) override;

  bool Render(bool skip_present) override;
  bool RenderScreenshot(u32 width, u32 height, std::vector<u32>* out_pixels, u32* out_stride,
                        GPUTexture::Format* out_format) override;

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

  void RenderDisplay(s32 left, s32 top, s32 width, s32 height, Deko3D::Texture* texture, s32 texture_view_x,
                     s32 texture_view_y, s32 texture_view_width, s32 texture_view_height, bool linear_filter);

  void CheckStagingBufferSize(u32 required_size);

private:
  std::optional<dk::Device> m_device;
  std::unique_ptr<Deko3D::SwapChain> m_swap_chain;

  Deko3D::Texture m_display_pixels_texture;

  dk::Shader m_vertex_shader, m_display_fragment_shader;
  Deko3D::MemoryHeap::Allocation m_vertex_shader_memory;
  Deko3D::MemoryHeap::Allocation m_display_fragment_shader_memory;

  Deko3D::MemoryHeap::Allocation m_uniform_buffer;
  Deko3D::MemoryHeap::Allocation m_sampler_buffer;
  Deko3D::MemoryHeap::Allocation m_descriptor_buffer;

  Deko3D::MemoryHeap::Allocation m_readback_buffer;
};
