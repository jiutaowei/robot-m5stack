# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "1.4.3")
add_definitions(-DFIRMWARE_VERSION=\"${PROJECT_VER}\")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# Auto-load a local sdkconfig overlay (git-ignored) when present. Lets
# self-hosted / custom deployments pin CONFIG_STACKCHAN_SERVER_URL,
# CONFIG_OTA_URL, etc. without touching committed defaults.
# Manual equivalent: SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.local" idf.py build
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.local")
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.local"
        CACHE STRING "ESP-IDF sdkconfig default files (auto-extended with local overlay)" FORCE)
    message(STATUS "StackChan: detected sdkconfig.defaults.local, applying overlay")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# ESP-IDF v5.5 的 project.cmake 用 CMAKE_CURRENT_LIST_DIR 找 components，
# 但 include 时 CMAKE_CURRENT_LIST_DIR 指向 project.cmake 所在目录（不是 fw/）。
# 所以必须显式把项目根的 components 加到 EXTRA_COMPONENT_DIRS，否则 ArduinoJson/esp-now/mooncake
# 等本地 component 不会被 CMake 加载，导致 "Failed to resolve component 'ArduinoJson': unknown name"
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")

project(stack-chan)

# 字体子集预检：编译前扫描 main/ 与 managed_components/wifi-connect 里的中文字符，
# 对比 main/assets/fonts/symbols.txt，若缺字则编译失败并提示修复命令。
# 跳过条件：用户显式设 MIBAO_SKIP_ZH_CHECK=1。
if(NOT DEFINED MIBAO_SKIP_ZH_CHECK)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_scan_script "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/scan_zh_chars.py")
    if(EXISTS "${_scan_script}")
        execute_process(
            COMMAND ${Python3_EXECUTABLE} "${_scan_script}" --strict
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
            RESULT_VARIABLE _scan_rc
            OUTPUT_VARIABLE _scan_out
            ERROR_VARIABLE _scan_err)
        if(_scan_out)
            message(STATUS "${_scan_out}")
        endif()
        if(NOT _scan_rc EQUAL 0)
            message(FATAL_ERROR
                "\n[font-subset] 中文字体子集不完整，编译中止。\n"
                "  缺字列表见上方 [result] missing 行。\n"
                "  修复步骤：\n"
                "    1) 把缺字追加到 fw/main/assets/fonts/symbols.txt\n"
                "    2) 跑 lv_font_conv 重新生成 mibao_zh_font_16.c 和 mibao_zh_font.c\n"
                "    3) 重新 idf.py build\n"
                "  临时跳过本检查：cmake -DMIBAO_SKIP_ZH_CHECK=1 ..\n")
        endif()
    else()
        message(WARNING "[font-subset] 未找到 ${_scan_script}，跳过扫描。")
    endif()
endif()
