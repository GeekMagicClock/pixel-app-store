idf_component_register(
  SRCS
    "src/platforms/esp32/esp32_i2s_parallel_dma.cpp"
    "src/ESP32-HUB75-MatrixPanel-I2S-DMA.cpp"
    "src/ESP32-HUB75-MatrixPanel-leddrivers.cpp"
  INCLUDE_DIRS
    "src"
  REQUIRES
    driver
)

# Pure ESP-IDF build (no Arduino / no Adafruit GFX).
target_compile_options(${COMPONENT_TARGET} PUBLIC -DNO_GFX)

