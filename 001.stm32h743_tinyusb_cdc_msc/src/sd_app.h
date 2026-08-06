/* ---------------------------------------------------------------------------
 * FatFs application layer: mounts (and, on first use, formats) the SD card and
 * exposes a few CDC commands. Coordinates with the USB host so device-side
 * file access never collides with the host's mass-storage writes.
 * -------------------------------------------------------------------------*/
#pragma once

#include <stdbool.h>

void fatfs_init(void);            /* mount; format + seed README if blank */
void fatfs_release_for_host(void);/* host just mounted the U-disk        */
void fatfs_reacquire(void);       /* host just ejected the U-disk         */
bool fatfs_host_active(void);     /* true while the host owns the card    */

void cmd_sd(void);                /* CDC: show SD card / filesystem status */
void cmd_ls(void);                /* CDC: list root directory             */
void cmd_cat(const char* fname);  /* CDC: print a file                    */
void cmd_remount(void);           /* CDC: re-read the on-disk FAT (after  */
                                   /*       the USB host has changed files) */
