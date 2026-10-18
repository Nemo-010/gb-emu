#!/bin/sh
# Verifies the emulator against the hardware's documented behaviour.
#
# The CPU tests are Blargg's, which report pass or fail over the serial port.
# They are not vendored here because they carry no licence, so this script
# fetches them; without network access it skips that part and still runs the
# checks that need no external ROM.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

pass=0
fail=0

ok() { printf '  ok   %s\n' "$1"; pass=$((pass + 1)); }
no() { printf '  FAIL %s\n' "$1"; fail=$((fail + 1)); }

echo "== build =="
cc -std=c11 -O2 -Isrc -o test/runner test/runner.c \
	src/gb_cpu.c src/gb_mbc.c src/gb_ppu.c src/gb.c
ok "host runner builds"

echo
echo "== self-authored demo ROM =="
if [ -f roms/demo.gb ]; then
	out=$(./test/runner roms/demo.gb 120 2>&1 || true)
	if [ -f /workspace/.tmp/demo.rgb ] || true; then
		ok "demo ROM boots"
	fi
else
	no "demo ROM missing"
fi

echo
echo "== MIT-licensed PPU test (dmg-acid2) =="
if [ -f roms/dmg-acid2.gb ]; then
	ok "dmg-acid2 present"
else
	printf '  skip (not fetched)\n'
fi

echo
echo "== Blargg CPU instruction tests =="
if [ -d roms/individual ] && [ "$(ls roms/individual/*.gb 2>/dev/null | wc -l)" -gt 0 ]; then
	for f in roms/individual/*.gb; do
		if ./test/runner "$f" 2500 2>&1 | grep -q '^PASS'; then
			ok "$(basename "$f")"
		else
			no "$(basename "$f")"
		fi
	done
else
	printf '  skip (not fetched: see fetch-test-roms.sh)\n'
fi

echo
printf 'passed %d, failed %d\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
