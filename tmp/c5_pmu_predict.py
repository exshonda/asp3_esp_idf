#!/usr/bin/env python3
"""Compute the exact register values that stock pmu_init() will write on C5.

Bit layout taken from esp-idf/components/soc/esp32c5/register/soc/pmu_struct.h.
Model is VALIDATED against r35's independently measured stock values
(target_kernel_impl.c ESP32C5_R35_FIXED_*) before being used for prediction.
"""


def pack(fields):
    """fields = [(lsb, width, value)] -> word"""
    v = 0
    for lsb, w, val in fields:
        assert val < (1 << w), (lsb, w, val)
        v |= (val & ((1 << w) - 1)) << lsb
    return v


# ---- clock source enum values (soc/clk_tree_defs.h) ----
# PMU_CLK_SRC_VAL(): XTAL->0, RC_FAST->1, PLL_F160M->2, PLL_F240M->3, else 0
XTAL, PLL160 = 0, 2


def dig_power(vdd_spi_pd_en=0, mem_dslp=0, mem_pd_en=0, wifi_pd_en=0,
              cpu_pd_en=0, aon_pd_en=0, top_pd_en=0):
    # reserved0:21, vdd_spi_pd_en:1(21), mem_dslp:1(22), mem_pd_en:4(23),
    # wifi_pd_en:1(27), reserved1:1(28), cpu_pd_en:1(29), aon_pd_en:1(30), top_pd_en:1(31)
    return pack([(21, 1, vdd_spi_pd_en), (22, 1, mem_dslp), (23, 4, mem_pd_en),
                 (27, 1, wifi_pd_en), (29, 1, cpu_pd_en), (30, 1, aon_pd_en),
                 (31, 1, top_pd_en)])


def clk_power(i2c_iso_en=0, i2c_retention=0, xpd_bb_i2c=0, xpd_bbpll_i2c=0, xpd_bbpll=0):
    # reserved0:26, i2c_iso_en(26), i2c_retention(27), xpd_bb_i2c(28),
    # xpd_bbpll_i2c(29), xpd_bbpll(30), reserved1(31)
    return pack([(26, 1, i2c_iso_en), (27, 1, i2c_retention), (28, 1, xpd_bb_i2c),
                 (29, 1, xpd_bbpll_i2c), (30, 1, xpd_bbpll)])


def sysclk(dig_sysclk_nodiv=0, icg_sysclk_en=0, sysclk_slp_sel=0, icg_slp_sel=0, dig_sysclk_sel=0):
    # reserved0:26, nodiv(26), icg_sysclk_en(27), sysclk_slp_sel(28), icg_slp_sel(29), sel:2(30)
    return pack([(26, 1, dig_sysclk_nodiv), (27, 1, icg_sysclk_en), (28, 1, sysclk_slp_sel),
                 (29, 1, icg_slp_sel), (30, 2, dig_sysclk_sel)])


def regdma(d, e):
    return ((d << 4) | (e & 0xF)) & 0x1F


def backup_active(s2a_code, m2a_code, ret_mode, s2a_ret_en, m2a_ret_en,
                  s2a_clk_sel, m2a_clk_sel, s2a_mode, m2a_mode, s2a_en, m2a_en):
    return pack([(4, 2, s2a_code), (6, 2, m2a_code), (10, 1, ret_mode), (11, 1, s2a_ret_en),
                 (12, 1, m2a_ret_en), (14, 2, s2a_clk_sel), (16, 2, m2a_clk_sel),
                 (18, 5, s2a_mode), (23, 5, m2a_mode), (29, 1, s2a_en), (30, 1, m2a_en)])


def backup_modem(s2m_code, ret_mode, s2m_ret_en, s2m_clk_sel, s2m_mode, s2m_en):
    return pack([(4, 2, s2m_code), (10, 1, ret_mode), (11, 1, s2m_ret_en),
                 (14, 2, s2m_clk_sel), (20, 5, s2m_mode), (29, 1, s2m_en)])


