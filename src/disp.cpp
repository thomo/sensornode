#include "disp.h"
#include <FS.h>
#include <LittleFS.h>

uint16_t read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(fs::File &f) {
  uint32_t result;
  ((uint16_t *)&result)[0] = read16(f); // LSB
  ((uint16_t *)&result)[1] = read16(f); // MSB
  return result;
}

void drawBmp(TFT_eSPI &tft, const char *filename, int16_t x, int16_t y) {

  if ((x >= tft.width()) || (y >= tft.height()))
    return;

  fs::File bmpFS;
  if (LittleFS.begin()) {
    // debug_println("Init LittleFS - successful.");
  } else {
    Serial.println("Error while init LittleFS.");
    return;
  }

  // Open requested file on SD card
  bmpFS = LittleFS.open(filename, "r");

  if (!bmpFS) {
    Serial.printf("File not found: %s\n", filename);
    return;
  }

  uint32_t seekOffset;
  uint16_t w, h, row;
  uint8_t r, g, b;

  if (read16(bmpFS) == 0x4D42) {
    read32(bmpFS); // size
    read32(bmpFS); // reserved
    seekOffset = read32(bmpFS); 
    read32(bmpFS);
    w = read32(bmpFS);
    h = read32(bmpFS);
    uint32_t i;
    if ((i = read16(bmpFS)) != 1) {
      Serial.printf("Wrong number of color planes (exp. 1): %d\n", i);
      Serial.println("BMP format not recognized.");
    } else if ((i = read16(bmpFS)) != 24) {
      Serial.printf("Wrong number bits per pixel (exp. 24): %d\n", i);
      Serial.println("BMP format not recognized.");
    } else if((i = read32(bmpFS)) != 0) {
      Serial.printf("Compression not supported: %d\n", i);
      Serial.println("BMP format not recognized.");

    } else {
      y += h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      bmpFS.seek(seekOffset);

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3 + padding];

      for (row = 0; row < h; row++)
      {

        bmpFS.read(lineBuffer, sizeof(lineBuffer));
        uint8_t *bptr = lineBuffer;
        uint16_t *tptr = (uint16_t *)lineBuffer;
        // Convert 24 to 16 bit colours
        for (uint16_t col = 0; col < w; col++)
        {
          b = *bptr++;
          g = *bptr++;
          r = *bptr++;
          *tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }

        // Push the pixel row to screen, pushImage will crop the line if needed
        // y is decremented as the BMP image is drawn bottom up
        tft.pushImage(x, y--, w, 1, (uint16_t *)lineBuffer);
      }
      tft.setSwapBytes(oldSwapBytes);
      // Serial.print("Loaded in "); Serial.print(millis() - startTime);
      // Serial.println(" ms");
    }
  } else {
    Serial.println("Header dont match 0x4D42");
  }
  bmpFS.close();
  LittleFS.end();
}

void display(TFT_eSPI &tft, const char* inTemp, const char* outTemp, const char* icon, const char* timebuf, const char* datebuf) {
  tft.fillScreen(TFT_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK);

  if (DISP_GRID) {
    int incr = 10;
    for (int i = 0; i < 128; i = i + incr)
    {
      tft.drawLine(0, i, 159, i, TFT_GREEN);
    }
    for (int i = 0; i < 160; i = i + incr)
    {
      tft.drawLine(i, 0, i, 127, TFT_GREEN);
    }

    int d = 38;
    tft.drawRect(10,  4, d, d, TFT_RED);
    tft.drawRect(10, 50, d, d, TFT_RED);

    tft.drawRect(50,  4, 100, d, TFT_RED);
    tft.drawRect(50, 50, 100, d, TFT_RED);

    tft.drawRect(10, 100, 60, 22, TFT_RED);
    tft.drawRect(90, 100, 60, 22, TFT_RED);

  }

  drawBmp(tft, icon, 11, 5);
  drawBmp(tft, "in_d.bmp",  12, 53);

  tft.drawLine(15, 92, 144, 92, TFT_BLUE);
  tft.drawLine(15, 93, 144, 93, TFT_BLUE);

  tft.loadFont(FONT_LARGE);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(outTemp, 150,  7);
  tft.drawString(inTemp,  150, 53);
  tft.unloadFont();

  tft.loadFont(FONT_MIDDLE);
  tft.drawString(timebuf, 70, 98);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(datebuf, 90, 98);
  tft.unloadFont();
}
