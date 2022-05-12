#include "imgui.h"
#include "imgui_impl_switch.h"

// Data
static u64 g_Time = 0;

void ImGui_ImplSwitch_Init()
{
    // Setup back-end capabilities flags
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_switch";
}

void ImGui_ImplSwitch_Shutdown()
{
}

void ImGui_ImplSwitch_NewFrame()
{
    ImGuiIO& io = ImGui::GetIO();

    // Setup time step
    u64 current_time = armGetSystemTick();
    io.DeltaTime = g_Time > 0 ? (float)((double)armTicksToNs(current_time - g_Time) * 0.000000001) : (float)(1.0f / 60.0f);
    g_Time = current_time;
}
