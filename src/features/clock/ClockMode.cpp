#include "ClockMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "Clock.h"

ClockMode g_clockMode;

// ---- Stock-firmware-matching clock display ----------------------------------
// Replicates the official GeekMagic SmallTV Ultra "simple time" theme:
//   - Black background
//   - Huge centered time: hours in white, minutes in yellow/gold
//   - Day-of-week in a small green rounded rectangle badge, centered
//   - Date (YYYY/MM/DD) in white below the badge
// All on a 240×240 px ST7789 display.

// Colors matching the stock firmware
#define C_CLK_BG       0x0000   // black
#define C_CLK_HOUR     0xFFFF   // white (hours digits)
#define C_CLK_MIN      0xFE00   // yellow/gold (minutes digits)
#define C_CLK_COLON    0xFFFF   // white colon
#define C_CLK_DAY_BG   0x07E0   // green badge background
#define C_CLK_DAY_FG   0x0000   // black text on green badge
#define C_CLK_DATE     0xBDF7   // light gray for date text

static const char* DAYS[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

static void drawClockScreen(struct tm& t, bool use24h) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->setFont((const uint8_t*)NULL);  // Reset u8g2 font (WeatherMode may leave one active)
  gfx->fillScreen(C_CLK_BG);

  // --- Time display (huge, centered vertically around y=70..130) ---
  // Stock firmware: hours ~size 7, minutes ~size 7, total width ~200px
  // We draw hours and minutes separately so they can have different colors.
  int hour = use24h ? t.tm_hour : (t.tm_hour % 12 ? t.tm_hour % 12 : 12);

  char hourBuf[4], minBuf[4];
  snprintf(hourBuf, sizeof(hourBuf), "%d", hour);
  snprintf(minBuf, sizeof(minBuf), "%02d", t.tm_min);

  // Calculate total width:  hourW + colonW + minW
  // At size 7, each char is 42px wide (6*7), colon is also 42px
  const uint8_t sz = 7;
  int charW = 6 * sz;  // 42
  int hourW = (int)strlen(hourBuf) * charW;
  int colonW = charW;
  int minW = 2 * charW;
  int totalW = hourW + colonW + minW;
  int startX = (TFT_WIDTH - totalW) / 2;
  int timeY = 60;  // vertical position of time

  // Draw hour digits (white)
  gfx->setTextSize(sz);
  gfx->setTextColor(C_CLK_HOUR);
  gfx->setCursor(startX, timeY);
  gfx->print(hourBuf);

  // Draw colon (white)
  gfx->setTextColor(C_CLK_COLON);
  gfx->setCursor(startX + hourW, timeY);
  gfx->print(":");

  // Draw minute digits (yellow/gold)
  gfx->setTextColor(C_CLK_MIN);
  gfx->setCursor(startX + hourW + colonW, timeY);
  gfx->print(minBuf);

  // --- Day of week badge (green rounded rect with black text) ---
  // Centered below the time
  const char* dayStr = DAYS[t.tm_wday];
  int badgeW = (int)strlen(dayStr) * 6 * 2 + 12;  // text width at size 2 + padding
  int badgeH = 20;
  int badgeX = (TFT_WIDTH - badgeW) / 2;
  int badgeY = 150;

  gfx->fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, C_CLK_DAY_BG);
  gfx->setTextSize(2);
  gfx->setTextColor(C_CLK_DAY_FG);
  int textX = badgeX + 6;
  int textY = badgeY + 3;
  gfx->setCursor(textX, textY);
  gfx->print(dayStr);

  // --- Date (YYYY/MM/DD in white, centered below badge) ---
  char dateBuf[12];
  snprintf(dateBuf, sizeof(dateBuf), "%04d/%02d/%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  int dateW = (int)strlen(dateBuf) * 6 * 2;
  gfx->setTextSize(2);
  gfx->setTextColor(C_CLK_DATE);
  gfx->setCursor((TFT_WIDTH - dateW) / 2, 178);
  gfx->print(dateBuf);
}

// ---- DisplayMode interface --------------------------------------------------
void ClockMode::begin(const Settings& s) {
  if (!clockSynced()) clockBegin(s);
  lastMin_ = 0xFF;
  needRender_ = true;
}

void ClockMode::invalidate(const Settings& s) {
  needRender_ = true;
  lastMin_ = 0xFF;
}

void ClockMode::service(const Settings& s) {
  struct tm t;
  if (!clockNow(t)) {
    if (needRender_) {
      gfxMessage("Clock", "Waiting for NTP...", C_WHITE);
      needRender_ = false;
    }
    return;
  }

  // Only redraw when minute changes (saves CPU)
  if (t.tm_min != lastMin_ || needRender_) {
    lastMin_ = t.tm_min;
    needRender_ = false;
    drawClockScreen(t, s.weather.show24h);
  }
}
