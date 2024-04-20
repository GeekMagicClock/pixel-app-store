#include <memory>
#include <pgmspace.h>
#include "my_debug.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
//#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);
#include "web/html_system.h"
#include "web/html_image.h"
#include "web/html_ticker.h"
#include "web/html_time.h"
#include "web/html_network.h"
#include "web/html_weather.h"
#include "web/html_index.h"

#include "web/js_settings.h"
#include "web/js_time.h"
#include "web/js_network.h"
#include "web/js_weather.h"
#include "web/js_system.h"
#include "web/css_style.h"

String getContentType(String filename){
  if(filename.endsWith(".htm")) return F("text/html");
  else if(filename.endsWith(".html"))return F("text/html");
  else if(filename.endsWith(".html.gz")) return F("text/html");
  else if(filename.endsWith(".css")) return F("text/css");
  else if(filename.endsWith(".css.gz")) return F("text/css");
  else if(filename.endsWith(".js")) return F("text/javascript");
  else if(filename.endsWith(".js.gz")) return F("text/javascript");
  else if(filename.endsWith(".jpg")) return F("image/jpeg");
  else if(filename.endsWith(".gif")) return F("image/gif");
  return F("text/plain");
}

void notFound(AsyncWebServerRequest *request) {
      DBG_PTN(request->url());
  if (request->url().startsWith("/")) {
    String fileUri = request->url();
    String fileExtension = fileUri.substring(fileUri.lastIndexOf('.'));

    if (fileExtension == ".js" || fileExtension == ".html" || fileExtension == ".css") {
      String gzFileUri = fileUri + ".gz";
      if(LittleFS.exists(gzFileUri)) {
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, gzFileUri, getContentType(gzFileUri));
        response->addHeader("Cache-Control", "public, max-age=3600"); // Cache for 1 hour (3600 seconds)
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
      } else{
        DBG_PTN("1not found");
        DBG_PTN(request->url());
        request->send(404);
      }
    }else{
      if(LittleFS.exists(fileUri)){
        request->send(LittleFS, fileUri, getContentType(fileUri));
      }else{
        DBG_PTN("2not found");
        DBG_PTN(request->url());
        request->send(404);
      }
    }
  } else {
    request->send(404);
  }
}

bool isImageFile(String filename) {
    filename.toLowerCase();
    return filename.endsWith(".jpeg") ||filename.endsWith(".jpg") || filename.endsWith(".gif");
}

const String list_img() {
    String filelist = "[";
    File root = LittleFS.open("/image");
    if (!root || !root.isDirectory()) {
        return "None";
    }

    bool firstFile = true;
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory() || !isImageFile(file.name())) {
            file = root.openNextFile();
            continue;
        }
        if (!firstFile) {
            filelist += ",";
        } else {
            firstFile = false;
        }
        filelist += "{\"s\":\"";
        filelist += file.name();
        filelist += "\"}";
        file = root.openNextFile();
    }
    filelist += "]";
    
    // 检查 JSON 格式的合法性
    // 通过尝试解析 JSON 数据来验证其格式是否正确
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, filelist);
    if (error) {
        return "Invalid JSON";
    }

    return filelist;
}

