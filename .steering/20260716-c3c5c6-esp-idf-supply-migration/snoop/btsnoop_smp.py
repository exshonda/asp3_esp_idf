#!/usr/bin/env python3
"""btsnoop_smp.py — btsnoop から **SMP PDU 列を全件・opcode 名つき** で抽出する。

なぜ別ファイルか（`btsnoop_verify.py` との関係）:
  既存の `btsnoop_verify.py` も SMP を抽出しているが **`smp[:14]` で先頭14件に
  切っている**ため、ログ後半（＝直近の試験セッション）が見えない。
  既存器は過去の evidence が参照している資産なので**壊さず**、本ファイルを足す。

用途（evidence-05 §7.5 / fable 診断の「測定1」）:
  「**我々（peripheral）が鍵配布 PDU（0x08 Encryption Information /
  0x09 Master Identification / 0x0a Identity Information /
  0x0b Identity Address Information）を «電波に» 出したか**」を一次証拠で確定する。
  DUT 側の計装は「コントローラへ渡した」までしか証明できないため、
  空中を見ている btsnoop が真の一次証拠になる。

使い方:
  btsnoop_smp.py <btsnoop_hci.log> [--since SEC] [--until SEC]
    --since/--until はログ先頭からの相対秒（`btsnoop_verify.py` の表示と同じ基準）。

方向の読み:
  btsnoop の flags bit0 は「controller→host（受信）」。本スクリプトでは
    H->C = 我々のホストが送った（＝**空中に出る方向**）
    C->H = 相手から受け取った
  と表示する（`btsnoop_verify.py` と同じ規約）。
"""
import struct
import sys

SMP_OPS = {
    0x01: "Pairing Request",
    0x02: "Pairing Response",
    0x03: "Pairing Confirm",
    0x04: "Pairing Random",
    0x05: "Pairing Failed",
    0x06: "Encryption Information (LTK)",
    0x07: "Master Identification (EDIV/Rand)",
    0x08: "Identity Information (IRK)",
    0x09: "Identity Address Information",
    0x0a: "Signing Information (CSRK)",
    0x0b: "Security Request",
    0x0c: "Pairing Public Key",
    0x0d: "Pairing DHKey Check",
    0x0e: "Pairing Keypress Notification",
}
#  鍵配布 PDU（bond 成立に必要な «配る» もの）
KEY_DIST_OPS = {0x06, 0x07, 0x08, 0x09, 0x0a}

PAIRING_FAILED_REASON = {
    0x01: "Passkey Entry Failed", 0x02: "OOB Not Available",
    0x03: "Authentication Requirements", 0x04: "Confirm Value Failed",
    0x05: "Pairing Not Supported", 0x06: "Encryption Key Size",
    0x07: "Command Not Supported", 0x08: "Unspecified Reason",
    0x09: "Repeated Attempts", 0x0a: "Invalid Parameters",
    0x0b: "DHKey Check Failed", 0x0c: "Numeric Comparison Failed",
    0x0d: "BR/EDR pairing in progress", 0x0e: "Cross-transport Key Derivation Not Allowed",
}


def le16(b, o):
    return b[o] | (b[o + 1] << 8)


def parse(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"btsnoop\x00":
        raise SystemExit("not a btsnoop file")
    off = 16
    recs = []
    while off + 24 <= len(data):
        olen, ilen, flags, drops, ts = struct.unpack(">IIIIq", data[off:off + 24])
        off += 24
        body = data[off:off + ilen]
        off += ilen
        if len(body) < 1:
            continue
        recs.append((ts, flags, body[0], body[1:]))
    return recs


def main():
    args = [a for a in sys.argv[1:]]
    path = None
    since = None
    until = None
    i = 0
    while i < len(args):
        if args[i] == "--since":
            since = float(args[i + 1]); i += 2
        elif args[i] == "--until":
            until = float(args[i + 1]); i += 2
        else:
            path = args[i]; i += 1
    if not path:
        raise SystemExit(__doc__)

    recs = parse(path)
    base = min(r[0] for r in recs)

    def rel(t):
        return (t - base) / 1e6

    smp = []
    disc = []
    conn = []
    for ts, flags, typ, body in recs:
        t = rel(ts)
        if since is not None and t < since:
            continue
        if until is not None and t > until:
            continue
        if typ == 0x02 and len(body) >= 9:          # ACL
            if le16(body, 6) == 0x0006:             # L2CAP CID 0x0006 = SMP
                payload = body[8:]
                dirn = "C->H" if (flags & 1) else "H->C"
                smp.append((t, payload[0], dirn, payload))
        elif typ == 0x04 and len(body) >= 2:        # HCI Event
            code = body[0]
            if code == 0x05:
                disc.append((t, body[2:].hex()))
            elif code == 0x3e and len(body) >= 3 and body[2] == 0x01:
                conn.append((t, "CONN_COMPLETE"))

    rng = f"（{since if since is not None else 0:.0f}s 〜 {until if until is not None else rel(max(r[0] for r in recs)):.0f}s）"
    print(f"=== SMP PDU 全件 {rng} ===")
    if not smp:
        print("  （SMP PDU は 1 件も無い）")
    for t, op, dirn, payload in smp:
        name = SMP_OPS.get(op, "?")
        extra = ""
        if op == 0x05 and len(payload) >= 2:
            extra = f"  reason=0x{payload[1]:02x} ({PAIRING_FAILED_REASON.get(payload[1], '?')})"
        if op in (0x01, 0x02) and len(payload) >= 4:
            #  io_cap, oob, authreq
            extra = (f"  io_cap=0x{payload[1]:02x} oob=0x{payload[2]:02x} "
                     f"authreq=0x{payload[3]:02x}"
                     f"(bond={payload[3] & 0x03} mitm={(payload[3] >> 2) & 1} sc={(payload[3] >> 3) & 1})")
        if op == 0x0b and len(payload) >= 2:
            extra = (f"  authreq=0x{payload[1]:02x}"
                     f"(bond={payload[1] & 0x03} mitm={(payload[1] >> 2) & 1} sc={(payload[1] >> 3) & 1})")
        mark = " ★鍵配布" if op in KEY_DIST_OPS else ""
        print(f"  +{t:9.3f}s  {dirn}  op=0x{op:02x} {name}{extra}{mark}")

    #  ★測定1 の主目的＝「我々が鍵配布を出したか」の機械判定
    ours = [s for s in smp if s[2] == "H->C" and s[1] in KEY_DIST_OPS]
    theirs = [s for s in smp if s[2] == "C->H" and s[1] in KEY_DIST_OPS]
    print()
    print("=== 機械判定 ===")
    print(f"  我々(H->C)が送った鍵配布 PDU : {len(ours)} 件 "
          f"{[hex(s[1]) for s in ours]}")
    print(f"  相手(C->H)が送った鍵配布 PDU : {len(theirs)} 件 "
          f"{[hex(s[1]) for s in theirs]}")
    print(f"  CONN_COMPLETE {len(conn)} 件 / DISCONNECT {len(disc)} 件")
    for t, h in disc:
        reason = h[-2:] if len(h) >= 2 else "??"
        print(f"    +{t:9.3f}s DISCONNECT raw={h} reason=0x{reason}")


if __name__ == "__main__":
    main()
