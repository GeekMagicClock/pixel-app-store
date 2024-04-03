#ifndef _IMG_H
#define _IMG_H

void jpegInit();
void drawJpeg(const char *filename, int xpos, int ypos);
void drawArrayJpeg(int xpos, int ypos, const uint8_t arrayname[], uint32_t array_size);

#endif
