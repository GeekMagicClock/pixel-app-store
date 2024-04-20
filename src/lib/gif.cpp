//#include <FS.h>
#include <LittleFS.h>
//#include <SD.h>
#include <AnimatedGIF.h>
#include "my_debug.h"
//#define LittleFS LITTLEFS
#define  LITTLEFS LittleFS

#define GIF_POINTER
#ifdef GIF_POINTER
AnimatedGIF *gif = NULL;
#else
AnimatedGIF gif;
#endif

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
extern MatrixPanel_I2S_DMA mdisplay;

// GIFDraw is called by AnimatedGIF library frame to screen
#define DISPLAY_WIDTH  mdisplay.width()
#define DISPLAY_HEIGHT mdisplay.height()
#define BUFFER_SIZE 10            // Optimum is >= GIF width or integral division of width

#ifdef USE_DMA
  uint16_t usTemp[2][BUFFER_SIZE]; // Global to support DMA use
#else
  uint16_t usTemp[1][BUFFER_SIZE];    // Global to support DMA use
#endif
bool     dmaBuf = 0;

 
/* gif 图象的显示位置，只能显示一张 */
int x_offset, y_offset;
/* gif高度小于canvas高度时，用户canvas居中显示 */
int height_offset = 0;
int max_frame_height;

#if 0
// Draw a line of image directly on the LCD
static void GIFDraw(GIFDRAW *pDraw)
{
  uint8_t *s;
  uint16_t *d, *usPalette;
  int x, y, iWidth, iCount;
  if(pDraw->iHeight > max_frame_height)
    max_frame_height = pDraw->iHeight;
  height_offset = (DISPLAY_WIDTH-max_frame_height)/2;
  // Displ;ay bounds chech and cropping
  iWidth = pDraw->iWidth;
  if (iWidth + pDraw->iX > DISPLAY_WIDTH)
    iWidth = DISPLAY_WIDTH - pDraw->iX;
  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y; // current line
  if (y >= DISPLAY_HEIGHT || pDraw->iX >= DISPLAY_WIDTH || iWidth < 1){
    return;
  }

  // Old image disposal
  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) // restore to background color
  {
    for (x = 0; x < iWidth; x++)
    {
      if (s[x] == pDraw->ucTransparent)
        s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }

  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency) // if transparency used
  {
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    pEnd = s + iWidth;
    x = 0;
    iCount = 0; // count non-transparent pixels
    while (x < iWidth)
    {
      c = ucTransparent - 1;
      d = &usTemp[0][0];
      while (c != ucTransparent && s < pEnd && iCount < BUFFER_SIZE )
      {
        c = *s++;
        if (c == ucTransparent) // done, stop
        {
          s--; // back up to treat it like transparent
        }
        else // opaque
        {
          *d++ = usPalette[c];
          iCount++;
        }
      } // while looking for opaque pixels
      if (iCount) // any opaque pixels?
      {
        // DMA would degrtade performance here due to short line segments
#if 0
        spt.setColorDepth(8);
        spt.createSprite(90, 1);//创建窗口
        spt.fillSprite(0x0000);   //填充率
        spt.setAddrWindow(pDraw->iX + x, 0, iCount, 1);
        spt.pushPixels(usTemp, iCount); 
        spt.pushSprite(160,160+y);
        spt.deleteSprite();
#else
        mdisplay.startWrite(); // The TFT chip slect is locked low
        mdisplay.setAddrWindow(pDraw->iX + x+x_offset, y+y_offset, iCount, 1);
        mdisplay.pushPixels(usTemp, iCount);
        mdisplay.endWrite(); // The TFT chip slect is released low
#endif
        x += iCount;
        iCount = 0;
      }
      // no, look for a run of transparent pixels
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd)
      {
        c = *s++;
        if (c == ucTransparent)
          x++;
        else
          s--;
      }
    }
  }
  else
  {
    s = pDraw->pPixels;

    // Unroll the first pass to boost DMA performance
    // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
    if (iWidth <= BUFFER_SIZE)
      for (iCount = 0; iCount < iWidth; iCount++) usTemp[dmaBuf][iCount] = usPalette[*s++];
    else
      for (iCount = 0; iCount < BUFFER_SIZE; iCount++) usTemp[dmaBuf][iCount] = usPalette[*s++];

#ifdef USE_DMA // 71.6 fps (ST7796 84.5 fps)
    mdisplay.dmaWait();
    mdisplay.setAddrWindow(pDraw->iX + x_offset, y+y_offset, iWidth, 1);
    mdisplay.pushPixelsDMA(&usTemp[dmaBuf][0], iCount);
    dmaBuf = !dmaBuf;
#else // 57.0 fps
#if 0
        spt.setColorDepth(8);
        spt.createSprite(80, 1);//创建窗口
        spt.fillSprite(0x0000);   //填充率
        spt.setAddrWindow(pDraw->iX + x, 0, iCount, 1);
        spt.pushPixels(&usTemp[0][0], iCount); 
        spt.pushSprite(160,160+y);
        spt.deleteSprite();
#else
    mdisplay.startWrite(); // The TFT chip slect is locked low
    mdisplay.setAddrWindow(pDraw->iX+x_offset, y+y_offset, iWidth, 1);
    mdisplay.pushPixels(&usTemp[0][0], iCount);
    mdisplay.endWrite(); // The TFT chip slect is released low
#endif    
#endif

    iWidth -= iCount;
    // Loop if pixel buffer smaller than width
    while (iWidth > 0)
    {
      // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
      if (iWidth <= BUFFER_SIZE)
        for (iCount = 0; iCount < iWidth; iCount++) usTemp[dmaBuf][iCount] = usPalette[*s++];
      else
        for (iCount = 0; iCount < BUFFER_SIZE; iCount++) usTemp[dmaBuf][iCount] = usPalette[*s++];

#ifdef USE_DMA
      mdisplay.dmaWait();
      mdisplay.pushPixelsDMA(&usTemp[dmaBuf][0], iCount);
      dmaBuf = !dmaBuf;
#else
#if 0
        spt.setColorDepth(8);
        spt.createSprite(80, 1);//创建窗口
        spt.fillSprite(0x0000);   //填充率
        spt.setAddrWindow(pDraw->iX + x, 0, iCount, 1);
        spt.pushPixels(&usTemp[0][0], iCount); 
        spt.pushSprite(160,160+y);
        spt.deleteSprite();
#else
      mdisplay.startWrite(); // The TFT chip slect is locked low
      mdisplay.pushPixels(&usTemp[0][0], iCount);
      mdisplay.endWrite(); // The TFT chip slect is released low
#endif      
#endif
      iWidth -= iCount;
    }
  }
} /* GIFDraw() */
#endif

