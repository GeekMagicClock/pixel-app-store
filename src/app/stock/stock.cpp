#include "stock.h"
#include "my_debug.h"
#include <Arduino.h>

// STOCKmarket的持久化配置
//#define B_CONFIG_PATH "/stockmarket.cfg"
#define STOCK_NAME_LEN 32
#define STOCK_TOTAL 10
struct B_Config {
    uint32_t stock_scale;//哪种K线，默认5日，后续再增加其他种
    String stock_id[STOCK_TOTAL];              // bilibili的uid
    String st_kline;//kline interval
    int updateInterval; // 更新的时间间隔(s)
    int loop; // 是否循环切换几只股票
    int ani; // 是否显示图标动画
};

#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../../lib/settings.h"

static void write_config(const B_Config *cfg) {
    set_stock_config(cfg->ani, cfg->loop, cfg->updateInterval, cfg->stock_id[0].c_str(), cfg->stock_id[1].c_str(), cfg->stock_id[2].c_str(), cfg->stock_id[3].c_str(), cfg->stock_id[4].c_str(), cfg->stock_id[5].c_str(), cfg->stock_id[6].c_str(), cfg->stock_id[7].c_str(), cfg->stock_id[8].c_str(), cfg->stock_id[9].c_str());
}
static void read_config(B_Config *cfg) {

    char c0[STOCK_NAME_LEN] = {0};
    char c1[STOCK_NAME_LEN] = {0};
    char c2[STOCK_NAME_LEN] = {0};
    char c3[STOCK_NAME_LEN] = {0};
    char c4[STOCK_NAME_LEN] = {0};
    char c5[STOCK_NAME_LEN] = {0};
    char c6[STOCK_NAME_LEN] = {0};
    char c7[STOCK_NAME_LEN] = {0};
    char c8[STOCK_NAME_LEN] = {0};
    char c9[STOCK_NAME_LEN] = {0};
    int ret = read_stock_config(&cfg->ani, &cfg->loop, &cfg->updateInterval, c0, c1, c2,c3,c4,c5,c6,c7,c8,c9, STOCK_NAME_LEN);
    if(ret == -1) {
        cfg->stock_id[2] = "AAPL";  // 股票代码
        cfg->stock_id[1] = "TSLA";  // 股票代码
        cfg->stock_id[0] = "AMZN";  // 股票代码
        cfg->stock_id[3] = "BTC-USD";  // 股票代码
        cfg->stock_id[4] = "BTC-EUR";  // 股票代码
        cfg->stock_id[5] = "BTC-CAD";  // 股票代码
        cfg->stock_id[6] = "EURUSD=X";  // 股票代码
        cfg->stock_id[7] = "USDEUR=X";  // 股票代码
        cfg->stock_id[8] = "AUDUSD=X";  // 股票代码
        cfg->stock_id[9] = "GC=F";  // 股票代码
        //cfg->stock_scale = 5;//5min
        cfg->updateInterval = 30;
        cfg->loop = 1;
        write_config(cfg);
    }else{
        cfg->stock_id[0] = String(c0);  // 股票代码
        cfg->stock_id[1] = String(c1);  // 股票代码
        cfg->stock_id[2] = String(c2);  // 股票代码
        cfg->stock_id[3] = String(c3);  // 股票代码
        cfg->stock_id[4] = String(c4);  // 股票代码
        cfg->stock_id[5] = String(c5);  // 股票代码
        cfg->stock_id[6] = String(c6);  // 股票代码
        cfg->stock_id[7] = String(c7);  // 股票代码
        cfg->stock_id[8] = String(c8);  // 股票代码
        cfg->stock_id[9] = String(c9);  // 股票代码
        //cfg->stock_scale = 5;//5min
        if(cfg->updateInterval <20) cfg->updateInterval = 20;
    }

    cfg->st_kline = "1m";
    read_stock_kline_config(&cfg->st_kline);
}

static B_Config cfg_data;
StockmarketAppRunData *run_data = NULL;

