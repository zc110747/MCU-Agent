set confirm off
set pagination off
target extended-remote localhost:3333
monitor halt

echo \n########## RUNTIME VERIFY (attach to running target, no reflash) ##########\n
printf "uwTick       = %u ms\n", uwTick
printf "s_uptime_sec = %u s\n", s_uptime_sec
printf "s_font_ready = %u\n", s_font_ready
set $o = *(unsigned int*)0x58021814
printf "GPIOG ODR    = 0x%08x   PG7(LED)=%d  PG12(BL)=%d\n", $o, ($o>>7)&1, ($o>>12)&1

echo \n---------- 1) LED heartbeat (break at toggle, expect ~500ms) ----------\n
break app_main.c:142
continue
printf "LED toggle #1 : uwTick = %u ms\n", uwTick
continue
printf "LED toggle #2 : uwTick = %u ms\n", uwTick
continue
printf "LED toggle #3 : uwTick = %u ms\n", uwTick
delete

echo \n---------- 2) Periodic UI refresh path ----------\n
break LCD_DisplayNumber
continue
printf "LCD_DisplayNumber HIT : uwTick=%u ms  s_uptime_sec=%u\n", uwTick, s_uptime_sec
info args
bt 3
delete

echo \n---------- 3) SPI6 peripheral live state ----------\n
printf "hspi6.State     = %d\n", hspi6.State
printf "hspi6.ErrorCode = 0x%08x\n", hspi6.ErrorCode
set $cr1 = *(unsigned int*)0x58001400
printf "SPI6 CR1        = 0x%08x   (SPE=%d)\n", $cr1, $cr1 & 1

echo \n---------- 4) resume target and detach ----------\n
monitor resume
detach
quit
