// NFSMW Recompiled - iOS entry point
//
// SDL_main.h turns this main() into SDL_main() and supplies the real main(),
// which starts UIKit (UIApplicationMain) and calls in here once the app has
// finished launching. Inside LiveContainer it is the same: LiveContainer calls
// the app's main().
//
// What the Android launcher screen (Ajustes.java) and GameActivity do, this
// file does here, without a settings screen for now:
//
//   - folders: everything lives in the app's Documents/, which is visible from
//     the Files app and from Finder (UIFileSharingEnabled in Info.plist):
//         Documents/logs/nfsmw.log      the log
//         Documents/vulkan_probe.txt    the probe report
//         Documents/data/               game saves and settings
//         Documents/*.iso               the game ISO, the first one found
//   - the game arguments, the same fixed set the Android app passes.
//   - probe or game: without generated code (NFSMW_WITH_GAME=0) or without an
//     ISO, the Vulkan probe; otherwise the game.
//   - a startup trace, Documents/logs/boot.txt (boot_trace.cpp): the SDK's
//     own log only opens partway through starting the game.
//   - Documents/extra_args.txt, if present: extra arguments, one per line, to
//     try a setting without rebuilding (the iOS counterpart of the Android
//     test bench's nfsmw.args). Each one REPLACES the app's own value for the
//     same option: the cvar parser (CLI11) does not accept a non-boolean
//     option twice, and a repeated one left others unapplied on Android.

#import <Foundation/Foundation.h>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_messagebox.h>

#include <sys/stat.h>
#include <sys/sysctl.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "audio/coreaudio_driver.h"
#include "battery.h"
#include "boot_trace.h"
#include "launcher.h"
#include "memory_report.h"
#include "probe.h"

#ifndef NFSCARBON_WITH_GAME
#ifdef NFSMW_WITH_GAME
#define NFSCARBON_WITH_GAME NFSMW_WITH_GAME
#else
#define NFSCARBON_WITH_GAME 0
#endif
#endif

#ifndef NFSMW_WITH_GAME
#define NFSMW_WITH_GAME NFSCARBON_WITH_GAME
#endif

#ifndef NFSCARBON_GAME_LANGUAGE
#ifdef NFSMW_GAME_LANGUAGE
#define NFSCARBON_GAME_LANGUAGE NFSMW_GAME_LANGUAGE
#else
#define NFSCARBON_GAME_LANGUAGE "1"
#endif
#endif

#ifndef NFSMW_GAME_LANGUAGE
#define NFSMW_GAME_LANGUAGE NFSCARBON_GAME_LANGUAGE
#endif

#ifndef NFSCARBON_GAME_COUNTRY
#ifdef NFSMW_GAME_COUNTRY
#define NFSCARBON_GAME_COUNTRY NFSMW_GAME_COUNTRY
#else
#define NFSCARBON_GAME_COUNTRY "103"
#endif
#endif

#ifndef NFSMW_GAME_COUNTRY
#define NFSMW_GAME_COUNTRY NFSCARBON_GAME_COUNTRY
#endif

#if NFSMW_WITH_GAME
#include <map>
#include <memory>

#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_sdl.h>
#endif

namespace fs = std::filesystem;

// The SDK's log level (the launcher's Logging switch sets "off").
REXCVAR_DECLARE(std::string, log_level);

// MoltenVK's queue submissions, synchronous (its default) or not. Measured on
// an iPad Pro M4 in a race: every UI paint spent 12-17 ms inside vkQueueSubmit
// for a tiny command buffer (and ~0.05 ms in the menus), while holding the
// SDK's queue lock, so the game's command processor waited 7-8 ms per frame
// for it. MoltenVK only takes the CAMetalLayer drawable (nextDrawable, paced by
// the display) when it encodes the work that draws to it, and by default it
// encodes inside vkQueueSubmit. Asynchronous, vkQueueSubmit queues the work and
// returns; MoltenVK encodes it, in order, on its own dispatch queue.
REXCVAR_DEFINE_BOOL(ios_async_submits, true, "GPU",
                    "iOS: MoltenVK encodes queue submissions on its own thread "
                    "(MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=0) instead of inside vkQueueSubmit");

