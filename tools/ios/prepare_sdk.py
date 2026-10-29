#!/usr/bin/env python3
"""
Prepares the ReXGlue SDK tree for building the Need for Speed: Carbon iOS port.

    python3 tools/ios/prepare_sdk.py
    python3 tools/ios/prepare_sdk.py --sdk /path/to/rexglue-sdk-ios
    python3 tools/ios/prepare_sdk.py --status

Leaves an SDK v0.10.0 in ../rexglue-sdk-ios, SEPARATE from the Windows and
Android ones. Safe to run as often as needed: every step checks whether it is
already done before touching anything.
"""

import argparse
import importlib.util
import os
import pathlib
import subprocess
import sys
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _android():
    """tools/preparar_sdk.py as a module, untouched."""
    path = ROOT / "tools" / "preparar_sdk.py"
    if not path.exists():
        sys.exit(f"[ERROR] Cannot find {path}")
    spec = importlib.util.spec_from_file_location("preparar_sdk_android", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ANDROID = _android()

MOLTENVK_VERSION = "v1.4.1"  # = commit db445ff of the thirdparty/moltenvk submodule
MOLTENVK_URL = (
    f"https://github.com/KhronosGroup/MoltenVK/releases/download/"
    f"{MOLTENVK_VERSION}/MoltenVK-ios.tar"
)

# Order matters: each one anchors on what the earlier ones left.
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
    "patch_ios_fault_log",
    "patch_ios_save_log",
    "patch_ios_touch_pad",
    "patch_ios_dump_image",
    "patch_ios_upload_batch",
    "patch_ios_vblank_grid",
    "patch_ios_shader_contraction",
    "patch_ios_occlusion_async",
    "patch_ios_fps_cap",
    "patch_ios_present_pacing",
    "patch_ios_vblank_display",
    "patch_ios_ui_tick_cap",
    "patch_ios_register_check",
    "patch_ios_texture_descriptors",
    "patch_ios_readback_limit",
    "patch_ios_suboptimal",
    "patch_ios_surface_reattach",
    "patch_ios_paint_suspend",
    "patch_ios_texture_stats",
    "patch_ios_narrow_invalidation",
    "patch_ios_occlusion_batch",
    "patch_ios_dispatch_stats",
    "patch_ios_shared_stats",
    "patch_ios_hot_pages",
    "patch_ios_async_copies",
    "patch_ios_type0_ranges",
    "patch_ios_resolve_stats",
    "patch_ios_iso_stream",
    "patch_ios_native_only",
    "patch_ios_semaphore_trace",
    "patch_ios_xma_canary",
    "patch_ios_mnk_modifiers",
    "patch_ios_native_present_skip",
    "patch_ios_suboptimal_limit",
    "patch_ios_native_occlusion",
    "patch_ios_xma_starve_switch",
    "patch_ios_invalid_call_log",
    "patch_ios_read_timing",
    "patch_ios_xma_reset_stats",
    "patch_ios_xma_voice_log",
    "patch_ios_xma_direct_output",
    "patch_ios_shader_disasm_log",
    "patch_ios_small_shared_memory",
    "patch_ios_native_backend",
    "patch_ios_timer_queue_block",
    "patch_ios_cheap_sleep",
    "patch_ios_sdl_display_link",
    "patch_ios_stack_top",
    "patch_ios_save_hook",
    "patch_ios_sdl_scene",
    "patch_ios_retire_qos",
    "patch_ios_battery_group",
    "patch_ios_utility_qos",
    "patch_ios_packet_census",
    "patch_ios_cp_fast_runs",
    "patch_ios_cp_idle_wait",
    "patch_ios_vblank_realtime",
    "patch_ios_cap_30_at_120",
]


def step(n, text):
    print(f"\n== {n}. {text}")


