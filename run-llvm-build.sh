#!/usr/bin/env bash
# Detached LLVM Subleq toolchain build. Survives SSH/screen death (launched via setsid).
# Progress: llvm-build.log ; completion: llvm-build.done (contains exit code + timestamp).
set -o pipefail
cd "$(dirname "$0")"
rm -f llvm-build.done
START=$(date +%s)
ninja -C llvm-project/build \
  llc clang llvm-mc lld llvm-objcopy llvm-ar llvm-ranlib \
  llvm-nm llvm-readelf llvm-strip llvm-objdump 2>&1 | tee llvm-build.log
RC=${PIPESTATUS[0]}
echo "exit=$RC elapsed=$(( $(date +%s) - START ))s at=$(date -Is)" > llvm-build.done
