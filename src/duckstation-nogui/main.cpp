#include "common/assert.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/string_util.h"
#include "core/system.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef WITH_VTY
#include "vty_host_interface.h"
#endif

#ifdef WITH_SDL2
#include "sdl_host_interface.h"

static bool IsSDLHostInterfaceAvailable()
{
#if defined(__linux__)
  // Only available if we have a X11 or Wayland display.
  if (std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY"))
    return true;
  else
    return false;
#else
  // Always available on Windows/Apple.
  return true;
#endif
}
#endif

#ifdef __SWITCH__
#include "switch_host_interface.h"
#include <switch.h>

namespace Common::PageFaultHandler
{
bool PageFaultHandler(ThreadExceptionDump* ctx);
}

extern "C"
{

extern char __start__;
extern char __rodata_start;

void HandleFault(uint64_t pc, uint64_t lr, uint64_t fp, uint64_t faultAddr, uint32_t desc)
{
  if (pc >= (uint64_t)&__start__ && pc < (uint64_t)&__rodata_start)
  {
    printf("unintentional fault in .text at %p (type %d) (trying to access %p?)\n", 
      pc - (uint64_t)&__start__, desc, faultAddr);
    
    int frameNum = 0;
    while (true)
    {
      printf("stack frame %d %p\n", frameNum, lr - (uint64_t)&__start__);
      lr = *(uint64_t*)(fp + 8);
      fp = *(uint64_t*)fp;

      frameNum++;
      if (frameNum > 16 || fp == 0 || (fp & 0x7) != 0)
        break;
    }
  }
  else
  {
    printf("unintentional fault somewhere in deep (address) space at %p (type %d)\n", pc, desc);
    if (lr >= (uint64_t)&__start__ && lr < (uint64_t)&__rodata_start)
      printf("lr in range: %p\n", lr - (uint64_t)&__start__);
  }
}

void QuickContextRestore(u64*) __attribute__((noreturn));

alignas(16) uint8_t __nx_exception_stack[0x8000];
uint64_t __nx_exception_stack_size = 0x8000;

void __libnx_exception_handler(ThreadExceptionDump* ctx)
{
  if (Common::PageFaultHandler::PageFaultHandler(ctx))
    QuickContextRestore(&ctx->cpu_gprs[0].x);
  else
    HandleFault(ctx->pc.x, ctx->lr.x, ctx->fp.x, ctx->far.x, ctx->error_desc);
}

}

#endif

#ifdef _WIN32
#include "common/windows_headers.h"
#include "win32_host_interface.h"
#include <shellapi.h>
#endif

static std::unique_ptr<NoGUIHostInterface> CreateHostInterface()
{
  const char* platform = std::getenv("DUCKSTATION_NOGUI_PLATFORM");
  std::unique_ptr<NoGUIHostInterface> host_interface;

#ifdef WITH_SDL2
  if (!host_interface && (!platform || StringUtil::Strcasecmp(platform, "sdl") == 0) && IsSDLHostInterfaceAvailable())
    host_interface = SDLHostInterface::Create();
#endif

#ifdef WITH_VTY
  if (!host_interface && (!platform || StringUtil::Strcasecmp(platform, "vty") == 0))
    host_interface = VTYHostInterface::Create();
#endif

#ifdef _WIN32
  if (!host_interface && (!platform || StringUtil::Strcasecmp(platform, "win32") == 0))
    host_interface = Win32HostInterface::Create();
#endif

#ifdef __SWITCH__
  host_interface = SwitchHostInterface::Create();
#endif

  return host_interface;
}

static int Run(std::unique_ptr<NoGUIHostInterface> host_interface, std::unique_ptr<SystemBootParameters> boot_params)
{
  if (!host_interface->Initialize())
  {
    host_interface->Shutdown();
    return EXIT_FAILURE;
  }

  if (boot_params)
    host_interface->BootSystem(std::move(boot_params));

  int result;
  if (System::IsValid() || !host_interface->InBatchMode())
  {
    host_interface->Run();
    result = EXIT_SUCCESS;
  }
  else
  {
    host_interface->ReportError("No file specified, and we're in batch mode. Exiting.");
    result = EXIT_FAILURE;
  }

  host_interface->Shutdown();
  return result;
}

#ifdef _WIN32

int wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
  std::unique_ptr<NoGUIHostInterface> host_interface = CreateHostInterface();
  std::unique_ptr<SystemBootParameters> boot_params;

  {
    std::vector<std::string> argc_strings;
    argc_strings.reserve(1);

    // CommandLineToArgvW() only adds the program path if the command line is empty?!
    argc_strings.push_back(FileSystem::GetProgramPath());

    if (std::wcslen(lpCmdLine) > 0)
    {
      int argc;
      LPWSTR* argv_wide = CommandLineToArgvW(lpCmdLine, &argc);
      if (argv_wide)
      {
        for (int i = 0; i < argc; i++)
          argc_strings.push_back(StringUtil::WideStringToUTF8String(argv_wide[i]));

        LocalFree(argv_wide);
      }
    }

    std::vector<char*> argc_pointers;
    argc_pointers.reserve(argc_strings.size());
    for (std::string& arg : argc_strings)
      argc_pointers.push_back(arg.data());

    if (!host_interface->ParseCommandLineParameters(static_cast<int>(argc_pointers.size()), argc_pointers.data(),
                                                    &boot_params))
    {
      return EXIT_FAILURE;
    }
  }

  return Run(std::move(host_interface), std::move(boot_params));
}

#else

int main(int argc, char* argv[])
{
#ifdef __SWITCH__
  socketInitializeDefault();
  nxlinkStdio();
#endif

  std::unique_ptr<NoGUIHostInterface> host_interface = CreateHostInterface();
  std::unique_ptr<SystemBootParameters> boot_params;
  if (!host_interface->ParseCommandLineParameters(argc, argv, &boot_params))
    return EXIT_FAILURE;

  int result = Run(std::move(host_interface), std::move(boot_params));

#ifdef __SWITCH__
  socketExit();
#endif
  return result;
}

#endif