// MoltenVK's own profiling: how long its calls, encoding and shader work take,
// summarised in the log every 300 presented frames (as "mvk-info" Vulkan
// messages). It showed the GPU limits races (~20 ms of GPU time per command
// buffer on an iPad M4, the command processor waiting ~15 ms of each frame).
// Off by default: --ios_mvk_performance=true to measure again.
REXCVAR_DEFINE_BOOL(ios_mvk_performance, false, "GPU",
                    "iOS: MoltenVK performance tracking, summarised in the log every 300 frames");

#include <csignal>
#include <spdlog/spdlog.h>

#ifndef NFSC_GAME_LANGUAGE
#define NFSC_GAME_LANGUAGE "en"
#endif
#ifndef NFSC_GAME_COUNTRY
#define NFSC_GAME_COUNTRY "US"
#endif

// A CAPTURE button in the game that saves a Metal GPU frame capture to
// Documents/captures/ (gpu_capture.mm). Needs Metal capture enabled before the
// device exists, so it is decided here, at startup.
REXCVAR_DEFINE_BOOL(ios_gpu_capture, false, "GPU",
                    "iOS: a CAPTURE button that saves a Metal GPU frame capture to "
                    "Documents/captures (open it in Xcode)");

static void CrashSignalHandler(int sig) {
  const char* sig_name = "UNKNOWN";
  switch (sig) {
    case SIGSEGV: sig_name = "SIGSEGV"; break;
    case SIGBUS: sig_name = "SIGBUS"; break;
    case SIGABRT: sig_name = "SIGABRT"; break;
    case SIGILL: sig_name = "SIGILL"; break;
    case SIGFPE: sig_name = "SIGFPE"; break;
  }
  std::fprintf(stderr, "\n[FATAL CRASH] Caught signal %d (%s)!\n", sig, sig_name);
  REXLOG_ERROR("[FATAL CRASH] Caught signal {} ({})", sig, sig_name);
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
  std::fflush(nullptr);
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}

