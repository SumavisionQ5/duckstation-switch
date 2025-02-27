// SPDX-FileCopyrightText: 2019-2023 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#include "page_fault_handler.h"

#include "common/assert.h"
#include "common/log.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

Log_SetChannel(Common::PageFaultHandler);

#if defined(_WIN32)
#include "common/windows_headers.h"
#elif defined(__linux__) || defined(__ANDROID__)
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#define USE_SIGSEGV 1
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <signal.h>
#include <unistd.h>
#define USE_SIGSEGV 1
#endif

#ifdef __APPLE__
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/task.h>
#elif defined(__SWITCH__)
#include "switch_exception_frame.h"
#endif

namespace Common::PageFaultHandler {

static std::recursive_mutex s_exception_handler_mutex;
static Handler s_exception_handler_callback;
static bool s_in_exception_handler;

#if defined(CPU_ARCH_ARM64)
static bool IsStoreInstruction(const void* ptr)
{
  u32 bits;
  std::memcpy(&bits, ptr, sizeof(bits));

  // Based on vixl's disassembler Instruction::IsStore().
  // if (Mask(LoadStoreAnyFMask) != LoadStoreAnyFixed)
  if ((bits & 0x0a000000) != 0x08000000)
    return false;

  // if (Mask(LoadStorePairAnyFMask) == LoadStorePairAnyFixed)
  if ((bits & 0x3a000000) == 0x28000000)
  {
    // return Mask(LoadStorePairLBit) == 0
    return (bits & (1 << 22)) == 0;
  }

  switch (bits & 0xC4C00000)
  {
    case 0x00000000: // STRB_w
    case 0x40000000: // STRH_w
    case 0x80000000: // STR_w
    case 0xC0000000: // STR_x
    case 0x04000000: // STR_b
    case 0x44000000: // STR_h
    case 0x84000000: // STR_s
    case 0xC4000000: // STR_d
    case 0x04800000: // STR_q
      return true;

    default:
      return false;
  }
}
#elif defined(CPU_ARCH_RISCV64)
static bool IsStoreInstruction(const void* ptr)
{
  u32 bits;
  std::memcpy(&bits, ptr, sizeof(bits));

  return ((bits & 0x7Fu) == 0b0100011u);
}
#endif

#if defined(_WIN32) && (defined(CPU_ARCH_X64) || defined(CPU_ARCH_ARM64))
static PVOID s_veh_handle;

static LONG ExceptionHandler(PEXCEPTION_POINTERS exi)
{
  // Executing the handler concurrently from multiple threads wouldn't go down well.
  std::unique_lock lock(s_exception_handler_mutex);

  // Prevent recursive exception filtering.
  if (s_in_exception_handler)
    return EXCEPTION_CONTINUE_SEARCH;

  // Only interested in page faults.
  if (exi->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
    return EXCEPTION_CONTINUE_SEARCH;

#if defined(_M_AMD64)
  void* const exception_pc = reinterpret_cast<void*>(exi->ContextRecord->Rip);
#elif defined(_M_ARM64)
  void* const exception_pc = reinterpret_cast<void*>(exi->ContextRecord->Pc);
#else
  void* const exception_pc = nullptr;
#endif

  void* const exception_address = reinterpret_cast<void*>(exi->ExceptionRecord->ExceptionInformation[1]);
  const bool is_write = exi->ExceptionRecord->ExceptionInformation[0] == 1;

  s_in_exception_handler = true;

  const HandlerResult handled = s_exception_handler_callback(exception_pc, exception_address, is_write);

  s_in_exception_handler = false;

  return (handled == HandlerResult::ContinueExecution) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

#elif defined(USE_SIGSEGV)

static struct sigaction s_old_sigsegv_action;
#if defined(__APPLE__) || defined(__aarch64__)
static struct sigaction s_old_sigbus_action;
#endif

static void CallExistingSignalHandler(int signal, siginfo_t* siginfo, void* ctx)
{
#if defined(__aarch64__)
  const struct sigaction& sa = (signal == SIGBUS) ? s_old_sigbus_action : s_old_sigsegv_action;
#elif defined(__APPLE__)
  const struct sigaction& sa = s_old_sigbus_action;
#else
  const struct sigaction& sa = s_old_sigsegv_action;
#endif

  if (sa.sa_flags & SA_SIGINFO)
  {
    sa.sa_sigaction(signal, siginfo, ctx);
  }
  else if (sa.sa_handler == SIG_DFL)
  {
    // Re-raising the signal would just queue it, and since we'd restore the handler back to us,
    // we'd end up right back here again. So just abort, because that's probably what it'd do anyway.
    abort();
  }
  else if (sa.sa_handler != SIG_IGN)
  {
    sa.sa_handler(signal);
  }
}

static void SignalHandler(int sig, siginfo_t* info, void* ctx)
{
  // Executing the handler concurrently from multiple threads wouldn't go down well.
  std::unique_lock lock(s_exception_handler_mutex);

  // Prevent recursive exception filtering.
  if (s_in_exception_handler)
  {
    lock.unlock();
    CallExistingSignalHandler(sig, info, ctx);
    return;
  }

#if defined(__linux__) || defined(__ANDROID__)
  void* const exception_address = reinterpret_cast<void*>(info->si_addr);

#if defined(CPU_ARCH_X64)
  void* const exception_pc = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext.gregs[REG_RIP]);
  const bool is_write = (static_cast<ucontext_t*>(ctx)->uc_mcontext.gregs[REG_ERR] & 2) != 0;
#elif defined(CPU_ARCH_ARM32)
  void* const exception_pc = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext.arm_pc);
  const bool is_write = (static_cast<ucontext_t*>(ctx)->uc_mcontext.error_code & (1 << 11)) != 0; // DFSR.WnR
#elif defined(CPU_ARCH_ARM64)
  void* const exception_pc = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext.pc);
  const bool is_write = IsStoreInstruction(exception_pc);
#elif defined(CPU_ARCH_RISCV64)
  void* const exception_pc = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext.__gregs[REG_PC]);
  const bool is_write = IsStoreInstruction(exception_pc);
