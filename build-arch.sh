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
    LINUX_REF="51a994c5c390"                 # c06286b + userspace-exec fixes (put_user, ELF_HWCAP); c06286b scrambles create_elf_tables -> doom crashes
    UCLIBC_REF="073fbd989"                    # cable-upstream uClibc: page-0 crt, direct-call syscall
    ;;
  mmu)
    # MMU ("protected mode"): REG_BASE=1024 (register file on page 1), DYNAMIC (ET_DYN)
    # userspace, paging + NULL guard. Uses the 5660584 toolchain (enver-haase fork, REG_BASE=1024)
    # — the one that built the confirmed-rendering MMU-DOOM-sound image (~/vmlinux.mmu-sound.bootimage).
    # NB: the c591009/build-mmu toolchain (static ET_EXEC) REGRESSES doom codegen (M_CheckParm
    # returns to a bad RA) and must NOT be used for MMU. Kernel = lunatix-mmu with CONFIG_MMU=y
    # forced (its defconfig is actually NOMMU). MMU images are lunavm-only (CableVM has no MMU).
    TC="$ROOT/llvm-project/build"              # 5660584 (enver-haase), REG_BASE=1024, dynamic doom
    REGBASE=1024
    LINUX_REF="lunatix-mmu"
    UCLIBC_REF="lunatix-mmu"                   # MMU uClibc (1dc68ffce): page-1 crt regs + syscall-gate stub
    MMU=1
    ;;
  *) echo "unknown arch '$ARCH' (supported: cable-nommu, mmu)"; exit 2;;
esac

# Toolchain override (diagnostics): e.g. LUNATIX_TC_OVERRIDE=.../build-nommu-clean to build
# with the pre-c591009 (bb79724) REG_BASE=0 toolchain instead of the default option build.
[ -n "$LUNATIX_TC_OVERRIDE" ] && TC="$LUNATIX_TC_OVERRIDE"

# WITH_SOUND=1: use the sound-enabled kernel (nommu-sound-v2 = 51a994 + the /dev/dsp + /dev/opl
# driver, NOMMU, CONFIG_SUBLEQ_SOUND=y). The doom userspace already links the sound backend
# (device/i_snd_sound.c + i_snd_music.c); it just needs the kernel to expose the devices. The
# sound MMIO regs are top-of-space sentinels the lunavm sound card intercepts (stock CableVM
# has no sound card, so sound is lunavm-only until §8 moves the regs to the zero page).
if [ -n "$WITH_SOUND" ] && [ "$ARCH" = cable-nommu ]; then
  LINUX_REF="nommu-sound-v2"
fi

# ONE SYSROOT PER ARCHITECTURE. The two flavours' libc.a, crt1.o, libc.so and busybox are
# not interchangeable -- an MMU libc under a NOMMU kernel (or the reverse) dies at startup --
# and a shared sysroot means whichever arch built last silently supplies the C library to the
# other. That is the mixed-sysroot trap this tree has already paid for twice. There is
# deliberately no plain runtime/sysroot any more: a stale hardcoded reference should fail
# loudly rather than quietly link the wrong flavour.
SYS="$ROOT/runtime/sysroot-$ARCH"

