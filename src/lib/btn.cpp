#include "Button2.h"
#include "my_debug.h"
#include "../theme.h"

#define BTN_PIN 32
Button2 btn;
extern int theme_index;
extern int brt;
int page_index;
int rgb_off_flag = 0;
extern void set_screen_brt(int brt);

byte capStateHandler() {
   return digitalRead(BTN_PIN);
}

void btn_tap(Button2 &btn){

    //theme_index ++;
    //if(theme_index >= THEME_TOTAL) theme_index = 0;

    DBG_PTN("tap");
    unsigned int time = btn.wasPressedFor();
    Serial.print("You clicked ");
    if (time > 1500) {
        Serial.print("a really really long time.");
    } else if (time > 1000) {
        Serial.print("a really long time.");
    } else if (time > 500) {
        Serial.print("a long time.");        
    } else {
        Serial.print("long.");        
    }
    Serial.print(" (");        
    Serial.print(time);        
    Serial.println(" ms)");
}

//void btn_3_click(){
void btn_3_click(Button2 &btn){
    DBG_PTN("btn click 3");
}

void btn_double_click(Button2 &btn){
    if(rgb_off_flag == 1 )//亮屏
      set_screen_brt(brt);

    DBG_PTN("btn click 2");
    theme_index ++;
    if(theme_index >= THEME_TOTAL) theme_index = 0;
    //set_theme_config(theme_index);
}

//void btn_click(){
void btn_click(Button2 &btn){
    if(rgb_off_flag == 1 )//亮屏
      set_screen_brt(brt);

    theme_index ++;
    if(theme_index >= THEME_TOTAL) theme_index = 0;
    return;
    DBG_PTN("btn click 1");
    if(theme_index == 0) {//翻页时钟模式
    }

    if(theme_index == 1) {//图片时间模式
      page_index ++;
      if(page_index >= 23) page_index = 1;
      DBG_PTN(page_index);
    }

    if(theme_index == 2){//字体时间模式
      page_index ++;
      if(page_index >= 14) page_index = 1;
    }

    //set_page_index_config(page_index);
}

void longClickDetected(Button2& btn) {
    DBG_PTN("long press! ...");
    
    if(rgb_off_flag == 1 ){//亮屏
      set_screen_brt(brt);
      rgb_off_flag = 0;
      return;     
    }else{
      //关闭屏幕
      rgb_off_flag = 1;
      set_screen_brt(0);
    }
}
//void btn_long_press(){
void btn_long_press(Button2 &btn){
    //enter_app = 1;//!enter_app;
    DBG_PTN("long pressed ----");
    unsigned int time = btn.wasPressedFor();
    Serial.print("You clicked ");
    if (time > 1500) {
        Serial.print("a really really long time.");
    } else if (time > 1000) {
        Serial.print("a really long time.");
    } else if (time > 500) {
        Serial.print("a long time.");        
    } else {
        Serial.print("long.");        
    }
    Serial.print(" (");        
    Serial.print(time);        
    Serial.println(" ms)");
}

void init_btn(){
  pinMode(BTN_PIN, INPUT_PULLUP);
  btn.setClickHandler(btn_click);
  btn.setTapHandler(btn_tap);
  btn.setDoubleClickHandler(btn_double_click);
  btn.setTripleClickHandler(btn_3_click);
  btn.setLongClickTime(1000);
  btn.setLongClickHandler(btn_long_press);
  btn.setLongClickDetectedHandler(longClickDetected);
  btn.begin(BTN_PIN);
}

bool btn_status(){
  return btn.isPressed();
}

void update_btn(){
  //DBG_PTN(digitalRead(BTN_PIN));
  btn.loop();
}
#if 0
#include "OneButton.h"
#include "lib/display.h"
#include "lib/settings.h"
#include "lib/rgbled.h"
#include "my_debug.h"
//todo ifeng
OneButton btn(27, true, false);
extern int theme_index;
extern int force_time_display;
extern int brt;
extern int page_index;
extern int rgb_off_flag ;
extern String flip_index;

void btn_click(){

    if(rgb_off_flag) {
      rgb_off_flag = 0;
      set_tft_brt(brt);
      return;
    }

    set_tft_brt(brt);
    DBG_PTN("btn click 1");
    if(theme_index == 1) {//翻页时钟模式
      if(flip_index == ""){ 
        flip_index = "r_";
        page_index = 1;
      }
      else {
        flip_index = "";
        page_index = 2;
      }
      force_time_display = 1;
    }

    if(theme_index == 2) {//图片时间模式
      page_index ++;
      if(page_index >= 23) page_index = 1;
      DBG_PTN(page_index);
      force_time_display = 1;
    }

    if(theme_index == 3){//字体时间模式
      page_index ++;
      if(page_index >= 14) page_index = 1;
    }

    if(theme_index == 6){//特效动画模式
      page_index ++;
      if(page_index >= 6) page_index = 1;
    }
    set_page_index_config(page_index);
}

void btn_double_click(){

    if(rgb_off_flag) {
      rgb_off_flag = 0;
      set_tft_brt(brt);
      return;
    }
    set_tft_brt(brt);
    DBG_PTN("btn click 2");
    theme_index ++;
    if(theme_index >= THEME_TOTAL+1) theme_index = 1;
    set_theme_config(theme_index);
}

void btn_long_press(){
    //theme_index = 1;
    DBG_PTN("btn long press! todo ...");
    
    //关闭屏幕
    rgb_off_flag = 1;
    rgb_off();

    set_tft_brt(0);
}

#include "lib/settings.h"
void in_long_press(void *oneButton)
{
  Serial.print(((OneButton *)oneButton)->getPressedMs());
  DBG_PTN("\t - DuringLongPress()");
  int press_ms = ((OneButton *)oneButton)->getPressedMs();
  //if(press_ms > 2000 && press_ms < 4000){
    //关闭屏幕
  //  rgb_off_flag = 1;
  //  set_tft_brt(0);
  //}else 
  if(press_ms > 10000){
    reset_config();
    ESP.restart();
  }
}

extern int led_mode;
void btn_3_click(){
    if(btn.getNumberClicks() > 2) {
      led_mode ++;
      if(led_mode > 4) led_mode = 0;
    }
    DBG_PTN("btn click 3");
}

void init_btn(){
  btn.reset();
  btn.setPressTicks(1000);
  btn.attachClick(btn_click);  
  btn.attachDoubleClick(btn_double_click);
  btn.attachMultiClick(btn_3_click);
  btn.attachLongPressStart(btn_long_press);
  btn.attachDuringLongPress(in_long_press, &btn);
}

void update_btn(){
  btn.tick();
}
#endif