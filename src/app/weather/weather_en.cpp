#include "weather_en.h"
#include "my_debug.h"


Weather::Weather() {
    // Initialize your member variables here if needed
    requestPtr = nullptr; // 初始化指针为 nullptr
    //requestPtr1 = nullptr; // 初始化指针为 nullptr
    requestPtr2 = nullptr; // 初始化指针为 nullptr
}

Weather::~Weather() {
    if (requestPtr != nullptr) {
        delete requestPtr; // 释放 requestPtr 指向的动态分配的对象
        requestPtr = nullptr; // 将指针设置为 nullptr，以防止悬空指针
    }
}
void Weather::init(const String& usrKey, const String& pubKey, const String& cityCode, float lonti, float lati) {
    // Perform any initialization tasks here, such as setting API key and city code

    this->usr_weather_key = usrKey;
    this->pub_key = pubKey;
    this->cityCode = cityCode;
    lastUpdate = 0;
//    def_ntp_server = "ntp.aliyun.com";

    //用户会更改的设置：
    //updateInterval = 60000; // Set default update interval to 1 minute (60000 milliseconds)
    //updateInterval = 1200000; // Set default update interval to 1 minute (60000 milliseconds)
    updateInterval = 10000; // 刚开机提高更新频率，以获取到天气信息，更新完成后降低更新频率
//    dst_enable = 0;
//    my_ntp_server = "";
//    timeZone = 8;
//    minutesTimeZone = 0;
    //lat = 0;//坐标清零
    //lon = 0;
    lon = lonti;
    lat = lati;
    lastUpdate = millis();
    //lastForecastUpdate = 0;
    lastForecastTempUpdate = 0;
    // Set the callback function using lambda
    onReadyStateChangeCB = [this](void* optParm, AsyncHTTPRequest* request, int readyState) {
        onReadyStateChange(optParm, request, readyState);
    };
    #if 0
    forecastOnReadyStateChangeCB = [this](void* optParm, AsyncHTTPRequest* request, int readyState) {
        forecastOnReadyStateChange(optParm, request, readyState);
    };
    #endif
    forecastTempOnReadyStateChangeCB = [this](void* optParm, AsyncHTTPRequest* request, int readyState) {
        forecastTempOnReadyStateChange(optParm, request, readyState);
    };
}

void Weather::update(bool force) {
    // Check if it's time to update the weather based on the update interval
    if (!force && (millis() - lastUpdate < updateInterval)) {
        return; // If not enough time has passed, return without updating
    }

    lastUpdate = millis(); // Update the last update time

    send_request();
}

#define TOTAL_KEY 5
#if 0
// 原始key
String keys[TOTAL_KEY] = {
    //"4bb7ddefdb4b9b12c5374114d3588e1d",
    "61fa9f31e28cda7597fc864715347dc5",
    "830e7e6f63240a564abe10fea5eac576",
    "16d434f851447731a955820e67e069d9",
    "b81d296753b89dfd8d36c5b3f60feb15"
};
#endif

//反转后的key
static String keys[TOTAL_KEY] = {
    //"4bb7ddefdb4b9b12c5374114d3588e1d",
    //"0b72f2eb36e767b59610f270647ccad3",
    "9b27f7eb63e232b40389f729352ccad6",
    "38fa0f68e71cda2402fc135284652dc4",
    "169e2e3f36759a435abe89fea4eac423",
    "83d565f148552268a044179e32e930d0",
    "b18d703246b10dfd1d63c4b6f39feb84"
};

