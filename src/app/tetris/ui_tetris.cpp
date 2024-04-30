#include "tetris_clock.h"
#include "../../lib/display.h"
#include "../../lib/btn.h"
#include "../../lib/settings.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
extern MatrixPanel_I2S_DMA mdisplay;
//extern tetris_clock t_clock;
tetris_clock t_clock;
extern int hour12;
void init_tetris() {

    read_hour12_config(&hour12);

    mdisplay.clearScreen();
    mdisplay.setTextColor(parseRGBColor(C_LIGHT_PURPLE));
    mdisplay.setFont();
    mdisplay.setCursor(12,4);
    mdisplay.println("4.");
    mdisplay.setCursor(12,13);
    //mdisplay.println("Tetris");
    mdisplay.println("TETRIS");
    mdisplay.setCursor(12,22);
    //mdisplay.print("Clock");
    mdisplay.print("CLOCK");
    int i = 0;
    while(i<1000) {
        i++;
        delay(1);
        update_btn();
    }
    t_clock.setup();  
}

void display_tetris(){
    t_clock.loop();
}