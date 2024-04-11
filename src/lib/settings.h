#ifndef _SETTINGS_H
#define _SETTINGS_H

void updateBootCount();
int check_mac();

int init_config();
void reset_config();
int create_file(char *path);

int read_ntp_config(String *ntp);
int set_ntp_config(String ntp);

int read_wifi_config(char *ssid, char ssid_len, char *pwd, char pwd_len);
int set_wifi_config(const char *ssid, const char *pwd);

int read_city_config(char *city, char ct_len, char *code, char cd_len, char *location, char loc_len);
int set_city_config(const char *city, const char *code, const char *location);

int set_kline_config(String kline);
int read_kline_config(String *k);

int set_stock_kline_config(String kline);
int read_stock_kline_config(String *k);

int set_stock_config(int ani, int loop, int i, const char *c0, const char *c1, const char *c2, const char *c3, const char *c4, const char *c5, const char *c6, const char *c7, const char *c8, const char *c9);
int read_stock_config(int *ani, int *loop, int *i, char *c0, char *c1, char *c2,char *c3,char *c4,char *c5,char *c6,char *c7,char *c8,char *c9, int code_len);

int set_coin_config(int ani, int loop, int i, const char *c0, const char *c1, const char *c2, const char *c3,const char *c4,const char *c5,const char *c6,const char *c7,const char *c8,const char *c9);
int read_coin_config(int *ani, int *loop, int *i, char *c0, char *c1, char *c2, char *c3,char *c4,char *c5,char *c6,char *c7,char *c8,char *c9,int code_len);
int read_delay_config(int *delay);
int set_delay_config(int delay);

int set_brt_config(int brt);
int read_brt_config(int *brt);

int set_timer_brt_config(int en, int t1, int t2, int b2);
int read_timer_brt_config(int *en, int *t1, int *t2, int *b2);

int read_hour12_config(int *i);
int set_hour12_config(int i);

int set_colon_config(int i);
int read_colon_config(int *i);
//int read_bili_config(char *id, int id_len);
int read_bili_config(char *id, int id_len, int *b_i);
//int set_bili_config(const char * bili);
int set_bili_config(const char * bili, int b_i);

int set_gif_config(String path);
int read_gif_config(String &path);

int set_img_config(String path);
int read_img_config(String &path);

int read_key_config(String *key);
int set_key_config(String key);

int read_weather_interval_config(int *i);
int set_weather_interval_config(int i);

int read_unit_config(String *w_u, String *t_u, String *p_u);
int set_unit_config(String w_u, String t_u, String p_u);

int set_font_config(String path);
int read_font_config(String *path);

int read_dst_config(int *i);
int set_dst_config(int i);
void update_time_colors();
int read_time_color_config(String &h, String &m , String &s);
int set_time_color_config(String h, String m, String s);

int read_timezone_config(int *tz, int *mtz);
int set_timezone_config(int tz, int mtz);

int set_day_config(int day_format);
int read_day_config(int *day_format);

int set_daytimer_config(int yr, int mth, int day);
int read_daytimer_config(int *yr, int *mth, int *day);

int set_theme_config(int i);
int read_theme_config(int *i);

int set_page_index_config(int page_index);
int read_page_index_config(int *page_index);

int read_album_config(int *autoplay, int *i);
int set_album_config(int autoplay, int i);

//int read_stock_config(char *code, int code_len, char *exchange, int exchange_len);
int read_stock_config(char *code, int code_len, char *exchange, int exchange_len, int *s_i);
//int set_stock_config(const char *code, const char *exchange);
int set_stock_config(const char *code, const char *exchange, int s_i);

int saveThemeList(const char* themeList, int en, int interval);
int loadThemeList(char* themeList, size_t themeListSize, int *en, int *interval);
/*
1) 开机后reset1.flag 如果不存在，就创建reset1.flag，联网成功后删除reset1.flag
2) 开机后如果reset1.flag 存在，创建reset2.flag, 联网成功后删除reset2.flag 和 reset1.flag
3）开机后如果reset2.flag 存在，则直接进入配网模式。实现2次未正常联网，第三次直接进入配网。
*/
#define  R1_FLAG     "/r1.flag"
#define  R2_FLAG     "/r2.flag"

#endif