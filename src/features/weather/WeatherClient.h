// WeatherClient.h — OpenWeatherMap HTTP client
#pragma once
#include "Settings.h"
#include "WeatherData.h"

void               weatherInit(const Settings& s);
void               weatherService(const Settings& s);
void               weatherForceRefresh();
const WeatherData& weatherGet();