const String listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
    String filelist = "";
    String partlist;
    int i = 0;
    int gif = 0;//是否只显示gif文件
    if(!strcasecmp(dirname, "/gif")) gif = 1;
     Serial.printf("Listing directory: %s\r\n", dirname);
    File root;
    if(dirname == NULL || dirname[0] == '\0')
        root = fs.open("/image","r");
    else
        root = fs.open(dirname, "r");

    //DBG_PTN(dirname);
    if (!root) {
        //DBG_PTN("- failed to open directory");
        return "Empty";
    }
    if (!root.isDirectory()) {
        //DBG_PTN(" - not a directory");
        return "not";
    }

    File file = root.openNextFile();
    if (!file) {
        //DBG_PTN("- failed to open next file");
        return "Empty";
    }
    while (file) {
    #if 0
        if (file.isDirectory()) {
        //  Serial.print("  DIR : ");
        // DBG_PTN(file.name());
        if (levels) {
            listDir(fs, file.name(), levels - 1);
        }
        } else 
    #endif    
        {
            i++;
            String st_after_symb = String(file.name()).substring(String(file.name()).lastIndexOf("/") + 1);

            //Serial.print(file.name());
            
            if (file.isDirectory()) 
                  partlist +=  ("<tr><td>") + String(i) + ("</td><td>") + (("<a href='javascript:void(0);' path='")) + String(file.name()) + (("' onclick='listDir(this);return false;'>")) + st_after_symb + ("</a></td><td> - </td><td>") + (" ") + ("</td></tr>");
            else
                //if(gif)
                partlist +=  ("<tr><td>") + String(i) + ("</td><td>") + ("<a href='") + String(dirname) + String("/") + String(file.name()) + ("'>") + st_after_symb + ("</a></td><td>") + String(file.size() / 1024) + ("</td><td>") + ("<input type='button' onclick=\"deletef('") +String(dirname)+String("/")+ String(file.name()) + ("')\" value='X'>") + ("</td><td>") + ("<input type='button' onclick=\"setgif('") +String(dirname)+String("/")+ String(file.name()) + ("')\" value='Set'>")+ ("</td></tr>");
                //partlist +=  ("<tr><td>") + String(i) + ("</td><td>") + ("<img width=\"64\" height=\"32\" src='") + String(dirname) + String("/") + String(file.name()) + ("'>") + st_after_symb + ("</a></td><td>") + String(file.size() / 1024) + ("</td><td>") + ("<input type='button' class='btndel' onclick=\"deletef('") +String(dirname)+String("/")+ String(file.name()) + ("')\" value='Delete'>") + ("</td><td>") + ("<input type='button' class='btndel' onclick=\"setgif('") +String(dirname)+String("/")+ String(file.name()) + ("')\" value='Set'>")+ ("</td></tr>");
                //else
                //partlist +=  F("<tr><td>") + String(i) + F("</td><td>") + F("<a href='") + String(dirname) + String("/") + String(file.name()) + F("'>") + st_after_symb + F("</a></td><td>") + String(file.size() / 1024) + F("</td><td>") + F("<input type='button' class='btndel' onclick=\"deletef('") +String(dirname)+String("/")+ String(file.name()) + F("')\" value='X'>") + F("</td></tr>");
            //filelist = String("<table id='list'><tbody><tr><th>#</th><th>File name</th><th>Size(KB)</th><th></th></tr>") + partlist + String(" </tbody></table>");
        }
        file = root.openNextFile();
    }
    //DBG_PTN(partlist);
    filelist = String(("<table id='list'><tbody><tr><th>#</th><th>Name</th><th>Size(KB)</th><th></th><th></th></tr>")) + partlist + (" </tbody></table>");
    return filelist;
}

static int getRSSIasQuality(int RSSI) {
  int quality = 0;

  if (RSSI <= -100) {
    quality = 0;
  } else if (RSSI >= -50) {
    quality = 100;
  } else {
    quality = 2 * (RSSI + 100);
  }
  return quality;
}

#include <mDNS.h>
#include <DNSServer.h>

void init_http_server();

static const byte DNS_PORT = 53;
static DNSServer dnsServer;

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request){
    //request->addInterestingHeader("ANY");
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request) {

    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (uint8_t*)index_html, sizeof(index_html)); response->addHeader("Content-Encoding", "gzip"); 
    request->send(response);
  }
};

boolean startPortal(char const *apName, char const *apPassword) {

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName, apPassword);//password option

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  init_http_server();
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);//only when requested from AP
  while(1){
    dnsServer.processNextRequest();
  }
}

String get_wifi_scan(){
//异步扫描配合异步webserver, 开机后第一次扫描是空的，需要一些技巧避免
    String data = "";
    int n = WiFi.scanComplete();
    if(n == -2){
      n = WiFi.scanNetworks(true);
    } else {
      Serial.print(n);
      DBG_PTN("networks found");
      //display networks in page
      if(n>15) n = 15; 

      JsonDocument root;
      JsonArray array = root["aps"].to<JsonArray>();
 
      //String data = "{\"aps\":[";
      for (int i = 0; i < n; i++) {
        int quality = getRSSIasQuality(WiFi.RSSI(i));
        //20 经验值,取自开源版
        //if (24 < quality) 
        {
            String rssiQ;
            rssiQ += quality;
            //item.replace("{v}", WiFi.SSID(indices[i]));
            array[i]["c"] =  String(WiFi.channel(i));
            array[i]["ss"] = WiFi.SSID(i);
            if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) {
            //if (WiFi.encryptionType(i) != AUTH_OPEN) {
              array[i]["e"] = 1;
              //data += "\"e\":1,";
            } else {
              array[i]["e"] = 0;
            }
            int percentage = map(WiFi.RSSI(i), -99, -35, 1, 100);
            // 确保百分比在有效范围内（1% 到 100%）
            percentage = constrain(percentage, 1, 100);
            array[i]["r"] = (percentage);
            //array[i]["r"] = String(percentage)+"%";
        } 
      }
      WiFi.scanDelete();
      if(WiFi.scanComplete() == -2){
        DBG_PTN("scan again...");
        //WiFi.scanNetworks(true);
      }
      serializeJson(root,  data);
    }
    DBG_PTN(data);
    return data;
}

