#include "WeatherMode.h"
#include <Arduino_GFX_Library.h>
#include <ESP8266WiFi.h>
#include <math.h>
#include "Gfx.h"
#include "WeatherClient.h"
#include "Clock.h"
#include "AstroSprite.h"
#include "WeatherIcons.h"
#include "ClockFont.h"
#include "RobotoFonts.h"

WeatherMode g_weatherMode;

// ---- Stock firmware "Simple Weather Clock" — matched from device photo ------
// Layout (240×240, black bg):
//   y=0:   City name (WHITE, bold, size 3, LEFT-aligned, x=4)
//   y=30:  Rotating subtitle (WHITE, size 2, LEFT-aligned, x=4)
//   y=20:  Weather icon (LARGE ~40px cloud/sun, top-RIGHT, cx=190, cy=35)
//   y=55:  HH:MM (YELLOW, size 7, LEFT-aligned x=4) + [Condition] badge (right) + Seconds (size 4)
//   y=135: Date M/DD/YYYY (GREEN, size 2, left) + Day (YELLOW, size 2)
//   y=165: Thermometer icon + bar + temp°F/C (WHITE)
//   y=195: Waterdrop icon + bar + humidity% (WHITE)
//   Bottom-right: Large animated astronaut (~x=190, y=175)

// Colors (matched from stock firmware device photo)
#define C_BG         0x0000   // black
#define C_CITY       0xFFE0   // YELLOW — city name
#define C_SUBTITLE   0xFFFF   // WHITE — rotating subtitle
#define C_TIME_HR    0xFFFF   // WHITE — hours digits
#define C_TIME_MIN   0xFFE0   // YELLOW — minutes digits
#define C_TIME_COLON 0xFFFF   // WHITE — colon
#define C_SECONDS    0xFFFF   // WHITE — seconds digits
#define C_COND_BG    0xDEFB   // light gray — condition badge bg (matches stock)
#define C_COND_FG    0x001F   // BLUE — condition badge outline
#define C_COND_TXT   0x0000   // BLACK — badge text (high contrast)
#define C_DATE       0xFFFF   // WHITE — date (matches stock)
#define C_DAY        0x07E0   // GREEN — day of week (matches stock)
#define C_TEMP_TXT   0xFFE0   // YELLOW — temperature value
#define C_HUMID_TXT  0xFFFF   // WHITE — humidity value
#define C_TEMP_BAR   0x07E0   // GREEN — temp bar fill (matches stock)
#define C_HUMID_BAR  0x04FF   // bright blue — humidity bar fill
#define C_BAR_BG     0x4208   // dark gray — bar outline
#define C_ICON_SUN   0xFFE0   // yellow
#define C_ICON_CLOUD 0xFFFF   // white cloud
#define C_ICON_RAIN  0x3B8F   // teal rain
#define C_THERM_RED  0xF800   // red bulb
#define C_DROP_CYAN  0x07FF   // cyan/teal droplet

// ---- Weather icon (real stock 48x48 RGB565 glyph, top-right) ----
// Maps OWM condition ID -> one of the 9 stock icon-code bitmaps (day set).
// Blit path is draw16bitRGBBitmap (no setFont/GFXfont — same safe path as the astronaut).
// cx,cy is the CENTER of the icon slot; the 48x48 bitmap is drawn top-left = (cx-24, cy-24).
static const uint16_t* weatherIconBmp(int condId) {
  if (condId >= 200 && condId < 300) return wicon_11d;  // thunderstorm
  if (condId >= 300 && condId < 400) return wicon_09d;  // drizzle -> shower
  if (condId >= 500 && condId < 505) return wicon_10d;  // rain
  if (condId == 511)                 return wicon_13d;  // freezing rain -> snow
  if (condId >= 520 && condId < 600) return wicon_09d;  // shower rain
  if (condId >= 600 && condId < 700) return wicon_13d;  // snow
  if (condId >= 700 && condId < 800) return wicon_50d;  // atmosphere (mist/fog/haze)
  if (condId == 800)                 return wicon_01d;  // clear sky
  if (condId == 801)                 return wicon_02d;  // few clouds
  if (condId == 802)                 return wicon_03d;  // scattered clouds
  if (condId >= 803)                 return wicon_04d;  // broken/overcast clouds
  return wicon_01d;                                     // default clear
}

