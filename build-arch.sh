#!/usr/bin/env bash
# build-arch.sh — reproducible from-scratch build of a lunatix/ESI arch (plan §5).
#
# Usage: ./build-arch.sh cable-nommu [FROM_STEP]
#   cable-nommu : pure Cable upstream, REG_BASE=0, NOMMU ("real mode"), ET_DYN/PIE.
#   (mmu arch to be added once cable-nommu is green.)
#   FROM_STEP   : optional step number (2..8) to resume from (skips earlier steps).
#
# Encodes docs/nommu-baseline.md steps 2-8, parameterized by arch. Step 1 (the LLVM
# Subleq toolchain) is prebuilt: cable-nommu uses llvm-project/build-nommu (configured
# -DSUBLEQ_REG_BASE_DEFAULT=0). Every downstream step is forced onto that toolchain so a
# stray default (llvm-project/build = MMU) can't contaminate a NOMMU sysroot.
#
# Logs to build-<arch>.log; prints "=== STEP n ..." markers; "=== BUILD-ARCH DONE" at the end.
set -o pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
ARCH="${1:?usage: build-arch.sh <cable-nommu> [from_step]}"
FROM="${2:-2}"

case "$ARCH" in
  cable-nommu)
    # cable-NOMMU is PURE Cable upstream, toolchain included: the stock bb79724 LLVM
    # (REG_BASE=0 is the upstream default, no option needed). The c591009-based option
    # build regresses NOMMU codegen (its '-static ET_EXEC' change), producing a kernel
    # that halts at boot — so cable-nommu must NOT use it. The REG_BASE build-time option
    # (SUBLEQ_REG_BASE_DEFAULT) is only for the MMU arch (REG_BASE=1024).
    TC="$ROOT/llvm-project/build-nommu-clean"  # bb79724, pure upstream, REG_BASE=0
    REGBASE=0
    LINUX_REF="c06286b"                     # pure Cable upstream (plan §4); boots once gen_runtime REG_BASE=0
    ;;
  *) echo "unknown arch '$ARCH' (only cable-nommu supported here)"; exit 2;;
esac

# Toolchain override (diagnostics): e.g. LUNATIX_TC_OVERRIDE=.../build-nommu-clean to build
# with the pre-c591009 (bb79724) REG_BASE=0 toolchain instead of the default option build.
[ -n "$LUNATIX_TC_OVERRIDE" ] && TC="$LUNATIX_TC_OVERRIDE"

SYS="$ROOT/runtime/sysroot"                   # rebuilt wholesale by this run -> clean NOMMU sysroot
CLANG="$TC/bin/clang"                         # default triple = subleq-unknown-linux
export SUBLEQ_TOOLCHAIN="$TC" SUBLEQ_SYSROOT="$SYS" SUBLEQ_LINUX="$ROOT/linux"
export SUBLEQ_REG_BASE="$REGBASE"             # runtime generator (gen_runtime.py) must match the arch
LOG="$ROOT/build-$ARCH.log"

log(){ echo "=== $* ==="; }
run(){ echo "+ $*"; "$@"; }

[ -x "$CLANG" ] || { echo "toolchain missing: $CLANG (build step 1 first)"; exit 1; }
"$CLANG" --version | grep -q "subleq-unknown-linux" || { echo "clang is not subleq-targeted"; exit 1; }
log "build-arch $ARCH  toolchain=$TC  regbase=$REGBASE  sysroot=$SYS  from step $FROM"

# ---- step 0: pin the kernel tree to the arch's reference commit (cable-nommu = pure c06286b)
if [ "$FROM" -le 2 ]; then
  log "STEP 0  pin linux -> $LINUX_REF"
  run git -C "$ROOT/linux" checkout -q "$LINUX_REF"
fi

# ---- step 2: kernel headers -> sysroot
if [ "$FROM" -le 2 ]; then
  log "STEP 2  kernel headers_install"
  # headers_install compiles only HOST scripts (fixdep); use the system HOSTCC, not the
  # subleq clang (which can't build host binaries). No target CC needed here.
  run make -C "$ROOT/linux" ARCH=subleq HOSTCC=gcc \
      INSTALL_HDR_PATH="$SYS/kernel-headers" headers_install
fi

# ---- step 3: uClibc-ng -> sysroot (dev headers + libs). NOMMU .config already in tree.
if [ "$FROM" -le 3 ]; then
  log "STEP 3  uClibc-ng"
  run make -C "$ROOT/uclibc-ng" ARCH=subleq CROSS_COMPILE="" CC="$CLANG" HOSTCC=gcc \
      KERNEL_HEADERS="$SYS/kernel-headers/include" -j"$(nproc)"
  run make -C "$ROOT/uclibc-ng" ARCH=subleq CROSS_COMPILE="" CC="$CLANG" HOSTCC=gcc \
      KERNEL_HEADERS="$SYS/kernel-headers/include" PREFIX="$SYS" install
  run "$ROOT/uclibc-ng/postprocess_sysroot.sh" "$SYS"
  # Force-reassemble the C-runtime startup objects from source and install them. uClibc's
  # make can leave a STALE crt1.o (older than crt1.S) — and crt1.o is the userspace _start;
  # a stale/mis-toolchained one makes every binary read a wild pointer at launch and die
  # (the §7 "self-compiled userspace never ran" bug). Rebuilding here guarantees a fresh
  # _start built with THIS arch's toolchain.
  for c in crt1 crti crtn; do
    csrc="$ROOT/uclibc-ng/libc/sysdeps/linux/subleq/$c.S"
    [ -f "$csrc" ] && { run "$CLANG" -c "$csrc" -o "$SYS/lib/$c.o"; }
  done
