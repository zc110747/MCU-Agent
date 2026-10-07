/* generated configuration header file - do not edit */
#ifndef R_GLCDC_CFG_H_
#define R_GLCDC_CFG_H_
#ifdef __cplusplus
            extern "C" {
            #endif

            #define GLCDC_CFG_PARAM_CHECKING_ENABLE   (BSP_CFG_PARAM_CHECKING_ENABLE)
            #define GLCDC_CFG_COLOR_CORRECTION_ENABLE (false)

            /* Enable DSI function handling.
             *
             * This project drives a 2.0" MIPI DSI panel, so R_GLCDC_Open()
             * must forward to g_mipi_dsi0->p_api->open().  The reference BSP
             * ships this as `#if (RA_NOT_DEFINED != 1)`, i.e. always true.
             *
             * NOTE: the non-DSI (RGB 4.3") variant of this generated header
             * reads `#if (RA_NOT_DEFINED != RA_NOT_DEFINED)` - a constant
             * compared against itself, which is *always false*.  That variant
             * was previously in place here; it silently disabled DSI, which
             * left SQCH0IER == 0, the six DSI NVIC lines un-enabled and the
             * panel black.  Do not copy that form back in. */
            #if (RA_NOT_DEFINED != 1)
            #define GLCDC_CFG_USING_DSI
            #endif

            #ifdef __cplusplus
            }
            #endif
#endif /* R_GLCDC_CFG_H_ */
