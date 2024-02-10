#pragma once

#include "imgui.h"
#include <deko3d.hpp>

// Initialization data, for ImGui_ImplVulkan_Init()
// [Please zero-clear before use!]
struct ImGui_ImplDeko3D_InitInfo
{
  dk::Device Device;
  uint32_t QueueFamily;
  dk::Queue Queue;
  uint32_t MinImageCount; // >= 2
  uint32_t ImageCount;    // >= MinImageCount
  // VkSampleCountFlagBits MSAASamples; // >= VK_SAMPLE_COUNT_1_BIT
  // const VkAllocationCallbacks* Allocator;
};

// Called by user code
IMGUI_IMPL_API bool ImGui_ImplDeko3D_Init(ImGui_ImplDeko3D_InitInfo* info);
IMGUI_IMPL_API void ImGui_ImplDeko3D_Shutdown();
IMGUI_IMPL_API void ImGui_ImplDeko3D_RenderDrawData(ImDrawData* draw_data, dk::CmdBuf command_buffer);
IMGUI_IMPL_API bool ImGui_ImplDeko3D_CreateFontsTexture(dk::CmdBuf command_buffer);
IMGUI_IMPL_API void ImGui_ImplDeko3D_DestroyFontUploadObjects();