def ensure_git_tags(sdk):
    """Ensure v* git tag is reachable so rex_compute_version resolves cleanly."""
    chk = subprocess.run(
        ["git", "describe", "--tags", "--match", "v*"],
        cwd=sdk,
        capture_output=True,
        text=True,
    )
    if chk.returncode != 0:
        subprocess.run(["git", "fetch", "--tags", "--depth=1"], cwd=sdk, capture_output=True)
        chk2 = subprocess.run(
            ["git", "describe", "--tags", "--match", "v*"],
            cwd=sdk,
            capture_output=True,
            text=True,
        )
        if chk2.returncode != 0:
            subprocess.run(["git", "tag", "-f", "v0.10.0", "HEAD"], cwd=sdk, capture_output=True)
            print("  [ok] Tagged v0.10.0 on HEAD for rex_compute_version")


def safeguard_vulkan_loader(sdk):
    """Extra safeguard: quote ${SYSTEM_PROCESSOR} and ${CMAKE_SYSTEM_PROCESSOR} in vulkan-loader."""
    loader_cmake = sdk / "thirdparty" / "vulkan-loader" / "loader" / "CMakeLists.txt"
    if loader_cmake.exists():
        txt = loader_cmake.read_text(encoding="utf-8")
        orig = txt
        for var in ["${SYSTEM_PROCESSOR}", "${CMAKE_SYSTEM_PROCESSOR}"]:
            if var in txt:
                tmp = f"___TMP_QUOTED_{var[2:-1]}___"
                txt = txt.replace(f'"{var}"', tmp)
                txt = txt.replace(var, f'"{var}"')
                txt = txt.replace(tmp, f'"{var}"')
        if txt != orig:
            loader_cmake.write_text(txt, encoding="utf-8")
            print("  [ok] Safeguarded vulkan-loader/loader/CMakeLists.txt (quoted processor variables)")


def safeguard_rex_version(sdk):
    """Ensure rex_version.cmake cleanly resolves 0.10.0 without warnings on iOS."""
    ver_cmake = sdk / "cmake" / "rex_version.cmake"
    if ver_cmake.exists():
        txt = ver_cmake.read_text(encoding="utf-8")
        old_warn = 'message(WARNING\n            "rex_compute_version: no v* tag reachable from HEAD.'
        if old_warn in txt:
            new_warn = 'set(${out_var} "0.10.0" PARENT_SCOPE)\n        return()\n        message(DEBUG\n            "rex_compute_version: no v* tag reachable from HEAD.'
            txt = txt.replace(old_warn, new_warn)
            ver_cmake.write_text(txt, encoding="utf-8")
            print("  [ok] Safeguarded rex_version.cmake (clean 0.10.0 fallback)")


def moltenvk(sdk):
    dest = sdk / ".nfsc-ios" / "MoltenVK"
    marker = dest / (MOLTENVK_VERSION + ".ready")
    if marker.exists():
        print(f"[ok] MoltenVK {MOLTENVK_VERSION} already there")
        return
    dest.mkdir(parents=True, exist_ok=True)
    tar = dest.parent / "MoltenVK-ios.tar"
    if not tar.exists():
        print(f"[..] Downloading MoltenVK {MOLTENVK_VERSION} for iOS")
        with urllib.request.urlopen(MOLTENVK_URL, timeout=300) as resp:
            tar.write_bytes(resp.read())
    with tarfile.open(tar) as t:
        t.extractall(dest)
    xcf = list(dest.rglob("dynamic/MoltenVK.xcframework"))
    if not xcf:
        sys.exit(f"[ERROR] The MoltenVK tar has no dynamic/MoltenVK.xcframework ({dest})")
    marker.write_text(str(xcf[0].relative_to(dest)) + "\n", encoding="utf-8")
    # Also write marker to .nfsmw-ios for compatibility
    alt_marker = sdk / ".nfsmw-ios" / "MoltenVK"
    alt_marker.mkdir(parents=True, exist_ok=True)
    (alt_marker / (MOLTENVK_VERSION + ".ready")).write_text(
        str(xcf[0].relative_to(dest)) + "\n", encoding="utf-8"
    )
    tar.unlink(missing_ok=True)
    print(f"[ok] MoltenVK in {xcf[0]}")


