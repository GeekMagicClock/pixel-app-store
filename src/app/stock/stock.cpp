#include "stock.h"
#include "my_debug.h"
#include <Arduino.h>

// STOCKmarket的持久化配置
//#define B_CONFIG_PATH "/stockmarket.cfg"
#define STOCK_NAME_LEN 32
struct B_Config {
    uint32_t stock_scale;//哪种K线，默认5日，后续再增加其他种
    String stock_id[STOCK_NAME_LEN];              // bilibili的uid
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
        cfg->stock_id[3] = "TTOO";  // 股票代码
        cfg->stock_id[4] = "EBET";  // 股票代码
        cfg->stock_id[5] = "NVOS";  // 股票代码
        cfg->stock_id[6] = "OPGN";  // 股票代码
        cfg->stock_id[7] = "NKLA";  // 股票代码
        cfg->stock_id[8] = "AVTX";  // 股票代码
        cfg->stock_id[9] = "MCOM";  // 股票代码
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
void init_stock()
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

    //display_stockmarket(run_data->stockdata, LV_SCR_LOAD_ANIM_NONE);
    //display_stock(run_data->stockdata, LV_SCR_LOAD_ANIM_NONE);
    //display_stock(&run_data->stockdata, 30, LV_SCR_LOAD_ANIM_NONE);
}
#define STOCK_TOTAL 10
extern int enter_app;
extern int page_index;
static int last_page_index;
static int cur_stock_index = -1;
static unsigned long last_loop_time;

#include "time.h"

void fill_candle_data(JsonArray& open_prices, JsonArray& close_prices, JsonArray& high_prices, JsonArray& low_prices, CandleData* candles, int num_candles) {
  int num_data_points = min(open_prices.size(), min(close_prices.size(), min(high_prices.size(), low_prices.size())));
  for (int i = 0; i < num_data_points && i < num_candles; i++) {
    candles[i].open = open_prices[i].as<float>();
    candles[i].close = close_prices[i].as<float>();
    candles[i].high = high_prices[i].as<float>();
    candles[i].low = low_prices[i].as<float>();
  }
}
#include "HttpClient.h"
void get_realtime_price_yahoo(){
//  String url = "https://query1.finance.yahoo.com/v7/finance/quote?symbols="+run_data->stock_id;
  String url = "https://query1.finance.yahoo.com/v8/finance/chart/"+run_data->stock_id+"?interval=1m&range=1m";
  HTTPClient http;
  http.setTimeout(3000);
  http.setConnectTimeout(2000);
  //http.addHeader("user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/109.0.0.0 Safari/537.36");
  http.begin(url);
  http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36"));
  float current_price = 0;

#ifndef DEBUG_STOCK
  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) 
#else
  if (1) 
