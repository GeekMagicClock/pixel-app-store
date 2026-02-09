#ifndef WEATHER_EN_H
#define WEATHER_EN_H

#include "../../lib/AsyncHTTPRequest_Generic.h"             // https://github.com/khoih-prog/AsyncHTTPRequest_Generic
#include <ArduinoJson.h>
#include <functional>

void init_weather();
void update_weather(bool force);
void display_weather();
void exit_weather();

struct forecast_t{
    int temp;
    int humidity;
    int min_temp;
    int max_temp;
    String weather;
    String icon;
};

class Weather {
public:
    Weather();
    ~Weather();

    void init(const String& usrKey,const String& pubKey, const String& cityCode, float lon, float lat);

    void update(bool force = false);
    //void update_forecast(bool force = false);
    void update_forecast_temp(bool force = false);

    // Weather data variables
    bool init_done;
    float lon;
    float lat;
    int temperature;
    int humidity;
    int min_temp;
    int max_temp;
    String weather_code;
    int wind_speed;
    String weather_str;
    String weather_desc;
    String cityCode;
    int feels_like;
    String temp_unit;
    String wind_unit;
    String pressure_unit;
    int pressure;
    String weather_icon;
    String usr_weather_key;
    String pub_key;
    String key;
    int key_index;
    String cityName;
    String country;
    bool weather_changed;
    bool forecast_changed;
    //String my_ntp_server;
    //String def_ntp_server;//默认的NTP 服务器
    //int dst_enable;
    int timeZone;
    int minutesTimeZone;
    unsigned long updateInterval;
    struct forecast_t forecast[3];

    // Callback function type
    using ReadyStateChangeCB = std::function<void(void*, AsyncHTTPRequest*, int)>;

private:
    unsigned long lastUpdate;
    //unsigned long lastForecastUpdate;
    unsigned long lastForecastTempUpdate;
    AsyncHTTPRequest* requestPtr; // 用于保存动态分配的 request 对象指针
    //AsyncHTTPRequest* requestPtr1; // 用于保存动态分配的 request 对象指针
    AsyncHTTPRequest* requestPtr2; // 用于保存动态分配的 request 对象指针

    ReadyStateChangeCB onReadyStateChangeCB; // Callback function
    void parse_rsp_data(const String* payload);
    void onReadyStateChange(void* optParm, AsyncHTTPRequest* request, int readyState);
    void send_request();
    void update_time();
    static void request_cb(void* optParm, AsyncHTTPRequest* request, int readyState);
    #if 0
    ReadyStateChangeCB forecastOnReadyStateChangeCB; // Callback function
    void forecast_parse_rsp_data(const String* payload);
    void forecastOnReadyStateChange(void* optParm, AsyncHTTPRequest* request, int readyState);
    void forecast_send_request();
    static void forecast_request_cb(void* optParm, AsyncHTTPRequest* request, int readyState);
    ReadyStateChangeCB forecastTempOnReadyStateChangeCB; // Callback function
    void forecast_temp_parse_rsp_data(const String* payload);
    void forecastTempOnReadyStateChange(void* optParm, AsyncHTTPRequest* request, int readyState);
    void forecast_temp_send_request();
    static void forecast_temp_request_cb(void* optParm, AsyncHTTPRequest* request, int readyState);
    #endif
};

#endif // WEATHER_EN_H
