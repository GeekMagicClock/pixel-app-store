#include <Arduino.h>
#ifndef DISPLAY_H
#define DISPLAY_H
// 基本颜色
#define C_RED "FF0000"
#define C_GREEN "00FF00"
#define C_BLUE "0000FF"
#define C_WHITE "FFFFFF"
#define C_BLACK "000000"

// 亮度和饱和度调整的颜色
#define C_LIGHT_RED "FF8888"
#define C_DARK_RED "800000"
#define C_LIGHT_GREEN "88FF88"
#define C_DARK_GREEN "008000"
#define C_LIGHT_BLUE "8888FF"
#define C_DARK_BLUE "000080"
#define C_LIGHT_YELLOW "FFFF88"
#define C_DARK_YELLOW "808000"
#define C_LIGHT_ORANGE "FFA500"
#define C_DARK_ORANGE "FF4500"
#define C_LIGHT_PURPLE "EE82EE"
#define C_DARK_PURPLE "800080"

// 中性色
#define C_GRAY "808080"
#define C_LIGHT_GRAY "D3D3D3"
#define C_DARK_GRAY "A9A9A9"
#define C_SILVER "C0C0C0"

// 饱和度调整的灰色
#define C_LIGHTER_GRAY "CCCCCC"
#define C_DARKER_GRAY "666666"

// 其他常用颜色
#define C_YELLOW "FFFF00"
#define C_MAGENTA "FF00FF"
#define C_CYAN "00FFFF"
#define C_PINK "FFC0CB"
#define C_LAVENDER "E6E6FA"
#define C_TAN "D2B48C"
#define C_GOLD "FFD700"
#define C_NAVY "000080"
#define C_SKY_BLUE "87CEEB"
#define C_FOREST_GREEN "228B22"
#define C_BROWN "A52A2A"
#define C_INDIGO "4B0082"

uint16_t parseRGBColor(String hexColor);
void animateText(const String& message, int y);

#endif