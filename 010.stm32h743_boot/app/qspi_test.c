/**
  ******************************************************************************
  * @file    app/qspi_test.c
  * @brief   QSPI flash self-test (indirect HAL read/write + memory-mapped XIP)
  *
  * Encapsulated: NOT called from main(). Use BSP_QSPI_RunSelfTest() when needed.
  ******************************************************************************
  */
#include "qspi_test.h"
#include "bsp_log.h"

#define TEST_ADDR       0x00000000UL   /* sector 0 */
#define TEST_LEN        256U

static void DumpHex(const char *label, const uint8_t *buf, uint32_t len)
{
    PRINT_LOG("  %s:\r\n  ", label);
    for (uint32_t i = 0; i < len; i++) {
        PRINT_LOG("%02X ", buf[i]);
        if ((i + 1) % 16 == 0) PRINT_LOG("\r\n  ");
    }
    PRINT_LOG("\r\n");
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
    PRINT_LOG("\r\n[1] Init QUADSPI peripheral... ");
    st = BSP_QSPI_Init();
    if (st != QSPI_OK) {
        PRINT_LOG("FAIL (0x%02X)\r\n", st);
        return 1;
    }
    PRINT_LOG("OK\r\n");

    /* ---- Read JEDEC ID ---- */
    PRINT_LOG("[2] Read JEDEC ID (cmd 0x9F)... ");
    st = BSP_QSPI_ReadID(id, 3);
    if (st != QSPI_OK) {
        PRINT_LOG("FAIL (0x%02X)\r\n", st);
        return 1;
    }
    PRINT_LOG("OK  -> MFR=0x%02X MemType=0x%02X Cap=0x%02X\r\n",
                    id[ 0], id[1], id[2]);
    if (id[0] == 0xEF) {
        PRINT_LOG("    Detected: Winbond W25Q64JV (8MB)\r\n");
    } else if (id[0] == 0x68) {
        PRINT_LOG("    Detected: Boya BY25Q64 (W25Q64-compatible clone, 8MB)\r\n");
    } else {
        PRINT_LOG("    WARNING: unrecognized manufacturer 0x%02X\r\n", id[0]);
    }

    /* ---- Prepare test pattern ---- */
    for (uint32_t i = 0; i < TEST_LEN; i++) {
        wr[i] = (uint8_t)((i * 7 + 0x11) & 0xFF);  /* deterministic pattern */
    }

    /* ---- TEST A: Indirect (HAL) read/write mode ---- */
    PRINT_LOG("\r\n[TEST A] Indirect (HAL) read/write mode\r\n");
    PRINT_LOG("  Erase sector @0x%08lX... ", TEST_ADDR);
    st = BSP_QSPI_EraseSector(TEST_ADDR);
    PRINT_LOG(st == QSPI_OK ? "OK\r\n" : "FAIL\r\n");
    if (st != QSPI_OK) { fail++; goto mm_test; }

    PRINT_LOG("  Program %u bytes... ", TEST_LEN);
    st = BSP_QSPI_WritePage(TEST_ADDR, wr, TEST_LEN);
    PRINT_LOG(st == QSPI_OK ? "OK\r\n" : "FAIL\r\n");
    if (st != QSPI_OK) { fail++; goto mm_test; }

    PRINT_LOG("  Read back (HAL indirect)... ");
    st = BSP_QSPI_ReadIndirect(TEST_ADDR, rd_ind, TEST_LEN);
    PRINT_LOG(st == QSPI_OK ? "OK\r\n" : "FAIL\r\n");
    if (st != QSPI_OK) { fail++; goto mm_test; }

    int mismatch = 0;
    for (uint32_t i = 0; i < TEST_LEN; i++) {
        if (rd_ind[i] != wr[i]) { mismatch++; }
    }
    DumpHex("  Written pattern", wr, 32);
    DumpHex("  Read  (indirect)", rd_ind, 32);
    if (mismatch == 0) {
        PRINT_LOG("  RESULT: PASS (indirect read matches written data)\r\n");
    } else {
        PRINT_LOG("  RESULT: FAIL (%d byte mismatches)\r\n", mismatch);
        fail++;
    }

mm_test:
    /* ---- TEST B: Memory-mapped (XIP) mode ---- */
    PRINT_LOG("\r\n[TEST B] Memory-mapped (XIP) mode (reads @0x90000000)\r\n");
    PRINT_LOG("  Enter memory-mapped mode (cmd 0xEB, quad)... ");
    st = BSP_QSPI_EnableMemoryMapped();
    PRINT_LOG(st == QSPI_OK ? "OK\r\n" : "FAIL\r\n");
    if (st != QSPI_OK) { fail++; goto summary; }

    PRINT_LOG("  Read flash via 0x90000000 pointer... ");
    const volatile uint8_t *pflash = (const volatile uint8_t *)(QSPI_BASE_ADDR + TEST_ADDR);
    for (uint32_t i = 0; i < TEST_LEN; i++) {
        rd_mm[i] = pflash[i];
    }
    PRINT_LOG("OK\r\n");

    int mm_mismatch = 0;
    for (uint32_t i = 0; i < TEST_LEN; i++) {
        if (rd_mm[i] != wr[i]) { mm_mismatch++; }
    }
    DumpHex("  Read  (memory-mapped)", rd_mm, 32);
    if (mm_mismatch == 0) {
        PRINT_LOG("  RESULT: PASS (memory-mapped read matches written data)\r\n");
    } else {
        PRINT_LOG("  RESULT: FAIL (%d byte mismatches)\r\n", mm_mismatch);
        fail++;
    }

    PRINT_LOG("  Exit memory-mapped mode... ");
    st = BSP_QSPI_DisableMemoryMapped();
    PRINT_LOG(st == QSPI_OK ? "OK\r\n" : "FAIL\r\n");

summary:
    PRINT_LOG("\r\n=================================================\r\n");
    if (fail == 0) {
        PRINT_LOG(" OVERALL: PASS - QSPI flash works in BOTH modes\r\n");
    } else {
        PRINT_LOG(" OVERALL: FAIL - %d test(s) failed\r\n", fail);
    }
    PRINT_LOG("=================================================\r\n");
    return fail;
}
