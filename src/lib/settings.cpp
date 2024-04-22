#include <ArduinoJson.h>
//#include <FS.h>
#include <LittleFS.h>
#include "theme.h"
#include "my_debug.h"
#include <WiFi.h>

//#define LittleFS LITTLEFS
#define  LITTLEFS LittleFS

#define CONFIG_PATH     F("/.sys/config.json") /* wifi 信息 */
#define UNIT_PATH       F("/.sys/unit.json") /* 单位是公制还是英制 */
#define NTP_PATH       F("/.sys/ntp.json") /* NTP 服务器设置 */
#define CITY_PATH       F("/.sys/city.json")
#define BRT_PATH       F("/.sys/brt.json")
#define DELAY_PATH       F("/.sys/delay.json") /* 延迟联网 */
//#define BILI_PATH       F("/bili.json")
#define GIF_PATH       F("/.sys/gif.json") /* gif文件顺序 */
#define IMG_PATH       F("/.sys/img.json") /* gif文件顺序 */
#define THEME_PATH       F("/.sys/app.json") /* 主题*/
#define TIME_COLOR_PATH       F("/.sys/timecolor.json") /* 时间数字的颜色 */
#define ALBUM_PATH        F("/.sys/album.json") /* 相册设置 */
#define STOCK_PATH        F("/.sys/stock.json") /* 股票设置 */
#define STOCK_BG_PATH        F("/.sys/stock_bg.json") /* 股票设置 */
#define STOCK_COLOR_PATH        F("/.sys/stock_color.json") /* 股票设置 */
#define T_BRT_PATH       F("/.sys/timebrt.json") /* 定时亮度 */
#define HOUR12_PATH       F("/.sys/hour12.json") /* 定时亮度 */
#define COLON_PATH       F("/.sys/colon.json") /* 是否开启冒号闪烁 */
#define THEME_LIST_PATH       F("/.sys/theme_list.json") /* 循环主题列表, 存放是否开启该主题循环的开关 */
#define DAYTIMER_PATH       F("/.sys/daytimer.json") /* 天数倒计时 */
#define DAY_PATH       F("/.sys/day.json") /* 日期显示格式 */
#define COIN_PATH       F("/.sys/coin.json") /* 日期显示格式 */
#define KLINE_PATH       F("/.sys/kline.json") 
#define STOCK_KLINE_PATH       F("/.sys/stk.json") 
#define FONT_PATH       F("/.sys/font.json") /* 字体编号 */
#define KEY_PATH       F("/.sys/key.json") /* 天气KEY */
#define WEATHER_INTERVAL_PATH       F("/.sys/w_i.json") /* 天气更新间隔 */
#define DST_PATH       F("/.sys/dst.json") /* 夏令时 */
#define TZ_PATH       F("/.sys/tz.json") /* time zone*/

void reset_config(){
  LittleFS.remove(CONFIG_PATH);
  LittleFS.remove(CITY_PATH);
  LittleFS.remove(BRT_PATH);
  LittleFS.remove(NTP_PATH);
  LittleFS.remove(UNIT_PATH);
  LittleFS.remove(DELAY_PATH);
//  LittleFS.remove(BILI_PATH);
  LittleFS.remove(GIF_PATH);
  LittleFS.remove(THEME_PATH);
  LittleFS.remove(TIME_COLOR_PATH);
  LittleFS.remove(ALBUM_PATH);
  LittleFS.remove(TZ_PATH);
  LittleFS.remove(STOCK_PATH);
  LittleFS.remove(STOCK_BG_PATH);
  LittleFS.remove(STOCK_COLOR_PATH);
  LittleFS.remove(STOCK_KLINE_PATH);
  LittleFS.remove(T_BRT_PATH);
  LittleFS.remove(HOUR12_PATH);
  LittleFS.remove(THEME_LIST_PATH);
  LittleFS.remove(DAYTIMER_PATH);
  LittleFS.remove(COLON_PATH);
  LittleFS.remove(FONT_PATH);
  LittleFS.remove(KEY_PATH);
  LittleFS.remove(WEATHER_INTERVAL_PATH);
  LittleFS.remove(DST_PATH);
}
#if 1

byte encryptionKey[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10 };

void xorEncrypt(byte* data, int length, const byte* key, int keyLength) {
  for (int i = 0; i < length; i++) {
    data[i] ^= key[i % keyLength];
  }
}

void xorDecrypt(byte* data, int length, const byte* key, int keyLength) {
  xorEncrypt(data, length, key, keyLength);
}

