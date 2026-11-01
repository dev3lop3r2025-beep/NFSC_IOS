#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <spdlog/spdlog.h>

#include "carbon_missing_entries.h"

#if __has_include("../generated/default/nfscarbon_pch.h")
#include "../generated/default/nfscarbon_pch.h"
#elif __has_include("generated/default/nfscarbon_pch.h")
#include "generated/default/nfscarbon_pch.h"
#elif __has_include("nfscarbon_pch.h")
#include "nfscarbon_pch.h"
#elif __has_include("../generated/default/nfsc_pch.h")
#include "../generated/default/nfsc_pch.h"
#elif __has_include("generated/default/nfsc_pch.h")
#include "generated/default/nfsc_pch.h"
#elif __has_include("nfsc_pch.h")
#include "nfsc_pch.h"
#endif

#if defined(__APPLE__)
// On Mach-O, weak_import is required for undefined weak symbols (not [[gnu::weak]]).
#define CARBON_WEAK_IMPORT __attribute__((weak_import))
#elif defined(__GNUC__) || defined(__clang__)
#define CARBON_WEAK_IMPORT __attribute__((weak))
#else
#define CARBON_WEAK_IMPORT
#endif

// Forward declarations of verified compiled guest routines
extern "C" {
// __imp__ variants delegate to the rexglue-compiled function body.
// Direct (non-imp) variants are the REX_FUNC wrappers defined in this file.
CARBON_WEAK_IMPORT void __imp__xstart(PPCContext& __restrict ctx, uint8_t* base);
void sub_824E9780(PPCContext& __restrict ctx, uint8_t* base);
CARBON_WEAK_IMPORT void __imp__sub_824E9780(PPCContext& __restrict ctx, uint8_t* base);
CARBON_WEAK_IMPORT void __imp__sub_829446E0(PPCContext& __restrict ctx, uint8_t* base);
void sub_82944340(PPCContext& __restrict ctx, uint8_t* base);

// Kernel import __imp__ symbols -- used for version-agnostic address discovery.
// rexglue registers: SetFunction(<guest_addr>, __imp__KernelFunc) in nfsc_register.cpp.
// We find the guest address at runtime by looking up the __imp__ pointer in the dispatcher.
CARBON_WEAK_IMPORT void __imp__XamLoaderTerminateTitle(PPCContext& __restrict ctx, uint8_t* base);
CARBON_WEAK_IMPORT void __imp__ExTerminateThread(PPCContext& __restrict ctx, uint8_t* base);
CARBON_WEAK_IMPORT void __imp__XexUnloadImageAndExitThread(PPCContext& __restrict ctx, uint8_t* base);
CARBON_WEAK_IMPORT void __imp__HalReturnToFirmware(PPCContext& __restrict ctx, uint8_t* base);
CARBON_WEAK_IMPORT void __imp__KeBugCheck(PPCContext& __restrict ctx, uint8_t* base);
CARBON_WEAK_IMPORT void __imp__KeBugCheckEx(PPCContext& __restrict ctx, uint8_t* base);
CARBON_WEAK_IMPORT void __imp__DbgPrint(PPCContext& __restrict ctx, uint8_t* base);
}

