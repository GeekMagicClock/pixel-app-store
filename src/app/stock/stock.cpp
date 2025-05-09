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
        cfg->st_kline = "1d";
        write_config(cfg);
    }else{
        cfg->loop = 1;//不loop 有bug
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
        if(cfg->updateInterval <30) cfg->updateInterval = 30;
        //cfg->updateInterval = 5;
    }

    cfg->st_kline = "1d";
    read_stock_kline_config(&cfg->st_kline);
}

static B_Config cfg_data;
StockmarketAppRunData *run_data = NULL;
bool req_stock_done = true;

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
    run_data->stock_index = -1;
    DBG_PTN("init stock ...");
    req_stock_done = true;
    read_yahoo_cookie(&run_data->cookie);
    read_yahoo_crumb(&run_data->crumb);
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
#define USE_SSL 0
#if USE_SSL
//20240601没有调通，发生 https://github.com/me-no-dev/AsyncTCP/issues/88
#include "lib/AsyncHTTPSRequest_Generic.h"             // https://github.com/khoih-prog/AsyncHTTPRequest_Generic
AsyncHTTPSRequest req_stock;
#else
#include "lib/AsyncHTTPRequest_Generic.h"             // https://github.com/khoih-prog/AsyncHTTPRequest_Generic
AsyncHTTPRequest req_stock;
#endif
#if USE_SSL
void req_stock_cb(void *optParm, AsyncHTTPSRequest *request, int readyState) {
#else
void req_stock_cb(void *optParm, AsyncHTTPRequest *request, int readyState) {
#endif

  if(run_data == NULL){

    req_stock_done = true;//完成请求，才可以发送下一个
    return;//修复频繁按键切换主题，退出时，仍然会进入到这里导致崩溃。20240430
  }

  if (readyState == readyStateDone) {
    int code = request->responseHTTPcode();
    if (code == 200) {
      String payload = request->responseText();
      //DBG_PTN(payload);
      DynamicJsonDocument doc(1024*12);
      auto err = deserializeJson(doc, payload);
      if (err) {
        DBG_PTN("deser failed: ");
        DBG_PTN(err.c_str());
        req_stock_done = true;//完成请求，才可以发送下一个
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
      //prev_close = stock_data->candles[1].close;
      //DBG_PTN(price);
      //DBG_PTN(prev_close);
      //stock_data->percent = ((price -prev_close) /prev_close * 100);
      //DBG_PTN(stock_data->percent);
      //stock_data->percent = roundf(stock_data->percent * 10)/10;
      //DBG_PTN(stock_data->percent);
      //timesynced = true;
    }else{
      run_data->err = code;
      run_data->last_err_time = millis();
      DBG_PTN("rsp code = " + String(code));
    }
    req_stock_done = true;//完成请求，才可以发送下一个
  }
}

void send_req_stock(){
  if(req_stock_done == false) return; 
  //DBG_PTN(("rq t"));
  String a = run_data->stockdata.kline_interval;
  //DBG_PTN(a);
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
  //DBG_PTN("value = "+String(value));
  String unit = a.substring(i);
  String range = String(value*CANDLE_NUMS+10)+unit;
  //DBG_PTN(range);
#if USE_SSL
  String url = "https://query1.finance.yahoo.com/v8/finance/chart/"+run_data->stock_id+"?interval="+a+"&range="+range;
#else
  String url = "http://query1.finance.yahoo.com/v8/finance/chart/"+run_data->stock_id+"?interval="+a+"&range="+range;
#endif
  //DBG_PTN(url);
  bool requestOpenResult;
  //Serial.println(req_stock.headers());
  if (req_stock.readyState() == readyStateUnsent || req_stock.readyState() == readyStateDone) {

    requestOpenResult = req_stock.open("GET", url.c_str());
    if (requestOpenResult) {
      //要放到这里才生效
      req_stock.setReqHeader("User-Agent","Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36");
      req_stock.setReqHeader("Accept", "application/json");
      req_stock.setReqHeader("Connection", "keep-alive");
      //req_stock.setReqHeader("Connection", "close");
      //DBG_PTN(req_stock.headers());
      req_stock.send();
      req_stock_done = false;//完成请求，才可以发送下一个
      //http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36"));
    } else {
      DBG_PTN(("bad request" + url));
      req_stock_done = true;//完成请求，才可以发送下一个
    }
  } else {
    DBG_PTN("rt can't send req" + url);
    req_stock_done = true;//完成请求，才可以发送下一个
    //req_stock.abort();
  }
}
#include "HTTPClient.h"
bool getCookieFromYahoo() {
  //if (WiFi.status() == WL_CONNECTED) 

  {
    HTTPClient http;
    String url = ("https://fc.yahoo.com");
    //http.addHeader("User-Agent", "Mozilla/5.0 (compatible; yahoo-finance2/0.0.1)");
    http.setTimeout(8000);
    http.begin(url);
    //http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36");
    // 创建随机数（6位）
    long randomNum = random(100000, 1000000); // [100000, 999999]
    // 构造 User-Agent 字符串
    String userAgent = "Mozilla/5.0 (" + String(randomNum) + ")";
    http.setUserAgent(userAgent);
 
    //http.setUserAgent( "Mozilla/5.0 (X11; Linux x86_64)");
    //http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36"));
    DBG_PTN(url);
    // 定义我们感兴趣的HTTP头文件的键
    const char* headerKeys[] = {"Set-Cookie"};
    size_t headerKeysCount = sizeof(headerKeys) / sizeof(char*);

    // 只收集我们感兴趣的HTTP头文件
    http.collectHeaders(headerKeys, headerKeysCount);

    int httpCode = http.GET();
    if (httpCode > 0) {
      #if 0
        Serial.println("Headers received:");
        int headerCount = http.headers();
        for (int i = 0; i < headerCount; i++) {
          String headerName = http.headerName(i);
          String headerValue = http.header(i);
          Serial.println(headerName + ": " + headerValue);
        }
      #endif
        
      if (httpCode == HTTP_CODE_NOT_FOUND ||httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_FOUND) {
        // Print all headers
        
        String header = http.header("set-cookie");
        if (header.length() > 0) {
          //cookie = header.substring(0, header.indexOf(';'));
          run_data->cookie = header;
          //Serial.println("rsp Cookie: " + run_data->cookie);
          http.end();
          set_yahoo_cookie(run_data->cookie);
          return true;
        }
      }
      DBG_PTN(httpCode);
    } else {
      DBG_PTN("Error on HTTP request: ");
      DBG_PTN(httpCode);
    }

    http.end();
  } 
  return false;
}

bool getCrumbFromYahoo() {
  //if (WiFi.status() == WL_CONNECTED) 
  {
    HTTPClient http;
    String url = ("https://query2.finance.yahoo.com/v1/test/getcrumb");
    DBG_PTN(url);
    http.begin(url);
    //DBG_PTN(run_data->cookie);
    http.addHeader("Cookie", run_data->cookie);
    http.setReuse(true);
    //http.setUserAgent(("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36"));
    http.setUserAgent( "Mozilla/5.0 (X11; Linux x86_64)");
    DBG_PTN(http.header("Cookie"));
    int httpCode = http.GET();
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) 
      {
        run_data->crumb = http.getString();
        //Serial.println("rsp Crumb: " + run_data->crumb);
        http.end();
        set_yahoo_crumb(run_data->crumb);
        return true;
      }
      DBG_PTN(httpCode);
      DBG_PTN(http.getString());
      //run_data->cookie = "";
      //set_yahoo_cookie("");
    } else {
      DBG_PTN("Error on HTTP request: ");
      DBG_PTN(httpCode);
    }

    http.end();
  } 
  return false;
}
bool get_today_price_from_yahoo() {
  //if (WiFi.status() == WL_CONNECTED) 
  {
    HTTPClient http;
    String url = ("https://query1.finance.yahoo.com/v7/finance/quote?&symbols="+run_data->stock_id+"&fields=currency,regularMarketChange,regularMarketChangePercent,regularMarketPrice&crumb="+run_data->crumb);
    DBG_PTN(url);
    http.begin(url);
    http.addHeader("Cookie", run_data->cookie);//http.begin 必须在前，添加的header 才生效 20250507
    //http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36"));
    // 创建随机数（6位）
    long randomNum = random(100000, 1000000); // [100000, 999999]
    // 构造 User-Agent 字符串
    String userAgent = "Mozilla/5.0 (" + String(randomNum) + ")";
    http.setUserAgent(userAgent);
    //http.setUserAgent(USER_AGENT);

    int httpCode = http.GET();
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DBG_PTN(payload);
        http.end();
        // 解析JSON数据
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
          DBG_PTN("deserializeJson() failed: " + String(error.c_str()));
          return false;
        }
        
        // 获取当前价格和涨跌幅
        float regularMarketPrice = doc["quoteResponse"]["result"][0]["regularMarketPrice"].as<float>();
        float regularMarketChange = doc["quoteResponse"]["result"][0]["regularMarketChange"].as<float>();
        float regularMarketChangePercent = doc["quoteResponse"]["result"][0]["regularMarketChangePercent"].as<float>();
        DBG_PTN(regularMarketPrice);
        DBG_PTN(regularMarketChangePercent);
        //run_data->stockdata.percent = roundf(regularMarketChangePercent);
        run_data->stockdata.percent = (regularMarketChangePercent);
        return true;
      }else{
        DBG_PTN("Error on HTTP request: ");
        DBG_PTN(httpCode);
        if(httpCode == 401 || httpCode == 403){
          run_data->cookie="";//清空 cookie，重新获取 crumb
          run_data->crumb="";
          set_yahoo_cookie("");
          set_yahoo_crumb("");
        }
        if(httpCode == 429)
          cfg_data.updateInterval = 60;
        //run_data->cookie="";//清空 cookie，重新获取 crumb
        //run_data->crumb="";
        //set_yahoo_cookie("");
        //set_yahoo_crumb("");
      }
    } else {
      DBG_PTN("Error on HTTP request: ");
      DBG_PTN(httpCode);
    }
    http.end();
  } 
  return false;
}

