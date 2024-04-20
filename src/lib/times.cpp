#include <TimeLib.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <WiFi.h>
#include <Arduino.h> 
#include <HTTPClient.h>

#include "settings.h"
#include "my_debug.h"

extern uint16_t bgColor;
int years;
int months;
int today;
int hours = 0;
int minutes = 0;
int sec = 0;
String weekCN = "";
String monthCN = "";
String monthEN = "";
String my_ntp_server = "";

int timeZone = 8;     //东八区
int minutesTimeZone = 0;//分钟的时区偏移，还有相差半小时的时区
//NTP服务器参数
//很奇怪的bug，当使用PROGMEM的时候，会引起ipaddress的fromString4 崩溃, 20230819
//const char ntpServerName[] PROGMEM = "ntp.aliyun.com";
//const char ntpServerName[] = "ntp.aliyun.com";
//static const char ntpServerName[] = "hk.pool.ntp.org";
//static const char ntpServerName2[] PROGMEM = ("time.windows.com");
//static const char ntpServerName3[] PROGMEM = ("pool.ntp.org");
//static const char ntpServerName4[] PROGMEM = ("hk.pool.ntp.org");
//static const char ntpServerName5[] PROGMEM = ("asia.pool.ntp.org");
const char ntpServerName[][20] = {
    "ntp.aliyun.com",
    "time.windows.com",
    "pool.ntp.org",
    "hk.pool.ntp.org",
    "asia.pool.ntp.org"
};

//函数声明
time_t getNtpTime();
//String num2str(int digits);
void sendNTPpacket(IPAddress &address);

WiFiClient wificlient;
WiFiUDP Udp;

int hour12 = 0;//12 小时制显示
//bool hour12 = false;//24 小时制显示

String monthDay();

void update_time(){

  if(hour12)
    hours = hourFormat12();
  else
    hours = hour();

  minutes = minute();
  sec = second();
  years = year();
  months = month();
  monthCN = monthDay();
  monthEN = String(year())+"/"+String(month())+"/"+String(day());
  today = day(); 
  //getNtpTime();
}
//extern const String wk[];
//星期
#if 0
//const char wk[][8] = {"日","一","二","三","四","五","六"};
const char wk[][8] PROGMEM = {"日","一","二","三","四","五","六"};
String week()
{
  //String wk[7] = {"日","一","二","三","四","五","六"};
  String s = "周" + String(wk[weekday()-1]);
  return s;
}
#endif

//月日
String monthDay()
{
  String s = String(month());
  s = s + "月" + day() + "日";
  //s = String(year())+"/"+String(month())+"/"+String(day());
  return s;
}

//判断给定年是否闰年
bool isLeapYear(int iYear) {
  if (iYear % 100 == 0)
    return ((iYear % 400 == 0));
  else
    return ((iYear % 4 == 0));
}

int getRemainDays(int iYear1, int iMonth1, int iDay1, int iYear2, int iMonth2, int iDay2)   //1. 确保 日期1 < 日期2
{
  if(iYear1 > iYear2) return 0;
  if(iYear1 == iYear2 && iMonth1 > iMonth2) return 0;
  if(iYear1 == iYear2 && iMonth1 == iMonth2 && iDay1 >= iDay2) return 0;

  unsigned int iDate1 = iYear1 * 10000 + iMonth1 * 100 + iDay1;
  unsigned int iDate2 = iYear2 * 10000 + iMonth2 * 100 + iDay2;
  if (iDate1 > iDate2)
  {
    iYear1 = iYear1 + iYear2 - (iYear2 = iYear1);
    iMonth1 = iMonth1 + iMonth2 - (iMonth2 = iMonth1);
    iDay1 = iDay1 + iDay2 - (iDay2 = iDay1);
  }
  
  //2. 开始计算天数
  //计算 从 iYear1年1月1日 到 iYear2年1月1日前一天 之间的天数
  int iDays = 0;
  for (int i = iYear1; i < iYear2; i++)
  {
    iDays += (isLeapYear(i) ? 366 : 365);
  }
  
  //减去iYear1年前iMonth1月的天数
  for (int i = 1; i < iMonth1; i++)
  {
    switch (i)
    {
    case 1: case 3: case 5: case 7: case 8: case 10: case 12:
      iDays -= 31;
      break;
    case 4: case 6: case 9: case 11:
      iDays -= 30;
      break;
    case 2:
      iDays -= (isLeapYear(iYear1) ? 29 : 28);
      break;
    }
  }
  //减去iYear1年iMonth1月前iDay1的天数
  iDays -= (iDay1 - 1);
  //加上iYear2年前iMonth2月的天数
  for (int i = 1; i < iMonth2; i++)
  {
    switch (i)
    {
    case 1: case 3: case 5: case 7: case 8: case 10: case 12:
      iDays += 31;
      break;
    case 4: case 6: case 9: case 11:
      iDays += 30;
      break;
    case 2:
      iDays += (isLeapYear(iYear2) ? 29 : 28);
      break;
    }
  }
  //加上iYear2年iMonth2月前iDay2的天数
  iDays += (iDay2 - 1);
  return iDays;
}