#endif
  {
    JsonDocument doc;
#ifndef DEBUG_STOCK
    String payload = http.getString();
#else
    String payload = R"({"quoteResponse":{"result":[{"language":"en-US","region":"US","quoteType":"EQUITY","typeDisp":"Equity","quoteSourceName":"Nasdaq Real Time Price","triggerable":true,"customPriceAlertConfidence":"HIGH","currency":"USD","marketState":"REGULAR","exchange":"NMS","shortName":"Amazon.com, Inc.","longName":"Amazon.com, Inc.","messageBoardId":"finmb_18749","exchangeTimezoneName":"America/New_York","exchangeTimezoneShortName":"EDT","gmtOffSetMilliseconds":-14400000,"market":"us_market","esgPopulated":false,"firstTradeDateMilliseconds":863703000000,"priceHint":2,"regularMarketChangePercent":1.3424743,"regularMarketPrice":103.42,"fiftyTwoWeekLowChangePercent":0.27004787,"fiftyTwoWeekRange":"81.43 - 146.57","fiftyTwoWeekHighChange":-43.15001,"fiftyTwoWeekHighChangePercent":-0.29439864,"fiftyTwoWeekLow":81.43,"fiftyTwoWeekHigh":146.57,"earningsTimestamp":1682631000,"earningsTimestampStart":1690369140,"earningsTimestampEnd":1690804800,"trailingAnnualDividendRate":0.0,"sourceInterval":15,"exchangeDataDelayedBy":0,"averageAnalystRating":"1.8 - Buy","tradeable":false,"cryptoTradeable":false,"regularMarketChange":1.3699951,"regularMarketTime":1683038182,"regularMarketDayHigh":103.79,"regularMarketDayRange":"101.15 - 103.79","regularMarketDayLow":101.15,"regularMarketVolume":23356449,"regularMarketPreviousClose":102.05,"bid":103.33,"ask":103.36,"bidSize":11,"askSize":9,"fullExchangeName":"NasdaqGS","financialCurrency":"USD","regularMarketOpen":101.47,"averageDailyVolume3Month":62884950,"averageDailyVolume10Day":80597650,"fiftyTwoWeekLowChange":21.989998,"trailingAnnualDividendYield":0.0,"epsTrailingTwelveMonths":-0.26,"epsForward":2.57,"epsCurrentYear":1.55,"priceEpsCurrentYear":66.72258,"sharesOutstanding":10257999872,"bookValue":14.259,"fiftyDayAverage":99.02,"fiftyDayAverageChange":4.4000015,"fiftyDayAverageChangePercent":0.044435486,"twoHundredDayAverage":106.78505,"twoHundredDayAverageChange":-3.3650513,"twoHundredDayAverageChangePercent":-0.031512383,"marketCap":1060882350080,"forwardPE":40.241245,"priceToBook":7.252963,"displayName":"Amazon.com","symbol":"AMZN"}],"error":null}})";
#endif
    http.end();
    DBG_PTN(payload);
    auto err = deserializeJson(doc, payload);
    if (err) {
      DBG_PTN("deseri failed: ");
      DBG_PTN(err.c_str());
      return;
    }

    run_data->stockdata.price = doc['chart']['result'][0]['meta']['regularMarketPrice'].as<float>();
    //run_data->stockdata.price = doc["quoteResponse"]["result"][0]["regularMarketPrice"];
    //run_data->stockdata.percent  = doc["quoteResponse"]["result"][0]["regularMarketChangePercent"];
    float previous_close_price = doc['chart']['result'][0]['meta']['chartPreviousClose'].as<float>();
    float price_change = run_data->stockdata.price - previous_close_price;
    run_data->stockdata.percent = (price_change / previous_close_price) * 10;

    //DBG_PTN(run_data->stockdata.price);
    //DBG_PTN(run_data->stockdata.percent);
  } else {
    DBG_PTN("Err HTTP req: ");
    //DBG_PTN(httpResponseCode);
    http.end();
  }
  return;
}

#define DEBUG_STOCK 1

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
  String range = String(value*30)+unit;
  DBG_PTN(range);
  String url = "https://query1.finance.yahoo.com/v8/finance/chart/"+run_data->stock_id+"?interval="+a+"&range="+range;

  //DBG_PTN(url);
  //http.addHeader("user-agent", "Mozilla/5.0");
  http.setTimeout(5000);
  http.setConnectTimeout(5000);
  http.begin(url);
  http.setUserAgent(F("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36"));
#ifndef DEBUG_STOCK
  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