void async_http_get_stock() {
  // Step 1: Get Set-Cookie from Yahoo
  if(run_data->cookie == "" || run_data->crumb ==""){
    if (getCookieFromYahoo()) {
      // Step 2: Use the cookie to get the crumb
      if (getCrumbFromYahoo()) {
        //Serial.println("Crumb: " + run_data->crumb);
        //Serial.println("Cookie: " +run_data->cookie);
      } else {
        DBG_PTN("Failed to get c.");
      }
    }
  }

  if(run_data->crumb != ""){
    get_today_price_from_yahoo();
  }

  req_stock.setDebug(false);
  req_stock.onReadyStateChange(req_stock_cb);
  send_req_stock();
}
#include <HTTPClient.h>
//记录：使用同步方式请求http雅虎接口会出现301转发。但使用异步接口访问http，却能得到正确返回。20250601 
void get_kline_data_yahoo(){
  HTTPClient http;
  //String finnhub_url = "https://finnhub.io/api/v1/stock/candle?symbol="+STOCK_NAME+"&resolution=5&from="+String(now()-50*30*60)+"&to="+String(now())+"&token=ch7rk9hr01qhapm5f7j0ch7rk9hr01qhapm5f7jg";
  //String finnhub_url = "https://finnhub.io/api/v1/stock/candle?symbol="+STOCK_NAME+"&resolution=5&from=1682969996&to=1683022246&token=ch7rk9hr01qhapm5f7j0ch7rk9hr01qhapm5f7jg";
  //unsigned long current_time = now(); // 获取当前 UNIX 时间戳
  //unsigned long start_timestamp = current_time - (45 * 24 * 60 * 60); // 计算 30 天前的 UNIX 时间戳
  //String finnhub_url = "https://finnhub.io/api/v1/stock/candle?symbol="+run_data->stock_id+"&resolution=D&from="+String(start_timestamp)+"&to="+String(current_time)+"&token=ch7rk9hr01qhapm5f7j0ch7rk9hr01qhapm5f7jg";
  //[1m, 2m, 5m, 15m, 30m, 60m, 90m, 1h, 1d, 5d, 1wk, 1mo, 3mo]
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
  //String range = String(value*30)+unit;
  String range = String(value*CANDLE_NUMS+10)+unit;
  DBG_PTN(range);
  String url = "https://query1.finance.yahoo.com/v8/finance/chart/"+run_data->stock_id+"?interval="+a+"&range="+range;

  //DBG_PTN(url);
  //http.addHeader("user-agent", "Mozilla/5.0");
  http.setTimeout(8000);
  http.setConnectTimeout(5000);
  http.begin(url);
  // 创建随机数（6位）
  long randomNum = random(100000, 1000000); // [100000, 999999]
  // 构造 User-Agent 字符串
  String userAgent = "Mozilla/5.0 (" + String(randomNum) + ")";
  http.setUserAgent(userAgent);
 
  //http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36"));
  //http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  DBG_PTN(http.headers());
#ifndef DEBUG_STOCK
  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
#else
  if(1){
#endif
    DynamicJsonDocument doc(1024 * 12);
    //JsonDocument doc;
    String payload = http.getString();
    //String payload = R"({"chart":{"result":[{"meta":{"currency":"USD","symbol":"AMZN","exchangeName":"NMS","instrumentType":"EQUITY","firstTradeDate":863703000,"regularMarketTime":1683057604,"gmtoffset":-14400,"timezone":"EDT","exchangeTimezoneName":"America/New_York","regularMarketPrice":103.63,"chartPreviousClose":102.05,"previousClose":102.05,"scale":3,"priceHint":2,"currentTradingPeriod":{"pre":{"timezone":"EDT","start":1683014400,"end":1683034200,"gmtoffset":-14400},"regular":{"timezone":"EDT","start":1683034200,"end":1683057600,"gmtoffset":-14400},"post":{"timezone":"EDT","start":1683057600,"end":1683072000,"gmtoffset":-14400}},"tradingPeriods":[[{"timezone":"EDT","start":1683034200,"end":1683057600,"gmtoffset":-14400}]],"dataGranularity":"1m","range":"30m","validRanges":["1d","5d","1mo","3mo","6mo","1y","2y","5y","10y","ytd","max"]},"timestamp":[1683055800,1683055860,1683055920,1683055980,1683056040,1683056100,1683056160,1683056220,1683056280,1683056340,1683056400,1683056460,1683056520,1683056580,1683056640,1683056700,1683056760,1683056820,1683056880,1683056940,1683057000,1683057060,1683057120,1683057180,1683057240,1683057300,1683057360,1683057420,1683057480,1683057540,1683057600],"indicators":{"quote":[{"high":[103.72000122070312,103.68000030517578,103.68000030517578,103.66419982910156,103.60990142822266,103.63829803466797,103.75,103.75,103.76499938964844,103.87000274658203,103.86710357666016,103.78710174560547,103.76499938964844,103.70999908447266,103.69999694824219,103.68070220947266,103.66999816894531,103.66000366210938,103.5999984741211,103.5999984741211,103.66000366210938,103.69999694824219,103.75,103.73500061035156,103.72000122070312,103.7249984741211,103.74800109863281,103.77999877929688,103.79000091552734,103.79000091552734,103.62999725341797],"close":[103.5999984741211,103.5999984741211,103.66500091552734,103.6050033569336,103.56500244140625,103.62999725341797,103.72840118408203,103.69000244140625,103.76000213623047,103.83999633789062,103.7500991821289,103.7300033569336,103.71009826660156,103.62999725341797,103.66000366210938,103.56999969482422,103.66000366210938,103.57499694824219,103.54000091552734,103.5999984741211,103.58999633789062,103.69999694824219,103.69000244140625,103.66500091552734,103.68000030517578,103.67060089111328,103.68000030517578,103.73500061035156,103.7750015258789,103.66000366210938,103.62999725341797],"open":[103.69999694824219,103.6050033569336,103.6052017211914,103.66419982910156,103.5999984741211,103.55500030517578,103.625,103.7300033569336,103.69000244140625,103.75,103.8396987915039,103.75499725341797,103.73500061035156,103.70999908447266,103.62000274658203,103.66500091552734,103.57499694824219,103.65989685058594,103.57499694824219,103.53500366210938,103.59809875488281,103.58999633789062,103.69999694824219,103.69419860839844,103.66000366210938,103.68000030517578,103.67500305175781,103.67500305175781,103.7300033569336,103.7750015258789,103.62999725341797],"volume":[0,200320,105044,107497,171676,156262,115712,153781,137695,197050,187189,152296,144236,134027,121863,163943,139638,110475,161123,231181,251760,232015,217153,199454,261127,276844,357772,371413,380628,862819,0],"low":[103.5999984741211,103.59500122070312,103.59439849853516,103.5999984741211,103.5,103.55500030517578,103.61000061035156,103.67009735107422,103.69000244140625,103.73999786376953,103.7500991821289,103.69999694824219,103.69999694824219,103.60240173339844,103.60250091552734,103.55010223388672,103.55000305175781,103.56500244140625,103.5199966430664,103.5186996459961,103.52999877929688,103.58999633789062,103.69000244140625,103.6500015258789,103.62999725341797,103.66000366210938,103.66000366210938,103.66999816894531,103.7300033569336,103.58000183105469,103.62999725341797]}]}}],"error":null}})";
    http.end();
    DBG_PTN(payload);
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
  DBG_PTN("price_str=");
  DBG_PTN(price_str);
  int prev_close_pos = payload.indexOf("chartPreviousClose");//百分比是依据这个计算
  int prev_close_start = payload.indexOf(":", prev_close_pos) + 1;
  int prev_close_end = payload.indexOf(",", prev_close_start);
  String prev_close_str = payload.substring(prev_close_start, prev_close_end);

  //float price1 = doc['chart']['result'][0]['meta']['regularMarketPrice'].as<float>();
  //DBG_PTN("price1 float =");
  //DBG_PTN(price1);
  //float price = price_str.toFloat();
  char *endPtr; // 指向字符串末尾的指针
  float price = strtof(price_str.c_str(), &endPtr); // 使用 strtof 转换为 float

  DBG_PTN("price float =");
  DBG_PTN(price);
  float prev_close = prev_close_str.toFloat();

    JsonArray open_prices = doc["chart"]["result"][0]["indicators"]["quote"][0]["open"];
    JsonArray high_prices = doc["chart"]["result"][0]["indicators"]["quote"][0]["high"];
    JsonArray low_prices = doc["chart"]["result"][0]["indicators"]["quote"][0]["low"];
    JsonArray close_prices = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"];
    
    // Process and draw the candle data using LVGL here
    my_stock_t* stock_data = &run_data->stockdata;
    stock_data->price = price;
    DBG_PTN(price);
    DBG_PTN(prev_close);
    stock_data->percent = ((price -prev_close) /prev_close * 100);
    DBG_PTN(stock_data->percent);
    stock_data->percent = (stock_data->percent * 10)/10;
    DBG_PTN(stock_data->percent);
    //这里有bug，char 太长，解析不出实时价格 20230508 ifeng
    //stock_data->price= doc['chart']['result'][0]['meta']['regularMarketPrice'];
    //stock_data->price= doc['chart']['result'][0]['meta']['regular'];
    //float previous_close_price = doc['chart']['result'][0]['meta']['chartPreviousClose'].as<float>();
    //float price_change = stock_data->price - previous_close_price;
    //stock_data->percent = (price_change / previous_close_price) * 100;

    stock_data->kline_updated = 1;
    run_data->refresh_status = 2;
    fill_candle_data(open_prices, close_prices, high_prices, low_prices, stock_data->candles, CANDLE_NUMS);
    //fill_candle_data(open_prices, close_prices, high_prices, low_prices, stock_data->candles, 30);
  } else {
    DBG_PTN("Err req: ");
#ifndef DEBUG_STOCK
    DBG_PTN(httpResponseCode);
#endif
    http.end();
  }
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
      for(int i= 0; i < STOCK_TOTAL; i++){
        run_data->stock_index ++;
        if(run_data->stock_index >= STOCK_TOTAL) run_data->stock_index = 0;

        if(cfg_data.stock_id[run_data->stock_index] != "") {
          run_data->stock_id = cfg_data.stock_id[run_data->stock_index];
          run_data->stockdata.stock_name = run_data->stock_id;
          break;
        }
      }
      DBG_PTN(run_data->stock_id);
    }
    if(cfg_data.stock_id[run_data->stock_index] == "") return;
    //get_kline_data_yahoo();
    async_http_get_stock();
    run_data->refresh_time_millis = millis();
    //get_realtime_stock_data();    
    //get_30_days_kline_data();
}

void exit_stock(){
//todo

  req_stock.abort();
  if(run_data){
    free(run_data);
    run_data = NULL;
  }
}