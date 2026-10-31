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
#include "../generated/default/nfsc_pch.h"

// Forward declarations of verified compiled guest routines
extern "C" {
void xstart(PPCContext& __restrict ctx, uint8_t* base);
void __imp__xstart(PPCContext& __restrict ctx, uint8_t* base);
void sub_824E9780(PPCContext& __restrict ctx, uint8_t* base);
void __imp__sub_824E9780(PPCContext& __restrict ctx, uint8_t* base);
void sub_829446E0(PPCContext& __restrict ctx, uint8_t* base);
void __imp__sub_829446E0(PPCContext& __restrict ctx, uint8_t* base);
void sub_82944340(PPCContext& __restrict ctx, uint8_t* base);
void __imp__sub_82944340(PPCContext& __restrict ctx, uint8_t* base);
void sub_8294BF08(PPCContext& __restrict ctx, uint8_t* base);
void __imp__sub_8294BF08(PPCContext& __restrict ctx, uint8_t* base);
void sub_82949748(PPCContext& __restrict ctx, uint8_t* base);
void __imp__sub_82949748(PPCContext& __restrict ctx, uint8_t* base);
void sub_827A3300(PPCContext& __restrict ctx, uint8_t* base);
void __imp__sub_827A3300(PPCContext& __restrict ctx, uint8_t* base);
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

// =============================================================================
//  Startup Breadcrumb Hooks
// =============================================================================

// 1. Entry point 0x829446E0 redirection:
// The USA default.xex header has entry_point = 0x829446E0, which is just an epilogue
// blr stub (sub_829446E0). If executed directly, the guest thread terminates in 1 ms!
// We intercept 0x829446E0 and redirect execution to the real CRT entry point xstart (0x82944510).
void Carbon_EntryPoint_0x829446E0_Hook(PPCContext& ctx, uint8_t* base) {
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "[carbon-boot] GUEST ENTRY 0x829446E0 -> redirecting to xstart (0x82944510)! (lr=0x%08X, sp=0x%08X, r13=0x%08X)",
                static_cast<uint32_t>(ctx.lr), ctx.r1.u32, ctx.r13.u32);
  REXLOG_INFO("{}", buf);
  LogBootBreadcrumb(buf);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }

  // Invoke the real CRT entry point
  xstart(ctx, base);

  REXLOG_INFO("[carbon-boot] <<< xstart (0x82944510) finished execution (return code r3={})", ctx.r3.s32);
  LogBootBreadcrumb("[carbon-boot] <<< xstart (0x82944510) finished execution");
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
}

extern "C" REX_FUNC(sub_829446E0) {
  Carbon_EntryPoint_0x829446E0_Hook(ctx, base);
}

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
  } else {
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
  } else {
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

  // 1. Redirect dummy header entry point (0x829446E0) to xstart (0x82944510)
  dispatcher->SetFunction(0x829446E0, &Carbon_EntryPoint_0x829446E0_Hook);
  REXLOG_INFO("[carbon] Hooked header entry point 0x829446E0 -> redirects to xstart (0x82944510)");

  // 2. Instrument xstart (0x82944510)
  if (auto* orig = dispatcher->GetFunction(0x82944510)) {
    original_xstart = orig;
  }
  dispatcher->SetFunction(0x82944510, &Carbon_XStart_Hook);
  REXLOG_INFO("[carbon] Hooked xstart (0x82944510) with breadcrumb logger");

  // 3. Override display/privilege check sub_82944340 to force success (r3 = 0)
  dispatcher->SetFunction(0x82944340, &sub_82944340);
  REXLOG_INFO("[carbon] Overrode sub_82944340 with success handler (r3 = 0)");

  // 4. Instrument Carbon's real main() (0x824E9780)
  if (auto* orig = dispatcher->GetFunction(0x824E9780)) {
    original_carbon_main = orig;
  }
  dispatcher->SetFunction(0x824E9780, &Carbon_Main_Hook);
  REXLOG_INFO("[carbon] Hooked Carbon main() (0x824E9780) with breadcrumb logger");

  // 5. Instrument title/thread termination routines
  if (auto* orig = dispatcher->GetFunction(0x82AACDF4)) {
    original_terminate_title = orig;
    dispatcher->SetFunction(0x82AACDF4, &Carbon_TerminateTitle_Hook);
    REXLOG_INFO("[carbon] Hooked XamLoaderTerminateTitle (0x82AACDF4)");
  }
  if (auto* orig = dispatcher->GetFunction(0x82AAD5A4)) {
    original_terminate_thread = orig;
    dispatcher->SetFunction(0x82AAD5A4, &Carbon_ExTerminateThread_Hook);
    REXLOG_INFO("[carbon] Hooked ExTerminateThread (0x82AAD5A4)");
  }
  if (auto* orig = dispatcher->GetFunction(0x82AAD714)) {
    original_unload_and_exit = orig;
    dispatcher->SetFunction(0x82AAD714, &Carbon_XexUnloadImageAndExitThread_Hook);
    REXLOG_INFO("[carbon] Hooked XexUnloadImageAndExitThread (0x82AAD714)");
  }
  if (auto* orig = dispatcher->GetFunction(0x82AAD774)) {
    original_return_to_firmware = orig;
    dispatcher->SetFunction(0x82AAD774, &Carbon_HalReturnToFirmware_Hook);
    REXLOG_INFO("[carbon] Hooked HalReturnToFirmware (0x82AAD774)");
  }
  if (auto* orig = dispatcher->GetFunction(0x82AAC344)) {
    original_bug_check = orig;
    dispatcher->SetFunction(0x82AAC344, &Carbon_KeBugCheck_Hook);
    REXLOG_INFO("[carbon] Hooked KeBugCheck (0x82AAC344)");
  }
  if (auto* orig = dispatcher->GetFunction(0x82AAD494)) {
    original_bug_check_ex = orig;
    dispatcher->SetFunction(0x82AAD494, &Carbon_KeBugCheckEx_Hook);
    REXLOG_INFO("[carbon] Hooked KeBugCheckEx (0x82AAD494)");
  }

  // 6. Instrument DbgPrint
  if (auto* orig = dispatcher->GetFunction(0x82AACE24)) {
    original_dbg_print = orig;
    dispatcher->SetFunction(0x82AACE24, &Carbon_DbgPrint_Hook);
    REXLOG_INFO("[carbon] Hooked DbgPrint (0x82AACE24)");
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
