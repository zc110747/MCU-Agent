/**
 * @file app_config.h
 * @brief Compile-time application feature switches
 *
 * Baseline scope of project 301: an LVGL menu application on the MIPI DSI
 * panel, with the CST812T touch panel as the input device.
 *
 * The camera stack (GPT7 XCLK + SCCB + OV5640 + CEU) and the OpenMV-derived
 * application layer were REMOVED from this project entirely (files deleted,
 * r_ceu dropped from the build).  They belong to the follow-up project, so
 * there is deliberately no APP_ENABLE_CAMERA / APP_ENABLE_OPENMV switch left:
 * a dead macro that nothing compiles is worse than no macro at all.
 *
 * The touch panel is the one feature switch that remains, because the driver
 * can be built out to fall back to the msh-driven cursor on a board with no
 * panel fitted.
 */
#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

/** CST812T capacitive touch panel over SCI3 I2C (SDA=P208, SCL=P209). */
#ifndef APP_ENABLE_TOUCH
#define APP_ENABLE_TOUCH   (1)
#endif

#endif /* APP_CONFIG_H_ */