size_t content_len;
File file;
bool opened = false;
void handleDoUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
        content_len = request->contentLength();
        Serial.printf("UploadStart: %s\n", filename.c_str());
    }
    String dir = request->getParam("dir")->value();

    if (opened == false) {
        opened = true;
        if(dir == "" || dir == "/") dir = "/image";
        Serial.printf("upload dir: %s, file %s\n", dir.c_str(), filename.c_str());
        file = LittleFS.open(dir + "/" + filename, "w");
        if (!file) {
            Serial.println("- failed to open file for writing");
            return;
        }
    }

    if (file.write(data, len) != len) {
        Serial.println("- failed to write");
        return;
    }

    if (final) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Ok");
        response->addHeader("Refresh", "20");
        response->addHeader("Location", "/filesystem");
        request->send(response);
        file.close();
        opened = false;
        Serial.println("Upload complete");
    }
}

void deleteFile(fs::FS &fs, const String& path) {
  Serial.printf("Deleting file: %s\r\n", path);
  //不允许删除根目录下的文件
  if(path.lastIndexOf("/") == 0) return;

  if (fs.remove(path)) {
    Serial.println("- file deleted");
  } else {
    Serial.println("- delete failed");
  }
}

extern String cityName, cityFont, cityCode;
extern int theme_index;
extern int tmp_theme_index;
extern uint16_t h1_color;
extern uint16_t m1_color;
extern uint16_t s1_color;
int force_time_display = 0; //是否强制更新时间显示
extern int album_time;
extern int autoplay;
extern String autoplay_path;
String stock_code, stock_exchange;
extern int t1,t2,b1,b2,timer_brt_en;
extern int hour12;
extern int brt;
int delay_wifi_time;
extern String my_ntp_server;
#include "../app/weather/weather_en.h"
#include <TimeLib.h>
int loop_interval;
int enable_theme_loop;
int user_year;
int user_month;
int user_day;
int colon;// 是否显示冒号
int myfont;
#include "theme.h"
#include "display.h"
extern struct theme_loop theme_loop_list[THEME_TOTAL];
extern String c_color;
extern String h_color;
extern String m_color;
extern String s_color;
String gif_path;
extern String temp_unit;
extern String windspeed_unit;
extern String pressure_unit;
extern String weather_key;
extern String ticker_bg;
extern int weather_interval;
int dst_enable;
int day_format;
extern Weather wea;
int rot;
extern void set_screen_brt(int brt);
#include "settings.h"
#include "times.h"
extern int timeZone;
extern int minutesTimeZone;
void handleSet(AsyncWebServerRequest * request){
  String message = "OK";
  if (request->hasArg("cd1")){
    String city  = request->arg("cd1");
    if(city != cityCode){
      cityCode = city;
      DBG_PTN(city);
      set_city_config("", city.c_str(), "");
      wea.cityCode = cityCode;
      wea.update(true);
      force_time_display = 1;
      sync_time(true);
    }
   //update_weather(true);
  }else if (request->hasArg("tz")||request->hasArg("mtz")) {//rotation screen
    timeZone = request->arg("tz").toInt();
    minutesTimeZone = request->arg("mtz").toInt();
    set_timezone_config(timeZone,minutesTimeZone);
  }else if (request->hasArg("rot")) {//rotation screen
    rot = request->arg("rot").toInt();
  }else if (request->hasArg("img")) {
    String tmp_img_path = request->arg("img");
    if(LittleFS.exists(tmp_img_path))  
      autoplay_path = tmp_img_path;
    else {
      message = "FAIL";
      request->send(200, "text/html", message);
      return;
    }
    autoplay = 0;
    //自动设置为不轮播模式
    set_album_config(autoplay, album_time);
    set_img_config(autoplay_path);
  }else if (request->hasArg("gif")) {
    String tmp_gif_path = request->arg("gif");
    if(LittleFS.exists(tmp_gif_path))  
      gif_path = tmp_gif_path;
    else
      return;
    //gifDeinit();    
    //gifChange();
    set_gif_config(gif_path);
   }else if (request->hasArg("w_u")&&request->hasArg("t_u")&&request->hasArg("p_u")) {
    windspeed_unit = request->arg("w_u");
    temp_unit = request->arg("t_u");
    pressure_unit = request->arg("p_u");
    set_unit_config(request->arg("w_u"), request->arg("t_u"), request->arg("p_u")); 
    force_time_display = 1;
  }else if (request->hasArg("key")) {
    weather_key = request->arg("key");
    //Serial.printf("key:[%s]\r\n",weather_key);
    if(weather_key == ""){
      //Serial.printf("error key");
    }else {
      set_key_config(weather_key);
      //update_weather(true);
      wea.key = weather_key;
      wea.update(false);
      //weather_changed = true;
    }
   }else if (request->hasArg("dst")) {
    if(request->arg("dst") == "1")
      dst_enable = 1;
    else
      dst_enable = 0;
    set_dst_config(dst_enable); 
    //update_time();
  }else if (request->hasArg("w_i")) {
    int tmp_w_i = request->arg("w_i").toInt();
    if(tmp_w_i < 5) tmp_w_i = 5;
    //在用户已经设置KEY的情况下，更新间隔才生效
    String key = "";
    read_key_config(&key);
    if(key != ""){
        weather_interval = tmp_w_i;
        wea.updateInterval = weather_interval*60*1000;
    }
    //Serial.printf("key:[%s]\r\n",weather_key);
    set_weather_interval_config(tmp_w_i);
  }else if (request->hasArg("i_i") && request->hasArg("autoplay")) {
    album_time = request->arg("i_i").toInt();
    autoplay = request->arg("autoplay").toInt();
    DBG_PTN(autoplay);
    set_album_config(autoplay, album_time);
  }else if (request->hasArg("font")) {
    myfont = request->arg("font").toInt();
    //set_font_config(myfont);
  }else if (request->hasArg("brt")) {
    brt  = request->arg("brt").toInt();
    set_screen_brt(brt);
    set_brt_config(brt);
  }else if (request->hasArg("ntp")) {
    my_ntp_server = request->arg("ntp");
    Serial.printf("ntp:[%s]\r\n",my_ntp_server.c_str());
    if(my_ntp_server == ""){
    }else {
      set_ntp_config(my_ntp_server);
      //setSyncProvider(getNtpTime);
      //weather_changed = true;
      //todo
    }
 }else if (request->hasArg("delay")) {
    delay_wifi_time  = request->arg("delay").toInt();
    //Serial.printf("delay_wifi:[%d]\r\n", delay_wifi_time);
    set_delay_config(delay_wifi_time);
  }else if (request->hasArg("reset")) {
    reset_config();
    ESP.restart();
  }else if (request->hasArg("reboot")) {
    ESP.restart();
  } else if (request->hasArg("colon")) {
    colon = request->arg("colon").toInt();
    set_colon_config(colon);
    force_time_display = 1;
  } else if (request->hasArg("hour")) {
    hour12  = request->arg("hour").toInt();
    set_hour12_config(hour12);
    force_time_display = 1;
  }else if (request->hasArg("theme")) {
    tmp_theme_index = request->arg("theme").toInt();
    Serial.printf("theme:[%d]\r\n", theme_index);
    //退出相册轮播
    //if(theme_index != THEME_ALBUM) app_exit = 1;
  }else if (request->hasArg("theme_list") && request->hasArg("sw_en")&& request->hasArg("theme_interval")) {
    //Serial.printf("theme:[%d]\r\n", theme_index);
    char theme_list[20];
    request->arg("theme_list").toCharArray(theme_list, sizeof(theme_list));
    //DBG_PTN(theme_list);
    for(int i=0, j=0; j<THEME_TOTAL; i=i+2,j++){
      if(theme_list[i] == '1'){
        theme_loop_list[j].loop_en = 1;
      }else{
        theme_loop_list[j].loop_en = 0;
      }
    }
    enable_theme_loop = request->arg("sw_en").toInt();
    DBG_PTN(enable_theme_loop);
    loop_interval = request->arg("theme_interval").toInt();
    //限制下切换频率
    if(loop_interval < 11) loop_interval = 10;
    saveThemeList(theme_list, enable_theme_loop, loop_interval);
  }else if (request->hasArg("yr") && request->hasArg("mth") && request->hasArg("day")) {
    user_year = request->arg("yr").toInt();
    user_month = request->arg("mth").toInt();
    user_day = request->arg("day").toInt();
    set_daytimer_config(request->arg("yr").toInt(),request->arg("mth").toInt(),request->arg("day").toInt());
    force_time_display = 1;
  } else if(request->hasArg("day")){
    day_format = request->arg("day").toInt();
    force_time_display = 1;
    set_day_config(day_format); 
  }else if (request->hasArg("cc") && request->hasArg("hc") && request->hasArg("mc") && request->hasArg("sc")) {
    c_color = request->urlDecode(request->arg("cc")).substring(1);//#AABBCC -> AABBCC
    h_color = request->urlDecode(request->arg("hc")).substring(1);
    m_color = request->urlDecode(request->arg("mc")).substring(1);
    s_color = request->urlDecode(request->arg("sc")).substring(1);
 
    //Serial.printf("hc:[%X]\r\n",h1_color);
    set_time_color_config(h_color, m_color, s_color, c_color);
    //update_time_colors();
    force_time_display = 1;
 }else if (request->hasArg("uid") && request->hasArg("b_i")) {
    //bili_id = request->arg("uid");
    //Serial.printf("bili:[%s]\r\n",bili_id);
    //set_bili_config(bili_id.c_str(), request->arg("b_i").toInt());
  }else if(request->hasArg("st_kline")){
    set_stock_kline_config(request->arg("st_kline"));
  }else if (request->hasArg("ticker_bg")){
    ticker_bg = "/image/"+request->arg("ticker_bg");
    set_stock_bg(ticker_bg);
    DBG_PTN(ticker_bg);
  }else if (request->hasArg("c0") &&request->hasArg("c1") && request->hasArg("c2")&& request->hasArg("c3")&& request->hasArg("c4")&& request->hasArg("c5")&& request->hasArg("c6")&& request->hasArg("c7")&& request->hasArg("c8")&& request->hasArg("c9") && request->hasArg("s_i")&& request->hasArg("s_l")&& request->hasArg("s_ani")) {
    set_stock_config(request->arg("s_ani").toInt(), request->arg("s_l").toInt(),request->arg("s_i").toInt(),request->arg("c0").c_str(),request->arg("c1").c_str(),request->arg("c2").c_str(),request->arg("c3").c_str(),request->arg("c4").c_str(),request->arg("c5").c_str(),request->arg("c6").c_str(),request->arg("c7").c_str(),request->arg("c8").c_str(),request->arg("c9").c_str());
  }else if(request->hasArg("en") && request->hasArg("t2") && request->hasArg("b2") && request->hasArg("t1")){
    t1 = request->arg("t1").toInt();
    t2 = request->arg("t2").toInt();
    //b1 = request->arg("b1").toInt();
    b2 = request->arg("b2").toInt();
    timer_brt_en = request->arg("en").toInt();
    /* 小时发生变化，立刻更新小时 */
    //last_hour = !last_hour;
    //恢复正常亮度
    if(timer_brt_en == 0) set_screen_brt(brt);
    set_timer_brt_config(timer_brt_en, t1, t2, b2);
    Serial.printf("en:[%d]%d,%d,%d\r\n",timer_brt_en, t1, t2,b2);
  }else{
    message = "FAIL";
  }

 request->send(200, "text/html", message);
 return;
}

