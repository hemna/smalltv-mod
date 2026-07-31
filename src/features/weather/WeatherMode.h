// WeatherMode.h — "Simple Weather Clock" matching the stock GeekMagic firmware.
//
// Layout: City + country badge + icon circle | Max temp | Large time + condition
// badge + seconds | Date + day | Temp bar | Humidity bar
#pragma once
#include "Mode.h"
#include "config.h"

class WeatherMode : public DisplayMode {
 public:
  const char* id() const override { return "weather"; }
  uint8_t     modeConst() const override { return MODE_WEATHER; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needRender_ = true; }

 private:
  uint32_t lastRenderedOk_ = 0xFFFFFFFF;
  uint32_t lastAstroMs_ = 0;
  uint8_t  lastSec_ = 0xFF;
  bool     needRender_ = true;
};

extern WeatherMode g_weatherMode;
