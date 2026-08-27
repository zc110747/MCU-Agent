cmake_minimum_required(VERSION 3.16)

# 第三方代码放 third_party/，官方/SDK 库与板级驱动放 Drivers/
# ESP-IDF 会自动扫描这两个目录下的组件（空目录不影响当前编译）
set(EXTRA_COMPONENT_DIRS third_party Drivers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_rndis_wifi)