#else
  void* const exception_pc = nullptr;
  const bool is_write = false;
#endif

#elif defined(__APPLE__)

#if defined(CPU_ARCH_X64)
  void* const exception_address =
    reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext->__es.__faultvaddr);
  void* const exception_pc = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext->__ss.__rip);
  const bool is_write = (static_cast<ucontext_t*>(ctx)->uc_mcontext->__es.__err & 2) != 0;
#elif defined(CPU_ARCH_ARM64)
  void* const exception_address = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext->__es.__far);
  void* const exception_pc = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext->__ss.__pc);
  const bool is_write = IsStoreInstruction(exception_pc);
#else
  void* const exception_address = reinterpret_cast<void*>(info->si_addr);
  void* const exception_pc = nullptr;
  const bool is_write = false;
#endif

#elif defined(__FreeBSD__)

#if defined(CPU_ARCH_X64)
  void* const exception_address = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext.mc_addr);
  void* const exception_pc = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext.mc_rip);
  const bool is_write = (static_cast<ucontext_t*>(ctx)->uc_mcontext.mc_err & 2) != 0;
#elif defined(CPU_ARCH_ARM64)
  void* const exception_address = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext->__es.__far);
  void* const exception_pc = reinterpret_cast<void*>(static_cast<ucontext_t*>(ctx)->uc_mcontext->__ss.__pc);
  const bool is_write = IsStoreInstruction(exception_pc);
#else
  void* const exception_address = reinterpret_cast<void*>(info->si_addr);
  void* const exception_pc = nullptr;
  const bool is_write = false;
#endif

