#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "stock.h"
#include "../../lib/display.h"
#include "my_debug.h"

extern MatrixPanel_I2S_DMA mdisplay;
// 自定义的线性比例缩放函数
float scale(float value, float inMin, float inMax, float outMin, float outMax) {
    return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

#if 1
void linearInterpolation(float input[], int inputLength, float output[], int outputLength, int screenHeight) {
    // 归一化处理
    float minValue = input[0];
    float maxValue = input[0];
    for (int i = 1; i < inputLength; i++) {
        if (input[i] < minValue) minValue = input[i];
        if (input[i] > maxValue) maxValue = input[i];
    }

    for (int i = 0; i < inputLength; i++) {
        //output[i] = map(input[i], minValue, maxValue, 0, screenHeight); // 归一化到 [0, 1] 范围
        output[i] = scale(input[i], minValue, maxValue, 0, screenHeight); // 归一化到 [0, screenHeight] 范围
    }
    return;
}
#endif
void drawLineChart(float data[], int dataLength, uint16_t lineColor, uint16_t fillColor) {
    int screenWidth = 64;
    int screenHeight = 16;

    // 计算填充后的数据长度
    int filledLength = max(dataLength, screenWidth);

    // 计算数据归一化所需的最小值和最大值
    float minValue = data[0];
    float maxValue = data[0];
    for (int i = 1; i < dataLength; i++) {
        if (data[i] < minValue) minValue = data[i];
        if (data[i] > maxValue) maxValue = data[i];
    }

    // 补充数据到64个数组长度，并且归一化到0和1
    float filledData[screenWidth];
    linearInterpolation(data, dataLength, filledData, screenWidth, screenHeight);

    #define HEIGHT_OFFSET 16 
    // 绘制折线，以及填充折线以下部分
    for (int x = 0; x < screenWidth; x++){
        int y = int(filledData[x]);
        Serial.print(y);
        Serial.print(" ");
        mdisplay.drawPixel(screenWidth-1-x, screenHeight-1 -y + HEIGHT_OFFSET, lineColor);//画点
        mdisplay.drawFastVLine(screenWidth -1 -x, screenHeight-y + HEIGHT_OFFSET, y, fillColor);//画线，需要注意绘制起点，绘制方向，绘制长度，是从Y点继续增加的，以及绘制长度
    }
}

extern StockmarketAppRunData *run_data;

float processPrice(float price) {
    if (price < 1) {
        return roundf(price * 10000) / 10000; // 保留3位小数
    } else if (price < 100) {
        return roundf(price * 100) / 100; // 保留2位小数
    } else {
        return roundf(price * 10) / 10; // 保留1位小数
        //return roundf(price); // 保留2位小数
    }
}

String s_c = "#FFFFFF";//symbol color
String p_c = "#FFD700";//price color
#include "Fonts/FreeMono9pt7b.h"
void display_stock(){
    if(run_data->err != 0){
        //mdisplay.clearScreen();
        mdisplay.setFont();
        mdisplay.setTextColor(parseRGBColor(C_RED));
        mdisplay.setCursor(4,8);
        mdisplay.println("Network");
        mdisplay.setCursor(4,17);
        mdisplay.print("Error");
        return;
    }
    if(!run_data->stockdata.kline_updated ) return;

    mdisplay.clearScreen();
    run_data->stockdata.kline_updated = 0;

    my_stock_t *data = &run_data->stockdata;
    uint16_t lineColor = mdisplay.color565(0, 255, 0); // 浅红色填充
    uint16_t fillColor = mdisplay.color565(150, 255, 150); // 浅红色填充
    float chart_data[CANDLE_NUMS];
    for (size_t i = 0; i < CANDLE_NUMS; i++) {
        //chart_data[i] = data->candles[CANDLE_NUMS-1 - i].open;//注意，蜡烛顺序要反一下, 旧的数据先绘制
        chart_data[i] = data->candles[i].close;//注意，蜡烛顺序要反一下, 旧的数据先绘制//注意是close的数据
        Serial.print(chart_data[i]);
        Serial.print(" ");
    }

    mdisplay.setFont();
    mdisplay.setCursor(0,0);
    mdisplay.setTextWrap(false);
    mdisplay.setTextSize(1);
    mdisplay.setTextColor(mdisplay.color565(255, 255, 255));
    //mdisplay.setFont(&FreeMono9pt7b);
    //CNYUSD=X
    String tmp_name = "";
    tmp_name = data->stock_name.indexOf('=') != -1 ? data->stock_name.substring(0, data->stock_name.indexOf('=')) : data->stock_name;

    #if 0
    if(data->stock_name.indexOf('-') != -1){
        tmp_name = data->stock_name.substring(0, data->stock_name.indexOf('-'));
    }else if(data->stock_name.indexOf('=') != -1){
        tmp_name = data->stock_name.substring(0, data->stock_name.indexOf('='));
    }else
        tmp_name = data->stock_name;
    #endif

    //mdisplay.print(data->stock_name);
    mdisplay.setTextColor(parseRGBColor(s_c));
    mdisplay.print(tmp_name);
    
    mdisplay.setCursor(53,0);
    mdisplay.setTextColor(parseRGBColor(p_c));
    mdisplay.print(data->kline_interval);

    mdisplay.setCursor(0,8);
    if(data->price >= 1000)
        mdisplay.print(String(int(data->price)));
    else if(data->price >= 100)
        mdisplay.print(String(processPrice(data->price),1));
    else
        mdisplay.print(String(processPrice(data->price)));

    mdisplay.setCursor(35,8);
    String percent = "";
    if(data->percent >= 0){
        mdisplay.setTextColor(mdisplay.color565(0, 255, 0));
        if(data->percent>=10)
            percent = ("+"+String(roundf(data->percent), 0)+"%");
        else
            percent = ("+"+String(data->percent, 1)+"%");
        lineColor = mdisplay.color565(0, 255, 0); // 绿色填充
        fillColor = mdisplay.color565(150, 255, 150); // 绿色填充
    } else{
        lineColor = mdisplay.color565(255, 0, 0); // 红色填充
        fillColor = mdisplay.color565(255, 150, 150); //浅红色填充
        mdisplay.setTextColor(mdisplay.color565(255, 0, 0));
        percent = (String(data->percent, 1)+"%");
    }
    //动态确定 percent的位置
    mdisplay.setCursor(mdisplay.width()- (percent.length()+1)*5,8);
    mdisplay.print(percent);

    drawLineChart(chart_data, CANDLE_NUMS, lineColor, fillColor);
}

String stock_name;
String stock_price;
String ticker_bg = "/image/ezgif-1-7d443236bb.gif";
String last_ticker_bg = "";
String last_stock_price = "";
#include "../../lib/gif.h"
#include "../../lib/jpg.h"
#include "../../font/agencyb8pt7b.h"


void display_stock2(){
    if(run_data->err != 0){
        //mdisplay.clearScreen();
        mdisplay.setFont();
        mdisplay.setTextColor(parseRGBColor(C_RED));
        mdisplay.setCursor(4,8);
        mdisplay.println("Network");
        mdisplay.setCursor(4,17);
        mdisplay.print("Error");
        return;
    }
    stock_name = run_data->stock_id;
    stock_price = String(run_data->stockdata.price);
   //1. gif 背景 
    if(ticker_bg.endsWith(".gif") ||ticker_bg.endsWith(".GIF") ){
        if(last_ticker_bg != ticker_bg) {
            gifDeinit();
            last_ticker_bg = ticker_bg;
        } 
        drawGif2(ticker_bg.c_str(), 0, 0); 
    }
    
    if(!run_data->stockdata.kline_updated ) return;

    mdisplay.clearScreen();
    run_data->stockdata.kline_updated = 0;

    //2. jpg 背景
    if(ticker_bg.endsWith(".jpg")||ticker_bg.endsWith(".jpeg")||ticker_bg.endsWith(".JPG")||ticker_bg.endsWith(".JPEG")){
        if(last_ticker_bg == ticker_bg && last_stock_price == stock_price) 
            return;
        last_ticker_bg = ticker_bg;
        last_stock_price = stock_price;
        drawJpeg(ticker_bg.c_str(), 0, 0);
        //mdisplay.setFont(&agencyb8pt7b);
        mdisplay.setCursor(5, 1);
        mdisplay.printf(stock_name.c_str());
        mdisplay.setCursor(5, 24);
        mdisplay.printf("%s", stock_price.c_str());
    }

}

extern void init_stock_config();
#include "../../lib/display.h"
#include "../../lib/settings.h"
#include "../../lib/btn.h"
//#include "../../font/agencyb8pt7b.h"
void init_stock(){
    jpegInit();
    init_stock_config();
    read_stock_color(&s_c,&p_c);
    //DBG_PTN(ticker_bg);
    mdisplay.clearScreen();
    mdisplay.setTextColor(parseRGBColor(C_GREEN));
    //mdisplay.setFont(&agencyb8pt7b);
    mdisplay.setFont();
    mdisplay.setCursor(10,4);
    mdisplay.println("1.");
    mdisplay.setCursor(10,13);
    //mdisplay.println("Market");
    mdisplay.println("MARKET");
    mdisplay.setCursor(10,22);
    //mdisplay.print("Ticker");
    mdisplay.print("TICKER I");
    //delay(2000);

    int i = 0;
    while(!btn_status() && i<200) {
      i++;
      delay(10);
    }
}
void init_stock2(){
    jpegInit();
    init_stock_config();
    read_stock_bg(&ticker_bg);
    read_stock_color(&s_c,&p_c);
    DBG_PTN(ticker_bg);
    mdisplay.clearScreen();
    mdisplay.setTextColor(parseRGBColor(C_GREEN));
    //mdisplay.setFont(&agencyb8pt7b);
    mdisplay.setCursor(8,4);
    mdisplay.setFont();
    mdisplay.println("2.");
    mdisplay.setCursor(8,13);
    //mdisplay.println("Market");
    mdisplay.println("MARKET");
    mdisplay.setCursor(8,22);
    //mdisplay.print("Ticker");
    mdisplay.print("TICKER II");
    //delay(2000);

    int i = 0;
    while(!btn_status() && i<200) {
      i++;
      delay(10);
    }
}