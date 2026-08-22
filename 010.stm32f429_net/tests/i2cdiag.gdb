set pagination off
file E:/cnb/git/Mcu_Project_Design_By_Agent/010.stm32f429_net/build/stm32f429_net.elf
target remote :3333
monitor halt
echo === hi2c2.Init ===\n
p /x hi2c2.Init
echo === I2C2 base 0x40005800 (CR1@0 CR2@4) ===\n
x /2x 0x40005800
echo === GPIOH MODER@0x40021C00 ===\n
x /1x 0x40021C00
echo === GPIOH AFR[0]@0x40021C20 AFR[1]@0x40021C24 ===\n
x /2x 0x40021C20
echo === NVIC ISER[0]@0xE000E100 ISER[1]@0xE000E104 ===\n
x /2x 0xE000E100
echo === RCC AHB1ENR@0x40023830 (GPIOH bit6) APB1ENR@0x40023840 (I2C2 bit22) ===\n
x /1x 0x40023830
x /1x 0x40023840
quit
