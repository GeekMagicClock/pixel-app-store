#include "my_debug.h"
#include "../../lib/gif.h"
#include "../../lib/display.h"
#include "../../lib/settings.h"
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
extern String m_color;
extern String s_color;

#include "WiFi.h"
#include "../../lib/btn.h"
bool wea_init_status = false;

String weather_key = "";
String pub_weather_key = "";//默认为空
float lon = 0, lat = 0;
String temp_unit ="C";
String windspeed_unit;
String pressure_unit;
int last_second;
extern int timeZone;
extern int minutesTimeZone;
int weather_interval;
extern int theme_index;

void init_weather(){
    read_unit_config(&windspeed_unit, &temp_unit, &pressure_unit);
    char citycode[32] = {0};
    char cityname[32] = {0};
    char location[64] = {0};
    int ret = read_city_config(cityname, sizeof(citycode), citycode, sizeof(cityname), location, sizeof(location));
    if(ret == 0) {
      wea.cityName = String(cityname);
      wea.cityCode = String(citycode);
    }

    read_key_config(&weather_key);
    if(weather_key.length() < 30){
      weather_key = pub_weather_key;
      DBG_PTN("use default interval");
      weather_interval  = 1200;//20分钟更新频率
    }else{
      read_weather_interval_config(&weather_interval);
      DBG_PTN("use user interval:" + String(weather_interval));
      DBG_PTN(weather_key);
      //todo check
      wea.updateInterval = weather_interval*60;
    }
    //wea.cityCode = "Zhongshan";
    wea.init(weather_key, pub_weather_key, wea.cityCode, lon, lat);
    wea.timeZone = timeZone;
    wea.minutesTimeZone = minutesTimeZone;

    mdisplay.clearScreen();
    mdisplay.setFont();
    mdisplay.setCursor(12,4);
    mdisplay.setTextColor(parseRGBColor(C_LIGHT_BLUE));
    mdisplay.println("6.");
    mdisplay.setCursor(12,13);
    //mdisplay.println("Weather");
    mdisplay.println("WEATHER");
    mdisplay.setCursor(12,22);
    mdisplay.print("CLOCK");
    int i = 0;
    while(i<1000) {
        i++;
        delay(1);
        update_btn();
    }

    mdisplay.clearScreen();
}

#include "../../lib/times.h"
void update_weather(bool force){
    wea.update(force);
}
#include "../../lib/gif.h"
void exit_weather(){
    //wea.init_done = false;
    gifDeinit();
    wea.weather_changed = false;
    wea.forecast_changed = false;
    wea_init_status = false;
}
extern int hour12;

int format_hour12(){
  if( hour(now()) == 0 )
    return 12; // 12 midnight
  else if( hour(now())  > 12)
    return hour(now()) - 12 ;
  else
    return hour(now());
}

String mon[] = {"Jan","Feb","Mar","Apr", "May", "Jun", "Jul","Aug", "Sep", "Oct", "Nov", "Dec"};
String month_day(){
  String mm = mon[month()-1];
  String dd = String(day())+"th";
  return mm +" "+ dd; 
}

