set pagination off
file E:/cnb/git/Mcu_Project_Design_By_Agent/010.stm32f429_net/build/stm32f429_net.elf
target remote :3333
monitor halt
info functions HAL_I2C_IsDeviceReady
set $addr = 0x08
while ($addr <= 0x77)
  set $r = HAL_I2C_IsDeviceReady(&hi2c2, $addr << 1, 1, 10)
  if ($r == 0)
    printf "ACK @ 0x%02X (w=0x%02X)\n", $addr, $addr << 1
  end
  set $addr = $addr + 1
end
printf "scan done\n"
quit
