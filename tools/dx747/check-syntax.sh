#!/bin/bash
# Syntax-check app sources natively with the simulator build's flags (the only
# compiler available without the JieLi toolchain). Usage: check-syntax.sh [files...]
cd "$(dirname "$0")/../.." || exit 1
FLAGS=(-std=c11 -fsyntax-only -DFM1_HOST_TEST -DFM1_OPEN_WEB -Wall -Wextra -Werror -Wno-unused-function -Wno-array-parameter
       -Iapp -Iapp/synth -Isdk/include -Iweb/engine -Iweb/synth -Ithird_party/synth)
files=("$@")
[ ${#files[@]} -eq 0 ] && files=(app/*.c)
status=0
for f in "${files[@]}"; do
    if clang "${FLAGS[@]}" "$f" 2>&1 | head -20 | grep -q .; then echo "FAIL $f"; clang "${FLAGS[@]}" "$f" 2>&1 | head -20; status=1; else echo "ok   $f"; fi
done
exit $status