fi

# ---- step 4: core / soft-float / libc++abi runtime (honors SUBLEQ_TOOLCHAIN/SYSROOT/LINUX)
if [ "$FROM" -le 4 ]; then
  log "STEP 4  core runtime"
  run "$ROOT/runtime/build_and_install_runtime.sh"
fi

# ---- step 5: libc++ (configure then build+install; initramfs bundles libc++.so.1.0)
if [ "$FROM" -le 5 ]; then
  log "STEP 5  libc++"
  run "$ROOT/runtime/build_libcxx.sh"
  run ninja -C "$ROOT/runtime/libcxx" install
fi

# ---- step 6: BusyBox (its .config is in-tree; force the NOMMU toolchain)
if [ "$FROM" -le 6 ]; then
  log "STEP 6  BusyBox"
  run make -C "$ROOT/busybox" CC="$CLANG" HOSTCC=cc \
      AR="$TC/bin/llvm-ar" STRIP="$TC/bin/llvm-strip" SKIP_STRIP=y -j"$(nproc)"
  run cp "$ROOT/busybox/busybox" "$ROOT/busybox/initramfs_root/bin/busybox"
fi

# ---- step 6.5: fbdoom (optional; WITH_DOOM=1). Build DOOM from source against the fresh
#      REG_BASE-correct sysroot, install it + its WAD into the initramfs, and swap in a DOOM
#      launcher /init. Must run AFTER the sysroot runtime (step 4) so doom links page-0 regs.
if [ "$FROM" -le 6 ] && [ -n "$WITH_DOOM" ]; then
  log "STEP 6.5  fbdoom (build from source + initramfs)"
  run make -C "$ROOT/doom" clean
  run make -C "$ROOT/doom" CC="$CLANG" MC="$TC/bin/llvm-mc" -j"$(nproc)"
  run mkdir -p "$ROOT/initramfs_root/root/doom"
  run cp "$ROOT/doom/doom" "$ROOT/initramfs_root/root/doom/doom"
  run cp "$ROOT/doom/doom1.wad" "$ROOT/initramfs_root/root/doom/doom1.wad"
  run chmod +x "$ROOT/initramfs_root/root/doom/doom"
  cat > "$ROOT/initramfs_root/init" <<'SH'
#!/bin/sh
/bin/busybox mount -t devtmpfs none /dev
/bin/busybox mount -t proc     none /proc
/bin/busybox mount -t sysfs    none /sys
cd /root/doom
echo "lunatix: launching fbdoom..." > /dev/console
./doom < /dev/tty0 > /dev/console 2>&1
exec /bin/sh
SH
  run chmod +x "$ROOT/initramfs_root/init"
fi

# ---- step 7: kernel (NOMMU: CONFIG_MMU unset via defconfig) -> linux/vmlinux
if [ "$FROM" -le 7 ]; then
  log "STEP 7  kernel mrproper + defconfig + build"
  # LLVM= points every tool at the subleq toolchain; override the HOST tools back to the
  # system compiler so host-side kernel scripts still build.
  K=(ARCH=subleq LLVM="$TC/bin/" HOSTCC=gcc HOSTCXX=g++ HOSTLD=ld HOSTAR=ar)
  # A from-scratch build MUST start clean: this tree may have just been switched from
  # another branch (e.g. via STEP 0), and an incremental make would link stale objects
  # into a Frankenstein kernel that halts at boot. mrproper wipes all generated state.
  run make -C "$ROOT/linux" "${K[@]}" mrproper
  # mrproper also removes the runtime libs copied in by STEP 4; restore them.
  run "$ROOT/runtime/build_and_install_runtime.sh"
  run make -C "$ROOT/linux" "${K[@]}" defconfig
  # DEBUG_CONSOLE=1: route the console to ttyS0 (which writes via __subleq_putchar -> host
  # stdout) and keep the boot console, so kernel AND userspace output are visible headless
  # (default console=tty0 goes to the framebuffer, hiding userspace + any exec/panic).
  if [ -n "$DEBUG_CONSOLE" ]; then
    run "$ROOT/linux/scripts/config" --file "$ROOT/linux/.config" \
        --set-str CONFIG_CMDLINE "console=ttyS0 keep_bootcon loglevel=8"
    run make -C "$ROOT/linux" "${K[@]}" olddefconfig
  fi
  run make -C "$ROOT/linux" "${K[@]}" -j"$(nproc)"
fi

# ---- step 8: boot image (REG_BASE=0 for cable-nommu)
if [ "$FROM" -le 8 ]; then
  log "STEP 8  boot image (--reg-base $REGBASE)"
  run python3 "$ROOT/tools/make_boot_image.py" --reg-base "$REGBASE" \
      --stack-size 536870912 "$ROOT/linux/vmlinux"
  ls -la "$ROOT/linux/vmlinux.bootimage"
fi

log "BUILD-ARCH DONE $ARCH -> $ROOT/linux/vmlinux.bootimage"