namespace {

void LogBootBreadcrumb(const char* message) {
  std::filesystem::path docs = rex::filesystem::GetUserFolder();
  if (!docs.empty()) {
    if (std::FILE* f = std::fopen((docs / "logs" / "boot.txt").string().c_str(), "a")) {
      std::fprintf(f, "%s\n", message);
      std::fclose(f);
    }
  }
}

void RecordUnregisteredTarget(uint32_t address, uint32_t caller_lr) {
  static std::mutex mutex;
  static std::set<uint32_t> seen;
  std::lock_guard<std::mutex> lock(mutex);
  if (seen.insert(address).second) {
    REXLOG_ERROR("[dispatch] UNRESOLVED INDIRECT CALL: target=0x{:08X}, lr=0x{:08X}",
                 address, caller_lr);
    rex::FlushLogging();
    if (auto logger = spdlog::default_logger()) {
      logger->flush();
    }

    std::filesystem::path log_path = "unregistered_targets.txt";
    std::filesystem::path docs = rex::filesystem::GetUserFolder();
    if (!docs.empty()) {
      log_path = docs / "logs" / "unregistered_targets.txt";
    }
    if (std::FILE* f = std::fopen(log_path.string().c_str(), "a")) {
      std::fprintf(f, "0x%08X (caller lr=0x%08X)\n", address, caller_lr);
      std::fclose(f);
    }
  }
}

void NoopTrap(PPCContext& ctx, uint8_t* /*base*/) {
  RecordUnregisteredTarget(static_cast<uint32_t>(ctx.last_indirect_target),
                           static_cast<uint32_t>(ctx.lr));
  ctx.r3.u64 = 0;
}

static ::PPCFunc* original_terminate_title = nullptr;
static ::PPCFunc* original_terminate_thread = nullptr;
static ::PPCFunc* original_unload_and_exit = nullptr;
static ::PPCFunc* original_return_to_firmware = nullptr;
static ::PPCFunc* original_bug_check = nullptr;
static ::PPCFunc* original_bug_check_ex = nullptr;
static ::PPCFunc* original_dbg_print = nullptr;
static ::PPCFunc* original_carbon_main = nullptr;
static ::PPCFunc* original_xstart = nullptr;
static ::PPCFunc* original_entry_point = nullptr;

// =============================================================================
//  Startup Breadcrumb Hooks
// =============================================================================

// 1. Entry point 0x829446E0 breadcrumb:
// In the USA default.xex, 0x829446E0 IS the genuine XEX entry point compiled by
// rexglue into sub_829446E0. This is NOT a blr stub (that was the PAL XEX).
// We log entry for boot diagnostics and then delegate to the compiled USA code.
//
// The hook delegates primarily to original_entry_point captured from the
// dispatcher at initialization time, with __imp__sub_829446E0 as a weak fallback.
void Carbon_EntryPoint_0x829446E0_Hook(PPCContext& ctx, uint8_t* base) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[carbon-boot] GUEST ENTRY 0x829446E0 (USA genuine entry point) called (lr=0x%08X, sp=0x%08X, r13=0x%08X)",
                static_cast<uint32_t>(ctx.lr), ctx.r1.u32, ctx.r13.u32);
  REXLOG_INFO("{}", buf);
  LogBootBreadcrumb(buf);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }

  // Delegate to the genuine USA compiled entry point.
  // 1. If original entry function was registered in dispatcher, call it.
  // 2. Otherwise fall back to __imp__sub_829446E0 if available.
  if (original_entry_point) {
    original_entry_point(ctx, base);
  } else if (&__imp__sub_829446E0) {
    __imp__sub_829446E0(ctx, base);
  }

  REXLOG_INFO("[carbon-boot] <<< sub_829446E0 (USA entry 0x829446E0) finished (r3={})", ctx.r3.s32);
  LogBootBreadcrumb("[carbon-boot] <<< sub_829446E0 (USA entry 0x829446E0) finished");
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
}

// NOTE: sub_829446E0 is NOT redefined here as REX_FUNC -- doing so conflicts with
// DEFINE_REX_FUNC(sub_829446E0) in the recomp library (nfsc_recomp.113.cpp).
// The hook is installed at runtime via CarbonInstallDiagnosticHooks()->dispatcher->SetFunction().

// 2. xstart (0x82944510) breadcrumb:
void Carbon_XStart_Hook(PPCContext& ctx, uint8_t* base) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[carbon-boot] >>> ENTERING xstart (0x82944510) - CRT startup sequence beginning (lr=0x%08X, sp=0x%08X)",
                static_cast<uint32_t>(ctx.lr), ctx.r1.u32);
  REXLOG_INFO("{}", buf);
  LogBootBreadcrumb(buf);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }

  if (original_xstart) {
    original_xstart(ctx, base);
  } else if (&__imp__xstart) {
    __imp__xstart(ctx, base);
  }

  REXLOG_INFO("[carbon-boot] <<< EXITED xstart (0x82944510) with r3={}", ctx.r3.s32);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
}

