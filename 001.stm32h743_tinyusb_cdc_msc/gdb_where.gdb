set pagination off
set confirm off
file build/debug/h743_tinyusb_cdc.elf
target remote :3333
monitor halt
echo \n>>> PC where firmware is stuck:\n
info registers pc
echo \n>>> backtrace:\n
bt
echo \n>>> RCC_CR (0x58024400) and PLLCKSELR (0x58024428):\n
monitor mdw 0x58024400 1
monitor mdw 0x58024428 1
detach
quit
