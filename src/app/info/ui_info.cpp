
#include <Arduino.h>
#include <WiFi.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "info.h"
#include "../../lib/display.h"
#include "../../font/alibaba-puhui-M64pt7b.h"

extern MatrixPanel_I2S_DMA mdisplay;

void exit_info(){
}

ScrollText scrollText1;
ScrollText scrollText2;
ScrollText scrollText3;

void init_info(){
    //scrollText.start("中文测试阿里巴巴普惠体",&alibabaM64pt7b,C_GOLD,0);
    scrollText1.start("GeekMagic Pixel Display",NULL, C_GOLD, 1);
    scrollText2.start("SSID: " + WiFi.SSID(),NULL, C_CYAN, 11);
    scrollText3.start("Please visit http://" + WiFi.localIP().toString() +" to change settings.", NULL, C_FOREST_GREEN, 21);
}

void display_info(){
  //scrollText.update(0);
  scrollText1.update(1); 
  scrollText2.update(11);
  scrollText3.update(21);
}