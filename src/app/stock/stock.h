#ifndef APP_STOCKMARKET_H
#define APP_STOCKMARKET_H
#include <Arduino.h>

#define CANDLE_NUMS 64

void init_stock();
void init_stock2();
void update_stock(bool force);
void display_stock();
void exit_stock();
void display_stock2();

typedef struct {
  float open;
  float close;
  float high;
  float low;
} CandleData;

typedef struct my_stock{
    String stock_name;//股票简称
    String code;//股票代码
    String kline_interval;//股票代码
    float price;//现价
    float open;//开盘价
    float close;//收盘价
    float percent;//涨跌幅度
    int kline_updated;//k线是否更新
    int ani;
    CandleData candles[CANDLE_NUMS];
}my_stock_t;

struct StockmarketAppRunData {
    unsigned int refresh_status;
    unsigned long refresh_time_millis;
    uint32_t stock_scale;
    String stock_id;//当前的stock id
    int stock_index;
    //String stock_kline_interval;//kline interval
    my_stock_t stockdata;
    int err;
};

#endif