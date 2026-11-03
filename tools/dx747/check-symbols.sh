#!/bin/bash
# Compile every source the simulator build uses natively and report global symbols
# that are defined twice or used but never defined: an early warning for link errors
# in the WASM build (and, for app/ files, in the firmware). Usage: check-symbols.sh [outdir]
cd "$(dirname "$0")/../.." || exit 1
out="${1:-${TMPDIR:-/tmp}/dx747-symbols}"
rm -rf "$out"; mkdir -p "$out"
FLAGS=(-std=c11 -c -w -DFM1_HOST_TEST -DFM1_OPEN_WEB -Iweb/engine -Iweb/synth -Ithird_party/synth -Isdk/include -Iapp/synth -Iapp)
n=0
for f in $(find app web third_party/synth -name '*.c' -not -path 'app/board/*'); do
    clang "${FLAGS[@]}" "$f" -o "$out/$(echo "$f" | tr / _).o" 2>"$out/err.txt" || { echo "compile failed: $f"; head -5 "$out/err.txt"; }
    n=$((n+1))
done
echo "compiled $n files"
nm -g --defined-only "$out"/*.o 2>/dev/null | awk 'NF==3 && $2 ~ /[TDBRS]/ {print $3}' | sort | uniq -d > "$out/dup.txt"
nm -g --defined-only "$out"/*.o 2>/dev/null | awk 'NF==3 && $2 ~ /[TDBRSC]/ {print $3}' | sort -u > "$out/defined.txt"
nm -u "$out"/*.o 2>/dev/null | awk 'NF==1 && $1 !~ /:$/ {print $1}' | sort -u > "$out/undef.txt"
echo "defined more than once:"; cat "$out/dup.txt"
echo "used but not defined (libc and platform symbols excluded):"
comm -13 "$out/defined.txt" "$out/undef.txt" | grep -E '^_?(groove|fm1|dx|mfx|sim|synth|dsp|fixed|dx7|sea|psg|pluck|drum|bass|sample)' | sed 's/^_//'
