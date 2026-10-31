#!/usr/bin/env python3
"""
tools/generate_code.py
Ahead-Of-Time (AOT) PowerPC-to-C++ Code Generator for Need for Speed: Carbon (Xbox 360, Title ID: 454107EC).

USA XEX staging:
  The authoritative source binary is 'xex/usa_default.xex' (NFS Carbon USA).
  This script auto-stages it to 'game/default.xex' before invoking rexglue so that
  the manifest path remains stable. Never use the PAL XEX (CRC 5E74E60D) for codegen
  -- the generated C++ addresses differ from what the USA ISO maps into guest RAM.
"""

import argparse
import os
import pathlib
import platform
import shutil
import subprocess
import sys
import tarfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
USA_XEX = ROOT / "xex" / "usa_default.xex"
DEFAULT_XEX = ROOT / "game" / "default.xex"
GENERATED_DIR = ROOT / "generated" / "default"
MANIFEST_FILE = ROOT / "nfscarbon_manifest.toml"
OVERRIDE_FILE = ROOT / "overrides.toml"

# Expected size of the USA default.xex (bytes). Used to reject obvious wrong-variant files.
_USA_XEX_EXPECTED_BYTES = 4_861_952
_XEX_SIZE_TOLERANCE = 32 * 1024  # +/- 32 KB


def fail(msg):
    sys.exit(f"[ERROR] {msg}")


def build_rexglue_cli(sdk_dir):
    """Compiles the host rexglue CLI tool from the prepared ReXGlue SDK."""
    if not sdk_dir.exists():
        fail(f"SDK directory does not exist: {sdk_dir}")

    is_mac = platform.system() == "Darwin"
    preset = "mac-arm64" if (is_mac and platform.machine() == "arm64") else ("mac-amd64" if is_mac else "linux-x64")
    print(f"[Codegen] Building rexglue host CLI using preset: {preset}...")

    # Configure
    if subprocess.run(["cmake", "--preset", preset], cwd=sdk_dir).returncode != 0:
        fail("Failed to configure ReXGlue SDK host tools via CMake.")

    # Build
    if subprocess.run(["cmake", "--build", f"out/build/{preset}", "--config", "Release", "--target", "rexglue"],
                      cwd=sdk_dir).returncode != 0:
        fail("Failed to build rexglue host CLI binary.")

    # Locate executable
    candidates = list((sdk_dir / "out").rglob("rexglue")) + list((sdk_dir / "out").rglob("rexglue.exe"))
    candidates = [c for c in candidates if c.is_file() and os.access(c, os.X_OK)]
    if not candidates:
        fail(f"Could not locate built rexglue executable under {sdk_dir / 'out'}")
    return candidates[0]


def check_and_extract_cache():
    """Checks for gen.tar.gz in current or parent directory and unpacks if available."""
    cache_candidates = [
        ROOT / "gen.tar.gz",
        ROOT.parent / "gen.tar.gz",
        ROOT.parent.parent / "gen.tar.gz",
    ]
    for c in cache_candidates:
        if c.is_file():
            print(f"[Codegen] Found pre-generated C++ code archive at: {c}")
            print(f"          Unpacking into {GENERATED_DIR}...")
            GENERATED_DIR.mkdir(parents=True, exist_ok=True)
            with tarfile.open(c, "r:gz") as tar:
                if hasattr(tarfile, 'data_filter'):
                    tar.extractall(path=GENERATED_DIR, filter='data')
                else:
                    tar.extractall(path=GENERATED_DIR)
            if (GENERATED_DIR / "sources.cmake").exists():
                print(f"[Codegen] Successfully restored generated sources from {c.name}!")
                return True
    return False


