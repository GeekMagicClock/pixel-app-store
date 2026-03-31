set(WEB_ASSET_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(F_HTML_SOURCE "${PROJECT_DIR}/webpages/f.html")
set(F_HTML_HEADER "${WEB_ASSET_GEN_DIR}/f_html_gz.h")

if(NOT CMAKE_SCRIPT_MODE_FILE)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  file(MAKE_DIRECTORY "${WEB_ASSET_GEN_DIR}")
  add_custom_command(
    OUTPUT "${F_HTML_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_DIR}/scripts/embed_web_asset.py"
            --input "${F_HTML_SOURCE}"
            --output "${F_HTML_HEADER}"
            --symbol "f_html_gz"
    DEPENDS
      "${F_HTML_SOURCE}"
      "${PROJECT_DIR}/scripts/embed_web_asset.py"
    COMMENT "Generating embedded web asset ${F_HTML_HEADER}"
    VERBATIM
  )
  add_custom_target(pixel_web_asset DEPENDS "${F_HTML_HEADER}")

  if(NOT EXISTS "${F_HTML_HEADER}" OR "${F_HTML_SOURCE}" IS_NEWER_THAN "${F_HTML_HEADER}")
    execute_process(
      COMMAND "${Python3_EXECUTABLE}" "${PROJECT_DIR}/scripts/embed_web_asset.py"
              --input "${F_HTML_SOURCE}"
              --output "${F_HTML_HEADER}"
              --symbol "f_html_gz"
      RESULT_VARIABLE WEB_ASSET_BOOTSTRAP_RESULT
    )
    if(NOT WEB_ASSET_BOOTSTRAP_RESULT EQUAL 0)
      message(FATAL_ERROR "failed to bootstrap embedded web asset: ${F_HTML_SOURCE}")
    endif()
  endif()
endif()

idf_component_register(
  SRCS
    "main.cpp"
    "app/buzzer.cpp"
    "app/hub75_config.cpp"
    "app/app_update_server.cpp"
    "app/display_control.cpp"
    "app/lua_app_runtime.cpp"
    "app/user_button.cpp"
    "app/wifi_manager.cpp"
    "third_party/animatedgif/AnimatedGIF.cpp"
    "ui/lvgl_hub75_port.cpp"
  "ui/lvgl_mem_utils.cpp"
    "ui/lvgl_boot_wifi_screen.cpp"
  "ui/lvgl_smoke_test.cpp"
    "ui/lvgl_lua_app_screen.cpp"
    "ui/lvgl_lua_app_carousel.cpp"
  "ui/fonts/lv_font_digits_thin_8.c"
    "ui/lv_font_silkscreen_regular_8.c"
    "ui/lv_font_pressstart2p_regular_8.c"
  INCLUDE_DIRS
    "."
  REQUIRES
    hub75_dma
    lvgl
    lua
    json
    driver
    esp_event
    esp_netif
    esp_wifi
  esp_system
    nvs_flash
    esp_http_server
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
  add_dependencies(${COMPONENT_LIB} pixel_web_asset)
  target_include_directories(${COMPONENT_LIB} PRIVATE "${WEB_ASSET_GEN_DIR}")
endif()
