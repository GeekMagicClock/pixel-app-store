/* 解码效果最好 */
#include <TJpg_Decoder.h>
// Return the minimum of two values a and b

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
extern MatrixPanel_I2S_DMA mdisplay;

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
   // Stop further decoding as image is running off bottom of screen
  if ( y >= mdisplay.height() ) return 0;

  // This function will clip the image block rendering automatically at the TFT boundaries
  //mdisplay.pushImage(x, y, w, h, bitmap);

  // This might work instead if you adapt the sketch to use the Adafruit_GFX library
   mdisplay.drawRGBBitmap(x, y, bitmap, w, h);

  // Return 1 to decode next block
  return 1;
}

void jpegInit(){
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tft_output);
}

void drawJpeg(const char *filename, int xpos, int ypos){
  /* 和GIF 库冲突 */
  //mdisplay.setSwapBytes(true); // We need to swap the colour bytes (endianess)

  char filepath[256] = {0};
  if(filename[0] != '/')
    snprintf(filepath, sizeof(filepath), "/%s",filename);
  else
    snprintf(filepath, sizeof(filepath), "%s",filename);

 // uint16_t w = 0, h = 0;
 // TJpgDec.getFsJpgSize(&w, &h, "/panda.jpg"); // Note name preceded with "/"
  //TJpgDec.drawSdJpg(xpos, ypos, filepath);
  TJpgDec.drawFsJpg(xpos, ypos, filepath, LittleFS);
}

void drawArrayJpeg(int xpos, int ypos, const uint8_t arrayname[], uint32_t array_size){
    TJpgDec.drawJpg(xpos, ypos, arrayname, array_size);
    return;
}