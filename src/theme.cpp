#include "theme.h"
#include "app/weather/weather_en.h"
#include "app/stock/stock.h"
#include "app/album/album.h"
#include "app/info/info.h"
#include "app/tetris/tetris_clock.h"
#include "app/morphing-clock/morphing-clock.h"

struct theme_loop theme_loop_list[THEME_TOTAL] = {
  {1, init_stock,   update_stock, display_stock, exit_stock},//股票等资产信息
  {1, init_album,   NULL, display_album, exit_album},//相册
  {1, init_tetris,  NULL, display_tetris, exit_tetris},//俄罗斯方块时钟
  {1, init_morphing, NULL, display_morphing, exit_morphing},//数字变形时钟
  {1, init_weather, update_weather, display_weather, exit_weather},//今日天气
  {1, init_info, NULL, display_info, exit_info}//信息
};