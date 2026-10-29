// =============================================================================
//  Need for Speed: Carbon (Xbox 360, Title ID: 454107EC) - App Definition
// =============================================================================

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/rex_app.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#if defined(__APPLE__)
#include "audio/coreaudio_driver.h"
#endif

extern "C" const rex::PPCImageConfig PPCImageConfig;

class NfsCarbonApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<NfsCarbonApp>(new NfsCarbonApp(ctx, "nfscarbon",
        PPCImageConfig));
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
#if defined(__APPLE__)
    // iOS Documents directory resolution
    const std::filesystem::path docs_dir = rex::filesystem::GetUserFolder();
    if (!docs_dir.empty()) {
      user_data_root_ = docs_dir;
      paths.user_data_root = docs_dir;
      paths.cache_root = docs_dir / "cache";
      paths.config_path = docs_dir / "nfscarbon.toml";
    }

    if (paths.game_data_root.empty()) {
      const std::filesystem::path candidates[] = {
          docs_dir / "game",
          docs_dir,
          std::filesystem::current_path() / "game",
      };
      for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!candidate.empty() && std::filesystem::is_directory(candidate / "NFS", ec)) {
          paths.game_data_root = candidate;
          break;
        }
      }
    }
#else
    if (!paths.game_data_root.empty()) {
      return;
    }
    const std::filesystem::path exe_dir = rex::filesystem::GetExecutableFolder();
    const std::filesystem::path candidates[] = {
        exe_dir / "game",
        std::filesystem::current_path() / "game",
        (exe_dir / ".." / ".." / ".." / "game").lexically_normal(),
    };
    for (const auto& candidate : candidates) {
      std::error_code ec;
      if (std::filesystem::is_directory(candidate / "NFS", ec)) {
        paths.game_data_root = candidate;
        return;
      }
    }
#endif
  }

  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override {
    (void)resume;
    rex::PathConfig paths = defaults;
    std::string configured_root = rex::cvar::GetFlagByName("game_data_root");
    if (!configured_root.empty()) {
      paths.game_data_root = configured_root;
    }
    return paths;
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }

#if defined(__APPLE__)
    config.audio_factory = REX_AUDIO_BACKEND(rex::audio::ios::CoreAudioSystem);
#endif

    auto set_default = [](const char* name, const char* value) {
      if (rex::cvar::GetFlagSource(name) == rex::cvar::Source::kDefault) {
        rex::cvar::SetFlagByName(name, value);
      }
    };
    set_default("input_backend", "sdl");
    set_default("mnk_mode", "true");
    set_default("mnk_mouse", "true");
    set_default("mnk_sensitivity", "1.5");
    set_default("keybind_a", "Space");       // Handbrake
    set_default("keybind_b", "F");           // Nitrous
    set_default("keybind_x", "LMB");         // Speedbreaker
    set_default("keybind_y", "E");           // Crew / Wingman Command
    set_default("keybind_left_shoulder", "Q");  // Look Back
    set_default("keybind_right_shoulder", "RMB"); // Camera
    set_default("keybind_left_trigger", "Shift"); // Brake / Reverse
    set_default("keybind_right_trigger", "Control"); // Accelerate
    set_default("keybind_lstick_up", "W");
    set_default("keybind_lstick_down", "S");
    set_default("keybind_lstick_left", "A");
    set_default("keybind_lstick_right", "D");
    set_default("keybind_lstick_press", "X");
    set_default("keybind_rstick_up", "Up");
    set_default("keybind_rstick_down", "Down");
    set_default("keybind_rstick_left", "Left");
    set_default("keybind_rstick_right", "Right");
    set_default("keybind_rstick_press", "R");
    set_default("keybind_dpad_up", "Shift+Up");
    set_default("keybind_dpad_down", "Shift+Down");
    set_default("keybind_dpad_left", "Shift+Left");
    set_default("keybind_dpad_right", "Shift+Right");
    set_default("keybind_back", "Tab");
    set_default("keybind_start", "Escape");
  }

  void OnPostSetup() override {
    rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "true");
  }

 private:
  std::filesystem::path user_data_root_;
};