String swapDigits(String &input) {
    String out = input;
    for (int i = 0; i < input.length(); i++) {
        if (isdigit(input[i])) {
            // 将数字字符转换为对应的调换后的字符
            out[i] = '9' - (input[i] - '0');
        }
    }
    return out;
}
//#include "../lib/gif.h"
void Weather::send_request() {
    //gifDeinit();//20240327因为是异步操作，可能会对正在展示的gif结构造成破坏，导致gif->close 非法内存访问 而出现崩溃。
    if (requestPtr == nullptr) {
        requestPtr = new AsyncHTTPRequest(); // 使用动态分配创建 request 对象
    }
    requestPtr->onReadyStateChange(request_cb, this);
    if(cityCode == "0" || cityCode == ""){
        delete requestPtr;
        requestPtr = nullptr;
        return;
    } 

    //如果用户没有设置自己的key，那么就用pubkey, pubkey 也会从设置中读出来直接使用该pubkey, 如果么有设置过pubkey, 就取第一个
    if(pub_key == "") {
        key = swapDigits(keys[key_index]);
    }

    //优先使用用户自己设置的key
    if(usr_weather_key != "")
        key = usr_weather_key;

    String url;
    if(isDigit(cityCode.charAt(0)))
        url = ("http://api.openweathermap.org:80/data/2.5/weather?id=") + cityCode + ("&appid=") + key + ("&units=metric") + ("&lang=en");
    else
        url = ("http://api.openweathermap.org:80/data/2.5/weather?q=") + cityCode + ("&appid=") + key + ("&units=metric") + ("&lang=en");
    DBG_PTN(url);
    static bool requestOpenResult;
    if (requestPtr->readyState() == readyStateUnsent || requestPtr->readyState() == readyStateDone) {
        requestOpenResult = requestPtr->open("GET", url.c_str());
        if (requestOpenResult) {
            requestPtr->send();
        } else {
            //DBG_PTN(F("bad request"));
            DBG_PTN(F("bad req"));
        }
    } else {
        //DBG_PTN("Fail");
        DBG_PTN(F("rq fail"));
    }
}

void Weather::parse_rsp_data(const String* payload) {
    
    //gifDeinit();
    DynamicJsonDocument doc(1024);
    //JsonDocument doc;
    //DynamicJsonDocument doc(1224);
    deserializeJson(doc, *payload);

    temperature = doc["main"]["temp"].as<int>();
    humidity = doc["main"]["humidity"].as<int>();
    min_temp = doc["main"]["temp_min"].as<int>();
    max_temp = doc["main"]["temp_max"].as<int>();
    pressure = doc["main"]["pressure"].as<int>();
    weather_code = doc["weather"][0]["icon"].as<String>().substring(0,2);
    weather_str = doc["weather"][0]["main"].as<String>();
    lat = doc["coord"]["lat"].as<float>();
    lon = doc["coord"]["lon"].as<float>();
    //DBG_PTN(lat);
    //DBG_PTN(lon);
    //DBG_PTN(lon);

    weather_desc = doc["weather"][0]["description"].as<String>();
    weather_desc[0] = toupper(weather_desc[0]);

    weather_icon = doc["weather"][0]["icon"].as<String>();
    wind_speed = doc["wind"]["speed"].as<int>();
    feels_like = doc["main"]["feels_like"].as<int>();
    temp_unit = "C"; // Assuming temperature unit is Celsius, you can adjust this based on API response
    wind_unit = "m/s"; // Assuming wind unit is meters per second, you can adjust this based on API response
    pressure_unit = "hPa"; // Assuming pressure unit is hectopascals, you can adjust this based on API response

    // Get other parameters from API response
    int utcOffsetSeconds = doc["timezone"];
    timeZone = utcOffsetSeconds / 3600; // Convert seconds to hours for timeZone
    minutesTimeZone = (utcOffsetSeconds - timeZone * 3600) / 60; // Calculate minutesTimeZone
    DBG_PTN("timezone:");
    DBG_PTN(timeZone);

    cityName = doc["name"].as<String>();
    country = doc["sys"]["country"].as<String>(); // Get country field from API response

    weather_changed = true;
    forecast_changed = true;
    updateInterval = 1200000; // Set default update interval to 1 minute (60000 milliseconds)
    //updateInterval = 60000; // Set default update interval to 1 minute (60000 milliseconds)
    //更新天气后呢，就强制更新一下天气预报
    //update_forecast_temp(true);
    doc.clear();
}

void Weather::update_time(){
 //getNtpTime();
}
#include "../../lib/settings.h"
void Weather::onReadyStateChange(void* optParm, AsyncHTTPRequest* request, int readyState) {
    if (readyState == readyStateDone) {
        //gifDeinit();
        if (request->responseHTTPcode() == 200) {
            //gifDeinit();
            String payload = request->responseText();
            //DBG_PTN(payload);
            DBG_PTN(payload);
            parse_rsp_data(&payload);

            //uptime = millis();
            //update_time();
            // 请求完成后销毁 requestPtr 指针
            if (requestPtr != nullptr) {
                delete requestPtr;
                requestPtr = nullptr;
            }
        }else{
            DBG_PTN(request->responseHTTPcode());
            if(request->responseHTTPcode() == 401){
                cityName = F("Err key!");
            }else
            if(usr_weather_key =="" && request->responseHTTPcode() == 429){
                cityName = F("Please set key!");
                key_index++;
                if(key_index >= TOTAL_KEY) {
                    key_index --;
                    DBG_PTN("ky!");
                }else{
                    key = swapDigits(keys[key_index]);
                    pub_key = key;
                    //set_pub_key_config(keys[key_index]);
                }
            }

        }
    }
}