// Shared with ForecastMode via WeatherIconDraw.h (non-static: one definition here,
// so the ~41KB of wicon_* PROGMEM data is not duplicated across translation units).
void drawWeatherIconAt(Arduino_GFX* gfx, int cx, int cy, int condId) {
  const uint16_t* bmp = weatherIconBmp(condId);
  // Block-copy from PROGMEM to RAM (flash-safe on ESP8266).
  static uint16_t iconBuf[WICON_W * WICON_H];
  memcpy_P(iconBuf, bmp, sizeof(iconBuf));
  // Scale 48x48 → 56x56 via nearest-neighbor for larger display
  static const int SCALED = 56;
  static uint16_t scaledBuf[56 * 56];
  for (int dy = 0; dy < SCALED; dy++) {
    int sy = dy * WICON_H / SCALED;
    for (int dx = 0; dx < SCALED; dx++) {
      int sx = dx * WICON_W / SCALED;
      scaledBuf[dy * SCALED + dx] = iconBuf[sy * WICON_W + sx];
    }
  }
  gfx->draw16bitRGBBitmap(cx - SCALED / 2, cy - SCALED / 2,
                          scaledBuf, SCALED, SCALED);
}

// ---- Thermometer icon (size ~12x22) ----
static void drawThermometer(Arduino_GFX* gfx, int x, int y) {
  gfx->drawRect(x + 4, y, 5, 15, 0xFFFF);  // white stem
  gfx->fillCircle(x + 6, y + 18, 5, C_THERM_RED);
  gfx->fillRect(x + 5, y + 7, 3, 9, C_THERM_RED);
}

// ---- Water droplet icon (size ~12x20) ----
static void drawWaterDrop(Arduino_GFX* gfx, int x, int y) {
  gfx->fillCircle(x + 6, y + 13, 6, C_DROP_CYAN);
  gfx->fillTriangle(x + 6, y, x + 1, y + 11, x + 11, y + 11, C_DROP_CYAN);
}

// ---- Progress bar ----
static void drawBar(Arduino_GFX* gfx, int x, int y, int w, int h,
                    float value, float maxVal, uint16_t color) {
  // Rounded end bar (capsule shape)
  int r = h / 2;
  gfx->drawRoundRect(x, y, w, h, r, C_BAR_BG);
  int fillW = (int)((value / maxVal) * (float)(w - 4));
  if (fillW < 2) fillW = 2;
  if (fillW > w - 4) fillW = w - 4;
  int fillH = h - 4;
  int innerR = fillH / 2;  // Clamp radius to half the inner height
  gfx->fillRoundRect(x + 2, y + 2, fillW, fillH, innerR, color);
}

// Temperature-relative color: cold=blue, mild=green, warm=yellow, hot=red
static uint16_t tempBarColor(float tempF) {
  if (tempF < 32) return 0x001F;       // blue (freezing)
  if (tempF < 50) return 0x07FF;       // cyan (cold)
  if (tempF < 75) return 0x07E0;       // green (cool)
  if (tempF < 90) return 0xFFE0;       // yellow (warm)
  if (tempF < 100) return 0xFD20;      // orange (hot)
  return 0xF800;                        // red (very hot)
}

// ---- Animated astronaut (bitmap-based, 4 frames) ----
static uint8_t s_astroFrame = 0;

static void drawAstronaut(Arduino_GFX* gfx, int x, int y, uint8_t frame) {
  // Select frame directly (avoid pgm_read_ptr alignment issues on ESP8266)
  const uint8_t* bmp;
  switch (frame % ASTRO_FRAMES) {
    case 0: bmp = astro_frame0; break;
    case 1: bmp = astro_frame1; break;
    case 2: bmp = astro_frame2; break;
    case 3: bmp = astro_frame3; break;
    case 4: bmp = astro_frame4; break;
    case 5: bmp = astro_frame5; break;
    case 6: bmp = astro_frame6; break;
    case 7: bmp = astro_frame7; break;
    case 8: bmp = astro_frame8; break;
    case 9: bmp = astro_frame9; break;
    case 10: bmp = astro_frame10; break;
    case 11: bmp = astro_frame11; break;
    case 12: bmp = astro_frame12; break;
    case 13: bmp = astro_frame13; break;
    case 14: bmp = astro_frame14; break;
    case 15: bmp = astro_frame15; break;
    case 16: bmp = astro_frame16; break;
    case 17: bmp = astro_frame17; break;
    case 18: bmp = astro_frame18; break;
    default: bmp = astro_frame19; break;
  }
  gfx->drawBitmap(x, y, bmp, ASTRO_W, ASTRO_H, 0xBDF7);  // light gray
}

