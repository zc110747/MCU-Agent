set confirm off
set pagination off
set print pretty on
set backtrace limit 20

target extended-remote localhost:3333
monitor reset halt

# --- boot: stop at main -----------------------------------------------------
break main
continue
printf "\n[S1] reached main(), pc=%p\n", $pc

# --- after SystemClock_Config ----------------------------------------------
tbreak main.c:47
continue
printf "[S1] SystemCoreClock      = %u Hz\n", SystemCoreClock
printf "[S1] RCC->CR              = 0x%08x\n", ((RCC_TypeDef*)0x58024400)->CR
printf "[S1] RCC->CFGR            = 0x%08x\n", ((RCC_TypeDef*)0x58024400)->CFGR
printf "[S1] RCC->PLLCKSELR       = 0x%08x\n", ((RCC_TypeDef*)0x58024400)->PLLCKSELR
printf "[S1] RCC->PLL1DIVR        = 0x%08x\n", ((RCC_TypeDef*)0x58024400)->PLL1DIVR
printf "[S1] FLASH ACR            = 0x%08x\n", ((FLASH_TypeDef*)0x52002000)->ACR

# --- after peripheral init --------------------------------------------------
tbreak main.c:52
continue
printf "\n[S1] --- peripherals ---\n"
printf "[S1] hspi6.State          = %d (1=READY)\n", hspi6.State
printf "[S1] hspi6.ErrorCode      = 0x%08x\n", hspi6.ErrorCode
printf "[S1] hsd1.State           = %d (1=READY)\n", hsd1.State
printf "[S1] hsd1.ErrorCode       = 0x%08x\n", hsd1.ErrorCode
printf "[S1] hsd1.SdCard.BlockNbr = %u\n", hsd1.SdCard.BlockNbr
printf "[S1] hsd1.SdCard.BlockSize= %u\n", hsd1.SdCard.BlockSize
printf "[S1] hsd1.SdCard.CardType = %u\n", hsd1.SdCard.CardType
printf "[S1] hsd1.SdCard.CardVer  = %u\n", hsd1.SdCard.CardVersion

# --- check HAL tick is actually running ------------------------------------
printf "\n[S1] --- HAL tick check ---\n"
printf "[S1] tick before delay    = %u\n", uwTick
shell sleep 1
printf "[S1] tick after 1s wall   = %u  (should be unchanged, core halted)\n", uwTick

quit