void display_weather(){

    //DBG_PTN("weather");
    //1. icon
    //2. weather string
    //3. scroll text
    //4. temperature / humidity
    //5. 

    if(wea.weather_changed && wea_init_status == false){mdisplay.clearScreen();}//第一次获取到数据，清屏一次
    if(wea.weather_changed) { wea.init_done= true; wea_init_status = true;}
    if(!wea.init_done){
        //drawGif("/image/w.gif", 15, 0);//loading.gif
        mdisplay.setFont();
        mdisplay.setCursor(0,12);
        mdisplay.setTextColor(mdisplay.color565(173, 216, 230));
        mdisplay.print("UPDATING..");
    }else{
        String cur_temp;  
        String feels_like;
        //scrollText[3] = "Min Temp "+ min_temp_str + C_F;
        if(temp_unit == "C"){
            //min_temp_str = "Min temp " + String(wea.min_temp)+ "C";
            cur_temp = String(wea.temperature)+"C";
            feels_like = "Feels like "+ String(wea.feels_like) + "C";
        }else {
            //min_temp_str = "Min temp " + String(int(wea.min_temp*1.8+32))+ "℉";
            //max_temp_str = "Max temp " + String(int(wea.max_temp*1.8+32)) + "℉";
            cur_temp = String(int(wea.temperature*1.8+32))+"F";
            feels_like = "Feels like "+ String(int(wea.feels_like*1.8+32)) + "F";
        }

        //mdisplay.print(String(hour())+":"+String(minute()/10)+String(minute()%10)+":"+String(second()/10)+String(second()%10));
        if(wea.weather_changed){
          mdisplay.fillRect(16,0,48,16, parseRGBColor(C_BLACK));
          mdisplay.setFont();
          mdisplay.setCursor(20,0);
          mdisplay.setTextColor(parseRGBColor(C_LIGHT_BLUE));
          mdisplay.print(wea.weather_str);

          mdisplay.setTextColor(parseRGBColor(C_FOREST_GREEN));
          mdisplay.setCursor(18,8);
          mdisplay.print(cur_temp);

          mdisplay.setTextColor(mdisplay.color565(173, 216, 230));
          mdisplay.setCursor(42,8);
          mdisplay.print(String(wea.humidity) + "%");
          wea.weather_changed = false;
        }
        String weather_gif = "/wea/" + wea.weather_code + ".gif";
        drawGif(weather_gif.c_str(), 0, 0);

        String min_temp_str;  
        String max_temp_str;  
        String wind;
        String pressure;
        if(pressure_unit == "hPa"){ 
          pressure = ". Atm " + String(wea.pressure) + "hPa";
        }else if(pressure_unit == "kPa"){
          pressure = ". Atm " + String(int(wea.pressure/10)) + "kPa";
        }else if(pressure_unit == "mmHg"){
          pressure = ". Atm "+ String(int(wea.pressure*0.75)) + "mmHg";
        }else if(pressure_unit == "inHg"){
          pressure = ". Atm "+ String(int(wea.pressure*0.02953)) + "inHg";
        }
        
        if(windspeed_unit == "m/s"){
          wind = ". Wind " + String(int(wea.wind_speed)) +"m/s";
        }else if(windspeed_unit == "km/h"){
          wind = ". Wind " + String(int(wea.wind_speed*3.6)) +"km/h";
        }else if(windspeed_unit == "mile/h"){
          wind = ". Wind " + String(int(wea.wind_speed*2.2367)) +"mile/h";
        }

        String messageToShow = "";
        messageToShow += month_day()+", ";
        messageToShow += wea.cityName +", ";
        messageToShow += wea.weather_desc +",";
        messageToShow += feels_like + wind + pressure +" "+ WiFi.localIP().toString();
        //messageToShow = "this is a very long text."; 
        if (messageToShow.length() > 0) {
            animateText(messageToShow, 16); // 开始动画
        }
#if 1
        //mdisplay.setFont(&agencyb8pt7b);
        if(hour12){
          if(last_second != second()){
            mdisplay.setFont();
            mdisplay.fillRect(0, 25, 64, 8, 0);
            mdisplay.setCursor(0,25);
            mdisplay.setTextColor(parseRGBColor(h_color));
            if(hour(now())<12)
              mdisplay.print("AM");
            else
              mdisplay.print("PM");

            int hh = format_hour12();
            mdisplay.setCursor(15,25);
            mdisplay.setTextColor(parseRGBColor(h_color));
            if(hour() == 12){
              mdisplay.setCursor(15,25);
              mdisplay.print(String(hh/10)+String(hh%10));
            } else {
              mdisplay.setCursor(22,25);
              mdisplay.print(String(hh%10));
            }
            mdisplay.print(":");

            mdisplay.setCursor(18+15,25);
            mdisplay.setTextColor(parseRGBColor(m_color));
            mdisplay.print(String(minute()/10)+String(minute()%10));
            mdisplay.print(":");

            mdisplay.setCursor(20+15+15,25);
            mdisplay.setTextColor(parseRGBColor(s_color));
            mdisplay.print(String(second()/10)+String(second()%10));
            last_second = second();
          }
        }else{
          if(last_second != second()){
            mdisplay.setFont();
            mdisplay.fillRect(0, 25, 64, 8, 0);
            mdisplay.setCursor(8,25);
            mdisplay.setTextColor(parseRGBColor(h_color));
            //mdisplay.print(hour());
            mdisplay.print(String(hour(now())/10)+String(hour(now())%10));
            mdisplay.print(":");
            //DBG_PTN(hour(now()));

            mdisplay.setTextColor(parseRGBColor(m_color));
            mdisplay.setCursor(10+15,25);
            mdisplay.print(String(minute(now())/10)+String(minute(now())%10));
            mdisplay.print(":");
            
            mdisplay.setCursor(12+15+15,25);
            mdisplay.setTextColor(parseRGBColor(s_color));
            mdisplay.print(String(second()/10)+String(second()%10));
            last_second = second();
          }
         //mdisplay.print(second());
        }
#endif
    }
}