// 3. sub_82944340: Security/Display/Privilege check
// In xstart:
//   sub_82944340(ctx, base);
//   cmpwi r3, 0; beq loc_82944560;
//   __imp__XamLoaderTerminateTitle(ctx, base);
// If r3 == 0, xstart branches to loc_82944560 and proceeds to Carbon's main().
// If r3 != 0, xstart terminates the title immediately!
// We override this function to always return r3 = 0.
extern "C" REX_FUNC(sub_82944340) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[carbon-boot] sub_82944340 (display/privilege check) called from lr=0x%08X -> forcing r3=0 (proceed to loc_82944560)",
                static_cast<uint32_t>(ctx.lr));
  REXLOG_INFO("{}", buf);
  LogBootBreadcrumb(buf);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
  ctx.r3.s64 = 0;
}

// 4. Carbon main() (0x824E9780) hook:
void Carbon_Main_Hook(PPCContext& ctx, uint8_t* base) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[carbon-boot] ========================================================\n"
                "[carbon-boot] >>> CARBON main() REACHED at 0x824E9780! argc=%d, argv=0x%08X\n"
                "[carbon-boot] ========================================================",
                ctx.r3.s32, ctx.r4.u32);
  REXLOG_INFO("{}", buf);
  LogBootBreadcrumb(buf);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }

  if (original_carbon_main) {
    original_carbon_main(ctx, base);
  } else if (&__imp__sub_824E9780) {
    __imp__sub_824E9780(ctx, base);
  }

  REXLOG_INFO("[carbon-boot] <<< CARBON main() returned with code={:d} (0x{:08X})",
              ctx.r3.s32, ctx.r3.u32);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
}

// 5. Kernel exit hooks:
void Carbon_TerminateTitle_Hook(PPCContext& ctx, uint8_t* base) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[carbon-boot] XamLoaderTerminateTitle CALLED! lr=0x%08X, exit_code=%d (0x%08X), r4=0x%08X",
                static_cast<uint32_t>(ctx.lr), ctx.r3.s32, ctx.r3.u32, ctx.r4.u32);
  REXLOG_ERROR("{}", buf);
  LogBootBreadcrumb(buf);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
  std::fflush(nullptr);

  if (original_terminate_title) {
    original_terminate_title(ctx, base);
  }
}

void Carbon_ExTerminateThread_Hook(PPCContext& ctx, uint8_t* base) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[carbon-boot] ExTerminateThread CALLED! lr=0x%08X, exit_code=%d (0x%08X)",
                static_cast<uint32_t>(ctx.lr), ctx.r3.s32, ctx.r3.u32);
  REXLOG_ERROR("{}", buf);
  LogBootBreadcrumb(buf);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
  std::fflush(nullptr);

  if (original_terminate_thread) {
    original_terminate_thread(ctx, base);
  }
}

void Carbon_XexUnloadImageAndExitThread_Hook(PPCContext& ctx, uint8_t* base) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[carbon-boot] XexUnloadImageAndExitThread CALLED! lr=0x%08X, handle=0x%08X, exit_code=%d",
                static_cast<uint32_t>(ctx.lr), ctx.r3.u32, ctx.r4.s32);
  REXLOG_ERROR("{}", buf);
  LogBootBreadcrumb(buf);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
  std::fflush(nullptr);

  if (original_unload_and_exit) {
    original_unload_and_exit(ctx, base);
  }
}

void Carbon_HalReturnToFirmware_Hook(PPCContext& ctx, uint8_t* base) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[carbon-boot] HalReturnToFirmware CALLED! lr=0x%08X, routine=%d (0x%08X)",
                static_cast<uint32_t>(ctx.lr), ctx.r3.s32, ctx.r3.u32);
  REXLOG_ERROR("{}", buf);
  LogBootBreadcrumb(buf);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
  std::fflush(nullptr);

  if (original_return_to_firmware) {
    original_return_to_firmware(ctx, base);
  }
}

