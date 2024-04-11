#include <WiFi.h>

// Initialize Wifi connection to the router
char ssid[33] = "1024";     // your network SSID (name)
char password[65] = "2048@@@@"; // your network key
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#define PANEL_RES_X 64      // Number of pixels wide of each INDIVIDUAL panel module. 
#define PANEL_RES_Y 32     // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1      // Total number of panels chained one to another
HUB75_I2S_CFG mxconfig(
  PANEL_RES_X,   // module width
  PANEL_RES_Y,   // module height
  PANEL_CHAIN    // Chain length
);
MatrixPanel_I2S_DMA mdisplay(mxconfig);

uint16_t myWHITE = mdisplay.color565(255, 255, 255);
#include "LittleFS.h"
#include "lib/web_server.h"
#include "lib/btn.h"
#include "lib/times.h"

#define NUM_ROWS 1
#define NUM_COLS 1
#include <ESP32-VirtualMatrixPanel-I2S-DMA.h>
//VirtualMatrixPanel *virtualDisp = nullptr;
VirtualMatrixPanel vdisplay(mdisplay, NUM_ROWS, NUM_COLS, PANEL_RES_X, PANEL_RES_Y);
#include "lib/settings.h"
#include "lib/gif.h"
#include "theme.h"
#include "lib/web_server.h"
int theme_index = 0;
int last_theme_index = -1;
int brt = 50;
extern int album_time;
extern int autodisplay;
extern struct theme_loop theme_loop_list[THEME_TOTAL];
char ap_ssid [] = "GeekMagic Pixel";

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
  read_brt_config(&brt);

  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
 // Display Setup
  //mxconfig.double_buff = true;
  mdisplay.begin(mxconfig);
  mdisplay.setBrightness8(50); //0-255
  mdisplay.clearScreen();
  //mdisplay.print("Hello");
  //delay(1000);

  //virtualDisp = new VirtualMatrixPanel(mdisplay, NUM_ROWS, NUM_COLS, PANEL_RES_X, PANEL_RES_Y);
  long timeout = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-timeout < 15*1000) {
    drawGif("/image/w.gif",0,0); 
    Serial.print(".");
    //delay(500);
  }

  if(WiFi.status() != WL_CONNECTED){
    mdisplay.clearScreen();
    mdisplay.setCursor(4, 10);
    mdisplay.print("WiFi start:");
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
}


void set_screen_brt(int brt){
  mdisplay.setBrightness8(brt); //0-255
}

//
void scroll_text(int16_t y, int16_t speed, String flight)
{
    int w_text = 64; // screen width
    int len = 0, len1 = flight.length() * 6; // 18 is width of character when font size is 3
   len = len1; 
    while (1) {
        // display flight
        vdisplay.flipDMABuffer();
        vdisplay.fillScreen(0);
        vdisplay.setCursor(w_text, y);
        vdisplay.setTextWrap(false);
        vdisplay.setTextColor(vdisplay.color444(0, 0, 15));
        vdisplay.print(flight);
        w_text--;
        if (w_text + len == 0) {
            w_text = 64;
        }
        delay(speed);
    }
}

void loop() {
    //scroll_text(0, 20, "This is a long text stringgggggggggggggggggggggg.");
    //return;
    //virtualDisp->flipDMABuffer();
    sync_http_time();
    update_btn();
    update_http_server();
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