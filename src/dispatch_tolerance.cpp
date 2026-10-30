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

namespace {

void RecordUnregisteredTarget(uint32_t address, uint32_t caller_lr) {
  static std::mutex mutex;
  static std::set<uint32_t> seen;
  std::lock_guard<std::mutex> lock(mutex);
  if (seen.insert(address).second) {
    REXLOG_ERROR("[dispatch] UNRESOLVED INDIRECT CALL: target=0x{:08X}, lr=0x{:08X}",
                 address, caller_lr);
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

static ::PPCFunc original_terminate_title = nullptr;
static ::PPCFunc original_dbg_print = nullptr;

void Carbon_TerminateTitle_Hook(PPCContext& ctx, uint8_t* base) {
  REXLOG_ERROR("[carbon] XamLoaderTerminateTitle CALLED! caller lr=0x{:08X}, exit_code(r3)={:d} (0x{:08X}), r4=0x{:08X}",
               ctx.lr, ctx.r3.s32, ctx.r3.u32, ctx.r4.u32);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
  std::fflush(nullptr);

  std::filesystem::path docs = rex::filesystem::GetUserFolder();
  if (!docs.empty()) {
    if (std::FILE* f = std::fopen((docs / "logs" / "boot.txt").string().c_str(), "a")) {
      std::fprintf(f, "XamLoaderTerminateTitle called: lr=0x%08X, exit_code=%d (0x%08X)\n",
                   static_cast<uint32_t>(ctx.lr), ctx.r3.s32, ctx.r3.u32);
      std::fclose(f);
    }
  }

  if (original_terminate_title) {
    original_terminate_title(ctx, base);
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

extern "C" REX_FUNC(sub_82944340) {
  REXLOG_INFO("[carbon] sub_82944340 (display/privilege check): forcing success (return 0)");
  ctx.r3.s64 = 0;
}

void CarbonInstallDiagnosticHooks(rex::runtime::FunctionDispatcher* dispatcher, uint8_t* base) {
  if (!dispatcher) return;

  if (auto* orig = dispatcher->GetFunction(0x82AACDF4)) {
    original_terminate_title = orig;
    dispatcher->SetFunction(0x82AACDF4, &Carbon_TerminateTitle_Hook);
    REXLOG_INFO("[carbon] Hooked XamLoaderTerminateTitle (0x82AACDF4)");
  }

  if (auto* orig = dispatcher->GetFunction(0x82AACE24)) {
    original_dbg_print = orig;
    dispatcher->SetFunction(0x82AACE24, &Carbon_DbgPrint_Hook);
    REXLOG_INFO("[carbon] Hooked DbgPrint (0x82AACE24)");
  }

  dispatcher->SetFunction(0x82944340, &sub_82944340);
  REXLOG_INFO("[carbon] Overrode sub_82944340 with success handler");
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
