// WeatherData.h — shared data structure for weather info
#pragma once
#include <Arduino.h>

struct ForecastDay {
  float tempMin, tempMax;
  int   conditionId;     // OWM condition code (for icon selection)
  char  dow[4];          // "Mon", "Tue" etc.
};

struct WeatherData {
  bool  valid = false;
  bool  error = false;

  // Current conditions
  float temp;
  float feelsLike;
  float pressure;       // atmospheric pressure hPa
  int   humidity;
  float windSpeed;
  int   conditionId;     // OWM weather condition ID
  char  description[24]; // "clear sky", "light rain" etc.
  char  condMain[16];    // OWM "main" field: "Clouds", "Clear", "Rain"
  char  city[MAX_CITY_LEN];

  // Sunrise/sunset (local hour:min)
  uint8_t sunriseH, sunriseM;
  uint8_t sunsetH, sunsetM;

  // 3-day forecast
  ForecastDay forecast[WEATHER_FORECAST_DAYS];
  uint8_t forecastCount;

  uint32_t lastOkMs;     // millis() of last successful fetch
};
