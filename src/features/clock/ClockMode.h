// ClockMode.h — Digital clock face matching the stock GeekMagic firmware.
//
// Replicates the official "simple time" theme: huge HH:MM (hours white,
// minutes yellow), green day-of-week badge, YYYY/MM/DD date.
#pragma once
#include "Mode.h"
#include "config.h"

class ClockMode : public DisplayMode {
 public:
  const char* id() const override { return "clock"; }
  uint8_t     modeConst() const override { return MODE_CLOCK; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needRender_ = true; }

 private:
  uint8_t lastMin_ = 0xFF;
  bool    needRender_ = true;
};

extern ClockMode g_clockMode;