#include "Update.h"
//update then reboot
bool shouldReboot = false;
extern char ssid[33];
extern char password[65];

const char HTTP_HEADER[] PROGMEM          = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/><title>{v}</title>";
const char HTTP_STYLE[] PROGMEM           = "<style>.c{text-align: center;} div,input{padding:5px;font-size:1em;} input{width:95%;} body{text-align: center;font-family:verdana;} button{border:0;border-radius:0.3rem;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%;} .q{float: right;width: 64px;text-align: right;} .l{background: url(\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAMAAABEpIrGAAAALVBMVEX///8EBwfBwsLw8PAzNjaCg4NTVVUjJiZDRUUUFxdiZGSho6OSk5Pg4eFydHTCjaf3AAAAZElEQVQ4je2NSw7AIAhEBamKn97/uMXEGBvozkWb9C2Zx4xzWykBhFAeYp9gkLyZE0zIMno9n4g19hmdY39scwqVkOXaxph0ZCXQcqxSpgQpONa59wkRDOL93eAXvimwlbPbwwVAegLS1HGfZAAAAABJRU5ErkJggg==\") no-repeat left center;background-size: 1em;}</style>";
const char HTTP_SCRIPT[] PROGMEM          = "<script>function c(l){document.getElementById('s').value=l.innerText||l.textContent;document.getElementById('p').focus();}</script>";
const char HTTP_HOME[] PROGMEM            = "<form action=\"/\" method=\"get\"><button>首页</button></form><br/>";
const char HTTP_END[] PROGMEM             = "</div></body></html>";
const char HTTP_HEADER_END[] PROGMEM        = "</head><body><div style='text-align:left;display:inline-block;min-width:260px;'>";
static const char serverIndex[] PROGMEM =
R"(<!DOCTYPE html>
<html lang='en'>
<head>
    <meta charset='utf-8'>
    <meta name='viewport' content='width=device-width,initial-scale=1'/>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
            background-color: #2e2e2e;
            color: #fff;
        }
        #updateForm {
            max-width: 400px;
            margin: auto;
        }
        #fileInputContainer {
            margin-top: 10px;
            text-align: left;
        }
        #chooseButton {
            padding: 10px;
            background-color: #3498db;
            color: #fff;
            cursor: pointer;
            border: 1px solid #3498db;
            border-radius: 5px;
            display: inline-block;
        }
        #selectedFileName {
            display: inline-block;
            margin-left: 10px;
            color: #0ec7f1;
        }
        #updateButton {
            display: none;  
            margin-top: 10px;
            padding: 10px;
            background-color: #2ecc71;
            color: #fff;
            cursor: pointer;
            border: 1px solid #2ecc71;
            border-radius: 5px;
            display: inline-block;
        }
        #fileInput {
            display: none;
        }
        #progressBarContainer {
            display: none;
            margin-top: 10px;
            background-color: #ddd;
            border-radius: 10px;
            padding: 5px;
        }
        #progressBar {
            width: 100%;
            height: 20px;
            background-color: #555;
            position: relative;
            border-radius: 5px;
        }
        #progressBarFill {
            height: 100%;
            background-color: #2ecc71;
            width: 0;
            transition: width 0.3s ease-in-out;
            border-radius: 5px;
        }
        #percentage {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            color: #fff;
        }
        #statusMessage {
            margin-top: 10px;
            font-weight: bold;
        }
        #updateButton:disabled {
            background-color: #cccccc;
            cursor: not-allowed;
        }
    </style>
