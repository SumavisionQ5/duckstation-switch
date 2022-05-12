#include "context_egl_switch.h"
#include <stdio.h>

namespace GL {
ContextEGLSwitch::ContextEGLSwitch(const WindowInfo& wi) : ContextEGL(wi) {}
ContextEGLSwitch::~ContextEGLSwitch() = default;

std::unique_ptr<Context> ContextEGLSwitch::Create(const WindowInfo& wi, const Version* versions_to_try,
                                               size_t num_versions_to_try)
{
  std::unique_ptr<ContextEGLSwitch> context = std::make_unique<ContextEGLSwitch>(wi);
  if (!context->Initialize(versions_to_try, num_versions_to_try))
    return nullptr;

  return context;
}

std::unique_ptr<Context> ContextEGLSwitch::CreateSharedContext(const WindowInfo& wi)
{
  std::unique_ptr<ContextEGLSwitch> context = std::make_unique<ContextEGLSwitch>(wi);
  context->m_display = m_display;

  if (!context->CreateContextAndSurface(m_version, m_context, false))
    return nullptr;

  return context;
}

EGLNativeWindowType ContextEGLSwitch::GetNativeWindow(EGLConfig config)
{
  return static_cast<EGLNativeWindowType>(m_wi.window_handle);
}
} // namespace GL