def backup_sleep(m2s_code, a2s_code, ret_mode, m2s_ret_en, a2s_ret_en,
                 m2s_clk_sel, a2s_clk_sel, m2s_mode, a2s_mode, m2s_en, a2s_en):
    return pack([(6, 2, m2s_code), (8, 2, a2s_code), (10, 1, ret_mode), (12, 1, m2s_ret_en),
                 (13, 1, a2s_ret_en), (16, 2, m2s_clk_sel), (18, 2, a2s_clk_sel),
                 (20, 5, m2s_mode), (25, 5, a2s_mode), (30, 1, m2s_en), (31, 1, a2s_en)])


# ================= VALIDATION against r35's measured stock values =================
print("=== model validation vs r35 independently-measured stock values ===")
checks = [
    ("HP_MODEM backup   (0x600B0050)", backup_modem(1, 0, 0, XTAL, regdma(0, 1), 0), 0x00100010),
    ("HP_SLEEP backup   (0x600B0084)", backup_sleep(0, 2, 0, 0, 0, XTAL, XTAL,
                                                    regdma(1, 1), regdma(1, 0), 0, 0), 0x21100200),
    ("HP_MODEM sysclk   (0x600B0058)", sysclk(0, 1, 1, 1, PLL160), 0xb8000000),
    ("HP_MODEM dig_power(0x600B0034)", dig_power(cpu_pd_en=1), 0x20000000),
    ("HP_MODEM ck_power (0x600B0048)", clk_power(0, 0, 1, 1, 1), 0x70000000),
    ("HP_SLEEP sysclk   (0x600B008C)", sysclk(0, 0, 1, 1, XTAL), 0x30000000),
    ("HP_SLEEP dig_power(0x600B0068)", dig_power(vdd_spi_pd_en=1, wifi_pd_en=1), 0x08200000),
    ("HP_SLEEP ck_power (0x600B007C)", clk_power(1, 1, 1, 0, 0), 0x1c000000),
    ("HP_MODEM icg_modem(0x600B0040)", 1 << 30, 0x40000000),
]
ok = True
for name, got, exp in checks:
    m = "OK " if got == exp else "MISMATCH"
    if got != exp:
        ok = False
    print(f"  [{m}] {name}: model={got:08x} r35_stock={exp:08x}")
print("model validated" if ok else "MODEL WRONG -- do not use")
print()

# ================= PREDICTION for HP_ACTIVE (never measured on stock) =============
print("=== PREDICTION: values pmu_init() writes to the HP_ACTIVE bank ===")
print("(whole-word writes -> exact; bitfield writes -> RMW, marked)")
pred = [
    ("0x600B0000 HP_ACTIVE_DIG_POWER   ", dig_power(), "whole word (.val=)"),
    ("0x600B0004 HP_ACTIVE_ICG_HP_FUNC ", 0xffffffff, "whole word"),
    ("0x600B0008 HP_ACTIVE_ICG_HP_APB  ", 0xffffffff, "whole word"),
    ("0x600B000C HP_ACTIVE_ICG_MODEM   ", 2 << 30, "bitfield code[31:30]=2"),
    ("0x600B0014 HP_ACTIVE_HP_CK_POWER ", clk_power(0, 0, 1, 1, 1), "whole word (.val=) <== RF/analog"),
    ("0x600B001C HP_ACTIVE_BACKUP      ", backup_active(2, 2, 0, 0, 0, XTAL, PLL160,
                                                        regdma(0, 0), regdma(0, 2), 0, 0), "whole word"),
    ("0x600B0020 HP_ACTIVE_BACKUP_CLK  ", 0xffffffff, "whole word"),
    ("0x600B0024 HP_ACTIVE_SYSCLK      ", sysclk(0, 1, 0, 0, XTAL), "bitfields[31:26]"),
]
for name, v, how in pred:
    print(f"  {name} = {v:08x}   ({how})")
print()
print("NOTE hp_modem2active_backup_clk_sel uses SOC_CPU_CLK_SRC_PLL_F160M (a DIFFERENT enum")
print("     than SOC_MOD_CLK_PLL_F160M that PMU_CLK_SRC_VAL compares against) -> value")
print("     depends on the numeric collision; both branches printed below.")
for sel, tag in ((PLL160, "if it maps to 2"), (0, "if it falls through to 0")):
    v = backup_active(2, 2, 0, 0, 0, XTAL, sel, regdma(0, 0), regdma(0, 2), 0, 0)
    print(f"  0x600B001C HP_ACTIVE_BACKUP = {v:08x}  ({tag})")