void Carbon_KeBugCheck_Hook(PPCContext& ctx, uint8_t* base) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[carbon-boot] KeBugCheck CALLED! lr=0x%08X, code=0x%08X",
                static_cast<uint32_t>(ctx.lr), ctx.r3.u32);
  REXLOG_ERROR("{}", buf);
  LogBootBreadcrumb(buf);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
  std::fflush(nullptr);

  if (original_bug_check) {
    original_bug_check(ctx, base);
  }
}

void Carbon_KeBugCheckEx_Hook(PPCContext& ctx, uint8_t* base) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[carbon-boot] KeBugCheckEx CALLED! lr=0x%08X, code=0x%08X, p1=0x%08X, p2=0x%08X, p3=0x%08X, p4=0x%08X",
                static_cast<uint32_t>(ctx.lr), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  REXLOG_ERROR("{}", buf);
  LogBootBreadcrumb(buf);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
  std::fflush(nullptr);

  if (original_bug_check_ex) {
    original_bug_check_ex(ctx, base);
  }
}

void Carbon_DbgPrint_Hook(PPCContext& ctx, uint8_t* base) {
  const char* str = ctx.r3.u32 ? reinterpret_cast<const char*>(base + ctx.r3.u32) : "<null>";
  REXLOG_INFO("[carbon] DbgPrint: format='{}', r4=0x{:08X} ({:d})", str, ctx.r4.u32, ctx.r4.s32);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
  if (original_dbg_print) {
    original_dbg_print(ctx, base);
  }
}

}  // namespace

