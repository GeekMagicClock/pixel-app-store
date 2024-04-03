#include <WiFi.h>

// Initialize Wifi connection to the router
char ssid[] = "1024";     // your network SSID (name)
char password[] = "2048@@@@"; // your network key
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
  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  // Display Setup
  //mxconfig.double_buff = true;
  mdisplay.begin(mxconfig);
  mdisplay.setBrightness8(20); //0-255
  mdisplay.clearScreen();
  mdisplay.setCursor(0, 10);
  mdisplay.print("Hello");
  delay(1000);
 
  //t_clock.setup();
  init_http_server();
  init_btn();
  init_time();
}

#include "theme.h"
int theme_index = 0;
int last_theme_index = -1;
extern struct theme_loop theme_loop_list[THEME_TOTAL];

void set_screen_brt(int brt){
  mdisplay.setBrightness8(brt); //0-255
}

void loop() {
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