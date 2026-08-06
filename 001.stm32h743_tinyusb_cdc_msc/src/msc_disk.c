/* ---------------------------------------------------------------------------
 * TinyUSB Mass Storage Class callbacks - the SD card is exposed to the USB
 * host as a single LUN (logical unit). All block moves go through sdcard.c,
 * exactly as FatFs does, so the two views of the card can never disagree.
 * -------------------------------------------------------------------------*/

#include "tusb.h"
#include "sdcard.h"
#include "sd_app.h"   /* fatfs_release_for_host / fatfs_reacquire */

void tud_msc_inquiry_cb(uint8_t lun,
                        uint8_t vendor_id[8],
                        uint8_t product_id[16],
                        uint8_t product_rev[4]) {
  (void) lun;
  /* The stack has already flagged this as a REMOVABLE device (U-disk). */
  static const char v[] = "STM32H7";
  static const char p[] = "SD_Card";
  static const char r[] = "1.0";
  memcpy(vendor_id,   v, sizeof(v) - 1);
  memcpy(product_id,  p, sizeof(p) - 1);
  memcpy(product_rev, r, sizeof(r) - 1);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  (void) lun;
  return sdcard_present();
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
  (void) lun;
  *block_count = sdcard_block_count();
  *block_size  = (uint16_t) sdcard_block_size();
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           void* buffer, uint32_t bufsize) {
  (void) lun;
  uint32_t const blk   = sdcard_block_size();
  uint32_t const start = lba + (offset / blk);
  uint32_t const n     = bufsize / blk;
  if (sdcard_read_blocks((uint8_t*) buffer, start, n) != SD_ST_OK) return -1;
  return (int32_t)(n * blk);
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                            uint8_t* buffer, uint32_t bufsize) {
  (void) lun;
  uint32_t const blk   = sdcard_block_size();
  uint32_t const start = lba + (offset / blk);
  uint32_t const n     = bufsize / blk;
  if (sdcard_write_blocks(buffer, start, n) != SD_ST_OK) return -1;
  return (int32_t)(n * blk);
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                         void* buffer, uint16_t bufsize) {
  (void) lun; (void) buffer; (void) bufsize;
  /* Commands the stack does not implement itself land here. SYNCHRONIZE CACHE
   * (0x35) is routinely sent by Linux/macOS hosts; acknowledge it with no data.
   * Anything else is reported as unsupported (negative -> STALL). */
  if (scsi_cmd[0] == 0x35) return 0;
  return -1;
}

/* The host tells us when it mounts (load) and ejects (unload) the volume.
 * While the host owns the card we release the device-side FatFs mount so the
 * two writers can never race and corrupt the filesystem. */
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition,
                            bool start, bool load_eject) {
  (void) lun; (void) power_condition;
  if (load_eject) {
    if (start) fatfs_release_for_host();
    else       fatfs_reacquire();
  }
  return true;
}
