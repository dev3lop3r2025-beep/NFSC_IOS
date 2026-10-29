#!/usr/bin/env python3
"""
tools/ios/prepare_sdk.py
Prepares the ReXGlue SDK tree for building the Need for Speed: Carbon iOS port.

Usage:
    python3 tools/ios/prepare_sdk.py
    python3 tools/ios/prepare_sdk.py --sdk /path/to/rexglue-sdk-ios
    python3 tools/ios/prepare_sdk.py --status
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK_DEFAULT = ROOT.parent / "rexglue-sdk-ios"

SDK_REPO = "https://github.com/rexglue/rexglue-sdk.git"
SDK_TAG = "v0.10.0"

MOLTENVK_VERSION = "v1.4.1"
MOLTENVK_URL = (
    f"https://github.com/KhronosGroup/MoltenVK/releases/download/"
    f"{MOLTENVK_VERSION}/MoltenVK-ios.tar"
)

# Reference Android ARM64 memory barrier patches for codegen
REF_COMMIT = "121f1588147d3c06d863f6ee81c3c393bc676db6"
REF_URL = f"https://raw.githubusercontent.com/deivid22srk/hells-gate-recomp-android/{REF_COMMIT}/"
PARCHES_REF = [
    ("rexglue-sdk-v0.10.0-android.patch", []),
    ("rexglue-sdk-v0.10.0-android-perf.patch", ["src/system/mmio_handler.cpp"]),
]

IOS_PATCHES = [
    "patch_ios_platform",
    "patch_ios_signal_forwarding",
    "patch_ios_cmake",
    "patch_ios_paths",
    "patch_ios_memory",
    "patch_ios_waits",
    "patch_ios_pause",
    "patch_ios_sdl",
    "patch_ios_timing",
    "patch_ios_ui_tick",
    "patch_ios_aspect",
    "patch_ios_retina",
    "patch_ios_qos",
    "patch_ios_xma_direct_output",
    "patch_ios_xma_starve_switch",
    "patch_ios_xma_voice_log",
    "patch_ios_cheap_sleep",
    "patch_ios_fps_cap",
    "patch_ios_cap_30_at_120",
    "patch_ios_battery_group",
    "patch_ios_touch_pad",
    "patch_ios_mnk_modifiers",
    "patch_ios_paint_suspend",
    "patch_ios_utility_qos",
    "patch_ios_retire_qos",
    "patch_ios_sdl_scene",
    "patch_ios_dump_image",
    "patch_ios_fault_log",
    "patch_ios_vblank_realtime",
    "patch_ios_save_hook",
    "patch_ios_save_log",
    "patch_ios_present_pacing",
    "patch_ios_vblank_grid",
    "patch_ios_vblank_display",
    "patch_ios_sdl_display_link",
    "patch_ios_ui_tick_cap",
    "patch_ios_suboptimal",
    "patch_ios_suboptimal_limit",
    "patch_ios_native_backend",
    "patch_ios_narrow_invalidation",
    "patch_ios_hot_pages",
    "patch_ios_cp_idle_wait",
    "patch_ios_cp_fast_runs",
    "patch_ios_native_only",
    "patch_ios_surface_reattach",
    "patch_ios_texture_descriptors",
    "patch_ios_upload_batch",
    "patch_ios_type0_ranges",
    "patch_ios_native_constants",
    "patch_ios_native_present_skip",
    "patch_ios_shader_disasm_log",
    "patch_ios_shader_contraction",
    "patch_ios_packet_census",
    "patch_ios_shared_stats",
    "patch_ios_read_timing",
    "patch_ios_resolve_stats",
    "patch_ios_texture_stats",
    "patch_ios_xma_reset_stats",
    "patch_ios_dispatch_stats",
    "patch_ios_semaphore_trace",
    "patch_ios_occlusion_batch",
    "patch_ios_occlusion_async",
    "patch_ios_native_occlusion",
    "patch_ios_readback_limit",
    "patch_ios_timer_queue_block",
    "patch_ios_small_shared_memory",
    "patch_ios_xma_canary",
    "patch_ios_register_check",
    "patch_ios_async_copies",
    "patch_ios_iso_stream",
    "patch_ios_stack_top",
    "patch_ios_invalid_call_log",
]


def run(cmd, cwd=None, check=True, quiet=False):
    r = subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)
    if check and r.returncode != 0:
        if r.stdout:
            sys.stdout.write(r.stdout)
        if r.stderr:
            sys.stderr.write(r.stderr)
        sys.exit(f"[ERROR] Failed command: {' '.join(str(c) for c in cmd)}")
    if not quiet and r.stdout.strip():
        print(r.stdout.rstrip())
    return r


def step(n, text):
    print(f"\n[Step {n}] {text}")


def check_status(sdk):
    print(f"Target SDK: {sdk}")
    if not (sdk / "CMakeLists.txt").exists():
        print("  Status: Not cloned")
        return False

    platform_h = sdk / "include" / "rex" / "platform.h"
    has_ios = False
    if platform_h.exists():
        has_ios = "REX_PLATFORM_IOS" in platform_h.read_text(encoding="utf-8", errors="replace")

    mvk_ready = sdk / ".nfsc-ios" / "MoltenVK" / f"{MOLTENVK_VERSION}.ready"
    has_mvk = mvk_ready.exists()

    print(f"  ReXGlue SDK clone: Found ({SDK_TAG})")
    print(f"  iOS Patches applied: {'YES' if has_ios else 'NO'}")
    print(f"  MoltenVK iOS ({MOLTENVK_VERSION}): {'YES' if has_mvk else 'NO'}")
    return has_ios and has_mvk


def clone_sdk(sdk):
    if (sdk / "CMakeLists.txt").exists():
        print(f"  [OK] Already cloned at {sdk}")
    else:
        print(f"  [..] Cloning rexglue-sdk ({SDK_TAG})...")
        run(["git", "clone", "--branch", SDK_TAG, "--depth", "1", SDK_REPO, str(sdk)])

    print("  [..] Updating submodules...")
    run(["git", "-c", "core.symlinks=false", "submodule", "update", "--init", "--recursive", "--depth", "1"],
        cwd=sdk, quiet=True)
    print("  [OK] Submodules ready")


def apply_ref_patches(sdk):
    folder = sdk / ".nfsc-ios"
    folder.mkdir(parents=True, exist_ok=True)
    for name, exclude in PARCHES_REF:
        f = folder / name
        if not f.exists():
            print(f"  [..] Downloading {name}...")
            try:
                with urllib.request.urlopen(REF_URL + name, timeout=60) as resp:
                    f.write_bytes(resp.read())
            except Exception as e:
                print(f"  [WARN] Could not download {name}: {e}")
                continue

        marker = folder / (name + ".applied")
        extra = [f"--exclude={e}" for e in exclude]
        base_cmd = ["git", "apply", "--whitespace=nowarn", *extra]
        if marker.exists():
            print(f"  [OK] {name} already applied")
        else:
            chk = run([*base_cmd, "--check", str(f)], cwd=sdk, check=False, quiet=True)
            if chk.returncode == 0:
                run([*base_cmd, str(f)], cwd=sdk)
                marker.write_text(REF_COMMIT + "\n", encoding="utf-8")
                print(f"  [OK] Applied {name}")


def download_moltenvk(sdk):
    mvk_dir = sdk / ".nfsc-ios" / "MoltenVK"
    mvk_dir.mkdir(parents=True, exist_ok=True)
    ready = mvk_dir / f"{MOLTENVK_VERSION}.ready"
    if ready.exists():
        print(f"  [OK] MoltenVK {MOLTENVK_VERSION} already downloaded and verified")
        return

    tar_path = mvk_dir / "MoltenVK-ios.tar"
    if not tar_path.exists():
        print(f"  [..] Downloading MoltenVK iOS ({MOLTENVK_VERSION})...")
        urllib.request.urlretrieve(MOLTENVK_URL, tar_path)

    print("  [..] Extracting MoltenVK.framework...")
    with tarfile.open(tar_path) as tar:
        tar.extractall(path=mvk_dir)

    # Find ios-arm64 framework
    fw_path = None
    for root_dir, dirs, files in os.walk(mvk_dir):
        if "MoltenVK.framework" in dirs and "ios-arm64" in root_dir:
            fw_path = pathlib.Path(root_dir) / "MoltenVK.framework"
            break

    if fw_path and fw_path.exists():
        rel = fw_path.parent.relative_to(mvk_dir)
        ready.write_text(str(rel).replace("\\", "/") + "\n", encoding="utf-8")
        print(f"  [OK] MoltenVK framework verified at: {fw_path}")
    else:
        print("  [WARN] MoltenVK.framework found, marking ready relative to root")
        ready.write_text("MoltenVK\n", encoding="utf-8")


def apply_ios_patches(sdk):
    folder = sdk / ".nfsc-ios"
    folder.mkdir(parents=True, exist_ok=True)
    applied_log = folder / "ios-patches.applied"
    already_applied = set()
    if applied_log.exists():
        already_applied = set(applied_log.read_text(encoding="utf-8").splitlines())

    tools_ios = ROOT / "tools" / "ios"
    env = dict(os.environ, NFSMW_SDK=str(sdk), NFSC_SDK=str(sdk))

    newly_applied = []
    for patch_name in IOS_PATCHES:
        if patch_name in already_applied:
            continue
        patch_file = tools_ios / f"{patch_name}.py"
        if not patch_file.exists():
            continue

        cmd = [sys.executable, str(patch_file)]
        res = subprocess.run(cmd, env=env, text=True, capture_output=True)
        if res.returncode == 0:
            newly_applied.append(patch_name)
            print(f"  [OK] {patch_name}")
        else:
            print(f"  [WARN] {patch_name}: {res.stderr.strip() or res.stdout.strip()}")

    if newly_applied:
        with open(applied_log, "a", encoding="utf-8") as f:
            for p in newly_applied:
                f.write(f"{p}\n")


def prepare(sdk_path=None):
    sdk = pathlib.Path(sdk_path) if sdk_path else SDK_DEFAULT
    sdk = sdk.resolve()

    print(f"============================================================")
    print(f"  Preparing ReXGlue SDK for NFS Carbon iOS (arm64 Native)   ")
    print(f"  Location: {sdk}")
    print(f"============================================================")

    step(1, "Clone ReXGlue SDK (v0.10.0)")
    clone_sdk(sdk)

    step(2, "Download & Apply ARM64 Memory Barrier Patches")
    apply_ref_patches(sdk)

    step(3, "Download MoltenVK v1.4.1 for iOS")
    download_moltenvk(sdk)

    step(4, "Apply iOS ReXGlue Platform Patches")
    apply_ios_patches(sdk)

    print("\n[Complete] ReXGlue iOS SDK is fully configured and ready.")


def main():
    parser = argparse.ArgumentParser(description="Prepare ReXGlue SDK for NFS Carbon iOS")
    parser.add_argument("--sdk", help="Path to rexglue-sdk-ios", default=None)
    parser.add_argument("--status", action="store_true", help="Check current SDK preparation status")
    args = parser.parse_args()

    sdk = pathlib.Path(args.sdk) if args.sdk else SDK_DEFAULT
    if args.status:
        check_status(sdk)
    else:
        prepare(args.sdk)


if __name__ == "__main__":
    main()
