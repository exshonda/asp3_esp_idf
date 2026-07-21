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

R=$HOME/TOPPERS/ASP3CORE/asp3_esp_idf
export IDF_PATH=$R/esp-idf
SRC=$IDF_PATH/examples/bluetooth/nimble/bleprph
DST=$R/tmp/stock_bleprph

rm -rf "$DST"
cp -r "$SRC" "$DST"
cd "$DST"

# prove the copy is verbatim before we touch it
diff -r "$SRC" "$DST" && echo "OK: copy is byte-identical to the submodule example"

# ---- deltas: 3 NEW files; NOT ONE stock file is edited except main.c:617 ----
cat > sdkconfig.ctl <<'EOF'
# (1) REQUIRED: stock default n -> example would neither bond nor expose an
#     encryption-required characteristic = would not test the thing at all.
CONFIG_EXAMPLE_BONDING=y
CONFIG_EXAMPLE_ENCRYPTION=y
# (2) REQUIRED: legacy adv instead of stock extended adv. Measured reasons:
#     a. the only scanner on this bench (hci0) is HCI 4.2 -> physically cannot
#        see extended advertising -> DUT unverifiable, C0 uninterpretable.
#     b. our ASP3 apps advertise legacy -> removes an air-interface confound.
CONFIG_EXAMPLE_EXTENDED_ADV=n
CONFIG_BT_NIMBLE_EXT_ADV=n
EOF
echo 'CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="IDFCTL-C6"' > sdkconfig.ctl.esp32c6
echo 'CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="IDFCTL-C5"' > sdkconfig.ctl.esp32c5

# ---- the ONLY source edit (declared): stock main.c:617 hardcodes
# ble_svc_gap_device_name_set("nimble-bleprph"), which OVERRIDES the Kconfig
# option above. Another project on this bench runs ESP-IDF BLE on ESP32/S3
# (measured on air: FMP-ESP32-BLE / FMP-ESP32S3-BLE) -> a colliding name could
# make us attribute THEIR board's behaviour to C5/C6. Make stock honour its own
# Kconfig option. Same call, same API, different string.
python3 - <<'PY'
p="main/main.c"; s=open(p).read()
old='    rc = ble_svc_gap_device_name_set("nimble-bleprph");'
new='    rc = ble_svc_gap_device_name_set(CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME);'
assert s.count(old)==1
open(p,"w").write(s.replace(old,new))
PY

echo "=== delta vs stock example (expect: main.c + 3 new sdkconfig.ctl* files) ==="
diff -r "$SRC" "$DST" || true

# shellcheck disable=SC1091
. "$IDF_PATH/export.sh" >/dev/null 2>&1

# NOTE: -B alone does NOT give each target its own sdkconfig -- the project-level
# sdkconfig is shared and the second set-target silently overwrites the first.
# SDKCONFIG=<per-target file> is REQUIRED. (This bit me; see evidence 2.3.)
for t in esp32c6 esp32c5; do
    idf.py -B "build_$t" -D SDKCONFIG="sdkconfig.$t" \
           -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ctl" set-target "$t"
    idf.py -B "build_$t" -D SDKCONFIG="sdkconfig.$t" \
           -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ctl" build
done

echo
echo "=== verify the deltas reached the OUTPUT, not just the cache ==="
NM=$HOME/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin/riscv32-esp-elf-nm
for t in esp32c6 esp32c5; do
    echo "$t: legacy_adv=$($NM build_$t/bleprph.elf | grep -cE ' T ble_gap_adv_start$')" \
         "ext_adv=$($NM build_$t/bleprph.elf | grep -cE ' T ble_gap_ext_adv_start$')" \
         "colliding_name=$(strings build_$t/bleprph.bin | grep -c nimble-bleprph)" \
         "name=$(strings build_$t/bleprph.bin | grep -oE 'IDFCTL-C[56]' | head -1)"
done

echo "=== toolchain actually used (measured, not assumed) ==="
for t in esp32c6 esp32c5; do
    grep -m1 -oE "$HOME/\.espressif/tools/riscv32-esp-elf/[^ ]*riscv32-esp-elf-gcc" "build_$t/build.ninja"
    # NB: -march is NOT in build.ninja; IDF v5.5.4 indirects flags via @response file
    grep -oE '\-march=[a-z0-9_]+' "build_$t/toolchain/cflags"
done
