#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "stock.h"

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
        //input[i] = map(input[i], minValue, maxValue, 0, screenHeight); // 归一化到 [0, 1] 范围
        input[i] = scale(input[i], minValue, maxValue, 0, screenHeight); // 归一化到 [0, screenHeight] 范围
    }

    for (int i = 0; i < outputLength; i++) {
        if (i % 2 == 0) { // 每隔一个数据点插入一个新数据点
            int dataIndex = i / 2; // 原始数据点的索引
            output[i] = input[dataIndex]; // 直接复制原始数据点
        } else {
            int prevIndex = i / 2;
            int nextIndex = min(i / 2 + 1, inputLength - 1);
            output[i] = (i - 2 * prevIndex) * (input[nextIndex] - input[prevIndex]) / 2 + input[prevIndex];
        }
    }
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
        return roundf(price); // 保留2位小数
    }
}

#include "Fonts/FreeMono9pt7b.h"
void display_stock(){
    if(!run_data->stockdata.kline_updated ) return;

    mdisplay.clearScreen();
    run_data->stockdata.kline_updated = 0;

    my_stock_t *data = &run_data->stockdata;
    uint16_t lineColor = mdisplay.color565(0, 255, 0); // 浅红色填充
    uint16_t fillColor = mdisplay.color565(150, 255, 150); // 浅红色填充
    float chart_data[CANDLE_NUMS];
    for (size_t i = 0; i < CANDLE_NUMS; i++) {
        chart_data[i] = data->candles[CANDLE_NUMS-1 - i].open;//注意，蜡烛顺序要反一下
        Serial.print(chart_data[i]);
        Serial.print(" ");
    }

    mdisplay.setFont();
    mdisplay.setCursor(0,0);
    mdisplay.setTextWrap(false);
    mdisplay.setTextSize(1);
    mdisplay.setTextColor(mdisplay.color565(255, 255, 255));
    //mdisplay.setFont(&FreeMono9pt7b);
    if(data->price >= 100)
        mdisplay.print(data->stock_name +" "+ String(int(data->price)));
    else
        mdisplay.print(data->stock_name +" "+ String(processPrice(data->price)));

    mdisplay.setCursor(30,8);
    if(data->percent >= 0){
        mdisplay.setTextColor(mdisplay.color565(0, 255, 0));
        mdisplay.print("+"+String(data->percent, 1)+"%");
        lineColor = mdisplay.color565(0, 255, 0); // 绿色填充
        fillColor = mdisplay.color565(150, 255, 150); // 绿色填充
    } else{
        lineColor = mdisplay.color565(255, 0, 0); // 红色填充
        fillColor = mdisplay.color565(255, 150, 150); //浅红色填充
        mdisplay.setTextColor(mdisplay.color565(255, 0, 0));
        mdisplay.print(String(data->percent, 1)+"%");
    }

    drawLineChart(chart_data, CANDLE_NUMS, lineColor, fillColor);
}