void GIFDraw(GIFDRAW *pDraw)
{
    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[320];
    int x, y, iWidth;

  iWidth = pDraw->iWidth;
  if (iWidth > MATRIX_WIDTH)
      iWidth = MATRIX_WIDTH;

    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y; // current line
    
    s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2) // restore to background color
    {
      for (x=0; x<iWidth; x++)
      {
        if (s[x] == pDraw->ucTransparent)
           s[x] = pDraw->ucBackground;
      }
      pDraw->ucHasTransparency = 0;
    }
    // Apply the new pixels to the main image
    if (pDraw->ucHasTransparency) // if transparency used
    {
      uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
      int x, iCount;
      pEnd = s + pDraw->iWidth;
      x = 0;
      iCount = 0; // count non-transparent pixels
      while(x < pDraw->iWidth)
      {
        c = ucTransparent-1;
        d = usTemp;
        while (c != ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent) // done, stop
          {
            s--; // back up to treat it like transparent
          }
          else // opaque
          {
             *d++ = usPalette[c];
             iCount++;
          }
        } // while looking for opaque pixels
        if (iCount) // any opaque pixels?
        {
          for(int xOffset = 0; xOffset < iCount; xOffset++ ){
            mdisplay.drawPixel(x + xOffset, y, usTemp[xOffset]); // 565 Color Format
          }
          x += iCount;
          iCount = 0;
        }
        // no, look for a run of transparent pixels
        c = ucTransparent;
        while (c == ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent)
             iCount++;
          else
             s--; 
        }
        if (iCount)
        {
          x += iCount; // skip these
          iCount = 0;
        }
      }
    }
    else // does not have transparency
    {
      s = pDraw->pPixels;
      // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
      for (x=0; x<pDraw->iWidth; x++)
      {
          mdisplay.drawPixel(x, y, usPalette[*s++]); // color 565
      }
    }
     // 绘制文字
     // 绘制文字背景
    #if 0
    String tt = "$" + String(brt)+"000";
    //mdisplay->fillRect(5, 20, 60, 8, mdisplay->color565(0, 0, 0)); 
    mdisplay->setFont(&FreeSansBold8pt7b);
    mdisplay->setFont(&agencyb8pt7b);
    //mdisplay->setFont(&FreeSansBold20pt7b);
   
    mdisplay->setCursor(5, 11);
    mdisplay->printf("BTC");
    mdisplay->setCursor(5, 30);
    mdisplay->printf("%s",tt.c_str());
    #endif
} /* GIFDraw() */
extern String stock_name;
extern String stock_price;
#include  "../font/agencyb8pt7b.h"
void GIFDraw2(GIFDRAW *pDraw)
{
    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[320];
    int x, y, iWidth;

  iWidth = pDraw->iWidth;
  if (iWidth > MATRIX_WIDTH)
      iWidth = MATRIX_WIDTH;

    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y; // current line
    
    s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2) // restore to background color
    {
      for (x=0; x<iWidth; x++)
      {
        if (s[x] == pDraw->ucTransparent)
           s[x] = pDraw->ucBackground;
      }
      pDraw->ucHasTransparency = 0;
    }
    // Apply the new pixels to the main image
    if (pDraw->ucHasTransparency) // if transparency used
    {
      uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
      int x, iCount;
      pEnd = s + pDraw->iWidth;
      x = 0;
      iCount = 0; // count non-transparent pixels
      while(x < pDraw->iWidth)
      {
        c = ucTransparent-1;
        d = usTemp;
        while (c != ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent) // done, stop
          {
            s--; // back up to treat it like transparent
          }
          else // opaque
          {
             *d++ = usPalette[c];
             iCount++;
          }
        } // while looking for opaque pixels
        if (iCount) // any opaque pixels?
        {
          for(int xOffset = 0; xOffset < iCount; xOffset++ ){
            mdisplay.drawPixel(x + xOffset, y, usTemp[xOffset]); // 565 Color Format
          }
          x += iCount;
          iCount = 0;
        }
        // no, look for a run of transparent pixels
        c = ucTransparent;
        while (c == ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent)
             iCount++;
          else
             s--; 
        }
        if (iCount)
        {
          x += iCount; // skip these
          iCount = 0;
        }
      }
    }
    else // does not have transparency
    {
      s = pDraw->pPixels;
      // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
      for (x=0; x<pDraw->iWidth; x++)
      {
          mdisplay.drawPixel(x, y, usPalette[*s++]); // color 565
      }
    }
     // 绘制文字
     // 绘制文字背景
    //String tt = "$" + String(brt)+"000";
    //mdisplay->fillRect(5, 20, 60, 8, mdisplay->color565(0, 0, 0)); 
    //mdisplay.setFont(&FreeSansBold8pt7b);
    mdisplay.setFont(&agencyb8pt7b);
    //mdisplay->setFont(&FreeSansBold20pt7b);
   
    mdisplay.setCursor(5, 11);
    mdisplay.printf(stock_name.c_str());
    mdisplay.setCursor(5, 30);
    mdisplay.printf("%s", stock_price.c_str());
} /* GIFDraw() */

