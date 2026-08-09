# ---------------------------------------------------------------------------
# Reliable F429 flash sequence for targets whose firmware may idle into a
# low-power mode and gate the debug clock (STOP/sleep). A plain "reset halt"
# misses the core because, after nRESET is released, the firmware boots and
# turns the debug domain off before OpenOCD's halt request lands.
#
# The trick (verified working): hold the core in reset, write DBGMCU_CR so the
# debug clock stays on across sleep/stop/standby, release reset, then halt.
# With DBGMCU_CR = 0x7 the debug domain never powers down, so "halt" works.
# ---------------------------------------------------------------------------
init
mww 0xe0042004 0x7
reset run
halt
flash write_image erase $IMAGE
flash verify_image $IMAGE
reset halt
echo "*** F429 flash done ***"
exit