namespace {

fs::path DocumentsFolder() {
  NSArray<NSString*>* paths =
      NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
  return fs::path(paths.firstObject.UTF8String);
}

std::vector<std::string> GameArguments(const fs::path& docs, const fs::path& iso) {
  return {
      "--gpu_plugin=xenos",
      "--gpu_backend=vulkan",
      "--render_target_path_vulkan=fbo",
      // On iOS the window is the whole screen (19.5:9 on an iPhone): the
      // presenter adds the 16:9 bars.
      "--present_letterbox=true",
      "--present_allow_overscan_cutoff=false",
      "--vulkan_sparse_shared_memory=false",
      "--fullscreen=true",
      "--resolution_scale=1",
      "--readback_resolve=fast",
      "--gpu_sin_msaa=true",
      "--carbon_single_pass=true",
      "--audio_maxqframes=64",
      // The Xbox 360 language and country of the game's edition (USA).
      std::string("--user_language=") + NFSCARBON_GAME_LANGUAGE,
      std::string("--user_country=") + NFSCARBON_GAME_COUNTRY,
      "--user_data_root=" + (docs / "data").string(),
      "--log_file=" + (docs / "logs" / "nfscarbon.log").string(),
      "--log_level=info",
      "--log_flush_interval=1",
      "--game_data_root=" + iso.string(),
  };
}


// "--name=value" or "--name" -> "--name".
std::string OptionName(const std::string& arg) {
  return arg.substr(0, arg.find('='));
}

// Puts `arg` in `args`, replacing any argument with the same option name
// (args[0] is the program name).
void SetArgument(std::vector<std::string>& args, const std::string& arg) {
  const std::string name = OptionName(arg);
  args.erase(std::remove_if(args.begin() + 1, args.end(),
                            [&](const std::string& x) { return OptionName(x) == name; }),
             args.end());
  args.push_back(arg);
}

// Utility QoS for the game threads runs them on the efficiency cores:
int utility_qos_efficiency_cores = -1;
const char* utility_qos_source = "default";

void ApplyUtilityQosDefault(std::vector<std::string>& args) {
  int cores = 0;
  size_t size = sizeof(cores);
  if (sysctlbyname("hw.perflevel1.physicalcpu", &cores, &size, nullptr, 0) != 0) cores = 0;
  utility_qos_efficiency_cores = cores;
  const bool on = cores >= 6;
  int cap = 60;
  for (const auto& arg : args) {
    if (OptionName(arg) == "--ios_fps_cap" && arg.find('=') != std::string::npos) cap = std::atoi(arg.c_str() + arg.find('=') + 1);
  }
  const bool high_cap = cap > 60;
  const std::pair<const char*, bool> defaults[] = {{"--ios_utility_qos", on && !high_cap},
                                                   {"--ios_utility_qos_guest", true},
                                                   {"--ios_adaptive_qos", !on || high_cap}};
  for (const auto& [flag, value] : defaults) {
    const bool given = std::any_of(args.begin() + 1, args.end(),
                                   [&](const std::string& a) { return OptionName(a) == flag; });
    if (given) {
      utility_qos_source = "Extra Args";
    } else {
      SetArgument(args, std::string(flag) + (value ? "=true" : "=false"));
    }
  }
}

// Documents/extra_args.txt: one argument per line; blank lines and lines
// starting with '#' are ignored. Each replaces any argument with the same
// option name already in `args`. UTF-8, or UTF-16 with its byte-order mark.
void ApplyExtraArguments(const fs::path& file, std::vector<std::string>& args) {
  NSData* data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:file.c_str()]];
  if (!data) {
    return;
  }
  NSString* text = nil;
  const auto* bytes = static_cast<const uint8_t*>(data.bytes);
  if (data.length >= 2 && ((bytes[0] == 0xFF && bytes[1] == 0xFE) || (bytes[0] == 0xFE && bytes[1] == 0xFF))) {
    text = [[NSString alloc] initWithData:data encoding:NSUTF16StringEncoding];
    nfsmw::boot::Trace("extra_args.txt: UTF-16, converted");
  }
  if (!text) {
    text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
  }
  if (!text) {
    nfsmw::boot::Trace("extra_args.txt: not UTF-8 or UTF-16; ignored");
    return;
  }
  if ([text hasPrefix:@"\uFEFF"]) {
    text = [text substringFromIndex:1];
  }
  std::istringstream in(text.UTF8String);
  std::string line;
  while (std::getline(in, line)) {
    const auto a = line.find_first_not_of(" \t");
    const auto b = line.find_last_not_of(" \t\r\n");
    if (a == std::string::npos || line[a] == '#') {
      continue;
    }
    line = line.substr(a, b - a + 1);
    for (const char* dash : {"\u2014", "\u2013"}) {
      const std::string d(dash);
      if (line.compare(0, d.size(), d) == 0) {
        line = "--" + line.substr(d.size());
        break;
      }
    }
    for (const char* quote : {"\u201C", "\u201D"}) {
      for (size_t at; (at = line.find(quote)) != std::string::npos;) {
        line.replace(at, std::string(quote).size(), "\"");
      }
    }
    if (line.compare(0, 2, "--") == 0) {
      const size_t name_end = std::min(line.find('='), line.size());
      std::replace(line.begin() + 2, line.begin() + name_end, '-', '_');
    }
    SetArgument(args, line);
    nfsmw::boot::Trace("extra argument: " + line);
  }
}

