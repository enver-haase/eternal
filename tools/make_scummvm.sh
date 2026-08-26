#!/usr/bin/env bash
# Build ScummVM for the lunatix guest -- with the one object that cannot be optimised.
#
# image/codecs/cinepak.cpp does not finish compiling on this backend at -O1 or above. The function
# is Image::CinepakDecoder::decodeVectors8, which inlines out to roughly 2500 basic blocks, and the
# machine passes blow up on it: measured 0.54 SECONDS at -O0 against more than ten minutes at -O1
# and at -O2, with -fno-optimize-sibling-calls making no difference. A `make clean` therefore
# appears to hang for half an hour, which is how an earlier session came to hand-build this object
# and (by using a hand-written command line) gave it different -D flags from the rest of the tree --
# an ODR mismatch that produced garbage in an unrelated member pointer.
#
# So: build that one object at -O0 with the REAL command line (taken from make itself, with only
# the -O level changed), then let make do everything else. ScummVM does not use Cinepak for the ADL
# engine; it is built because image/codecs is unconditional.
set -o pipefail
SRC="${SCUMMVM_SRC:-$HOME/git/scummvm}"
cd "$SRC" || exit 1

OBJ=image/codecs/cinepak.o
if [ ! -f "$OBJ" ]; then
  echo "=== $OBJ at -O0 (see the comment in this script)"
  CMD=$(make VERBOSE_BUILD=1 -n "$OBJ" 2>/dev/null | grep -E "clang\+\+|c\+\+" | tail -1)
  if [ -z "$CMD" ]; then echo "cannot get the compile command for $OBJ"; exit 1; fi
  echo "${CMD//-O2/-O0}" > /tmp/cinepak_O0.sh
  bash /tmp/cinepak_O0.sh || exit 1
fi

make -j"$(nproc)" "$@"