#endif

  s_in_exception_handler = true;

  const HandlerResult result = s_exception_handler_callback(exception_pc, exception_address, is_write);

  s_in_exception_handler = false;

  // Resumes execution right where we left off (re-executes instruction that caused the SIGSEGV).
  if (result == HandlerResult::ContinueExecution)
    return;

  // Call old signal handler, which will likely dump core.
  lock.unlock();
  CallExistingSignalHandler(sig, info, ctx);
}

#elif defined(__SWITCH__)

bool PageFaultHandler(ExceptionFrameA64* frame)
{
  // Executing the handler concurrently from multiple threads wouldn't go down well.
  std::unique_lock lock(s_exception_handler_mutex);

  // Prevent recursive exception filtering.
  if (s_in_exception_handler)
    return false;

  void* const exception_pc = reinterpret_cast<void*>(frame->pc);
  void* const exception_address = reinterpret_cast<void*>(frame->far);
  const bool is_write = IsStoreInstruction(exception_pc);

  s_in_exception_handler = true;

  HandlerResult handled = HandlerResult::ExecuteNextHandler;
  if (s_exception_handler_callback)
    handled = s_exception_handler_callback(exception_pc, exception_address, is_write);

  s_in_exception_handler = false;

  return handled == HandlerResult::ContinueExecution;
}

#endif

bool InstallHandler(Handler handler)
{
  std::unique_lock lock(s_exception_handler_mutex);
  AssertMsg(!s_exception_handler_callback, "A page fault handler is already registered.");
  if (!s_exception_handler_callback)
  {
#if defined(_WIN32) && (defined(CPU_ARCH_X64) || defined(CPU_ARCH_ARM64))
    s_veh_handle = AddVectoredExceptionHandler(1, ExceptionHandler);
    if (!s_veh_handle)
    {
      Log_ErrorPrint("Failed to add vectored exception handler");
      return false;
    }
#elif defined(USE_SIGSEGV)
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = SignalHandler;
#ifdef __linux__
    // Don't block the signal from executing recursively, we want to fire the original handler.
    sa.sa_flags |= SA_NODEFER;
#endif
    if (sigaction(SIGSEGV, &sa, &s_old_sigsegv_action) != 0)
      return false;
#if defined(__APPLE__) || defined(__aarch64__)
    // MacOS uses SIGBUS for memory permission violations
    if (sigaction(SIGBUS, &sa, &s_old_sigbus_action) != 0)
      return false;
#endif
#ifdef __APPLE__
    task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS, MACH_PORT_NULL, EXCEPTION_DEFAULT, 0);
#endif
#elif !defined(__SWITCH__) // handler is staically linked on Switch
    return false;
#endif
  }

  s_exception_handler_callback = handler;
  return true;
}

bool RemoveHandler(Handler handler)
{
  std::unique_lock lock(s_exception_handler_mutex);
  AssertMsg(!s_exception_handler_callback || s_exception_handler_callback == handler,
            "Not removing the same handler previously registered.");
  if (!s_exception_handler_callback)
    return false;

  s_exception_handler_callback = nullptr;

#if defined(_WIN32) && (defined(CPU_ARCH_X64) || defined(CPU_ARCH_ARM64))
  RemoveVectoredExceptionHandler(s_veh_handle);
  s_veh_handle = nullptr;
#elif defined(USE_SIGSEGV)
  struct sigaction sa;
#if defined(__APPLE__) || defined(__aarch64__)
  sigaction(SIGBUS, &s_old_sigbus_action, &sa);
  s_old_sigbus_action = {};
#endif
#if !defined(__APPLE__) || defined(__aarch64__)
  sigaction(SIGSEGV, &s_old_sigsegv_action, &sa);
  s_old_sigsegv_action = {};
#endif
#elif !defined(__SWITCH__) // handler is statically linked on Switch
  return false;
#endif

  return true;
}

} // namespace Common::PageFaultHandler