</head>
<body>
    <div id='updateFormContainer'>
        <form id='updateForm' method='POST' action='' enctype='multipart/form-data'>
            <p>
                Choose the firmware and click 'Update.' Please wait for the upgrade to finish automatically. Avoid clicking again.
            </p>
            <div id='fileInputContainer'>
                <label for='fileInput'>
                    <div id='chooseButton'>Choose Firmware</div>
                </label>
                <input type='file' accept='.bin,.bin.gz' name='firmware' id='fileInput' onchange='handleFileSelect(event)' required>
                <div id='selectedFileName'></div>
            </div>
            <button type='button' style="display: none;" id='updateButton' onclick='handleUpdateClick()'>Update</button>
            <div id='progressBarContainer'>
                <div id='progressBar'>
                    <div id='progressBarFill'></div>
                    <div id='percentage'>0%</div>
                </div>
            </div>
            <p id='statusMessage'></p>
        </form>
    </div>
    <script>
        function handleFileSelect(event) {
            const updateButton = document.getElementById('updateButton');
            const selectedFileName = document.getElementById('selectedFileName');
            const fileName = event.target.files[0].name;
            const fileInput = document.getElementById('fileInput');
            selectedFileName.textContent = ` ${fileName}`;
            updateButton.style.display = 'inline-block';
            const updateForm = document.getElementById('updateForm');
            const formData = new FormData(updateForm);
        }
        function handleUpdateClick() {
            const updateButton = document.getElementById('updateButton');
            const fileInput = document.getElementById('fileInput');
            const progressBarFill = document.getElementById('progressBarFill');
            const percentage = document.getElementById('percentage');
            const statusMessage = document.getElementById('statusMessage');
            const updateForm = document.getElementById('updateForm');
            const progressBarContainer = document.getElementById('progressBarContainer');
            updateButton.disabled = true;
            fileInput.disabled = true;
            progressBarContainer.style.display = 'block';
            const formData = new FormData(updateForm);
            formData.append('firmware', fileInput.files[0]);
            const xhr = new XMLHttpRequest();
            let progress = 0;
            const intervalId = setInterval(function () {
                progress = (progress + 4);
                if (progress <= 100) {
                    progressBarFill.style.width = `${progress}%`;
                    percentage.textContent = `${Math.round(progress)}%`;
                } else {
                    clearInterval(intervalId);
                }
            }, 1000);
            xhr.open('POST', '/update', true);
            xhr.upload.onprogress = function (event) {
            };
            xhr.onreadystatechange = function () {
                if (xhr.readyState === XMLHttpRequest.DONE) {
                    if (xhr.status === 200) {
                        statusMessage.textContent = 'Upload finished successfully.';
                        progressBarFill.style.width = '100%';
                        percentage.textContent = `${Math.round(100)}%`;
                        clearInterval(intervalId);
                        fileInput.disabled = false;
                        setTimeout(() => {
                            location.href = '/';
                        }, 15000);
                    } else {
                        statusMessage.textContent = 'Please check version.';
                        fileInput.disabled = false;
                    }
                }
            };
            xhr.send(formData);
        }
   </script>