// ---- Short condition label from OWM ID (matches stock firmware) ----
static const char* conditionLabel(int id) {
  if (id >= 200 && id < 300) return "Storm";
  if (id >= 300 && id < 400) return "Drizzle";
  if (id >= 500 && id < 600) return "Rain";
  if (id >= 600 && id < 700) return "Snow";
  if (id >= 700 && id < 800) return "Mist";
  if (id == 800)             return "Clear";
  if (id > 800)              return "Clouds";
  return "?";
}

// ---- Rotating subtitle ----
static uint8_t s_subtitleIdx = 0;

static void getSubtitle(char* buf, size_t len, const WeatherData& w,
                        const Settings& s, uint8_t idx) {
  const char* unit = s.weather.metric ? "\xB0""C" : "\xB0""F";
  const char* wunit = s.weather.metric ? "m/s" : "mph";

  // Build list of enabled subtitle slots
  // Each slot has an ID matching the switch cases below
  uint8_t enabled[8];
  uint8_t count = 0;
  if (s.weather.subWind)      enabled[count++] = 0;
  if (s.weather.subMinTemp)   enabled[count++] = 1;
  if (s.weather.subMaxTemp)   enabled[count++] = 2;
  if (s.weather.subFeelsLike) enabled[count++] = 3;
  if (s.weather.subPressure)  enabled[count++] = 4;
  if (s.weather.subSunrise)   enabled[count++] = 5;
  if (s.weather.subSunset)    enabled[count++] = 6;
  if (s.weather.subIp)        enabled[count++] = 7;

  if (count == 0) { snprintf(buf, len, " "); return; }

  uint8_t slot = enabled[idx % count];
  switch (slot) {
    case 0:
      snprintf(buf, len, "Wind %d%s", (int)lroundf(w.windSpeed), wunit);
      break;
    case 1:
      if (w.forecastCount > 0)
        snprintf(buf, len, "Min temp %d%s", (int)lroundf(w.forecast[0].tempMin), unit);
      else
        snprintf(buf, len, "Feels like %d%s", (int)lroundf(w.feelsLike), unit);
      break;
    case 2:
      if (w.forecastCount > 0)
        snprintf(buf, len, "Max temp %d%s", (int)lroundf(w.forecast[0].tempMax), unit);
      else
        snprintf(buf, len, "Temp %d%s", (int)lroundf(w.temp), unit);
      break;
    case 3:
      snprintf(buf, len, "Feels like %d%s", (int)lroundf(w.feelsLike), unit);
      break;
    case 4: {
      float p = w.pressure;
      switch (s.weather.pressureUnit) {
        case 1: // inHg
          snprintf(buf, len, "%.2f inHg", p * 0.02953f);
          break;
        case 2: // mmHg
          snprintf(buf, len, "%d mmHg", (int)lroundf(p * 0.75006f));
          break;
        case 3: // atm
          snprintf(buf, len, "%.3f atm", p / 1013.25f);
          break;
        default: // 0 = hPa
          snprintf(buf, len, "%d hPa", (int)lroundf(p));
          break;
      }
      break;
    }
    case 5:
      snprintf(buf, len, "Sunrise %02d:%02d", w.sunriseH, w.sunriseM);
      break;
    case 6:
      snprintf(buf, len, "Sunset %02d:%02d", w.sunsetH, w.sunsetM);
      break;
    case 7: {
      // Show device IP address
      IPAddress ip = WiFi.localIP();
      snprintf(buf, len, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      break;
    }
  }
}

// ---- Main draw ----
static void drawSimpleWeatherClock(const WeatherData& w, const Settings& s) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(C_BG);

  // Layout: two columns
  // Left column: x=0..159 (160px) — city, subtitle, time, date, bars
  // Right column: x=180..239 (60px) — weather icon, badge, seconds, astronaut
  const int LEFT_W = 180;
  const int RIGHT_X = 180;

  // ---- Row 1: City (YELLOW, Roboto Regular 14, centered in LEFT column, y=4) ----
  gfx->setTextSize(1, 1, 0);
  gfx->setFont(u8g2_font_roboto_reg_14_tr);
  gfx->setTextColor(C_CITY, C_BG);
  int cityW = (int)strlen(w.city) * 11;  // roboto_reg_14 ~11px/char avg
  int cityX = (LEFT_W - cityW) / 2;
  if (cityX < 0) cityX = 0;
  gfx->setCursor(cityX, 16);  // baseline at y=16
  gfx->print(w.city);

  // ---- Row 2: Subtitle (WHITE, Roboto Light 14, centered in LEFT column) ----
  char subtitle[32];
  getSubtitle(subtitle, sizeof(subtitle), w, s, s_subtitleIdx);
  gfx->setTextColor(C_SUBTITLE, C_BG);
  gfx->setFont(u8g2_font_roboto_light_14_tr);  // lighter weight = thinner strokes
  int subW = (int)strlen(subtitle) * 10;  // slightly tighter for shorter text
  int subX = (LEFT_W - subW) / 2;
  if (subX < 0) subX = 0;
  gfx->setCursor(subX, 46);  // baseline at y=46 (more gap from title)
  gfx->print(subtitle);

  // ---- RIGHT column: Weather icon (centered, top of right column) ----
  drawWeatherIconAt(gfx, RIGHT_X + 30, 26, w.conditionId);

  // ---- Row 3: Large time HH:MM ----
  // Default bitmap font at size 7, with narrow manually-drawn colon
  struct tm t;
  bool timeOk = clockNow(t);
  if (timeOk) {
    bool use24h = s.weather.show24h;
    int hour = use24h ? t.tm_hour : (t.tm_hour % 12 ? t.tm_hour % 12 : 12);

    char hourBuf[4], minBuf[4];
    snprintf(hourBuf, sizeof(hourBuf), "%d", hour);
    snprintf(minBuf, sizeof(minBuf), "%02d", t.tm_min);

    int timeY = 74;

    // ---- Oswald Bold condensed (71px tall) — fits in left column ----
    gfx->setTextSize(1, 1, 0);
    gfx->setFont(u8g2_font_oswald_bold_tn);

    // Hours (WHITE) — baseline = timeY + ascent (~58 for 71px digit height)
    int baseline = timeY + 58;
    gfx->setTextColor(C_TIME_HR, C_BG);
    gfx->setCursor(2, baseline);
    gfx->print(hourBuf);
    int colonX = gfx->getCursorX();

    // Colon (WHITE) — fub49_tn includes ':' glyph
    gfx->setTextColor(C_TIME_COLON, C_BG);
    gfx->print(":");
    int afterColon = gfx->getCursorX();

    // Minutes (YELLOW)
    gfx->setTextColor(C_TIME_MIN, C_BG);
    gfx->setCursor(afterColon, baseline);
    gfx->print(minBuf);
    int timeEndX = gfx->getCursorX();

    // ---- Reset back to bitmap font for everything else ----
    gfx->setFont((const GFXfont*)NULL);
    gfx->setTextSize(1);

    // ---- RIGHT column: Condition badge (aligned with time) ----
    const char* condLabel = conditionLabel(w.conditionId);
    // Use built-in GFX bitmap font at size 2 for maximum readability
    gfx->setFont((const GFXfont*)NULL);
    gfx->setTextSize(2);
    int condW = (int)strlen(condLabel) * 12 + 8;  // GFX size2: 12px/char + padding
    if (condW < 44) condW = 44;
    // Right-align badge to screen edge (240px) with 2px margin
    int badgeX = 240 - condW - 2;
    int badgeY = timeY + 2;
    gfx->fillRoundRect(badgeX, badgeY, condW, 22, 4, C_COND_BG);
    gfx->drawRoundRect(badgeX, badgeY, condW, 22, 4, C_COND_FG);  // blue outline
    gfx->setTextColor(C_COND_TXT, C_COND_BG);
    // Center text within badge
    int textW = (int)strlen(condLabel) * 12;
    int textX = badgeX + (condW - textW) / 2;
    gfx->setCursor(textX, badgeY + 3);  // GFX top-left baseline (3px top padding)
    gfx->print(condLabel);

    // ---- RIGHT column: Seconds (WHITE, oswald, below badge) ----
    char secBuf[4];
    snprintf(secBuf, sizeof(secBuf), "%02d", t.tm_sec);
    gfx->setTextSize(1, 1, 0);
    gfx->setFont(u8g2_font_oswald_bold_sec_tn);
    gfx->setTextColor(C_SECONDS, C_BG);
    int secW = 2 * 19;  // oswald sec digits ~19px wide each
    int secX = RIGHT_X + (60 - 40) / 2;  // center based on max "00" width (40px)
    gfx->setCursor(secX, badgeY + 22 + 4 + 37);  // baseline = badge_bottom(22) + gap(4) + ascent(37)
    gfx->print(secBuf);

    // ---- Row 4: Date (YELLOW, Roboto 16) + Day (YELLOW) — centered in LEFT column ----
    char dateBuf[12];
    snprintf(dateBuf, sizeof(dateBuf), "%d/%d/%04d",
             t.tm_mon + 1, t.tm_mday, t.tm_year + 1900);
    gfx->setTextSize(1, 1, 0);
    gfx->setFont(u8g2_font_roboto_reg_16_tr);
    gfx->setTextColor(C_DATE, C_BG);
    // Measure full "M/DD/YYYY Wed" width for centering
    static const char* DAYS[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    int datePixels = (int)strlen(dateBuf) * 12;  // roboto_reg_16 ~12px/char
    int dayPixels = 3 * 12 + 6;  // "Wed" + gap
    int dateRowW = datePixels + dayPixels;
    int dateStartX = (LEFT_W - dateRowW) / 2;
    if (dateStartX < 2) dateStartX = 2;
    gfx->setCursor(dateStartX, 155 + 16);  // baseline (roboto_reg_16 ascent ~16)
    gfx->print(dateBuf);

    // Day of week in YELLOW
    int dayX = gfx->getCursorX() + 6;
    gfx->setTextColor(C_DAY, C_BG);
    gfx->setCursor(dayX, 155 + 16);
    gfx->print(DAYS[t.tm_wday]);
  }

  // ---- Row 5: Thermometer + bar + temp (centered in LEFT column) ----
  const int BAR_GROUP_X = (LEFT_W - 146) / 2;  // 146px = icon+gap+bar+gap+text
  drawThermometer(gfx, BAR_GROUP_X, 190);
  float tempForBar = w.temp;
  float barMin, barMax;
  if (s.weather.metric) { barMin = -40; barMax = 50; }   // °C range
  else                  { barMin = -20; barMax = 120; }   // °F range
  if (tempForBar < barMin) tempForBar = barMin;
  if (tempForBar > barMax) tempForBar = barMax;
  // Convert to F for color if metric
  float tempF = s.weather.metric ? (w.temp * 9.0f / 5.0f + 32.0f) : w.temp;
  drawBar(gfx, BAR_GROUP_X + 18, 195, 70, 12, tempForBar - barMin, barMax - barMin, tempBarColor(tempF));
  gfx->setTextSize(1, 1, 0);
  gfx->setFont(u8g2_font_roboto_reg_14_tr);
  gfx->setTextColor(C_TEMP_TXT, C_BG);
  gfx->setCursor(BAR_GROUP_X + 92, 192 + 13);  // baseline
  gfx->print((int)lroundf(w.temp));
  int tdx = gfx->getCursorX();
  gfx->drawCircle(tdx + 2, 193, 2, C_TEMP_TXT);
  gfx->setCursor(tdx + 7, 192 + 13);
  gfx->print(s.weather.metric ? "C" : "F");

  // ---- Row 6: Waterdrop + bar + humidity (centered in LEFT column) ----
  drawWaterDrop(gfx, BAR_GROUP_X, 215);
  drawBar(gfx, BAR_GROUP_X + 18, 220, 70, 12, (float)w.humidity, 100.0f, C_HUMID_BAR);
  gfx->setTextColor(C_HUMID_TXT, C_BG);
  gfx->setCursor(BAR_GROUP_X + 92, 217 + 13);
  char humTxt[8];
  snprintf(humTxt, sizeof(humTxt), "%d%%", w.humidity);
  gfx->print(humTxt);

  // ---- RIGHT column: Astronaut (60x60, bottom of right column) ----
  drawAstronaut(gfx, 180, 170, s_astroFrame);
}

// ---- Partial update: only seconds + astronaut (no full repaint = no flicker) ----
static void drawSecondsAndAstro(const WeatherData& w, const Settings& s, uint8_t frame) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;

  struct tm t;
  if (!clockNow(t)) return;

  // Right column fixed positions (must match drawSimpleWeatherClock)
  const int RIGHT_X = 180;
  int timeY = 74;
  int badgeY = timeY + 2;
  int secY = badgeY + 26;

  // Clear seconds region in right column
  int secW = 2 * 19;  // oswald sec digits ~19px wide
  int secX = RIGHT_X + (60 - 40) / 2;  // center based on max "00" width (40px)
  gfx->fillRect(secX - 2, secY, 44, 42, C_BG);

  // Redraw seconds
  char secBuf[4];
  snprintf(secBuf, sizeof(secBuf), "%02d", t.tm_sec);
  gfx->setTextSize(1, 1, 0);
  gfx->setFont(u8g2_font_oswald_bold_sec_tn);
  gfx->setTextColor(C_SECONDS, C_BG);
  gfx->setCursor(secX, secY + 37);  // baseline = y + ascent(37)
  gfx->print(secBuf);

  // Clear and redraw astronaut
  gfx->fillRect(180, 170, ASTRO_W, ASTRO_H, C_BG);
  drawAstronaut(gfx, 180, 170, frame);
}

