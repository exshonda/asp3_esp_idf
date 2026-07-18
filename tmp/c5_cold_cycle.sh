#!/bin/bash
#  C5#2（hub port5）の**真のPOWERON**電源再投入＋読み戻し実証。
#
#  ★過去に「off したつもりで実は warm だった」事故（usb_status.md の事故記録）が
#  あるため、off 後に device count=0 を**必ず読み戻して**電源断を実証する。
#  出力を捨てないこと。
set -u
COUNT() { ls /dev/serial/by-id/ 2>/dev/null | grep -c "C8:94"; }

echo "[cold] before off: C8:94 device count = $(COUNT)"
python3 ~/bin/usbhub3c_ctl.py off 5
sleep 3
N=$(COUNT)
echo "[cold] after off (t+3s): C8:94 device count = $N"
if [ "$N" != "0" ]; then
    echo "[cold] *** FAILED: device still present -> power did NOT drop. ABORT. ***"
    exit 1
fi
python3 ~/bin/usbhub3c_ctl.py status 2>&1 | grep -A2 "^   5" | head -3
echo "[cold] holding 10s ..."
sleep 10
python3 ~/bin/usbhub3c_ctl.py on 5
sleep 4
echo "[cold] after on: C8:94 device count = $(COUNT)"
ls -l /dev/serial/by-id/ | grep -i "C8:94"