# The consumers hardcode the sysroot path in files under version control (doom/Makefile,
# busybox/.config, uclibc-ng/.config), so point them at this arch's sysroot. The pattern
# matches any runtime/sysroot* suffix, so re-running for either arch just rewrites it.
retarget_sysroot() {
  sed -i -E "s#(runtime/sysroot)[A-Za-z0-9_-]*#\1-$ARCH#g" \
      "$ROOT/doom/Makefile" "$ROOT/busybox/.config" "$ROOT/uclibc-ng/.config"
  grep -h -oE "runtime/sysroot[A-Za-z0-9_-]*" "$ROOT/doom/Makefile" "$ROOT/busybox/.config" \
      "$ROOT/uclibc-ng/.config" | sort -u | sed "s/^/    consumers now use /"
}
# Prove the C library actually reached THIS arch's sysroot before anything links against it.
# uClibc's RUNTIME_PREFIX/DEVEL_PREFIX are CONCATENATED onto PREFIX, so a stray "build/" in
# them installs a perfectly good libc into <sysroot>build/ -- a sibling directory nothing
# reads -- while every link quietly picks up whatever stale libc.a the sysroot already held.
# Statically, i.e. baked into busybox and doom. That is how a July libc ended up inside the
# MMU userspace under freshly assembled crt objects, and it presents as a userspace that dies
# at _start three steps later. Fail here, with the evidence, instead.
assert_libc_installed() {
  local installed="$SYS/lib/libc.a" built="$ROOT/uclibc-ng/lib/libc.a"
  if [ ! -f "$installed" ]; then
    echo "FATAL: $installed is missing -- uClibc's install did not reach this sysroot."
    echo "       Check RUNTIME_PREFIX/DEVEL_PREFIX in uclibc-ng/.config: they are appended to"
    echo "       PREFIX, so they must be \"/\" for a sysroot install. Stray copies:"
    find "$ROOT/runtime" -maxdepth 3 -name libc.a -printf "         %TY-%Tm-%Td %TH:%TM  %p\n" 2>/dev/null
    exit 1
  fi
  if [ -f "$built" ] && [ "$built" -nt "$installed" ]; then
    echo "FATAL: $installed is OLDER than the libc just built in uclibc-ng/lib."
    echo "       The install went somewhere else; linking now would bake a stale C library"
    echo "       into busybox and doom. Same cause as above: check the two PREFIX variables."
    ls -la "$built" "$installed"
    find "$ROOT/runtime" -maxdepth 3 -name libc.a -printf "         %TY-%Tm-%Td %TH:%TM  %p\n" 2>/dev/null
    exit 1
  fi
  log "libc.a in place: $(ls -la "$installed" | awk '{print $6, $7, $8, $9}')"
}

CLANG="$TC/bin/clang"                         # default triple = subleq-unknown-linux
export SUBLEQ_TOOLCHAIN="$TC" SUBLEQ_SYSROOT="$SYS" SUBLEQ_LINUX="$ROOT/linux"
export SUBLEQ_REG_BASE="$REGBASE"             # runtime generator (gen_runtime.py) must match the arch
LOG="$ROOT/build-$ARCH.log"

log(){ echo "=== $* ==="; }
# A failing command must stop the build. Without the status check every later step ran on stale
# output and the script still printed "BUILD-ARCH DONE" with a boot image from the previous run --
# which has cost real debugging time more than once (a uClibc that did not compile, an image that
# was never rebuilt). The per-step file checks further down catch some of it; this catches all.
run(){ echo "+ $*"; if ! "$@"; then echo "=== FAILED: $* ==="; exit 1; fi; }

[ -x "$CLANG" ] || { echo "toolchain missing: $CLANG (build step 1 first)"; exit 1; }
"$CLANG" --version | grep -q "subleq-unknown-linux" || { echo "clang is not subleq-targeted"; exit 1; }
log "build-arch $ARCH  toolchain=$TC  regbase=$REGBASE  sysroot=$SYS  from step $FROM"
log "retarget consumers to sysroot-$ARCH"
retarget_sysroot

