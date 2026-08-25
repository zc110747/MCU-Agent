# ============================================================================
# tools/boot_secret.py
# Single source of truth for the firmware-upgrade HMAC-SHA256 shared key.
#
# ALL firmware images (bootloader C code, this tool, the verifiers) MUST use
# exactly these bytes. It mirrors bsp/flash_secure.h BOOT_HMAC_KEY:
#   {'S','T','M','3','2','H','7','B','o','o','t','K',
#    'e','y','2','0','2','6','#','U','-','D','i','s','k'}
# Editing either side without the other breaks every existing upgrade package.
# ============================================================================
BOOT_HMAC_KEY = b"STM32H7BootKey2026#U-Disk"
BOOT_HMAC_KEY_LEN = len(BOOT_HMAC_KEY)   # 25 bytes

# Matches bsp/flash_secure.h APP_CFG_MAGIC / APP_CFG_STATUS_OK
APP_CFG_MAGIC = 0xB0075EED
APP_CFG_STATUS_OK = 0x00000001

# Flash layout (mirror of bsp/flash_upgrade.h)
APP_BASE_ADDR = 0x08020000
APP_VERSION_OFFSET = 0x1000          # version slot at 0x08021000
CFG_BASE_ADDR = 0x081E0000
APP_SIZE = 0x001C0000
