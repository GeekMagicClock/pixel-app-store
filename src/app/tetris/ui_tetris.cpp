#include "tetris_clock.h"
#include "../../lib/display.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
extern MatrixPanel_I2S_DMA mdisplay;
//extern tetris_clock t_clock;
tetris_clock t_clock;
void init_tetris() {
    mdisplay.clearScreen();
    mdisplay.setCursor(0,12);
    mdisplay.setTextColor(parseRGBColor(C_DARK_ORANGE));
    mdisplay.println("Tetris");
    mdisplay.print("Clock");
    delay(1000);
    t_clock.setup();  
}

void display_tetris(){
    t_clock.loop();
}