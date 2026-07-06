init
halt
foreach r {2 4 11 13 14} {
    set cmd [expr {($r<<8) | 0x6b}]
    mww 0x600af804 $cmd
    sleep 3
    set v [read_memory 0x600af804 32 1]
    echo [format "CHECK reg%d=%02x" $r [expr {($v>>16)&0xff}]]
}
resume
exit