# ---- step 0: pin the kernel + uClibc trees to the arch's reference commits.
# cable-NOMMU: pure upstream (page-0 crt, direct-call syscall). MMU: lunatix-mmu uClibc has
# page-1 crt registers + the syscall-gate stub (commit 1dc68ffce) — without it a page-1 MMU
# process reads page-0 registers (unmapped) and SIGSEGVs at _start.
if [ "$FROM" -le 3 ]; then
  log "STEP 0  pin linux -> $LINUX_REF ; uclibc -> $UCLIBC_REF"
  [ "$FROM" -le 2 ] && run git -C "$ROOT/linux" checkout -q "$LINUX_REF"
  run git -C "$ROOT/uclibc-ng" checkout -q "$UCLIBC_REF"
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
  # STRIPTOOL must be the subleq-aware llvm-strip: the MMU uClibc strips objects, and the host
  # `strip` can't read subleq ELF ("Unable to recognise the format") -> build fails.
  UC=(ARCH=subleq CROSS_COMPILE="" CC="$CLANG" HOSTCC=gcc STRIPTOOL="$TC/bin/llvm-strip"
      KERNEL_HEADERS="$SYS/kernel-headers/include")
  run make -C "$ROOT/uclibc-ng" "${UC[@]}" -j"$(nproc)"
  run make -C "$ROOT/uclibc-ng" "${UC[@]}" PREFIX="$SYS" install
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
  log "STEP 6  BusyBox (static)"
  assert_libc_installed
  # Link BusyBox statically: dynamic linking (ld-uClibc + libc.so) does not yet work in the
  # self-built NOMMU userspace (busybox dies at dynamic startup), but -static produces a
  # working self-relocating static-PIE (same path as the static crt test + the doom binary).
  # CONFIG_STATIC=y avoids needing libc.so/ld-uClibc at all.
  # Linkage: MMU is STATIC (the confirmed-rendering GOOD image is static ET_EXEC — its
  # initramfs, extracted 2026-07-23, has NO ld-uClibc/libc.so; busybox+doom are static).
  # cable-NOMMU is DYNAMIC by default (upstream: kernel resolves NEEDED libs at load).
  # BUSYBOX_STATIC=1 forces static on NOMMU too (fallback if kernel dynamic linking regresses).
  # The MMU arch must not build BusyBox for NOMMU: with CONFIG_NOMMU=y the shell uses vfork()
  # everywhere and re-executes itself through /proc/self/exe to run a command, which on this
  # system hangs every external program while builtins and in-process applets keep working.
  if [ -n "$MMU" ]; then
    sed -i 's/^CONFIG_NOMMU=y/# CONFIG_NOMMU is not set/' "$ROOT/busybox/.config"
  else
    sed -i 's/^# CONFIG_NOMMU is not set/CONFIG_NOMMU=y/' "$ROOT/busybox/.config"
  fi
  if [ -n "$BUSYBOX_STATIC" ] || [ -n "$MMU" ]; then
    sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "$ROOT/busybox/.config"
    grep -q '^CONFIG_EXTRA_LDFLAGS=.*-static' "$ROOT/busybox/.config" || \
      sed -i 's#^CONFIG_EXTRA_LDFLAGS="#CONFIG_EXTRA_LDFLAGS="-static #' "$ROOT/busybox/.config"
  else
    sed -i 's/^CONFIG_STATIC=y/# CONFIG_STATIC is not set/' "$ROOT/busybox/.config"
    sed -i 's#^CONFIG_EXTRA_LDFLAGS="-static #CONFIG_EXTRA_LDFLAGS="#' "$ROOT/busybox/.config"
  fi
  run make -C "$ROOT/busybox" CC="$CLANG" HOSTCC=cc oldconfig
  run make -C "$ROOT/busybox" CC="$CLANG" HOSTCC=cc \
      AR="$TC/bin/llvm-ar" STRIP="$TC/bin/llvm-strip" SKIP_STRIP=y -j"$(nproc)"
  # Install into the kernel's initramfs (CONFIG_INITRAMFS_SOURCE=../initramfs_root), NOT
  # busybox/initramfs_root (which doesn't exist — the old path silently failed, baking a
  # stale busybox).
  run cp "$ROOT/busybox/busybox" "$ROOT/initramfs_root/bin/busybox"
fi

