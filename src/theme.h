#ifndef _THEME_H
#define _THEME_H

#define THEME_TOTAL 5
#define THEME_ALBUM 2

struct theme_loop{
  int loop_en;
  void (*init)();
  void (*update)(bool force);
  void (*display)();
  void (*exit)();
};

void theme_weather();
void theme_weather_forecast();
void theme_album();
void theme_time1();
void theme_time2();
void theme_time3();
void theme12();
void theme_stock();
void theme_daytimer();
void theme_new_weather();
void theme9();
void theme10();

void auto_adjust_brt();
#endif