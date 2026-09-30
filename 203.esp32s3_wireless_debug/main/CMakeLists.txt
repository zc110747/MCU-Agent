# NOTE: wifi_dap is always linked. Do NOT try to make it conditional on
# CONFIG_DEBUG_ENABLE_WIFI here: the sdkconfig CMake variables are not
# reliable at component-registration time in this IDF version, so a
# conditional REQUIRES silently drops the include path and breaks the build.
# The run-time gate lives in app_main.c / wifi_dap.c (#if CONFIG_...).
idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES usb_device cmsis_dap wifi_dap debug_engine
                                swd target jtag)

# ---------------------------------------------------------------------------
# Whole-archive linking of all DAP components.
#
# ESP-IDF compiles each component into a static archive (.a) and the linker
# only pulls members that resolve an undefined symbol. That broke TinyUSB:
#   - usb_descriptors.c.obj (provides the required tud_descriptor_* callbacks)
#     was dropped -> undefined reference at final link
#   - strong definitions of __weak TinyUSB hooks could silently lose to the
#     weak stubs (HID reports never delivered)
# Wrapping every DAP component archive in --whole-archive forces all object
# files into the final link unconditionally.
# ---------------------------------------------------------------------------
set(DAP_COMPONENTS usb_device cmsis_dap wifi_dap debug_engine swd target jtag)

foreach(comp ${DAP_COMPONENTS})
    idf_component_get_property(lib ${comp} COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PRIVATE
        "-Wl,--whole-archive"
        "$<TARGET_FILE:${lib}>"
        "-Wl,--no-whole-archive")
endforeach()

# Provide tusb_config.h (lives in usb_device/include) to the tinyusb component
# itself, per espressif/tinyusb standalone-usage instructions.
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/usb_device/include")
