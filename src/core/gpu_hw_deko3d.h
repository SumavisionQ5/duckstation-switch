#pragma once
#include "common/deko3d/stream_buffer.h"
#include "common/deko3d/texture.h"
#include "common/dimensional_array.h"
#include "gpu_hw.h"
#include "texture_replacements.h"
#include <array>
#include <memory>
#include <tuple>

class GPU_HW_Deko3D : public GPU_HW
{
public:
  GPU_HW_Deko3D();
  ~GPU_HW_Deko3D() override;

  GPURenderer GetRendererType() const override;

  bool Initialize() override;
  void Reset(bool clear_vram) override;
  bool DoState(StateWrapper& sw, GPUTexture** host_texture, bool update_display) override;

  void ResetGraphicsAPIState() override;
  void RestoreGraphicsAPIState() override;
  void UpdateSettings() override;

protected:
  void ClearDisplay() override;
  void UpdateDisplay() override;
  void ReadVRAM(u32 x, u32 y, u32 width, u32 height) override;
  void FillVRAM(u32 x, u32 y, u32 width, u32 height, u32 color) override;
  void UpdateVRAM(u32 x, u32 y, u32 width, u32 height, const void* data, bool set_mask, bool check_mask) override;
  void CopyVRAM(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height) override;
  void UpdateVRAMReadTexture() override;
  void UpdateDepthBufferFromMaskBit() override;
  void ClearDepthBuffer() override;
  void SetScissorFromDrawingArea() override;
  void MapBatchVertexPointer(u32 required_vertices) override;
  void UnmapBatchVertexPointer(u32 used_vertices) override;
  void UploadUniformBuffer(const void* data, u32 data_size) override;
  void DrawBatchVertices(BatchRenderMode render_mode, u32 base_vertex, u32 num_vertices) override;

private:
  enum : u32
  {
    MAX_PUSH_CONSTANTS_SIZE = 64,
  };
  void DestroyResources();

  bool CreateSamplers();

  bool CompileShaders();
  void DestroyShaders();

  bool CreateFramebuffer();
  void ClearFramebuffer();
  void DestroyFramebuffer();

  bool CreateVertexBuffer();
  bool CreateUniformBuffer();
  bool CreateTextureBuffer();

  bool BlitVRAMReplacementTexture(const TextureReplacementTexture* tex, u32 dst_x, u32 dst_y, u32 width, u32 height);

  void DownsampleFramebuffer(Deko3D::Texture& source, u32 left, u32 top, u32 width, u32 height);
  void DownsampleFramebufferBoxFilter(Deko3D::Texture& source, u32 left, u32 top, u32 width, u32 height);

  void SetBlendMode(dk::CmdBuf cmdbuf, bool enable_blending, bool subtractive_blending, bool force = false);
  void SetDepthFunc(dk::CmdBuf cmdbuf, bool force = false);
  void SetDepthTest(dk::CmdBuf cmdbuf, bool enable, DkCompareOp func, bool force = false);
  void DisableBlending(dk::CmdBuf cmdbuf);

  void ExecuteCommandBuffer(bool wait_for_completion, bool restore_state);

  void RestoreGraphicsAPIStateEx(bool restore_rt, bool returning_from_known_state);

  void PushOtherUniform(dk::CmdBuf cmdbuf, DkStage stage, const void* data, u32 data_size);

  enum : u32
  {
    IMAGE_VRAM,
    IMAGE_VRAM_DEPTH,
    IMAGE_VRAM_READ,
    IMAGE_VRAM_READBACK,
    IMAGE_DISPLAY,
    IMAGE_TEXTURE_BUFFER,
    IMAGES_COUNT,
  };

  enum : u32
  {
    SAMPLER_POINT,
    SAMPLER_LINEAR,
    SAMPLER_TRILINEAR,
    SAMPLERS_COUNT
  };

  Deko3D::MemoryHeap::Allocation m_sampler_memory;
  Deko3D::MemoryHeap::Allocation m_image_descriptor_memory;

  dk::Image m_texture_buffer;

  Deko3D::Texture m_vram_texture;
  Deko3D::Texture m_vram_depth_texture;
  Deko3D::Texture m_vram_read_texture;
  Deko3D::Texture m_vram_readback_texture;
  Deko3D::Texture m_display_texture;

  Deko3D::StreamBuffer m_vertex_stream_buffer;
  Deko3D::StreamBuffer m_texture_stream_buffer;

  // texture replacements
  Deko3D::Texture m_vram_write_replacement_texture;

  // downsampling
  Deko3D::Texture m_downsample_texture;
  Deko3D::Texture m_downsample_weight_texture;

  Deko3D::MemoryHeap::Allocation m_batch_uniform;
  Deko3D::MemoryHeap::Allocation m_other_uniforms;

  /*struct SmoothMipView
  {
  };
  std::vector<SmoothMipView> m_downsample_mip_views;*/

  struct Shader
  {
    dk::Shader shader;
    Deko3D::MemoryHeap::Allocation memory;
  };

  dk::DepthStencilState m_current_depth_state;
  bool m_blending_enabled = false, m_subtractive_blending = false;

  DimensionalArray<Shader, 2> m_batch_vertex_shaders;
  DimensionalArray<Shader, 2, 2, 9, 4> m_batch_fragment_shaders;
  Shader m_fullscreen_quad_vertex_shader;
  Shader uv_quad_vertex_shader;
  DimensionalArray<Shader, 2, 2> m_vram_fill_shaders;
  Shader m_vram_read_shader;
  Shader m_vram_write_shader;
  Shader m_vram_update_depth_shader;
  Shader m_vram_copy_shader;
  DimensionalArray<Shader, 3, 2> m_display_shaders;
  Shader m_boxsample_downsample_shader;
};
