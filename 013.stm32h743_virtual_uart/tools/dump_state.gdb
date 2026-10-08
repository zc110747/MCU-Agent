set pagination off
set confirm off

target remote :3333

monitor reset run
shell sleep 2

# Sample GPIOA IDR many times in a row without halting in between is not
# possible, but repeated halt/sample still catches the low pulses if the
# waveform is slow enough at 9600 baud (104 us per bit).
printf "--- sample PA9 level 40x (ODR bit9) ---\n"
set $i = 0
while $i < 40
  printf "ODR=%08x IDR=%04x\n", *(unsigned int*)0x58020014, *(unsigned short*)0x58020010
  set $i = $i + 1
end

detach
quit
