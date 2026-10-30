/* ---------------------------------------------------------------------------
 * usb_hid_mouse.c - HID output endpoint wrapper.
 * -------------------------------------------------------------------------*/

#include "tusb.h"
#include "usb_hid_mouse.h"
#include "usb_descriptors.h"

/* The descriptor declares a single report and no Report ID, so the ID passed
 * to TinyUSB must be 0 - passing a non-zero value would prepend a byte the
 * host is not expecting and shift every field by one. */
#define HID_REPORT_ID_NONE   0u

static usb_hid_mouse_stats_t g_usb_mouse;

void usb_hid_mouse_init(void)
{
    g_usb_mouse.sent           = 0u;
    g_usb_mouse.refused        = 0u;
    g_usb_mouse.not_ready      = 0u;
    g_usb_mouse.mounted_events = 0u;
}

bool usb_hid_mouse_ready(void)
{
    return tud_hid_ready();
}

bool usb_hid_mouse_mounted(void)
{
    return tud_mounted();
}

bool usb_hid_mouse_send(const mouse_report_t *report)
{
    uint8_t  buffer[MOUSE_REPORT_WIRE_LEN];
    uint32_t length;

    if (report == 0)
    {
        return false;
    }

    /* tud_hid_report() would drop the report anyway when the endpoint has no
     * room, but checking first keeps "refused" and "not ready" distinguishable
     * in the STATUS output. */
    if (tud_hid_ready() == false)
    {
        g_usb_mouse.not_ready++;
        return false;
    }

    length = mouse_report_pack(report, buffer);
    if (length == 0u)
    {
        return false;
    }

    if (tud_hid_report(HID_REPORT_ID_NONE, buffer, (uint16_t) length) == false)
    {
        g_usb_mouse.refused++;
        return false;
    }

    g_usb_mouse.sent++;
    return true;
}

void usb_hid_mouse_get_stats(usb_hid_mouse_stats_t *stats)
{
    if (stats == 0)
    {
        return;
    }

    *stats = g_usb_mouse;
}

/* ---------------------------------------------------------------------------
 * TinyUSB callbacks
 * -------------------------------------------------------------------------*/

void tud_mount_cb(void)
{
    g_usb_mouse.mounted_events++;
}

void tud_umount_cb(void)
{
    g_usb_mouse.mounted_events++;
}

/* The host may try to push a report (e.g. a LED output report). This device
 * declares no output report, so the correct answer is "nothing to do" -
 * returning true acknowledges and discards. */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0u;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

/* Declared in usb_descriptors.c as well; TinyUSB asks for it through
 * tud_hid_descriptor_report_cb(), which lives there because the descriptor
 * does. Keeping the callback next to the data avoids two sources of truth. */
