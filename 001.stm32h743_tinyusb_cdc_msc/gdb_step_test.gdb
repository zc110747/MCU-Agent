set pagination off
set confirm off
file build/debug/h743_tinyusb_cdc.elf
target remote :3333
monitor halt
echo \n>>> halted, PC before step:\n
info registers pc
break cdc_task
continue
echo \n>>> hit breakpoint in cdc_task, PC:\n
info registers pc
stepi
echo \n>>> after stepi #1, PC:\n
info registers pc
stepi
echo \n>>> after stepi #2, PC:\n
info registers pc
stepi
echo \n>>> after stepi #3, PC:\n
info registers pc
monitor reset run
detach
quit
