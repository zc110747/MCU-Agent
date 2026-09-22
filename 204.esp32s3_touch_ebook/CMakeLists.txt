cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Ebook LVGL  --  Waveshare ESP32-S3-Touch-LCD-4.3B (800x480 RGB LCD)
# ESP-IDF project root.
# ---------------------------------------------------------------------------

# Pin the application version explicitly.  Left to itself, esp-idf derives it
# from `git describe` of the *enclosing* repository -- this project lives inside
# a larger one, so the firmware reported a version string belonging to a
# completely different project.  A version nobody can trust is worse than no
# version at all, so it is stated here and bumped by hand per phase.
set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ebook_lvgl)
