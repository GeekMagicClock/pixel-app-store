set(HUB75_DMA_SRCS
  "src/ESP32-HUB75-MatrixPanel-I2S-DMA.cpp"
  "src/ESP32-HUB75-MatrixPanel-leddrivers.cpp"
)

if(CONFIG_IDF_TARGET_ESP32S3)
  list(APPEND HUB75_DMA_SRCS "src/platforms/esp32s3/gdma_lcd_parallel16.cpp")
else()
  list(APPEND HUB75_DMA_SRCS "src/platforms/esp32/esp32_i2s_parallel_dma.cpp")
endif()

idf_component_register(
  SRCS
    ${HUB75_DMA_SRCS}
  INCLUDE_DIRS
    "src"
  REQUIRES
    driver
    esp_lcd
)

# Pure ESP-IDF build (no Arduino / no Adafruit GFX).
target_compile_options(${COMPONENT_TARGET} PUBLIC -DNO_GFX)
