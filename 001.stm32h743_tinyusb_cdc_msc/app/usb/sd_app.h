/* ---------------------------------------------------------------------------
 * FatFs application layer: mounts (and, on first use, formats) the SD card and
 * exposes a few CDC commands. Coordinates with the USB host so device-side
 * file access never collides with the host's mass-storage writes.
 * -------------------------------------------------------------------------*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void fatfs_init(void);            /* mount; format + seed README if blank */
void fatfs_release_for_host(void);/* host just mounted the U-disk        */
void fatfs_reacquire(void);       /* host just ejected the U-disk         */
bool fatfs_host_active(void);     /* true while the host owns the card    */
void msc_trace_arm(void);         /* re-arm READ tracing for the next mount */

void cmd_sd(void);                /* CDC: show SD card / filesystem status */
void cmd_ls(void);                /* CDC: list root directory             */
void cmd_cat(const char* fname);  /* CDC: print a file                    */
void cmd_remount(void);           /* CDC: re-read the on-disk FAT (after  */
                                   /*       the USB host has changed files) */
void cmd_sync(void);              /* CDC: force the write-back cache out  */
void cmd_scache(void);            /* CDC: block-cache counters / layout   */
void cmd_sdbench(const char* arg);/* CDC: raw SDIO read throughput (MB/s) */
void cmd_fatclean(void);          /* CDC: clear the FAT32 dirty flag that */
                                   /*      makes Windows verify the whole  */
                                   /*      FAT on every mount              */

#ifdef __cplusplus
}
#endif
