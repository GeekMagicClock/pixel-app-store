#include "FS.h"
#include <LittleFS.h>
//#include "OneButton.h"
//#include "btn.h"

#include "../../lib/jpg.h"
#include "../../lib/gif.h"
#include "../../lib/btn.h"
#include "my_debug.h"
#include "theme.h"
//FS fs = LittleFS;
int app_exit;
int album_time = 5;
int autoplay = 1;
extern int brt;
extern int theme_index;
extern void set_screen_brt(int brt);
String autoplay_path ="/image/spaceman.gif";
void display_album(){

  if(autoplay == 0){
    if(!LittleFS.exists(autoplay_path.c_str())){
      DBG_PTN("File not found");
      return;
    }else{
        File file = LittleFS.open(autoplay_path, "r");
        if (file) {
            size_t fileSize = file.size();
            DBG_PTN("File size: ");
            DBG_PTN(fileSize);
            file.close();
            if(fileSize == 0){
              return;
            }
        } else {
            DBG_PTN("Error opening file!");
            return;
        }
    }

    //判断String autoplay_path 是否包含.jpg 格式的文件
    if(autoplay_path.endsWith(".jpg") || autoplay_path.endsWith(".jpeg") || autoplay_path.endsWith(".JPG") || autoplay_path.endsWith(".JPEG")){
      //analogWrite(5, 255);
      drawJpeg(autoplay_path.c_str(), 0 ,0 );
      //delay(100);
      #if 0
      int i = 0;
      while(i<brt){
        analogWrite(5, 255-i++);
        delay(10);
      }
      #endif
    }else if (autoplay_path.endsWith(".gif") || autoplay_path.endsWith(".GIF")){
      while(drawGif(autoplay_path.c_str(), 0, 0) == 1 && theme_index == THEME_ALBUM){
        update_btn();
        //update_http_server();
      };
    }
    
    return;
  }

  File root;
  root = LittleFS.open("/image","r");
  if (!root) {
      //DBG_PTN("- failed to open directory");
      return;
  }
  if (!root.isDirectory()) {
      //DBG_PTN(" - not a directory");
      return;
  }

  File file = root.openNextFile();
  while (file && autoplay && theme_index == THEME_ALBUM) {
    //update_http_server();
    update_btn();
    if (file.isDirectory()) {
      file = root.openNextFile();
      continue;
    } 
    //const char * file_name = file.name().c_str();
    //char filename[256] = {0};
    //snprintf(filename, sizeof(filename), "%s", file.name());
    DBG_PTN(file.name());
    char path[256] = {0};
    int is_jpg = 0;
    snprintf(path, sizeof(path), "/image/%s", file.name());
    if (NULL != strstr(file.name(), ".jpg") || NULL != strstr(file.name(), ".jpeg")|| NULL != strstr(file.name(), ".JPEG") || NULL != strstr(file.name(), ".JPG"))
    {
      is_jpg = 1;
      set_screen_brt(0);
      //drawJpeg(path, 0 ,0 );
      int i = 0;
      while(i<brt && theme_index == THEME_ALBUM){
        update_btn();
        set_screen_brt(i++);
        delay(10);
      }
    }else if(strstr(file.name(), ".gif") != NULL || strstr(file.name(), ".GIF") != NULL){
      //playGif(path, 0, 0);
      //修复gif大小为0时，阻塞在while的问题
      if(file.size() == 0) {
          file = root.openNextFile();
          continue;
      }
      is_jpg = 0;
      while(drawGif(path, 0, 0) == 1 && theme_index == THEME_ALBUM){
        //update_http_server();
        update_btn();
      };
    }
    if(is_jpg) // jpg 播放后等待
    {
      //防止延时时间过长，无法切换主题
      int i = album_time*10;
      while(i-- && theme_index == THEME_ALBUM){
        //update_http_server();
        update_btn();
        if(app_exit){
          app_exit = 0;
          return; 
        }
      }
    }
    //DBG_PTN("free size :");
    //DBG_PTN(ESP.getFreeHeap());
    file = root.openNextFile();
  }
  return;
}

#include "../../lib/display.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
extern MatrixPanel_I2S_DMA mdisplay;
void init_album(){
  jpegInit();

  mdisplay.clearScreen();
  mdisplay.setCursor(0,4);
  mdisplay.setTextColor(parseRGBColor(C_LIGHT_ORANGE));
  mdisplay.println("2.");
  mdisplay.println("Images");
  mdisplay.print("Display");
  delay(2000);
}

void exit_album(){
  gifDeinit();  
}