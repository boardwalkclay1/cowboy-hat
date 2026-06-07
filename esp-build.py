#!/usr/bin/env python3
import os, sys, subprocess
from pathlib import Path

# Supported ESP chips + toolchain prefixes
TOOLCHAINS = {
    "esp32":      "xtensa-esp32-elf",
    "esp32s2":    "xtensa-esp32s2-elf",
    "esp32s3":    "xtensa-esp32s3-elf",
    "esp32c3":    "riscv32-esp-elf",
    "esp32c6":    "riscv32-esp-elf",
    "esp32h2":    "riscv32-esp-elf"
}

def run(cmd):
    print(">>", " ".join(cmd))
    subprocess.check_call(cmd)

def main():
    if len(sys.argv) < 3:
        print("Usage: espbuild.py <chip> <sourcefile.c/.cpp> [output.bin]")
        sys.exit(1)

    chip = sys.argv[1].lower()
    src  = Path(sys.argv[2]).resolve()
    out  = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else src.with_suffix(".bin")

    if chip not in TOOLCHAINS:
        print(f"Unsupported chip: {chip}")
        print("Supported:", ", ".join(TOOLCHAINS.keys()))
        sys.exit(1)

    if not src.exists():
        print("Source file not found:", src)
        sys.exit(1)

    tc = TOOLCHAINS[chip]
    build = src.parent / ".espbuild"
    build.mkdir(exist_ok=True)

    elf = build / "app.elf"

    # Compile
    run([
        f"{tc}-gcc",
        "-Os", "-nostdlib",
        "-o", str(elf),
        str(src)
    ])

    # Convert to BIN
    run([
        f"{tc}-objcopy",
        "-O", "binary",
        str(elf),
        str(out)
    ])

    print(f"\n✔ Built {out}")

if __name__ == "__main__":
    main()
