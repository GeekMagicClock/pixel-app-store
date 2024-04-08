#include "tetris_clock.h"
#include "../../lib/display.h"
#include "../../lib/btn.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
extern MatrixPanel_I2S_DMA mdisplay;
//extern tetris_clock t_clock;
tetris_clock t_clock;
void init_tetris() {
    mdisplay.clearScreen();
    mdisplay.setCursor(0,4);
    mdisplay.setTextColor(parseRGBColor(C_LIGHT_PURPLE));
    mdisplay.println("3.");
    mdisplay.println("Tetris");
    mdisplay.print("Clock");
    int i = 0;
    while(!btn_status() && i<200) {
      i++;
      delay(10);
    }
    //delay(2000);
    t_clock.setup();  
}

void display_tetris(){
    t_clock.loop();
}