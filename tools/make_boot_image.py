#!/usr/bin/env python3
"""
Create a bootable Subleq image from an ELF file.

This tool takes an ELF file and:
1. Extracts the raw binary using llvm-objcopy
2. Reads the entry point from the ELF header using llvm-readelf
3. Creates a boot sequence and prepends it to the binary
4. Outputs the result to <elf_file>.bootimage

Boot sequence layout:
- Word 0-2: Jump to word 3 (subleq 0,0,12)
- Word 3-5: subleq(24, SP_init_value, jump_to_main) - SP gets initialized via side effect
- Word 36: ZERO constant
- Word 38: -1 constant (MINUS_ONE)
- Word 39: 1 constant (ONE)
- Last 3 words: Jump to main (at text_start)
"""

import sys
import os
import struct
import argparse
import subprocess
import tempfile
import re

# Path to LLVM tools (relative to this script's location)
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
LLVM_BIN = os.path.join(PROJECT_ROOT, "llvm-project", "build", "bin")


# ESI register-file base, in WORDS. MUST match the toolchain's SUBLEQ_REG_BASE
# and the kernel's asm/subleq-regs.h REG_BASE (=REG_BASE*4 bytes), and the kernel
# link base (0x1000 for REG_BASE=0, 0x2000 for REG_BASE=1024).
#   0    = cable stock ABI (register file in page 0).
#   1024 = register file relocated to page 1 (page 0 reserved for I/O + vectors).
REG_BASE = 1024


def create_boot_sequence(text_start, stack_size, main_offset=0):
    """Create the boot sequence based on text_start.

    With REG_BASE=1024 the boot area is pages 0+1: page 0 = bootstrap + VM I/O +
    (disabled) interrupt vectors + kernel scratch; page 1 = the ESI register file.
    Kernel text starts at text_start (page 2). With REG_BASE=0 it is just page 0,
    reproducing cable's stock layout.
    """
    # Boot area is all the words before text_start
    boot_words = text_start // 4
    boot = [0] * boot_words

    # Word 0-2: initial jump subleq(0,0,12) -> word 3. Word 0/1 also serve as the
    # (disabled) interrupt-handler / saved-PC cells.
    boot[0] = 0       # A = byte addr 0 (also: interrupt handler = 0 = disabled)
    boot[1] = 0       # B = byte addr 0 (also: saved PC placeholder)
    boot[2] = 12      # C = byte addr 12 (word 3)

    # Word 3-5: unconditional jump subleq(0,0,jump_to_main) -> the last 3-word
    # 'jump to main' instruction. (Formerly abused word 4 to init SP via overlap;
    # with the register file moved to page 1, SP is pre-loaded directly below.)
    jump_to_main_addr = (boot_words - 3) * 4  # byte address of last 3-word jump
    boot[3] = 0                 # A = byte addr 0 (mem[0]=0)
    boot[4] = 0                 # B = byte addr 0 -> mem[0]-=mem[0]=0, branch taken
    boot[5] = jump_to_main_addr # C = byte addr of jump-to-main instruction

    # Word 6: video RAM location (VM cell, in words; does not move).
    boot[6] = 0x17F9C000

    # ESI register-file cells at their (relocated) homes.
    boot[4 + REG_BASE]  = stack_size  # SP init value (stack top)
    boot[36 + REG_BASE] = 0           # ZERO constant
    boot[38 + REG_BASE] = -1          # MINUS_ONE constant
    boot[39 + REG_BASE] = 1           # ONE constant

    # Last 3 words: jump to main. subleq(12,12,main): mem[word3]-=mem[word3]=0.
    boot[-3] = 12                      # A = byte addr 12 (word 3, =0 in boot area)
    boot[-2] = 12                      # B = byte addr 12
    boot[-1] = text_start + main_offset  # C = byte addr of main

    return boot


