#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
#
# Build dx747-sim when it is missing or any source is newer than it. `make` alone is not enough: the macOS
# make compares times at one-second resolution and treats a file edited within a second of a finished
# build as unchanged, which can leave a stale simulator behind. `find -newer` compares exact times.
#   tools/dx747/sim/native/ensure-built.sh
cd "$(dirname "$0")/../../../.." || exit 1
bin=tools/dx747/sim/native/dx747-sim
if [ ! -x "$bin" ] || [ -n "$(find app web third_party/synth sdk/include tools/dx747/sim/native/harness.c tools/dx747/sim/native/Makefile \
        \( -name '*.c' -o -name '*.h' -o -name '*.inc' -o -name Makefile \) -newer "$bin" -print | head -1)" ]; then
    make -B -s -C tools/dx747/sim/native || exit 1
fi
