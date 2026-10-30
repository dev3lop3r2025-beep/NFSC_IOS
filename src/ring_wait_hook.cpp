// =============================================================================
//  Need for Speed: Carbon - iOS/Apple Silicon D3D Ring Wait Hook
//
//  Replaces spin-wait in the game's D3D GPU command processor ring wait with
//  Darwin os_sync_wait_on_address / thread wait, preventing CPU core saturation
//  and thermal throttling.
//
//  USA Carbon Watchdog: sub_826E0088
//  Return Addresses: 0x826DBC4C (sub_826DBBB0), 0x826DBA30 (sub_826DB9B8)
//  Device Read Pointer Offset: 10768
// =============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <thread>
#include <algorithm>

#if defined(__APPLE__)
#include <os/os_sync_wait_on_address.h>
#include <mach/mach_time.h>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

#include "generated/default/nfscarbon_pch.h"

REXCVAR_DEFINE_INT32(ios_carbon_ring_wait_ms, 0, "GPU",
                     "Fixed cap (ms) for ring-space waits (0: automatic based on FPS cap)");

extern "C" void __imp__sub_826E0088(PPCContext& __restrict ctx, uint8_t* base);

namespace {

constexpr uint32_t kRingWaitReturn1 = 0x826DBC4C;  // sub_826DBBB0
constexpr uint32_t kRingWaitReturn2 = 0x826DBA30;  // sub_826DB9B8
constexpr uint32_t kDeviceRptrPointer = 10768;

std::atomic<uint32_t>* RptrSequence() {
  static std::atomic<uint32_t>* const sequence = []() -> std::atomic<uint32_t>* {
    const char* off = std::getenv("NFSC_RING_WAIT");
    if (off && std::strcmp(off, "0") == 0) {
      return nullptr;
    }
    for (const char* name : {"@rpath/librexgpu-carbon.dylib", "@rpath/librexgpu-xenos.dylib",
                             "librexgpu-carbon.so", "librexgpu-xenos.so"}) {
      if (void* plugin = dlopen(name, RTLD_NOW | RTLD_NOLOAD)) {
        if (void* symbol = dlsym(plugin, "nfsmw_cp_rptr_seq")) {
          return static_cast<std::atomic<uint32_t>*>(symbol);
        }
      }
    }
    return nullptr;
  }();
  return sequence;
}

}  // namespace

extern "C" REX_FUNC(sub_826E0088) {
  const uint32_t ret = uint32_t(ctx.lr);
  if (ret == kRingWaitReturn1 || ret == kRingWaitReturn2) {
    if (std::atomic<uint32_t>* sequence = RptrSequence()) {
      const uint32_t before = sequence->load(std::memory_order_acquire);
      const uint32_t state = ctx.r3.u32;
      const uint32_t device = REX_LOAD_U32(state + 0);
      const uint32_t seen = REX_LOAD_U32(state + 8);
      const uint32_t rptr = REX_LOAD_U32(REX_LOAD_U32(device + kDeviceRptrPointer));

      if (rptr == seen) {
        const int32_t fixed_ms = REXCVAR_GET(ios_carbon_ring_wait_ms);
        const int32_t set_cap = rex::cvar::Query<int32_t>("ios_fps_cap");
        const int32_t cap = set_cap > 0 ? set_cap : 60;
        const uint64_t timeout_ns = fixed_ms > 0
                                        ? uint64_t(std::min<int32_t>(fixed_ms, 50)) * 1000000
                                        : uint64_t(std::clamp(500.0 / cap, 1.0, 50.0) * 1e6);

#if defined(__APPLE__)
        os_sync_wait_on_address_with_timeout(
            sequence, before, sizeof(uint32_t),
            OS_SYNC_WAIT_ON_ADDRESS_NONE,
            OS_CLOCK_MACH_ABSOLUTE_TIME, timeout_ns);
#else
        std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<uint64_t>(timeout_ns, 1000000)));
#endif
      }
    }
  }
  __imp__sub_826E0088(ctx, base);
}