#else
  if(1){
#endif
    JsonDocument doc;
#ifndef DEBUG_STOCK
    String payload = http.getString();
#else
    //String payload = R"({"chart":{"result":[{"meta":{"currency":"USD","symbol":"BTC-USD","exchangeName":"CCC","fullExchangeName":"CCC","instrumentType":"CRYPTOCURRENCY","firstTradeDate":1410912000,"regularMarketTime":1711951860,"hasPrePostMarketData":false,"gmtoffset":0,"timezone":"UTC","exchangeTimezoneName":"UTC","regularMarketPrice":69269.51,"fiftyTwoWeekHigh":71312.17,"fiftyTwoWeekLow":69220.8,"regularMarketDayHigh":71312.17,"regularMarketDayLow":69220.8,"regularMarketVolume":22592059392,"chartPreviousClose":71312.17,"previousClose":71312.17,"scale":3,"priceHint":2,"currentTradingPeriod":{"pre":{"timezone":"UTC","start":1711929600,"end":1711929600,"gmtoffset":0},"regular":{"timezone":"UTC","start":1711929600,"end":1712015940,"gmtoffset":0},"post":{"timezone":"UTC","start":1712015940,"end":1712015940,"gmtoffset":0}},"tradingPeriods":[[{"timezone":"UTC","start":1711843200,"end":1711929540,"gmtoffset":0}],[{"timezone":"UTC","start":1711929600,"end":1712015940,"gmtoffset":0}]],"dataGranularity":"1h","range":"30h","validRanges":["1d","5d","1mo","3mo","6mo","1y","2y","5y","10y","ytd","max"]},"timestamp":[1711846800,1711850400,1711854000,1711857600,1711861200,1711864800,1711868400,1711872000,1711875600,1711879200,1711882800,1711886400,1711890000,1711893600,1711897200,1711900800,1711904400,1711908000,1711911600,1711915200,1711918800,1711922400,1711926000,1711929600,1711933200,1711936800,1711940400,1711944000,1711947600,1711951200,1711951860],"indicators":{"quote":[{"high":[70082.25,70137.1953125,69989.25,70123.4140625,70452.046875,70383.65625,70336.203125,70463.578125,70326.8359375,70330.0078125,70437.1015625,70560.71875,70740.7265625,70680.8046875,70553.9921875,70676.7890625,70551.5234375,71209.4296875,71120.15625,71109.4296875,70951.953125,71051.2734375,71354.8515625,71312.171875,71178.453125,70910.9609375,70891.578125,70670.0625,70525.1953125,69385.6484375,69269.5078125],"volume":[0,32086016,0,0,239124480,21594112,100079616,0,0,0,0,191879168,378425344,161843200,133246976,52795392,0,676413440,145086464,0,360996864,236505088,486950912,366051328,0,159588352,268599296,223326208,1257510912,694480896,0],"open":[69894.8125,70051.3515625,69984.2421875,69947.6171875,70123.9609375,70356.59375,70234.234375,70303.3203125,70273.3984375,70241.921875,70277.4140625,70387.984375,70518.8671875,70598.6640625,70553.9921875,70405.203125,70491.6484375,70515.1171875,70875.578125,71022.203125,70847.7421875,70854.9609375,70971.9140625,71312.171875,71172.2734375,70855.3125,70887.2734375,70623.0,70468.71875,69220.796875,69269.5078125],"close":[70050.0859375,69989.3203125,69937.15625,70123.4140625,70349.71875,70250.046875,70322.7421875,70259.1953125,70266.0234375,70283.40625,70386.2890625,70513.5625,70593.2890625,70554.0859375,70400.53125,70477.9296875,70513.484375,70854.9140625,71026.21875,70858.1171875,70782.8671875,70963.515625,71354.8515625,71158.453125,70849.203125,70887.28125,70619.5625,70488.375,69357.3671875,69379.9296875,69269.5078125],"low":[69876.0,69968.0703125,69810.4609375,69888.40625,70073.3359375,70208.8984375,70204.046875,70259.1953125,70207.3203125,70231.046875,70277.4140625,70387.984375,70518.8671875,70478.8828125,70346.78125,70401.1953125,70466.28125,70452.9140625,70705.2265625,70770.0,70711.25,70854.9609375,70956.5703125,70925.5546875,70849.203125,70803.8828125,70561.9765625,70482.0546875,69157.265625,68986.9453125,69269.5078125]}]}}],"error":null}})";
    String payload = R"({"chart":{"result":[{"meta":{"currency":"USD","symbol":"BTC-USD","exchangeName":"CCC","fullExchangeName":"CCC","instrumentType":"CRYPTOCURRENCY","firstTradeDate":1410912000,"regularMarketTime":1711953180,"hasPrePostMarketData":false,"gmtoffset":0,"timezone":"UTC","exchangeTimezoneName":"UTC","regularMarketPrice":69450.23,"fiftyTwoWeekHigh":71312.17,"fiftyTwoWeekLow":69220.8,"regularMarketDayHigh":71312.17,"regularMarketDayLow":69220.8,"regularMarketVolume":23178309632,"chartPreviousClose":69892.83,"previousClose":71312.17,"scale":3,"priceHint":2,"currentTradingPeriod":{"pre":{"timezone":"UTC","start":1711929600,"end":1711929600,"gmtoffset":0},"regular":{"timezone":"UTC","start":1711929600,"end":1712015940,"gmtoffset":0},"post":{"timezone":"UTC","start":1712015940,"end":1712015940,"gmtoffset":0}},"tradingPeriods":[[{"timezone":"UTC","start":1711670400,"end":1711756740,"gmtoffset":0}],[{"timezone":"UTC","start":1711756800,"end":1711843140,"gmtoffset":0}],[{"timezone":"UTC","start":1711843200,"end":1711929540,"gmtoffset":0}],[{"timezone":"UTC","start":1711929600,"end":1712015940,"gmtoffset":0}]],"dataGranularity":"1h","range":"64h","validRanges":["1d","5d","1mo","3mo","6mo","1y","2y","5y","10y","ytd","max"]},"timestamp":[1711724400,1711728000,1711731600,1711735200,1711738800,1711742400,1711746000,1711749600,1711753200,1711756800,1711760400,1711764000,1711767600,1711771200,1711774800,1711778400,1711782000,1711785600,1711789200,1711792800,1711796400,1711800000,1711803600,1711807200,1711810800,1711814400,1711818000,1711821600,1711825200,1711828800,1711832400,1711836000,1711839600,1711843200,1711846800,1711850400,1711854000,1711857600,1711861200,1711864800,1711868400,1711872000,1711875600,1711879200,1711882800,1711886400,1711890000,1711893600,1711897200,1711900800,1711904400,1711908000,1711911600,1711915200,1711918800,1711922400,1711926000,1711929600,1711933200,1711936800,1711940400,1711944000,1711947600,1711951200,1711953180],"indicators":{"quote":[{"open":[70105.4375,69179.6171875,69182.6328125,69428.8046875,69495.9453125,69673.2890625,69639.6640625,69449.0,69822.0234375,69889.7734375,69969.0390625,70026.0625,69979.453125,69815.1875,70100.828125,70049.4609375,70008.2578125,69893.171875,69994.140625,70040.0703125,70062.1015625,70216.2890625,70220.53125,70186.890625,70089.1875,70051.1953125,70231.171875,70088.8984375,70007.234375,69942.609375,69868.1640625,69789.703125,69744.015625,69647.7421875,69894.8125,70051.3515625,69984.2421875,69947.6171875,70123.9609375,70356.59375,70234.234375,70303.3203125,70273.3984375,70241.921875,70277.4140625,70387.984375,70518.8671875,70598.6640625,70553.9921875,70405.203125,70491.6484375,70515.1171875,70875.578125,71022.203125,70847.7421875,70854.9609375,70971.9140625,71312.171875,71172.2734375,70855.3125,70887.2734375,70623.0,70468.71875,69220.796875,69450.2265625],"high":[70134.203125,69550.1328125,69455.4609375,69599.8984375,69676.5859375,69725.5,69662.0,69830.375,69932.4453125,69960.109375,70171.3984375,70039.9609375,70056.46875,70109.734375,70342.1953125,70117.171875,70058.8125,70035.2265625,70160.2109375,70131.9921875,70206.546875,70252.4453125,70279.2265625,70210.0625,70175.6484375,70278.3359375,70231.171875,70088.8984375,70007.234375,69942.609375,69884.8125,69848.9921875,69791.3359375,69975.7890625,70082.25,70137.1953125,69989.25,70123.4140625,70452.046875,70383.65625,70336.203125,70463.578125,70326.8359375,70330.0078125,70437.1015625,70560.71875,70740.7265625,70680.8046875,70553.9921875,70676.7890625,70551.5234375,71209.4296875,71120.15625,71109.4296875,70951.953125,71051.2734375,71354.8515625,71312.171875,71178.453125,70910.9609375,70891.578125,70670.0625,70525.1953125,69495.0546875,69450.2265625],"close":[69208.921875,69176.1953125,69441.4296875,69486.28125,69676.5859375,69632.421875,69443.3125,69822.015625,69898.046875,69960.109375,70023.8671875,69976.9765625,69802.0859375,70105.6171875,70046.8203125,70033.921875,69902.7265625,69996.2734375,70044.859375,70059.34375,70206.546875,70220.53125,70185.1640625,70094.7265625,70049.546875,70239.1953125,70089.140625,70007.015625,69951.1171875,69875.890625,69792.234375,69743.3671875,69661.2265625,69896.4140625,70050.0859375,69989.3203125,69937.15625,70123.4140625,70349.71875,70250.046875,70322.7421875,70259.1953125,70266.0234375,70283.40625,70386.2890625,70513.5625,70593.2890625,70554.0859375,70400.53125,70477.9296875,70513.484375,70854.9140625,71026.21875,70858.1171875,70782.8671875,70963.515625,71354.8515625,71158.453125,70849.203125,70887.28125,70619.5625,70488.375,69357.3671875,69495.0546875,69450.2265625],"volume":[0,0,0,0,0,0,0,20166656,0,0,61065216,0,0,0,244492288,0,0,0,0,0,0,0,0,0,0,0,0,0,233680896,0,0,0,0,147933184,14798848,32086016,0,0,239124480,21594112,100079616,0,0,0,0,191879168,378425344,161843200,133246976,52795392,0,676413440,145086464,0,360996864,236505088,486950912,366051328,0,159588352,268599296,223326208,1257510912,1355702272,0],"low":[69140.4765625,69176.1953125,69182.6328125,69361.6171875,69401.828125,69580.6015625,69331.1328125,69449.0,69808.296875,69807.4609375,69884.984375,69896.8046875,69802.0859375,69807.390625,69947.9609375,69934.65625,69902.7265625,69856.8359375,69964.15625,70000.3671875,70062.1015625,70153.4453125,70117.9453125,70051.9296875,69992.578125,69968.8203125,70060.7265625,69942.421875,69771.0546875,69847.65625,69680.796875,69705.4609375,69601.0625,69647.7421875,69876.0,69968.0703125,69810.4609375,69888.40625,70073.3359375,70208.8984375,70204.046875,70259.1953125,70207.3203125,70231.046875,70277.4140625,70387.984375,70518.8671875,70478.8828125,70346.78125,70401.1953125,70466.28125,70452.9140625,70705.2265625,70770.0,70711.25,70854.9609375,70956.5703125,70925.5546875,70849.203125,70803.8828125,70561.9765625,70482.0546875,69157.265625,68986.9453125,69450.2265625]}]}}],"error":null}})";
    //String payload = R"({"chart":{"result":[{"meta":{"currency":"USD","symbol":"AMZN","exchangeName":"NMS","instrumentType":"EQUITY","firstTradeDate":863703000,"regularMarketTime":1683057604,"gmtoffset":-14400,"timezone":"EDT","exchangeTimezoneName":"America/New_York","regularMarketPrice":103.63,"chartPreviousClose":102.05,"previousClose":102.05,"scale":3,"priceHint":2,"currentTradingPeriod":{"pre":{"timezone":"EDT","start":1683014400,"end":1683034200,"gmtoffset":-14400},"regular":{"timezone":"EDT","start":1683034200,"end":1683057600,"gmtoffset":-14400},"post":{"timezone":"EDT","start":1683057600,"end":1683072000,"gmtoffset":-14400}},"tradingPeriods":[[{"timezone":"EDT","start":1683034200,"end":1683057600,"gmtoffset":-14400}]],"dataGranularity":"1m","range":"30m","validRanges":["1d","5d","1mo","3mo","6mo","1y","2y","5y","10y","ytd","max"]},"timestamp":[1683055800,1683055860,1683055920,1683055980,1683056040,1683056100,1683056160,1683056220,1683056280,1683056340,1683056400,1683056460,1683056520,1683056580,1683056640,1683056700,1683056760,1683056820,1683056880,1683056940,1683057000,1683057060,1683057120,1683057180,1683057240,1683057300,1683057360,1683057420,1683057480,1683057540,1683057600],"indicators":{"quote":[{"high":[103.72000122070312,103.68000030517578,103.68000030517578,103.66419982910156,103.60990142822266,103.63829803466797,103.75,103.75,103.76499938964844,103.87000274658203,103.86710357666016,103.78710174560547,103.76499938964844,103.70999908447266,103.69999694824219,103.68070220947266,103.66999816894531,103.66000366210938,103.5999984741211,103.5999984741211,103.66000366210938,103.69999694824219,103.75,103.73500061035156,103.72000122070312,103.7249984741211,103.74800109863281,103.77999877929688,103.79000091552734,103.79000091552734,103.62999725341797],"close":[103.5999984741211,103.5999984741211,103.66500091552734,103.6050033569336,103.56500244140625,103.62999725341797,103.72840118408203,103.69000244140625,103.76000213623047,103.83999633789062,103.7500991821289,103.7300033569336,103.71009826660156,103.62999725341797,103.66000366210938,103.56999969482422,103.66000366210938,103.57499694824219,103.54000091552734,103.5999984741211,103.58999633789062,103.69999694824219,103.69000244140625,103.66500091552734,103.68000030517578,103.67060089111328,103.68000030517578,103.73500061035156,103.7750015258789,103.66000366210938,103.62999725341797],"open":[103.69999694824219,103.6050033569336,103.6052017211914,103.66419982910156,103.5999984741211,103.55500030517578,103.625,103.7300033569336,103.69000244140625,103.75,103.8396987915039,103.75499725341797,103.73500061035156,103.70999908447266,103.62000274658203,103.66500091552734,103.57499694824219,103.65989685058594,103.57499694824219,103.53500366210938,103.59809875488281,103.58999633789062,103.69999694824219,103.69419860839844,103.66000366210938,103.68000030517578,103.67500305175781,103.67500305175781,103.7300033569336,103.7750015258789,103.62999725341797],"volume":[0,200320,105044,107497,171676,156262,115712,153781,137695,197050,187189,152296,144236,134027,121863,163943,139638,110475,161123,231181,251760,232015,217153,199454,261127,276844,357772,371413,380628,862819,0],"low":[103.5999984741211,103.59500122070312,103.59439849853516,103.5999984741211,103.5,103.55500030517578,103.61000061035156,103.67009735107422,103.69000244140625,103.73999786376953,103.7500991821289,103.69999694824219,103.69999694824219,103.60240173339844,103.60250091552734,103.55010223388672,103.55000305175781,103.56500244140625,103.5199966430664,103.5186996459961,103.52999877929688,103.58999633789062,103.69000244140625,103.6500015258789,103.62999725341797,103.66000366210938,103.66000366210938,103.66999816894531,103.7300033569336,103.58000183105469,103.62999725341797]}]}}],"error":null}})";
#endif
    http.end();
    //DBG_PTN(payload);
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
    stock_data->percent = ((price -prev_close) /prev_close * 10);
    stock_data->percent = roundf(stock_data->percent * 10)/10;
    //这里有bug，char 太长，解析不出实时价格 20230508 ifeng
    //stock_data->price= doc['chart']['result'][0]['meta']['regularMarketPrice'];
    //stock_data->price= doc['chart']['result'][0]['meta']['regular'];
    //float previous_close_price = doc['chart']['result'][0]['meta']['chartPreviousClose'].as<float>();
    //float price_change = stock_data->price - previous_close_price;
    //stock_data->percent = (price_change / previous_close_price) * 100;

    stock_data->kline_updated = 1;
    fill_candle_data(open_prices, close_prices, high_prices, low_prices, stock_data->candles, CANDLE_NUMS);
  } else {
    DBG_PTN("Err req: ");
#ifndef DEBUG_STOCK
    DBG_PTN(httpResponseCode);
#endif
    http.end();
  }
}

void update_stock(bool force){
    if(!force) return;
    //display_stock(&run_data->stockdata, 30, LV_SCR_LOAD_ANIM_FADE_IN);
    get_kline_data_yahoo();
    //get_realtime_stock_data();    
    //get_30_days_kline_data();
}

void exit_stock(){
//todo
  free(run_data);
}