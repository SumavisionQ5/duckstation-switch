// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#include "util.h"
#include "../assert.h"
#include "../types.h"

namespace Deko3D {
namespace Util {

void SetViewportAndScissor(dk::CmdBuf command_buffer, int x, int y, int width, int height, float min_depth /*= 0.0f*/,
                           float max_depth /*= 1.0f*/)
{
  const DkViewport vp{static_cast<float>(x),
                      static_cast<float>(y),
                      static_cast<float>(width),
                      static_cast<float>(height),
                      min_depth,
                      max_depth};
  const DkScissor scissor{static_cast<u32>(x), static_cast<u32>(y), static_cast<u32>(width), static_cast<u32>(height)};
  command_buffer.setViewports(0, {vp});
  command_buffer.setScissors(0, {scissor});
}

} // namespace Util
} // namespace Deko3D