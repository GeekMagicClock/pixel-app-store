
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <TetrisMatrixDraw.h>
//#include <ezTime.h>
#include "TimeLib.h"

extern MatrixPanel_I2S_DMA mdisplay;
TetrisMatrixDraw tetris(mdisplay); // Main clock
TetrisMatrixDraw tetris2(mdisplay); // The "M" of AM/PM
TetrisMatrixDraw tetris3(mdisplay); // The "P" or "A" of AM/PM

unsigned long oneSecondLoopDue = 0;

bool showColon = true;
volatile bool finishedAnimating = false;
bool displayIntro = true;

String lastDisplayedTime = "";
String lastDisplayedAmPm = "";

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t * timer = NULL;
hw_timer_t * animationTimer = NULL;

//bool hour12 = false;
extern bool hour12;
bool forceRefresh = false;

// This method is needed for driving the display
void IRAM_ATTR display_updater() {
  portENTER_CRITICAL_ISR(&timerMux);
  //mdisplay.display(10);
  portEXIT_CRITICAL_ISR(&timerMux);
}

// This method is for controlling the tetris library draw calls
void animationHandler()
{
#ifndef double_buffer
  portENTER_CRITICAL_ISR(&timerMux);
#endif

  // Not clearing the display and redrawing it when you
  // dont need to improves how the refresh rate appears
  if (!finishedAnimating) {
#ifdef double_buffer
    display.fillScreen(tetris.tetrisBLACK);
#else
    mdisplay.clearScreen();
#endif
    //display.fillScreen(tetris.tetrisBLACK);
    if (displayIntro) {
      finishedAnimating = tetris.drawText(1, 21);
    } else {
      if (hour12) {
        // Place holders for checking are any of the tetris objects
        // currently still animating.
        bool tetris1Done = false;
        bool tetris2Done = false;
        bool tetris3Done = false;

        tetris1Done = tetris.drawNumbers(-6, 26, showColon);
        tetris2Done = tetris2.drawText(56, 25);

        // Only draw the top letter once the bottom letter is finished.
        if (tetris2Done) {
          tetris3Done = tetris3.drawText(56, 15);
        }

        finishedAnimating = tetris1Done && tetris2Done && tetris3Done;

      } else {
        finishedAnimating = tetris.drawNumbers(2, 26, showColon);
      }
    }
#ifdef double_buffer
    display.showBuffer();
#endif
  }
#ifndef double_buffer
  portEXIT_CRITICAL_ISR(&timerMux);
#endif
}

void drawIntro(int x = 0, int y = 0)
{
  tetris.drawChar("P", x, y, tetris.tetrisCYAN);
  tetris.drawChar("o", x + 5, y, tetris.tetrisMAGENTA);
  tetris.drawChar("w", x + 11, y, tetris.tetrisYELLOW);
  tetris.drawChar("e", x + 17, y, tetris.tetrisGREEN);
  tetris.drawChar("r", x + 22, y, tetris.tetrisBLUE);
  tetris.drawChar("e", x + 27, y, tetris.tetrisRED);
  tetris.drawChar("d", x + 32, y, tetris.tetrisWHITE);
  tetris.drawChar(" ", x + 37, y, tetris.tetrisMAGENTA);
  tetris.drawChar("b", x + 42, y, tetris.tetrisYELLOW);
  tetris.drawChar("y", x + 47, y, tetris.tetrisGREEN);
}

void drawConnecting(int x = 0, int y = 0)
{
  tetris.drawChar("C", x, y, tetris.tetrisCYAN);
  tetris.drawChar("o", x + 5, y, tetris.tetrisMAGENTA);
  tetris.drawChar("n", x + 11, y, tetris.tetrisYELLOW);
  tetris.drawChar("n", x + 17, y, tetris.tetrisGREEN);
  tetris.drawChar("e", x + 22, y, tetris.tetrisBLUE);
  tetris.drawChar("c", x + 27, y, tetris.tetrisRED);
  tetris.drawChar("t", x + 32, y, tetris.tetrisWHITE);
  tetris.drawChar("i", x + 37, y, tetris.tetrisMAGENTA);
  tetris.drawChar("n", x + 42, y, tetris.tetrisYELLOW);
  tetris.drawChar("g", x + 47, y, tetris.tetrisGREEN);
}