void Weather::request_cb(void* optParm, AsyncHTTPRequest* request, int readyState) {
    Weather* weatherInstance = reinterpret_cast<Weather*>(optParm);
    if (weatherInstance != nullptr) {
        weatherInstance->onReadyStateChange(optParm, request, readyState);
    }
#if 0
    // 请求完成后销毁 requestPtr 指针
    if (weatherInstance->requestPtr != nullptr) {
        delete weatherInstance->requestPtr;
        weatherInstance->requestPtr = nullptr;
    }
#endif
}
#if 0
//注意此接口废弃，返回数据不准确，免费用户
void Weather::update_forecast(bool force) {
    // Check if it's time to update the weather based on the update interval
    if(lat == 0 && lon == 0) return;
    if (!force && (millis() - lastForecastUpdate < updateInterval)) {
        return; // If not enough time has passed, return without updating
    }

    lastForecastUpdate = millis(); // Update the last update time

    forecast_send_request();
}

void Weather::forecast_send_request() {
    if (requestPtr1 == nullptr) {
        requestPtr1 = new AsyncHTTPRequest(); // 使用动态分配创建 request 对象
    }
    requestPtr1->onReadyStateChange(forecast_request_cb, this);
    if(cityCode == "0" || cityCode == "") return;
    String url = "http://api.openweathermap.org:80/data/2.5/forecast?id=" + cityCode + "&appid=" + key + "&units=metric&cnt=3" + "&lang=en";
    //DBG_PTN(url);
    DBG_PTN(url);
    static bool requestOpenResult;
    if (requestPtr1->readyState() == readyStateUnsent || requestPtr1->readyState() == readyStateDone) {
        requestOpenResult = requestPtr1->open("GET", url.c_str());
        if (requestOpenResult) {
            requestPtr1->send();
        } else {
            DBG_PTN(F("bad req"));
        }
    } else {
        //DBG_PTN("Fail");
        DBG_PTN(F("fail"));
    }
}

void Weather::forecast_parse_rsp_data(const String* payload) {
    DynamicJsonDocument doc(2024);
    deserializeJson(doc, *payload);
// 获取 "list" 数组
  JsonArray forecastArray = doc["list"];

  // 遍历 "list" 数组
  int i = 0;
  for (JsonObject obj : forecastArray) {
    forecast[i].temp = obj["main"]["temp"];
    forecast[i].humidity = obj["main"]["humidity"];
    //forecast[i].min_temp = obj["main"]["temp_min"];
    //forecast[i].max_temp = obj["main"]["temp_max"];
    forecast[i].weather = obj["weather"][0]["main"].as<String>();  // 这里需要填充星期几的信息，你可以根据日期计算
    //forecast[i].icon = obj["weather"][0]["icon"].as<String>();
    i++;
  }

    weather_changed = true;
}

void Weather::forecastOnReadyStateChange(void* optParm, AsyncHTTPRequest* request, int readyState) {
    if (readyState == readyStateDone) {
        if (request->responseHTTPcode() == 200) {
            String payload = request->responseText();
            //DBG_PTN(payload);
            DBG_PTN(payload);
            forecast_parse_rsp_data(&payload);

            //uptime = millis();
            //update_time();
            // 请求完成后销毁 requestPtr 指针
            if (requestPtr1 != nullptr) {
                delete requestPtr1;
                requestPtr1 = nullptr;
            }
        }
    }
}

void Weather::forecast_request_cb(void* optParm, AsyncHTTPRequest* request, int readyState) {
    Weather* weatherInstance = reinterpret_cast<Weather*>(optParm);
    if (weatherInstance != nullptr) {
        weatherInstance->forecastOnReadyStateChange(optParm, request, readyState);
    }
#if 0
    // 请求完成后销毁 requestPtr 指针
    if (weatherInstance->requestPtr != nullptr) {
        delete weatherInstance->requestPtr;
        weatherInstance->requestPtr = nullptr;
    }
#endif
}
#endif
void Weather::update_forecast_temp(bool force) {
    // Check if it's time to update the weather based on the update interval
    //DBG_PTN("update forecast");
    //DBG_PTN(lat);
    //DBG_PTN(lon);
    if(lat == 0 && lon == 0) return;
    if (!force && (millis() - lastForecastTempUpdate < updateInterval + 10000)) {
        return; // If not enough time has passed, return without updating
    }

    lastForecastTempUpdate = millis(); // Update the last update time

    forecast_temp_send_request();
}