//#include <FS.h>
int check_mac() {
  if (LittleFS.exists("/.vr")) {
    File fp = LittleFS.open("/.vr", "r");
    int storedValueLength = fp.size();
    byte storedValue[storedValueLength];
    fp.read(storedValue, storedValueLength);
    fp.close();

    // 读取ESP.getEfuseMac()的值
    String efuseMac = WiFi.macAddress();// String(ESP.getEfuseMac(), HEX);
    int efuseMacLength = efuseMac.length();
    byte efuseMacBytes[efuseMacLength];
    efuseMac.getBytes(efuseMacBytes, efuseMacLength);

    // 对ESP.getEfuseMac()的值进行加密
    xorEncrypt(efuseMacBytes, efuseMacLength, encryptionKey, sizeof(encryptionKey));

    // 将加密后的值与文件中的内容进行比较
    bool contentVerified = (memcmp(efuseMacBytes, storedValue, storedValueLength) == 0);
    if (contentVerified) {
      // 内容一致，正常启动程序
      //DBG_PTN("Content verified. Starting program...");
      // TODO: 执行正常启动的操作
      return 0;
    } else {
      // 内容不一致，停止执行程序
      DBG_PTN("Content verification failed...");
      //ESP.restart();
      while (1) {
        delay(1);
      }
    }
  } else {
    // version.mac文件不存在

    // 读取ESP.getEfuseMac()的值
    //String efuseMac = String(ESP.getEfuseMac(), HEX);
    String efuseMac = WiFi.macAddress();// String(ESP.getEfuseMac(), HEX);
    int efuseMacLength = efuseMac.length();
    byte efuseMacBytes[efuseMacLength];
    efuseMac.getBytes(efuseMacBytes, efuseMacLength);

    // 对ESP.getEfuseMac()的值进行加密
    xorEncrypt(efuseMacBytes, efuseMacLength, encryptionKey, sizeof(encryptionKey));

    File versionFile = LittleFS.open("/.vr", "w");
    versionFile.write(efuseMacBytes, efuseMacLength);
    versionFile.close();

    // 正常启动程序
    //DBG_PTN("First boot. Starting program...");
    // TODO: 执行正常启动的操作
  }
  return 0;
}
#endif
int create_file(char *path){
    File f = LITTLEFS.open(path, "w");
    f.close();
    return 0;
}