#if 1
//wifi连接UDP设置参数
unsigned int localPort = 8000;
bool udp_time_fail = true;
bool last_udp_time_fail = true;
#define UDP_TIME_UPDATE_INTERVAL 300 //5minutes

//time_t sync_http_time();
void sync_udp_time(){
  //udp 同步成功，后续就启用 udp 间隔同步时间
  if (last_udp_time_fail != udp_time_fail) {
    DBG_PTN("enable UDP update.");
    setSyncProvider(getNtpTime);
    setSyncInterval(UDP_TIME_UPDATE_INTERVAL); 
    last_udp_time_fail = udp_time_fail;
  }
}

void init_time(){
  Udp.begin(localPort);
  //DBG_PTN("等待同步...");
  //setSyncProvider(getNtpTime);
  getNtpTime();
  //setSyncProvider(sync_http_time);
  // 5 分钟同步一次
  //setSyncInterval(300);
  //update_time();
}

//#include "lib/NtpClientLib.h"
const int NTP_PACKET_SIZE = 48; // NTP时间在消息的前48字节中
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t syncNtp(const char *serverName){
  IPAddress ntpServerIP; // NTP server's ip address
  WiFi.hostByName(serverName, ntpServerIP);
  DBG_PTN(serverName);
  //Serial.print(": ");
  //DBG_PTN(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 2500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      //DBG_PTN("Receive NTP Response");
      //DBG_PTN(serverName);
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      //udp同步成功，开启5分钟同步一次时间
      udp_time_fail = false;
      DBG_PTN("UDP 同步成功.");
      //会崩溃 setSyncProvider(getNtpTime);
      //setSyncInterval(300);
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      //DBG_PTN(secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
      unsigned long tt =  secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR + minutesTimeZone*SECS_PER_MIN;
      setTime(tt);
      DBG_PTN(tt);
      DBG_PTN(hour(now()));
      return tt;
    }
  }
  return 0; // 无法获取时间时返回0
}

extern String my_ntp_server;
bool http_time_fail = true;
time_t getNtpTime() {
  //IPAddress ntpServerIP; // NTP server's ip address
  //如果http同步成功，那说明udp第一次是失败了的，不必再继续udp同步，否则界面卡住
  //if(http_time_fail == false) return 0;

  time_t tt;
  if(my_ntp_server != ""){
    tt = syncNtp(my_ntp_server.c_str());
    if(tt) return tt;
  }

  //try backup ntp server
  tt = syncNtp(ntpServerName[0]);
  if(tt) return tt;

  tt = syncNtp(ntpServerName[1]);
  if(tt) return tt;

  tt = syncNtp(ntpServerName[2]);
  if(tt) return tt;

  tt = syncNtp(ntpServerName[3]);
  if(tt) return tt;

  tt = syncNtp(ntpServerName[4]);
  if(tt) return tt;

  return 0;
}

// 向NTP服务器发送请求
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
#endif

#if 0
extern WiFiClient wificlient;
extern int minutesTimeZone;
unsigned long http_update_start_time = 0;
void sync_http_time(){
  if(!udp_time_fail) return; //udp 同步成功，则不启用http同步
  if(http_time_fail && millis() - http_update_start_time < 10000) return;//http 同步失败情况下，10s 同步一次
  if(!http_time_fail && millis() - http_update_start_time < 1000*3600) return;//http 同步成功后每隔60m同步一次
  http_update_start_time = millis();

  HTTPClient http;
  //http.begin(wificlient, F("http://worldtimeapi.org/api/timezone/UTC")); 
  String url = "http://worldtimeapi.org/api/timezone/UTC";
  DBG_PTN(url);
  //http.setUserAgent(F("Mozilla/5.0 (iPhone; CPU iPhone OS 14_1 like Mac OS X) AppleWebKit/604.1.38 (KHTML, like Gecko) Version/11.0 Mobile/15A372 Safari/604.1"));

  //http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.begin(wificlient, url); 
  int httpCode = http.GET();
  
  //如果服务器响应OK则从服务器获取响应体信息并通过串口输出
  if (httpCode == HTTP_CODE_OK) {
    int len = http.getSize();
    if(len <= 0) {
      //DBG_PTN("empty body");
      http.end();
      return;
    }
    //const String &payload = http.getString();//此处有bug，遇到网络不好，会阻塞到这里，一直阻塞，超时不管用
    WiFiClient * stream = http.getStreamPtr();
    String payload = "";
    DBG_PTN("Forecast received:");
    while (stream->available()) {
        char c = stream->read();
        payload += c;
    }

    DBG_PTN(payload);
    //DBG_PTN(payload);
    DynamicJsonDocument doc(412);
    deserializeJson(doc, payload);
    time_t unixtime = doc["unixtime"];
    http.end();
    //return (unixtime+ SECS_PER_HOUR * 8);
    setTime(unixtime+ SECS_PER_HOUR * timeZone + minutesTimeZone * 60);
  }
  http.end();
  return;
}
#else

