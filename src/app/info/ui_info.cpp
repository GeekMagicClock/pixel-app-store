
#include <Arduino.h>
#include <WiFi.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "info.h"
#include "../../lib/display.h"

extern MatrixPanel_I2S_DMA mdisplay;

void exit_info(){

}

struct ScrollText {
  String message;
  int16_t x;
  String color;
  uint32_t prevMillis;
  bool scrolling;

  ScrollText() : x(0), prevMillis(0), scrolling(true) {}

  void start(const String& msg, String c, int y) {
    message = msg;
    color = c;
    x = mdisplay.width();
    prevMillis = 0;
    scrolling = true;
  }

  void update(int y) {
    if (!scrolling) return;

    uint32_t currentMillis = millis();
    int textWidth = message.length() * 6; // 每个字符宽度为 6 个像素
    int textHeight = 9; // 字高为 9 个像素
    float speed = 1; // 每毫秒移动像素数

    if (currentMillis - prevMillis >= 30) { // 控制移动速度
      prevMillis = currentMillis;

      // 在 x 位置绘制字符串之前清空文本区域
      mdisplay.fillRect(x, y, textWidth * 2, textHeight, mdisplay.color565(0, 0, 0));

      // 在 x 位置绘制字符串
      mdisplay.setFont();
      mdisplay.setTextWrap(false);
      mdisplay.setTextColor(parseRGBColor(color));
      //mdisplay.setTextColor(mdisplay.color565(0, 0, 200));
      mdisplay.setCursor(x, y);
      mdisplay.print(message);

      // 更新 x 位置
      x -= speed;
      if (x + textWidth < 0) { // 如果字符串移出屏幕左侧
        x = mdisplay.width(); // 重新从屏幕右侧开始移动
        //scrolling = false; // 停止滚动
      }
    }
  }
};

// 使用示例
ScrollText scrollText1;
ScrollText scrollText2;
ScrollText scrollText3;

void init_info(){
    scrollText1.start("GeekMagic Pixel Display", C_GOLD, 0);
    scrollText2.start("SSID: " + WiFi.SSID(), C_CYAN, 10);
    scrollText3.start("IP: " + WiFi.localIP().toString(), C_FOREST_GREEN, 20);
}

void display_info(){
    
  scrollText1.update(0); 
  scrollText2.update(10);
  scrollText3.update(20);
   //animateText("GeekMagic Pixel Display", 0); 
   //animateText("SSID: ", 8); 
   //animateText("IP: " + WiFi.localIP().toString(), 8); 
}