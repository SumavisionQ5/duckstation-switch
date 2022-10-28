// Copyright 2016 Dolphin Emulator Project
// Copyright 2020 DuckStation Emulator Project
// Licensed under GPLv2+
// Refer to the LICENSE file included.

#pragma once

#include "../types.h"
#include <deko3d.hpp>

namespace Deko3D {
namespace Util {

void SetViewportAndScissor(dk::CmdBuf command_buffer, int x, int y, int width, int height,
                           float min_depth = 0.0f, float max_depth = 1.0f);


} // namespace Util
} // namespace Deko3D