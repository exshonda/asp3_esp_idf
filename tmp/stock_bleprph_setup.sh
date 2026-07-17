#!/usr/bin/env bash
# Rebuild the stock ESP-IDF bleprph control experiment from scratch.
#
# Purpose: this is the CONTROL arm for the "C5 x Android BLE bond fails" hunt.
# It takes the entire ASP3 port (kernel, shim/OSAL, direct boot, our cmake and
# our NimBLE config) OUT of the variable set by using Espressif's own example,
# Espressif's own FreeRTOS startup, and Espressif's own idf.py build.
#
# Supply  : the repo submodule esp-idf/ @ v5.5.4 (735507283d) -- NOT ~/tools/esp-idf
#           (which is really v5.5.0) and NOT esp-idf-v6.1.
# Toolchain: whatever idf.py picks by itself (measured: esp-14.2.0_20260121).
#
# Delta from the stock example: sdkconfig.defaults gains exactly two options,
# both of which are stock Kconfig options OF THE EXAMPLE ITSELF
# (main/Kconfig.projbuild). No asp3 config is imported.
#   CONFIG_EXAMPLE_BONDING=y     -> ble_hs_cfg.sm_bonding = 1
#   CONFIG_EXAMPLE_ENCRYPTION=y  -> adds READ_ENC/WRITE_ENC to the custom chr
# Without these the example neither bonds nor exposes an encryption-required
# characteristic, i.e. it would not exercise the thing under test at all.
# Everything else is left stock -- notably SC=n, MITM=n, IO_CAP=NO_IO,
# key dist = ENC only, EXT_ADV=y (see the evidence file for why these matter).
set -euo pipefail

R=/home/honda/TOPPERS/ASP3CORE/asp3_esp_idf
export IDF_PATH=$R/esp-idf
SRC=$IDF_PATH/examples/bluetooth/nimble/bleprph
DST=$R/tmp/stock_bleprph

rm -rf "$DST"
cp -r "$SRC" "$DST"
cd "$DST"

# prove the copy is verbatim before we touch it
diff -r "$SRC" "$DST" && echo "OK: copy is byte-identical to the submodule example"

cat >> sdkconfig.defaults <<'EOF'

#
# ---- ONLY delta from the stock example (asp3 control experiment) ----
# Stock defaults are BONDING=n / ENCRYPTION=n, which would make the example
# neither bond nor expose an encryption-required characteristic -- i.e. it
# would not exercise the thing under test at all. Both are stock Kconfig
# options of the example itself (main/Kconfig.projbuild); no asp3 config is
# imported. Everything else is left at the example's stock defaults
# (notably: SC=n, MITM=n, IO_CAP=NO_IO, key dist = ENC only).
#
CONFIG_EXAMPLE_BONDING=y
CONFIG_EXAMPLE_ENCRYPTION=y
EOF

echo "=== delta vs stock example (must be sdkconfig.defaults only) ==="
diff -r "$SRC" "$DST" || true

# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" >/dev/null 2>&1

# NOTE: -B alone does NOT give each target its own sdkconfig -- the project-level
# sdkconfig is shared and the second set-target silently overwrites the first.
# SDKCONFIG=<per-target file> is REQUIRED. (This bit me; see evidence 2.3.)
for t in esp32c6 esp32c5; do
    idf.py -B "build_$t" -D SDKCONFIG="sdkconfig.$t" set-target "$t"
    idf.py -B "build_$t" -D SDKCONFIG="sdkconfig.$t" build
done

echo
echo "=== toolchain actually used (measured, not assumed) ==="
for t in esp32c6 esp32c5; do
    grep -m1 -oE "/home/honda/\.espressif/tools/riscv32-esp-elf/[^ ]*riscv32-esp-elf-gcc" "build_$t/build.ninja"
    # NB: -march is NOT in build.ninja; IDF v5.5.4 indirects flags via @response file
    grep -oE '\-march=[a-z0-9_]+' "build_$t/toolchain/cflags"
done
