#include "theme.h"
#include "app/weather/weather_en.h"
#include "app/stock/stock.h"
#include "app/album/album.h"
#include "app/tetris/tetris_clock.h"
#include "app/morphing-clock/morphing-clock.h"

struct theme_loop theme_loop_list[THEME_TOTAL] = {
  {1, init_weather, update_weather, display_weather, exit_weather},//3日天气
  {1, init_stock,   update_stock, display_stock, exit_stock},//当天天气
  {1, init_album,   NULL, display_album, exit_album},//当天天气
  {1, init_tetris,  NULL, display_tetris, exit_tetris},//相册
  {1, init_morphing, NULL, display_morphing, exit_morphing}//相册
};