</body>
</html>)";
/** Handle the WLAN save form and redirect to WLAN config page again */
void handleWifiSave(AsyncWebServerRequest * request) {
    String ss = request->getParam("s")->value();
    String pp = request->getParam("p")->value();
    ss.toCharArray(ssid, 32);
    pp.toCharArray(password, 64);

    set_wifi_config(ssid, password);

    String page = FPSTR(HTTP_HEADER);
    page.replace("{v}", "WiFi");
    page += FPSTR(HTTP_SCRIPT);
    page += FPSTR(HTTP_STYLE);
    page += FPSTR(HTTP_HEADER_END);
    //page += "连接中,请等待30s,如失败,请重试.";
    page += "Restart to connnect...";
    page += FPSTR(HTTP_END);
    //request->addHeader("Content-Length", String(page.length()));
    request->send_P(200, "text/html", page.c_str());
    delay(500);
    //重启联网
    ESP.restart();
}
void send_version(AsyncWebServerRequest* request){
  request->send(200, F("text/json"), ("{\"m\": \""+String(PRODUCT_MODEL)+"\",\"v\":\"" + String(SW_VERSION) + "\"}"));
}
void init_http_server() {
  //以下写法，解决web portal 开启时候，无法正确加载脚本的问题
  server.on("/css/cropper.min.css", HTTP_GET, [](AsyncWebServerRequest *request){
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, request->url(), getContentType(request->url()));
      response->addHeader("Cache-Control", "public, max-age=36000"); // Cache for 1 hour (3600 seconds)
      request->send(response);
  });
  server.on("/js/cropper.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, request->url(), getContentType(request->url()));
      response->addHeader("Cache-Control", "public, max-age=3600"); // Cache for 1 hour (3600 seconds)
      request->send(response);
  });
  server.on("/js/jquery.min.js", HTTP_GET, [](AsyncWebServerRequest *request){
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, request->url(), getContentType(request->url()));
      response->addHeader("Cache-Control", "public, max-age=36000"); // Cache for 1 hour (3600 seconds)
      request->send(response);
  });

  server.on("/v.json",  HTTP_GET, [] (AsyncWebServerRequest * request) {
    send_version(request);
  });


  server.on("/set",  HTTP_GET, [] (AsyncWebServerRequest * request) {
        handleSet(request);
  });
 
  server.on("/wifisave",  HTTP_GET, [] (AsyncWebServerRequest * request) {
        handleWifiSave(request);
  });
  server.on("/delete", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String inputMessage;
    String inputParam;
    if (request->hasParam("file")) {
      inputMessage = request->getParam("file")->value();
      inputParam = "file";

      deleteFile(LittleFS, inputMessage);

      Serial.print("File=");
      Serial.println(inputMessage);
      Serial.println(" has been deleted");
    }
    else {
      inputMessage = "No message sent";
      inputParam = "none";
    }
    request->send(200, "text/plain", "OK");
  });
  server.on("/doUpload", HTTP_POST, [](AsyncWebServerRequest * request) {
      opened = false;
    },
    [](AsyncWebServerRequest * request, const String & filename, size_t index, uint8_t *data, size_t len, bool final) {
      handleDoUpload(request, filename, index, data, len, final);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    //request->send(200, "text/html", "Please choose the correct .bin file, and then click \"update\" and wait.<p><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
    request->send(200, "text/html", serverIndex);
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    shouldReboot = !Update.hasError();
    Serial.println("update success");
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot?"SUCCESS":"FAIL");
    response->addHeader("Connection", "close");
    request->send(response);
  },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      Serial.printf("Update Start: %s\n", filename.c_str());
      //Update.runAsync(true);
      if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){
        Update.printError(Serial);
      }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        Serial.printf("Update Success: %uB\n", index+len);
        ESP.restart();
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.on("/filelist", HTTP_GET, [](AsyncWebServerRequest *request){
    String dir = request->getParam("dir")->value();
    if(dir == "/" || dir == "")
      dir = "/image";
    const String &page = listDir(LittleFS, dir.c_str(), 0); 
    request->send(200, "text/plain", page);
  });

  server.on("/wifi.json", HTTP_GET, [](AsyncWebServerRequest *request){
    const String wifi_list = get_wifi_scan(); 
    DBG_PTN(wifi_list);
    //request->send(200, "text/json", wifi_list);
    request->send(200, "application/json", wifi_list);
    //delay(100);
    //再响应网页以后，再触发一次扫描。修复触发扫描时候响应 web 会导致接收不到的问题。20240409 fix
    WiFi.scanNetworks(true);
  });

  server.on("/img.json",  HTTP_GET, [] (AsyncWebServerRequest * request) {
    String img_list = list_img();
    request->send(200, "application/json", img_list);
  });

  server.on("/space.json", HTTP_GET, [](AsyncWebServerRequest *request){
    size_t ss = LittleFS.totalBytes()-LittleFS.usedBytes();
    if(ss < 500)
        request->send(200, F("text/json"), String("{\"total\":")+String(LittleFS.totalBytes())+",\"free\":1}");
    else
        request->send(200, F("text/json"), String("{\"total\":")+String(LittleFS.totalBytes())+",\"free\":"+String(LittleFS.totalBytes()-LittleFS.usedBytes() - 500)+"}");

  });

  server.on("/image.html",HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (uint8_t*)image_html, sizeof(image_html)); response->addHeader("Content-Encoding", "gzip");response->addHeader("Cache-Control", "public, max-age=3600"); request->send(response); });

  server.on("/network.html",HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (uint8_t*)network_html, sizeof(network_html)); response->addHeader("Content-Encoding", "gzip");response->addHeader("Cache-Control", "public, max-age=3600"); request->send(response); });

  server.on("/",HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (uint8_t*)system_html, sizeof(system_html)); response->addHeader("Content-Encoding", "gzip");response->addHeader("Cache-Control", "public, max-age=3600"); request->send(response); });

  server.on("/system.html",HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (uint8_t*)system_html, sizeof(system_html)); response->addHeader("Content-Encoding", "gzip");response->addHeader("Cache-Control", "public, max-age=3600"); request->send(response); });

  server.on("/time.html",HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (uint8_t*)time_html, sizeof(time_html)); response->addHeader("Content-Encoding", "gzip");response->addHeader("Cache-Control", "public, max-age=3600"); request->send(response); });

  server.on("/weather.html",HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (uint8_t*)weather_html, sizeof(weather_html)); response->addHeader("Content-Encoding", "gzip");response->addHeader("Cache-Control", "public, max-age=3600"); request->send(response); });
  server.on("/ticker.html",HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (uint8_t*)ticker_html, sizeof(ticker_html)); response->addHeader("Content-Encoding", "gzip");response->addHeader("Cache-Control", "public, max-age=3600"); request->send(response); });

  server.on("/index.html",HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", (uint8_t*)index_html, sizeof(index_html)); response->addHeader("Content-Encoding", "gzip");response->addHeader("Cache-Control", "public, max-age=3600"); request->send(response); });

  server.on("/js/settings.js",HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "application/javascript", (uint8_t*)settings_js, sizeof(settings_js)); response->addHeader("Content-Encoding", "gzip");response->addHeader("Cache-Control", "public, max-age=3600"); request->send(response); });
  server.on("/css/style.css",HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/css", (uint8_t*)style_css, sizeof(style_css)); response->addHeader("Content-Encoding", "gzip");response->addHeader("Cache-Control", "public, max-age=3600"); request->send(response); });

  server.onNotFound(notFound);
  server.begin();
}
void update_http_server()
{
  //server.handleClient();
}