def stage_usa_xex(target_path: pathlib.Path) -> pathlib.Path:
    """Auto-stage the USA XEX from xex/usa_default.xex into game/default.xex.

    Returns the path to use for codegen (may be the same as target_path if no USA XEX found).
    """
    if USA_XEX.is_file():
        size = USA_XEX.stat().st_size
        if abs(size - _USA_XEX_EXPECTED_BYTES) > _XEX_SIZE_TOLERANCE:
            print(
                f"[Codegen] WARNING: {USA_XEX} size={size} bytes deviates from expected "
                f"{_USA_XEX_EXPECTED_BYTES} bytes by more than {_XEX_SIZE_TOLERANCE} bytes. "
                f"Verify you have the correct USA variant (not PAL/JPN/other)."
            )
        target_path.parent.mkdir(parents=True, exist_ok=True)
        if not target_path.is_file() or target_path.stat().st_size != size:
            print(f"[Codegen] Staging USA XEX: {USA_XEX} -> {target_path}")
            shutil.copy2(str(USA_XEX), str(target_path))
        else:
            print(f"[Codegen] USA XEX already staged at {target_path} ({size} bytes). Skipping copy.")
        return target_path
    return target_path


def generate_code(xex_path, sdk_path=None, rexglue_bin=None, force_codegen=False):
    xex_path = pathlib.Path(xex_path).resolve()
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)

    # Auto-stage the USA XEX from xex/ directory when the caller is using the default path.
    xex_path = stage_usa_xex(xex_path)

    # Only fall back to pre-generated C++ archive if no XEX file exists and not forcing codegen.
    if not force_codegen and not xex_path.is_file():
        if check_and_extract_cache():
            return

    if not xex_path.is_file():
        fail(
            f"Target executable does not exist: {xex_path}\n"
            f"Place the NFS Carbon USA default.xex in 'xex/usa_default.xex' (preferred) or\n"
            f"'game/default.xex', or specify with --xex.\n"
            f"IMPORTANT: Use the USA XEX only. The PAL variant (CRC 5E74E60D) has different\n"
            f"addresses and will produce broken codegen (addresses shifted ~0x1D0 bytes)."
        )


    print(f"============================================================")
    print(f"  NFS Carbon Xbox 360 (454107EC) AOT Codegen Pipeline       ")
    print(f"  Input:  {xex_path}")
    print(f"  Output: {GENERATED_DIR}")
    print(f"============================================================")

    sdk_dir = pathlib.Path(sdk_path).resolve() if sdk_path else (ROOT.parent / "rexglue-sdk-ios")
    if not rexglue_bin:
        rexglue_exe = build_rexglue_cli(sdk_dir)
    else:
        rexglue_exe = pathlib.Path(rexglue_bin).resolve()

    if not rexglue_exe.is_file():
        fail(f"Invalid rexglue executable: {rexglue_exe}")

    print(f"[Codegen] Using rexglue tool: {rexglue_exe}")
    print(f"[Codegen] Running static analysis and PowerPC AOT translation...")

    # Execute rexglue codegen
    cmd = [
        str(rexglue_exe),
        "codegen",
        str(MANIFEST_FILE)
    ]
    res = subprocess.run(cmd, cwd=ROOT)
    if res.returncode != 0:
        fail(f"Codegen failed with exit code {res.returncode}")

    sources_cmake = GENERATED_DIR / "sources.cmake"
    if not sources_cmake.is_file():
        fail(f"Codegen did not produce expected sources file: {sources_cmake}")

    print(f"\n[Codegen Complete] Recompiled C++ sources generated at: {GENERATED_DIR}")


def main():
    parser = argparse.ArgumentParser(description="Ahead-of-Time Code Generator for NFS Carbon iOS")
    parser.add_argument("--xex", default=str(DEFAULT_XEX), help="Path to Carbon default.xex")
    parser.add_argument("--sdk", default=None, help="Path to ReXGlue iOS SDK")
    parser.add_argument("--rexglue", default=None, help="Path to pre-built rexglue CLI")
    parser.add_argument("--unpack-cache", action="store_true", help="Extract gen.tar.gz cache without invoking compiler")
    parser.add_argument("--force-codegen", action="store_true", help="Force recompilation even if cache is present")
    args = parser.parse_args()

    if args.unpack_cache:
        if check_and_extract_cache():
            sys.exit(0)
        else:
            fail("No gen.tar.gz cache found.")

    generate_code(args.xex, args.sdk, args.rexglue, force_codegen=args.force_codegen)


if __name__ == "__main__":
    main()