File f;
static void * GIFOpenFile(const char *fname, int32_t *pSize)
{
  //f = SPIFFS.open(fname, "r");
  //f = SD.open(fname, "r");
  f = LittleFS.open(fname, "r");
  //  digitalWrite(TFT_CS_PIN,HIGH);
  //  digitalWrite(SD_CS_PIN,LOW);
  //f = SD.open(fname);
  if (f)
  {
    *pSize = f.size();
    //Serial.printf("filesize [%d]\n", *pSize);
    if(f.size() == 0) return NULL;
    //DBG_PTN("open success");
    return (void *)&f;
  }
  Serial.printf("%s op fail\r\n", fname);
  return NULL;
} /* GIFOpenFile() */

static void GIFCloseFile(void *pHandle)
{
  File *f = static_cast<File *>(pHandle);
  //  digitalWrite(TFT_CS_PIN,HIGH);
  //  digitalWrite(SD_CS_PIN,LOW);
  if (f != NULL){
    f->close();
    pHandle = NULL;
  }
  //DBG_PTN("close success");
} /* GIFCloseFile() */

static int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    int32_t iBytesRead;
    iBytesRead = iLen;
    File *f = static_cast<File *>(pFile->fHandle);
    // Note: If you read a file all the way to the last byte, seek() stops working
    if ((pFile->iSize - pFile->iPos) < iLen)
       iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
    if (iBytesRead <= 0){
       //Serial.printf("read 0 bytes\n");
       return 0;
    }
    //Serial.printf("1file[%s] available size %d\r\n", f->name(), f->available());
  //  digitalWrite(TFT_CS_PIN,HIGH);
  //  digitalWrite(SD_CS_PIN,LOW);
    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    //Serial.printf("2file[%s] available size %d\r\n", f->name(), f->available());
    //Serial.printf("read [%d] success\r\n", iBytesRead);
    return iBytesRead;
} /* GIFReadFile() */

