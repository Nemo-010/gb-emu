#!/bin/sh
# Fetches the external test ROMs.
#
# They are deliberately not committed: Blargg's suite carries no licence, so it
# is downloaded on demand rather than redistributed. dmg-acid2 is MIT and could
# be vendored, but keeping the rule simple is worth more than the convenience.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/roms/individual"
cd "$ROOT/roms"

BASE=https://raw.githubusercontent.com/retrio/gb-test-roms/master

for t in "01-special" "02-interrupts" "03-op sp,hl" "04-op r,imm" "05-op rp" \
         "06-ld r,r" "07-jr,jp,call,ret,rst" "08-misc instrs" "09-op r,r" \
         "10-bit ops" "11-op a,(hl)"; do
	enc=$(printf '%s' "$t.gb" | sed 's/ /%20/g')
	out="individual/$t.gb"
	[ -s "$out" ] && continue
	printf 'fetching %s\n' "$t"
	curl -sfL -o "$out" "$BASE/cpu_instrs/individual/$enc" || rm -f "$out"
done

[ -s dmg-acid2.gb ] || \
	curl -sfL -o dmg-acid2.gb \
		https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb

echo "done"
