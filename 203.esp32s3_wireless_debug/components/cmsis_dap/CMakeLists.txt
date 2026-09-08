idf_component_register(SRCS "cmsis_dap.c"
                       INCLUDE_DIRS "include"
                       REQUIRES usb_device debug_engine swd target esp_rom jtag)