#include "lib/AsyncHTTPRequest_Generic.h"             // https://github.com/khoih-prog/AsyncHTTPRequest_Generic
//extern bool udp_time_fail;
AsyncHTTPRequest req_time;


void req_time_cb (void *optParm, AsyncHTTPRequest *request, int readyState) {
  (void) optParm;
  if (readyState == readyStateDone) {
    int code = request->responseHTTPcode();
    if (code == 200) {
      String payload = request->responseText();
      DBG_PTN(payload);
      JsonDocument doc;
      deserializeJson(doc, payload);
      time_t unixtime = doc["unixtime"];
      setTime(unixtime+ SECS_PER_HOUR * timeZone + minutesTimeZone * 60);
      http_time_fail = false;
      //timesynced = true;
    }else{
      DBG_PTN("time code = " + String(code));
    }
  }
}

void send_req_time(){
  //DBG_PTN(("rq t"));
  String url = "http://worldtimeapi.org/api/timezone/UTC";
  //String url = "https://query1.finance.yahoo.com/v8/finance/chart/BTC-EUR?interval=1d&range=84d";
  DBG_PTN(url);
  static bool requestOpenResult;
  if (req_time.readyState() == readyStateUnsent || req_time.readyState() == readyStateDone) {
    //requestOpenResult = request.open("GET", "http://worldtimeapi.org/api/timezone/Europe/London.txt");
    requestOpenResult = req_time.open("GET", url.c_str());
    if (requestOpenResult)
    {
      // Only send() if open() returns true, or crash
      req_time.send();
    }
    else
    {
      DBG_PTN(F("bad request"));
    }
  } else {
    DBG_PTN("rt can't send req");
  }
}

time_t getUnixTime() {
  req_time.onReadyStateChange(req_time_cb);
  send_req_time();
  return 0;
}

//bool http_time_fail = true;
unsigned long http_update_start_time = 0;
void sync_http_time(bool force){
  if(!force){
    if(!udp_time_fail) return; //udp 同步成功，则不启用http同步
    if(http_time_fail && millis() - http_update_start_time < 10000) return;//http 同步失败情况下，10s 同步一次
    if(!http_time_fail && millis() - http_update_start_time < 1000*3600) return;//http 同步成功后每隔60m同步一次
  }
  
  getUnixTime();
  http_update_start_time = millis();
}
#endif

#if 0
constexpr const char *ntpServerNames[] = {
    "ntp.aliyun.com",
};
boolean syncEventTriggered = false; // True if a time even has been triggered
NTPSyncEvent_t ntpEvent; // Last triggered event
int dst_enable = 0;

void update_ntp_time(){
  if(my_ntp_server != "")
    NTP.begin (my_ntp_server, timeZone, dst_enable, minutesTimeZone);
  else
    NTP.begin (ntpServerNames[0], timeZone, dst_enable, minutesTimeZone);
}

#define NTP_TIMEOUT 1500
void init_ntp() {
  NTP.onNTPSyncEvent([](NTPSyncEvent_t event) {
    ntpEvent = event;
    syncEventTriggered = true;
  });

  NTP.setInterval(60);
  NTP.setNTPTimeout(NTP_TIMEOUT);
  update_ntp_time();
}

void processSyncEvent (NTPSyncEvent_t ntpEvent) {
    if (ntpEvent < 0) {
        DBG_PTN (("er"));
    } else {
        if (ntpEvent == timeSyncd) {
           //DBG_PTN("udp get time");
           udp_time_fail = false;
           DBG_PTN (NTP.getTimeDateString (NTP.getLastNTPSync ()));
        }
    }
}

void process_ntp_event(){
  if (syncEventTriggered) {
      processSyncEvent (ntpEvent);
      syncEventTriggered = false;
  }
}
#endif

void sync_time(bool force){

  //sync_udp_time();

  if(!force){
    if(udp_time_fail == true) 
      sync_http_time(force);
    else 
      sync_udp_time();
  }
  else
      sync_http_time(force);
}