def get_entry_point(elf_file, llvm_readelf):
    """Get the entry point address from an ELF file."""
    try:
        result = subprocess.run(
            [llvm_readelf, "-h", elf_file],
            capture_output=True,
            text=True,
            check=True
        )
        # Look for "Entry point address:" line
        for line in result.stdout.splitlines():
            if "Entry" in line:
                # Extract hex value - typically looks like "Entry point address: 0x1234"
                match = re.search(r'0x([0-9a-fA-F]+)', line)
                if match:
                    return int(match.group(1), 16)
        raise RuntimeError(f"Could not find entry point in ELF header")
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"llvm-readelf failed: {e.stderr}")


def extract_binary(elf_file, output_file, llvm_objcopy):
    """Extract raw binary from ELF file."""
    try:
        subprocess.run(
            [llvm_objcopy, "-O", "binary", elf_file, output_file],
            check=True,
            capture_output=True,
            text=True
        )
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"llvm-objcopy failed: {e.stderr}")


def main():
    parser = argparse.ArgumentParser(
        description='Create a bootable Subleq image from an ELF file')
    parser.add_argument('elf_file', help='Input ELF file')
    parser.add_argument('--text-start', type=int, default=(4096 * (2 if REG_BASE else 1)),
                        help='Byte address where code starts (default: 8192 for '
                             'REG_BASE=1024 register-file-in-page-1, else 4096)')
    parser.add_argument('--stack-size', type=int, default=0x800000,
                        help='Stack pointer initial value in bytes (default: 8MB)')
    parser.add_argument('--llvm-bin', type=str, default=LLVM_BIN,
                        help=f'Path to LLVM bin directory (default: {LLVM_BIN})')
    parser.add_argument('--output', '-o', type=str, default=None,
                        help='Output file (default: <elf_file>.bootimage)')
    args = parser.parse_args()
    
    # Determine output filename
    output_file = args.output if args.output else f"{args.elf_file}.bootimage"
    
    # Paths to LLVM tools
    llvm_objcopy = os.path.join(args.llvm_bin, "llvm-objcopy")
    llvm_readelf = os.path.join(args.llvm_bin, "llvm-readelf")
    
    # Check that tools exist
    if not os.path.exists(llvm_objcopy):
        print(f"Error: llvm-objcopy not found at {llvm_objcopy}", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(llvm_readelf):
        print(f"Error: llvm-readelf not found at {llvm_readelf}", file=sys.stderr)
        sys.exit(1)
    
    # Check that ELF file exists
    if not os.path.exists(args.elf_file):
        print(f"Error: ELF file not found: {args.elf_file}", file=sys.stderr)
        sys.exit(1)
    
    # Get entry point from ELF
    entry_point = get_entry_point(args.elf_file, llvm_readelf)
    print(f"Entry point: 0x{entry_point:x}")
    
    # Calculate entry offset from text_start
    entry_offset = entry_point - args.text_start
    print(f"Entry offset from text_start: {entry_offset}")
    
    # Extract binary to temp file
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as tmp:
        tmp_binary = tmp.name
    
    try:
        extract_binary(args.elf_file, tmp_binary, llvm_objcopy)
        
        # Read raw binary
        with open(tmp_binary, 'rb') as f:
            code = f.read()
        
        # Create boot sequence
        boot = create_boot_sequence(args.text_start, args.stack_size, entry_offset)
        
        # Write output: boot sequence + code
        with open(output_file, 'wb') as f:
            # Write boot sequence as 32-bit little-endian words
            for val in boot:
                # Handle negative values (two's complement)
                if val < 0:
                    val = val & 0xFFFFFFFF
                f.write(struct.pack('<I', val))
            
            # Write code
            f.write(code)
        
        boot_size = len(boot) * 4
        code_size = len(code)
        total_size = boot_size + code_size
        print(f"Boot sequence: {boot_size} bytes ({len(boot)} words)")
        print(f"Code: {code_size} bytes ({code_size // 4} words)")
        print(f"Total: {total_size} bytes ({total_size // 4} words)")
        print(f"Entry at byte {args.text_start + entry_offset} (word {(args.text_start + entry_offset) // 4})")
        print(f"Output: {output_file}")
        
    finally:
        # Clean up temp file
        if os.path.exists(tmp_binary):
            os.unlink(tmp_binary)


if __name__ == '__main__':
    main()