//static String f_key = "f33ec10656e849ef88a54746231204";
static String f_key =   ("f66ec89343e150ef11a45253768795");
void Weather::forecast_temp_send_request() {
    //gifDeinit();
    if (requestPtr2 == nullptr) {
        requestPtr2 = new AsyncHTTPRequest(); // 使用动态分配创建 request 对象
    }
    requestPtr2->onReadyStateChange(forecast_temp_request_cb, this);
    String url = ("http://api.weatherapi.com/v1/forecast.json?key=")+swapDigits(f_key)+"&q="+String(lat)+","+String(lon)+("&days=3&aqi=no&alerts=no");
    DBG_PTN(url);
    static bool requestOpenResult;
    if (requestPtr2->readyState() == readyStateUnsent || requestPtr2->readyState() == readyStateDone) {
        requestOpenResult = requestPtr2->open("GET", url.c_str());
        if (requestOpenResult) {
            requestPtr2->send();
        } else {
            DBG_PTN(F("bad req"));
        }
    } else {
        DBG_PTN("Fail");
    }
}

void Weather::forecast_temp_parse_rsp_data(const String* payload) {

    //gifDeinit();
    //DynamicJsonDocument doc(3224);//分配空间过小有可能导致无法解析到3天的天气
    DynamicJsonDocument doc(2048);//分配空间过小有可能导致无法解析到3天的天气
    //JsonDocument doc;//分配空间过小有可能导致无法解析到3天的天气
    deserializeJson(doc, *payload);
    // 获取 "forecast" 数组
    JsonArray forecastArray = doc["forecast"]["forecastday"];

    // 遍历 "forecast" 数组
    int i = 0;
    for (JsonObject obj : forecastArray) {
        forecast[i].max_temp = obj["day"]["maxtemp_c"];
        forecast[i].min_temp = obj["day"]["mintemp_c"];
        forecast[i].humidity = obj["day"]["avghumidity"];
        forecast[i].temp = obj["day"]["avgtemp_c"];
        String temp_icon = obj["day"]["condition"]["icon"];
        // 找到 "day/" 的位置
        int dayIndex = temp_icon.indexOf("day/");

        // 找到 ".png" 的位置
        int pngIndex = temp_icon.indexOf(".png");

        if (dayIndex != -1 && pngIndex != -1) {
            // 提取位于 "day/" 和 ".png" 之间的子字符串
            String number = temp_icon.substring(dayIndex + 4, pngIndex);
            forecast[i].icon = "day/"+number;
        }

        // 使用forecast接口的数据来修正最高和最低气温
        if(i == 0) {
            min_temp = forecast[i].min_temp;
            max_temp = forecast[i].max_temp;
        }

        DBG_PTN(forecast[i].icon);
        i++;
        if (i >= 3) {
            break; // 解析前3个元素，如果数组更长，请根据需要调整
        }
    }

    doc.clear();
    weather_changed = true;
    forecast_changed = true;
}

void Weather::forecastTempOnReadyStateChange(void* optParm, AsyncHTTPRequest* request, int readyState) {
    if (readyState == readyStateDone) {
        if (request->responseHTTPcode() == 200) {
            //gifDeinit();
            Serial.println("heap1");
            Serial.println(ESP.getFreeHeap());
            String payload = request->responseText();
            Serial.println(ESP.getFreeHeap());
            //DBG_PTN(payload);
            forecast_temp_parse_rsp_data(&payload);

            //uptime = millis();
            //update_time();
            // 请求完成后销毁 requestPtr 指针
            if (requestPtr2 != nullptr) {
                delete requestPtr2;
                requestPtr2 = nullptr;
            }
        }
    }
}

void Weather::forecast_temp_request_cb(void* optParm, AsyncHTTPRequest* request, int readyState) {
    Weather* weatherInstance = reinterpret_cast<Weather*>(optParm);
    if (weatherInstance != nullptr) {
        weatherInstance->forecastTempOnReadyStateChange(optParm, request, readyState);
    }
#if 0
    // 请求完成后销毁 requestPtr 指针
    if (weatherInstance->requestPtr != nullptr) {
        delete weatherInstance->requestPtr;
        weatherInstance->requestPtr = nullptr;
    }
#endif
}

Weather wea;



