#!/usr/bin/env bash
# Cross-build SDL 1.2 for the lunatix MMU guest.
#
# SDL 1.2 is the last SDL with a framebuffer video driver (fbcon, straight onto /dev/fb0) and it
# speaks OSS audio to /dev/dsp -- which is exactly the hardware this guest has. SDL2 dropped
# fbcon and wants KMSDRM or a window system, neither of which exists here. So: 1.2 first, as the
# platform's SDL, and anything SDL-based (ScummVM among them) becomes a porting job rather than
# a from-scratch backend.
#
# Everything is disabled except fbcon video. Static only: the MMU userspace is static ET_EXEC.
#
# --disable-threads is not a preference: uClibc's linuxthreads faults on this port even for
# pthread_mutex_lock() on a static PTHREAD_MUTEX_INITIALIZER (see init/pthreadtest.c), and with
# threads enabled SDL_Init dies inside __pthread_lock. Audio goes with them -- SDL 1.2 feeds the
# device from a thread -- so a thread-free SDL gives graphics and input but no sound. Turning
# threads back on is a libc job, not an SDL one.
set -o pipefail
ROOT="$HOME/git/eternal"
SRC="$HOME/git/sdl12/SDL-1.2.15"
SYS="$ROOT/runtime/sysroot-mmu"
TC="$ROOT/llvm-project/build"
KHDR="$SYS/kernel-headers/include"

CC="$TC/bin/clang"
CFLAGS="--sysroot=$SYS -isystem $KHDR -O2 -fno-strict-aliasing"

cd "$SRC" || exit 1

# autoconf has never heard of subleq, and config.sub rejects the triple outright. Answer for it
# before it starts guessing -- lying about the architecture (say, arm-linux) would switch on
# assembly paths that cannot exist here.
for f in build-scripts/config.sub; do
  grep -q 'subleq' "$f" || {
    python3 - "$f" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
marker = "# Split fields of configuration type\n"
inject = """# lunatix: teach config.sub the subleq triple (see build_sdl12.sh)
case $1 in
  subleq | subleq-* )
    echo subleq-unknown-linux-gnu
    exit ;;
esac

"""
if marker in s:
    s = s.replace(marker, inject + marker, 1)
else:
    lines = s.split("\n")
    for i, l in enumerate(lines):
        if l.startswith("me=") or l.startswith("timestamp="):
            lines.insert(i + 1, inject)
            break
    s = "\n".join(lines)
open(p, "w").write(s)
print("config.sub taught about subleq")
PYEOF
  }
done

CC="$CC" CFLAGS="$CFLAGS" LDFLAGS="--sysroot=$SYS" \
./configure \
  --host=subleq-unknown-linux-gnu \
  --prefix="$SYS" \
  --disable-shared --enable-static \
  --enable-video-fbcon \
  --disable-video-x11 --disable-video-dga --disable-video-directfb --disable-video-fbcon-hw \
  --disable-video-svga --disable-video-vgl --disable-video-aalib --disable-video-caca \
  --disable-video-opengl --disable-video-photon --disable-video-ps3 --disable-video-dummy \
  --disable-esd --disable-arts --disable-nas --disable-pulseaudio \
  --disable-threads \
  --disable-alsa --disable-esd --disable-arts --disable-nas --disable-pulseaudio \
  --disable-diskaudio --disable-dummyaudio \
  --disable-joystick --disable-cdrom --disable-nasm --disable-assembly --disable-altivec \
  --disable-sdl-dlopen --disable-input-tslib --disable-mintaudio \
  2>&1 | tail -25

echo "=== configure status: $? ==="
make -j"$(nproc)" 2>&1 | tail -25
echo "=== make status: $? ==="
ls -l build/.libs/libSDL.a 2>/dev/null || ls -l build/libSDL.la 2>/dev/null || echo "no library yet"