int read_ntp_config(String *ntp){
  if (LittleFS.exists(NTP_PATH)){
    File fp = LittleFS.open(NTP_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *ntp =  obj[String("ntp")].as<String>();
    return 0;
  }  
  return -1;
}

int set_ntp_config(String ntp){
  
    File fp = LittleFS.open(NTP_PATH, "w");
    if(!fp) return 0;
    char settings[64] = {0};
    snprintf(settings, sizeof(settings), "{\"ntp\":\"%s\"}", ntp.c_str());
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_unit_config(String *w_u, String *t_u, String *p_u){
  if (LittleFS.exists(UNIT_PATH)){
    File fp = LittleFS.open(UNIT_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    //DBG_PTN(settings);
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();
    *w_u =  obj[String("w_u")].as<String>();
    *t_u =  obj[String("t_u")].as<String>();
    *p_u =  obj[String("p_u")].as<String>();
    return 0;
  }  
  return -1;
}

int set_unit_config(String w_u, String t_u, String p_u){
    File fp = LittleFS.open(UNIT_PATH, "w");
    if(!fp) return 0;
    char settings[64] = {0};
    snprintf(settings, sizeof(settings), "{\"w_u\":\"%s\",\"t_u\":\"%s\",\"p_u\":\"%s\"}", w_u.c_str(), t_u.c_str(), p_u.c_str());
    fp.write((uint8_t*)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_key_config(String *key){
  if (LittleFS.exists(KEY_PATH)){
    File fp = LittleFS.open(KEY_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();
    *key =  obj[String("key")].as<String>();
    return 0;
  }  
  return -1;
}

int set_key_config(String key){
    File fp = LittleFS.open(KEY_PATH, "w");
    if(!fp) return 0;
    char settings[256] = {0};
    snprintf(settings, sizeof(settings), "{\"key\":\"%s\"}", key.c_str());
    fp.write((uint8_t*)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_weather_interval_config(int *i){
  if (LittleFS.exists(WEATHER_INTERVAL_PATH)){
    File fp = LittleFS.open(WEATHER_INTERVAL_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    DynamicJsonDocument doc(128);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *i =  obj[String("w_i")].as<int>();
    fp.close();
    return 0;
  }
  return -1;
}

int set_weather_interval_config(int i){
    File fp = LittleFS.open(WEATHER_INTERVAL_PATH, "w");
    if(!fp) return 0;
    char settings[64] = {0};
    snprintf(settings, sizeof(settings), "{\"w_i\":%d}", i);
    fp.write((uint8_t*)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_wifi_config(char *ssid, char ssid_len, char *pwd, char pwd_len){
  if (LittleFS.exists(CONFIG_PATH)){
    File fp = LittleFS.open(CONFIG_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    //DBG_PTN(settings);
    
    DynamicJsonDocument doc(128);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    snprintf(ssid, ssid_len, "%s", obj[String("a")].as<String>().c_str());
    snprintf(pwd, pwd_len, "%s", obj[String("p")].as<String>().c_str());
    //DBG_PTN("ssid:");
    //DBG_PTN(ssid);

    //DBG_PTN("pwd:");
    //DBG_PTN(pwd);
    fp.close();
    return 0;
  }  
  return -1;
}

int set_wifi_config(const char *ssid, const char *pwd){
    File fp = LittleFS.open(CONFIG_PATH, "w");
    if(!fp) return 0;
    char settings[128] = {0};
    //ap, password
    snprintf(settings, sizeof(settings), "{\"a\":\"%s\", \"p\":\"%s\"}", ssid, pwd);
    fp.write((uint8_t *)settings, strlen(settings));

    fp.close();
    return 0;
}

int read_timezone_config(int *tz, int *mtz){
  if (LittleFS.exists(TZ_PATH)){
    File fp = LittleFS.open(TZ_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    //DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *tz= obj[String("tz")].as<int>();
    *mtz= obj[String("mtz")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}
int set_timezone_config(int tz, int mtz){
    File fp = LittleFS.open(TZ_PATH, "w");
    if(!fp) return 0;
    char settings[256] = {0};
    snprintf(settings, sizeof(settings), "{\"tz\":%d, \"mtz\":%d}", tz, mtz);
    fp.write((uint8_t*)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_day_config(int *day_format){
  if (LittleFS.exists(DAY_PATH)){
    File fp = LittleFS.open(DAY_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    //DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *day_format = obj[String("day_fmt")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}
int set_day_config(int day_format){
    File fp = LittleFS.open(DAY_PATH, "w");
    if(!fp) return 0;
    char settings[256] = {0};
    snprintf(settings, sizeof(settings), "{\"day_fmt\":%d}", day_format);
    fp.write((uint8_t*)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_city_config(char *city, char ct_len, char *code, char cd_len, char *location, char loc_len){
  //if (SPIFFS.exists(CITY_PATH)){
  if (LittleFS.exists(CITY_PATH)){
    File fp = LittleFS.open(CITY_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    //DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    snprintf(city, ct_len, "%s", obj[String("ct")].as<String>().c_str());
    snprintf(code, cd_len, "%s", obj[String("cd")].as<String>().c_str());
    snprintf(location, loc_len, "%s", obj[String("loc")].as<String>().c_str());
    fp.close();
    return 0;
  }  
  return -1;
}

int set_city_config(const char *city, const char *code, const char *location){
  
    File fp = LittleFS.open(CITY_PATH, "w");
    if(!fp) return 0;
    char settings[256] = {0};
    snprintf(settings, sizeof(settings), "{\"ct\":\"%s\", \"cd\":\"%s\", \"loc\":\"%s\"}", city, code, location);
    fp.write((uint8_t*)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_colon_config(int *i){
  if (LittleFS.exists(COLON_PATH)){
    File fp = LittleFS.open(COLON_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    DBG_PTN(settings);
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();
    *i = obj[String("colon")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_colon_config(int i){
    File fp = LittleFS.open(COLON_PATH, "w");
    if(!fp) return 0;
    char settings[32] = {0};
    snprintf(settings, sizeof(settings), "{\"colon\":%d}", i);
    fp.write((uint8_t*)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_hour12_config(int *i){
  if (LittleFS.exists(HOUR12_PATH)){
    File fp = LittleFS.open(HOUR12_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    DBG_PTN(settings);
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();
    *i = obj[String("h")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_hour12_config(int i){
    File fp = LittleFS.open(HOUR12_PATH, "w");
    if(!fp) return 0;
    char settings[32] = {0};
    snprintf(settings, sizeof(settings), "{\"h\":%d}", i);
    fp.write((uint8_t*)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_theme_config(int *i){
  if (LittleFS.exists(THEME_PATH)){
    File fp = LittleFS.open(THEME_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *i = obj[String("theme")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}
extern int theme_index;
int read_page_index_config(int *page_index){
    String file = "/t1.json";
    if(theme_index == 1){
        file = "/t1.json";
    }else if (theme_index == 2){
        file = "/t2.json";
    }else if (theme_index == 3){
        file = "/t3.json";
    }else if (theme_index == 4){
        file = "/t4.json";
    }else if (theme_index == 5){
        file = "/t5.json";
    }else if (theme_index == 6){
        file = "/t6.json";
    }

    if (LittleFS.exists(file)){
      File fp = LittleFS.open(file, "r");
      String settings = fp.readString();
      DBG_PTN(settings);
      
      DynamicJsonDocument doc(256);
      deserializeJson(doc, settings);
      JsonObject obj = doc.as<JsonObject>();

      *page_index = obj[String("p")].as<int>();
      fp.close();
      return 0;
    }  
    return -1;
}

int set_page_index_config(int page_index){
    String file = "/t1.json";
    if(theme_index == 1){
        file = "/t1.json";
    }else if (theme_index == 2){
        file = "/t2.json";
    }else if (theme_index == 3){
        file = "/t3.json";
    }else if (theme_index == 4){
        file = "/t4.json";
    }else if (theme_index == 5){
        file = "/t5.json";
    }else if (theme_index == 6){
        file = "/t6.json";
    }

    File fp = LittleFS.open(file,"w");
    char settings[32] = {0};
    snprintf(settings, sizeof(settings), "{\"p\":%d}", page_index);
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

int set_theme_config(int i){
    File fp = LittleFS.open(THEME_PATH, "w");
    char settings[32] = {0};
    snprintf(settings, sizeof(settings), "{\"theme\":%d}", i);
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_img_config(String &path){
  if (LittleFS.exists(IMG_PATH)){
    File fp = LittleFS.open(IMG_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    DynamicJsonDocument doc(512);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    path =  obj[String("img")].as<String>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_img_config(String path){
    File fp = LittleFS.open(IMG_PATH, "w");
    char settings[256] = {0};
    snprintf(settings, sizeof(settings), "{\"img\":\"%s\"}", path.c_str());
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_gif_config(String &path){
  if (LittleFS.exists(GIF_PATH)){
    File fp = LittleFS.open(GIF_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    JsonDocument doc;
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    path =  obj[String("gif")].as<String>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_gif_config(String path){
    File fp = LittleFS.open(GIF_PATH, "w");
    char settings[256] = {0};
    snprintf(settings, sizeof(settings), "{\"gif\":\"%s\"}", path.c_str());
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_font_config(String *path){
  if (LittleFS.exists(FONT_PATH)){
    File fp = LittleFS.open(FONT_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    JsonDocument doc;
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *path =  obj[String("font")].as<String>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_font_config(String path){
    File fp = LittleFS.open(FONT_PATH, "w");
    char settings[256] = {0};
    snprintf(settings, sizeof(settings), "{\"font\":\"%s\"}",path.c_str());
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_delay_config(int *delay){
  if (LittleFS.exists(DELAY_PATH)){
    File fp = LittleFS.open(DELAY_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *delay = obj[String("delay")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_delay_config(int delay){
    File fp = LittleFS.open(DELAY_PATH, "w");
    char settings[32] = {0};
    snprintf(settings, sizeof(settings), "{\"delay\":%d}", delay);
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_brt_config(int *brt){
  if (LittleFS.exists(BRT_PATH)){
    File fp = LittleFS.open(BRT_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    JsonDocument doc;
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *brt = obj[String("brt")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_brt_config(int brt){
    File fp = LittleFS.open(BRT_PATH, "w");
    char settings[32] = {0};
    snprintf(settings, sizeof(settings), "{\"brt\":\"%d\"}", brt);
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

/*定时调节亮度*/
int read_timer_brt_config(int *en, int *t1, int *t2, int *b2){
  if (LittleFS.exists(T_BRT_PATH)){
    File fp = LittleFS.open(T_BRT_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *en = obj[String("en")].as<int>();
    *t1 = obj[String("t1")].as<int>();
    *t2 = obj[String("t2")].as<int>();
    //*b1 = obj[String("b1")].as<int>();
    *b2 = obj[String("b2")].as<int>();
    return 0;
  }  
  return -1;
}

int set_timer_brt_config(int en, int t1, int t2, int b2){
    //File fp = SPIFFS.open(BRT_PATH, "w");
    File fp = LittleFS.open(T_BRT_PATH, "w");
    if(!fp) return 0;
    char settings[32] = {0};
    snprintf(settings, sizeof(settings), "{\"en\":%d,\"t1\":%d,\"t2\":%d,\"b2\":%d}", en,t1,t2,b2);
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

int set_daytimer_config(int yr, int mth, int day){
    //File fp = SPIFFS.open(BRT_PATH, "w");
    File fp = LittleFS.open(DAYTIMER_PATH, "w");
    if(!fp) return 0;
    char settings[64] = {0};
    snprintf(settings, sizeof(settings), "{\"yr\":%d,\"mth\":%d,\"day\":%d}", yr,mth,day);
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}
int read_daytimer_config(int *yr, int *mth, int *day){
  if (LittleFS.exists(DAYTIMER_PATH)){
    File fp = LittleFS.open(DAYTIMER_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *yr = obj[String("yr")].as<int>();
    *mth = obj[String("mth")].as<int>();
    *day = obj[String("day")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}
#if 0
int read_bili_config(char *id, int id_len, int *b_i){
  if (LittleFS.exists(BILI_PATH)){
    File fp = LittleFS.open(BILI_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    snprintf(id, id_len, "%s", obj[String("uid")].as<String>().c_str());
    *b_i = obj[String("b_i")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_bili_config(const char * bili, int b_i){
    File fp = LittleFS.open(BILI_PATH, "w");
    char settings[32] = {0};
    snprintf(settings, sizeof(settings), "{\"uid\":\"%s\",\"b_i\":\"%d\"}", bili, b_i);
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}
#endif
int read_album_config(int *autoplay, int *i){
  if (LittleFS.exists(ALBUM_PATH)){
    File fp = LittleFS.open(ALBUM_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *i = obj[String("i_i")].as<int>();
    *autoplay = obj[String("autoplay")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_album_config(int autoplay ,int i){
    File fp = LittleFS.open(ALBUM_PATH, "w");
    char settings[32] = {0};
    snprintf(settings, sizeof(settings), "{\"autoplay\":%d,\"i_i\":%d}", autoplay, i);
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    DBG_PTN(settings);
    return 0;
}

int set_kline_config(String kline){
    File fp = LittleFS.open(KLINE_PATH, "w");
    if(!fp) return 0;
    char settings[32] = {0};
    snprintf(settings, sizeof(settings), "{\"k\":\"%s\"}", kline.c_str());
    fp.write((uint8_t*)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_kline_config(String *k){
  if (LittleFS.exists(KLINE_PATH)){
    File fp = LittleFS.open(KLINE_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    //DBG_PTN(settings);

    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *k = obj[String("k")].as<String>();
    return 0;
  }
  return -1;
}
int read_stock_kline_config(String *k){
  if (LittleFS.exists(STOCK_KLINE_PATH)){
    File fp = LittleFS.open(STOCK_KLINE_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    //DBG_PTN(settings);

    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *k = obj[String("st_kline")].as<String>();
    return 0;
  }
  return -1;
}

int set_stock_kline_config(String kline){
    File fp = LittleFS.open(STOCK_KLINE_PATH, "w");
    if(!fp) return 0;
    char settings[32] = {0};
    snprintf(settings, sizeof(settings), "{\"st_kline\":\"%s\"}", kline.c_str());
    fp.write((uint8_t*)settings, strlen(settings));
    fp.close();
    return 0;
}

int set_stock_bg(String bg){
    File fp = LittleFS.open(STOCK_BG_PATH, "w");
    char settings[512] = {0};
    snprintf(settings, sizeof(settings), "{\"ticker_bg\":\"%s\"}",bg.c_str());
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_stock_bg(String *bg){
  if (LittleFS.exists(STOCK_BG_PATH)){
    File fp = LittleFS.open(STOCK_BG_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    DBG_PTN(settings);

    JsonDocument doc;
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *bg = obj[String("ticker_bg")].as<String>();
    return 0;
  }
  return -1;
}

int set_stock_color(String s_c, String p_c){
    File fp = LittleFS.open(STOCK_COLOR_PATH, "w");
    char settings[512] = {0};
    snprintf(settings, sizeof(settings), "{\"s_c\":\"%s\",\"p_c\":\"%s\"}",s_c.c_str(),p_c.c_str());
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_stock_color(String *s_c, String *p_c){
  if (LittleFS.exists(STOCK_COLOR_PATH)){
    File fp = LittleFS.open(STOCK_COLOR_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    DBG_PTN(settings);

    JsonDocument doc;
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *s_c = obj[String("s_c")].as<String>();
    *p_c = obj[String("p_c")].as<String>();
    return 0;
  }
  return -1;
}

int set_stock_config(int ani, int loop, int i, const char *c0, const char *c1, const char *c2, const char *c3, const char *c4, const char *c5, const char *c6, const char *c7, const char *c8, const char *c9){
    File fp = LittleFS.open(STOCK_PATH, "w");
    char settings[512] = {0};
    snprintf(settings, sizeof(settings), "{\"s_ani\":\"%d\",\"s_l\":\"%d\",\"s_i\":\"%d\",\"c0\":\"%s\", \"c1\":\"%s\", \"c2\":\"%s\", \"c3\":\"%s\", \"c4\":\"%s\", \"c5\":\"%s\", \"c6\":\"%s\", \"c7\":\"%s\", \"c8\":\"%s\", \"c9\":\"%s\"}",ani, loop, i, c0, c1, c2, c3, c4, c5, c6, c7, c8, c9);
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}
int read_stock_config(int *ani, int *loop, int *i, char *c0, char *c1, char *c2,char *c3,char *c4,char *c5,char *c6,char *c7,char *c8,char *c9, int code_len){
  if (LittleFS.exists(STOCK_PATH)){
    File fp = LittleFS.open(STOCK_PATH, "r");
    String settings = fp.readString();

    DynamicJsonDocument doc(512);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *ani = obj[String("s_ani")].as<int>();
    *loop = obj[String("s_l")].as<int>();
    *i = obj[String("s_i")].as<int>();
    snprintf(c0, code_len, "%s", obj[String("c0")].as<String>().c_str());
    snprintf(c1, code_len, "%s", obj[String("c1")].as<String>().c_str());
    snprintf(c2, code_len, "%s", obj[String("c2")].as<String>().c_str());
    snprintf(c3, code_len, "%s", obj[String("c3")].as<String>().c_str());
    snprintf(c4, code_len, "%s", obj[String("c4")].as<String>().c_str());
    snprintf(c5, code_len, "%s", obj[String("c5")].as<String>().c_str());
    snprintf(c6, code_len, "%s", obj[String("c6")].as<String>().c_str());
    snprintf(c7, code_len, "%s", obj[String("c7")].as<String>().c_str());
    snprintf(c8, code_len, "%s", obj[String("c8")].as<String>().c_str());
    snprintf(c9, code_len, "%s", obj[String("c9")].as<String>().c_str());
    fp.close();
    return 0;
  }
  return -1;
}

int read_coin_config(int *ani, int *loop, int *i, char *c0, char *c1, char *c2, char *c3,char *c4,char *c5,char *c6,char *c7,char *c8,char *c9,int code_len){
  if (LittleFS.exists(COIN_PATH)){
    File fp = LittleFS.open(COIN_PATH, "r");
    String settings = fp.readString();

    DynamicJsonDocument doc(512);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *ani = obj[String("c_ani")].as<int>();
    *loop = obj[String("c_l")].as<int>();
    *i = obj[String("c_i")].as<int>();
    snprintf(c0, code_len, "%s", obj[String("cin0")].as<String>().c_str());
    snprintf(c1, code_len, "%s", obj[String("cin1")].as<String>().c_str());
    snprintf(c2, code_len, "%s", obj[String("cin2")].as<String>().c_str());
    snprintf(c3, code_len, "%s", obj[String("cin3")].as<String>().c_str());
    snprintf(c4, code_len, "%s", obj[String("cin4")].as<String>().c_str());
    snprintf(c5, code_len, "%s", obj[String("cin5")].as<String>().c_str());
    snprintf(c6, code_len, "%s", obj[String("cin6")].as<String>().c_str());
    snprintf(c7, code_len, "%s", obj[String("cin7")].as<String>().c_str());
    snprintf(c8, code_len, "%s", obj[String("cin8")].as<String>().c_str());
    snprintf(c9, code_len, "%s", obj[String("cin9")].as<String>().c_str());
    fp.close();
    return 0;
  }
  return -1;
}

int set_coin_config(int ani, int loop, int i, const char *c0, const char *c1, const char *c2, const char *c3,const char *c4,const char *c5,const char *c6,const char *c7,const char *c8,const char *c9){
    File fp = LittleFS.open(COIN_PATH, "w");
    char settings[512] = {0};
    snprintf(settings, sizeof(settings), "{\"c_ani\":\"%d\",\"c_l\":\"%d\",\"c_i\":\"%d\",\"cin0\":\"%s\", \"cin1\":\"%s\", \"cin2\":\"%s\", \"cin3\":\"%s\", \"cin4\":\"%s\", \"cin5\":\"%s\", \"cin6\":\"%s\", \"cin7\":\"%s\", \"cin8\":\"%s\", \"cin9\":\"%s\"}", ani, loop, i, c0, c1, c2, c3, c4,c5,c6,c7,c8,c9);
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

#if 0
int read_stock_config(char *code, int code_len, char *exchange, int exchange_len, int *s_i){
  if (LittleFS.exists(STOCK_PATH)){
    File fp = LittleFS.open(STOCK_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    snprintf(code, code_len, "%s", obj[String("code")].as<String>().c_str());
    snprintf(exchange, exchange_len, "%s", obj[String("exchange")].as<String>().c_str());
    *s_i = obj[String("s_i")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_stock_config(const char *code, const char *exchange, int s_i){
    File fp = LittleFS.open(STOCK_PATH, "w");
    char settings[64] = {0};
    snprintf(settings, sizeof(settings), "{\"code\":\"%s\", \"exchange\":\"%s\", \"s_i\":\"%d\"}", code, exchange, s_i);
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}
#endif

int read_time_color_config(String &h, String &m , String &s, String &c){
  if (LittleFS.exists(TIME_COLOR_PATH)){
    File fp = LittleFS.open(TIME_COLOR_PATH, "r");
    String settings = fp.readString();
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    h = obj[String("hc")].as<String>();
    m = obj[String("mc")].as<String>();
    s = obj[String("sc")].as<String>();
    c = obj[String("cc")].as<String>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_time_color_config(String h, String m, String s,String c){
    //File fp = SPIFFS.open(BRT_PATH, "w");
    File fp = LittleFS.open(TIME_COLOR_PATH, "w");
    char settings[128] = {0};
    snprintf(settings, sizeof(settings), "{\"hc\":\"%s\",\"mc\":\"%s\",\"sc\":\"%s\",\"cc\":\"%s\"}", h.c_str(), m.c_str(), s.c_str(),c.c_str());
    fp.write((uint8_t *)settings, strlen(settings));
    fp.close();
    return 0;
}

int read_dst_config(int *i){
  if (LittleFS.exists(DST_PATH)){
    File fp = LittleFS.open(DST_PATH, "r");
    if(!fp) return 0;
    String settings = fp.readString();
    DynamicJsonDocument doc(128);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *i =  obj[String("dst")].as<int>();
    fp.close();
    return 0;
  }
  return -1;
}

int set_dst_config(int i){
    File fp = LittleFS.open(DST_PATH,"w");
    if(!fp) return 0;
    char settings[64] = {0};
    snprintf(settings, sizeof(settings), "{\"dst\":%d}", i);
    fp.write((uint8_t*)settings, strlen(settings));
    fp.close();
    return 0;
}
#if 0
int read_time_color_config(uint16_t *h, uint16_t *m , uint16_t *s){
  //if (SPIFFS.exists(BRT_PATH)){
  if (LittleFS.exists(TIME_COLOR_PATH)){
    //File fp = SPIFFS.open(BRT_PATH, "r");
    File fp = LittleFS.open(TIME_COLOR_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    *h = obj[String("h")].as<int>();
    *m = obj[String("m")].as<int>();
    *s = obj[String("s")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}

int set_time_color_config(uint16_t h, uint16_t m, uint16_t s){
    //File fp = SPIFFS.open(BRT_PATH, "w");
    File fp = LittleFS.open(TIME_COLOR_PATH, "w");
    char settings[64] = {0};
    snprintf(settings, sizeof(settings), "{\"h\":\"%d\",\"m\":\"%d\",\"s\":\"%d\"}", h, m, s);
    fp.write((uint8_t *)settings, sizeof(settings));
    fp.close();
    return 0;
}
#endif

int saveThemeList(const char* themeList, int en, int interval) {
  // 打开文件（如果文件不存在则创建）
  File fp = LittleFS.open(THEME_LIST_PATH, "w");
  if (!fp) {
    //DBG_PTN("无法打开文件");
    return -1;
  }
  char settings[128] = {0};
  snprintf(settings, sizeof(settings), "{\"list\":\"%s\",\"sw_en\":\"%d\",\"sw_i\":\"%d\"}", themeList, en, interval);
  fp.write((uint8_t *)settings, strlen(settings));
  // 关闭文件
  fp.close();
  //DBG_PTN("主题列表已保存到文件");
  return 0;
}

int loadThemeList(char* themeList, size_t themeListSize, int *en, int *interval) {
  if (LittleFS.exists(THEME_LIST_PATH)){
    //File fp = SPIFFS.open(BRT_PATH, "r");
    File fp = LittleFS.open(THEME_LIST_PATH, "r");
    String settings = fp.readString();
    DBG_PTN(settings);
    
    DynamicJsonDocument doc(256);
    deserializeJson(doc, settings);
    JsonObject obj = doc.as<JsonObject>();

    snprintf(themeList, themeListSize, "%s", obj[String("list")].as<String>().c_str());
    *en = obj[String("sw_en")].as<int>();
    *interval = obj[String("sw_i")].as<int>();
    fp.close();
    return 0;
  }  
  return -1;
}

//char ssid[64] = {0};
extern char ssid[32];
char pwd[65] = {0};
char city[16]={0};
char city_font[4]={0};
char code[16]={0};
String cityName = "";
String cityFont = "1";
String cityCode = "1835848"; 

extern int brt;
extern int gif_index;
extern int theme_index;
extern int album_time;
extern int autoplay;
extern int last_theme_index;
extern uint16_t h1_color, m1_color, s1_color;

extern String bili_id;
extern String stock_code;
extern String stock_exchange;

extern int timer_brt_en;
extern int t1,t2,b2;
extern int hour12;
extern int delay_wifi_time;
extern String my_ntp_server;
extern String unit_format;

extern String h_color; 
extern String m_color;
extern String s_color;
char bili[16];
extern uint16_t h1_color;
extern uint16_t m1_color;
extern uint16_t s1_color;
void update_time_colors() {
  uint32_t h_color_32;
  uint32_t m_color_32;
  uint32_t s_color_32;
  sscanf(h_color.c_str(), "#%X", &h_color_32);
  sscanf(m_color.c_str(), "#%X", &m_color_32);
  sscanf(s_color.c_str(), "#%X", &s_color_32);
  //todo
  //h1_color = mdisplay.color24to16(h_color_32);
  //m1_color = mdisplay.color24to16(m_color_32);
  //s1_color = mdisplay.color24to16(s_color_32);
}

extern int enable_theme_loop;
extern int loop_interval;
extern int user_year;
extern int user_month;
extern int user_day;
extern int colon;
extern int myfont;
extern int weather_interval;
extern String gif_path;

#include "../app/weather/weather_en.h"
extern struct theme_loop theme_loop_list[];

extern String temp_unit;
extern String windspeed_unit;
extern String pressure_unit;
extern String pub_weather_key;
extern String weather_key;
extern String my_ntp_server;
extern String autoplay_path;
extern String flip_index;
extern String font_path;
extern int dst_enable;
extern int day_format;
extern int page_index;
extern int timeZone;     //东八区
extern int minutesTimeZone;//分钟的时区偏移，还有相差半小时的时区

int init_config(){
  read_font_config(&font_path);
  read_delay_config(&delay_wifi_time);
  read_ntp_config(&my_ntp_server);
  read_key_config(&weather_key);
  if(weather_key.length() < 30){
    weather_key = pub_weather_key;
    DBG_PTN("use default interval");
    weather_interval  = 1200;//20分钟更新频率
  }else{
    read_weather_interval_config(&weather_interval);
    DBG_PTN("use user interval:" + String(weather_interval));
    DBG_PTN(weather_key);
  }
 
  read_unit_config(&windspeed_unit, &temp_unit, &pressure_unit);
  read_brt_config(&brt);
  read_gif_config(gif_path);
  //read_img_config(&autoplay_path);
  read_album_config(&autoplay, &album_time);
  read_hour12_config(&hour12);
  read_colon_config(&colon);
  read_timer_brt_config(&timer_brt_en, &t1, &t2, &b2);
  read_daytimer_config(&user_year, &user_month, &user_day);
  read_dst_config(&dst_enable);
  read_day_config(&day_format);

  char theme_list[20];
  int ret = loadThemeList(theme_list, sizeof(theme_list), &enable_theme_loop, &loop_interval);
  if(ret == 0){
    for(int i = 0, j=0; j < 7; i=i+2, j++){
      if(theme_list[i] == '1')
        theme_loop_list[j].loop_en = 1; 
      else
        theme_loop_list[j].loop_en = 0; 
    }
  }
  #if 0
  int b_i = 0;
  ret = read_bili_config(bili, sizeof(bili), &b_i);
  if(ret == 0){
    bili_id = String(bili);
  }

  char code[32];
  char exchange[32];
  int s_i = 0;
  ret = read_stock_config(code, sizeof(code), exchange, sizeof(exchange), &s_i);
  if(ret == 0){
    stock_code = String(code);
    stock_exchange = String(exchange);
  }
  #endif
  read_theme_config(&theme_index);
  if(theme_index <= 0) theme_index = 1;
  if(theme_index > THEME_TOTAL) theme_index = 1;
  //read_time_color_config(&h_color, &m1_color, &s1_color);
 
  read_page_index_config(&page_index);
  if(theme_index == 1){
    if(page_index == 2) flip_index = "";
  }

  update_time_colors();

  read_wifi_config(ssid, sizeof(ssid), pwd, sizeof(pwd));
  char citycode[32] = {0};
  char cityname[32] = {0};
  char location[64] = {0};
  ret = read_city_config(cityname, sizeof(citycode), citycode, sizeof(cityname), location, sizeof(location));
  if (ret == 0) {
    cityName = String(cityname);
    cityCode = String(citycode);
  }
  return 0;
}

const char* bootCountFile = "/bt.txt";

int readBootCount() {
  File file = LittleFS.open(bootCountFile, "r");
  if (!file) {
    return 0; // 默认值为0，如果文件不存在
  }
  int bootCount = file.parseInt();
  file.close();
  return bootCount;
}

void writeBootCount(int count) {
  File file = LittleFS.open(bootCountFile, "w");
  if (file) {
    file.print(count);
    file.close();
  }
}

void updateBootCount() {
  int bootCount = readBootCount();
  DBG_PTN("boot = "  + String(bootCount));
  bootCount++; // 每次开机自增
  writeBootCount(bootCount);
}