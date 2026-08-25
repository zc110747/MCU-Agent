/**
  ******************************************************************************
  * @file    app/qspi_test.c
  * @brief   QSPI flash self-test (indirect HAL read/write + memory-mapped XIP)
  *
  * Encapsulated: NOT called from main(). Use BSP_QSPI_RunSelfTest() when needed.
  ******************************************************************************
  */
#include "qspi_test.h"
#include "uart.h"

#define TEST_ADDR       0x00000000UL   /* sector 0 */
#define TEST_LEN        256U

static void DumpHex(const char *label, const uint8_t *buf, uint32_t len)
{
    BSP_UART_Printf("  %s:\r\n  ", label);
    for (uint32_t i = 0; i < len; i++) {
        BSP_UART_Printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0) BSP_UART_Printf("\r\n  ");
    }
    BSP_UART_Printf("\r\n");
}

int BSP_QSPI_RunSelfTest(void)
{
    uint8_t id[3] = {0};
    uint8_t wr[TEST_LEN];
    uint8_t rd_ind[TEST_LEN];
    uint8_t rd_mm[TEST_LEN];
    QSPI_Status_t st;
    int fail = 0;

    /* ---- Init QSPI ---- */
    BSP_UART_Printf("\r\n[1] Init QUADSPI peripheral... ");
    st = BSP_QSPI_Init();
    if (st != QSPI_OK) {
        BSP_UART_Printf("FAIL (0x%02X)\r\n", st);
        return 1;
    }
    BSP_UART_Printf("OK\r\n");

    /* ---- Read JEDEC ID ---- */
    BSP_UART_Printf("[2] Read JEDEC ID (cmd 0x9F)... ");
    st = BSP_QSPI_ReadID(id, 3);
    if (st != QSPI_OK) {
        BSP_UART_Printf("FAIL (0x%02X)\r\n", st);
        return 1;
    }
    BSP_UART_Printf("OK  -> MFR=0x%02X MemType=0x%02X Cap=0x%02X\r\n",
                    id[ 0], id[1], id[2]);
    if (id[0] == 0xEF) {
        BSP_UART_Printf("    Detected: Winbond W25Q64JV (8MB)\r\n");
    } else if (id[0] == 0x68) {
        BSP_UART_Printf("    Detected: Boya BY25Q64 (W25Q64-compatible clone, 8MB)\r\n");
    } else {
        BSP_UART_Printf("    WARNING: unrecognized manufacturer 0x%02X\r\n", id[0]);
    }

    /* ---- Prepare test pattern ---- */
    for (uint32_t i = 0; i < TEST_LEN; i++) {
        wr[i] = (uint8_t)((i * 7 + 0x11) & 0xFF);  /* deterministic pattern */
    }

    /* ---- TEST A: Indirect (HAL) read/write mode ---- */
    BSP_UART_Printf("\r\n[TEST A] Indirect (HAL) read/write mode\r\n");
    BSP_UART_Printf("  Erase sector @0x%08lX... ", TEST_ADDR);
    st = BSP_QSPI_EraseSector(TEST_ADDR);
    BSP_UART_Printf(st == QSPI_OK ? "OK\r\n" : "FAIL\r\n");
    if (st != QSPI_OK) { fail++; goto mm_test; }

    BSP_UART_Printf("  Program %u bytes... ", TEST_LEN);
    st = BSP_QSPI_WritePage(TEST_ADDR, wr, TEST_LEN);
    BSP_UART_Printf(st == QSPI_OK ? "OK\r\n" : "FAIL\r\n");
    if (st != QSPI_OK) { fail++; goto mm_test; }

    BSP_UART_Printf("  Read back (HAL indirect)... ");
    st = BSP_QSPI_ReadIndirect(TEST_ADDR, rd_ind, TEST_LEN);
    BSP_UART_Printf(st == QSPI_OK ? "OK\r\n" : "FAIL\r\n");
    if (st != QSPI_OK) { fail++; goto mm_test; }

    int mismatch = 0;
    for (uint32_t i = 0; i < TEST_LEN; i++) {
        if (rd_ind[i] != wr[i]) { mismatch++; }
    }
    DumpHex("  Written pattern", wr, 32);
    DumpHex("  Read  (indirect)", rd_ind, 32);
    if (mismatch == 0) {
        BSP_UART_Printf("  RESULT: PASS (indirect read matches written data)\r\n");
    } else {
        BSP_UART_Printf("  RESULT: FAIL (%d byte mismatches)\r\n", mismatch);
        fail++;
    }

mm_test:
    /* ---- TEST B: Memory-mapped (XIP) mode ---- */
    BSP_UART_Printf("\r\n[TEST B] Memory-mapped (XIP) mode (reads @0x90000000)\r\n");
    BSP_UART_Printf("  Enter memory-mapped mode (cmd 0xEB, quad)... ");
    st = BSP_QSPI_EnableMemoryMapped();
    BSP_UART_Printf(st == QSPI_OK ? "OK\r\n" : "FAIL\r\n");
    if (st != QSPI_OK) { fail++; goto summary; }

    BSP_UART_Printf("  Read flash via 0x90000000 pointer... ");
    const volatile uint8_t *pflash = (const volatile uint8_t *)(QSPI_BASE_ADDR + TEST_ADDR);
    for (uint32_t i = 0; i < TEST_LEN; i++) {
        rd_mm[i] = pflash[i];
    }
    BSP_UART_Printf("OK\r\n");

    int mm_mismatch = 0;
    for (uint32_t i = 0; i < TEST_LEN; i++) {
        if (rd_mm[i] != wr[i]) { mm_mismatch++; }
    }
    DumpHex("  Read  (memory-mapped)", rd_mm, 32);
    if (mm_mismatch == 0) {
        BSP_UART_Printf("  RESULT: PASS (memory-mapped read matches written data)\r\n");
    } else {
        BSP_UART_Printf("  RESULT: FAIL (%d byte mismatches)\r\n", mm_mismatch);
        fail++;
    }

    BSP_UART_Printf("  Exit memory-mapped mode... ");
    st = BSP_QSPI_DisableMemoryMapped();
    BSP_UART_Printf(st == QSPI_OK ? "OK\r\n" : "FAIL\r\n");

summary:
    BSP_UART_Printf("\r\n=================================================\r\n");
    if (fail == 0) {
        BSP_UART_Printf(" OVERALL: PASS - QSPI flash works in BOTH modes\r\n");
    } else {
        BSP_UART_Printf(" OVERALL: FAIL - %d test(s) failed\r\n", fail);
    }
    BSP_UART_Printf("=================================================\r\n");
    return fail;
}