void setMatrixTime() {
  String timeString = "00:00";
  String AmPmString = "";
  if (hour12) {
    // Get the time in format "1:15" or 11:15 (12 hour, no leading 0)
    // Check the EZTime Github page for info on
    // time formatting
    int first = hour()/10;
    int second = hour()%10;
    /* 开启十二小时制显示时间 */
    if(hour() > 12 && hour12 != 0){
      first = (hour()-12)/10;
      second = (hour()-12)%10;
    }else
    /*00点对应12点am, 12点对应12点pm*/
    if(hour() == 0 && hour12 != 0){
      first = 1; second = 2;
    }
    if(hour()!=12)
      timeString = String(second)+ ":" +String(minute()/10)+String(minute()%10);//myTZ.dateTime("g:i");
    else
      timeString = String(first) +String(second)+ ":" +String(minute()/10)+String(minute()%10);//myTZ.dateTime("g:i");

    //If the length is only 4, pad it with
    // a space at the beginning
    if (timeString.length() == 4) {
      timeString = " " + timeString;
    }

    //Get if its "AM" or "PM"
    if(hour()>= 12)
      AmPmString = "PM";//myTZ.dateTime("A");
    else
      AmPmString = "AM";//myTZ.dateTime("A");

    if (lastDisplayedAmPm != AmPmString) {
      Serial.println(AmPmString);
      lastDisplayedAmPm = AmPmString;
      // Second character is always "M"
      // so need to parse it out
      tetris2.setText("M", forceRefresh);

      // Parse out first letter of String
      tetris3.setText(AmPmString.substring(0, 1), forceRefresh);
    }
  } else {
    // Get time in format "01:15" or "22:15"(24 hour with leading 0)
    //timeString = myTZ.dateTime("H:i");
    timeString = String(hour()/10) + String(hour()%10)+":"+String(minute()/10)+String(minute()%10);
  }

  // Only update Time if its different
  if (lastDisplayedTime != timeString) {
    Serial.println(timeString);
    lastDisplayedTime = timeString;
    tetris.setTime(timeString, forceRefresh);

    // Must set this to false so animation knows
    // to start again
    finishedAnimating = false;
  }
}

void handleColonAfterAnimation() {

  // It will draw the colon every time, but when the colour is black it
  // should look like its clearing it.
  uint16_t colour =  showColon ? tetris.tetrisWHITE : tetris.tetrisBLACK;
  // The x position that you draw the tetris animation object
  int x = hour12 ? -6 : 2;
  // The y position adjusted for where the blocks will fall from
  // (this could be better!)
  int y = 26 - (TETRIS_Y_DROP_DEFAULT * tetris.scale);
  tetris.drawColon(x, y, colour);
}

#include "tetris_clock.h"

void tetris_clock::setup() {
  // Start the Animation Timer
  //tetris.setText("GEEKMAGIC");
  animationTimer = timerBegin(1, 80, true);
  timerAttachInterrupt(animationTimer, &animationHandler, false);
  timerAlarmWrite(animationTimer, 10000, true);
  timerAlarmEnable(animationTimer);

  finishedAnimating = false;
  displayIntro = false;
  tetris.scale = 2;
}

void tetris_clock::loop() {
  unsigned long now = millis();
  if (now > oneSecondLoopDue) {
    // We can call this often, but it will only
    // update when it needs to
    setMatrixTime();
    showColon = !showColon;

    // To reduce flicker on the screen we stop clearing the screen
    // when the animation is finished, but we still need the colon to
    // to blink
    if (finishedAnimating) {
      handleColonAfterAnimation();
    }
    oneSecondLoopDue = now + 1000;
  }
}

tetris_clock t_clock;
void init_tetris() {
    t_clock.setup();  
}

void exit_tetris() {
  finishedAnimating = false;
  displayIntro = false;
  tetris.scale = 2;
  lastDisplayedAmPm = "";
  lastDisplayedTime = "";
  timerDetachInterrupt(animationTimer);
  timerEnd(animationTimer);
  animationTimer = nullptr;
}