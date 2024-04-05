#include "display.h"

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
extern MatrixPanel_I2S_DMA mdisplay;

String messageToShow = "";
int16_t x = mdisplay.width();

void animateText(const String& message, int y) {
  static uint32_t prevMillis = 0;
  uint32_t currentMillis = millis();

  int textWidth = message.length() * 6; // 每个字符宽度为6个像素
  int textHeight = 9; // 字高为8个像素
  float speed = 1;//每毫秒移动像素数

  if (currentMillis - prevMillis >= 30) { // 控制移动速度
    prevMillis = currentMillis;

    //mdisplay.flipDMABuffer();
    //必须清空大于字符串宽度的区域，否则移动出现乱码
    mdisplay.fillRect(x,y,textWidth*2, textHeight, mdisplay.color565(0,0,0));
    // 在x位置绘制字符串
    mdisplay.setFont();
    //mdisplay.setFont(&agencyb6pt7b);
    //mdisplay.setFont(&Conther_26pt7b);
    mdisplay.setTextWrap(false);
    mdisplay.setTextColor(mdisplay.color565(0,0,200));
    mdisplay.setCursor(x, y);
    mdisplay.print(message);

    // 更新x位置
    x -= speed;
    if (x + textWidth < 0) { // 如果字符串移出屏幕左侧
      x = mdisplay.width(); // 重新从屏幕右侧开始移动
      //animationRunning = false; // 动画结束
      //messageToShow = ""; // 清空消息
    }
  }
}

uint16_t parseRGBColor(String hexColor) {
    uint32_t rgb = strtol(hexColor.c_str(), NULL, 16);
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;

    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}