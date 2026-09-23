# ---------------------------------------------------------------------------
# main component -- Ebook LVGL application
#
# Layering (strictly one-directional, see the task spec section 11/12):
#   app/       pages, navigation, business interaction
#   ui/        LVGL widgets, theme, layout, assets
#   services/  FileService / BookService / WeatherService / ...
#   platform/  display, lvgl, touch, storage, system  (the only layer that
#              talks to ESP-IDF drivers, GPIOs and I2C)
#
# Sources AND include directories are both discovered automatically, so adding
# a module folder (platform/storage/, app/pages/settings/, ...) is a pure
# source-tree operation and never needs an edit here.
#
# The glob is evaluated at configure time, so a new *file or folder* needs a
# CMake re-run before it is compiled: `./tools/run_idf.bat reconfigure`, or
# touch this file, or delete build/.  Without one, the new file is silently not
# built and the failure shows up far away as an undefined reference at link
# time.  CONFIGURE_DEPENDS would automate exactly this, but ESP-IDF includes
# this file in *script mode* (component_get_requirements.cmake), where CMake
# rejects the keyword outright, so the re-run stays manual.
# ---------------------------------------------------------------------------

set(APP_DIRS
    app
    ui
    services
    platform
)

set(APP_SOURCES "")
set(APP_INCLUDE_DIRS ".")

foreach(dir ${APP_DIRS})
    # The layer root itself (app/, ui/, ...) is an include directory too.
    list(APPEND APP_INCLUDE_DIRS "${dir}")

    # Sources: every .c/.cpp under the layer.
    file(GLOB_RECURSE _sources
        "${CMAKE_CURRENT_LIST_DIR}/${dir}/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/${dir}/*.cpp"
    )
    list(APPEND APP_SOURCES ${_sources})

    # Include directories: every directory under the layer, so that headers can
    # be included by bare name from anywhere in the component.
    file(GLOB_RECURSE _entries LIST_DIRECTORIES true
        "${CMAKE_CURRENT_LIST_DIR}/${dir}/*"
    )
    foreach(entry ${_entries})
        if(IS_DIRECTORY "${entry}")
            file(RELATIVE_PATH _rel "${CMAKE_CURRENT_LIST_DIR}" "${entry}")
            list(APPEND APP_INCLUDE_DIRS "${_rel}")
        endif()
    endforeach()
endforeach()

list(REMOVE_DUPLICATES APP_INCLUDE_DIRS)
list(SORT APP_INCLUDE_DIRS)

list(LENGTH APP_SOURCES APP_SOURCE_COUNT)
list(LENGTH APP_INCLUDE_DIRS APP_INCLUDE_COUNT)
message(STATUS "ebook: ${APP_SOURCE_COUNT} application sources, ${APP_INCLUDE_COUNT} include dirs")

idf_component_register(
    SRCS "main.cpp" ${APP_SOURCES}
    INCLUDE_DIRS ${APP_INCLUDE_DIRS}
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        esp_driver_sdspi
        esp_lcd
        esp_lcd_touch
        esp_lcd_touch_gt911
        esp_timer
        esp_psram
        esp_app_format
        spi_flash
        nvs_flash
        sdmmc
        fatfs
        lvgl
        esp_lvgl_port
)

