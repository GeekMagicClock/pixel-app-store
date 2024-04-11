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
int last_theme_index = -1;
extern int album_time;
extern int autodisplay;
extern int hour12;
extern struct theme_loop theme_loop_list[THEME_TOTAL];
char ap_ssid [] = "GeekMagic";

extern int timeZone;
extern int minutesTimeZone;

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
  read_hour12_config(&hour12);
  //提取时区
  read_timezone_config(&timeZone, &minutesTimeZone);

  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
 // Display Setup
  //mxconfig.double_buff = true;
  //mdisplay.print("Hello");
  //delay(1000);
  init_display();

  //virtualDisp = new VirtualMatrixPanel(mdisplay, NUM_ROWS, NUM_COLS, PANEL_RES_X, PANEL_RES_Y);
  unsigned long timeout = millis();
  while (WiFi.status() != WL_CONNECTED && (millis()-timeout) < 15*1000) {
    drawGif("/image/w.gif",0,0); 
    Serial.print(".");
    //delay(500);
  }

  if(WiFi.status() != WL_CONNECTED){
    mdisplay.clearScreen();
    mdisplay.setCursor(0, 4);
    mdisplay.print("WiFi start");
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
  mdisplay.setCursor(0, 10);
  mdisplay.print("Sync");
  mdisplay.setCursor(24, 10);
  mdisplay.print("time..");
  init_time();
  //init_ntp();
}

void loop() {
    //scroll_text(0, 20, "This is a long text stringgggggggggggggggggggggg.");
    //return;
    //virtualDisp->flipDMABuffer();
    //sync_http_time(false);
  #if 1
    sync_time(false);//
  #endif
    update_btn();
    update_http_server();
    auto_adjust_brt();
    if(last_theme_index != theme_index){
        if(last_theme_index >= 0)
            theme_loop_list[last_theme_index].exit();

        theme_loop_list[theme_index].init();

        if(theme_loop_list[theme_index].update != NULL)
          theme_loop_list[theme_index].update(true);

        last_theme_index = theme_index;
        mdisplay.clearScreen();
    }

    theme_loop_list[theme_index].display();

    if(theme_loop_list[theme_index].update != NULL)
      theme_loop_list[theme_index].update(false);
}