//const int num_candles = 30;
//CandleData candles[num_candles];
//const char* kline_api_url = "http://money.finance.sina.com.cn/quotes_service/api/json_v2.php/CN_MarketData.getKLineData?symbol=sh000001&scale=240&ma=1&datalen=30";
void init_stock_config()
{
    //stockmarket_gui_init();
    // 获取配置信息
    read_config(&cfg_data);
    // 初始化运行时参数
    run_data = (StockmarketAppRunData *)calloc(1,sizeof(StockmarketAppRunData));
    run_data->refresh_status = 0;
    run_data->refresh_time_millis = 0;//millis() - cfg_data.updateInterval;
    run_data->stock_id = cfg_data.stock_id[0];
    //run_data->stock_kline_interval = cfg_data.st_kline;
    run_data->stock_scale = cfg_data.stock_scale;
    run_data->stockdata.stock_name = run_data->stock_id;
    run_data->stockdata.kline_interval = cfg_data.st_kline;
    run_data->stockdata.ani = cfg_data.ani;
    DBG_PTN("init stock ...");
}
extern int enter_app;
extern int page_index;
static int last_page_index;
static int cur_stock_index = -1;
static unsigned long last_loop_time;

#include "time.h"

void fill_candle_data(JsonArray& open_prices, JsonArray& close_prices, JsonArray& high_prices, JsonArray& low_prices, CandleData* candles, int num_candles) {
  int num_data_points = min(open_prices.size(), min(close_prices.size(), min(high_prices.size(), low_prices.size())));
  int j = 0;
  //假设我们请求的数据一定是大于蜡烛数的, [0] 存放的是最新的数据
  for (int i = num_data_points-1; i >= 0 && j < num_candles; i--,j++) {
    if(open_prices[i].as<String>() == "null") {
      j--;
      continue;//修复服务器返回null数据点的 bug
    }
    candles[j].open = open_prices[i].as<float>();
    candles[j].close = close_prices[i].as<float>();
    candles[j].high = high_prices[i].as<float>();
    candles[j].low = low_prices[i].as<float>();
    //DBG_PTN(candles[j].open);
  }
  return;
}


#include "lib/AsyncHTTPRequest_Generic.h"             // https://github.com/khoih-prog/AsyncHTTPRequest_Generic
AsyncHTTPRequest req_stock;

void req_stock_cb(void *optParm, AsyncHTTPRequest *request, int readyState) {

  if(run_data == NULL) return;//修复频繁按键切换主题，退出时，仍然会进入到这里导致崩溃。20240430

  if (readyState == readyStateDone) {
    int code = request->responseHTTPcode();
    if (code == 200) {
      String payload = request->responseText();
      DBG_PTN(payload);
      JsonDocument doc;
      auto err = deserializeJson(doc, payload);
      if (err) {
        DBG_PTN("deser failed: ");
        DBG_PTN(err.c_str());
        return;
      }
      
      int price_pos = payload.indexOf("regularMarketPrice");
      int price_start = payload.indexOf(":", price_pos) + 1;
      int price_end = payload.indexOf(",", price_start);
      String price_str = payload.substring(price_start, price_end);

      int prev_close_pos = payload.indexOf("chartPreviousClose");
      int prev_close_start = payload.indexOf(":", prev_close_pos) + 1;
      int prev_close_end = payload.indexOf(",", prev_close_start);
      String prev_close_str = payload.substring(prev_close_start, prev_close_end);

      float price = price_str.toFloat();
      float prev_close = prev_close_str.toFloat();

      JsonArray open_prices = doc["chart"]["result"][0]["indicators"]["quote"][0]["open"];
      JsonArray high_prices = doc["chart"]["result"][0]["indicators"]["quote"][0]["high"];
      JsonArray low_prices = doc["chart"]["result"][0]["indicators"]["quote"][0]["low"];
      JsonArray close_prices = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"];


      // Process and draw the candle data using LVGL here
      my_stock_t* stock_data = &run_data->stockdata;
      stock_data->price = price;
      //需要修正， 网站是用(close[63]-close[62])/close[62] 计算得来的百分比，20240428
            //这里有bug，char 太长，解析不出实时价格 20230508 ifeng
      //stock_data->price= doc['chart']['result'][0]['meta']['regularMarketPrice'];
      //stock_data->price= doc['chart']['result'][0]['meta']['regular'];
      //float previous_close_price = doc['chart']['result'][0]['meta']['chartPreviousClose'].as<float>();
      //float price_change = stock_data->price - previous_close_price;
      //stock_data->percent = (price_change / previous_close_price) * 100;

      stock_data->kline_updated = 1;
      fill_candle_data(open_prices, close_prices, high_prices, low_prices, stock_data->candles, CANDLE_NUMS);
      run_data->err = 0;
      prev_close = stock_data->candles[1].close;
      DBG_PTN(price);
      DBG_PTN(prev_close);
      stock_data->percent = ((price -prev_close) /prev_close * 100);
      DBG_PTN(stock_data->percent);
      stock_data->percent = roundf(stock_data->percent * 10)/10;
      DBG_PTN(stock_data->percent);
      //timesynced = true;
    }else{
      run_data->err = code;
      DBG_PTN("time code = " + String(code));
    }
  }
}

