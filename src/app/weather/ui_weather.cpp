#include "my_debug.h"
#include "../../lib/gif.h"
#include "../../lib/display.h"
#include "weather_en.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <ESP32-VirtualMatrixPanel-I2S-DMA.h>
#include "font/Conther_26pt7b.h"
#include <Arduino.h>
#include "TimeLib.h"
#include "font/agencyb6pt7b.h"


extern Weather wea;
extern MatrixPanel_I2S_DMA mdisplay;
//extern VirtualMatrixPanel vdisplay;
extern String h_color;


#include "WiFi.h"
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
          String weather_gif = "/wea/" + wea.weather_code + ".gif";
        drawGif(weather_gif.c_str(), 0, 0);
        String feels_like = ". Feels like "+ String(wea.feels_like)+ wea.temp_unit;
        String wind = ". Wind speed "+ String(wea.wind_speed) + wea.wind_unit;
        String atm = ". Outdoor barometric pressure "+ String(wea.pressure) + wea.pressure_unit;

        String messageToShow = wea.weather_desc +" "+ WiFi.localIP().toString();
        messageToShow += feels_like + wind + atm;
        //messageToShow = "this is a very long text."; 
        if (messageToShow.length() > 0) {
            animateText(messageToShow, 16); // 开始动画
        }
#if 1
        //mdisplay.setFont(&agencyb8pt7b);
        mdisplay.setFont();
        mdisplay.setTextColor(parseRGBColor(h_color));
        mdisplay.setCursor(8,24);

        mdisplay.fillRect(0, 24, 64, 8, 0);
        mdisplay.print(String(hour())+":"+String(minute()/10)+String(minute()%10)+":"+String(second()/10)+String(second()%10));
#endif
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