static int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{ 
  int i = micros();
  File *f = static_cast<File *>(pFile->fHandle);
  //  digitalWrite(TFT_CS_PIN,HIGH);
  //  digitalWrite(SD_CS_PIN,LOW);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  i = micros() - i;
  //Serial.printf("Seek time = %d us\n", i);
  return pFile->iPos;
} /* GIFSeekFile() */

static int init_gif = 0;
static int ShowNextFrame(const char *name){
  long lTime = micros();
  int iFrames = 0;
  if(gif == NULL){
    DBG_PTN("gif == null");
    return 0;
  }
  //未初始化gif，停止播放
  if (init_gif == 0){
    
      DBG_PTN("gif not init");
#ifdef GIF_POINTER
      gif->close();
#else
      gif.close();
#endif      
      return init_gif;
  }
  //Serial.printf("debug gif draw2\r\n");

  //Serial.printf("Start show gif  [%s]\r\n",name); 
  {
    //Serial.printf("Successfully opened GIF; Canvas size = %d x %d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
    //mdisplay.startWrite(); // The TFT chip slect is locked low
    //Serial.printf(" tft size = %d x %d\n",mdisplay.width(), mdisplay.height());

    //DBG_PTN("gif play frame");
#ifdef GIF_POINTER
    if(gif->playFrame(true, NULL) == 1)
#else
    if(gif.playFrame(true, NULL))
#endif
    {
      iFrames++;
      //yield();
      //Serial.printf(" frames [%d]\n",iFrames);
    }else //no more frames
    {
#ifdef GIF_POINTER
        gif->close();
#else
        gif.close();
#endif
        // open next gif file;
        init_gif = 0;
        // reopen to play 
        //gif.open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw);
        Serial.printf("close gif %s\r\n", name);
    }
      
    //mdisplay.endWrite(); // Release TFT chip select for other SPI devices
    lTime = micros() - lTime;
    //DBG_PTNiFrames / (lTime / 1000000.0));
    //DBG_PTN(" fps");
  }
  return init_gif;
}

void gifDeinit(){
  init_gif = 0;
  
#ifdef GIF_POINTER
  if(gif != NULL){gif->close(); free(gif); gif = NULL; }
  //if(gif != NULL){gif->close(); delete gif; gif = NULL; }
#endif
}

void gifInit2(const char *name){
  if (init_gif == 1){
    //DBG_PTN("already gif init");
    return;
  } 

#ifdef GIF_POINTER
#if 0 
//使用try catch 来捕获new造成的OOM, 但需要开启-fexceptions 编译选项，增加了flash代码内存占用.
  try {
    // 尝试分配内存
    //int* myInt = new int;
    gif = new AnimatedGIF; 
  } catch (const std::bad_alloc& e) {
    // 捕获到内存分配失败的情况
    Serial.println("oom: " + String(e.what()));
    // 你可以在这里进行任何必要的错误处理，例如释放资源
  }
#else
#endif
#if 0
  if(gif == NULL)
    gif = new AnimatedGIF; 
#else
  if(gif == NULL){
    gif = static_cast<AnimatedGIF*>(malloc(sizeof(AnimatedGIF)));//明明内存还够，却有内存无法分配的情况。
    if(gif == NULL){
      Serial.println("oo");
      Serial.print(ESP.getFreeHeap());
      return;
    }
  }
#endif
  //new (gif) AnimatedGIF();
#endif

#ifdef GIF_POINTER
  //gif->begin(GIF_PALETTE_RGB565_BE);
  gif->begin(LITTLE_ENDIAN_PIXELS);
#else
  gif.begin(GIF_PALETTE_RGB565_BE);
#endif

#ifdef GIF_POINTER
  int ret = 0;
  //DBG_PTN("open gif");
  //DBG_PTN(name);
  ret = gif->open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw2);
  if(ret == 0){
    init_gif = 0;
    gifDeinit();//释放内存
    return;
  }
