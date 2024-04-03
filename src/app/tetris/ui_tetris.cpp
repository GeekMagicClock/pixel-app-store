#include "tetris_clock.h"

extern tetris_clock t_clock;
void display_tetris(){
    t_clock.loop();
}