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
bool wea_init_status = false;

String weather_key = "";
String pub_weather_key = "";//默认为空
float lon = 0, lat = 0;

void init_weather(){
    wea.cityCode = "Zhongshan";
    wea.init(weather_key, pub_weather_key, wea.cityCode, lon, lat);
    mdisplay.clearScreen();
    mdisplay.setCursor(0,12);
    mdisplay.print("Weather");
    delay(1000);
}

void update_weather(bool force){
        wea.update(force);
}
void exit_weather(){
    //wea.init_done = false;
    wea.weather_changed = false;
    wea.forecast_changed = false;
    wea_init_status = false;
}
void display_weather(){

    //DBG_PTN("weather");
    //1. icon
    //2. weather string
    //3. scroll text
    //4. temperature / humidity
    //5. 

    String cur_temp;  
    if(wea.weather_changed && wea_init_status == false){mdisplay.clearScreen();}//第一次获取到数据，清屏一次
    if(wea.weather_changed) { wea.init_done= true; wea_init_status = true;}
    if(!wea.init_done){
        //drawGif("/image/w.gif", 15, 0);//loading.gif
        mdisplay.setFont();
        mdisplay.setCursor(0,16);
        mdisplay.setTextColor(mdisplay.color565(173, 216, 230));
        mdisplay.print("Updating..");
    }else{
        String weather_gif = "/wea/" + wea.weather_code + ".gif";
        drawGif(weather_gif.c_str(), 0, 0);

        //String feels_like = ". Feels like "+ String(wea.feels_like)+ wea.temp_unit;
        //String wind; 
        //String atm = ". Outdoor barometric pressure "+ String(wea.pressure) + wea.pressure_unit;

        String min_temp_str;  
        String max_temp_str;  
        String feels_like;
        String wind;
        String pressure;
        //scrollText[3] = "Min Temp "+ min_temp_str + C_F;
        if(wea.temp_unit == "C"){
            //min_temp_str = "Min temp " + String(wea.min_temp)+ "C";
            cur_temp = String(wea.temperature)+"C";
            feels_like = ". Feels like "+ String(wea.feels_like) + "C";
        }else {
            //min_temp_str = "Min temp " + String(int(wea.min_temp*1.8+32))+ "℉";
            //max_temp_str = "Max temp " + String(int(wea.max_temp*1.8+32)) + "℉";
            cur_temp = String(int(wea.temperature*1.8+32))+"F";
            feels_like = ". Feels like "+ String(int(wea.feels_like*1.8+32)) + "F";
        }

        if(wea.pressure_unit == "hPa"){ 
          pressure = ". Atm " + String(wea.pressure) + "hPa";
        }else if(wea.pressure_unit == "kPa"){
          pressure = ". Atm " + String(int(wea.pressure/10)) + "kPa";
        }else if(wea.pressure_unit == "mmHg"){
          pressure = ". Atm "+ String(int(wea.pressure*0.75)) + "mmHg";
        }else if(wea.pressure_unit == "inHg"){
          pressure = ". Atm "+ String(int(wea.pressure*0.02953)) + "inHg";
        }
        
        if(wea.wind_unit == "m/s"){
          wind = ". Wind " + String(int(wea.wind_speed)) +"m/s";
        }else if(wea.wind_unit == "km/h"){
          wind = ". Wind " + String(int(wea.wind_speed*3.6)) +"km/h";
        }else if(wea.wind_unit == "mile/h"){
          wind = ". Wind " + String(int(wea.wind_speed*2.2367)) +"mile/h";
        }

        String messageToShow = wea.weather_desc +" "+ WiFi.localIP().toString();
        messageToShow += feels_like + wind + pressure;
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

        mdisplay.setFont();
        mdisplay.setCursor(20,0);
        mdisplay.setTextColor(mdisplay.color565(173, 216, 230));
        mdisplay.print(wea.weather_str);

        mdisplay.setTextColor(mdisplay.color565(0, 216, 0));
        mdisplay.setCursor(18,8);
        mdisplay.print(cur_temp);

        mdisplay.setTextColor(mdisplay.color565(173, 216, 230));
        mdisplay.setCursor(42,8);
        mdisplay.print(String(wea.humidity) + "%");
        wea.weather_changed = false;
 
#endif
    }

    if(wea.weather_changed){
        //wea.init_done = true;
        //mdisplay.clearScreen();
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
   }
}