# ---- step 6.5: fbdoom (optional; WITH_DOOM=1). Build DOOM from source against the fresh
#      REG_BASE-correct sysroot, install it + its WAD into the initramfs, and swap in a DOOM
#      launcher /init. Must run AFTER the sysroot runtime (step 4) so doom links page-0 regs.
if [ "$FROM" -le 6 ] && [ -n "$WITH_DOOM" ]; then
  log "STEP 6.5  fbdoom (build from source + initramfs)"
  assert_libc_installed
  # doom_asm.S is arch-specific: the MMU commit 8367ddf moved R_DrawColumn/R_DrawSpan
  # draw-scratch from .text to .bss (needed because MMU makes .text read-only). On NOMMU
  # .text is writable and that .bss move corrupts memory near the visplane arrays -> doom
  # crashes in R_InitPlanes. cable-NOMMU must use the upstream (pre-MMU) doom_asm.S where
  # the scratch lives in .text.
  if [ "$REGBASE" = 0 ]; then
    run git -C "$ROOT/doom" checkout 511dffb -- src/doom_asm.S       # NOMMU: scratch in writable .text
  else
    # MMU: .text is read-only under paging, so use the .bss-scratch doom_asm.S (commit 8367ddf,
    # which lives in the nested doom/ layout). Extract it into this (flat) tree.
    echo "+ install MMU (.bss) doom_asm.S from 8367ddf"
    git -C "$ROOT/doom" show 8367ddf:doom/src/doom_asm.S > "$ROOT/doom/src/doom_asm.S"
  fi
  # Linkage: MMU = STATIC ET_EXEC; NOMMU = DYNAMIC ET_DYN. GROUND TRUTH (2026-07-23): the
  # confirmed-rendering GOOD MMU image (~/vmlinux.mmu-sound-doom.GOOD.bootimage) has a STATIC
  # ET_EXEC doom (e_type=EXEC, 7.0MB) in a mmu_initramfs.txt cpio with no shared libs. The
  # earlier "dynamic for both" note was wrong (it never rendered). The c591009 regression was
  # a TOOLCHAIN issue, not staticness — static built by 5660584 (build/) renders fine.
  if [ -n "$MMU" ]; then
    grep -q '\-static -o doom' "$ROOT/doom/Makefile" || \
      sed -i 's#-o doom \$(TARGET_OBJS)#-static -o doom $(TARGET_OBJS)#' "$ROOT/doom/Makefile"
  else
    sed -i 's#-static \(-static \)*-o doom \$(TARGET_OBJS)#-o doom $(TARGET_OBJS)#' "$ROOT/doom/Makefile"
  fi
  # THOROUGH clean: `make clean` leaves the 64 objects in doom/build/, so a subsequent build
  # re-links whatever .o's are present — including STALE objects from a prior build with a
  # DIFFERENT toolchain (e.g. build-nommu/2ba79ab). That produced a mixed-toolchain doom
  # (bb79724 + 2ba79ab) that HANGS after I_InitSound (same "stale object" disease as the §7
  # crt1.o bug). Remove all objects so every .o is compiled fresh by $CLANG -> uniform toolchain.
  run rm -rf "$ROOT/doom/build"
  run find "$ROOT/doom" -name '*.o' -delete
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
# A WAD supplied by the VM host (Vaadoom: fetched by the browser) shows up as
# /dev/wad, with the name it should be played under in /dev/wadname. DOOM's
# IdentifyVersion() picks the game mode from that name, so link the device under
# it and drop the bundled shareware WAD when the two differ. No copying: DOOM
# reads the lumps straight off the device, and the host does those copies.
WAD=`/bin/busybox cat /dev/wadname 2>/dev/null`
if [ -n "$WAD" ]; then
	if [ "$WAD" != doom1.wad ]; then
		/bin/busybox rm -f doom1.wad
	fi
	/bin/busybox ln -sf /dev/wad "$WAD"
	echo "lunatix: playing host WAD as $WAD" > /dev/console
