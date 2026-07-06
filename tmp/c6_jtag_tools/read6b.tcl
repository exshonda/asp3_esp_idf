init
halt
for {set r 0} {$r < 16} {incr r} {
    set cmd [expr {0x6b | ($r << 8)}]
    mww 0x600af804 $cmd
    sleep 3
    set v [read_memory 0x600af804 32 1]
    set d [expr {($v >> 16) & 0xff}]
    echo [format "REG6B %d %02x" $r $d]
}
resume
exit