#if NFSMW_WITH_GAME
int RunGame(int argc, char* argv[], const fs::path& docs) {
  namespace boot = nfsmw::boot;
  rex::audio::ios::PrepareAudioSession();
  boot::Trace("game: parsing the arguments");
  std::vector<std::string> experiment_args(argv, argv + argc);
  ApplyUtilityQosDefault(experiment_args);
  std::vector<char*> experiment_argv;
  for (auto& arg : experiment_args) experiment_argv.push_back(arg.data());
  experiment_argv.push_back(nullptr);
  auto leftovers = rex::cvar::Init(int(experiment_args.size()), experiment_argv.data());
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();

  setenv("MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS", REXCVAR_GET(ios_async_submits) ? "0" : "1", 1);
  setenv("MTL_CAPTURE_ENABLED", REXCVAR_GET(ios_gpu_capture) ? "1" : "0", 1);
  if (REXCVAR_GET(ios_mvk_performance)) {
    setenv("MVK_CONFIG_PERFORMANCE_TRACKING", "1", 1);
    setenv("MVK_CONFIG_PERFORMANCE_LOGGING_FRAME_COUNT", "300", 1);
  }

  rex::InitLogging((docs / "logs" / "nfscarbon.log").c_str(),
                   REXCVAR_GET(log_level) == "off" ? spdlog::level::off : spdlog::level::info);
  if (auto logger = spdlog::default_logger()) {
    logger->flush_on(spdlog::level::info);
  }
  spdlog::flush_every(std::chrono::seconds(1));

  REXLOG_INFO("[ios] game: starting, build " __DATE__ " " __TIME__
              " (MoltenVK submissions {}, MoltenVK profiling {})",
              REXCVAR_GET(ios_async_submits) ? "asynchronous" : "synchronous",
              REXCVAR_GET(ios_mvk_performance) ? "on" : "off");
  nfsmw::memory::Report("log open");
  REXLOG_INFO("[utility-qos] {} efficiency cores; main/GPU threads {}, other guest threads {}, adaptive {} ({})",
              utility_qos_efficiency_cores, rex::cvar::Query<bool>("ios_utility_qos") ? "on" : "off",
              rex::cvar::Query<bool>("ios_utility_qos_guest") ? "on" : "off",
              rex::cvar::Query<bool>("ios_adaptive_qos") ? "on" : "off", utility_qos_source);

  boot::Trace("game: initializing the SDL context");

  rex::ui::SDLWindowedAppContext context;
  if (!context.Initialize()) {
    boot::Trace("game: the SDL context did NOT initialize");
    REXLOG_ERROR("Could not initialize the SDL context");
    return EXIT_FAILURE;
  }
  boot::Trace("game: creating the app");
  std::unique_ptr<rex::ui::WindowedApp> app = rex::ui::GetWindowedAppCreator()(context);

  const auto& names = app->GetPositionalOptions();
  std::map<std::string, std::string> options;
  const size_t n = std::min(leftovers.size(), names.size());
  for (size_t i = 0; i < n; ++i) {
    options[names[i]] = leftovers[i];
  }
  app->SetParsedArguments(std::move(options));

  nfsmw::memory::Report("SDL ready");
  boot::Trace("game: OnInitialize (paths, log, GPU plugin, runtime, XEX)");
  rex::FlushLogging();
  const bool initialized = app->OnInitialize();
  nfsmw::memory::Report("runtime, GPU and XEX loaded");
  nfsmw::memory::StartPeriodicReports(10);
  nfsmw::battery::Start();
  boot::Trace(initialized ? "game: OnInitialize done, entering the main loop"
                          : "game: OnInitialize FAILED (reason in nfscarbon.log)");
  REXLOG_INFO("[ios] entering main message loop...");
  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }

  const int result = initialized ? context.RunMainMessageLoop() : EXIT_FAILURE;

  REXLOG_INFO("[ios] main message loop returned with exit code {}", result);

  rex::FlushLogging();
  if (auto logger = spdlog::default_logger()) {
    logger->flush();
  }
  std::fflush(nullptr);

  boot::Trace("game: main loop ended (exit code " + std::to_string(result) + ")");
  app->InvokeOnDestroy();
  return result;
}
#endif

