cmake_minimum_required(VERSION 3.16)

project(lvgl_pixel_sim LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)

set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h CACHE PATH "" FORCE)
set(LV_BUILD_LVGL_H_SIMPLE_INCLUDE ON CACHE BOOL "" FORCE)

execute_process(
    COMMAND sdl2-config --cflags
    OUTPUT_VARIABLE SDL2_CFLAGS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
    COMMAND sdl2-config --libs
    OUTPUT_VARIABLE SDL2_LIBS_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

separate_arguments(SDL2_CFLAGS_LIST NATIVE_COMMAND "${SDL2_CFLAGS_RAW}")
separate_arguments(SDL2_LIBS_LIST NATIVE_COMMAND "${SDL2_LIBS_RAW}")

add_subdirectory(${ROOT_DIR}/components/lvgl ${CMAKE_CURRENT_BINARY_DIR}/lvgl)
target_compile_options(lvgl PRIVATE ${SDL2_CFLAGS_LIST})

add_executable(lvgl_pixel_sim
    ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/sim_stubs.cpp
    ${ROOT_DIR}/src/ui/lvgl_boot_wifi_screen.cpp
    ${ROOT_DIR}/src/ui/lvgl_stock_screen6.cpp
    ${ROOT_DIR}/src/ui/lvgl_mem_utils.cpp
    ${ROOT_DIR}/src/ui/lv_font_silkscreen_regular_8.c
)

target_include_directories(lvgl_pixel_sim PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${ROOT_DIR}/src
    ${ROOT_DIR}/components/lvgl
    ${ROOT_DIR}/components/lvgl/src
    ${ROOT_DIR}/components/lvgl/src/drivers/sdl
)

target_compile_options(lvgl_pixel_sim PRIVATE ${SDL2_CFLAGS_LIST})
target_link_libraries(lvgl_pixel_sim PRIVATE lvgl ${SDL2_LIBS_LIST} m)
