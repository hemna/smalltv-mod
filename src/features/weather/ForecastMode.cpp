#include "ForecastMode.h"
#include <Arduino_GFX_Library.h>
#include <math.h>
#include "Gfx.h"
#include "WeatherClient.h"
#include "Clock.h"
#include "WeatherIconDraw.h"

ForecastMode g_forecastMode;

// ---- Stock firmware "Forecast" screen layout --------------------------------
// Exact replica:
//   Header: "Forecast HH:MM" (white, right-aligned at top)
//   3 rows, each ~70px tall, separated by thin lines:
//     Line 1: Day name (green, large) + cloud/sun icon (center) + High° (white, right)
//     Line 2: DD/Mon (yellow, smaller) + small icon + "Lo / Hi°" (white, right)
// Black background, 240×240.

// Colors
#define C_FC_BG       0x0000   // black
#define C_FC_HDR      0xFFE0   // yellow — header text (matches stock)
#define C_FC_DAY      0x07E0   // green — day name
#define C_FC_DATE     0xFE00   // yellow — date (DD/Mon)
#define C_FC_TEMP     0xFFFF   // white — temperature values
#define C_FC_LINE     0x07FF   // cyan — row borders
#define C_FC_ICON_SUN 0xFE00   // yellow — sun
#define C_FC_ICON_CLD 0xBDF7   // light gray — cloud
#define C_FC_ICON_RN  0x3B8F   // teal — rain cloud

// Months for date display
static const char* MONTHS[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};

static void drawForecastScreen(const WeatherData& w, const Settings& s) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(C_FC_BG);

  // Reset to bitmap font (clear any U8g2 font left from weather clock)
  gfx->setFont((const GFXfont*)NULL);

  // ---- Header: "Forecast HH:MM" (right-aligned, top) ----
  struct tm t;
  bool timeOk = clockNow(t);
  char hdrBuf[20];
  if (timeOk) {
    bool use24h = s.weather.show24h;
    int hour = use24h ? t.tm_hour : (t.tm_hour % 12 ? t.tm_hour % 12 : 12);
    snprintf(hdrBuf, sizeof(hdrBuf), "Forecast %d:%02d", hour, t.tm_min);
  } else {
    snprintf(hdrBuf, sizeof(hdrBuf), "Forecast");
  }
  gfx->setTextSize(2);
  gfx->setTextColor(C_FC_HDR);
  int hdrW = (int)strlen(hdrBuf) * 12;
  gfx->setCursor((TFT_WIDTH - hdrW) / 2, 4);
  gfx->print(hdrBuf);

  // ---- 3 forecast rows ----
  // Each row is ~68px tall, starting at y=28
  int rowH = 68;
  int startY = 28;

  for (int i = 0; i < w.forecastCount && i < 3; i++) {
    const ForecastDay& fd = w.forecast[i];
    int ry = startY + i * rowH;

    // Row border (rounded rectangle outline) — 4px gap between rows
    gfx->drawRoundRect(2, ry, 236, rowH - 4, 4, C_FC_LINE);

    // ---- Top line of row: Day name (green, large) + icon + High temp ----
    // Day name (size 2, green, left) — with padding from border
    gfx->setTextSize(2);
    gfx->setTextColor(C_FC_DAY);
    gfx->setCursor(10, ry + 6);
    gfx->print(fd.dow);

    // Weather icon — real stock 48x48 RGB565 glyph (same as the weather clock),
    // centered in the row. Icon center at x=110 (spans 86-134).
    drawWeatherIconAt(gfx, 110, ry + (rowH - 2) / 2, fd.conditionId);

    // High temp (white, right side)
    char highBuf[8];
    snprintf(highBuf, sizeof(highBuf), "%d", (int)lroundf(fd.tempMax));
    gfx->setTextSize(2);
    gfx->setTextColor(C_FC_TEMP);
    int highW = (int)strlen(highBuf) * 12;
    gfx->setCursor(TFT_WIDTH - highW - 14, ry + 6);
    gfx->print(highBuf);
    // Degree symbol
    int dx = gfx->getCursorX();
    gfx->drawCircle(dx + 2, ry + 3, 2, C_FC_TEMP);

    // ---- Bottom line of row: Date (yellow) + Lo/Hi range (white) ----
    // Date: DD/Mon (yellow, size 1 or small size 2)
    // We need to derive the actual date from the forecast data
    // The forecast day index tells us how many days ahead
    char dateBuf[10];
    if (timeOk) {
      // Calculate date for this forecast day
      time_t now = mktime(&t);
      time_t dayT = now + (time_t)(i + 1) * 86400;
      struct tm dt;
      localtime_r(&dayT, &dt);
      snprintf(dateBuf, sizeof(dateBuf), "%d/%s", dt.tm_mday, MONTHS[dt.tm_mon]);
    } else {
      snprintf(dateBuf, sizeof(dateBuf), "%s", fd.dow);
    }
    gfx->setTextSize(2);
    gfx->setTextColor(C_FC_DATE);
    gfx->setCursor(10, ry + 40);
    gfx->print(dateBuf);

    // Lo / Hi range (white, right side — ensure no icon overlap)
    char rangeBuf[16];
    snprintf(rangeBuf, sizeof(rangeBuf), "%d / %d",
             (int)lroundf(fd.tempMin), (int)lroundf(fd.tempMax));
    gfx->setTextSize(2);
    gfx->setTextColor(C_FC_TEMP);
    int rangeW = (int)strlen(rangeBuf) * 12;
    int rangeX = TFT_WIDTH - rangeW - 14;
    if (rangeX < 140) rangeX = 140;  // don't overlap icon
    gfx->setCursor(rangeX, ry + 40);
    gfx->print(rangeBuf);
    // Degree symbol after range
    dx = gfx->getCursorX();
    gfx->drawCircle(dx + 2, ry + 37, 2, C_FC_TEMP);
  }
}

// ---- DisplayMode interface --------------------------------------------------
void ForecastMode::begin(const Settings& s) {
  weatherInit(s);
  if (!clockSynced()) clockBegin(s);
  lastRenderedOk_ = 0xFFFFFFFF;
  lastMin_ = 0xFF;
  needRender_ = true;
}

void ForecastMode::invalidate(const Settings& s) {
  needRender_ = true;
  lastRenderedOk_ = 0xFFFFFFFF;
  weatherInit(s);
  weatherForceRefresh();
}

void ForecastMode::service(const Settings& s) {
  weatherService(s);
  const WeatherData& w = weatherGet();

  if (!w.valid) {
    if (needRender_) {
      if (w.error && (s.weather.apiKey.length() < 8 || s.weather.city.length() < 2))
        gfxMessage("Forecast", "Set API key + city\nin web UI", C_YELLOW);
      else
        gfxMessage("Forecast", "Fetching...", C_WHITE);
      needRender_ = false;
    }
    return;
  }

  // Redraw every minute (time in header changes)
  struct tm t;
  bool timeOk = clockNow(t);
  uint8_t curMin = timeOk ? t.tm_min : 0xFF;

  if (w.lastOkMs != lastRenderedOk_ || curMin != lastMin_ || needRender_) {
    lastRenderedOk_ = w.lastOkMs;
    lastMin_ = curMin;
    needRender_ = false;
    drawForecastScreen(w, s);
  }
}
