// =============================================================================
//  Need for Speed: Carbon - iOS Recompiled Application Host
// =============================================================================

#include "nfscarbon_init.h"
#include "nfscarbon_app.h"

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>

#include "achievements_export.h"
#include "audio/coreaudio_driver.h"
#include "battery.h"
#include "boot_trace.h"
#include "display_rate.h"
#include "gpu_capture.h"
#include "ingame_options.h"
#include "launcher.h"
#include "lifecycle.h"
#include "touch_controls.h"

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <cstring>

extern "C" void nfsmw_ios_suspend_paints(bool suspend);

REXCVAR_DECLARE(bool, ios_gpu_capture);  // ios_main.mm
REXCVAR_DECLARE(int32_t, ios_fps_cap);    // present pacing

REXCVAR_DEFINE_BOOL(ios_touch_controls, true, "Input",
                    "iOS: on-screen touch controls (auto-hide when a gamepad/keyboard connects)");
REXCVAR_DEFINE_BOOL(ios_show_fps, true, "Display",
                    "iOS: frames per second and ms per frame in the top-left corner");

class IosNfsCarbonApp : public NfsCarbonApp {
 public:
  using NfsCarbonApp::NfsCarbonApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<IosNfsCarbonApp>(new IosNfsCarbonApp(ctx, "nfscarbon", PPCImageConfig));
  }

 protected:
  void OnPreSetup(rex::RuntimeConfig& config) override {
    NfsCarbonApp::OnPreSetup(config);
    config.audio_factory = REX_AUDIO_BACKEND(rex::audio::ios::CoreAudioSystem);
  }

  static void LogBuildIds() {
    for (uint32_t i = 0; i < _dyld_image_count(); ++i) {
      const char* path = _dyld_get_image_name(i);
      const char* name = path ? std::strrchr(path, '/') : nullptr;
      name = name ? name + 1 : path;
      if (!name || (std::strncmp(name, "librex", 6) != 0 && std::strcmp(name, "nfscarbon") != 0)) {
        continue;
      }
      const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
      const auto* cmd = reinterpret_cast<const load_command*>(header + 1);
      for (uint32_t c = 0; c < header->ncmds; ++c) {
        if (cmd->cmd == LC_UUID) {
          const uint8_t* u = reinterpret_cast<const uuid_command*>(cmd)->uuid;
          REXLOG_INFO("[ios] build of {}: {:02X}{:02X}{:02X}{:02X}-{:02X}{:02X}-{:02X}{:02X}-"
                      "{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}",
                      name, u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10],
                      u[11], u[12], u[13], u[14], u[15]);
          break;
        }
        cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(cmd) + cmd->cmdsize);
      }
    }
  }

  void OnPostSetup() override {
    nfsmw::boot::Trace("game: runtime set up (OnPostSetup)");
    LogBuildIds();
    NfsCarbonApp::OnPostSetup();

    // Lifecycle: suspend paints when backgrounded
    nfsmw::lifecycle::Install([] { nfsmw_ios_suspend_paints(true); },
                              [] { nfsmw_ios_suspend_paints(false); });

    // Achievements export for launcher
    nfsmw::achievements::Export(nfsmw::launcher::Documents());

    // Touch controls installation
    if (REXCVAR_GET(ios_touch_controls)) {
      nfsmw::touch::Install();
    }
    if (REXCVAR_GET(ios_show_fps)) {
      nfsmw::touch::InstallFpsCounter();
    }

    // ProMotion display rate request
    nfsmw::display::RequestRateForCap(REXCVAR_GET(ios_fps_cap));

    if (auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system())) {
      input->SetActiveCallback([] { return !nfsmw::options::BlocksGameInput(); });
    }
    nfsmw::options::Install();

    if (REXCVAR_GET(ios_gpu_capture)) {
      nfsmw::gpu_capture::InstallButton();
    }
  }

  void OnShutdown() override { NfsCarbonApp::OnShutdown(); }
};

REX_DEFINE_APP(nfscarbon, IosNfsCarbonApp::Create)
