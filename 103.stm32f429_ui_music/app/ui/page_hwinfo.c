/**
  ******************************************************************************
  * @file    app/ui/page_hwinfo.c
  * @brief   Page 1 - hardware information screen.
  *
  *  AP3216C (IR / ambient light / proximity) and MPU9250 (accelerometer,
  *  gyroscope, magnetometer).  refresh_hwinfo() is called by the core 2 Hz tick
  *  while this page is on screen.
  ******************************************************************************
  */
#include "ui_common.h"

/**
  * @brief  Format a float with two decimals without pulling in the floating
  *         point printf support of newlib-nano (which is not linked).
  */
static void fmt_fixed2(char *buf, size_t n, float v)
{
    int  neg = (v < 0.0f) ? 1 : 0;
    float a  = neg ? -v : v;
    long ip  = (long)a;
    long fp  = (long)((a - (float)ip) * 100.0f + 0.5f);

    if (fp >= 100L) { fp -= 100L; ip += 1L; }
    (void)snprintf(buf, n, "%s%ld.%02ld", neg ? "-" : "", ip, fp);
}

static void fmt_vec3(char *buf, size_t n, const char *name,
                     float a, float b, float c, const char *unit)
{
    char fa[16], fb[16], fc[16];

    fmt_fixed2(fa, sizeof(fa), a);
    fmt_fixed2(fb, sizeof(fb), b);
    fmt_fixed2(fc, sizeof(fc), c);
    (void)snprintf(buf, n, "%s  X %s  Y %s  Z %s %s", name, fa, fb, fc, unit);
}

void refresh_hwinfo(void)
{
    sensor_data_t d;
    char buf[80];

    sensor_get(&d);

    if (s_ui.p1_ir != NULL)
    {
        if (d.ap3216_ok != 0U)
        {
            lv_label_set_text_fmt(s_ui.p1_ir, "红外 IR  %u", (unsigned int)d.ir);
            lv_label_set_text_fmt(s_ui.p1_als, "环境光  %u lux", (unsigned int)d.als);
            lv_label_set_text_fmt(s_ui.p1_ps, "接近  %u", (unsigned int)d.ps);
        }
        else
        {
            lv_label_set_text(s_ui.p1_ir, "红外 IR  --");
            lv_label_set_text(s_ui.p1_als, "环境光  --");
            lv_label_set_text(s_ui.p1_ps, "接近  --");
        }
    }

    if (s_ui.p1_acc != NULL)
    {
        if (d.mpu_ok != 0U)
        {
            fmt_vec3(buf, sizeof(buf), "加速度", d.ax, d.ay, d.az, "g");
            lv_label_set_text(s_ui.p1_acc, buf);

            fmt_vec3(buf, sizeof(buf), "角速度", d.gx, d.gy, d.gz, "dps");
            lv_label_set_text(s_ui.p1_gyr, buf);

            if (d.mag_ok != 0U)
            {
                fmt_vec3(buf, sizeof(buf), "磁场", d.mx, d.my, d.mz, "uT");
                lv_label_set_text(s_ui.p1_mag, buf);
            }
            else
            {
                lv_label_set_text(s_ui.p1_mag, "磁场  AK8963 未装配");
            }
        }
        else
        {
            lv_label_set_text(s_ui.p1_acc, "加速度  --");
            lv_label_set_text(s_ui.p1_gyr, "角速度  --");
            lv_label_set_text(s_ui.p1_mag, "磁场  --");
        }
    }

    if (s_ui.p1_stat != NULL)
    {
        lv_label_set_text_fmt(s_ui.p1_stat, "采样  成功 %lu / 失败 %lu  触摸事件 %lu",
                              (unsigned long)d.samples, (unsigned long)d.errors,
                              (unsigned long)bsp_touch_press_count());
    }
}

void build_page_hwinfo(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *b1;
    lv_obj_t *b2;
    lv_obj_t *b3;

    /* Band 1 - AP3216C. */
    b1 = make_band(scr, s_band_y[0], s_band_h, "环境传感器  AP3216C");
    s_ui.p1_ir  = mk_label(b1, 8, 32, &lv_font_gbk_16, COL_TXT, "红外 IR  --");
    s_ui.p1_als = mk_label(b1, 8, 56, &lv_font_gbk_16, COL_TXT, "环境光  --");
    s_ui.p1_ps  = mk_label(b1, 8, 80, &lv_font_gbk_16, COL_TXT, "接近  --");

    /* Band 2 - MPU9250 accel + gyro. */
    b2 = make_band(scr, s_band_y[1], s_band_h, "运动传感器  MPU9250");
    s_ui.p1_acc = mk_label(b2, 8, 32, &lv_font_gbk_16, COL_TXT, "加速度  --");
    s_ui.p1_gyr = mk_label(b2, 8, 56, &lv_font_gbk_16, COL_TXT, "角速度  --");
    s_ui.p1_mag = mk_label(b2, 8, 80, &lv_font_gbk_16, COL_TXT, "磁场  --");

    /* Band 3 - sampling health. */
    b3 = make_band(scr, s_band_y[2], s_band_h, "采样状态");
    s_ui.p1_stat = mk_label(b3, 8, 32, &lv_font_gbk_16, COL_DIM, "采样  --");
    (void)mk_label(b3, 8, 56, &lv_font_gbk_16, COL_DIM, "I2C2  PH4(SCL) / PH5(SDA) 400kHz");
}
