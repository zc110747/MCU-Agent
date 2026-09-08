idf_component_register(SRCS "jtag.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio swd esp_rom)
