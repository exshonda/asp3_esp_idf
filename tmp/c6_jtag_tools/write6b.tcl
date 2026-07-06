init
halt
# RFデバイスが応答するか（scan中か）を確認: reg3読み(期待0x3b)
mww 0x600af804 0x036b
sleep 3
set v [read_memory 0x600af804 32 1]
echo [format "PROBE reg3=%02x (expect 3b)" [expr {($v>>16)&0xff}]]
# 5レジスタをnative値に上書き: cmd=0x05000000|(data<<16)|(reg<<8)|0x6b
foreach {r d} {2 0x31 4 0xa4 11 0x29 13 0x06 14 0x3f} {
    set cmd [expr {0x05000000 | ($d<<16) | ($r<<8) | 0x6b}]
    mww 0x600af804 $cmd
    sleep 3
}
# 読み戻し検証
foreach r {2 4 11 13 14} {
    set cmd [expr {($r<<8) | 0x6b}]
    mww 0x600af804 $cmd
    sleep 3
    set v [read_memory 0x600af804 32 1]
    echo [format "VERIFY reg%d=%02x" $r [expr {($v>>16)&0xff}]]
}
resume
exit