int RunProbe(int argc, char* argv[], const fs::path& docs) {
  rex::cvar::Init(argc, argv);
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();
  rex::InitLogging((docs / "logs" / "nfscarbon.log").c_str());
  if (auto logger = spdlog::default_logger()) {
    logger->flush_on(spdlog::level::info);
  }
  return NfsmwVulkanProbe((docs / "vulkan_probe.txt").string());
}

}  // namespace

int main(int argc, char* argv[]) {
  std::signal(SIGSEGV, CrashSignalHandler);
  std::signal(SIGBUS, CrashSignalHandler);
  std::signal(SIGABRT, CrashSignalHandler);
  std::signal(SIGILL, CrashSignalHandler);
  std::signal(SIGFPE, CrashSignalHandler);

  setenv("SDL_IOS_ORIENTATIONS", "LandscapeLeft LandscapeRight", 1);
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  const fs::path docs = DocumentsFolder();
  std::error_code ec;
  fs::create_directories(docs / "logs", ec);
  fs::create_directories(docs / "data", ec);

  namespace boot = nfsmw::boot;
  boot::Open((docs / "logs" / "boot.txt").string());
  boot::Trace("---- start, build " __DATE__ " " __TIME__);
  boot::Trace("Documents: " + docs.string());

#if NFSMW_WITH_GAME
  boot::Trace("launcher: on screen");
  const fs::path iso = nfsmw::launcher::Run(docs);
  boot::Trace("launcher: Play");
#else
  const auto isos = nfsmw::launcher::FindIsos(docs);
  const fs::path iso = isos.empty() ? fs::path() : isos.front();
#endif
  const bool play = (NFSCARBON_WITH_GAME || NFSMW_WITH_GAME) && !iso.empty();
  if (iso.empty()) {
    boot::Trace("ISO: none in Documents/");
  } else {
    struct stat st{};
    const bool ok = stat(iso.c_str(), &st) == 0;
    boot::Trace("ISO: " + iso.filename().string() + ", " +
                (ok ? std::to_string(st.st_size) + " bytes" : "stat FAILED"));
  }
  boot::Trace(std::string("mode: ") + (play ? "game" : "probe") +
              ((NFSCARBON_WITH_GAME || NFSMW_WITH_GAME) ? "" : " (this build has no game code)"));

  std::vector<std::string> texts{argc > 0 ? argv[0] : "nfscarbon"};
  if (play) {
    const auto game = GameArguments(docs, iso);
    texts.insert(texts.end(), game.begin(), game.end());
    for (const std::string& a : nfsmw::launcher::SettingsArguments(docs)) {
      SetArgument(texts, a);
      boot::Trace("setting: " + a);
    }
    ApplyExtraArguments(docs / "extra_args.txt", texts);
    if (std::find(texts.begin(), texts.end(), std::string("--log_level=off")) != texts.end()) {
      boot::Trace("logging off");
      boot::SetEnabled(false);
    }
  } else {
    texts.push_back("--log_file=" + (docs / "logs" / "nfscarbon.log").string());
    texts.push_back("--log_level=info");
  }
  std::vector<char*> args;
  for (auto& t : texts) {
    args.push_back(t.data());
  }
  args.push_back(nullptr);
  const int n = static_cast<int>(args.size()) - 1;

#if NFSMW_WITH_GAME
  if (play) {
    const int result = RunGame(n, args.data(), docs);
    if (result != EXIT_SUCCESS) {
      boot::Trace("game: failed to start");
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NFS Carbon",
                               "The game failed to start.\n\nThe reason is in the app's "
                               "Documents/logs folder (boot.txt and nfscarbon.log).",
                               nullptr);
    }
    return result;
  }
#endif
  return RunProbe(n, args.data(), docs);
}