def project_patches(sdk):
    """The project patches still missing, each one only once."""
    marker = sdk / ".nfsc-ios" / "project-patches.applied"
    if not marker.exists():
        marker = sdk / ".nfsmw-ios" / "project-patches.applied"
    done = marker.read_text(encoding="utf-8").split() if marker.exists() else []
    full = ANDROID.PARCHES_PROYECTO
    pending = [p for p in full if p not in done]
    if not pending:
        print(f"[ok] All {len(full)} project patches already applied")
        return
    last = max((full.index(d) for d in done if d in full), default=-1)
    for p in pending:
        if full.index(p) < last:
            print(f"[!!] {p} sits in the middle of list and is applied after the "
                  f"ones that follow it: if it fails, prepare the SDK from scratch")

    marker.parent.mkdir(parents=True, exist_ok=True)
    original = ANDROID.PARCHES_PROYECTO
    try:
        for p in pending:
            ANDROID.PARCHES_PROYECTO = [p]
            ANDROID.parches_proyecto(sdk)
            done.append(p)
            marker.write_text("\n".join(done) + "\n", encoding="utf-8")
    finally:
        ANDROID.PARCHES_PROYECTO = original


def ios_patches(sdk, status=False):
    env = dict(
        os.environ,
        NFSCARBON_SDK=str(sdk),
        NFSC_SDK=str(sdk),
        NFSMW_SDK=str(sdk),
    )
    for p in IOS_PATCHES:
        cmd = [sys.executable, str(ROOT / "tools" / "ios" / f"{p}.py")]
        if status:
            cmd.append("--status")
        r = subprocess.run(cmd, env=env, text=True, capture_output=True)
        out = (r.stdout + r.stderr).strip()
        if r.returncode != 0:
            print(out)
            sys.exit(f"[ERROR] {p} failed. The earlier ones are already applied; "
                     f"re-running this script is safe.")
        if status:
            print(out)
        else:
            first = next((l for l in out.splitlines() if l.startswith("[")), "")
            print(f"  {p:30s} {first}")


def main():
    ap = argparse.ArgumentParser(description="Prepares the ReXGlue SDK for iOS.")
    ap.add_argument("--sdk", default=str(ROOT.parent / "rexglue-sdk-ios"),
                    help="iOS SDK folder (default ../rexglue-sdk-ios)")
    ap.add_argument("--status", action="store_true", help="only report which patches are in")
    args = ap.parse_args()
    sdk = pathlib.Path(args.sdk).resolve()

    if args.status:
        if not (sdk / "CMakeLists.txt").exists():
            sys.exit(f"[ERROR] No SDK in {sdk}")
        marker = sdk / ".nfsc-ios" / "project-patches.applied"
        if not marker.exists():
            marker = sdk / ".nfsmw-ios" / "project-patches.applied"
        done = marker.read_text(encoding="utf-8").split() if marker.exists() else []
        full = ANDROID.PARCHES_PROYECTO
        print(f"  project patches: {sum(p in done for p in full)} of {len(full)} applied (recorded)")
        for p in full:
            print(f"      {'yes' if p in done else 'NO':>3}  {p}")
        ios_patches(sdk, status=True)
        return 0

    step(1, f"SDK {ANDROID.SDK_TAG}")
    ANDROID.clonar(sdk)
    ensure_git_tags(sdk)
    ANDROID.reparar_symlinks(sdk)

    step(2, "hells-gate's Android SDK patch: ARM64 barriers")
    ANDROID.aplicar_parches_ref(sdk)

    step(3, "Project patches (Android base, once each)")
    project_patches(sdk)

    step(4, f"MoltenVK {MOLTENVK_VERSION} for iOS")
    moltenvk(sdk)

    step(5, "iOS platform patches")
    ios_patches(sdk)

    step(6, "Extra safeguards")
    safeguard_vulkan_loader(sdk)
    safeguard_rex_version(sdk)
    ensure_git_tags(sdk)

    print(f"\n[ok] SDK ready in {sdk}")
    print("     Next: build from ios/ (cmake --preset ios).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