void CarbonInstallDiagnosticHooks(rex::runtime::FunctionDispatcher* dispatcher, uint8_t* /*base*/) {
  if (!dispatcher) return;

  // 1. Instrument entry point 0x829446E0 with a breadcrumb logger.
  // The recomp-generated sub_829446E0 is registered in the dispatcher.
  // Save the original function pointer so the hook delegates to it.
  if (auto* orig = dispatcher->GetFunction(0x829446E0)) {
    original_entry_point = orig;
  }
  dispatcher->SetFunction(0x829446E0, &Carbon_EntryPoint_0x829446E0_Hook);
  REXLOG_INFO("[carbon] Hooked USA entry point 0x829446E0 with breadcrumb logger (dispatcher)");

  const uint32_t kImportScanStart = PPCImageConfig.code_base + PPCImageConfig.code_size - 0x100000;
  const uint32_t kImportScanEnd   = PPCImageConfig.code_base + PPCImageConfig.code_size;
  constexpr uint32_t kImportStep  = 4;

  // 2. Instrument xstart (0x82944510): find its address or locate via __imp__xstart.
  uint32_t xstart_addr = 0x82944510;
  if (&__imp__xstart) {
    for (uint32_t addr = kImportScanStart; addr < kImportScanEnd; addr += kImportStep) {
      if (dispatcher->GetFunction(addr) == reinterpret_cast<::PPCFunc*>(&__imp__xstart)) {
        xstart_addr = addr;
        break;
      }
    }
  }
  if (auto* orig = dispatcher->GetFunction(xstart_addr)) {
    original_xstart = orig;
  }
  dispatcher->SetFunction(xstart_addr, &Carbon_XStart_Hook);
  REXLOG_INFO("[carbon] Hooked xstart at 0x{:08X} with breadcrumb logger", xstart_addr);

  // 3. Override display/privilege check sub_82944340 to force success (r3 = 0).
  // This is a small function called inside xstart that checks display/privilege.
  // Its address is version-specific; we use the known PAL address and let it fail
  // gracefully if the USA address differs. TODO: update after USA codegen.
  dispatcher->SetFunction(0x82944340, &sub_82944340);
  REXLOG_INFO("[carbon] Overrode sub_82944340 with success handler (r3 = 0)");

  // 4. Instrument Carbon's real main().
  // Address is version-specific; fails gracefully if USA codegen moves it.
  // TODO: update 0x824E9780 to USA address after running codegen.
  if (auto* orig = dispatcher->GetFunction(0x824E9780)) {
    original_carbon_main = orig;
  }
  dispatcher->SetFunction(0x824E9780, &Carbon_Main_Hook);
  REXLOG_INFO("[carbon] Hooked Carbon main() (0x824E9780) with breadcrumb logger");

  // 5. Instrument kernel import thunks.
  // We use __imp__ function pointers directly: rexglue registers them as
  // SetFunction(<guest_thunk_addr>, __imp__KernelFunc) in nfsc_register.cpp.
  // By looking up the function pointer in the dispatcher we get the correct
  // guest address regardless of which XEX (PAL or USA) was used for codegen.
  struct KernelHook {
    ::PPCFunc* imp_ptr;          // __imp__KernelFunc pointer
    ::PPCFunc* hook;             // our diagnostic wrapper
    ::PPCFunc** original_out;    // where to save the original (may be nullptr)
    const char* name;            // for logging
  };
  const KernelHook kernel_hooks[] = {
    { reinterpret_cast<::PPCFunc*>(&__imp__XamLoaderTerminateTitle),
      &Carbon_TerminateTitle_Hook, &original_terminate_title,
      "XamLoaderTerminateTitle" },
    { reinterpret_cast<::PPCFunc*>(&__imp__ExTerminateThread),
      &Carbon_ExTerminateThread_Hook, &original_terminate_thread,
      "ExTerminateThread" },
    { reinterpret_cast<::PPCFunc*>(&__imp__XexUnloadImageAndExitThread),
      &Carbon_XexUnloadImageAndExitThread_Hook, &original_unload_and_exit,
      "XexUnloadImageAndExitThread" },
    { reinterpret_cast<::PPCFunc*>(&__imp__HalReturnToFirmware),
      &Carbon_HalReturnToFirmware_Hook, &original_return_to_firmware,
      "HalReturnToFirmware" },
    { reinterpret_cast<::PPCFunc*>(&__imp__KeBugCheck),
      &Carbon_KeBugCheck_Hook, &original_bug_check,
      "KeBugCheck" },
    { reinterpret_cast<::PPCFunc*>(&__imp__KeBugCheckEx),
      &Carbon_KeBugCheckEx_Hook, &original_bug_check_ex,
      "KeBugCheckEx" },
    { reinterpret_cast<::PPCFunc*>(&__imp__DbgPrint),
      &Carbon_DbgPrint_Hook, &original_dbg_print,
      "DbgPrint" },
  };
  for (const auto& h : kernel_hooks) {
    if (!h.imp_ptr) continue;
    bool hooked = false;
    for (uint32_t addr = kImportScanStart; addr < kImportScanEnd; addr += kImportStep) {
      if (dispatcher->GetFunction(addr) == h.imp_ptr) {
        if (h.original_out) *h.original_out = h.imp_ptr;
        dispatcher->SetFunction(addr, h.hook);
        REXLOG_INFO("[carbon] Hooked {} at 0x{:08X}", h.name, addr);
        hooked = true;
        break;
      }
    }
    if (!hooked) {
      REXLOG_WARN("[carbon] Could not find {} thunk in dispatcher (wrong XEX version?)", h.name);
    }
  }


  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
}

namespace rex::runtime {

::PPCFunc* ResolveIndirectFunction(uint32_t guest_address) {
  if (Runtime* rt = Runtime::instance()) {
    if (FunctionDispatcher* d = rt->function_dispatcher()) {
      if (::PPCFunc* f = d->GetFunction(guest_address)) {
        return f;
      }
    }
  }
  RecordUnregisteredTarget(guest_address, 0);
  return &NoopTrap;
}

}  // namespace rex::runtime