void send_req_stock(){
  //DBG_PTN(("rq t"));
  String a = run_data->stockdata.kline_interval;
  DBG_PTN(a);
  int value = 0;//a.toInt();
  //String unit = a.substring(a.length()-1);
  int i = 0;
  for (i = 0; i < a.length(); i++) {
    if (isDigit(a.charAt(i))) {
      value = value*10+(a.charAt(i) - '0');
    } else {
      // 遇到非数字字符时退出循环
      break;
    }
  }
  DBG_PTN("value = "+String(value));
  String unit = a.substring(i);
  String range = String(value*CANDLE_NUMS+20)+unit;
  DBG_PTN(range);
  //注意下，雅虎也提供了http接口，异步http client 似乎暂不支持https的请求, 20240420
  String url = "http://query1.finance.yahoo.com/v8/finance/chart/"+run_data->stock_id+"?interval="+a+"&range="+range;
  DBG_PTN(url);
  bool requestOpenResult;
  //Serial.println(req_stock.headers());
  if (req_stock.readyState() == readyStateUnsent || req_stock.readyState() == readyStateDone) {

    requestOpenResult = req_stock.open("GET", url.c_str());
    if (requestOpenResult) {
      //要放到这里才生效
      req_stock.setReqHeader("User-Agent","Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36");
      req_stock.setReqHeader("Accept", "application/json");
      req_stock.setReqHeader("Connection", "keep-alive");
      DBG_PTN(req_stock.headers());
      req_stock.send();
      //http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36"));
    } else {
      DBG_PTN(F("bad request"));
    }
  } else {
    DBG_PTN("rt can't send req");
    //req_stock.abort();
  }
}

void async_http_get_stock() {
  req_stock.setDebug(true);
  req_stock.onReadyStateChange(req_stock_cb);
  send_req_stock();
}

//todo 股票更新逻辑：
//1. 一支股票固定间隔更新。
//2. 多只股票间隔更新
//3. 设置突然改变，立刻更新。
void update_stock(bool force){
    if(run_data == NULL) return;
    if(!force && millis() - run_data->refresh_time_millis < cfg_data.updateInterval*1000 ) return;
    //display_stock(&run_data->stockdata, 30, LV_SCR_LOAD_ANIM_FADE_IN);
    if(cfg_data.loop){
      if(run_data->stock_index >= STOCK_TOTAL) run_data->stock_index = 0;
      run_data->stock_id = cfg_data.stock_id[run_data->stock_index++];
      run_data->stockdata.stock_name = run_data->stock_id;
      DBG_PTN(run_data->stock_id);
    }
    //get_kline_data_yahoo();
    async_http_get_stock();
    run_data->refresh_time_millis = millis();
    //get_realtime_stock_data();    
    //get_30_days_kline_data();
}

void exit_stock(){
//todo

  //req_stock.abort();
  free(run_data);
  run_data = NULL;
}