#ifndef _disp_h_
#define _disp_h_

#include <Arduino.h>
#include <TFT_eSPI.h> // Hardware-specific library

#include "Landasans36.h"
#include "Landasans48.h"
#define FONT_MIDDLE Landasans36
#define FONT_LARGE Landasans48

#define DISP_GRID 0

void display(TFT_eSPI &tft, const char* inTemp, const char* outTemp, const char* icon, const char* timebuf, const char* datebuf);

#endif