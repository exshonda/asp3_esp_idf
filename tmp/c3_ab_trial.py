#!/usr/bin/env python3
"""c3_ab_trial.py — C3 の BLE ペアリング試行を «壊さずに» 連続観測し、A/B を自動分類する。

目的（evidence-06 §6.4 の未確定1）:
  Android 相手のペアリングは同一条件でも経過が分岐する（A=bond成立／B=要求来ず／
  C=要求来るが不成立）。**`esp_shim: pend path engaged` が失敗の弁別指標か**を
  判定するには、成功・失敗それぞれの試行でこの行の有無を突き合わせる必要がある。
  1 回の試行ごとに人手を挟むと回数が稼げないので、**シリアルを読み続けたまま
  試行を自動で切り出して分類**する。

★観測が対象を壊さないための鉄則（evidence-06 §6.3 で 4 件の事故を踏まえた）:
  - **DTR/RTS を一切触らない**（触るとリセットが入り、観測したい接続を殺す）。
  - **接続を始める «前» から読み続ける**（後から開くとリセット前のログが失われる）。
  - 途中で開き直さない。

使い方:
  c3_ab_trial.py <port> [秒数]      # 既定 600 秒
  実行中にスマホで「接続→ペアリング承認→切断」を繰り返すだけでよい。
  各試行の分類と、判定に使う行の有無を逐次表示する。

分類:
  A = PAIRING_COMPLETE status=0 を観測（bond 成立）
  B = CONNECT はあるが PAIRING_COMPLETE が出ないまま ENC_CHANGE status=13
  C = PAIRING_COMPLETE が status!=0
  ?  = 上記に当てはまらない（切断のみ等）
"""
import re
import sys
import time

import serial

RE_CONNECT = re.compile(r"GAP CONNECT status=(\d+)")
RE_PEND = re.compile(r"pend path engaged \(dtqid=(\d+)\)")
RE_SECINIT = re.compile(r"security_initiate\(slave SecReq\) rc=(-?\d+)")
RE_PC = re.compile(r"GAP PAIRING_COMPLETE status=(-?\d+) bonds our=(\d+) peer=(\d+)")
RE_PCSEC = re.compile(r"PC sec_state enc=(\d+) auth=(\d+) bond=(\d+) keysz=(\d+)")
RE_ENC = re.compile(r"GAP ENC_CHANGE status=(-?\d+)")
RE_SEC = re.compile(r"sec_state enc=(\d+) auth=(\d+) bond=(\d+) keysz=(\d+)")
RE_DISC = re.compile(r"GAP DISCONNECT|DISCONNECT")


class Trial:
    def __init__(self, n, t0):
        self.n = n
        self.t0 = t0
        self.pend = False
        self.secinit = None
        self.pc = None
        self.pcsec = None
        self.enc = None
        self.sec = None

    def classify(self):
        if self.pc is not None and self.pc[0] == 0:
            return "A（bond 成立）"
        if self.pc is not None:
            return f"C（PAIRING_COMPLETE status={self.pc[0]}）"
        if self.enc is not None:
            return f"B（要求来ず・ENC_CHANGE status={self.enc}）"
        return "?（判定不能）"

    def report(self):
        print(f"\n--- 試行 #{self.n}  (+{self.t0:.0f}s) ---")
        print(f"  分類            : {self.classify()}")
        print(f"  ★pend path      : {'あり' if self.pend else 'なし'}")
        print(f"  security_initiate: rc={self.secinit}")
        if self.pc:
            print(f"  PAIRING_COMPLETE : status={self.pc[0]} our={self.pc[1]} peer={self.pc[2]}")
        if self.pcsec:
            print(f"  ★PC sec_state    : enc={self.pcsec[0]} bond={self.pcsec[2]} keysz={self.pcsec[3]}")
        if self.enc is not None:
            print(f"  ENC_CHANGE       : status={self.enc}")
        if self.sec:
            print(f"  最終 sec_state   : enc={self.sec[0]} bond={self.sec[2]}")
        sys.stdout.flush()


def main():
    port = sys.argv[1]
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 600.0

    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.3
    #  ★ここが肝：開く前に DTR/RTS を落としてリセットを誘発しない
    s.dtr = False
    s.rts = False
    s.open()

    print(f"読み出し開始（{secs:.0f}秒）。スマホで «接続→承認→切断» を繰り返してください。")
    sys.stdout.flush()

    t0 = time.time()
    buf = ""
    trials = []
    cur = None
    raw = []

    while time.time() - t0 < secs:
        d = s.read(4096)
        if not d:
            continue
        raw.append(d)
        buf += d.decode("utf-8", "replace")
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            line = line.strip()
            if not line:
                continue

            if RE_CONNECT.search(line):
                if cur is not None:
                    cur.report()
                    trials.append(cur)
                cur = Trial(len(trials) + 1, time.time() - t0)
                continue
            if cur is None:
                continue

            m = RE_PEND.search(line)
            if m:
                cur.pend = True
            m = RE_SECINIT.search(line)
            if m:
                cur.secinit = int(m.group(1))
            m = RE_PC.search(line)
            if m:
                cur.pc = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
            m = RE_PCSEC.search(line)
            if m:
                cur.pcsec = tuple(int(g) for g in m.groups())
            m = RE_ENC.search(line)
            if m:
                cur.enc = int(m.group(1))
                #  ENC_CHANGE まで来たら 1 試行の区切りとみなす
            m = RE_SEC.search(line)
            if m and "PC sec_state" not in line:
                cur.sec = tuple(int(g) for g in m.groups())
                cur.report()
                trials.append(cur)
                cur = None

    if cur is not None:
        cur.report()
        trials.append(cur)
    s.close()

    with open("/tmp/c3_ab_raw.log", "wb") as f:
        f.write(b"".join(raw))

    #  ★A/B 対照の集計＝「pend path engaged は失敗の弁別指標か」
    print("\n" + "=" * 60)
    print("=== 集計（pend path engaged は弁別指標か） ===")
    if not trials:
        print("  試行が観測されませんでした。")
        return
    tab = {}
    for t in trials:
        key = (t.classify().split("（")[0], t.pend)
        tab[key] = tab.get(key, 0) + 1
    print(f"  {'分類':6} {'pendあり':>10} {'pendなし':>10}")
    for cls in ("A", "B", "C", "?"):
        y = tab.get((cls, True), 0)
        n = tab.get((cls, False), 0)
        if y or n:
            print(f"  {cls:6} {y:>10} {n:>10}")
    print(f"  総試行: {len(trials)}")
    print("  ★判定: 成功(A)と失敗(B/C)で pend の有無が分かれれば «弁別指標»、")
    print("          どちらにも出る／どちらにも出ないなら «無関係» と切り分けられる。")
    print(f"  生ログ: /tmp/c3_ab_raw.log")


if __name__ == "__main__":
    main()
