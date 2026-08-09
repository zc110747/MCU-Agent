/* Minimal stand-in for the ST system file. We boot from internal flash at
 * 0x08000000, so the only thing worth doing here is pointing VTOR at it.
 * No clock/PLL setup is needed: the demo runs on the reset HSI (16 MHz). */

#define SCB_VTOR (*(volatile unsigned int *)0xE000ED08)

void SystemInit(void) {
    SCB_VTOR = 0x08000000;
}

/* The ST startup calls this for C++ static constructors; we have none. */
void __libc_init_array(void) {
    /* empty */
}
