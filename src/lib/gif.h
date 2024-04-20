#ifndef __GIF_H_
#define __GIF_H_
void gifInit(const char *name);
void gifDeinit();
void gifChange();
int drawGif(const char *filename, int x, int y);
void playGif(const char *filename, int x, int y);

int drawGif2(const char *filename, int xpos, int ypos);
#endif