#else
  gif.open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw);
#endif
  init_gif = 1;
}

void gifInit(const char *name){
  if (init_gif == 1) return;

#ifdef GIF_POINTER
#if 0 
//使用try catch 来捕获new造成的OOM, 但需要开启-fexceptions 编译选项，增加了flash代码内存占用.
  try {
    // 尝试分配内存
    //int* myInt = new int;
    gif = new AnimatedGIF; 
  } catch (const std::bad_alloc& e) {
    // 捕获到内存分配失败的情况
    Serial.println("oom: " + String(e.what()));
    // 你可以在这里进行任何必要的错误处理，例如释放资源
  }
#else
#endif
#if 0
  if(gif == NULL)
    gif = new AnimatedGIF; 
#else
  if(gif == NULL)
    gif = static_cast<AnimatedGIF*>(malloc(sizeof(AnimatedGIF)));//明明内存还够，却有内存无法分配的情况。
  if(gif == NULL){
    Serial.println("oo");
    Serial.print(ESP.getFreeHeap());
    return;
  }
#endif
  //new (gif) AnimatedGIF();
#endif

#ifdef GIF_POINTER
  //gif->begin(GIF_PALETTE_RGB565_BE);
  gif->begin(LITTLE_ENDIAN_PIXELS);
#else
  gif.begin(GIF_PALETTE_RGB565_BE);
#endif

#ifdef GIF_POINTER
  int ret = 0;
  ret = gif->open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw);
  if(ret == 0){
    init_gif = 0;
    gifDeinit();//释放内存
    return;
  }
#else
  gif.open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw);
#endif
  init_gif = 1;
}

 
static int gif_change = 0;
void gifChange(){
  gif_change = 1;
}

static String cur_gif = "";
int drawGif(const char *filename, int xpos, int ypos){
  //Serial.printf("debug gif draw1\r\n");
  x_offset = xpos;
  y_offset = ypos;

  //gif变化后，立刻更新为新的gif播放
  if(cur_gif != String(filename))  
    gif_change = 1;

  if(gif_change){
    gifDeinit();
    gif_change = 0;
  }

  cur_gif = String(filename);
  gifInit(filename);
  return ShowNextFrame(filename);
}

int drawGif2(const char *filename, int xpos, int ypos){
  //Serial.printf("debug gif draw2\r\n");
  x_offset = xpos;
  y_offset = ypos;

  //gif变化后，立刻更新为新的gif播放
  if(cur_gif != String(filename))  
    gif_change = 1;

  if(gif_change){
    gifDeinit();
    gif_change = 0;
  }

  cur_gif = String(filename);
  gifInit2(filename);
  return ShowNextFrame(filename);
}

#if 0
//有bug，播放失败
void playGif(const char *filename, int xpos, int ypos){
#ifdef GIF_POINTER
  if(gif == NULL)
    gif = new AnimatedGIF; 
  gif->begin(GIF_PALETTE_RGB565_BE);
#else
  gif.begin(GIF_PALETTE_RGB565_BE);
#endif
  /* 和Tjpg_decoder 冲突 */
  mdisplay.setSwapBytes(false); // We need to swap the colour bytes (endianess)

  x_offset = xpos;
  y_offset = ypos;

  char filepath[256] = {0};
  if(filename[0] != '/')
    snprintf(filepath, sizeof(filepath), "/%s",filename);
  else
    snprintf(filepath, sizeof(filepath), "%s",filename);

#ifdef GIF_POINTER
  if (gif->open(filepath, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw))
#else
  if (gif.open(filepath, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw))
#endif
  {
    //Serial.printf("Successfully opened GIF; Canvas size = %d x %d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
    //mdisplay.startWrite(); // The TFT chip slect is locked low
    //Serial.printf(" tft size = %d x %d\n",mdisplay.width(), mdisplay.height());
#ifdef GIF_POINTER
    while (gif->playFrame(true, NULL))
#else
    while (gif.playFrame(true, NULL))
#endif    
    {
      //yield();
      //Serial.printf(" frames [%d]\n",iFrames);
    }
#ifdef GIF_POINTER
    gif->close();
    if(gif != NULL){ delete gif; gif = NULL; }
#else
    gif.close();
#endif
    //mdisplay.endWrite(); // Release TFT chip select for other SPI devices
  }
  else{
   DBG_PTN("f g"); 
  }
  return;
}
#endif