fi
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
  # MMU links kernel text at page 2 (0x2000): page 0 = I/O/boot, page 1 = register file
  # (REG_BASE=1024), kernel text above. Must match the packer's --reg-base 1024 text-start
  # (0x2000). lunatix-mmu's Makefile defaults to 0x1000 (the NOMMU value), so override it.
  [ -n "$MMU" ] && K+=(SUBLEQ_TEXT_START=0x2000)
  # A from-scratch build MUST start clean: this tree may have just been switched from
  # another branch (e.g. via STEP 0), and an incremental make would link stale objects
  # into a Frankenstein kernel that halts at boot. mrproper wipes all generated state.
  run make -C "$ROOT/linux" "${K[@]}" mrproper
  # mrproper also removes the runtime libs copied in by STEP 4; restore them.
  run "$ROOT/runtime/build_and_install_runtime.sh"
  run make -C "$ROOT/linux" "${K[@]}" defconfig
  # MMU arch: lunatix-mmu's defconfig is actually NOMMU, so force CONFIG_MMU=y (+ olddefconfig
  # to pull in the paging/uaccess deps) for the mmu build.
  if [ -n "$MMU" ]; then
    run "$ROOT/linux/scripts/config" --file "$ROOT/linux/.config" --enable CONFIG_MMU
    # The GOOD MMU image packs the STATIC mmu_initramfs.txt cpio spec (busybox+doom+sndtest+wad,
    # /init->/doom), NOT the dynamic initramfs_root dir that defconfig/NOMMU uses. Packing the
    # dynamic dir into an MMU kernel is exactly the 118MB/2-color no-render failure.
    run "$ROOT/linux/scripts/config" --file "$ROOT/linux/.config" \
        --set-str CONFIG_INITRAMFS_SOURCE "../mmu_initramfs.txt"
    run make -C "$ROOT/linux" "${K[@]}" olddefconfig
  fi
  # A blocked task must say so. Without this a child that never runs looks exactly like a child
  # that is merely slow -- which is how "udoom just hangs" was indistinguishable from "udoom is
  # loading a 12 MB WAD". 30 s is short enough to be useful and long enough not to fire on the
  # genuinely slow startup this machine has.
  if [ -n "$MMU" ]; then
    run "$ROOT/linux/scripts/config" --file "$ROOT/linux/.config" \
        --enable CONFIG_DETECT_HUNG_TASK --set-val CONFIG_DEFAULT_HUNG_TASK_TIMEOUT 30
    run make -C "$ROOT/linux" "${K[@]}" olddefconfig
  fi

  # Keep the boot console alive alongside tty0. Without keep_bootcon the kernel hands the
  # console to the framebuffer and stdout goes dark, so anything that happens afterwards --
  # userspace output, an exec failure, a panic -- is only visible as pixels. That cost real
  # time: a guest that halted seconds after boot looked like silence, and the panic was
  # sitting on the framebuffer where nothing could read it. console=tty0 (not ttyS0) is
  # deliberate: the guest keyboard driver picks its mode from the command line, and a ttyS
  # console switches it to serial and stops keys reaching the game.
  if [ -z "$DEBUG_CONSOLE" ] && [ -n "$MMU" ]; then
    run "$ROOT/linux/scripts/config" --file "$ROOT/linux/.config" \
        --set-str CONFIG_CMDLINE "console=tty0 keep_bootcon loglevel=7"
    run make -C "$ROOT/linux" "${K[@]}" olddefconfig
  fi
  # DEBUG_CONSOLE=1: route the console to ttyS0 (which writes via __subleq_putchar -> host
  # stdout) and keep the boot console, so kernel AND userspace output are visible headless
  # (default console=tty0 goes to the framebuffer, hiding userspace + any exec/panic).
  if [ -n "$DEBUG_CONSOLE" ]; then
    run "$ROOT/linux/scripts/config" --file "$ROOT/linux/.config" \
        --set-str CONFIG_CMDLINE "console=ttyS0 keep_bootcon loglevel=8"
    run make -C "$ROOT/linux" "${K[@]}" olddefconfig
  fi
  run make -C "$ROOT/linux" "${K[@]}" -j"$(nproc)"
  # Do not carry on past a broken kernel: step 8 would fail to find vmlinux, and the DONE line
  # below plus an ls of the PREVIOUS image reads exactly like a successful build.
  [ -f "$ROOT/linux/vmlinux" ] || { echo "=== STEP 7 FAILED: no linux/vmlinux ==="; exit 1; }
fi

# ---- step 8: boot image (REG_BASE=0 for cable-nommu)
if [ "$FROM" -le 8 ]; then
  log "STEP 8  boot image (--reg-base $REGBASE)"
  run python3 "$ROOT/tools/make_boot_image.py" --reg-base "$REGBASE" \
      --stack-size 536870912 "$ROOT/linux/vmlinux"
  ls -la "$ROOT/linux/vmlinux.subleq"
  [ "$ROOT/linux/vmlinux.subleq" -nt "$ROOT/linux/vmlinux" ] || {
      echo "=== STEP 8 FAILED: boot image older than vmlinux (stale) ==="; exit 1; }
fi

log "BUILD-ARCH DONE $ARCH -> $ROOT/linux/vmlinux.subleq"
