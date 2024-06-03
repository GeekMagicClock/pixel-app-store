#include <WiFi.h>

// Initialize Wifi connection to the router
char ssid[33] = "1024";     // your network SSID (name)
char password[65] = "2048@@@@"; // your network key
//uint16_t myWHITE = mdisplay.color565(255, 255, 255);
#include "LittleFS.h"
#include "lib/web_server.h"
#include "lib/btn.h"
#include "lib/times.h"
#include "lib/display.h"
#include "my_debug.h"

//#define NUM_ROWS 1
//#define NUM_COLS 1
//#include <ESP32-VirtualMatrixPanel-I2S-DMA.h>
//VirtualMatrixPanel *virtualDisp = nullptr;
//VirtualMatrixPanel vdisplay(mdisplay, NUM_ROWS, NUM_COLS, PANEL_RES_X, PANEL_RES_Y);
#include "lib/settings.h"
#include "lib/gif.h"
#include "theme.h"
#include "lib/web_server.h"
int theme_index = 0;
int tmp_theme_index = 0;
int last_theme_index = -1;
extern int album_time;
extern int autodisplay;
extern int hour12;
extern struct theme_loop theme_loop_list[THEME_TOTAL];
char ap_ssid [] = "GeekMagic";

extern int timeZone;
extern int minutesTimeZone;
extern int force_time_display;
static int last_tz = 8;
static int last_mtz = 0;
void updateDataTask(void *pvParameters) {
  while (1) {
    //btn.tick();
    //不延迟会造成整个系统卡顿
    //esp_task_wdt_reset();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);

  // Attempt to connect to Wifi network:
  Serial.print("Connecting Wifi: ");
  Serial.println(ssid);
  // Start filesystem
  Serial.println(" * Loading SPIFFS");
  if(!LittleFS.begin()){
    Serial.println("SPIFFS Mount Failed");
  }
  if(!LittleFS.exists("/.sys")) {
    LittleFS.mkdir("/.sys");
  }
  read_wifi_config(ssid, sizeof(ssid), password, sizeof(password)); 
  read_theme_config(&theme_index);
  tmp_theme_index = theme_index;//解决异步切换主题的问题
  read_hour12_config(&hour12);
  //提取时区
  read_timezone_config(&timeZone, &minutesTimeZone);

  WiFi.scanNetworks(true);
  delay(500);
  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
 // Display Setup
  //mxconfig.double_buff = true;
  //mdisplay.print("Hello");
  //delay(1000);
  init_display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setAutoReconnect(true);
  //virtualDisp = new VirtualMatrixPanel(mdisplay, NUM_ROWS, NUM_COLS, PANEL_RES_X, PANEL_RES_Y);
  unsigned long timeout = millis();
  while ((millis()-timeout) < 15*1000) {
    if((millis()-timeout) < 4.3*1000)
     drawGif("/w.gif",0,0); 
    else if(WiFi.status() != WL_CONNECTED){
     drawGif("/w.gif",0,0); 
    }else if(WiFi.status() == WL_CONNECTED) {
      break;
    }

    Serial.print(".");
    //delay(500);
  }
  pinMode(32,INPUT_PULLUP);
  if(WiFi.status() != WL_CONNECTED || digitalRead(32) == LOW){
    mdisplay.clearScreen();
    mdisplay.setCursor(0, 1);
    mdisplay.println("WiFi");
    mdisplay.setCursor(0, 9);
    mdisplay.println("START:");
    mdisplay.setCursor(0, 18);
    mdisplay.print("GeekMagic");
    startPortal(ap_ssid, "");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
 
  init_http_server();
  init_btn();

  mdisplay.clearScreen();
  mdisplay.setCursor(0, 4);
  mdisplay.println("NEW IP:");
  mdisplay.setCursor(0, 13);
  mdisplay.setTextColor(parseRGBColor(C_GREEN));
  mdisplay.print(WiFi.localIP().toString());
  delay(3000);
 
  mdisplay.clearScreen();
  mdisplay.setCursor(0, 12);
  mdisplay.print("SYNC");
  mdisplay.setCursor(25, 12);
  mdisplay.print("TIME..");
  init_time();
  //init_ntp();
  #if 0
  xTaskCreatePinnedToCore(
      updateDataTask,     // 任务函数
      "dataTask",   // 任务名称
      10000,          // 任务堆栈大小（字节）
      NULL,           // 任务参数
      1,              // 任务优先级
      NULL,           // 任务句柄
      1               // 分配给第二个内核的内核编号（1表示第二个内核）
  );
  #endif
}
unsigned long lastConnectionAttempt = 0;
const int connectionInterval = 5 * 60 * 1000; // 5分钟，以毫秒为单位

void check_wifi(){
    unsigned long currentMillis = millis();
    if(!WiFi.isConnected()) {
    if (currentMillis - lastConnectionAttempt >= connectionInterval) {
      // 如果连接间隔超过5分钟，尝试重新连接
      DBG_PTN("reconnect");
      WiFi.begin(ssid, password);
      lastConnectionAttempt = currentMillis;
    }
  }
}
int sys_update_status = 0;
void loop() {
    //scroll_text(0, 20, "This is a long text stringgggggggggggggggggggggg.");
    //return;
    //virtualDisp->flipDMABuffer();
    //sync_http_time(false);
    if(sys_update_status == 1){
      display_update(sys_update_status); 
    }else if(sys_update_status == -1){
      display_update(sys_update_status); 
    }else if(sys_update_status == 2){
      display_update(sys_update_status); 
    }
    if(sys_update_status != 0) return;

    check_wifi();
  #if 1
    if(last_tz != timeZone || last_mtz != minutesTimeZone){
      sync_time(true);//
      last_mtz = minutesTimeZone;
      last_tz = timeZone;
    } else
      sync_time(false);//
  #endif
    update_btn();
    update_http_server();
    auto_adjust_brt();
    if(last_theme_index != theme_index || force_time_display){
        if(last_theme_index >= 0)
            theme_loop_list[last_theme_index].exit();

        mdisplay.clearScreen();
        last_theme_index = theme_index;
        theme_loop_list[theme_index].init();
        //if(last_theme_index != theme_index) return;//初始化阶段又被按下按键

        if(theme_loop_list[theme_index].update != NULL)
          theme_loop_list[theme_index].update(true);

        if(force_time_display) force_time_display = 0;
    }

    theme_loop_list[theme_index].display();

    if(theme_loop_list[theme_index].update != NULL)
      theme_loop_list[theme_index].update(false);
    if(tmp_theme_index != theme_index) {
      //说明客户端修改了主题，需要切换。阻塞式改变theme_index的值
      if(tmp_theme_index >= 0 && tmp_theme_index <THEME_TOTAL){
        theme_index = tmp_theme_index;
        set_theme_config(theme_index);
      }
    }
}