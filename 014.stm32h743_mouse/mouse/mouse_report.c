/* ---------------------------------------------------------------------------
 * mouse_report.c - internal event -> HID wire payload.
 * -------------------------------------------------------------------------*/

#include "mouse_report.h"

static int8_t clamp_axis(int32_t value)
{
    if (value > MOUSE_REPORT_AXIS_MAX)
    {
        return (int8_t) MOUSE_REPORT_AXIS_MAX;
    }
    if (value < -MOUSE_REPORT_AXIS_MAX)
    {
        return (int8_t) -MOUSE_REPORT_AXIS_MAX;
    }
    return (int8_t) value;
}

void mouse_report_clear(mouse_report_t *report)
{
    if (report == 0)
    {
        return;
    }

    report->buttons = 0u;
    report->x       = 0;
    report->y       = 0;
    report->wheel   = 0;
    report->pan     = 0;
}

bool mouse_report_is_idle(const mouse_report_t *report)
{
    if (report == 0)
    {
        return true;
    }

    return (report->buttons == 0u) && (report->x == 0) && (report->y == 0) &&
           (report->wheel == 0) && (report->pan == 0);
}

uint32_t mouse_report_pack(const mouse_report_t *report, uint8_t *out)
{
    if ((report == 0) || (out == 0))
    {
        return 0u;
    }

    out[0] = (uint8_t) (report->buttons & MOUSE_BUTTON_MASK);
    out[1] = (uint8_t) clamp_axis((int32_t) report->x);
    out[2] = (uint8_t) clamp_axis((int32_t) report->y);
    out[3] = (uint8_t) clamp_axis((int32_t) report->wheel);
    out[4] = (uint8_t) clamp_axis((int32_t) report->pan);

    return MOUSE_REPORT_WIRE_LEN;
}
