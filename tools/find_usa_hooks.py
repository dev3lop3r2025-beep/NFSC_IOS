#!/usr/bin/env python3
"""
tools/find_usa_hooks.py
After running rexglue codegen against xex/usa_default.xex, scan the generated
C++ output to find the correct USA guest addresses for all hardcoded hook sites.

The USA XEX payload is LZX-compressed and cannot be scanned directly. This script
reads the generated nfsc_register.cpp and generated/*.cpp to:
  1. Confirm that required USA boot symbols are registered.
  2. Find the USA equivalents of all hardcoded PAL addresses in:
       - src/dispatch_tolerance.cpp  (kernel import thunks)
       - src/render_mode_hook.cpp    (renderer ctor/select/size + data globals)
       - src/carbon_missing_entries.cpp (fixed PPC-sequence addresses)

Output is printed as a diff-ready summary of required source changes.

Usage (run AFTER codegen):
  python3 tools/find_usa_hooks.py

Pre-requisite:
  python3 tools/generate_code.py --force-codegen   (uses xex/usa_default.xex)
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
REGISTER_CPP = ROOT / "generated" / "default" / "nfsc_register.cpp"
GENERATED_DIR = ROOT / "generated" / "default"


# ============================================================================
# Symbols we must find in nfsc_register.cpp / generated C++ after USA codegen
# ============================================================================

# Known PAL addresses and their descriptions, for cross-reference reporting.
PAL_HOOKS = {
    # dispatch_tolerance.cpp kernel imports (PAL addresses)
    "XamLoaderTerminateTitle":     0x82AACDF4,
    "ExTerminateThread":           0x82AAD5A4,
    "XexUnloadImageAndExitThread": 0x82AAD714,
    "HalReturnToFirmware":         0x82AAD774,
    "KeBugCheck":                  0x82AAC344,
    "KeBugCheckEx":                0x82AAD494,
    "DbgPrint":                    0x82AACE24,
    # render_mode_hook.cpp (PAL addresses)
    "renderer_ctor sub_824FFAB0":  0x824FFAB0,
    "renderer_select sub_8250B0D0": 0x8250B0D0,
    "renderer_size sub_82500168":  0x82500168,
    # carbon_missing_entries.cpp (PAL addresses)
    "state_entry 0x82296468":      0x82296468,
    "story_entry 0x822A9490":      0x822A9490,
    "adjustor_thunk 0x82641480":   0x82641480,
    "vtable_dispatch 0x82322048":  0x82322048,
    # Boot chain (PAL)
    "xstart":                      0x82944510,
    "carbon_main sub_824E9780":    0x824E9780,
    "entry_point sub_829446E0":    0x829446E0,
    "display_check sub_82944340":  0x82944340,
}

# Import names to search for in nfsc_register.cpp to find the USA thunk addresses.
# The generated code for kernel imports usually has a comment or function name
# containing the import ordinal or name.
KERNEL_IMPORT_NAMES = [
    "XamLoaderTerminateTitle",
    "ExTerminateThread",
    "XexUnloadImageAndExitThread",
    "HalReturnToFirmware",
    "KeBugCheck",
    "KeBugCheckEx",
    "DbgPrint",
]


def find_register_file() -> pathlib.Path | None:
    for name in ("nfscarbon_register.cpp", "nfsc_register.cpp"):
        p = GENERATED_DIR / name
        if p.is_file():
            return p
    if GENERATED_DIR.is_dir():
        for f in GENERATED_DIR.glob("*register*.cpp"):
            if f.is_file():
                return f
    return None


def read_register_cpp() -> str:
    p = find_register_file()
    if not p:
        print(f"[find_usa_hooks] ERROR: No register.cpp found under {GENERATED_DIR}.")
        print("  Run codegen first: python3 tools/generate_code.py --force-codegen")
        sys.exit(1)
    return p.read_text(encoding="utf-8", errors="replace")


def find_all_registered_addresses(content: str) -> set[int]:
    """Extract all 0x8xxxxxxx addresses from SetFunction/RegisterFunction calls."""
    pattern = re.compile(r'0x(8[0-9A-Fa-f]{7})')
    return {int(m.group(1), 16) for m in pattern.finditer(content)}


def find_symbol_address(content: str, symbol: str) -> list[int]:
    """Find guest addresses associated with a symbol name in nfsc_register.cpp."""
    # Patterns like: SetFunction(0x82944510, &xstart)
    # or: RegisterFunction("xstart", 0x82944510)
    # or: // xstart at 0x82944510
    addr_pattern = re.compile(
        r'(?i)' + re.escape(symbol) + r'[^0-9A-Fa-f]*0x(8[0-9A-Fa-f]{7})'
        r'|'
        r'0x(8[0-9A-Fa-f]{7})[^0-9A-Fa-f]*' + re.escape(symbol),
        re.IGNORECASE,
    )
    results = []
    for m in addr_pattern.finditer(content):
        addr_str = m.group(1) or m.group(2)
        if addr_str:
            results.append(int(addr_str, 16))
    return results


def find_import_thunks(content: str, import_name: str) -> list[int]:
    """Find kernel import thunk addresses via comment/name patterns in generated C++."""
    # Look for patterns like:
    #   SetFunction(0x82AACDF4, &__kernel_XamLoaderTerminateTitle_thunk);
    #   // XamLoaderTerminateTitle (ordinal 0x1234) -> 0x82AACDF4
    results = find_symbol_address(content, import_name)
    # Also search as a substring of function names
    pattern = re.compile(r'0x(8[0-9A-Fa-f]{7})[^\n]*' + re.escape(import_name), re.IGNORECASE)
    for m in pattern.finditer(content):
        addr = int(m.group(1), 16)
        if addr not in results:
            results.append(addr)
    return results


def scan_generated_sources(symbol_names: list[str]) -> dict[str, list[int]]:
    """Search all generated .cpp files for symbol addresses."""
    results: dict[str, list[int]] = {name: [] for name in symbol_names}
    cpp_files = list(GENERATED_DIR.glob("*.cpp"))
    print(f"[find_usa_hooks] Scanning {len(cpp_files)} generated C++ files...")
    for cpp_path in cpp_files:
        content = cpp_path.read_text(encoding="utf-8", errors="replace")
        for name in symbol_names:
            addrs = find_symbol_address(content, name)
            for a in addrs:
                if a not in results[name]:
                    results[name].append(a)
    return results


def report() -> None:
    print("=" * 72)
    print("  NFS Carbon USA XEX Hook Address Finder")
    print("  Scans codegen output to find USA equivalents of PAL hook addresses")
    print("=" * 72)
    print()

    reg_content = read_register_cpp()
    all_addresses = find_all_registered_addresses(reg_content)
    print(f"[find_usa_hooks] nfsc_register.cpp: {len(all_addresses)} registered addresses")
    print()

    # -------------------------------------------------------------------------
    # 1. Boot chain symbols
    # -------------------------------------------------------------------------
    print("--- Boot chain symbols (dispatch_tolerance.cpp) ---")
    boot_syms = ["xstart", "sub_829446E0", "sub_824E9780", "sub_82944340"]
    for sym in boot_syms:
        hits = find_symbol_address(reg_content, sym)
        pal_addr = next(
            (v for k, v in PAL_HOOKS.items() if sym in k), None
        )
        status = "FOUND" if hits else "NOT FOUND"
        note = f"PAL was 0x{pal_addr:08X}" if pal_addr else ""
        print(f"  {sym}: {status}  {note}")
        for h in hits:
            print(f"    USA addr: 0x{h:08X}")
    print()

    # -------------------------------------------------------------------------
    # 2. Kernel import thunks (dispatch_tolerance.cpp)
    # -------------------------------------------------------------------------
    print("--- Kernel import thunks (dispatch_tolerance.cpp) ---")
    print("  These are dynamically registered by rexglue from the XEX import table.")
    for import_name in KERNEL_IMPORT_NAMES:
        pal_addr = PAL_HOOKS.get(import_name, 0)
        hits = find_import_thunks(reg_content, import_name)
        status = "FOUND" if hits else "NOT FOUND in register.cpp"
        print(f"  {import_name}:")
        print(f"    PAL: 0x{pal_addr:08X}  Status: {status}")
        for h in hits:
            print(f"    USA thunk: 0x{h:08X}")
    print()
    print("  NOTE: Kernel thunk addresses are in the .text section of the XEX")
    print("  import table. If not found above, search generated nfsc_register.cpp")
    print("  for 'XamLoaderTerminateTitle' or look in generated/default/*.cpp")
    print()

    # -------------------------------------------------------------------------
    # 3. Renderer hook functions (render_mode_hook.cpp)
    # -------------------------------------------------------------------------
    print("--- Renderer hook functions (render_mode_hook.cpp) ---")
    renderer_syms = {
        "sub_824FFAB0": "renderer ctor",
        "sub_8250B0D0": "mode select",
        "sub_82500168": "output size",
    }
    for sym, desc in renderer_syms.items():
        hits = find_symbol_address(reg_content, sym)
        pal_addr = PAL_HOOKS.get(f"renderer_{desc} {sym}", 0)
        status = "FOUND" if hits else "NOT FOUND"
        print(f"  {sym} ({desc}): {status}")
        for h in hits:
            print(f"    USA addr: 0x{h:08X}")
            print(f"    -> Update render_mode_hook.cpp: sub_{h:08X}")
        if not hits:
            print(f"    PAL was 0x{pal_addr:08X}")
            print(f"    -> Run tools/find_usa_hooks.py after codegen completes")
    print()

    # -------------------------------------------------------------------------
    # 4. carbon_missing_entries.cpp fixed addresses
    # -------------------------------------------------------------------------
    print("--- carbon_missing_entries.cpp fixed-sequence addresses ---")
    fixed_syms = {
        "sub_82296468": ("state_entry", 0x82296468),
        "sub_822A9490": ("story_entry", 0x822A9490),
        "sub_82641480": ("adjustor",   0x82641480),
        "sub_82322048": ("vtable_dispatch", 0x82322048),
    }
    for sym, (varname, pal_addr) in fixed_syms.items():
        hits = find_symbol_address(reg_content, sym)
        status = "FOUND" if hits else "NOT FOUND"
        print(f"  {sym} ({varname}): {status}  PAL=0x{pal_addr:08X}")
        for h in hits:
            print(f"    USA addr: 0x{h:08X}")
            print(f"    -> Update carbon_missing_entries.cpp: constexpr uint32_t {varname} = 0x{h:08X};")
    print()

    # -------------------------------------------------------------------------
    # 5. Summary: PAL addresses that still appear in register.cpp
    # -------------------------------------------------------------------------
    pal_addrs_in_gen = {v for v in PAL_HOOKS.values() if v in all_addresses}
    if pal_addrs_in_gen:
        print("--- WARNING: PAL addresses found in nfsc_register.cpp ---")
        print("  This suggests codegen was run against the PAL XEX, not the USA XEX!")
        for name, addr in PAL_HOOKS.items():
            if addr in pal_addrs_in_gen:
                print(f"  PAL 0x{addr:08X} ({name}) appears in USA register.cpp")
    else:
        print("--- No PAL-only addresses found in nfsc_register.cpp (expected for USA codegen) ---")
    print()
    print("[find_usa_hooks] Done. Apply address changes above to dispatch_tolerance.cpp,")
    print("                 render_mode_hook.cpp, and carbon_missing_entries.cpp.")


if __name__ == "__main__":
    report()
