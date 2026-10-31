#!/usr/bin/env python3
"""
tools/verify_usa_codegen.py
Fail-fast assertion: verify that rexglue codegen was run against the USA XEX,
not the PAL XEX (CRC 5E74E60D).

The USA and PAL binaries differ by ~0x1D0 bytes across the entire .text section.
After running codegen on the USA XEX, these specific functions MUST exist in
generated/default/nfsc_register.cpp.  If any one is absent the IPA was built from
PAL-generated code against a USA ISO mapping, which causes:
  - 75+ UNRESOLVED INDIRECT CALL errors (vtables, static initializers)
  - Null-pointer write at lr=0x821A35E8
  - Guest crash at <57 ms

Checked addresses (USA XEX, NFS Carbon Title ID 454107EC):
  0x829446E0  -- XEX entry point (IS the real entry in USA, not a blr stub!)
  0x82944510  -- xstart() CRT entry
  0x824E9780  -- Carbon main()
  0x823217D0  -- USA direct branch target (sub_823217D0)
  0x82A63408  -- USA direct branch target (sub_82A63408)
  0x82A66790  -- USA direct branch target (sub_82A66790)
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
REGISTER_CPP = ROOT / "generated" / "default" / "nfsc_register.cpp"

# Functions that MUST appear in the USA-generated nfsc_register.cpp.
REQUIRED_USA_SYMBOLS = [
    ("0x829446E0", "sub_829446E0", "XEX entry point (USA genuine, not PAL blr stub)"),
    ("0x82944510", "xstart",       "CRT entry point xstart"),
    ("0x824E9780", "sub_824E9780", "Carbon main()"),
    ("0x823217D0", "sub_823217D0", "USA direct branch entrypoint (0x823217D0)"),
    ("0x82A63408", "sub_82A63408", "USA direct branch entrypoint (0x82A63408)"),
    ("0x82A66790", "sub_82A66790", "USA direct branch entrypoint (0x82A66790)"),
]

# PAL-only addresses that must NOT appear (if they do, codegen was run on PAL XEX).
FORBIDDEN_PAL_ONLY_SYMBOLS: list[tuple[str, str]] = [
    # Example: PAL xstart was at 0x82944510 in PAL but we'd expect PAL-shifted values;
    # currently we don't have PAL function names to blacklist, so this list is intentionally empty.
    # Add entries here if we identify PAL-specific signatures.
]


def check_register_cpp() -> int:
    if not REGISTER_CPP.is_file():
        print(
            f"[verify_usa_codegen] FAIL: {REGISTER_CPP} not found!\n"
            f"  Run codegen first: python3 tools/generate_code.py --force-codegen"
        )
        return 1

    content = REGISTER_CPP.read_text(encoding="utf-8", errors="replace")
    failures: list[str] = []

    for addr, symbol, desc in REQUIRED_USA_SYMBOLS:
        # Match either the hex address or the symbol name in a dispatcher registration line.
        addr_pattern = re.compile(re.escape(addr), re.IGNORECASE)
        sym_pattern = re.compile(r'\b' + re.escape(symbol) + r'\b')
        if not addr_pattern.search(content) and not sym_pattern.search(content):
            failures.append(
                f"  MISSING: {symbol} ({addr}) -- {desc}"
            )

    for addr, desc in FORBIDDEN_PAL_ONLY_SYMBOLS:
        if re.search(re.escape(addr), content, re.IGNORECASE):
            failures.append(
                f"  FORBIDDEN PAL SYMBOL FOUND: {addr} -- {desc}\n"
                f"  This indicates codegen was run against the PAL XEX, not the USA XEX!"
            )

    if failures:
        print("[verify_usa_codegen] FAIL: USA codegen verification failed!\n")
        for f in failures:
            print(f)
        print(
            "\n[verify_usa_codegen] The generated code was probably produced from the PAL XEX.\n"
            "  Fix: run `python3 tools/generate_code.py --force-codegen` with xex/usa_default.xex\n"
            "  present, then re-run this script.\n"
            "  NEVER ship an IPA built from mismatched generated code."
        )
        return 1

    print(f"[verify_usa_codegen] PASS: All {len(REQUIRED_USA_SYMBOLS)} required USA symbols found in nfsc_register.cpp.")
    return 0


if __name__ == "__main__":
    sys.exit(check_register_cpp())
