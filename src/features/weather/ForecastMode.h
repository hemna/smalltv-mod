// ForecastMode.h — 3-day weather forecast matching stock GeekMagic firmware.
//
// Layout: "Forecast HH:MM" header, then 3 rows with day name, weather icon,
// high temp, date, and low/high range.
#pragma once
#include "Mode.h"
#include "config.h"

class ForecastMode : public DisplayMode {
 public:
  const char* id() const override { return "forecast"; }
  uint8_t     modeConst() const override { return MODE_FORECAST; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needRender_ = true; }

 private:
  uint32_t lastRenderedOk_ = 0xFFFFFFFF;
  uint8_t  lastMin_ = 0xFF;
  bool     needRender_ = true;
};

extern ForecastMode g_forecastMode;
