#!/usr/bin/env bash
# Build the RAM rescue image from this checkout.  No device access occurs here.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
profile=${1:?usage: scripts/build-rescue.sh PROFILE.json [OUTPUT_DIR]}
output=${2:-"$root/out"}
cross=${CROSS_COMPILE:-arm-linux-gnueabi-}

case "$profile" in /*) ;; *) profile="$PWD/$profile";; esac
case "$output" in /*) ;; *) output="$PWD/$output";; esac
mkdir -p "$output"
cd "$root"
generated="$root/include/configs/qua_trial_values.h"
if [ -e "$generated" ]; then
  printf 'refusing to replace existing generated header: %s\n' "$generated" >&2
  exit 1
fi
trap 'rm -f "$generated"' EXIT INT TERM

"${cross}as" -march=armv8-a -o "$output/chain-to-arm64.o" "$root/scripts/chain-to-arm64.S"
"${cross}ld" -Ttext=0 -e _start -o "$output/chain-to-arm64.elf" "$output/chain-to-arm64.o"
"${cross}objcopy" -O binary "$output/chain-to-arm64.elf" "$output/chain-to-arm64.bin"
"${cross}as" -march=armv8-a -o "$output/chain-copy-arm32.o" "$root/scripts/chain-copy-arm32.S"
"${cross}ld" -Ttext=0 -e _start -o "$output/chain-copy-arm32.elf" "$output/chain-copy-arm32.o"
"${cross}objcopy" -O binary "$output/chain-copy-arm32.elf" "$output/chain-copy-arm32.bin"
python3 "$root/scripts/generate-chain-values.py" "$profile" "$output/chain-to-arm64.bin" "$generated"
mkdir -p "$output/include/configs"
cp "$generated" "$output/include/configs/qua_trial_values.h"
make O="$output/build" CROSS_COMPILE="$cross" quastation_ram_chain_config
make O="$output/build" CROSS_COMPILE="$cross" -j"${JOBS:-2}" "$output/build/u-boot.bin"
python3 "$root/scripts/package-ram-console.py" \
  "$output/build/u-boot" "$output/build/u-boot.bin" "$output/chain-copy-arm32.bin" \
  "$output/package" --staging-address 0x25000000 --chain-plan "$profile"

printf 'Built package: %s\n' "$output/package"
