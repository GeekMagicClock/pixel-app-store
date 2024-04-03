#include "my_debug.h"
#include "../../lib/gif.h"
#include "weather_en.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "font/agencyb8pt7b.h"
#include <Arduino.h>
#include "TimeLib.h"

extern Weather wea;
extern MatrixPanel_I2S_DMA mdisplay;

bool animationRunning = false;
String messageToShow = "";
void animateText(const String& message, int y) {
  static int16_t x = mdisplay.width();
  static uint32_t prevMillis = 0;
  uint32_t currentMillis = millis();

  int textWidth = message.length() * 5; // 每个字符宽度为6个像素
  int textHeight = 8; // 字高为8个像素
  float speed = 0.01 * (textWidth + mdisplay.width()) / 2; // 计算速度，每毫秒移动像素数

  if (currentMillis - prevMillis >= 1) { // 控制移动速度
    prevMillis = currentMillis;


    mdisplay.fillRect(x,y,mdisplay.width(), textHeight, mdisplay.color565(0,0,0));
    // 在x位置绘制字符串
    mdisplay.setFont();
    mdisplay.setTextWrap(false);
    mdisplay.setTextColor(mdisplay.color565(0,0,200));
    mdisplay.setCursor(x, y);
    mdisplay.print(message);

    // 更新x位置
    x -= speed;
    if (x + textWidth < 0) { // 如果字符串移出屏幕左侧
      x = mdisplay.width(); // 重新从屏幕右侧开始移动
      animationRunning = false; // 动画结束
      messageToShow = ""; // 清空消息
    }
  }
}

void startAnimation(const String& message) {
  if (!animationRunning && message.length() > 0) {
    messageToShow = message;
    animationRunning = true;
  }
}

void display_weather(){

    //DBG_PTN("weather");
    //1. icon
    //2. weather string
    //3. scroll text
    //4. temperature / humidity
    //5. 
    if(wea.weather_changed) wea.init_done= true;
    if(!wea.init_done){
        drawGif("/image/w.gif", 15, 0);//loading.gif
    }else{
        messageToShow = wea.weather_desc;
        if (!animationRunning && messageToShow.length() > 0) {
            animateText(messageToShow, 16); // 开始动画
        }

        String weather_gif = "/wea/" + wea.weather_code + ".gif";
        drawGif(weather_gif.c_str(), 0, 0);

        //mdisplay.setFont(&agencyb8pt7b);
        mdisplay.setFont();
        mdisplay.setTextColor(mdisplay.color565(0, 216, 0));
        mdisplay.setCursor(1,24);

        mdisplay.fillRect(1, 16, 32, 16, mdisplay.color565(0, 0, 0));
        mdisplay.print(String(hour())+":"+String(minute()));
    }

    if(wea.weather_changed){
        //wea.init_done = true;
        mdisplay.clearScreen();
#if 0
    if(wea.init_done){
        String weather_gif = "/wea/" + wea.weather_code + ".gif";
        drawGif(weather_gif.c_str(), 0, 0);
    }

    if(!wea.init_done){
        drawGif("/image/w.gif", 16, 0);//loading.gif
    }

    if(!wea.weather_changed) return;
    if(wea.weather_changed){ wea.init_done = true; } 
    mdisplay.fillRect(16, 0, 32, 32, mdisplay.color565(0, 0, 0));
#endif

        //mdisplay.setFont(&FreeSans6pt7b);
        mdisplay.setFont();
        mdisplay.setCursor(20,0);
        mdisplay.setTextColor(mdisplay.color565(173, 216, 230));
        mdisplay.print(wea.weather_str);

        mdisplay.setTextColor(mdisplay.color565(0, 216, 0));
        mdisplay.setCursor(18,8);
        mdisplay.print(String(wea.temperature) + "C");

        mdisplay.setTextColor(mdisplay.color565(173, 216, 230));
        mdisplay.setCursor(42,8);
        mdisplay.print(String(wea.humidity) + "%");
        wea.weather_changed = false;
    }
}