// ---- DisplayMode interface --------------------------------------------------
void WeatherMode::begin(const Settings& s) {
  weatherInit(s);
  if (!clockSynced()) clockBegin(s);
  lastRenderedOk_ = 0xFFFFFFFF;
  lastSec_ = 0xFF;
  needRender_ = true;
}

void WeatherMode::invalidate(const Settings& s) {
  needRender_ = true;
  lastRenderedOk_ = 0xFFFFFFFF;
  weatherInit(s);
  weatherForceRefresh();
}

void WeatherMode::service(const Settings& s) {
  weatherService(s);
  const WeatherData& w = weatherGet();

  if (!w.valid) {
    if (needRender_) {
      if (w.error && (s.weather.apiKey.length() < 8 || s.weather.city.length() < 2))
        gfxMessage("Weather", "Set API key + city\nin web UI", C_YELLOW);
      else
        gfxMessage("Weather", "Fetching...", C_WHITE);
      needRender_ = false;
    }
    return;
  }

  struct tm t;
  bool timeOk = clockNow(t);
  uint8_t curSec = timeOk ? t.tm_sec : 0xFF;

  // Full repaint needed on: first draw, weather data update, minute change (time digits),
  // or subtitle rotation (every 5 sec)
  bool subtitleChanged = timeOk && (t.tm_sec % 5 == 0) && curSec != lastSec_;
  bool minuteChanged = timeOk && lastSec_ != 0xFF && (curSec == 0);
  bool weatherChanged = w.lastOkMs != lastRenderedOk_;

  if (needRender_ || weatherChanged || minuteChanged || subtitleChanged) {
    lastRenderedOk_ = w.lastOkMs;
    lastSec_ = curSec;
    needRender_ = false;
    s_astroFrame++;
    if (subtitleChanged) s_subtitleIdx++;
    drawSimpleWeatherClock(w, s);
    lastAstroMs_ = millis();
  } else if (curSec != lastSec_) {
    // Only seconds changed — partial update for seconds (no flicker)
    lastSec_ = curSec;
    Arduino_GFX* gfx = gfxDev();
    if (gfx) {
      const int RIGHT_X = 180;
      int timeY = 74;
      int badgeY = timeY + 2;
      int secY = badgeY + 26;
      int secW = 2 * 19;  // oswald sec digits ~19px wide
      int secX = RIGHT_X + (60 - 40) / 2;  // center based on max "00" width (40px)

      gfx->fillRect(secX - 2, secY, 44, 42, C_BG);
      char secBuf[4];
      snprintf(secBuf, sizeof(secBuf), "%02d", t.tm_sec);
      gfx->setTextSize(1, 1, 0);
      gfx->setFont(u8g2_font_oswald_bold_sec_tn);
      gfx->setTextColor(C_SECONDS, C_BG);
      gfx->setCursor(secX, secY + 37);  // baseline
      gfx->print(secBuf);
    }
  }

  // Astronaut runs on its own fast timer (~4fps), independent of seconds
  uint32_t now = millis();
  if (now - lastAstroMs_ >= 250) {  // 250ms = 4fps (gentler on ESP8266)
    lastAstroMs_ = now;
    s_astroFrame++;
    Arduino_GFX* gfx = gfxDev();
    if (gfx) {
      gfx->fillRect(180, 170, ASTRO_W, ASTRO_H, C_BG);
      drawAstronaut(gfx, 180, 170, s_astroFrame);
    }
  }
}
