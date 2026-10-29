# Need for Speed: Carbon (Xbox 360) Recompiled for iOS

[![Build iOS IPA](https://github.com/dev3lop3r2025-beep/NFSC_IOS/actions/workflows/build-ipa.yml/badge.svg)](https://github.com/dev3lop3r2025-beep/NFSC_IOS/actions/workflows/build-ipa.yml)
[![Platform](https://img.shields.io/badge/Platform-iOS%2016.0%2B%20%7C%20iPadOS%2016.0%2B-blue)](https://apple.com)
[![Architecture](https://img.shields.io/badge/Architecture-Apple%20Silicon%20ARM64-orange)](https://apple.com)
[![Graphics](https://img.shields.io/badge/Renderer-Native%20Metal%20%2B%20MetalFX-red)](https://developer.apple.com/metal/)

**Need for Speed: Carbon (Xbox 360 release, Title ID: `454107EC`), statically recompiled to run natively on iPhone and iPad with Apple Metal, MetalFX upscaling, up to 120 FPS ProMotion, touch controls, and MFi/PlayStation/Xbox controller support.**

This is **not an emulator**. The game's PowerPC Xenon machine code is translated ahead-of-time (AOT) into native C++ using the **ReXGlue SDK** and compiled directly for Apple Silicon (`arm64`). The Xbox 360's ATI Xenos GPU commands are translated into native Apple Metal with MoltenVK fallback.

---

## Features

- **Native Apple Silicon ARM64 Execution**: Ahead-of-time translated C++20 running natively on Apple Silicon SoCs (A14/M1 or newer recommended).
- **Native Metal Renderer & MetalFX**:
  - Full Apple Metal graphics pipeline with MoltenVK fallback.
  - **MetalFX Upscaling**: Spatial and Temporal upscaling modes for crystal-clear visuals at low energy draw.
  - **Single-Pass Scene Rendering**: Carbon retail "Mode 2" single-pass scene rendering hook, eliminating redundant tiling passes and saving CPU/GPU draw call overhead.
- **Display & ProMotion**:
  - 30, 40, 60, and 120 FPS frame rate caps with ProMotion display synchronization.
  - Aspect ratio modes: Auto, 4:3, 16:9, and 19.5:9 iPhone Ultrawide HUD clamping.
- **Controls & Input**:
  - **Carbon Touch Driving Layout**: Steering wheel/slider, Gas, Brake, Handbrake, Nitrous, Speedbreaker, Gear Up/Down, Camera, Look Back, and Carbon's signature **Crew Command / Wingman Activate button (Y)**!
  - **Virtual Pad Layout**: Classic gamepad layout for menus, garage, and tuning.
  - **Auto-Switching & Auto-Hide**: On-screen touch controls automatically fade out when an MFi, DualSense, Xbox controller, or hardware keyboard is connected.
- **Audio & Media**:
  - High-performance, low-latency CoreAudio RemoteIO stereo output clocked at 48 kHz.
  - Multi-threaded XMA audio decoding and VP6 movie playback with background/foreground pause and resume handling.
- **Storage & Cloud Sync**:
  - Carbon save containers in `Documents/data/` with automatic local backup history (newest 10 retained).
  - Optional **iCloud Drive** sync via security-scoped bookmark selection in the launcher.

---

## Requirements

### To Play:
- iPhone or iPad running **iOS 16.0 or newer** (A14 Bionic / M1 or newer recommended).
- Sideloading utility: **[LiveContainer](https://github.com/LiveContainer/LiveContainer)**, **[SideStore](https://sidestore.io)**, **[AltStore](https://altstore.io)**, or **[TrollStore](https://github.com/opa334/TrollStore)**.
- A legally obtained copy of **Need for Speed: Carbon (Xbox 360 edition, Title ID: `454107EC`)**.

### To Build:
- macOS 14+ or 15+ with **Xcode 15+** and Apple Command Line Tools.
- Python 3.9+, CMake 3.25+, and Ninja (`brew install cmake ninja`).
- Alternatively, build via **GitHub Actions** without requiring a local Mac.

---

## Quick Start: Building the IPA

### Option A: Automated Build with GitHub Actions (No Mac Required)

1. Fork or push this repository to your GitHub account (a private repository is recommended).
2. (Optional) Set the repository secret **`CARBON_XEX_SECRET`** to:
   - The base64-encoded string of your `default.xex`: `base64 -i default.xex | pbcopy`
   - Or a private raw download URL to your `default.xex`.
3. Go to **Actions** $\to$ **Build iOS IPA** $\to$ **Run workflow**.
4. Once completed, download `NFS-Carbon-iOS-unsigned.ipa` from the workflow summary artifacts (or GitHub Releases on tags `v*`).

### Option B: Fast Cloud Build with Codemagic (M2 Apple Silicon, Instant Builds)

1. Sign up / Log in to [Codemagic.io](https://codemagic.io) using your GitHub account (`dev3lop3r2025-beep`).
2. Click **Add application**, select **`NFSC_IOS`**, and select **codemagic.yaml**.
3. Click **Start new build** with the `ios-release` workflow.
4. Codemagic spins up a dedicated Apple Silicon **`mac_mini_m2`** instance, compiles `NFSCarbon.app`, and packages `NFS-Carbon-iOS-unsigned.ipa` ready to download from the build artifacts (and sent directly to your email).

---

### Option C: Building Locally on macOS

```bash
# 1. Clone this repository
git clone https://github.com/dev3lop3r2025-beep/NFSC_IOS.git
cd NFSC_IOS

# 2. Prepare the ReXGlue iOS SDK & MoltenVK for iOS
python3 tools/prepare_sdk.py

# 3. Supply your retail Carbon Xbox 360 default.xex:
# Either extract it from your retail ISO:
python3 tools/extract_iso.py "/path/to/NFS_Carbon_Xbox360.iso" --solo-xex -o game
# Or copy your default.xex into game/default.xex

# 4. Generate the translated native C++ code
python3 tools/generate_code.py --xex game/default.xex

# 5. Build and package the unsigned .ipa
./build_ipa.sh
```

The resulting package will be generated at:
`build/NFS-Carbon-iOS-unsigned.ipa`

---

## Installing and Playing on Device

1. Sideload `NFS-Carbon-iOS-unsigned.ipa` using **LiveContainer**, **SideStore**, **TrollStore**, or **AltStore**.
2. Launch the app once to initialize the directory layout (`Documents/game/`, `Documents/logs/`, `Documents/data/`).
3. **Supplying Game Assets (Two Options):**
   - **Option A (Files.app / LiveContainer):** Using the iOS **Files** app ("On My iPhone" $\to$ "NFS Carbon"), copy your extracted game folder (`default.xex`, `NFS/`, `Movies/`) or retail `.iso` into `Documents/game/`! The in-app scanner automatically finds and loads it on startup.
   - **Option B (In-App File Picker):** In the pre-game launcher, tap the game row to import an ISO or folder from Files or iCloud Drive.
4. Select your preferred resolution scale, FPS cap, and MetalFX mode, and tap **Play**!

---

## Repository Structure

```
├── .github/workflows/
│   └── build-ipa.yml          # Production macOS 15 GitHub Actions CI/CD workflow
├── build_ipa.sh               # Local macOS build & packaging script
├── CMakeLists.txt             # Root CMake configuration
├── nfscarbon_manifest.toml    # ReXGlue Carbon manifest (Title ID: 454107EC)
├── carbon_analysis.toml       # Direct branch destination overrides
├── overrides.toml             # Codegen register & function boundary overrides
├── ios/
│   ├── CMakeLists.txt         # iOS app build & IPA packaging target
│   ├── CMakePresets.json      # iOS arm64 build presets (iOS 16.0+)
│   ├── Info.plist.in          # App bundle metadata & controller config
│   ├── nfscarbon.entitlements # Mach memory & execution entitlements
│   ├── resources/
│   │   └── Assets.xcassets/   # Compiled asset catalog with generated AppIcon
│   └── src/                   # iOS launcher, UIKit lifecycle, touch controls, audio
├── src/                       # Carbon-specific engine & render mode hooks
│   ├── carbon_missing_entries.cpp / .h  # PPC entry point translations
│   ├── carbon_nativo_ganchos.cpp / .h   # D3D draw & shader hooks
│   ├── carbon_nativo_enlaces.cpp        # Native renderer compatibility stubs
│   ├── render_mode_hook.cpp             # Retail Mode 2 single-pass scene hook
│   ├── ring_wait_hook.cpp               # Apple Darwin futex / ring wait hook
│   ├── dispatch_tolerance.cpp           # Unregistered indirect call tolerance
│   ├── nfscarbon_app.h                  # NfsCarbonApp definition
│   └── codegen/
│       └── phase_gapfill.cpp            # Carbon-specific terminator splitting
└── tools/
    ├── extract_iso.py         # Pure-Python Xbox 360 XDVDFS ISO extractor
    ├── generate_appicon.py    # Deterministic AppIcon generator & catalog updater
    ├── generate_code.py       # Ahead-of-time PowerPC-to-C++ codegen runner
    └── ios/
        └── prepare_sdk.py     # ReXGlue iOS SDK setup & MoltenVK downloader
```

---

## Credits & Acknowledgments

- **[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)**: Static recompilation SDK for Xbox 360 binaries.
- **[bep1s69/nfsmw-ios](https://github.com/bep1s69/nfsmw-ios)**: The pioneering Xbox 360 iOS platform harness, native Metal renderer, and Mach memory sandbox.
- **[Sampreeth004/nfs-carbon-recomp](https://github.com/Sampreeth004/nfs-carbon-recomp)** & **[codepdbh/NFS-CARBON-360-ANDROID-RECOMP](https://github.com/codepdbh/NFS-CARBON-360-ANDROID-RECOMP)**: Need for Speed: Carbon function boundaries, manifests, and D3D hooks.
- **[KhronosGroup/MoltenVK](https://github.com/KhronosGroup/MoltenVK)**: Vulkan implementation on Apple Metal.
