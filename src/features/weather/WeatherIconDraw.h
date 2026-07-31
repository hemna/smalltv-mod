// WeatherIconDraw.h — shared accessor for the real stock RGB565 weather icons.
// The wicon_* PROGMEM arrays + lookup live in WeatherMode.cpp (single TU, so the
// ~41KB of icon data is not duplicated). Both the weather clock and the forecast
// screen call drawWeatherIconAt() to blit the same glyphs. Blit path is the
// flash-safe memcpy_P -> RAM -> draw16bitRGBBitmap (see WeatherMode.cpp for why).
#pragma once

class Arduino_GFX;

// Draw the stock 48x48 weather icon for an OWM condition id, centered at (cx, cy).
void drawWeatherIconAt(Arduino_GFX* gfx, int cx, int cy, int condId);
